#pragma once

#include "AppState.h"

#include <filesystem>
#include <string>

std::filesystem::path LayoutFilePath();
std::filesystem::path StateFilePath();

std::wstring Lowercase(std::wstring value);

bool ReadBoolSetting(
    const wchar_t* key,
    bool fallback,
    const std::wstring& path
);

void LoadLayouts(AppState& app);

void LoadMonitorLayouts(AppState& app);

void SaveCurrentMonitorLayout(AppState& app);

void SelectMonitor(
    AppState& app,
    POINT point
);

void ApplyStartupSetting(AppState& app);