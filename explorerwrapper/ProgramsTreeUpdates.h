#pragma once
#include "common.h"

class StartMenuRoots;

// Sources must retain the distinct Programs/Start Menu roots (Initialize(false)).
// Attaches once; teardown follows WM_NCDESTROY on the control's UI thread.
HRESULT WatchProgramsTree(INameSpaceTreeControl2* control, const StartMenuRoots& sources);

void RefreshStartMenuHighlights(HWND notificationSource);

// Retains the native five-second timer and only changes its MFU enumeration request.
HRESULT HookStartMenuRefreshTimer(HMODULE explorer);

// The detour retains the native MFU notification handler as its trampoline.
using StartMenuNewItemsProc = LRESULT (__fastcall*)(uintptr_t, NMHDR*);
extern StartMenuNewItemsProc StartMenuHaveNewItemsOriginal;
LRESULT __fastcall StartMenuHaveNewItemsHook(uintptr_t host, NMHDR* notification);
