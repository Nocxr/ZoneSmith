#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr wchar_t kOverlayClass[] = L"ZoneSmithOverlay";
constexpr int kMaxZones = 9;
constexpr int kQuitHotkeyId = 1;
constexpr UINT_PTR kSnapTimerId = 1;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayInstructionsId = 1001;
constexpr UINT kTrayLayoutsId = 1002;
constexpr UINT kTrayPauseId = 1003;
constexpr UINT kTrayStartupId = 1004;
constexpr UINT kTrayExitId = 1005;

struct SavedWindow {
    RECT rect{};
    bool maximized{};
};

struct PendingSnap {
    HWND window{};
    RECT target{};
};

struct PendingRestore {
    HWND window{};
    SIZE size{};
    POINT cursor{};
};

struct NormalizedRect {
    double left{};
    double top{};
    double right{};
    double bottom{};
};

struct Layout {
    std::wstring name;
    std::vector<NormalizedRect> zones;
};

HINSTANCE g_instance{};
HWND g_overlay{};
HHOOK g_mouseHook{};
HHOOK g_keyboardHook{};
HWINEVENTHOOK g_moveSizeHook{};
HWND g_dragWindow{};
POINT g_cursor{};
RECT g_monitorWork{};
RECT g_overlayBounds{};
bool g_leftDown{};
bool g_overlayVisible{};
bool g_swallowRightUp{};
bool g_compactMode{};
bool g_backtickDown{};
unsigned int g_hotZones{};
unsigned int g_selectedZones{};
std::vector<Layout> g_layouts;
size_t g_layoutIndex{};
std::array<bool, kMaxZones> g_numberKeyDown{};
std::vector<HWND> g_cycleWindows;
int g_cycleIndex{-1};
bool g_windowCycleActive{};
bool g_windowCycleNeedsRetarget{};
bool g_leftWindowsDown{};
bool g_rightWindowsDown{};
bool g_windowsWheelUsed{};
int g_windowOverlapPercent{25};
int g_snapPadding{};
bool g_pauseInFullscreen{true};
bool g_startWithWindows{};
bool g_paused{};
std::wstring g_currentMonitorKey;
std::unordered_map<std::wstring, std::wstring> g_monitorLayouts;
std::unordered_set<std::wstring> g_excludedApps;
std::unordered_map<HWND, SavedWindow> g_savedWindows;
PendingSnap g_pendingSnap{};
PendingRestore g_pendingRestore{};
HWND g_restoreOnReleaseWindow{};
NOTIFYICONDATAW g_trayIcon{};

std::wstring StartupMessage();
void ShowOverlay(POINT point);

const Layout& CurrentLayout() {
    return g_layouts[g_layoutIndex];
}

std::filesystem::path LayoutFilePath() {
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return std::filesystem::path(std::wstring(path.data(), length)).replace_filename(L"layouts.ini");
}

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t character) { return static_cast<wchar_t>(towlower(character)); });
    return value;
}

bool ReadBoolSetting(const wchar_t* key, bool fallback, const std::wstring& path) {
    std::array<wchar_t, 32> value{};
    GetPrivateProfileStringW(L"Settings", key, fallback ? L"true" : L"false",
                             value.data(), static_cast<DWORD>(value.size()), path.c_str());
    const std::wstring normalized = Lowercase(value.data());
    return normalized == L"true" || normalized == L"yes" || normalized == L"on" ||
           normalized == L"1";
}

std::vector<std::pair<std::wstring, std::wstring>> ReadIniSection(
    const wchar_t* section, const std::wstring& path) {
    std::array<wchar_t, 16384> values{};
    GetPrivateProfileSectionW(section, values.data(), static_cast<DWORD>(values.size()),
                              path.c_str());
    std::vector<std::pair<std::wstring, std::wstring>> result;
    for (const wchar_t* entry = values.data(); *entry; entry += wcslen(entry) + 1) {
        const std::wstring line = entry;
        const size_t separator = line.find(L'=');
        if (separator != std::wstring::npos) {
            result.emplace_back(line.substr(0, separator), line.substr(separator + 1));
        }
    }
    return result;
}

void AddFallbackLayout() {
    g_layouts.push_back(Layout{L"Three Columns", {
        {0.0, 0.0, 33.333, 100.0},
        {33.333, 0.0, 66.667, 100.0},
        {66.667, 0.0, 100.0, 100.0},
    }});
}

void LoadLayouts() {
    const std::wstring path = LayoutFilePath().wstring();
    g_windowOverlapPercent = std::clamp(
        GetPrivateProfileIntW(L"Settings", L"windowOverlapPercent", 25, path.c_str()),
        0u, 100u);
    g_snapPadding = std::clamp(
        GetPrivateProfileIntW(L"Settings", L"padding", 0, path.c_str()),
        0u, 500u);
    g_pauseInFullscreen = ReadBoolSetting(L"pauseInFullscreen", true, path);
    g_startWithWindows = ReadBoolSetting(L"startWithWindows", false, path);

    for (const auto& [name, enabled] : ReadIniSection(L"ExcludedApps", path)) {
        const std::wstring normalized = Lowercase(enabled);
        if (normalized != L"0" && normalized != L"false" && normalized != L"off") {
            g_excludedApps.insert(Lowercase(std::filesystem::path(name).filename().wstring()));
        }
    }
    for (const auto& [monitor, layout] : ReadIniSection(L"MonitorLayouts", path)) {
        if (!monitor.empty() && !layout.empty()) g_monitorLayouts[Lowercase(monitor)] = layout;
    }
    std::array<wchar_t, 8192> sectionNames{};
    GetPrivateProfileSectionNamesW(sectionNames.data(),
                                   static_cast<DWORD>(sectionNames.size()), path.c_str());

    for (const wchar_t* section = sectionNames.data(); *section;
         section += wcslen(section) + 1) {
        if (_wcsicmp(section, L"Settings") == 0 ||
            _wcsicmp(section, L"ExcludedApps") == 0 ||
            _wcsicmp(section, L"MonitorLayouts") == 0) {
            continue;
        }
        Layout layout;
        layout.name = section;
        for (int index = 1; index <= kMaxZones; ++index) {
            const std::wstring key = L"zone" + std::to_wstring(index);
            std::array<wchar_t, 256> value{};
            GetPrivateProfileStringW(section, key.c_str(), L"", value.data(),
                                     static_cast<DWORD>(value.size()), path.c_str());
            if (value[0] == L'\0') continue;

            std::wstring coordinates = value.data();
            std::replace(coordinates.begin(), coordinates.end(), L',', L' ');
            std::wistringstream stream(coordinates);
            NormalizedRect zone{};
            if (stream >> zone.left >> zone.top >> zone.right >> zone.bottom &&
                zone.left >= 0.0 && zone.top >= 0.0 && zone.right <= 100.0 &&
                zone.bottom <= 100.0 && zone.right > zone.left && zone.bottom > zone.top) {
                layout.zones.push_back(zone);
            }
        }
        if (!layout.zones.empty()) g_layouts.push_back(std::move(layout));
    }
    if (g_layouts.empty()) AddFallbackLayout();
}

void ApplyStartupSetting() {
    constexpr wchar_t runKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, runKey, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    if (g_startWithWindows) {
        const std::wstring command = L"\"" +
            std::filesystem::absolute(LayoutFilePath().replace_filename(L"ZoneSmith.exe")).wstring() +
            L"\"";
        RegSetValueExW(key, L"ZoneSmith", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, L"ZoneSmith");
    }
    RegCloseKey(key);
}

RECT VisibleWindowRect(HWND window) {
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS,
                                     &rect, sizeof(rect)))) {
        GetWindowRect(window, &rect);
    }
    return rect;
}

std::wstring WindowExecutableName(HWND window) {
    DWORD processId{};
    GetWindowThreadProcessId(window, &processId);
    if (!processId) return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return {};
    std::array<wchar_t, 32768> path{};
    DWORD length = static_cast<DWORD>(path.size());
    const bool success = QueryFullProcessImageNameW(process, 0, path.data(), &length) != FALSE;
    CloseHandle(process);
    return success ? Lowercase(std::filesystem::path(std::wstring(path.data(), length)).filename().wstring())
                   : std::wstring{};
}

bool IsWindowExcluded(HWND window) {
    const std::wstring executable = WindowExecutableName(window);
    return !executable.empty() && g_excludedApps.contains(executable);
}

bool IsFullscreenWindow(HWND window) {
    if (!window || window == GetShellWindow() || !IsWindowVisible(window) || IsIconic(window)) {
        return false;
    }
    RECT rect = VisibleWindowRect(window);
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
        return false;
    }
    constexpr LONG tolerance = 2;
    return rect.left <= monitor.rcMonitor.left + tolerance &&
           rect.top <= monitor.rcMonitor.top + tolerance &&
           rect.right >= monitor.rcMonitor.right - tolerance &&
           rect.bottom >= monitor.rcMonitor.bottom - tolerance;
}

bool ShouldSuspendForForeground() {
    static HWND cachedWindow{};
    static bool cachedExcluded{};
    HWND foreground = GetForegroundWindow();
    if (foreground != cachedWindow) {
        cachedWindow = foreground;
        cachedExcluded = IsWindowExcluded(foreground);
    }
    return g_paused || cachedExcluded ||
           (g_pauseInFullscreen && IsFullscreenWindow(foreground));
}

RECT ZoneRectInArea(int zone, const RECT& area) {
    const auto& normalized = CurrentLayout().zones[zone];
    const double width = area.right - area.left;
    const double height = area.bottom - area.top;
    return RECT{
        area.left + static_cast<LONG>(std::round(width * normalized.left / 100.0)),
        area.top + static_cast<LONG>(std::round(height * normalized.top / 100.0)),
        area.left + static_cast<LONG>(std::round(width * normalized.right / 100.0)),
        area.top + static_cast<LONG>(std::round(height * normalized.bottom / 100.0)),
    };
}

RECT ZoneRect(int zone) {
    return ZoneRectInArea(zone, g_monitorWork);
}

unsigned int ZonesAt(POINT point) {
    const RECT& selectionArea = g_compactMode ? g_overlayBounds : g_monitorWork;
    if (!PtInRect(&selectionArea, point)) return 0;
    const int tolerance = g_compactMode ? 6 : 10;
    unsigned int zones = 0;
    for (int zone = 0; zone < static_cast<int>(CurrentLayout().zones.size()); ++zone) {
        RECT hitArea = ZoneRectInArea(zone, selectionArea);
        InflateRect(&hitArea, tolerance, tolerance);
        if (PtInRect(&hitArea, point)) zones |= 1u << zone;
    }
    return zones;
}

RECT ZonesRect(unsigned int zones) {
    RECT result{LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN};
    for (int zone = 0; zone < static_cast<int>(CurrentLayout().zones.size()); ++zone) {
        if ((zones & (1u << zone)) == 0) continue;
        const RECT rect = ZoneRect(zone);
        result.left = std::min(result.left, rect.left);
        result.top = std::min(result.top, rect.top);
        result.right = std::max(result.right, rect.right);
        result.bottom = std::max(result.bottom, rect.bottom);
    }
    if (result.left != LONG_MAX && g_snapPadding > 0) {
        const LONG maxHorizontalPadding = std::max(0L, (result.right - result.left - 1) / 2);
        const LONG maxVerticalPadding = std::max(0L, (result.bottom - result.top - 1) / 2);
        const LONG horizontalPadding = std::min(static_cast<LONG>(g_snapPadding),
                                                maxHorizontalPadding);
        const LONG verticalPadding = std::min(static_cast<LONG>(g_snapPadding),
                                              maxVerticalPadding);
        result.left += horizontalPadding;
        result.right -= horizontalPadding;
        result.top += verticalPadding;
        result.bottom -= verticalPadding;
    }
    return result;
}

void CycleLayout(int direction) {
    if (g_layouts.empty()) return;
    const auto count = static_cast<int>(g_layouts.size());
    g_layoutIndex = static_cast<size_t>((static_cast<int>(g_layoutIndex) + direction + count) % count);
    if (!g_currentMonitorKey.empty()) {
        g_monitorLayouts[g_currentMonitorKey] = CurrentLayout().name;
        WritePrivateProfileStringW(L"MonitorLayouts", g_currentMonitorKey.c_str(),
                                   CurrentLayout().name.c_str(), LayoutFilePath().c_str());
    }
    g_selectedZones = 0;
    ShowOverlay(g_cursor);
}

void SelectMonitor(POINT point) {
    MONITORINFOEXW info{sizeof(info)};
    GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &info);
    g_monitorWork = info.rcWork;
    g_currentMonitorKey = info.szDevice;
    if (g_currentMonitorKey.starts_with(L"\\\\.\\")) g_currentMonitorKey.erase(0, 4);
    g_currentMonitorKey = Lowercase(g_currentMonitorKey);
    const auto saved = g_monitorLayouts.find(g_currentMonitorKey);
    if (saved != g_monitorLayouts.end()) {
        const auto layout = std::find_if(g_layouts.begin(), g_layouts.end(),
            [&](const Layout& candidate) { return _wcsicmp(candidate.name.c_str(), saved->second.c_str()) == 0; });
        if (layout != g_layouts.end()) {
            g_layoutIndex = static_cast<size_t>(std::distance(g_layouts.begin(), layout));
        }
    }
}

void ShowOverlay(POINT point) {
    SelectMonitor(point);
    if (g_compactMode) {
        const int monitorWidth = g_monitorWork.right - g_monitorWork.left;
        const int monitorHeight = g_monitorWork.bottom - g_monitorWork.top;
        const int width = std::min(420, std::max(270, monitorWidth / 5));
        const int height = std::clamp(width * monitorHeight / std::max(1, monitorWidth), 140, 240);
        int left = point.x - width / 2;
        int top = point.y - height / 2;
        left = std::clamp(left, static_cast<int>(g_monitorWork.left),
                          static_cast<int>(g_monitorWork.right) - width);
        top = std::clamp(top, static_cast<int>(g_monitorWork.top),
                         static_cast<int>(g_monitorWork.bottom) - height);
        g_overlayBounds = RECT{left, top, left + width, top + height};
    } else {
        g_overlayBounds = g_monitorWork;
    }
    g_hotZones = ZonesAt(point);
    SetWindowPos(g_overlay, HWND_TOPMOST, g_overlayBounds.left, g_overlayBounds.top,
                 g_overlayBounds.right - g_overlayBounds.left,
                 g_overlayBounds.bottom - g_overlayBounds.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g_overlayVisible = true;
    InvalidateRect(g_overlay, nullptr, TRUE);
}

void HideOverlay() {
    g_overlayVisible = false;
    g_hotZones = 0;
    ShowWindow(g_overlay, SW_HIDE);
}

void AddTrayIcon() {
    g_trayIcon = {};
    g_trayIcon.cbSize = sizeof(g_trayIcon);
    g_trayIcon.hWnd = g_overlay;
    g_trayIcon.uID = 1;
    g_trayIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    g_trayIcon.uCallbackMessage = kTrayMessage;
    g_trayIcon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_trayIcon.szTip, L"ZoneSmith - Ctrl+Alt+Q to quit");
    Shell_NotifyIconW(NIM_ADD, &g_trayIcon);
    g_trayIcon.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_trayIcon);
}

void RemoveTrayIcon() {
    if (g_trayIcon.hWnd) Shell_NotifyIconW(NIM_DELETE, &g_trayIcon);
}

void ShowTrayMenu() {
    POINT cursor{};
    GetCursorPos(&cursor);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING | (g_paused ? MF_CHECKED : MF_UNCHECKED),
                kTrayPauseId, L"Pause ZoneSmith");
    AppendMenuW(menu, MF_STRING | (g_startWithWindows ? MF_CHECKED : MF_UNCHECKED),
                kTrayStartupId, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayInstructionsId, L"How to use ZoneSmith");
    AppendMenuW(menu, MF_STRING, kTrayLayoutsId, L"Open layouts.ini");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExitId, L"Exit ZoneSmith");
    SetForegroundWindow(g_overlay);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   cursor.x, cursor.y, 0, g_overlay, nullptr);
    DestroyMenu(menu);
}

void QueueSnap() {
    if (!g_dragWindow || !IsWindow(g_dragWindow) ||
        (g_selectedZones == 0 && g_hotZones == 0)) return;

    if (!g_savedWindows.contains(g_dragWindow)) {
        const WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
        WINDOWPLACEMENT current = placement;
        GetWindowPlacement(g_dragWindow, &current);
        const RECT rect = VisibleWindowRect(g_dragWindow);
        g_savedWindows.emplace(g_dragWindow, SavedWindow{rect, current.showCmd == SW_SHOWMAXIMIZED});
    }

    const unsigned int zones = g_selectedZones != 0
        ? (g_selectedZones | g_hotZones) : g_hotZones;
    const RECT target = ZonesRect(zones);
    g_pendingSnap = PendingSnap{g_dragWindow, target};
    g_pendingRestore = {};
    g_restoreOnReleaseWindow = nullptr;
    // The shell applies one final move after WM_LBUTTONUP. Run the snap just after
    // that native move loop completes so our target rectangle wins.
    SetTimer(g_overlay, kSnapTimerId, 75, nullptr);
}

void QueueRestoreAfterMove(HWND window, POINT cursor) {
    const auto found = g_savedWindows.find(window);
    if (found == g_savedWindows.end()) return;
    g_pendingSnap = {};
    g_pendingRestore = PendingRestore{
        window,
        SIZE{found->second.rect.right - found->second.rect.left,
             found->second.rect.bottom - found->second.rect.top},
        cursor,
    };
    SetTimer(g_overlay, kSnapTimerId, 75, nullptr);
}

void ApplyPendingOperation() {
    KillTimer(g_overlay, kSnapTimerId);
    if (g_pendingSnap.window && IsWindow(g_pendingSnap.window)) {
        if (IsZoomed(g_pendingSnap.window)) ShowWindow(g_pendingSnap.window, SW_RESTORE);
        const RECT target = g_pendingSnap.target;
        SetWindowPos(g_pendingSnap.window, nullptr, target.left, target.top,
                     target.right - target.left, target.bottom - target.top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        g_pendingSnap = {};
        return;
    }
    g_pendingSnap = {};

    if (g_pendingRestore.window && IsWindow(g_pendingRestore.window)) {
        const RECT current = VisibleWindowRect(g_pendingRestore.window);
        const LONG currentWidth = std::max(1L, current.right - current.left);
        const double ratio = std::clamp(
            static_cast<double>(g_pendingRestore.cursor.x - current.left) / currentWidth,
            0.0, 1.0);
        const int left = g_pendingRestore.cursor.x -
            static_cast<int>(std::round(ratio * g_pendingRestore.size.cx));
        SetWindowPos(g_pendingRestore.window, nullptr, left, current.top,
                     g_pendingRestore.size.cx, g_pendingRestore.size.cy,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        g_savedWindows.erase(g_pendingRestore.window);
    }
    g_pendingRestore = {};
}

void CALLBACK MoveSizeEventHook(HWINEVENTHOOK, DWORD event, HWND window,
                                LONG objectId, LONG, DWORD, DWORD) {
    if (objectId != OBJID_WINDOW || !window || window == g_overlay) return;

    if (event == EVENT_SYSTEM_MOVESIZESTART) {
        if (ShouldSuspendForForeground() || IsWindowExcluded(window)) {
            g_dragWindow = nullptr;
            HideOverlay();
            return;
        }
        // This event is emitted only when Windows enters its native top-level
        // move/resize loop (title bar, caption, or non-client sizing handle).
        g_dragWindow = GetAncestor(window, GA_ROOT);
        g_restoreOnReleaseWindow = g_savedWindows.contains(g_dragWindow)
            ? g_dragWindow : nullptr;
    } else if (event == EVENT_SYSTEM_MOVESIZEEND && !g_leftDown) {
        g_dragWindow = nullptr;
        HideOverlay();
    }
}

struct WindowCycleContext {
    HWND source{};
    RECT sourceRect{};
    std::vector<HWND> windows;
};

BOOL CALLBACK CollectOverlappingWindow(HWND window, LPARAM data) {
    auto& context = *reinterpret_cast<WindowCycleContext*>(data);
    if (window == g_overlay || window == GetShellWindow() || !IsWindowVisible(window) ||
        IsIconic(window) || IsWindowExcluded(window) ||
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
        return TRUE;
    }

    DWORD cloaked{};
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
        cloaked != 0) {
        return TRUE;
    }

    RECT rect{};
    if (!GetWindowRect(window, &rect)) return TRUE;
    RECT overlap{};
    if (IntersectRect(&overlap, &context.sourceRect, &rect)) {
        const long long overlapArea =
            static_cast<long long>(overlap.right - overlap.left) * (overlap.bottom - overlap.top);
        const long long candidateArea =
            static_cast<long long>(rect.right - rect.left) * (rect.bottom - rect.top);
        if (candidateArea > 0 && overlapArea * 100 >= candidateArea * g_windowOverlapPercent) {
            context.windows.push_back(window);
        }
    }
    return TRUE;
}

bool IsCycleWindow(HWND window) {
    return window && window != g_overlay && window != GetShellWindow() &&
           IsWindowVisible(window) && !IsIconic(window) && !IsWindowExcluded(window) &&
           (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) == 0;
}

void AdvanceWindowPreview(HWND source, int direction) {
    if (!g_windowCycleActive) {
        if (!IsCycleWindow(source)) return;
    WindowCycleContext context{source, VisibleWindowRect(source), {}};
    EnumWindows(CollectOverlappingWindow, reinterpret_cast<LPARAM>(&context));
    if (context.windows.size() < 2) return;

    const auto current = std::find(context.windows.begin(), context.windows.end(), source);
        g_cycleIndex = current == context.windows.end()
        ? 0 : static_cast<int>(std::distance(context.windows.begin(), current));
        g_cycleWindows = std::move(context.windows);
        g_windowCycleActive = true;
    }

    const int count = static_cast<int>(g_cycleWindows.size());
    for (int attempt = 0; attempt < count; ++attempt) {
        g_cycleIndex = (g_cycleIndex + direction + count) % count;
        if (IsWindow(g_cycleWindows[g_cycleIndex])) {
            // Every wheel step promotes the next overlapping window immediately.
            HWND selected = g_cycleWindows[g_cycleIndex];
            SetWindowPos(selected, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            SetForegroundWindow(selected);
            BringWindowToTop(selected);
            return;
        }
    }

    g_cycleWindows.clear();
    g_cycleIndex = -1;
    g_windowCycleActive = false;
    g_windowCycleNeedsRetarget = false;
}

void CommitWindowPreview() {
    if (!g_windowCycleActive) return;
    g_cycleWindows.clear();
    g_cycleIndex = -1;
    g_windowCycleActive = false;
    g_windowCycleNeedsRetarget = false;
}

LRESULT CALLBACK MouseHook(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        const auto* event = reinterpret_cast<MSLLHOOKSTRUCT*>(data);
        g_cursor = event->pt;

        if (message == WM_MOUSEMOVE && g_windowCycleActive &&
            (g_leftWindowsDown || g_rightWindowsDown)) {
            g_windowCycleNeedsRetarget = true;
        }

        const bool suspensionRelevant = g_overlayVisible || g_windowCycleActive ||
            message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
            message == WM_MOUSEWHEEL;
        if (g_paused || (suspensionRelevant && ShouldSuspendForForeground())) {
            HideOverlay();
            g_dragWindow = nullptr;
            g_cycleWindows.clear();
            g_cycleIndex = -1;
            g_windowCycleActive = false;
            g_windowCycleNeedsRetarget = false;
            if (message == WM_LBUTTONUP) g_leftDown = false;
            return CallNextHookEx(g_mouseHook, code, message, data);
        }

        if (message == WM_MOUSEWHEEL && !g_leftDown &&
            (g_leftWindowsDown || g_rightWindowsDown)) {
            g_windowsWheelUsed = true;
            HWND source = GetAncestor(WindowFromPoint(event->pt), GA_ROOT);
            if (g_windowCycleNeedsRetarget) {
                g_cycleWindows.clear();
                g_cycleIndex = -1;
                g_windowCycleActive = false;
                g_windowCycleNeedsRetarget = false;
            }
            if (source || g_windowCycleActive) {
                const SHORT wheelDelta = static_cast<SHORT>(HIWORD(event->mouseData));
                AdvanceWindowPreview(source, wheelDelta > 0 ? 1 : -1);
                return 1;
            }
        }

        if (message == WM_LBUTTONDOWN) {
            g_leftDown = true;
        } else if (message == WM_MOUSEMOVE && g_leftDown) {
            if (g_overlayVisible) {
                HMONITOR oldMonitor = MonitorFromRect(&g_monitorWork, MONITOR_DEFAULTTONEAREST);
                HMONITOR newMonitor = MonitorFromPoint(event->pt, MONITOR_DEFAULTTONEAREST);
                if (oldMonitor != newMonitor) ShowOverlay(event->pt);
                const unsigned int zones = ZonesAt(event->pt);
                if (zones != g_hotZones) {
                    g_hotZones = zones;
                    InvalidateRect(g_overlay, nullptr, TRUE);
                }
            }
        } else if (message == WM_MOUSEWHEEL && g_leftDown && g_dragWindow &&
                   g_overlayVisible) {
            const SHORT wheelDelta = static_cast<SHORT>(HIWORD(event->mouseData));
            if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) {
                CycleLayout(wheelDelta > 0 ? 1 : -1);
            } else {
                const unsigned int zones = ZonesAt(event->pt);
                if (zones != 0) {
                if (wheelDelta > 0) {
                    g_selectedZones |= zones;
                } else if (wheelDelta < 0) {
                    g_selectedZones &= ~zones;
                }
                g_hotZones = zones;
                InvalidateRect(g_overlay, nullptr, TRUE);
                }
            }
            return 1;
        } else if (message == WM_RBUTTONDOWN && g_leftDown && g_dragWindow) {
            if (g_overlayVisible) {
                HideOverlay();
            } else {
                g_selectedZones = 0;
                ShowOverlay(event->pt);
            }
            g_swallowRightUp = true;
            return 1; // Do not open the dragged window's context menu.
        } else if (message == WM_RBUTTONUP && g_swallowRightUp) {
            g_swallowRightUp = false;
            return 1;
        } else if (message == WM_LBUTTONUP) {
            if (g_overlayVisible) {
                QueueSnap();
            } else if (g_restoreOnReleaseWindow == g_dragWindow) {
                QueueRestoreAfterMove(g_dragWindow, event->pt);
            }
            HideOverlay();
            g_leftDown = false;
            g_dragWindow = nullptr;
            g_restoreOnReleaseWindow = nullptr;
        }
    }
    return CallNextHookEx(g_mouseHook, code, message, data);
}

LRESULT CALLBACK KeyboardHook(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        const auto* event = reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
        const bool keyDown = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const bool keyUp = message == WM_KEYUP || message == WM_SYSKEYUP;
        int zoneNumber = -1;
        if (event->vkCode >= '1' && event->vkCode <= '9') {
            zoneNumber = static_cast<int>(event->vkCode - '1');
        } else if (event->vkCode >= VK_NUMPAD1 && event->vkCode <= VK_NUMPAD9) {
            zoneNumber = static_cast<int>(event->vkCode - VK_NUMPAD1);
        }
        if (event->vkCode == VK_LWIN) {
            if (keyDown) {
                if (!g_leftWindowsDown && !g_rightWindowsDown) g_windowsWheelUsed = false;
                g_leftWindowsDown = true;
            }
            if (keyUp) g_leftWindowsDown = false;
        } else if (event->vkCode == VK_RWIN) {
            if (keyDown) {
                if (!g_leftWindowsDown && !g_rightWindowsDown) g_windowsWheelUsed = false;
                g_rightWindowsDown = true;
            }
            if (keyUp) g_rightWindowsDown = false;
        }
        if (keyUp && (event->vkCode == VK_LWIN || event->vkCode == VK_RWIN) &&
            !g_leftWindowsDown && !g_rightWindowsDown && g_windowsWheelUsed) {
            if (g_windowCycleActive) CommitWindowPreview();

            // The Windows-key press was delivered to the shell, so its release
            // must also be delivered. A harmless Ctrl tap marks it as a chord
            // and prevents the Start menu from opening after the gesture.
            std::array<INPUT, 2> inputs{};
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = VK_CONTROL;
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = VK_CONTROL;
            inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
            g_windowsWheelUsed = false;
        }
        const bool suspensionRelevant = g_overlayVisible || g_windowCycleActive ||
            event->vkCode == VK_LWIN || event->vkCode == VK_RWIN ||
            event->vkCode == VK_OEM_3 || event->vkCode == VK_ESCAPE || zoneNumber >= 0;
        if (g_paused || (suspensionRelevant && ShouldSuspendForForeground())) {
            HideOverlay();
            g_cycleWindows.clear();
            g_cycleIndex = -1;
            g_windowCycleActive = false;
            g_windowCycleNeedsRetarget = false;
            return CallNextHookEx(g_keyboardHook, code, message, data);
        }
        if (zoneNumber >= 0) {
            if (keyUp && g_numberKeyDown[zoneNumber]) {
                g_numberKeyDown[zoneNumber] = false;
                return 1;
            }
            if (keyDown && g_leftDown && g_dragWindow && g_overlayVisible &&
                zoneNumber < static_cast<int>(CurrentLayout().zones.size())) {
                if (!g_numberKeyDown[zoneNumber]) {
                    g_numberKeyDown[zoneNumber] = true;
                    g_selectedZones = 0;
                    g_hotZones = 1u << zoneNumber;
                    const HWND draggedWindow = g_dragWindow;
                    PostMessageW(draggedWindow, WM_CANCELMODE, 0, 0);
                    QueueSnap();
                    HideOverlay();
                }
                return 1;
            }
        }

        if (event->vkCode == VK_OEM_3) {
            if (keyUp) g_backtickDown = false;
            if (g_leftDown && g_dragWindow && (keyDown || keyUp)) {
                if (keyDown && !g_backtickDown) {
                    g_backtickDown = true;
                    g_compactMode = !g_compactMode;
                    if (g_overlayVisible) ShowOverlay(g_cursor);
                }
                return 1;
            }
        }
        if (keyDown && event->vkCode == VK_ESCAPE && g_overlayVisible) {
            HideOverlay();
            return 1;
        }
    }
    return CallNextHookEx(g_keyboardHook, code, message, data);
}

void PaintOverlay(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);

    HBRUSH background = CreateSolidBrush(RGB(18, 24, 38));
    FillRect(dc, &client, background);
    DeleteObject(background);

    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_CENTER | TA_BASELINE);
    const int fontHeight = std::clamp(client.bottom / 3, 32L, 72L);
    HFONT font = CreateFontW(fontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));

    const double width = client.right;
    const double height = client.bottom;
    for (int zone = 0; zone < static_cast<int>(CurrentLayout().zones.size()); ++zone) {
        const bool selected = (g_selectedZones & (1u << zone)) != 0;
        const bool hovered = (g_hotZones & (1u << zone)) != 0;
        const auto& normalized = CurrentLayout().zones[zone];
        RECT rect{
            static_cast<LONG>(std::round(width * normalized.left / 100.0)) + 6,
            static_cast<LONG>(std::round(height * normalized.top / 100.0)) + 6,
            static_cast<LONG>(std::round(width * normalized.right / 100.0)) - 6,
            static_cast<LONG>(std::round(height * normalized.bottom / 100.0)) - 6,
        };
        const COLORREF fillColor = selected
            ? (hovered ? RGB(36, 185, 122) : RGB(31, 145, 96))
            : (hovered ? RGB(35, 135, 230) : RGB(65, 79, 105));
        HBRUSH fill = CreateSolidBrush(fillColor);
        FillRect(dc, &rect, fill);
        DeleteObject(fill);

        const int borderWidth = selected || hovered ? 5 : 2;
        const COLORREF borderColor = selected ? RGB(200, 255, 226)
            : (hovered ? RGB(190, 225, 255) : RGB(135, 155, 185));
        HPEN pen = CreatePen(PS_SOLID, borderWidth, borderColor);
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
        Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);

        SetTextColor(dc, RGB(255, 255, 255));
        const std::wstring number = std::to_wstring(zone + 1);
        TextOutW(dc, (rect.left + rect.right) / 2,
                 (rect.top + rect.bottom + fontHeight) / 2,
                 number.c_str(), static_cast<int>(number.size()));
    }

    HFONT labelFont = CreateFontW(g_compactMode ? 18 : 26, 0, 0, 0, FW_SEMIBOLD,
                                  FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    SelectObject(dc, labelFont);
    SetTextAlign(dc, TA_LEFT | TA_TOP);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT labelRect{12, 10, client.right - 12, client.bottom - 10};
    DrawTextW(dc, CurrentLayout().name.c_str(), -1, &labelRect,
              DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
    DeleteObject(labelFont);

    DeleteObject(font);
    EndPaint(window, &paint);
}

LRESULT CALLBACK OverlayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == kTrayMessage) {
        const UINT event = LOWORD(lParam);
        if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) ShowTrayMenu();
        if (event == NIN_SELECT || event == NIN_KEYSELECT || event == WM_LBUTTONDBLCLK) {
            MessageBoxW(nullptr, StartupMessage().c_str(), L"ZoneSmith POC",
                        MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
        }
        return 0;
    }

    switch (message) {
    case WM_PAINT:
        PaintOverlay(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_TIMER:
        if (wParam == kSnapTimerId) {
            ApplyPendingOperation();
            return 0;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == kTrayInstructionsId) {
            MessageBoxW(nullptr, StartupMessage().c_str(), L"ZoneSmith POC",
                        MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
            return 0;
        }
        if (LOWORD(wParam) == kTrayLayoutsId) {
            const std::wstring path = LayoutFilePath().wstring();
            ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (LOWORD(wParam) == kTrayPauseId) {
            g_paused = !g_paused;
            HideOverlay();
            g_cycleWindows.clear();
            g_cycleIndex = -1;
            g_windowCycleActive = false;
            g_windowCycleNeedsRetarget = false;
            return 0;
        }
        if (LOWORD(wParam) == kTrayStartupId) {
            g_startWithWindows = !g_startWithWindows;
            WritePrivateProfileStringW(L"Settings", L"startWithWindows",
                                       g_startWithWindows ? L"true" : L"false",
                                       LayoutFilePath().c_str());
            ApplyStartupSetting();
            return 0;
        }
        if (LOWORD(wParam) == kTrayExitId) {
            PostQuitMessage(0);
            return 0;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

std::wstring StartupMessage() {
    POINT point{};
    GetCursorPos(&point);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &info);
    const int width = info.rcMonitor.right - info.rcMonitor.left;
    const int height = info.rcMonitor.bottom - info.rcMonitor.top;
    return L"ZoneSmith is running.\n\nCurrent monitor: " + std::to_wstring(width) + L" x " +
           std::to_wstring(height) +
           L"\n\nLeft-drag a window, then right-click to reveal zones. "
           L"Wheel up adds a zone; wheel down removes it. Release to fit all selected zones.\n"
           L"Ctrl+wheel cycles layouts. Number keys 1-9 snap instantly.\n"
           L"Press ` during a window drag to toggle the compact zone map."
           L"\nHold Win and scroll over a window to cycle overlapping windows to the top."
           L"\n\nPress Ctrl+Alt+Q to quit.";
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_instance = instance;
    LoadLayouts();
    ApplyStartupSetting();

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = OverlayProc;
    windowClass.lpszClassName = kOverlayClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&windowClass)) return 1;

    g_overlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        kOverlayClass, L"ZoneSmith overlay", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!g_overlay) return 2;
    SetLayeredWindowAttributes(g_overlay, 0, 190, LWA_ALPHA);
    AddTrayIcon();

    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, instance, 0);
    if (!g_mouseHook) return 3;
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, instance, 0);
    if (!g_keyboardHook) {
        UnhookWindowsHookEx(g_mouseHook);
        return 4;
    }
    g_moveSizeHook = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND,
                                     nullptr, MoveSizeEventHook, 0, 0,
                                     WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!g_moveSizeHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        UnhookWindowsHookEx(g_mouseHook);
        return 5;
    }
    RegisterHotKey(nullptr, kQuitHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'Q');

    MessageBoxW(nullptr, StartupMessage().c_str(), L"ZoneSmith POC",
                MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_HOTKEY && message.wParam == kQuitHotkeyId) break;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    HideOverlay();
    RemoveTrayIcon();
    UnregisterHotKey(nullptr, kQuitHotkeyId);
    UnhookWinEvent(g_moveSizeHook);
    UnhookWindowsHookEx(g_keyboardHook);
    UnhookWindowsHookEx(g_mouseHook);
    DestroyWindow(g_overlay);
    return 0;
}
