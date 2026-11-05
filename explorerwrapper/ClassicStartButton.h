#pragma once
#include <windows.h>

bool IsNativeStartButtonWindow(HWND hwnd);
void EnsureClassicStartButtonSubclass(HWND hwnd);
