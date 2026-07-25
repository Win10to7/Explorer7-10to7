#pragma once
#include "util.h"
#include "common.h"
#include "dbgprint.h"
#include "OptionConfig.h"
#include "OSVersion.h"
#include "TypeDefinitions.h"


#ifndef DWM_E_COMPOSITIONDISABLED
#define DWM_E_COMPOSITIONDISABLED 0x80263001
#endif
// Ittr: Address import patches are now in this file

// Adjust ShellURL so that searching by file extension is functional again
HRESULT WINAPI SHCoCreateInstanceNEW(PCWSTR pszCLSID, const CLSID* pclsid, IUnknown* pUnkOuter, IID& riid, void** ppv)
{
	HRESULT res = SHCoCreateInstance(pszCLSID, pclsid, pUnkOuter, riid, ppv);
	if (res != S_OK && riid == GUID_88df9332_6adb_4604_8218_508673ef7f8a)
	{
		IShellURL10* shellurl10;
		res = SHCoCreateInstance(pszCLSID, pclsid, pUnkOuter, GUID_4f33718d_bae1_4f9b_96f2_d2a16e683346, (void**)&shellurl10);
		*ppv = new CShellURLWrapper(shellurl10);
	}
	return res;
}

// Adjust a window's position to be pushed away from the taskbar
SIZE AdjustWindowRectForTaskbar(RECT* lprc)
{
	HMONITOR hm = MonitorFromRect(lprc, MONITOR_DEFAULTTONEAREST);
	HDC hDC = GetDC(NULL);
	int offset = MulDiv(8, GetDeviceCaps(hDC, LOGPIXELSY), 96);
	ReleaseDC(NULL, hDC);

	MONITORINFO mi = { sizeof(MONITORINFO) };
	GetMonitorInfoW(hm, &mi);

	int dx = 0, dy = 0;
	PLONG plrc = (PLONG)lprc;
	PLONG plwrc = (PLONG)&mi.rcWork;
	for (int i = 0; i < 4; i++)
	{
		int curOffset = plwrc[i] - plrc[i];
		curOffset = (curOffset < 0) ? -curOffset : curOffset;

		if (curOffset < offset)
		{
			int* set = (i % 2 == 0) ? &dx : &dy;
			if (i > 1)
			{
				*set -= offset - curOffset;
			}
			else
			{
				*set += offset - curOffset;
			}
		}
	}
	return { dx, dy };
}

// Adjust by 8 pixels accordingly
BOOL WINAPI CalculatePopupWindowPositionNEW(
	const POINT* anchorPoint,
	const SIZE* windowSize,
	UINT         flags,
	RECT* excludeRect,
	RECT* popupWindowPosition
)
{
	UINT popupFlags = flags;
	if (!IsAppThemed())
	{
		popupFlags &= ~TPM_WORKAREA;
	}
	BOOL res = CalculatePopupWindowPosition(
		anchorPoint, windowSize, popupFlags,
		excludeRect, popupWindowPosition
	);
	if (IsAppThemed() && (!IsClassicTheme() && IsCompositionActive() && !IsCompositionManuallyDisabled()) && res && (flags & TPM_WORKAREA) != 0)
	{
		SIZE adjust = AdjustWindowRectForTaskbar(popupWindowPosition);
		OffsetRect(popupWindowPosition, adjust.cx, adjust.cy);
	}
	return res;
}

BOOL WINAPI TrackPopupMenuExNEW(
	HMENU hMenu,
	UINT uFlags,
	int x,
	int y,
	HWND hwnd,
	LPTPMPARAMS lptpm
)
{
	HWND taskbar = GetTaskbarWnd();
	if (!IsAppThemed() && taskbar && hwnd && (hwnd == taskbar || IsChild(taskbar, hwnd)))
	{
		APPBARDATA abd = { sizeof(APPBARDATA) };
		abd.hWnd = taskbar;
		if (SHAppBarMessage(ABM_GETTASKBARPOS, &abd))
		{
			HDC screen = GetDC(NULL);
			int offset = MulDiv(4, GetDeviceCaps(screen, LOGPIXELSY), 96);
			ReleaseDC(NULL, screen);
			switch (abd.uEdge)
			{
			case ABE_LEFT:
				x -= offset;
				break;
			case ABE_TOP:
				y -= offset;
				break;
			case ABE_RIGHT:
				x += offset;
				break;
			case ABE_BOTTOM:
				y += offset;
				break;
			}
		}
	}

	return TrackPopupMenuEx(hMenu, uFlags, x, y, hwnd, lptpm);
}

// Additional helper for removing immersive menus. Better inter-operability between Windows versions. Used alongside the pattern method.
BOOL SystemParametersInfoWNEW(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni)
{
	if (uiAction == SPI_GETSCREENREADER)
	{
		*(BOOL*)pvParam = TRUE;
		return TRUE;
	}

	return SystemParametersInfoW(uiAction, uiParam, pvParam, fWinIni);
}

// For SWCA, so we can import our colorization configuration
BOOL WINAPI SetWindowCompositionAttributeNEW(HWND hwnd, WINDOWCOMPOSITIONATTRIBDATA* pAttrData) // Ittr: re-organised again 25/10/24
{
	if ((IsClassicTheme() || !IsCompositionActive() || IsCompositionManuallyDisabled()) && IsWrapperManagedWindow(hwnd)) // we do funny things so explorer works properly for classic/basic.
	{
		int bNCRenderingEnabled = DWMNCRP_DISABLED;

		// Disable DWM frames
		WINDOWCOMPOSITIONATTRIBDATA attrData;
		attrData.Attrib = WCA_NCRENDERING_POLICY;
		attrData.pvData = &bNCRenderingEnabled;
		attrData.cbData = sizeof(bNCRenderingEnabled);

		return SetWindowCompositionAttribute(hwnd, &attrData); //byebye
	}

	if (pAttrData->Attrib == WCA_DISALLOW_PEEK)
	{
		if (hwnd == GetTaskbarWnd() || hwnd == GetStartMenuWnd() || hwnd == GetThumbnailWnd())
		{
			UpdateShellWindowAccent(hwnd, hwnd == GetThumbnailWnd());
		}

		if (!ShouldDisableShellWindowTransparency() && IsCompositionActive())
		{
			ForceActiveWindowAppearance(hwnd); // mainly for legacy but doesn't seem to harm anything by applying anyway
		}
	}

	return SetWindowCompositionAttribute(hwnd, pAttrData);
}

// Temporary partial bug workaround
int WINAPI SetWindowRgnNEW(HWND hwnd, HRGN hRgn, BOOL bRedraw)
{
	// Ittr: This hack (first written in 2012) cancels out a window region call that no longer behaves correctly post-RS4
	if (hRgn == NULL && hwnd == GetStartMenuWnd()) return 0;

	// There is then an adjustment to ensure that the region is re-drawn due to a visual issue that appears otherwise
	if (hwnd == GetStartMenuWnd()) bRedraw = TRUE;

	return SetWindowRgn(hwnd, hRgn, bRedraw);
}

// Set applicable AUMID, create immersive stack where applicable
UINT WINAPI SetErrorModeNEW(UINT uMode)
{
	SetCurrentProcessExplicitAppUserModelID(L"Microsoft.Windows.Explorer");

	if (s_EnableImmersiveShellStack == 1)
		CreateTwinUI_UWP();

	return SetErrorMode(uMode);
}
//Ittr: Intercept these functions where appropriate for basic theme to be forced at compile time if required
BOOL WINAPI IsCompositionActiveNEW()
{
	return IsCompositionActive();
}

// Disable composition where appropriate
HRESULT WINAPI DwmIsCompositionEnabledNEW(BOOL* pfEnabled)
{
	HRESULT hr;
	if (DwmIsCompositionEnabledOrig)
	{
		hr = DwmIsCompositionEnabledOrig(pfEnabled);
	}
	else
	{
		static auto fn = reinterpret_cast<DwmIsCompositionEnabledAPI>(GetProcAddress(GetModuleHandleW(L"dwmapi.dll"), "DwmIsCompositionEnabled"));
		hr = fn ? fn(pfEnabled) : E_FAIL;
	}

	if (SUCCEEDED(hr) && pfEnabled && *pfEnabled && IsCompositionManuallyDisabled())
	{
		*pfEnabled = FALSE;
	}

	return hr;
}
HRESULT WINAPI DwmExtendFrameIntoClientAreaNEW(HWND hwnd, const MARGINS* pMarInset)
{
	if (ShouldTreatDwmAsDisabledForExplorerFrame(hwnd))
	{
		if (!GetPropW(hwnd, CLASSIC_FRAME_PROP))
		{
			SyncExplorerFrameTheme(hwnd);
		}
		return DWM_E_COMPOSITIONDISABLED;
	}

	if (DwmExtendFrameIntoClientAreaOrig)
	{
		return DwmExtendFrameIntoClientAreaOrig(hwnd, pMarInset);
	}

	static auto fn = reinterpret_cast<DwmExtendFrameIntoClientAreaAPI>(GetProcAddress(GetModuleHandleW(L"dwmapi.dll"), "DwmExtendFrameIntoClientArea"));
	return fn ? fn(hwnd, pMarInset) : E_FAIL;
}

HRESULT WINAPI DwmSetWindowAttributeNEW(HWND hwnd, DWORD dwAttribute, LPCVOID pvAttribute, DWORD cbAttribute)
{
	int bNCRenderingPolicy = DWMNCRP_DISABLED;
	if (dwAttribute == DWMWA_NCRENDERING_POLICY &&
		ShouldTreatDwmAsDisabledForExplorerFrame(hwnd))
	{
		if (!GetPropW(hwnd, CLASSIC_FRAME_PROP))
		{
			SyncExplorerFrameTheme(hwnd);
		}
		pvAttribute = &bNCRenderingPolicy;
		cbAttribute = sizeof(bNCRenderingPolicy);
	}

	if (DwmSetWindowAttributeOrig)
	{
		return DwmSetWindowAttributeOrig(hwnd, dwAttribute, pvAttribute, cbAttribute);
	}

	static auto fn = reinterpret_cast<DwmSetWindowAttributeAPI>(GetProcAddress(GetModuleHandleW(L"dwmapi.dll"), "DwmSetWindowAttribute"));
	return fn ? fn(hwnd, dwAttribute, pvAttribute, cbAttribute) : E_FAIL;
}

// Disable legacy DwmEnableBlurBehindWindow when new methods are in use
HRESULT WINAPI DwmEnableBlurBehindWindowNEW(HWND hwnd, DWM_BLURBEHIND* pBlurBehind)
{
	if (hwnd == GetThumbnailWnd()) // does this even do anything??
	{
		ForceActiveWindowAppearance(hwnd);
	}

	if ((hwnd == GetTaskbarWnd() || hwnd == GetStartMenuWnd() || hwnd == GetThumbnailWnd()) &&
		(pBlurBehind && (s_ColorizationOptions != 0 || ShouldDisableShellWindowTransparency())))
	{
		pBlurBehind->fEnable = 0;
	}

	return DwmEnableBlurBehindWindow(hwnd, pBlurBehind);
}

// Adjust preview activation to prevent possible crashing
__int64 DwmpActivateLivePreviewNEW(int a1, __int64 a2, __int64 a3, int a4, void* a5)
{
	if (a5 == (void*)8)
	{
		a5 = 0;
	}

	if (a5)
	{
		MEMORY_BASIC_INFORMATION mbi;
		if (!VirtualQuery(a5, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
		{
			a5 = 0;
		}
	}
	if (ShouldDisableAeroPeek() && a1)
	{
		return 0;
	}

	return DwmpActivateLivePreview(a1, a2, a3, a4, a5);
}

// Adjust colorization parameters
DWORD WINAPI DwmGetColorizationParametersNEW(PDWMCOLORIZATIONPARAMS colors)
{
	CHAR buffer[0x28];
	memset(buffer, 0, 0x28);
	dbgprintf(L"DwmGetColorizationParameters\nColorizationColor %p\nColorizationAfterglow %p\nColorizationColorBalance %p\nColorizationAfterglowBalance %p\nColorizationBlurBalance %p\nColorizationGlassReflectionIntensity %p\nColorizationOpaqueBlend %p",
		colors->ColorizationColor, colors->ColorizationAfterglow, colors->ColorizationColorBalance, colors->ColorizationAfterglowBalance, colors->ColorizationBlurBalance, colors->ColorizationGlassReflectionIntensity, colors->ColorizationOpaqueBlend);
	DWORD ret = DwmGetColorizationParametersOrig(&buffer);
	memcpy(colors, (PVOID)buffer, sizeof(DWMCOLORIZATIONPARAMS));
	return ret;
}


struct StartupRunKey
{
	HKEY hKey;
	HKEY hRoot;
};

StartupRunKey g_startupRunKeys[8] = {};
SRWLOCK g_startupRunKeysLock = SRWLOCK_INIT;

bool IsStartupRunKey(PCWSTR subKey)
{
	static const WCHAR runKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	return subKey && lstrcmpiW(subKey, runKey) == 0;
}

void TrackStartupRunKey(HKEY hKey, HKEY hRoot)
{
	AcquireSRWLockExclusive(&g_startupRunKeysLock);
	for (StartupRunKey& key : g_startupRunKeys)
	{
		if (!key.hKey)
		{
			key = { hKey, hRoot };
			break;
		}
	}
	ReleaseSRWLockExclusive(&g_startupRunKeysLock);
}

HKEY GetStartupRunKeyRoot(HKEY hKey)
{
	HKEY hRoot = NULL;
	AcquireSRWLockShared(&g_startupRunKeysLock);
	for (const StartupRunKey& key : g_startupRunKeys)
	{
		if (key.hKey == hKey)
		{
			hRoot = key.hRoot;
			break;
		}
	}
	ReleaseSRWLockShared(&g_startupRunKeysLock);
	return hRoot;
}

void UntrackStartupRunKey(HKEY hKey)
{
	AcquireSRWLockExclusive(&g_startupRunKeysLock);
	for (StartupRunKey& key : g_startupRunKeys)
	{
		if (key.hKey == hKey)
		{
			key = {};
			break;
		}
	}
	ReleaseSRWLockExclusive(&g_startupRunKeysLock);
}

bool IsStartupItemEnabled(HKEY hRoot, PCWSTR approvalKey, PCWSTR valueName)
{
	DWORD state[3] = {};
	DWORD stateSize = sizeof(state);
	LSTATUS status = RegGetValueW(
		hRoot, approvalKey, valueName, RRF_RT_REG_BINARY, NULL, state, &stateSize);

	// Modern Explorer treats a missing/invalid record as enabled. For a valid
	// record, even states (2 and 6) are enabled and odd states (3 and 7) are blocked.
	return status != ERROR_SUCCESS || !stateSize || (state[0] & 1) == 0;
}

decltype(&RegOpenKeyExW) RegOpenKeyExWOrig;
decltype(&RegEnumValueW) RegEnumValueWOrig;
decltype(&RegCloseKey) RegCloseKeyOrig;


LSTATUS WINAPI RegOpenKeyExWNEW(HKEY hKey, LPCWSTR subKey, DWORD options, REGSAM samDesired, PHKEY result)
{
	LSTATUS status = RegOpenKeyExWOrig(hKey, subKey, options, samDesired, result);
	if (status == ERROR_SUCCESS && IsStartupRunKey(subKey))
		TrackStartupRunKey(*result, hKey);
	return status;
}

LSTATUS WINAPI RegCloseKeyNEW(HKEY hKey)
{
	UntrackStartupRunKey(hKey);
	return RegCloseKeyOrig(hKey);
}

LSTATUS WINAPI RegEnumValueWNEW(
	HKEY hKey,
	DWORD index,
	LPWSTR valueName,
	LPDWORD valueNameSize,
	LPDWORD reserved,
	LPDWORD type,
	LPBYTE data,
	LPDWORD dataSize)
{
	HKEY hRoot = GetStartupRunKeyRoot(hKey);
	if (!hRoot)
		return RegEnumValueWOrig(hKey, index, valueName, valueNameSize, reserved, type, data, dataSize);

	DWORD enabledIndex = 0;
	for (DWORD physicalIndex = 0; ; ++physicalIndex)
	{
		WCHAR candidate[16384];
		DWORD candidateSize = ARRAYSIZE(candidate);
		LSTATUS status = RegEnumValueWOrig(hKey, physicalIndex, candidate, &candidateSize, NULL, NULL, NULL, NULL);
		if (status != ERROR_SUCCESS)
			return status;

		static const WCHAR approvalKey[] =
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
		if (IsStartupItemEnabled(hRoot, approvalKey, candidate) && enabledIndex++ == index)
			return RegEnumValueWOrig(hKey, physicalIndex, valueName, valueNameSize, reserved, type, data, dataSize);
	}
}

DWORD ProcessRun6432NEW()
{
	static const WCHAR runKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	static const WCHAR approvalKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run32";
	HKEY hKey;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, runKey, 0, KEY_READ | KEY_WOW64_32KEY, &hKey) != ERROR_SUCCESS)
		return 0;

	bool launched = false;
	for (DWORD index = 0; ; ++index)
	{
		WCHAR valueName[256];
		WCHAR command[32768];
		DWORD valueNameSize = ARRAYSIZE(valueName);
		DWORD commandSize = sizeof(command);
		DWORD type;
		LSTATUS status = RegEnumValueW(
			hKey, index, valueName, &valueNameSize, NULL, &type,
			reinterpret_cast<LPBYTE>(command), &commandSize);
		if (status == ERROR_NO_MORE_ITEMS)
			break;
		if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) ||
			!IsStartupItemEnabled(HKEY_LOCAL_MACHINE, approvalKey, valueName))
		{
			continue;
		}

		WCHAR expandedCommand[32768];
		PWSTR commandLine = command;
		if (type == REG_EXPAND_SZ)
		{
			DWORD expandedSize = ExpandEnvironmentStringsW(command, expandedCommand, ARRAYSIZE(expandedCommand));
			if (!expandedSize || expandedSize > ARRAYSIZE(expandedCommand))
				continue;
			commandLine = expandedCommand;
		}

		STARTUPINFOW startupInfo = { sizeof(startupInfo) };
		PROCESS_INFORMATION processInfo;
		if (CreateProcessW(NULL, commandLine, NULL, NULL, FALSE, 0, NULL, NULL, &startupInfo, &processInfo))
		{
			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);
			launched = true;
		}
	}

	RegCloseKey(hKey);
	return launched;
}

typedef int(*ExecStartupEnumProc_t)(IShellFolder*, PCUITEMID_CHILD, IServiceProvider*, UINT, int*);
ExecStartupEnumProc_t ExecStartupEnumProcOrig;

int ExecStartupEnumProcNEW(
	IShellFolder* folder,
	PCUITEMID_CHILD item,
	IServiceProvider* serviceProvider,
	UINT startupFolder,
	int* result)
{
	STRRET displayName;
	WCHAR valueName[MAX_PATH];
	if (SUCCEEDED(folder->GetDisplayNameOf(item, SHGDN_INFOLDER, &displayName)) &&
		SUCCEEDED(StrRetToBufW(&displayName, item, valueName, ARRAYSIZE(valueName))))
	{
		static const WCHAR approvalKey[] =
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder";
		HKEY approvalRoot = startupFolder == CSIDL_COMMON_STARTUP
			? HKEY_LOCAL_MACHINE
			: HKEY_CURRENT_USER;
		if (!IsStartupItemEnabled(approvalRoot, approvalKey, valueName))
			return TRUE;
	}

	return ExecStartupEnumProcOrig(folder, item, serviceProvider, startupFolder, result);
}

void PatchStartupFolder()
{
	char* execStartupEnumProc =
		"48 89 5C 24 ?? 55 56 57 41 54 41 55 48 81 EC 80 04 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 ?? ?? ?? ?? 48 8B 01";
	void* target = reinterpret_cast<void*>(FindPattern(reinterpret_cast<uintptr_t>(GetModuleHandle(NULL)), execStartupEnumProc));
	if (target)
		MH_CreateHook(target, ExecStartupEnumProcNEW, reinterpret_cast<void**>(&ExecStartupEnumProcOrig));
}


void PatchProcessRun6432()
{
	char* processRun6432 = "FF F3 48 81 EC 80 04 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 ?? ?? ?? ?? B9 46 00 00 40";
	void* target = reinterpret_cast<void*>(FindPattern(reinterpret_cast<uintptr_t>(GetModuleHandle(NULL)), processRun6432));
	if (target)
		MH_CreateHook(target, ProcessRun6432NEW, NULL);
}


void PatchAdvapi32()
{
	HMODULE advapi32 = GetModuleHandle(L"advapi32.dll");
	MH_CreateHook(
		GetProcAddress(advapi32, "RegOpenKeyExW"), RegOpenKeyExWNEW,
		reinterpret_cast<void**>(&RegOpenKeyExWOrig));
	MH_CreateHook(
		GetProcAddress(advapi32, "RegEnumValueW"), RegEnumValueWNEW,
		reinterpret_cast<void**>(&RegEnumValueWOrig));
	MH_CreateHook(
		GetProcAddress(advapi32, "RegCloseKey"), RegCloseKeyNEW,
		reinterpret_cast<void**>(&RegCloseKeyOrig));
	PatchProcessRun6432();
	PatchStartupFolder();
}


// Import address changes for shell32.dll modulename
void PatchShell32()
{
	// Fixes the "search by extension" feature in the start menu
	ChangeImportedAddress(GetModuleHandle(NULL), "shell32.dll", GetProcAddress(GetModuleHandle(L"shell32.dll"), "SHCoCreateInstance"), SHCoCreateInstanceNEW);
}

// Import address changes for user32.dll modulename
void PatchUser32()
{
	// Update popup positioning to account for OS changes in Windows 10.
	// Explorer uses CalculatePopupWindowPosition directly, while shell32 also owns popup menu placement for shell surfaces such as jumplists.
	FARPROC calculatePopupWindowPosition = GetProcAddress(GetModuleHandle(L"user32.dll"), "CalculatePopupWindowPosition");
	FARPROC trackPopupMenuEx = GetProcAddress(GetModuleHandle(L"user32.dll"), "TrackPopupMenuEx");
	ChangeImportedAddress(GetModuleHandle(NULL), "user32.dll", calculatePopupWindowPosition, CalculatePopupWindowPositionNEW);
	HMODULE shell32 = GetModuleHandle(L"shell32.dll");
	if (shell32)
	{
		ChangeImportedAddress(shell32, "user32.dll", calculatePopupWindowPosition, CalculatePopupWindowPositionNEW);
		ChangeImportedAddress(shell32, "user32.dll", trackPopupMenuEx, TrackPopupMenuExNEW);
	}

	// Ensure as much as we can that immersive menus are gone, if the pattern code isn't enough.
	// Only applied to shell32, as application to ExplorerFrame breaks the program list hover behaviour.
	if (shell32)
	{
		ChangeImportedAddress(shell32, "user32.dll", SystemParametersInfoW, SystemParametersInfoWNEW);
	}

	// Load functions needed for task enumeration hook
	HMODULE user32 = LoadLibrary(L"user32.dll");
	IsShellFrameWindow = (IsShellWindow_t)GetProcAddress(user32, (LPCSTR)2573);
	GhostWindowFromHungWindow = (GhostWindowFromHungWindow_t)GetProcAddress(user32, "GhostWindowFromHungWindow");
	ChangeImportedAddress(GetModuleHandle(NULL), "user32.dll", IsWindowVisible, IsWindowVisibleNEW); // perform the actual hook

	// Add DWM colorization attributes to taskbar and start menu (how this renders is theme-dependent).
	SetWindowCompositionAttribute = (SetWindowCompositionAttributeAPI)GetProcAddress(GetModuleHandle(L"user32.dll"), "SetWindowCompositionAttribute");
	ChangeImportedAddress(GetModuleHandle(NULL), "user32.dll", SetWindowCompositionAttribute, SetWindowCompositionAttributeNEW);

	// Disable WindowRgn modifications under most circumstances temporarily due to visual issues
	ChangeImportedAddress(GetModuleHandle(NULL), "user32.dll", SetWindowRgn, SetWindowRgnNEW);
}

// Import address changes for kernel32.dll modulename
void PatchKernel32()
{
	// Change appid
	ChangeImportedAddress(GetModuleHandle(NULL), "kernel32.dll", SetErrorMode, SetErrorModeNEW);
}

// Import address changes for uxtheme.dll modulename
void PatchUxTheme()
{

	// Disable DWM composition as quickly as we can (if registry key set)
	ChangeImportedAddress(GetModuleHandle(NULL), "uxtheme.dll", IsCompositionActive, IsCompositionActiveNEW);

}

// Import address changes for dwmapi.dll modulename
void PatchDwmApi()
{
	// Declare this type so we can use it elsewhere
	DwmpUpdateAccentBlurRect = (DwmpUpdateAccentBlurRect_t)GetProcAddress(GetModuleHandle(L"dwmapi.dll"), (LPSTR)159);

	// Force explorer windows to see DWM as off while classic/high contrast/non-themed mode is active
	ChangeImportedAddress(GetModuleHandle(NULL), "dwmapi.dll", DwmIsCompositionEnabled, DwmIsCompositionEnabledNEW);
	ChangeImportedAddress(GetModuleHandle(NULL), "dwmapi.dll", DwmExtendFrameIntoClientArea, DwmExtendFrameIntoClientAreaNEW);
	ChangeImportedAddress(GetModuleHandle(NULL), "dwmapi.dll", DwmSetWindowAttribute, DwmSetWindowAttributeNEW);

	// Adjust DwmEnableBlurBehindWindow behaviour as necessary
	ChangeImportedAddress(GetModuleHandle(NULL), "dwmapi.dll", DwmEnableBlurBehindWindow, DwmEnableBlurBehindWindowNEW);

	// Adapt colorization api
	DwmGetColorizationParametersOrig = (SHPtrParamAPI)GetProcAddress(GetModuleHandle(L"dwmapi.dll"), (LPSTR)127);
	DwmpActivateLivePreview = (decltype(DwmpActivateLivePreview))GetProcAddress(GetModuleHandle(L"dwmapi.dll"), (LPSTR)113);
	ChangeImportedAddress(GetModuleHandle(NULL), "dwmapi.dll", DwmpActivateLivePreview, DwmpActivateLivePreviewNEW);
	ChangeImportedAddress(GetModuleHandle(NULL), "dwmapi.dll", DwmGetColorizationParametersOrig, DwmGetColorizationParametersNEW);
}


// Consolidate all of the above so they can be changed at runtime as needed
void ChangeAddressImports()
{
	PatchShell32();
	PatchUser32();
	PatchKernel32();
	PatchUxTheme();
	PatchDwmApi();
}
