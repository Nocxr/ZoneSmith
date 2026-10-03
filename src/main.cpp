#include "zonesmith/App.h"
#include "zonesmith/Config.h"
#include "zonesmith/Overlay.h"
#include "zonesmith/WindowCycle.h"
#include <shellapi.h>
#include <cwchar>

namespace {
AppState state{};
NOTIFYICONDATAW tray{};
UINT taskbarCreated = 0;
void AddTray(HWND window) {
    tray.cbSize = sizeof(tray); tray.hWnd = window; tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; tray.uCallbackMessage = kTrayMessage;
    tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(tray.szTip, _countof(tray.szTip), L"ZoneSmith");
    Shell_NotifyIconW(NIM_ADD, &tray);
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (taskbarCreated && message == taskbarCreated) { AddTray(window); return 0; }
    if (message == kTrayMessage && (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU)) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (state.paused ? MF_CHECKED : 0), kTrayPauseId, L"Paused");
        AppendMenuW(menu, MF_STRING, kTrayLayoutsId, L"Edit layouts");
        AppendMenuW(menu, MF_STRING, 1100, L"Reload layouts");
        AppendMenuW(menu, MF_STRING | (state.startWithWindows ? MF_CHECKED : 0), kTrayStartupId, L"Start with Windows");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_STRING, kTrayExitId, L"Exit");
        POINT point{}; GetCursorPos(&point); SetForegroundWindow(window);
        const UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
        DestroyMenu(menu); PostMessageW(window, WM_NULL, 0, 0);
        switch (choice) {
            case kTrayPauseId:
                state.paused = !state.paused;
                if (state.paused) { HideOverlay(state); ResetWindowCycle(state); state.leftDown = false; state.dragWindow = nullptr; state.selectedZones = 0; state.hotZones = 0; }
                break;
            case kTrayLayoutsId: ShellExecuteW(window, L"open", LayoutFilePath().c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
            case 1100: SaveCurrentMonitorLayout(state); LoadLayouts(state); HideOverlay(state); ResetWindowCycle(state); break;
            case kTrayStartupId: state.startWithWindows = !state.startWithWindows; ApplyStartupSetting(state); break;
            case kTrayExitId: DestroyWindow(window); break;
        }
        return 0;
    }
    if (message == WM_DESTROY) { Shell_NotifyIconW(NIM_DELETE, &tray); PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ZoneSmithStandalone");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) { if (mutex) CloseHandle(mutex); return 0; }
    WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.hInstance = instance; cls.lpfnWndProc = WindowProc; cls.lpszClassName = L"ZoneSmithTrayHost";
    if (!RegisterClassExW(&cls)) { CloseHandle(mutex); return 1; }
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"ZoneSmith", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!window) { CloseHandle(mutex); return 1; }
    if (!InitializeZoneSmith(state, instance)) { DestroyWindow(window); CloseHandle(mutex); return 1; }
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated"); AddTray(window);
    MSG message{}; int result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    Shell_NotifyIconW(NIM_DELETE, &tray); ShutdownZoneSmith(state); CloseHandle(mutex);
    return result < 0 ? 1 : 0;
}
