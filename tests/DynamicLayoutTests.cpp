// Unit tests for the pure Dynamic layout model. No Win32 dependency.

#include "DynamicLayout.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace zsdyn;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                        \
    } while (0)

constexpr Rect kWork{0, 0, 1920, 1080};
constexpr WindowId A = 101, B = 102, C = 103, D = 104;

Rect RectOf(const Tree& tree, WindowId w, Rect work = kWork) {
    for (const auto& p : Placements(tree, work))
        if (p.window == w) return p.rect;
    return Rect{-1, -1, -1, -1};
}

const DropOption* Find(const std::vector<DropOption>& options, DropKind kind, int level = 0) {
    for (const auto& o : options)
        if (o.kind == kind && (kind != DropKind::Split || o.level == level)) return &o;
    return nullptr;
}

Tree Drop(const Tree& full, Point p, WindowId dragged, DropKind kind, int level = 0,
          bool tiledElsewhere = false) {
    const auto options = DropOptionsAt(full, kWork, p, dragged, tiledElsewhere);
    const auto* option = Find(options, kind, level);
    if (!option) {
        std::printf("  no option kind=%d level=%d at (%ld,%ld)\n", static_cast<int>(kind), level, p.x, p.y);
        ++g_failures;
        return full;
    }
    auto result = ApplyDrop(full, *option, dragged);
    if (!result) {
        std::printf("  ApplyDrop rejected kind=%d\n", static_cast<int>(kind));
        ++g_failures;
        return full;
    }
    CHECK(Validate(result->tree).empty());
    return result->tree;
}

// [A | B], each half the monitor.
Tree SideBySide() {
    Tree t = MakeTree(A);
    return Drop(t, {1800, 540}, B, DropKind::Split);
}

void TestFillEmptyMonitor() {
    const auto options = DropOptionsAt(Tree{}, kWork, {500, 500}, A, false);
    CHECK(options.size() == 1 && options[0].kind == DropKind::Fill);
    auto r = ApplyDrop(Tree{}, options[0], A);
    CHECK(r && RectOf(r->tree, A) == kWork);
}

void TestDropToRightHalf() {
    const Tree t = SideBySide();
    CHECK((RectOf(t, A) == Rect{0, 0, 960, 1080}));
    CHECK((RectOf(t, B) == Rect{960, 0, 1920, 1080}));
}

void TestSplitOnlyRightTileVertically() {
    const Tree t = SideBySide();
    // Bottom edge of B, close to the monitor's bottom edge too.
    const auto options = DropOptionsAt(t, kWork, {1440, 1060}, C, false);
    CHECK(options.size() == 2);
    CHECK(options[0].kind == DropKind::Split && options[0].level == 0 && !options[0].wholeMonitor);
    CHECK(options[1].kind == DropKind::Split && options[1].wholeMonitor);
    const Tree r = Drop(t, {1440, 1060}, C, DropKind::Split, 0);
    CHECK((RectOf(r, A) == Rect{0, 0, 960, 1080}));
    CHECK((RectOf(r, B) == Rect{960, 0, 1920, 540}));
    CHECK((RectOf(r, C) == Rect{960, 540, 1920, 1080}));
}

void TestSplitRightTileHorizontallyAtMonitorEdge() {
    // The original bug: the monitor's right band stole this gesture.
    const Tree t = SideBySide();
    const Tree r = Drop(t, {1910, 540}, C, DropKind::Split, 0);
    CHECK((RectOf(r, A) == Rect{0, 0, 960, 1080}));
    CHECK((RectOf(r, B) == Rect{960, 0, 1440, 1080}));
    CHECK((RectOf(r, C) == Rect{1440, 0, 1920, 1080}));
}

void TestInnerEdgeOffersOnlyTileScope() {
    const Tree t = SideBySide();
    const auto options = DropOptionsAt(t, kWork, {980, 540}, C, false);
    CHECK(options.size() == 1);
    CHECK(options[0].kind == DropKind::Split && options[0].side == Side::Left);
    const Tree r = Drop(t, {980, 540}, C, DropKind::Split, 0);
    CHECK((RectOf(r, C) == Rect{960, 0, 1440, 1080}));
    CHECK((RectOf(r, B) == Rect{1440, 0, 1920, 1080}));
}

void TestFullWidthBottomRegion() {
    const Tree t = SideBySide();
    const Tree r = Drop(t, {1440, 1060}, C, DropKind::Split, 1);
    CHECK((RectOf(r, A) == Rect{0, 0, 960, 540}));
    CHECK((RectOf(r, B) == Rect{960, 0, 1920, 540}));
    CHECK((RectOf(r, C) == Rect{0, 540, 1920, 1080}));
}

// [A | (B / C)]
Tree ColumnLayout() {
    return Drop(SideBySide(), {1440, 1060}, C, DropKind::Split, 0);
}

void TestAncestorScopeCombinesAdjacentTiles() {
    const Tree t = ColumnLayout();
    // Right edge of C: tile C, the right column (B+C), and the monitor.
    const auto options = DropOptionsAt(t, kWork, {1910, 800}, D, false);
    CHECK(options.size() == 3);
    CHECK(options[0].level == 0 && options[1].level == 1 && options[2].level == 2);
    CHECK(options[2].wholeMonitor);
    CHECK((options[1].scope == Rect{960, 0, 1920, 1080}));
    const Tree r = Drop(t, {1910, 800}, D, DropKind::Split, 1);
    // D spans the full height of the right column; B and C keep stacking.
    CHECK((RectOf(r, D) == Rect{1440, 0, 1920, 1080}));
    CHECK((RectOf(r, B) == Rect{960, 0, 1440, 540}));
    CHECK((RectOf(r, C) == Rect{960, 540, 1440, 1080}));
    CHECK((RectOf(r, A) == Rect{0, 0, 960, 1080}));
}

void TestMoveTiledWindowToMonitorBottom() {
    const Tree t = ColumnLayout();
    // Drag C (already tiled) to the bottom edge of A, monitor scope.
    const auto options = DropOptionsAt(t, kWork, {480, 1060}, C, false);
    const auto* monitor = Find(options, DropKind::Split, 1);
    CHECK(monitor && monitor->wholeMonitor);
    const Tree r = Drop(t, {480, 1060}, C, DropKind::Split, 1);
    CHECK((RectOf(r, C) == Rect{0, 540, 1920, 1080}));
    CHECK((RectOf(r, A) == Rect{0, 0, 960, 540}));
    CHECK((RectOf(r, B) == Rect{960, 0, 1920, 540}));
    CHECK(Windows(r).size() == 3);
}

void TestSwap() {
    const Tree t = SideBySide();
    const auto options = DropOptionsAt(t, kWork, {1440, 540}, A, false);
    CHECK(options.size() == 2 && options[0].kind == DropKind::Swap && options[1].kind == DropKind::Stack);
    const Tree r = Drop(t, {1440, 540}, A, DropKind::Swap);
    CHECK((RectOf(r, B) == Rect{0, 0, 960, 1080}));
    CHECK((RectOf(r, A) == Rect{960, 0, 1920, 1080}));
    CHECK(Windows(r).size() == 2);
}

void TestSwapAcrossMonitors() {
    const Tree target = SideBySide();
    const Tree source = MakeTree(C);
    const auto options = DropOptionsAt(target, kWork, {1440, 540}, C, true);
    const auto* swap = Find(options, DropKind::Swap);
    CHECK(swap != nullptr);
    if (!swap) return;
    auto r = ApplyDrop(target, *swap, C);
    CHECK(r && r->displaced == std::vector<WindowId>{B});
    auto newSource = ReplaceWindow(source, C, r->displaced);
    CHECK(newSource && Windows(*newSource) == std::vector<WindowId>{B});
    CHECK((RectOf(r->tree, C) == Rect{960, 0, 1920, 1080}));
}

void TestOwnTileCentreReturns() {
    const Tree t = SideBySide();
    const auto options = DropOptionsAt(t, kWork, {480, 540}, A, false);
    CHECK(options.size() == 1 && options[0].kind == DropKind::Return);
    auto r = ApplyDrop(t, options[0], A);
    CHECK(r && r->tree == t);
    // A lone window dragged to its own edge has nothing to split.
    const Tree single = MakeTree(A);
    const auto edge = DropOptionsAt(single, kWork, {1910, 540}, A, false);
    CHECK(edge.size() == 1 && edge[0].kind == DropKind::Return);
}

void TestStack() {
    const Tree t = SideBySide();
    const auto options = DropOptionsAt(t, kWork, {1440, 540}, C, false);
    CHECK(options.size() == 1 && options[0].kind == DropKind::Stack);
    const Tree r = Drop(t, {1440, 540}, C, DropKind::Stack);
    CHECK(RectOf(r, C) == RectOf(r, B));
    CHECK((RectOf(r, A) == Rect{0, 0, 960, 1080}));
    // Removing the stacked window leaves the tile to B.
    const Tree removed = RemoveWindow(r, C);
    CHECK((RectOf(removed, B) == Rect{960, 0, 1920, 1080}));
}

void TestCloseReclaimsSpace() {
    const Tree t = ColumnLayout();
    const Tree r = RemoveWindow(t, B);
    CHECK(Validate(r).empty());
    CHECK((RectOf(r, C) == Rect{960, 0, 1920, 1080}));
    const Tree r2 = RemoveWindow(r, A);
    CHECK(RectOf(r2, C) == kWork);
    const Tree r3 = RemoveWindow(r2, C);
    CHECK(r3.Empty() && Validate(r3).empty());
}

void TestMinimizeRestoreKeepsPositions() {
    Tree full = ColumnLayout();
    const auto original = Placements(full, kWork);
    for (int round = 0; round < 5; ++round) {
        for (WindowId hidden : {A, B, C}) {
            const Tree visible = Visible(full, [&](WindowId w) { return w == hidden; });
            CHECK(Validate(visible).empty());
            CHECK(!Contains(visible, hidden));
            CHECK(Placements(visible, kWork).size() == 2);
        }
        // Restoring everything returns the exact original layout.
        CHECK(Placements(Visible(full, [](WindowId) { return false; }), kWork) == original);
    }
    // B minimized: C uses the whole right column while B is hidden.
    const Tree visible = Visible(full, [](WindowId w) { return w == B; });
    CHECK((RectOf(visible, C) == Rect{960, 0, 1920, 1080}));
    CHECK(Contains(full, B));
}

void TestDropWhileSiblingMinimized() {
    // [A | B] with A minimized: B fills the screen. A new drop at B's right
    // edge splits the region B occupies (the whole monitor), so A returns to
    // a sensible place when restored.
    const Tree full = SideBySide();
    const auto hidden = [](WindowId w) { return w == A; };
    const Tree visible = Visible(full, hidden);
    const auto options = DropOptionsAt(visible, kWork, {1910, 540}, C, false);
    CHECK(options.size() == 1 && options[0].wholeMonitor);
    auto r = ApplyDrop(full, options[0], C);
    CHECK(r.has_value());
    if (!r) return;
    CHECK(Contains(r->tree, A));
    // While A stays minimized, the preview geometry is what gets placed.
    const Tree after = Visible(r->tree, hidden);
    CHECK((RectOf(after, B) == Rect{0, 0, 960, 1080}));
    CHECK((RectOf(after, C) == Rect{960, 0, 1920, 1080}));
    // Restoring A brings it back next to B.
    CHECK(Placements(r->tree, kWork).size() == 3);
    CHECK((RectOf(r->tree, A) == Rect{0, 0, 480, 1080}));
}

void TestDividers() {
    const Tree t = SideBySide();
    const auto divider = DividerAt(t, kWork, {965, 300}, 12);
    CHECK(divider && divider->vertical && divider->position == 960);
    CHECK(!DividerAt(t, kWork, {700, 300}, 12));
    if (!divider) return;
    auto r = SetRatio(t, divider->id, 0.3);
    CHECK(r && (RectOf(*r, A) == Rect{0, 0, 576, 1080}));
    auto clamped = SetRatio(t, divider->id, 0.01);
    CHECK(clamped && clamped->nodes[clamped->root].ratio == kMinDividerRatio);
    CHECK(!SetRatio(t, 9999, 0.5));
    // Divider ids survive later drops, so an adjusted ratio is kept.
    const Tree nested = Drop(*r, {1440, 1060}, C, DropKind::Split, 0);
    CHECK((RectOf(nested, A) == Rect{0, 0, 576, 1080}));
}

void TestValidationRejectsCorruption() {
    Tree t = SideBySide();
    CHECK(Validate(t).empty());

    Tree duplicate = t;
    for (auto& n : duplicate.nodes)
        if (n.IsLeaf()) n.windows = {A};
    CHECK(!Validate(duplicate).empty());

    Tree cycle = t;
    cycle.nodes[cycle.root].first = cycle.root;
    CHECK(!Validate(cycle).empty());

    Tree empty = t;
    for (auto& n : empty.nodes)
        if (n.IsLeaf()) n.windows.clear();
    CHECK(!Validate(empty).empty());

    DropOption stale{DropKind::Split, 9999, Side::Right};
    CHECK(!ApplyDrop(t, stale, C));
    CHECK(!ApplyDrop(cycle, DropOption{DropKind::Split, 1, Side::Right}, C));
    DropOption swapSplit{DropKind::Swap, t.nodes[t.root].id};
    CHECK(!ApplyDrop(t, swapSplit, C));  // swap target must be a tile
}

void TestNoNodeGrowthAcrossManyOperations() {
    Tree t = ColumnLayout();
    for (int i = 0; i < 200; ++i) {
        const WindowId w = (i % 2) ? A : C;
        t = Drop(t, {1910, 100}, w, DropKind::Split, 0);
        t = Drop(t, {10, 1060}, w, DropKind::Split, 0);
    }
    CHECK(Validate(t).empty());
    CHECK(Windows(t).size() == 3);
    CHECK(t.nodes.size() == 5);
}

void TestMonitorsAreIndependent() {
    // Moving a tiled window from monitor 1 to monitor 2: target gets the
    // split, source loses the tile and reclaims its space.
    const Rect second{1920, 0, 3840, 1080};
    const Tree source = SideBySide();
    const Tree target = MakeTree(C);
    const auto options = DropOptionsAt(target, second, {3830, 540}, B, true);
    CHECK(options.size() == 1);
    if (options.empty()) return;
    auto r = ApplyDrop(target, options[0], B);
    CHECK(r.has_value());
    if (!r) return;
    const Tree newSource = RemoveWindow(source, B);
    CHECK(RectOf(newSource, A) == kWork);
    CHECK((RectOf(r->tree, B, second) == Rect{2880, 0, 3840, 1080}));
    CHECK((RectOf(r->tree, C, second) == Rect{1920, 0, 2880, 1080}));
}

void TestBorderResizeMovesDividers() {
    const Tree t = SideBySide();
    // Drag A's right border to x=1200: the shared divider follows.
    auto r = ResizeTile(t, t, kWork, A, Rect{0, 0, 1200, 1080});
    CHECK(r && (RectOf(*r, A) == Rect{0, 0, 1200, 1080}));
    CHECK(r && (RectOf(*r, B) == Rect{1200, 0, 1920, 1080}));
    // Dragging an edge on the monitor boundary changes nothing.
    auto outer = ResizeTile(t, t, kWork, A, Rect{-60, 0, 960, 1080});
    CHECK(outer && *outer == t);

    // Nested: C's top edge moves the B/C divider, its left edge the root one.
    const Tree c = ColumnLayout();
    auto nested = ResizeTile(c, c, kWork, C, Rect{800, 400, 1920, 1080});
    CHECK(nested.has_value());
    if (nested) {
        CHECK((RectOf(*nested, B) == Rect{800, 0, 1920, 400}));
        CHECK((RectOf(*nested, C) == Rect{800, 400, 1920, 1080}));
        CHECK((RectOf(*nested, A) == Rect{0, 0, 800, 1080}));
    }
    CHECK(!ResizeTile(c, c, kWork, D, Rect{0, 0, 10, 10}));
}

void TestBorderResizeWithHiddenSibling() {
    const Tree full = ColumnLayout();
    const Tree visible = Visible(full, [](WindowId w) { return w == B; });
    // C fills the right column while B is minimized: its top edge is the
    // monitor edge, so only its left edge can move a divider.
    auto r = ResizeTile(full, visible, kWork, C, Rect{900, 300, 1920, 1080});
    CHECK(r.has_value());
    if (!r) return;
    CHECK((RectOf(*r, A) == Rect{0, 0, 900, 1080}));
    // B and C keep their split when B is restored.
    CHECK((RectOf(*r, B) == Rect{900, 0, 1920, 540}));
}

void TestMinimumSizes() {
    const Tree t = SideBySide();
    const auto none = [](WindowId) { return Size{}; };
    CHECK(TooSmall(t, kWork, 0, none).empty());
    const auto wideA = [](WindowId w) { return w == A ? Size{1000, 0} : Size{}; };
    CHECK(TooSmall(t, kWork, 0, wideA) == std::vector<WindowId>{A});
    const auto fits = [](WindowId w) { return w == A ? Size{950, 0} : Size{}; };
    CHECK(TooSmall(t, kWork, 0, fits).empty());
    CHECK(TooSmall(t, kWork, 8, fits) == std::vector<WindowId>{A});  // padding counts
}

void TestSerializeRoundTrip() {
    Tree t = Drop(ColumnLayout(), {1440, 300}, D, DropKind::Stack);
    auto adjusted = SetRatio(t, t.nodes[t.root].id, 0.3);
    CHECK(adjusted.has_value());
    if (!adjusted) return;
    t = *adjusted;
    const auto key = [](WindowId w) {
        switch (w) {
        case A: return std::wstring(L"app.exe\tClass\tTitle with spaces");
        case B: return std::wstring(L"b.exe\tC\t100% \x00FCn\x00EF");
        case C: return std::wstring(L"c.exe\tC\t");
        default: return std::wstring(L"d.exe\tC\tD");
        }
    };
    const std::wstring text = Serialize(t, key);
    CHECK(text.find(L'\t') == std::wstring::npos);
    const auto keys = SerializedWindowKeys(text);
    CHECK(keys.size() == 4);
    if (keys.size() != 4) return;

    std::vector<WindowId> windows;
    for (const auto& k : keys) {
        for (WindowId w : {A, B, C, D})
            if (key(w) == k) windows.push_back(w);
    }
    CHECK(windows.size() == 4);
    auto restored = Deserialize(text, windows);
    CHECK(restored.has_value());
    if (restored) CHECK(Placements(*restored, kWork) == Placements(t, kWork));

    // A window that is no longer open: the others take its space.
    auto partial = windows;
    for (auto& w : partial)
        if (w == A) w = 0;
    auto missing = Deserialize(text, partial);
    CHECK(missing && Validate(*missing).empty() && !Contains(*missing, A));
    CHECK(missing && (RectOf(*missing, B) == Rect{0, 0, 1920, 540}));

    // One window matched to two keys keeps only its first tile.
    auto twice = windows;
    twice[1] = twice[0];
    auto deduped = Deserialize(text, twice);
    CHECK(deduped && Validate(*deduped).empty());

    CHECK(!Deserialize(L"Sv0.5 L1 Wx", {A}));       // missing second child
    CHECK(!Deserialize(L"Sv1.5 L1 Wx L1 Wy", {A, B})); // bad ratio
    CHECK(!Deserialize(L"Q", {}));
    auto empty = Deserialize(L"", {});
    CHECK(empty && empty->Empty());
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"fill empty monitor", TestFillEmptyMonitor},
        {"drop to right half", TestDropToRightHalf},
        {"split only the right tile vertically", TestSplitOnlyRightTileVertically},
        {"split right tile horizontally at monitor edge", TestSplitRightTileHorizontallyAtMonitorEdge},
        {"inner edge offers only tile scope", TestInnerEdgeOffersOnlyTileScope},
        {"full-width bottom region", TestFullWidthBottomRegion},
        {"ancestor scope combines adjacent tiles", TestAncestorScopeCombinesAdjacentTiles},
        {"move tiled window to monitor bottom", TestMoveTiledWindowToMonitorBottom},
        {"swap", TestSwap},
        {"swap across monitors", TestSwapAcrossMonitors},
        {"own tile centre returns", TestOwnTileCentreReturns},
        {"stack", TestStack},
        {"close reclaims space", TestCloseReclaimsSpace},
        {"minimize/restore keeps positions", TestMinimizeRestoreKeepsPositions},
        {"drop while sibling minimized", TestDropWhileSiblingMinimized},
        {"dividers", TestDividers},
        {"validation rejects corruption", TestValidationRejectsCorruption},
        {"no node growth across many operations", TestNoNodeGrowthAcrossManyOperations},
        {"monitors are independent", TestMonitorsAreIndependent},
        {"border resize moves dividers", TestBorderResizeMovesDividers},
        {"border resize with hidden sibling", TestBorderResizeWithHiddenSibling},
        {"minimum sizes", TestMinimumSizes},
        {"serialize round trip", TestSerializeRoundTrip},
    };
    for (const auto& [name, test] : tests) {
        const int before = g_failures;
        test();
        std::printf("%s %s\n", g_failures == before ? "PASS" : "FAIL", name);
    }
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
