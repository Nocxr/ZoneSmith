#pragma once

#include "AppState.h"

LRESULT CALLBACK MouseHook(
    int code,
    WPARAM message,
    LPARAM data
);

LRESULT CALLBACK KeyboardHook(
    int code,
    WPARAM message,
    LPARAM data
);

void CALLBACK MoveSizeEventHook(
    HWINEVENTHOOK,
    DWORD event,
    HWND window,
    LONG objectId,
    LONG childId,
    DWORD eventThread,
    DWORD eventTime
);