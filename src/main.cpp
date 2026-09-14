#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr wchar_t kOverlayClass[] = L"ZoneSmithOverlay";
constexpr int kMaxZones = 9;
constexpr int kQuitHotkeyId = 1;
constexpr UINT_PTR kSnapTimerId = 1;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayInstructionsId = 1001;
constexpr UINT kTrayExitId = 1002;

struct SavedWindow {
    RECT rect{};
    bool maximized{};
};

struct PendingSnap {
    HWND window{};
    RECT target{};
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
int g_hotZone{-1};
unsigned int g_selectedZones{};
std::vector<Layout> g_layouts;
size_t g_layoutIndex{};
std::array<bool, kMaxZones> g_numberKeyDown{};
std::unordered_map<HWND, SavedWindow> g_savedWindows;
PendingSnap g_pendingSnap{};
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

void AddFallbackLayout() {
    g_layouts.push_back(Layout{L"Three Columns", {
        {0.0, 0.0, 33.333, 100.0},
        {33.333, 0.0, 66.667, 100.0},
        {66.667, 0.0, 100.0, 100.0},
    }});
}

void LoadLayouts() {
    const std::wstring path = LayoutFilePath().wstring();
    std::array<wchar_t, 8192> sectionNames{};
    GetPrivateProfileSectionNamesW(sectionNames.data(),
                                   static_cast<DWORD>(sectionNames.size()), path.c_str());

    for (const wchar_t* section = sectionNames.data(); *section;
         section += wcslen(section) + 1) {
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

RECT VisibleWindowRect(HWND window) {
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS,
                                     &rect, sizeof(rect)))) {
        GetWindowRect(window, &rect);
    }
    return rect;
}

HWND WindowWhoseTitleBarIsAt(POINT point) {
    HWND window = GetAncestor(WindowFromPoint(point), GA_ROOT);
    if (!window || window == g_overlay || !IsWindowVisible(window)) return nullptr;

    DWORD_PTR hitTest{};
    const LPARAM coordinates = MAKELPARAM(static_cast<SHORT>(point.x),
                                          static_cast<SHORT>(point.y));
    if (!SendMessageTimeoutW(window, WM_NCHITTEST, 0, coordinates,
                             SMTO_ABORTIFHUNG, 50, &hitTest)) {
        return nullptr;
    }
    return static_cast<LRESULT>(hitTest) == HTCAPTION ? window : nullptr;
}

int ZoneAt(POINT point) {
    const RECT& selectionArea = g_compactMode ? g_overlayBounds : g_monitorWork;
    if (!PtInRect(&selectionArea, point)) return -1;
    for (size_t zone = 0; zone < CurrentLayout().zones.size(); ++zone) {
        const auto& normalized = CurrentLayout().zones[zone];
        const double width = selectionArea.right - selectionArea.left;
        const double height = selectionArea.bottom - selectionArea.top;
        RECT rect{
            selectionArea.left + static_cast<LONG>(std::round(width * normalized.left / 100.0)),
            selectionArea.top + static_cast<LONG>(std::round(height * normalized.top / 100.0)),
            selectionArea.left + static_cast<LONG>(std::round(width * normalized.right / 100.0)),
            selectionArea.top + static_cast<LONG>(std::round(height * normalized.bottom / 100.0)),
        };
        if (PtInRect(&rect, point)) return static_cast<int>(zone);
    }
    return -1;
}

RECT ZoneRect(int zone) {
    const auto& normalized = CurrentLayout().zones[zone];
    const double width = g_monitorWork.right - g_monitorWork.left;
    const double height = g_monitorWork.bottom - g_monitorWork.top;
    return RECT{
        g_monitorWork.left + static_cast<LONG>(std::round(width * normalized.left / 100.0)),
        g_monitorWork.top + static_cast<LONG>(std::round(height * normalized.top / 100.0)),
        g_monitorWork.left + static_cast<LONG>(std::round(width * normalized.right / 100.0)),
        g_monitorWork.top + static_cast<LONG>(std::round(height * normalized.bottom / 100.0)),
    };
}

RECT SelectedZonesRect() {
    RECT result{LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN};
    for (int zone = 0; zone < static_cast<int>(CurrentLayout().zones.size()); ++zone) {
        if ((g_selectedZones & (1u << zone)) == 0) continue;
        const RECT rect = ZoneRect(zone);
        result.left = std::min(result.left, rect.left);
        result.top = std::min(result.top, rect.top);
        result.right = std::max(result.right, rect.right);
        result.bottom = std::max(result.bottom, rect.bottom);
    }
    return result;
}

void CycleLayout(int direction) {
    if (g_layouts.empty()) return;
    const auto count = static_cast<int>(g_layouts.size());
    g_layoutIndex = static_cast<size_t>((static_cast<int>(g_layoutIndex) + direction + count) % count);
    g_selectedZones = 0;
    ShowOverlay(g_cursor);
}

void SelectMonitor(POINT point) {
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &info);
    g_monitorWork = info.rcWork;
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
    g_hotZone = ZoneAt(point);
    SetWindowPos(g_overlay, HWND_TOPMOST, g_overlayBounds.left, g_overlayBounds.top,
                 g_overlayBounds.right - g_overlayBounds.left,
                 g_overlayBounds.bottom - g_overlayBounds.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g_overlayVisible = true;
    InvalidateRect(g_overlay, nullptr, TRUE);
}

void HideOverlay() {
    g_overlayVisible = false;
    g_hotZone = -1;
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
    AppendMenuW(menu, MF_STRING, kTrayInstructionsId, L"How to use ZoneSmith");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExitId, L"Exit ZoneSmith");
    SetForegroundWindow(g_overlay);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   cursor.x, cursor.y, 0, g_overlay, nullptr);
    DestroyMenu(menu);
}

void RestoreBeforeDrag(HWND window, POINT cursor) {
    const auto found = g_savedWindows.find(window);
    if (found == g_savedWindows.end()) return;

    const RECT current = VisibleWindowRect(window);
    const LONG currentWidth = std::max(1L, current.right - current.left);
    const int savedWidth = found->second.rect.right - found->second.rect.left;
    const int savedHeight = found->second.rect.bottom - found->second.rect.top;
    const double horizontalRatio = static_cast<double>(cursor.x - current.left) / currentWidth;
    const int newLeft = cursor.x - static_cast<int>(std::round(horizontalRatio * savedWidth));
    const int newTop = cursor.y - std::min(cursor.y - current.top, 40L);

    if (IsZoomed(window)) ShowWindow(window, SW_RESTORE);
    SetWindowPos(window, nullptr, newLeft, newTop, savedWidth, savedHeight,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    g_savedWindows.erase(found);
}

void QueueSnap() {
    if (!g_dragWindow || !IsWindow(g_dragWindow) ||
        (g_selectedZones == 0 && g_hotZone < 0)) return;

    if (!g_savedWindows.contains(g_dragWindow)) {
        const WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
        WINDOWPLACEMENT current = placement;
        GetWindowPlacement(g_dragWindow, &current);
        const RECT rect = VisibleWindowRect(g_dragWindow);
        g_savedWindows.emplace(g_dragWindow, SavedWindow{rect, current.showCmd == SW_SHOWMAXIMIZED});
    }

    const RECT target = g_selectedZones != 0 ? SelectedZonesRect() : ZoneRect(g_hotZone);
    g_pendingSnap = PendingSnap{g_dragWindow, target};
    // The shell applies one final move after WM_LBUTTONUP. Run the snap just after
    // that native move loop completes so our target rectangle wins.
    SetTimer(g_overlay, kSnapTimerId, 75, nullptr);
}

void ApplyPendingSnap() {
    KillTimer(g_overlay, kSnapTimerId);
    if (!g_pendingSnap.window || !IsWindow(g_pendingSnap.window)) {
        g_pendingSnap = {};
        return;
    }

    if (IsZoomed(g_pendingSnap.window)) ShowWindow(g_pendingSnap.window, SW_RESTORE);
    const RECT target = g_pendingSnap.target;
    SetWindowPos(g_pendingSnap.window, nullptr, target.left, target.top,
                 target.right - target.left, target.bottom - target.top,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    g_pendingSnap = {};
}

void CALLBACK MoveSizeEventHook(HWINEVENTHOOK, DWORD event, HWND window,
                                LONG objectId, LONG, DWORD, DWORD) {
    if (objectId != OBJID_WINDOW || !window || window == g_overlay) return;

    if (event == EVENT_SYSTEM_MOVESIZESTART) {
        // This event is emitted only when Windows enters its native top-level
        // move/resize loop (title bar, caption, or non-client sizing handle).
        g_dragWindow = GetAncestor(window, GA_ROOT);
    } else if (event == EVENT_SYSTEM_MOVESIZEEND && !g_leftDown) {
        g_dragWindow = nullptr;
        HideOverlay();
    }
}

LRESULT CALLBACK MouseHook(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        const auto* event = reinterpret_cast<MSLLHOOKSTRUCT*>(data);
        g_cursor = event->pt;

        if (message == WM_LBUTTONDOWN) {
            g_leftDown = true;
            // Restore before the target receives the click. Its native move loop
            // will therefore start with the remembered dimensions instead of
            // fighting a resize performed in the middle of the drag.
            if (HWND titleBarWindow = WindowWhoseTitleBarIsAt(event->pt);
                titleBarWindow && g_savedWindows.contains(titleBarWindow)) {
                RestoreBeforeDrag(titleBarWindow, event->pt);
            }
        } else if (message == WM_MOUSEMOVE && g_leftDown) {
            if (g_overlayVisible) {
                HMONITOR oldMonitor = MonitorFromRect(&g_monitorWork, MONITOR_DEFAULTTONEAREST);
                HMONITOR newMonitor = MonitorFromPoint(event->pt, MONITOR_DEFAULTTONEAREST);
                if (oldMonitor != newMonitor) ShowOverlay(event->pt);
                const int zone = ZoneAt(event->pt);
                if (zone != g_hotZone) {
                    g_hotZone = zone;
                    InvalidateRect(g_overlay, nullptr, TRUE);
                }
            }
        } else if (message == WM_MOUSEWHEEL && g_leftDown && g_dragWindow &&
                   g_overlayVisible) {
            const SHORT wheelDelta = static_cast<SHORT>(HIWORD(event->mouseData));
            if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) {
                CycleLayout(wheelDelta > 0 ? 1 : -1);
            } else {
                const int zone = ZoneAt(event->pt);
                if (zone >= 0) {
                if (wheelDelta > 0) {
                    g_selectedZones |= 1u << zone;
                } else if (wheelDelta < 0) {
                    g_selectedZones &= ~(1u << zone);
                }
                g_hotZone = zone;
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
            if (g_overlayVisible) QueueSnap();
            HideOverlay();
            g_leftDown = false;
            g_dragWindow = nullptr;
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
                    g_hotZone = zoneNumber;
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
        const bool hovered = zone == g_hotZone;
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
            ApplyPendingSnap();
            return 0;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == kTrayInstructionsId) {
            MessageBoxW(nullptr, StartupMessage().c_str(), L"ZoneSmith POC",
                        MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
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
           L"\n\nPress Ctrl+Alt+Q to quit.";
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_instance = instance;
    LoadLayouts();

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
