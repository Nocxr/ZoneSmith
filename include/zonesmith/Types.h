#pragma once

#include <windows.h>

#include <string>
#include <vector>

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