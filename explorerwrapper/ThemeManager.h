#pragma once
#include "common.h"

bool IsHighContrastEnabled();
extern bool g_highContrastThemeActive;
bool HasLoadedInactiveTheme();
HTHEME OpenLoadedInactiveTheme(HWND hwnd, LPCWSTR pszClassList, DWORD dwFlags);
bool IsLoadedInactiveThemeClassDefined(HWND hwnd, LPCWSTR pszAppName, LPCWSTR pszClassId);
void CloseLoadedInactiveThemeHandles();
void CloseRetiredInactiveThemeResources();
void ThemeManagerInitialize();
void ThemeManagerUninitialize();