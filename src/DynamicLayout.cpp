#include "DynamicLayout.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <limits>
#include <unordered_set>

namespace zsdyn {
namespace {

// Marks the new tile during a split until the dragged window's old tile has
// been removed. Never valid in a committed tree.
constexpr WindowId kPlaceholder = std::numeric_limits<WindowId>::max();

using WindowFilter = std::function<std::vector<WindowId>(const std::vector<WindowId>&)>;

int IndexOf(const Tree& tree, int id) {
    for (int i = 0; i < static_cast<int>(tree.nodes.size()); ++i)
        if (tree.nodes[i].id == id) return i;
    return -1;
}

int LeafIndexOf(const Tree& tree, WindowId window) {
    for (int i = 0; i < static_cast<int>(tree.nodes.size()); ++i) {
        const auto& n = tree.nodes[i];
        if (n.IsLeaf() && std::find(n.windows.begin(), n.windows.end(), window) != n.windows.end())
            return i;
    }
    return -1;
}

// Copies the subtree at `index` into dst, filtering every leaf's windows.
// Empty leaves disappear and their sibling takes the parent's place. With
// mapRegion the surviving sibling records the collapsed parent's region.
int Rebuild(const Tree& src, int index, Tree& dst, const WindowFilter& filter,
            bool mapRegion, int depth) {
    if (index < 0 || index >= static_cast<int>(src.nodes.size()) ||
        depth > static_cast<int>(src.nodes.size()))
        return -1;
    const Node& n = src.nodes[index];
    if (n.IsLeaf()) {
        auto windows = filter(n.windows);
        if (windows.empty()) return -1;
        Node copy = n;
        copy.windows = std::move(windows);
        dst.nodes.push_back(std::move(copy));
        return static_cast<int>(dst.nodes.size()) - 1;
    }
    const int a = Rebuild(src, n.first, dst, filter, mapRegion, depth + 1);
    const int b = Rebuild(src, n.second, dst, filter, mapRegion, depth + 1);
    if (a < 0 && b < 0) return -1;
    if (a < 0 || b < 0) {
        const int survivor = a < 0 ? b : a;
        if (mapRegion) dst.nodes[survivor].region = n.region;
        return survivor;
    }
    Node copy = n;
    copy.windows.clear();
    copy.first = a;
    copy.second = b;
    dst.nodes.push_back(std::move(copy));
    return static_cast<int>(dst.nodes.size()) - 1;
}

Tree Filtered(const Tree& tree, const WindowFilter& filter, bool mapRegion) {
    Tree out;
    out.nextId = tree.nextId;
    out.root = Rebuild(tree, tree.root, out, filter, mapRegion, 0);
    if (out.root < 0) out.nodes.clear();
    return out;
}

Tree Compact(const Tree& tree) {
    return Filtered(tree, [](const std::vector<WindowId>& w) { return w; }, false);
}

bool SameSubtree(const Tree& a, int ia, const Tree& b, int ib, int depth) {
    if (ia < 0 || ib < 0) return ia == ib;
    if (ia >= static_cast<int>(a.nodes.size()) || ib >= static_cast<int>(b.nodes.size()) ||
        depth > static_cast<int>(a.nodes.size()))
        return false;
    const Node& x = a.nodes[ia];
    const Node& y = b.nodes[ib];
    if (x.id != y.id || x.region != y.region || x.IsLeaf() != y.IsLeaf()) return false;
    if (x.IsLeaf()) return x.windows == y.windows;
    return x.vertical == y.vertical && x.ratio == y.ratio &&
           SameSubtree(a, x.first, b, y.first, depth + 1) &&
           SameSubtree(a, x.second, b, y.second, depth + 1);
}

long EdgeOf(const Rect& r, Side side) {
    switch (side) {
    case Side::Left: return r.left;
    case Side::Top: return r.top;
    case Side::Right: return r.right;
    case Side::Bottom: return r.bottom;
    }
    return 0;
}

std::optional<Tree> Checked(Tree tree) {
    if (!Validate(tree).empty()) return std::nullopt;
    return tree;
}

std::optional<Tree> SplitRegion(const Tree& full, int regionId, Side side, WindowId dragged) {
    Tree t = full;
    const int index = IndexOf(t, regionId);
    if (index < 0) return std::nullopt;

    // The region keeps its id (and its whole subtree) at a new index; a new
    // split node takes its old index so the parent link stays valid.
    Node moved = t.nodes[index];
    t.nodes.push_back(moved);
    const int movedIndex = static_cast<int>(t.nodes.size()) - 1;

    Node leaf;
    leaf.id = leaf.region = t.nextId++;
    leaf.windows = {kPlaceholder};
    t.nodes.push_back(leaf);
    const int leafIndex = static_cast<int>(t.nodes.size()) - 1;

    const bool before = side == Side::Left || side == Side::Top;
    Node split;
    split.id = split.region = t.nextId++;
    split.vertical = side == Side::Left || side == Side::Right;
    split.first = before ? leafIndex : movedIndex;
    split.second = before ? movedIndex : leafIndex;
    split.ratio = before ? kSplitRatio : 1.0 - kSplitRatio;
    t.nodes[index] = split;

    // Remove the dragged window's previous tile (if any), then let the new
    // tile adopt it. Doing it in this order keeps regionId addressable even
    // when removing the old tile collapses one of its ancestors.
    t = RemoveWindow(t, dragged);
    const int placeholder = LeafIndexOf(t, kPlaceholder);
    if (placeholder < 0) return std::nullopt;
    t.nodes[placeholder].windows = {dragged};
    return Checked(std::move(t));
}

}  // namespace

bool Tree::operator==(const Tree& other) const {
    return SameSubtree(*this, root, other, other.root, 0);
}

std::string Validate(const Tree& tree) {
    const int count = static_cast<int>(tree.nodes.size());
    if (tree.root < 0) return count == 0 ? std::string{} : "nodes without a root";
    if (tree.root >= count) return "root out of range";

    std::vector<char> seen(count, 0);
    std::unordered_set<int> ids;
    std::unordered_set<WindowId> windows;
    std::vector<int> pending{tree.root};
    int visited = 0;
    while (!pending.empty()) {
        const int index = pending.back();
        pending.pop_back();
        if (index < 0 || index >= count) return "child index out of range";
        if (seen[index]) return "node reachable twice";
        seen[index] = 1;
        ++visited;
        const Node& n = tree.nodes[index];
        if (n.id <= 0 || n.id >= tree.nextId) return "invalid node id";
        if (!ids.insert(n.id).second) return "duplicate node id";
        if (n.IsLeaf()) {
            if (n.second >= 0) return "leaf with a second child";
            if (n.windows.empty()) return "empty tile";
            for (WindowId w : n.windows) {
                if (w == 0 || w == kPlaceholder) return "invalid window";
                if (!windows.insert(w).second) return "window appears twice";
            }
        } else {
            if (n.second < 0) return "split with one child";
            if (!n.windows.empty()) return "split holding windows";
            if (!std::isfinite(n.ratio) || n.ratio <= 0.0 || n.ratio >= 1.0)
                return "invalid ratio";
            pending.push_back(n.first);
            pending.push_back(n.second);
        }
    }
    if (visited != count) return "unreachable nodes";
    return {};
}

Tree MakeTree(WindowId window) {
    Tree t;
    Node leaf;
    leaf.id = leaf.region = t.nextId++;
    leaf.windows = {window};
    t.nodes.push_back(leaf);
    t.root = 0;
    return t;
}

bool Contains(const Tree& tree, WindowId window) {
    const auto all = Windows(tree);
    return std::find(all.begin(), all.end(), window) != all.end();
}

std::vector<WindowId> Windows(const Tree& tree) {
    std::vector<WindowId> out;
    for (const auto& leaf : Leaves(tree, Rect{0, 0, 1, 1}))
        out.insert(out.end(), leaf.windows.begin(), leaf.windows.end());
    return out;
}

const Node* FindNode(const Tree& tree, int id) {
    const int index = IndexOf(tree, id);
    return index < 0 ? nullptr : &tree.nodes[index];
}

Tree RemoveWindow(const Tree& tree, WindowId window) {
    return RemoveWindows(tree, [window](WindowId w) { return w == window; });
}

Tree RemoveWindows(const Tree& tree, const std::function<bool(WindowId)>& pred) {
    return Filtered(tree, [&](const std::vector<WindowId>& in) {
        std::vector<WindowId> out;
        for (WindowId w : in)
            if (!pred(w)) out.push_back(w);
        return out;
    }, false);
}

Tree Visible(const Tree& tree, const std::function<bool(WindowId)>& hidden) {
    return Filtered(tree, [&](const std::vector<WindowId>& in) {
        std::vector<WindowId> out;
        for (WindowId w : in)
            if (!hidden(w)) out.push_back(w);
        return out;
    }, true);
}

std::pair<Rect, Rect> Divide(const Rect& rect, bool vertical, double ratio) {
    const long extent = vertical ? rect.Width() : rect.Height();
    const long pixel = std::clamp(static_cast<long>(std::lround(extent * ratio)), 0L, std::max(0L, extent));
    Rect a = rect, b = rect;
    if (vertical) {
        a.right = rect.left + pixel;
        b.left = a.right;
    } else {
        a.bottom = rect.top + pixel;
        b.top = a.bottom;
    }
    return {a, b};
}

std::vector<LeafArea> Leaves(const Tree& tree, Rect work) {
    std::vector<LeafArea> out;
    const int count = static_cast<int>(tree.nodes.size());
    const auto walk = [&](auto&& self, int index, Rect rect, int depth) -> void {
        if (index < 0 || index >= count || depth > count) return;
        const Node& n = tree.nodes[index];
        if (n.IsLeaf()) {
            out.push_back({n.id, rect, n.windows});
            return;
        }
        const auto [a, b] = Divide(rect, n.vertical, n.ratio);
        self(self, n.first, a, depth + 1);
        self(self, n.second, b, depth + 1);
    };
    walk(walk, tree.root, work, 0);
    return out;
}

std::vector<WindowPlacement> Placements(const Tree& tree, Rect work) {
    std::vector<WindowPlacement> out;
    for (const auto& leaf : Leaves(tree, work))
        for (WindowId w : leaf.windows) out.push_back({w, leaf.rect});
    return out;
}

std::vector<DropOption> DropOptionsAt(const Tree& visible, Rect work, Point p,
                                      WindowId dragged, bool tiledElsewhere) {
    std::vector<DropOption> out;
    if (work.Width() <= 0 || work.Height() <= 0 || !work.Contains(p)) return out;
    if (visible.Empty()) {
        out.push_back({DropKind::Fill, -1, Side::Right, work, 0, true, -1, 4});
        return out;
    }

    struct Step {
        int index;
        Rect rect;
    };
    std::vector<Step> path;
    int index = visible.root;
    Rect rect = work;
    for (int guard = 0; guard <= static_cast<int>(visible.nodes.size()); ++guard) {
        if (index < 0 || index >= static_cast<int>(visible.nodes.size())) return {};
        path.push_back({index, rect});
        const Node& n = visible.nodes[index];
        if (n.IsLeaf()) break;
        const auto [a, b] = Divide(rect, n.vertical, n.ratio);
        if (a.Contains(p)) {
            index = n.first;
            rect = a;
        } else {
            index = n.second;
            rect = b;
        }
    }
    const Node& leaf = visible.nodes[path.back().index];
    if (!leaf.IsLeaf()) return {};
    const Rect tile = path.back().rect;

    const double w = std::max(1L, tile.Width());
    const double h = std::max(1L, tile.Height());
    const double fx = (p.x - tile.left + 0.5) / w;
    const double fy = (p.y - tile.top + 0.5) / h;
    const double distance[4] = {fx, fy, 1.0 - fx, 1.0 - fy};  // Side order
    int nearest = 0;
    for (int i = 1; i < 4; ++i)
        if (distance[i] < distance[nearest]) nearest = i;

    const bool ownTile =
        std::find(leaf.windows.begin(), leaf.windows.end(), dragged) != leaf.windows.end();

    if (distance[nearest] <= kEdgeBand) {
        const Side side = static_cast<Side>(nearest);
        // Every region on the path that shares this edge with the hovered
        // tile is a candidate scope, from the tile itself up to the monitor.
        int level = 0;
        for (int i = static_cast<int>(path.size()) - 1; i >= 0; --i) {
            if (EdgeOf(path[i].rect, side) != EdgeOf(tile, side)) break;
            const Node& n = visible.nodes[path[i].index];
            const bool pointless = n.IsLeaf() && n.windows.size() == 1 && n.windows[0] == dragged;
            if (pointless) continue;
            out.push_back({DropKind::Split, n.region, side, path[i].rect, level++, i == 0,
                           leaf.id, nearest});
        }
        if (out.empty() && ownTile)
            out.push_back({DropKind::Return, leaf.id, side, tile, 0, false, leaf.id, nearest});
        return out;
    }

    if (ownTile) {
        out.push_back({DropKind::Return, leaf.id, Side::Left, tile, 0, false, leaf.id, 4});
        return out;
    }
    if (tiledElsewhere || Contains(visible, dragged))
        out.push_back({DropKind::Swap, leaf.id, Side::Left, tile, 0, false, leaf.id, 4});
    out.push_back({DropKind::Stack, leaf.id, Side::Left, tile, 0, false, leaf.id, 4});
    return out;
}

std::optional<DropResult> ApplyDrop(const Tree& full, const DropOption& option, WindowId dragged) {
    if (dragged == 0 || dragged == kPlaceholder || !Validate(full).empty()) return std::nullopt;

    switch (option.kind) {
    case DropKind::Fill: {
        if (full.Empty()) return DropResult{MakeTree(dragged), {}};
        // Everything tiled here is hidden: share the monitor with it.
        auto t = SplitRegion(full, full.nodes[full.root].id, Side::Right, dragged);
        if (!t) return std::nullopt;
        return DropResult{std::move(*t), {}};
    }
    case DropKind::Return:
        if (!Contains(full, dragged)) return std::nullopt;
        return DropResult{full, {}};
    case DropKind::Split: {
        auto t = SplitRegion(full, option.node, option.side, dragged);
        if (!t) return std::nullopt;
        return DropResult{std::move(*t), {}};
    }
    case DropKind::Swap: {
        const int target = IndexOf(full, option.node);
        if (target < 0 || !full.nodes[target].IsLeaf()) return std::nullopt;
        Tree t = full;
        DropResult result;
        const int source = LeafIndexOf(t, dragged);
        if (source == target) return DropResult{full, {}};
        const auto targetWindows = t.nodes[target].windows;
        if (source >= 0) {
            auto& sw = t.nodes[source].windows;
            sw.erase(std::find(sw.begin(), sw.end(), dragged));
            sw.insert(sw.begin(), targetWindows.begin(), targetWindows.end());
        } else {
            result.displaced = targetWindows;
        }
        t.nodes[target].windows = {dragged};
        auto checked = Checked(std::move(t));
        if (!checked) return std::nullopt;
        result.tree = std::move(*checked);
        return result;
    }
    case DropKind::Stack: {
        Tree t = RemoveWindow(full, dragged);
        const int target = IndexOf(t, option.node);
        if (target < 0 || !t.nodes[target].IsLeaf()) return std::nullopt;
        auto& tw = t.nodes[target].windows;
        tw.insert(tw.begin(), dragged);
        auto checked = Checked(std::move(t));
        if (!checked) return std::nullopt;
        return DropResult{std::move(*checked), {}};
    }
    }
    return std::nullopt;
}

std::optional<Tree> ReplaceWindow(const Tree& tree, WindowId window,
                                  const std::vector<WindowId>& replacement) {
    Tree t = tree;
    const int index = LeafIndexOf(t, window);
    if (index < 0) return std::nullopt;
    auto& windows = t.nodes[index].windows;
    const auto at = windows.erase(std::find(windows.begin(), windows.end(), window));
    windows.insert(at, replacement.begin(), replacement.end());
    return Checked(Compact(t));
}

std::vector<Divider> Dividers(const Tree& tree, Rect work) {
    std::vector<Divider> out;
    const int count = static_cast<int>(tree.nodes.size());
    const auto walk = [&](auto&& self, int index, Rect rect, int depth) -> void {
        if (index < 0 || index >= count || depth > count) return;
        const Node& n = tree.nodes[index];
        if (n.IsLeaf()) return;
        const auto [a, b] = Divide(rect, n.vertical, n.ratio);
        out.push_back({n.id, rect, n.vertical, n.vertical ? a.right : a.bottom});
        self(self, n.first, a, depth + 1);
        self(self, n.second, b, depth + 1);
    };
    walk(walk, tree.root, work, 0);
    return out;
}

std::optional<Divider> DividerAt(const Tree& tree, Rect work, Point p, int tolerance) {
    std::optional<Divider> best;
    long bestDistance = tolerance + 1L;
    for (const auto& d : Dividers(tree, work)) {
        const bool inside = d.vertical ? p.y >= d.bounds.top && p.y < d.bounds.bottom
                                       : p.x >= d.bounds.left && p.x < d.bounds.right;
        const long distance = std::abs((d.vertical ? p.x : p.y) - d.position);
        if (inside && distance < bestDistance) {
            best = d;
            bestDistance = distance;
        }
    }
    return best;
}

std::optional<Tree> SetRatio(const Tree& tree, int splitId, double ratio) {
    Tree t = tree;
    const int index = IndexOf(t, splitId);
    if (index < 0 || t.nodes[index].IsLeaf() || !std::isfinite(ratio)) return std::nullopt;
    t.nodes[index].ratio = std::clamp(ratio, kMinDividerRatio, kMaxDividerRatio);
    return Checked(std::move(t));
}

std::optional<Tree> ResizeTile(const Tree& full, const Tree& visible, Rect work,
                               WindowId window, Rect resized) {
    constexpr long kTolerance = 2;
    std::optional<Rect> tile;
    for (const auto& leaf : Leaves(visible, work))
        if (std::find(leaf.windows.begin(), leaf.windows.end(), window) != leaf.windows.end())
            tile = leaf.rect;
    if (!tile) return std::nullopt;

    Tree t = full;
    for (const auto& d : Dividers(visible, work)) {
        // Only dividers of regions containing the tile can border it.
        if (tile->left < d.bounds.left || tile->right > d.bounds.right ||
            tile->top < d.bounds.top || tile->bottom > d.bounds.bottom)
            continue;
        long target;
        if (d.vertical && d.position == tile->right) target = resized.right;
        else if (d.vertical && d.position == tile->left) target = resized.left;
        else if (!d.vertical && d.position == tile->bottom) target = resized.bottom;
        else if (!d.vertical && d.position == tile->top) target = resized.top;
        else continue;
        if (std::abs(target - d.position) <= kTolerance) continue;
        const long origin = d.vertical ? d.bounds.left : d.bounds.top;
        const long extent = std::max(1L, d.vertical ? d.bounds.Width() : d.bounds.Height());
        auto updated = SetRatio(t, d.id, static_cast<double>(target - origin) / extent);
        if (!updated) return std::nullopt;
        t = std::move(*updated);
    }
    return t;
}

std::vector<WindowId> TooSmall(const Tree& visible, Rect work, long padding,
                               const std::function<Size(WindowId)>& minimum) {
    std::vector<WindowId> out;
    for (const auto& p : Placements(visible, work)) {
        const Size need = minimum(p.window);
        if (p.rect.Width() - 2 * padding < need.width || p.rect.Height() - 2 * padding < need.height)
            out.push_back(p.window);
    }
    return out;
}

namespace {

// Printable ASCII passes through; everything else (spaces, '%', non-ASCII)
// becomes %XXXX so the text survives INI files and token splitting.
std::wstring Escape(const std::wstring& text) {
    std::wstring out;
    for (wchar_t ch : text) {
        if (ch > 0x20 && ch < 0x7F && ch != L'%') {
            out.push_back(ch);
        } else {
            wchar_t code[8]{};
            std::swprintf(code, 8, L"%%%04X", static_cast<unsigned>(ch) & 0xFFFFu);
            out += code;
        }
    }
    return out;
}

std::optional<std::wstring> Unescape(const std::wstring& text) {
    std::wstring out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != L'%') {
            out.push_back(text[i]);
            continue;
        }
        if (i + 4 >= text.size()) return std::nullopt;
        unsigned value = 0;
        for (size_t j = 1; j <= 4; ++j) {
            const wchar_t c = text[i + j];
            value <<= 4;
            if (c >= L'0' && c <= L'9') value |= static_cast<unsigned>(c - L'0');
            else if (c >= L'A' && c <= L'F') value |= static_cast<unsigned>(c - L'A' + 10);
            else return std::nullopt;
        }
        out.push_back(static_cast<wchar_t>(value));
        i += 4;
    }
    return out;
}

std::vector<std::wstring> Tokens(const std::wstring& text) {
    std::vector<std::wstring> out;
    size_t start = 0;
    while (start < text.size()) {
        const size_t end = text.find(L' ', start);
        const size_t stop = end == std::wstring::npos ? text.size() : end;
        if (stop > start) out.push_back(text.substr(start, stop - start));
        start = stop + 1;
    }
    return out;
}

}  // namespace

std::wstring Serialize(const Tree& tree, const std::function<std::wstring(WindowId)>& key) {
    std::wstring out;
    const int count = static_cast<int>(tree.nodes.size());
    const auto walk = [&](auto&& self, int index, int depth) -> void {
        if (index < 0 || index >= count || depth > count) return;
        const Node& n = tree.nodes[index];
        if (!out.empty()) out.push_back(L' ');
        if (n.IsLeaf()) {
            out += L"L" + std::to_wstring(n.windows.size());
            for (WindowId w : n.windows) out += L" W" + Escape(key(w));
            return;
        }
        out += std::wstring(L"S") + (n.vertical ? L"v" : L"h") + std::to_wstring(n.ratio);
        self(self, n.first, depth + 1);
        self(self, n.second, depth + 1);
    };
    walk(walk, tree.root, 0);
    return out;
}

std::vector<std::wstring> SerializedWindowKeys(const std::wstring& text) {
    std::vector<std::wstring> out;
    for (const auto& token : Tokens(text)) {
        if (token.empty() || token[0] != L'W') continue;
        auto key = Unescape(token.substr(1));
        out.push_back(key ? *key : std::wstring{});
    }
    return out;
}

std::optional<Tree> Deserialize(const std::wstring& text, const std::vector<WindowId>& windows) {
    if (text.empty()) return Tree{};
    const auto tokens = Tokens(text);
    size_t position = 0;
    size_t windowIndex = 0;
    Tree raw;
    bool ok = true;
    const auto parse = [&](auto&& self, int depth) -> int {
        if (!ok || depth > 64 || position >= tokens.size()) {
            ok = false;
            return -1;
        }
        const std::wstring token = tokens[position++];
        Node n;
        n.id = n.region = raw.nextId++;
        if (token.size() >= 2 && token[0] == L'L') {
            const long count = std::wcstol(token.c_str() + 1, nullptr, 10);
            if (count < 1 || count > 64) {
                ok = false;
                return -1;
            }
            for (long i = 0; i < count; ++i) {
                if (position >= tokens.size() || tokens[position][0] != L'W') {
                    ok = false;
                    return -1;
                }
                ++position;
                const WindowId w = windowIndex < windows.size() ? windows[windowIndex] : 0;
                ++windowIndex;
                if (w) n.windows.push_back(w);
            }
            raw.nodes.push_back(std::move(n));
            return static_cast<int>(raw.nodes.size()) - 1;
        }
        if (token.size() >= 3 && token[0] == L'S' && (token[1] == L'v' || token[1] == L'h')) {
            wchar_t* end = nullptr;
            n.vertical = token[1] == L'v';
            n.ratio = std::wcstod(token.c_str() + 2, &end);
            if (!end || *end != L'\0' || !std::isfinite(n.ratio) || n.ratio <= 0.0 || n.ratio >= 1.0) {
                ok = false;
                return -1;
            }
            const int index = static_cast<int>(raw.nodes.size());
            raw.nodes.push_back(n);
            const int first = self(self, depth + 1);
            const int second = self(self, depth + 1);
            raw.nodes[index].first = first;
            raw.nodes[index].second = second;
            return index;
        }
        ok = false;
        return -1;
    };
    raw.root = parse(parse, 0);
    if (!ok || position != tokens.size()) return std::nullopt;
    // A window matched twice keeps its first tile; empty tiles collapse.
    std::unordered_set<WindowId> seen;
    for (auto& n : raw.nodes) {
        std::vector<WindowId> unique;
        for (WindowId w : n.windows)
            if (seen.insert(w).second) unique.push_back(w);
        n.windows = std::move(unique);
    }
    return Checked(Compact(raw));
}

}  // namespace zsdyn
