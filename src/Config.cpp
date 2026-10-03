#include "Config.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace {

std::vector<std::pair<std::wstring, std::wstring>>
ReadIniSection(
    const wchar_t* section,
    const std::wstring& path
) {
    std::array<wchar_t, 16384> values{};

    GetPrivateProfileSectionW(
        section,
        values.data(),
        static_cast<DWORD>(values.size()),
        path.c_str()
    );

    std::vector<std::pair<std::wstring, std::wstring>>
        result;

    for (
        const wchar_t* entry = values.data();
        *entry;
        entry += wcslen(entry) + 1
    ) {
        const std::wstring line = entry;

        const size_t separator =
            line.find(L'=');

        if (separator == std::wstring::npos) {
            continue;
        }

        result.emplace_back(
            line.substr(0, separator),
            line.substr(separator + 1)
        );
    }

    return result;
}

std::filesystem::path ExecutablePath() {
    std::array<wchar_t, 32768> path{};

    const DWORD length =
        GetModuleFileNameW(
            nullptr,
            path.data(),
            static_cast<DWORD>(
                path.size()
            )
        );

    return std::filesystem::path(
        std::wstring(
            path.data(),
            length
        )
    );
}

std::filesystem::path ZoneSmithDataDirectory() {
    std::array<wchar_t, 32768> path{};

    const DWORD length =
        GetEnvironmentVariableW(
            L"LOCALAPPDATA",
            path.data(),
            static_cast<DWORD>(
                path.size()
            )
        );

    std::filesystem::path directory;

    if (
        length > 0 &&
        length < path.size()
    ) {
        directory =
            std::filesystem::path(
                std::wstring(
                    path.data(),
                    length
                )
            ) /
            L"ZoneSmith";
    } else {
        directory =
            ExecutablePath()
                .parent_path();
    }

    std::error_code error;

    std::filesystem::create_directories(
        directory,
        error
    );

    return directory;
}

void WriteDefaultLayoutsFile(
    const std::filesystem::path& path
) {
    std::ofstream output(
        path,
        std::ios::trunc
    );

    if (!output) {
        return;
    }

    output <<
R"([Settings]
windowOverlapPercent=25
padding=0
horizontalCycleNotches=2
pauseInFullscreen=true
startWithWindows=false

[3 Across]
zone1=0,0,33.333,100
zone2=33.333,0,66.667,100
zone3=66.667,0,100,100

[4 Across x 2 Tall]
zone1=0,0,25,50
zone2=25,0,50,50
zone3=50,0,75,50
zone4=75,0,100,50
zone5=0,50,25,100
zone6=25,50,50,100
zone7=50,50,75,100
zone8=75,50,100,100

[66 Left + 2 Tall 33]
zone1=0,0,66.667,100
zone2=66.667,0,100,50
zone3=66.667,50,100,100
)";
}

void EnsurePersistentLayoutsFile(
    const std::filesystem::path& destination
) {
    std::error_code error;

    if (
        std::filesystem::exists(
            destination,
            error
        ) &&
        !error
    ) {
        return;
    }

    const std::filesystem::path executable =
        ExecutablePath();

    std::vector<std::filesystem::path>
        legacyCandidates{
            executable
                .parent_path() /
                L"layouts.ini",
            executable
                .parent_path()
                .parent_path() /
                L"layouts.ini",
            std::filesystem::current_path(
                error
            ) /
                L"layouts.ini"
        };

    for (const auto& candidate :
         legacyCandidates)
    {
        error.clear();

        if (
            candidate == destination ||
            !std::filesystem::exists(
                candidate,
                error
            ) ||
            error
        ) {
            continue;
        }

        error.clear();

        std::filesystem::copy_file(
            candidate,
            destination,
            std::filesystem::copy_options::none,
            error
        );

        if (!error) {
            return;
        }
    }

    WriteDefaultLayoutsFile(
        destination
    );
}

void AddFallbackLayouts(AppState& app) {
    app.layouts.push_back(
        Layout{
            L"3 Across",
            {
                {0.0, 0.0, 33.333, 100.0},
                {33.333, 0.0, 66.667, 100.0},
                {66.667, 0.0, 100.0, 100.0},
            }
        }
    );

    app.layouts.push_back(
        Layout{
            L"4 Across x 2 Tall",
            {
                {0.0, 0.0, 25.0, 50.0},
                {25.0, 0.0, 50.0, 50.0},
                {50.0, 0.0, 75.0, 50.0},
                {75.0, 0.0, 100.0, 50.0},
                {0.0, 50.0, 25.0, 100.0},
                {25.0, 50.0, 50.0, 100.0},
                {50.0, 50.0, 75.0, 100.0},
                {75.0, 50.0, 100.0, 100.0},
            }
        }
    );

    app.layouts.push_back(
        Layout{
            L"66 Left + 2 Tall 33",
            {
                {0.0, 0.0, 66.667, 100.0},
                {66.667, 0.0, 100.0, 50.0},
                {66.667, 50.0, 100.0, 100.0},
            }
        }
    );
}

}

std::filesystem::path LayoutFilePath() {
    const std::filesystem::path path =
        ZoneSmithDataDirectory() /
        L"layouts.ini";

    EnsurePersistentLayoutsFile(
        path
    );

    return path;
}

std::filesystem::path StateFilePath() {
    return
        ZoneSmithDataDirectory() /
        L"state.ini";
}

std::wstring Lowercase(std::wstring value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](wchar_t character) {
            return static_cast<wchar_t>(
                towlower(character)
            );
        }
    );

    return value;
}

bool ReadBoolSetting(
    const wchar_t* key,
    bool fallback,
    const std::wstring& path
) {
    std::array<wchar_t, 32> value{};

    GetPrivateProfileStringW(
        L"Settings",
        key,
        fallback ? L"true" : L"false",
        value.data(),
        static_cast<DWORD>(value.size()),
        path.c_str()
    );

    const std::wstring normalized =
        Lowercase(value.data());

    return
        normalized == L"true" ||
        normalized == L"yes" ||
        normalized == L"on" ||
        normalized == L"1";
}

void LoadMonitorLayouts(AppState& app) {
    app.monitorLayouts.clear();

    const std::wstring statePath =
        StateFilePath().wstring();

    auto saved =
        ReadIniSection(
            L"MonitorLayouts",
            statePath
        );

    // One-time compatibility with the old system that
    // stored MonitorLayouts in layouts.ini.
    if (saved.empty()) {
        saved =
            ReadIniSection(
                L"MonitorLayouts",
                LayoutFilePath().wstring()
            );
    }

    for (
        const auto& [monitor, layout] :
        saved
    ) {
        if (
            monitor.empty() ||
            layout.empty()
        ) {
            continue;
        }

        const std::wstring key =
            Lowercase(monitor);

        app.monitorLayouts[key] =
            layout;

        WritePrivateProfileStringW(
            L"MonitorLayouts",
            key.c_str(),
            layout.c_str(),
            statePath.c_str()
        );
    }
}

void LoadLayouts(AppState& app) {
    const std::wstring path =
        LayoutFilePath().wstring();

    app.layouts.clear();
    app.excludedApps.clear();

    app.windowOverlapPercent =
        std::clamp(
            GetPrivateProfileIntW(
                L"Settings",
                L"windowOverlapPercent",
                25,
                path.c_str()
            ),
            0u,
            100u
        );

    app.snapPadding =
        std::clamp(
            GetPrivateProfileIntW(
                L"Settings",
                L"padding",
                0,
                path.c_str()
            ),
            0u,
            500u
        );

    app.horizontalCycleNotches =
        std::clamp(
            GetPrivateProfileIntW(
                L"Settings",
                L"horizontalCycleNotches",
                2,
                path.c_str()
            ),
            1u,
            10u
        );

    app.pauseInFullscreen =
        ReadBoolSetting(
            L"pauseInFullscreen",
            true,
            path
        );

    app.startWithWindows =
        ReadBoolSetting(
            L"startWithWindows",
            false,
            path
        );

    for (
        const auto& [name, enabled] :
        ReadIniSection(
            L"ExcludedApps",
            path
        )
    ) {
        const std::wstring normalized =
            Lowercase(enabled);

        if (
            normalized == L"0" ||
            normalized == L"false" ||
            normalized == L"off"
        ) {
            continue;
        }

        app.excludedApps.insert(
            Lowercase(
                std::filesystem::path(name)
                    .filename()
                    .wstring()
            )
        );
    }

    LoadMonitorLayouts(app);

    std::array<wchar_t, 8192> sectionNames{};

    GetPrivateProfileSectionNamesW(
        sectionNames.data(),
        static_cast<DWORD>(
            sectionNames.size()
        ),
        path.c_str()
    );

    for (
        const wchar_t* section =
            sectionNames.data();

        *section;

        section += wcslen(section) + 1
    ) {
        if (
            _wcsicmp(
                section,
                L"Settings"
            ) == 0 ||
            _wcsicmp(
                section,
                L"ExcludedApps"
            ) == 0 ||
            _wcsicmp(
                section,
                L"MonitorLayouts"
            ) == 0
        ) {
            continue;
        }

        Layout layout;
        layout.name = section;

        for (
            int index = 1;
            index <= kMaxZones;
            ++index
        ) {
            const std::wstring key =
                L"zone" +
                std::to_wstring(index);

            std::array<wchar_t, 256> value{};

            GetPrivateProfileStringW(
                section,
                key.c_str(),
                L"",
                value.data(),
                static_cast<DWORD>(
                    value.size()
                ),
                path.c_str()
            );

            if (value[0] == L'\0') {
                continue;
            }

            std::wstring coordinates =
                value.data();

            std::replace(
                coordinates.begin(),
                coordinates.end(),
                L',',
                L' '
            );

            std::wistringstream stream(
                coordinates
            );

            NormalizedRect zone{};

            if (
                stream >>
                    zone.left >>
                    zone.top >>
                    zone.right >>
                    zone.bottom &&

                zone.left >= 0.0 &&
                zone.top >= 0.0 &&
                zone.right <= 100.0 &&
                zone.bottom <= 100.0 &&
                zone.right > zone.left &&
                zone.bottom > zone.top
            ) {
                layout.zones.push_back(
                    zone
                );
            }
        }

        if (!layout.zones.empty()) {
            app.layouts.push_back(
                std::move(layout)
            );
        }
    }

    if (app.layouts.empty()) {
        AddFallbackLayouts(app);
    }

    if (
        app.layoutIndex >=
        app.layouts.size()
    ) {
        app.layoutIndex = 0;
    }
}

void SaveCurrentMonitorLayout(
    AppState& app
) {
    if (
        app.currentMonitorKey.empty() ||
        app.layouts.empty()
    ) {
        return;
    }

    const std::wstring layoutName =
        CurrentLayout(app).name;

    app.monitorLayouts[
        app.currentMonitorKey
    ] = layoutName;

    WritePrivateProfileStringW(
        L"MonitorLayouts",
        app.currentMonitorKey.c_str(),
        layoutName.c_str(),
        StateFilePath().c_str()
    );
}

void SelectMonitor(
    AppState& app,
    POINT point
) {
    MONITORINFOEXW info{
        sizeof(info)
    };

    GetMonitorInfoW(
        MonitorFromPoint(
            point,
            MONITOR_DEFAULTTONEAREST
        ),
        &info
    );

    std::wstring newKey =
        info.szDevice;

    if (
        newKey.starts_with(
            L"\\\\.\\"
        )
    ) {
        newKey.erase(0, 4);
    }

    newKey =
        Lowercase(newKey);

    const bool monitorChanged =
        newKey !=
        app.currentMonitorKey;

    if (
        monitorChanged &&
        !app.currentMonitorKey.empty()
    ) {
        SaveCurrentMonitorLayout(app);
    }

    app.monitorWork =
        info.rcWork;

    app.currentMonitorKey =
        std::move(newKey);

    if (!monitorChanged) {
        return;
    }

    const auto saved =
        app.monitorLayouts.find(
            app.currentMonitorKey
        );

    if (
        saved ==
        app.monitorLayouts.end()
    ) {
        return;
    }

    const auto layout =
        std::find_if(
            app.layouts.begin(),
            app.layouts.end(),
            [&](const Layout& candidate) {
                return
                    _wcsicmp(
                        candidate.name.c_str(),
                        saved->second.c_str()
                    ) == 0;
            }
        );

    if (
        layout !=
        app.layouts.end()
    ) {
        app.layoutIndex =
            static_cast<size_t>(
                std::distance(
                    app.layouts.begin(),
                    layout
                )
            );
    }
}

void ApplyStartupSetting(
    AppState& app
) {
    constexpr wchar_t runKey[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    HKEY key{};

    if (
        RegCreateKeyExW(
            HKEY_CURRENT_USER,
            runKey,
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr
        ) != ERROR_SUCCESS
    ) {
        return;
    }

    if (app.startWithWindows) {
        std::array<wchar_t, 32768> exePath{};

        const DWORD length =
            GetModuleFileNameW(
                nullptr,
                exePath.data(),
                static_cast<DWORD>(
                    exePath.size()
                )
            );

        const std::wstring command =
            L"\"" +
            std::wstring(
                exePath.data(),
                length
            ) +
            L"\"";

        RegSetValueExW(
            key,
            L"ZoneSmith",
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(
                command.c_str()
            ),
            static_cast<DWORD>(
                (
                    command.size() +
                    1
                ) *
                sizeof(wchar_t)
            )
        );
    } else {
        RegDeleteValueW(
            key,
            L"ZoneSmith"
        );
    }

    RegCloseKey(key);
}