#pragma once
#include "common.h"

bool IsHighContrastEnabled();
extern bool g_highContrastThemeActive;
bool HasLoadedInactiveTheme();
HTHEME OpenLoadedInactiveTheme(HWND hwnd, LPCWSTR pszClassList, DWORD dwFlags);
void CloseLoadedInactiveThemeHandles();
void ThemeManagerInitialize();
void ThemeManagerUninitialize();