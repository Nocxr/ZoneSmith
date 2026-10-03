#include "Overlay.h"

#include "Config.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace {

bool IsZoneInPreviewSpan(
    const AppState& app,
    int zone
) {
    if (
        app.selectedZones == 0 ||
        app.hotZones == 0
    ) {
        return false;
    }

    const unsigned int endpoints =
        app.selectedZones |
        app.hotZones;

    double left = 100.0;
    double top = 100.0;
    double right = 0.0;
    double bottom = 0.0;

    for (
        int index = 0;
        index <
            static_cast<int>(
                CurrentLayout(app)
                    .zones
                    .size()
            );
        ++index
    ) {
        if (
            (endpoints & (1u << index)) ==
            0
        ) {
            continue;
        }

        const auto& endpoint =
            CurrentLayout(app)
                .zones[index];

        left =
            std::min(
                left,
                endpoint.left
            );

        top =
            std::min(
                top,
                endpoint.top
            );

        right =
            std::max(
                right,
                endpoint.right
            );

        bottom =
            std::max(
                bottom,
                endpoint.bottom
            );
    }

    const auto& candidate =
        CurrentLayout(app)
            .zones[zone];

    const double centerX =
        (
            candidate.left +
            candidate.right
        ) /
        2.0;

    const double centerY =
        (
            candidate.top +
            candidate.bottom
        ) /
        2.0;

    return
        centerX >= left &&
        centerX <= right &&
        centerY >= top &&
        centerY <= bottom;
}

}

RECT ZoneRectInArea(
    const AppState& app,
    int zone,
    const RECT& area
) {
    const auto& normalized =
        CurrentLayout(app)
            .zones[zone];

    const double width =
        area.right -
        area.left;

    const double height =
        area.bottom -
        area.top;

    return RECT{
        area.left +
            static_cast<LONG>(
                std::round(
                    width *
                    normalized.left /
                    100.0
                )
            ),

        area.top +
            static_cast<LONG>(
                std::round(
                    height *
                    normalized.top /
                    100.0
                )
            ),

        area.left +
            static_cast<LONG>(
                std::round(
                    width *
                    normalized.right /
                    100.0
                )
            ),

        area.top +
            static_cast<LONG>(
                std::round(
                    height *
                    normalized.bottom /
                    100.0
                )
            ),
    };
}

RECT ZoneRect(
    const AppState& app,
    int zone
) {
    return ZoneRectInArea(
        app,
        zone,
        app.monitorWork
    );
}

unsigned int ZonesAt(
    const AppState& app,
    POINT point
) {
    const RECT& selectionArea =
        app.compactMode
            ? app.overlayBounds
            : app.monitorWork;

    if (
        !PtInRect(
            &selectionArea,
            point
        )
    ) {
        return 0;
    }

    const int tolerance =
        app.compactMode
            ? 6
            : 10;

    unsigned int zones = 0;

    for (
        int zone = 0;
        zone <
            static_cast<int>(
                CurrentLayout(app)
                    .zones
                    .size()
            );
        ++zone
    ) {
        RECT hitArea =
            ZoneRectInArea(
                app,
                zone,
                selectionArea
            );

        InflateRect(
            &hitArea,
            tolerance,
            tolerance
        );

        if (
            PtInRect(
                &hitArea,
                point
            )
        ) {
            zones |=
                1u << zone;
        }
    }

    return zones;
}

RECT ZonesRect(
    const AppState& app,
    unsigned int zones
) {
    RECT result{
        LONG_MAX,
        LONG_MAX,
        LONG_MIN,
        LONG_MIN
    };

    for (
        int zone = 0;
        zone <
            static_cast<int>(
                CurrentLayout(app)
                    .zones
                    .size()
            );
        ++zone
    ) {
        if (
            (zones & (1u << zone)) ==
            0
        ) {
            continue;
        }

        const RECT rect =
            ZoneRect(
                app,
                zone
            );

        result.left =
            std::min(
                result.left,
                rect.left
            );

        result.top =
            std::min(
                result.top,
                rect.top
            );

        result.right =
            std::max(
                result.right,
                rect.right
            );

        result.bottom =
            std::max(
                result.bottom,
                rect.bottom
            );
    }

    if (
        result.left != LONG_MAX &&
        app.snapPadding > 0
    ) {
        const LONG maxHorizontal =
            std::max(
                0L,
                (
                    result.right -
                    result.left -
                    1
                ) /
                2
            );

        const LONG maxVertical =
            std::max(
                0L,
                (
                    result.bottom -
                    result.top -
                    1
                ) /
                2
            );

        const LONG horizontal =
            std::min(
                static_cast<LONG>(
                    app.snapPadding
                ),
                maxHorizontal
            );

        const LONG vertical =
            std::min(
                static_cast<LONG>(
                    app.snapPadding
                ),
                maxVertical
            );

        result.left += horizontal;
        result.right -= horizontal;
        result.top += vertical;
        result.bottom -= vertical;
    }

    return result;
}

void CycleLayout(
    AppState& app,
    int direction
) {
    if (app.layouts.empty()) {
        return;
    }

    const int count =
        static_cast<int>(
            app.layouts.size()
        );

    app.layoutIndex =
        static_cast<size_t>(
            (
                static_cast<int>(
                    app.layoutIndex
                ) +
                direction +
                count
            ) %
            count
        );

    SaveCurrentMonitorLayout(app);

    app.selectedZones = 0;

    ShowOverlay(
        app,
        app.cursor
    );
}

void ShowOverlay(
    AppState& app,
    POINT point
) {
    SelectMonitor(
        app,
        point
    );

    if (app.compactMode) {
        const int monitorWidth =
            app.monitorWork.right -
            app.monitorWork.left;

        const int monitorHeight =
            app.monitorWork.bottom -
            app.monitorWork.top;

        const int width =
            std::min(
                420,
                std::max(
                    270,
                    monitorWidth / 5
                )
            );

        const int height =
            std::clamp(
                width *
                    monitorHeight /
                    std::max(
                        1,
                        monitorWidth
                    ),
                140,
                240
            );

        int left =
            point.x -
            width / 2;

        int top =
            point.y -
            height / 2;

        left =
            std::clamp(
                left,
                static_cast<int>(
                    app.monitorWork.left
                ),
                static_cast<int>(
                    app.monitorWork.right
                ) -
                    width
            );

        top =
            std::clamp(
                top,
                static_cast<int>(
                    app.monitorWork.top
                ),
                static_cast<int>(
                    app.monitorWork.bottom
                ) -
                    height
            );

        app.overlayBounds =
            RECT{
                left,
                top,
                left + width,
                top + height
            };
    } else {
        app.overlayBounds =
            app.monitorWork;
    }

    app.hotZones =
        ZonesAt(
            app,
            point
        );

    SetWindowPos(
        app.overlay,
        HWND_TOPMOST,
        app.overlayBounds.left,
        app.overlayBounds.top,
        app.overlayBounds.right -
            app.overlayBounds.left,
        app.overlayBounds.bottom -
            app.overlayBounds.top,
        SWP_NOACTIVATE |
            SWP_SHOWWINDOW
    );

    app.overlayVisible = true;

    InvalidateRect(
        app.overlay,
        nullptr,
        TRUE
    );
}

void HideOverlay(
    AppState& app
) {
    app.overlayVisible = false;
    app.hotZones = 0;

    if (app.overlay) {
        ShowWindow(
            app.overlay,
            SW_HIDE
        );
    }
}

void PaintOverlay(
    AppState& app,
    HWND window
) {
    PAINTSTRUCT paint{};

    HDC dc =
        BeginPaint(
            window,
            &paint
        );

    RECT client{};

    GetClientRect(
        window,
        &client
    );

    HBRUSH background =
        CreateSolidBrush(
            RGB(
                18,
                24,
                38
            )
        );

    FillRect(
        dc,
        &client,
        background
    );

    DeleteObject(
        background
    );

    SetBkMode(
        dc,
        TRANSPARENT
    );

    SetTextAlign(
        dc,
        TA_CENTER |
            TA_BASELINE
    );

    const int fontHeight =
        std::clamp(
            client.bottom / 3,
            32L,
            72L
        );

    HFONT font =
        CreateFontW(
            fontHeight,
            0,
            0,
            0,
            FW_BOLD,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI"
        );

    HFONT oldFont =
        static_cast<HFONT>(
            SelectObject(
                dc,
                font
            )
        );

    const double width =
        client.right;

    const double height =
        client.bottom;

    for (
        int zone = 0;
        zone <
            static_cast<int>(
                CurrentLayout(app)
                    .zones
                    .size()
            );
        ++zone
    ) {
        const bool selected =
            (
                app.selectedZones &
                (1u << zone)
            ) != 0;

        const bool hovered =
            (
                app.hotZones &
                (1u << zone)
            ) != 0;

        const bool spanFill =
            !selected &&
            !hovered &&
            IsZoneInPreviewSpan(
                app,
                zone
            );

        const auto& normalized =
            CurrentLayout(app)
                .zones[zone];

        RECT rect{
            static_cast<LONG>(
                std::round(
                    width *
                    normalized.left /
                    100.0
                )
            ) + 6,

            static_cast<LONG>(
                std::round(
                    height *
                    normalized.top /
                    100.0
                )
            ) + 6,

            static_cast<LONG>(
                std::round(
                    width *
                    normalized.right /
                    100.0
                )
            ) - 6,

            static_cast<LONG>(
                std::round(
                    height *
                    normalized.bottom /
                    100.0
                )
            ) - 6
        };

        const COLORREF fillColor =
            selected
                ? (
                    hovered
                        ? RGB(36, 185, 122)
                        : RGB(31, 145, 96)
                )
                : (
                    hovered
                        ? RGB(35, 135, 230)
                        : (
                            spanFill
                                ? RGB(210, 165, 28)
                                : RGB(65, 79, 105)
                        )
                );

        HBRUSH fill =
            CreateSolidBrush(
                fillColor
            );

        FillRect(
            dc,
            &rect,
            fill
        );

        DeleteObject(fill);

        const int borderWidth =
            selected ||
            hovered
                ? 5
                : 2;

        const COLORREF borderColor =
            selected
                ? RGB(200, 255, 226)
                : (
                    hovered
                        ? RGB(190, 225, 255)
                        : (
                            spanFill
                                ? RGB(255, 225, 120)
                                : RGB(135, 155, 185)
                        )
                );

        HPEN pen =
            CreatePen(
                PS_SOLID,
                borderWidth,
                borderColor
            );

        HPEN oldPen =
            static_cast<HPEN>(
                SelectObject(
                    dc,
                    pen
                )
            );

        HBRUSH oldBrush =
            static_cast<HBRUSH>(
                SelectObject(
                    dc,
                    GetStockObject(
                        NULL_BRUSH
                    )
                )
            );

        Rectangle(
            dc,
            rect.left,
            rect.top,
            rect.right,
            rect.bottom
        );

        SelectObject(
            dc,
            oldBrush
        );

        SelectObject(
            dc,
            oldPen
        );

        DeleteObject(pen);

        SetTextColor(
            dc,
            RGB(
                255,
                255,
                255
            )
        );

        const std::wstring number =
            std::to_wstring(
                zone + 1
            );

        TextOutW(
            dc,
            (
                rect.left +
                rect.right
            ) /
                2,
            (
                rect.top +
                rect.bottom +
                fontHeight
            ) /
                2,
            number.c_str(),
            static_cast<int>(
                number.size()
            )
        );
    }

    HFONT labelFont =
        CreateFontW(
            app.compactMode
                ? 18
                : 26,
            0,
            0,
            0,
            FW_SEMIBOLD,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI"
        );

    SelectObject(
        dc,
        labelFont
    );

    SetTextAlign(
        dc,
        TA_LEFT |
            TA_TOP
    );

    SetTextColor(
        dc,
        RGB(
            255,
            255,
            255
        )
    );

    RECT labelRect{
        12,
        10,
        client.right - 12,
        client.bottom - 10
    };

    DrawTextW(
        dc,
        CurrentLayout(app)
            .name
            .c_str(),
        -1,
        &labelRect,
        DT_LEFT |
            DT_TOP |
            DT_SINGLELINE |
            DT_END_ELLIPSIS
    );

    SelectObject(
        dc,
        oldFont
    );

    DeleteObject(
        labelFont
    );

    DeleteObject(font);

    EndPaint(
        window,
        &paint
    );
}