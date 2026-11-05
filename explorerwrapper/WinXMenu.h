#pragma once
#include "common.h"

// Clear of the Windows 7 tray's own hotkey ids, 500 through 566.
#define WINX_HOTKEY_ID 0x7858

// Posted to the tray so registration runs on its owning thread. Zero when disabled.
extern UINT g_winXRegisterMsg;

void InstallWinXMenu();
void RegisterWinXHotkey(HWND tray);
void OpenWinXMenuFromKeyboard(HWND tray);

// Uses the RegisterHotKey trampoline when immersive-shell hooks are active.
BOOL RegisterHotKeyUnhooked(HWND hwnd, int id, UINT mod, UINT vk);
