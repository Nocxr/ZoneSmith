#pragma once

// Pure Dynamic layout model. No Win32 calls: every operation is a function
// from (tree, input) to a new validated tree, so the overlay preview and the
// committed placement are computed by exactly the same code and can be tested.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace zsdyn {

using WindowId = std::uintptr_t;

struct Point {
    long x{};
    long y{};
};

struct Rect {
    long left{};
    long top{};
    long right{};
    long bottom{};

    long Width() const { return right - left; }
    long Height() const { return bottom - top; }
    bool Contains(Point p) const {
        return p.x >= left && p.x < right && p.y >= top && p.y < bottom;
    }
    bool operator==(const Rect&) const = default;
};

enum class Side { Left, Top, Right, Bottom };

// A binary split tree stored in a vector. Node ids are stable across
// operations; vector indices are not, so operations address nodes by id.
struct Node {
    int id{};
    // Id of the full-tree node whose screen region this node occupies. Equal
    // to id in a full tree; Visible() points it at collapsed ancestors.
    int region{};
    // Leaf: windows stacked in this region, front (active) window first.
    std::vector<WindowId> windows;
    int first{-1};
    int second{-1};
    // true: left|right split (vertical divider). false: top/bottom split.
    bool vertical{true};
    double ratio{0.5};

    bool IsLeaf() const { return first < 0; }
};

struct Tree {
    std::vector<Node> nodes;
    int root{-1};
    int nextId{1};

    bool Empty() const { return root < 0; }
    bool operator==(const Tree& other) const;
};

constexpr double kSplitRatio = 0.5;
constexpr double kMinDividerRatio = 0.15;
constexpr double kMaxDividerRatio = 0.85;
// Fraction of the hovered tile's width/height treated as its edge band.
// It only decides "edge of this tile" vs "centre of this tile"; it never
// decides tile-versus-monitor scope.
constexpr double kEdgeBand = 0.25;

// Returns an empty string when the tree is structurally valid.
std::string Validate(const Tree& tree);

Tree MakeTree(WindowId window);
bool Contains(const Tree& tree, WindowId window);
std::vector<WindowId> Windows(const Tree& tree);
const Node* FindNode(const Tree& tree, int id);

Tree RemoveWindow(const Tree& tree, WindowId window);
// Permanently removes every window matching pred (e.g. destroyed windows).
Tree RemoveWindows(const Tree& tree, const std::function<bool(WindowId)>& pred);
// The tree as it appears on screen: hidden windows (minimized, invisible)
// release their space but keep their place in the full tree.
Tree Visible(const Tree& tree, const std::function<bool(WindowId)>& hidden);

std::pair<Rect, Rect> Divide(const Rect& rect, bool vertical, double ratio);

struct LeafArea {
    int id{};
    Rect rect{};
    std::vector<WindowId> windows;
};
std::vector<LeafArea> Leaves(const Tree& tree, Rect work);

struct WindowPlacement {
    WindowId window{};
    Rect rect{};
    bool operator==(const WindowPlacement&) const = default;
};
std::vector<WindowPlacement> Placements(const Tree& tree, Rect work);

enum class DropKind {
    Fill,   // empty layout: take the whole monitor
    Split,  // split a region (tile, ancestor or monitor) at one edge
    Swap,   // exchange places with the hovered tile's windows
    Stack,  // share the hovered tile, stacked on top
    Return, // hovering the dragged window's own tile: put it back
};

struct DropOption {
    DropKind kind{};
    // Split: full-tree region id. Swap/Stack/Return: leaf id. Fill: -1.
    int node{-1};
    Side side{};
    // On-screen region affected by this option (the scope being split, or
    // the hovered tile).
    Rect scope{};
    // Split: 0 = hovered tile, increasing for each enclosing region.
    int level{};
    bool wholeMonitor{};
    // Hovered leaf and zone (0-3 = Side, 4 = centre). Identifies the hover
    // context so a caller can keep its option index while it is unchanged.
    int hoverLeaf{-1};
    int hoverZone{-1};
};

// Options for dropping `dragged` at point p, ordered from narrowest to
// widest scope. `visible` must be the on-screen tree (see Visible()).
// tiledElsewhere: dragged is tiled on another monitor (enables Swap there).
std::vector<DropOption> DropOptionsAt(const Tree& visible, Rect work, Point p,
                                      WindowId dragged, bool tiledElsewhere);

struct DropResult {
    Tree tree;
    // Windows displaced by a Swap with a window not in this tree; the caller
    // puts them where the dragged window came from.
    std::vector<WindowId> displaced;
};

// Applies an option produced by DropOptionsAt (on Visible(full)) to the full
// tree. Returns nullopt rather than producing an invalid tree.
std::optional<DropResult> ApplyDrop(const Tree& full, const DropOption& option,
                                    WindowId dragged);

// Replaces one window with a list of windows in the same tile (front first).
// An empty replacement removes the window.
std::optional<Tree> ReplaceWindow(const Tree& tree, WindowId window,
                                  const std::vector<WindowId>& replacement);

struct Divider {
    int id{};          // split node id
    Rect bounds{};     // region being divided
    bool vertical{};
    long position{};   // x (vertical) or y (horizontal) of the divider line
};
std::vector<Divider> Dividers(const Tree& tree, Rect work);
std::optional<Divider> DividerAt(const Tree& tree, Rect work, Point p, int tolerance);
std::optional<Tree> SetRatio(const Tree& tree, int splitId, double ratio);

// A tiled window was resized by its border to `resized` (tile coordinates,
// i.e. padding already removed). Every edge that moved and sits on a divider
// moves that divider; edges on the monitor boundary are ignored. Hit-testing
// uses `visible`; ratios are written to the same split ids in `full`.
std::optional<Tree> ResizeTile(const Tree& full, const Tree& visible, Rect work,
                               WindowId window, Rect resized);

struct Size {
    long width{};
    long height{};
};
// Windows whose tile (minus padding on each side) is smaller than their
// minimum size.
std::vector<WindowId> TooSmall(const Tree& visible, Rect work, long padding,
                               const std::function<Size(WindowId)>& minimum);

// Text form of a tree for saving across restarts. Window identities come from
// `key` and are escaped; Deserialize takes the windows matched to those keys
// in order (0 = no match, dropped) and assigns fresh node ids.
std::wstring Serialize(const Tree& tree, const std::function<std::wstring(WindowId)>& key);
std::vector<std::wstring> SerializedWindowKeys(const std::wstring& text);
std::optional<Tree> Deserialize(const std::wstring& text, const std::vector<WindowId>& windows);

}  // namespace zsdyn
