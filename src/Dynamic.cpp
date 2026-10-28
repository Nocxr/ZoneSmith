#include "Dynamic.h"
#include "WindowUtils.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <string>
#include <utility>

namespace {

using zsdyn::WindowId;

WindowId Id(HWND window) { return reinterpret_cast<WindowId>(window); }
HWND Hwnd(WindowId id) { return reinterpret_cast<HWND>(id); }

zsdyn::Rect ToModel(const RECT& r) { return {r.left, r.top, r.right, r.bottom}; }
RECT ToWin(const zsdyn::Rect& r) { return {r.left, r.top, r.right, r.bottom}; }

std::wstring NormalizeMonitorKey(const wchar_t* device) {
    std::wstring key = device;
    if (key.starts_with(L"\\\\.\\")) key.erase(0, 4);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return key;
}

std::wstring KeyForPoint(POINT point) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &info);
    return NormalizeMonitorKey(info.szDevice);
}

bool WorkForKey(const std::wstring& key, RECT& work) {
    struct Context {
        const std::wstring* key;
        RECT* work;
        bool found;
    } context{&key, &work, false};
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM param) -> BOOL {
        auto& ctx = *reinterpret_cast<Context*>(param);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info) || NormalizeMonitorKey(info.szDevice) != *ctx.key)
            return TRUE;
        *ctx.work = info.rcWork;
        ctx.found = true;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&context));
    return context.found;
}

bool IsDynamicKey(const AppState& app, const std::wstring& key) {
    if (app.layouts.empty()) return false;
    if (key == app.currentMonitorKey) return CurrentLayout(app).name == L"Dynamic";
    const auto saved = app.monitorLayouts.find(key);
    return saved != app.monitorLayouts.end() && _wcsicmp(saved->second.c_str(), L"Dynamic") == 0;
}

bool IsDeadWindow(WindowId id) { return !IsWindow(Hwnd(id)); }

// Hidden windows release their space but keep their logical position.
bool IsHiddenWindow(WindowId id) {
    const HWND window = Hwnd(id);
    if (!IsWindow(window) || IsIconic(window) || !IsWindowVisible(window)) return true;
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
           cloaked != 0;
}

zsdyn::Tree OnScreen(const zsdyn::Tree& tree) { return zsdyn::Visible(tree, IsHiddenWindow); }

void Place(AppState& app, const std::vector<zsdyn::WindowPlacement>& placements) {
    app.dynamicReflowApplying = true;
    for (const auto& placement : placements) {
        const HWND window = Hwnd(placement.window);
        if (!IsWindow(window) || IsIconic(window)) continue;
        const RECT bounds = ToWin(placement.rect);
        RECT target = bounds;
        InflateRect(&target,
                    -std::min(app.snapPadding, static_cast<int>((bounds.right - bounds.left - 1) / 2)),
                    -std::min(app.snapPadding, static_cast<int>((bounds.bottom - bounds.top - 1) / 2)));
        if (IsZoomed(window)) ShowWindow(window, SW_RESTORE);
        SetWindowPos(window, nullptr, target.left, target.top,
                     std::max(1L, target.right - target.left), std::max(1L, target.bottom - target.top),
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
    app.dynamicReflowApplying = false;
}

// Moves windows only when the on-screen layout differs from what was last
// applied (or when forced after an explicit layout change).
bool ReflowMonitor(AppState& app, const std::wstring& key, bool force) {
    const auto it = app.dynamicMonitors.find(key);
    RECT work{};
    if (it == app.dynamicMonitors.end() || !WorkForKey(key, work)) return false;
    auto placements = zsdyn::Placements(OnScreen(it->second.tree), ToModel(work));
    if (!force && placements == it->second.applied) return false;
    Place(app, placements);
    it->second.applied = std::move(placements);
    if (app.dividerGuideWindow) InvalidateRect(app.dividerGuideWindow, nullptr, FALSE);
    return true;
}

DynamicDropPlan PlanDrop(const AppState& app, HWND window, POINT point, int optionIndex) {
    DynamicDropPlan plan;
    plan.window = window;
    if (!window || !IsWindow(window)) return plan;
    const std::wstring key = KeyForPoint(point);
    RECT work{};
    if (!IsDynamicKey(app, key) || !WorkForKey(key, work)) return plan;

    zsdyn::Tree full;
    unsigned int revision = 0;
    if (const auto it = app.dynamicMonitors.find(key); it != app.dynamicMonitors.end()) {
        full = zsdyn::RemoveWindows(it->second.tree, IsDeadWindow);
        revision = it->second.revision;
    }

    const WindowId dragged = Id(window);
    const std::wstring* sourceKey = nullptr;
    const DynamicMonitorState* source = nullptr;
    for (const auto& [otherKey, state] : app.dynamicMonitors) {
        if (otherKey != key && zsdyn::Contains(state.tree, dragged)) {
            sourceKey = &otherKey;
            source = &state;
            break;
        }
    }

    const auto options = zsdyn::DropOptionsAt(OnScreen(full), ToModel(work), {point.x, point.y},
                                              dragged, source != nullptr);
    if (options.empty()) return plan;
    const int count = static_cast<int>(options.size());
    const int index = ((optionIndex % count) + count) % count;
    auto result = zsdyn::ApplyDrop(full, options[index], dragged);
    if (!result) return plan;

    if (source) {
        auto sourceResult = result->displaced.empty()
            ? std::optional<zsdyn::Tree>(zsdyn::RemoveWindow(source->tree, dragged))
            : zsdyn::ReplaceWindow(source->tree, dragged, result->displaced);
        if (!sourceResult) return plan;
        plan.hasSource = true;
        plan.sourceKey = *sourceKey;
        plan.sourceResult = std::move(*sourceResult);
        plan.sourceRevision = source->revision;
    } else if (!result->displaced.empty()) {
        return plan;
    }

    plan.valid = true;
    plan.key = key;
    plan.work = ToModel(work);
    plan.option = options[index];
    plan.optionIndex = index;
    plan.optionCount = count;
    plan.result = std::move(result->tree);
    plan.revision = revision;
    return plan;
}

std::wstring ContextOf(const DynamicDropPlan& plan) {
    if (!plan.valid) return {};
    return plan.key + L"|" + std::to_wstring(plan.option.hoverLeaf) + L"|" +
           std::to_wstring(plan.option.hoverZone);
}

bool SamePreview(const DynamicDropPlan& a, const DynamicDropPlan& b) {
    if (a.valid != b.valid || a.window != b.window) return false;
    if (!a.valid) return true;
    return a.key == b.key && a.optionIndex == b.optionIndex && a.optionCount == b.optionCount &&
           a.option.kind == b.option.kind && a.option.node == b.option.node &&
           a.option.side == b.option.side && a.option.scope == b.option.scope &&
           a.result == b.result;
}

void Commit(AppState& app, const DynamicDropPlan& plan) {
    auto& target = app.dynamicMonitors[plan.key];
    // The layout changed after the preview was computed: drop nothing rather
    // than apply an operation to a tree it was not computed for.
    if (target.revision != plan.revision) return;
    if (plan.hasSource) {
        const auto source = app.dynamicMonitors.find(plan.sourceKey);
        if (source == app.dynamicMonitors.end() || source->second.revision != plan.sourceRevision)
            return;
        source->second.tree = plan.sourceResult;
        ++source->second.revision;
    }
    target.tree = plan.result;
    ++target.revision;
    if (plan.hasSource && IsDynamicKey(app, plan.sourceKey)) ReflowMonitor(app, plan.sourceKey, true);
    ReflowMonitor(app, plan.key, true);
}

}  // namespace

bool IsDynamicLayout(const AppState& app) {
    return !app.layouts.empty() && CurrentLayout(app).name == L"Dynamic";
}

bool IsDynamicMonitorAt(const AppState& app, POINT point) {
    return IsDynamicKey(app, KeyForPoint(point));
}

bool IsDynamicTiled(const AppState& app, HWND window) {
    if (!window) return false;
    for (const auto& [key, state] : app.dynamicMonitors)
        if (zsdyn::Contains(state.tree, Id(window))) return true;
    return false;
}

void BeginDynamicDrag(AppState& app, HWND window, bool titleBarMove) {
    app.dynamicDragIsMove = titleBarMove;
    app.dynamicDragWasZoomed = window && IsZoomed(window);
    RECT rect{};
    if (window) GetWindowRect(window, &rect);
    app.dynamicDragStartSize = {rect.right - rect.left, rect.bottom - rect.top};
    app.dynamicOverlayDismissed = false;
    app.dynamicOptionIndex = 0;
    app.dynamicOptionContext.clear();
    app.dynamicPreview = {};
}

bool UpdateDynamicDragPreview(AppState& app, POINT cursor) {
    if (!app.dragWindow || !app.dynamicDragIsMove) {
        const bool changed = app.dynamicPreview.valid;
        app.dynamicPreview = {};
        return changed;
    }
    auto plan = PlanDrop(app, app.dragWindow, cursor, app.dynamicOptionIndex);
    const std::wstring context = ContextOf(plan);
    if (context != app.dynamicOptionContext) {
        // A different tile or edge: start again from the narrowest scope.
        app.dynamicOptionContext = context;
        app.dynamicOptionIndex = 0;
        if (plan.valid && plan.optionIndex != 0) plan = PlanDrop(app, app.dragWindow, cursor, 0);
    }
    const bool changed = !SamePreview(plan, app.dynamicPreview);
    app.dynamicPreview = std::move(plan);
    return changed;
}

void CycleDynamicDropOption(AppState& app) {
    if (!app.dynamicPreview.valid || app.dynamicPreview.optionCount < 2) return;
    app.dynamicOptionIndex = (app.dynamicPreview.optionIndex + 1) % app.dynamicPreview.optionCount;
    UpdateDynamicDragPreview(app, app.cursor);
}

void QueueDynamicDrop(AppState& app) {
    if (!app.dragWindow || !app.dynamicDragIsMove) return;
    // Belt and braces for "resizing never docks": a drag that changed the
    // window's size (other than leaving a maximized state) was a resize.
    RECT rect{};
    GetWindowRect(app.dragWindow, &rect);
    const bool resized = !app.dynamicDragWasZoomed &&
        (std::abs((rect.right - rect.left) - app.dynamicDragStartSize.cx) > 2 ||
         std::abs((rect.bottom - rect.top) - app.dynamicDragStartSize.cy) > 2);
    if (resized) return;

    UpdateDynamicDragPreview(app, app.cursor);
    // An invalid plan (e.g. released over the taskbar) still records the
    // window, so a tiled window returns to its tile instead of floating.
    app.pendingDynamicDrop = app.dynamicPreview;
    app.pendingDynamicDrop.window = app.dragWindow;
    SetTimer(app.overlay, kSnapTimerId, 75, nullptr);
}

void QueueDynamicUndock(AppState& app, HWND window) {
    app.pendingDynamicUndock = window;
    SetTimer(app.overlay, kSnapTimerId, 75, nullptr);
}

bool ApplyPendingDynamicOperation(AppState& app) {
    if (const HWND window = std::exchange(app.pendingDynamicUndock, nullptr)) {
        for (auto& [key, state] : app.dynamicMonitors) {
            if (!zsdyn::Contains(state.tree, Id(window))) continue;
            state.tree = zsdyn::RemoveWindow(state.tree, Id(window));
            ++state.revision;
            if (IsDynamicKey(app, key)) ReflowMonitor(app, key, true);
        }
    }

    const DynamicDropPlan plan = std::exchange(app.pendingDynamicDrop, {});
    if (!plan.window) return false;
    if (plan.valid) {
        Commit(app, plan);
    } else {
        for (const auto& [key, state] : app.dynamicMonitors)
            if (zsdyn::Contains(state.tree, Id(plan.window)) && IsDynamicKey(app, key))
                ReflowMonitor(app, key, true);
    }
    return true;
}

namespace {

COLORREF ScopeColor(const zsdyn::DropOption& option) {
    switch (option.kind) {
    case zsdyn::DropKind::Swap: return RGB(36, 185, 122);
    case zsdyn::DropKind::Stack: return RGB(30, 190, 200);
    case zsdyn::DropKind::Return: return RGB(150, 160, 175);
    case zsdyn::DropKind::Fill: return RGB(35, 135, 230);
    case zsdyn::DropKind::Split: break;
    }
    if (option.wholeMonitor && option.level > 0) return RGB(245, 150, 40);  // monitor
    if (option.level > 0) return RGB(175, 110, 245);                          // group of tiles
    return RGB(35, 135, 230);                                                 // single tile
}

const wchar_t* SideName(zsdyn::Side side) {
    switch (side) {
    case zsdyn::Side::Left: return L"left";
    case zsdyn::Side::Top: return L"top";
    case zsdyn::Side::Right: return L"right";
    case zsdyn::Side::Bottom: return L"bottom";
    }
    return L"";
}

std::wstring OptionTitle(const zsdyn::DropOption& option, int tilesInScope) {
    switch (option.kind) {
    case zsdyn::DropKind::Fill: return L"Fill monitor";
    case zsdyn::DropKind::Swap: return L"Swap";
    case zsdyn::DropKind::Stack: return L"Stack";
    case zsdyn::DropKind::Return: return L"Return to tile";
    case zsdyn::DropKind::Split: break;
    }
    std::wstring scope;
    if (option.wholeMonitor) scope = option.level > 0 ? L"Split whole monitor" : L"Split monitor";
    else if (option.level == 0) scope = L"Split tile";
    else scope = L"Split " + std::to_wstring(tilesInScope) + L" tiles";
    return scope + L" - " + SideName(option.side);
}

}  // namespace

void PaintDynamicOverlay(const AppState& app, HDC dc, RECT client) {
    const RECT work = app.monitorWork;
    const double sx = static_cast<double>(client.right) / std::max(1L, work.right - work.left);
    const double sy = static_cast<double>(client.bottom) / std::max(1L, work.bottom - work.top);
    const auto toClient = [&](const zsdyn::Rect& r, int inset) {
        return RECT{static_cast<LONG>(std::lround((r.left - work.left) * sx)) + inset,
                    static_cast<LONG>(std::lround((r.top - work.top) * sy)) + inset,
                    static_cast<LONG>(std::lround((r.right - work.left) * sx)) - inset,
                    static_cast<LONG>(std::lround((r.bottom - work.top) * sy)) - inset};
    };
    const auto frame = [&](const RECT& r, COLORREF color, int width) {
        HPEN pen = CreatePen(PS_SOLID, width, color);
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
        Rectangle(dc, r.left, r.top, r.right, r.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    };
    const auto fill = [&](const RECT& r, COLORREF color) {
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(dc, &r, brush);
        DeleteObject(brush);
    };

    zsdyn::Tree current;
    if (const auto it = app.dynamicMonitors.find(app.currentMonitorKey); it != app.dynamicMonitors.end())
        current = OnScreen(it->second.tree);

    const DynamicDropPlan& plan = app.dynamicPreview;
    const bool valid = plan.valid && plan.key == app.currentMonitorKey;
    const WindowId dragged = Id(plan.window);

    // Every tile of the layout that will exist after the drop. The
    // rectangles come from the same tree the drop commits.
    RECT destination{};
    const auto leaves = zsdyn::Leaves(valid ? OnScreen(plan.result) : current, ToModel(work));
    for (const auto& leaf : leaves) {
        const bool incoming = valid &&
            std::find(leaf.windows.begin(), leaf.windows.end(), dragged) != leaf.windows.end();
        const RECT r = toClient(leaf.rect, 6);
        if (incoming) destination = r;
        fill(r, incoming ? ScopeColor(plan.option) : RGB(65, 79, 105));
        frame(r, incoming ? RGB(235, 245, 255) : RGB(135, 155, 185), incoming ? 4 : 2);
    }
    if (!valid) return;

    // The region the chosen option acts on, in today's geometry, so a split
    // of one tile and a split of the whole monitor look different.
    int tilesInScope = 0;
    for (const auto& leaf : zsdyn::Leaves(current, ToModel(work))) {
        const auto& s = plan.option.scope;
        if (leaf.rect.left >= s.left && leaf.rect.right <= s.right &&
            leaf.rect.top >= s.top && leaf.rect.bottom <= s.bottom)
            ++tilesInScope;
    }
    frame(toClient(plan.option.scope, 2), ScopeColor(plan.option), 6);

    HFONT titleFont = CreateFontW(app.compactMode ? 16 : 30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT hintFont = CreateFontW(app.compactMode ? 13 : 20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, titleFont));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    SetTextAlign(dc, TA_LEFT | TA_TOP);

    const std::wstring title = OptionTitle(plan.option, tilesInScope);
    RECT text = destination;
    const int centreY = (destination.top + destination.bottom) / 2;
    text.top = centreY - (app.compactMode ? 18 : 34);
    text.bottom = centreY + 4;
    DrawTextW(dc, title.c_str(), -1, &text, DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (plan.optionCount > 1) {
        SelectObject(dc, hintFont);
        const std::wstring hint = L"Middle-click: option " + std::to_wstring(plan.optionIndex + 1) +
                                  L" of " + std::to_wstring(plan.optionCount);
        text.top = centreY + 6;
        text.bottom = centreY + (app.compactMode ? 24 : 36);
        DrawTextW(dc, hint.c_str(), -1, &text, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    SelectObject(dc, oldFont);
    DeleteObject(titleFont);
    DeleteObject(hintFont);
}

bool StartDynamicDividerDrag(AppState& app, POINT cursor) {
    if (app.paused) return false;
    const std::wstring key = KeyForPoint(cursor);
    const auto it = app.dynamicMonitors.find(key);
    RECT work{};
    if (!IsDynamicKey(app, key) || it == app.dynamicMonitors.end() || !WorkForKey(key, work))
        return false;
    // Hit-test what is on screen; ratios are written to the same split ids
    // in the full tree, so hidden windows keep their place.
    const auto divider = zsdyn::DividerAt(OnScreen(it->second.tree), ToModel(work), {cursor.x, cursor.y}, 30);
    if (!divider) return false;
    app.dynamicDividerMonitor = key;
    app.dynamicDividerNode = divider->id;
    app.dynamicDividerBounds = ToWin(divider->bounds);
    ++app.dynamicDividerCaptureCount;
    if (app.dividerGuideWindow) InvalidateRect(app.dividerGuideWindow, nullptr, FALSE);
    return true;
}

bool MoveDynamicDividerDrag(AppState& app, POINT cursor) {
    if (app.dynamicDividerNode < 0) return false;
    const auto it = app.dynamicMonitors.find(app.dynamicDividerMonitor);
    if (it == app.dynamicMonitors.end()) return false;
    const auto* node = zsdyn::FindNode(it->second.tree, app.dynamicDividerNode);
    if (!node || node->IsLeaf()) return false;
    const RECT& bounds = app.dynamicDividerBounds;
    const bool vertical = node->vertical;
    const LONG span = std::max(1L, vertical ? bounds.right - bounds.left : bounds.bottom - bounds.top);
    const LONG origin = vertical ? bounds.left : bounds.top;
    const LONG position = vertical ? cursor.x : cursor.y;
    auto updated = zsdyn::SetRatio(it->second.tree, app.dynamicDividerNode,
                                   static_cast<double>(position - origin) / span);
    if (!updated) return false;
    it->second.tree = std::move(*updated);
    ++it->second.revision;
    app.dynamicDividerApplying = true;
    ReflowMonitor(app, app.dynamicDividerMonitor, true);
    app.dynamicDividerApplying = false;
    ++app.dynamicDividerApplyCount;
    return true;
}

void EndDynamicDividerDrag(AppState& app) {
    app.dynamicDividerNode = -1;
    app.dynamicDividerMonitor.clear();
    if (app.dividerGuideWindow) InvalidateRect(app.dividerGuideWindow, nullptr, FALSE);
}

namespace {
constexpr wchar_t kDividerGuideClass[] = L"ZoneSmithDividerGuide";

LRESULT CALLBACK DividerGuideProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const auto* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        RECT bounds{};
        GetWindowRect(hwnd, &bounds);
        if (app) {
            const auto it = app->dynamicMonitors.find(app->dividerGuideMonitor);
            if (it != app->dynamicMonitors.end()) {
                const int active = app->dynamicDividerMonitor == app->dividerGuideMonitor
                    ? app->dynamicDividerNode : -1;
                for (const auto& d : zsdyn::Dividers(OnScreen(it->second.tree), ToModel(bounds))) {
                    RECT bar = d.vertical
                        ? RECT{d.position - 4, d.bounds.top + 4, d.position + 4, d.bounds.bottom - 4}
                        : RECT{d.bounds.left + 4, d.position - 4, d.bounds.right - 4, d.position + 4};
                    OffsetRect(&bar, -bounds.left, -bounds.top);
                    HBRUSH brush = CreateSolidBrush(d.id == active ? RGB(255, 184, 35) : RGB(35, 214, 239));
                    FillRect(dc, &bar, brush);
                    DeleteObject(brush);
                }
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
}  // namespace

void ShowDynamicDividerGuides(AppState& app, bool enabled) {
    const auto hide = [&] {
        app.dividerGuideVisible = false;
        app.dividerGuideMonitor.clear();
        if (app.dividerGuideWindow) ShowWindow(app.dividerGuideWindow, SW_HIDE);
    };
    // Keep the guides on the dragged divider's monitor for the whole drag.
    const std::wstring key = app.dynamicDividerNode >= 0 ? app.dynamicDividerMonitor : KeyForPoint(app.cursor);
    const auto it = app.dynamicMonitors.find(key);
    RECT bounds{};
    if (!enabled || app.paused || !IsDynamicKey(app, key) || it == app.dynamicMonitors.end() ||
        it->second.tree.Empty() || !WorkForKey(key, bounds)) {
        if (app.dividerGuideVisible) hide();
        return;
    }
    if (app.dividerGuideVisible && app.dividerGuideMonitor == key) return;
    if (!app.dividerGuideWindow) {
        WNDCLASSW cls{};
        cls.hInstance = app.instance;
        cls.lpfnWndProc = DividerGuideProc;
        cls.lpszClassName = kDividerGuideClass;
        RegisterClassW(&cls);
        app.dividerGuideWindow = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            kDividerGuideClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, app.instance, nullptr);
        if (!app.dividerGuideWindow) return;
        SetWindowLongPtrW(app.dividerGuideWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));
        SetLayeredWindowAttributes(app.dividerGuideWindow, RGB(0, 0, 0), 0, LWA_COLORKEY);
    }
    app.dividerGuideMonitor = key;
    SetWindowPos(app.dividerGuideWindow, HWND_TOPMOST, bounds.left, bounds.top,
                 bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    app.dividerGuideVisible = true;
    InvalidateRect(app.dividerGuideWindow, nullptr, FALSE);
}

void DestroyDynamicDividerGuides(AppState& app) {
    if (app.dividerGuideWindow) {
        DestroyWindow(app.dividerGuideWindow);
        app.dividerGuideWindow = nullptr;
    }
    app.dividerGuideVisible = false;
    if (app.instance) UnregisterClassW(kDividerGuideClass, app.instance);
}

namespace {
void CALLBACK DynamicWindowEventHook(HWINEVENTHOOK, DWORD, HWND window, LONG objectId, LONG childId,
                                     DWORD, DWORD) {
    if (!g_app || !window || objectId != OBJID_WINDOW || childId != CHILDID_SELF) return;
    if (!IsDynamicTiled(*g_app, window)) return;
    // Coalesce bursts and let minimize/restore finish before reading state.
    SetTimer(g_app->overlay, kDynamicWindowEventTimerId, 60, nullptr);
}
}  // namespace

bool InstallDynamicWindowHooks(AppState& app) {
    const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    app.dynamicMinimizeHook = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND,
                                              nullptr, DynamicWindowEventHook, 0, 0, flags);
    app.dynamicObjectHook = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr,
                                            DynamicWindowEventHook, 0, 0, flags);
    app.dynamicCloakHook = SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr,
                                           DynamicWindowEventHook, 0, 0, flags);
    return app.dynamicMinimizeHook && app.dynamicObjectHook && app.dynamicCloakHook;
}

void RemoveDynamicWindowHooks(AppState& app) {
    for (HWINEVENTHOOK* hook : {&app.dynamicMinimizeHook, &app.dynamicObjectHook, &app.dynamicCloakHook}) {
        if (*hook) UnhookWinEvent(*hook);
        *hook = nullptr;
    }
    if (app.overlay) KillTimer(app.overlay, kDynamicWindowEventTimerId);
}

void ReconcileDynamicWindowStates(AppState& app) {
    // Never move windows under an active gesture or before a queued drop.
    if (app.dynamicDividerNode >= 0 || app.dynamicReflowApplying || app.dragWindow ||
        app.pendingDynamicDrop.window || app.pendingDynamicUndock)
        return;
    for (auto& [key, state] : app.dynamicMonitors) {
        auto pruned = zsdyn::RemoveWindows(state.tree, IsDeadWindow);
        if (!(pruned == state.tree)) {
            state.tree = std::move(pruned);
            ++state.revision;
        }
        if (!IsDynamicKey(app, key)) {
            // Dormant while the monitor uses a static layout; re-applied when
            // Dynamic is selected again.
            state.applied.clear();
            continue;
        }
        ReflowMonitor(app, key, false);
    }
}
