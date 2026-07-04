#pragma once
#pragma warning(disable:4302)
#pragma warning(disable:4311)
#pragma warning(disable:4312)

#include "common.h"
#include "dbgprint.h"
#include "OptionConfig.h"
#include "OSVersion.h"
#include "TypeDefinitions.h"
#include "ThemeManager.h"
#include "RegistryManager.h"

// Ittr: Code that doesn't relate to specific hooks resides here
// e.g. helper functions, HWND retrieval functions, error messages, non-descript registry changes

BOOL WINAPI RetTrue()
{
	return TRUE;
}

BOOL FileExists(LPCTSTR szPath)
{
	DWORD dwAttrib = GetFileAttributes(szPath);

	return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
		!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

HMODULE GetCurrentModuleHandle() //use for internal resource calls... honestly i just wanted to show it could be done 
{
	HMODULE hMod = NULL;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&GetCurrentModuleHandle), &hMod);
	return hMod;
}

bool IsClassicTheme(void)
{
	return !IsThemeActive() || s_ClassicTheme || IsHighContrastEnabled();
}

bool IsCompositionManuallyDisabled(void)
{
	return s_DisableComposition || IsHighContrastEnabled();
}
bool ShouldDisableAeroPeek(void)
{
	return !IsAppThemed() || IsClassicTheme() || !IsCompositionActive() || IsCompositionManuallyDisabled();
}

bool ShouldForceExplorerFrameDwmOff(void)
{
	return !IsAppThemed() || IsClassicTheme() || IsCompositionManuallyDisabled();
}

bool ShouldDisableShellWindowTransparency(void)
{
	return !IsAppThemed() || IsClassicTheme() || IsCompositionManuallyDisabled();
}

bool ShouldApplyShellWindowAccent(void)
{
	return !ShouldDisableShellWindowTransparency() && IsCompositionActive() && s_ColorizationOptions != 0;
}

bool AllowThemes(void)
{
	return !IsClassicTheme();
}

static HWND GetTaskbarWnd()
{
	if (!hwnd_taskbar)
		hwnd_taskbar = FindWindow(L"Shell_TrayWnd", NULL);
	return hwnd_taskbar;
}

static BOOL CALLBACK FindSMCallback(HWND hwnd, LPARAM lParam)
{
	if (GetClassWord(hwnd, GCW_ATOM) == (ATOM)lParam && (GetProp(hwnd, L"StartMenuTag")))
	{
		hwnd_startmenu = hwnd;
		return FALSE;
	}
	return TRUE;
}

static HWND GetStartMenuWnd()
{
	if (!hwnd_startmenu || !IsWindow(hwnd_startmenu))
	{
		WNDCLASS dummy = { 0 };
		ATOM dv2atom = GetClassInfo(GetModuleHandle(NULL), L"DV2ControlHost", &dummy);
		EnumThreadWindows(GetCurrentThreadId(), FindSMCallback, (LPARAM)dv2atom);
	}
	return hwnd_startmenu;
}

static HWND GetThumbnailWnd()
{
	if (!hwnd_taskthumb)
		hwnd_taskthumb = FindWindow(L"TaskListThumbnailWnd", NULL);
	return hwnd_taskthumb;
}
static bool IsExplorerFrameWindow(HWND hwnd)
{
	if (!hwnd)
		return false;

	HWND root = GetAncestor(hwnd, GA_ROOT);
	if (!root)
		root = hwnd;

	WCHAR className[64] = {};
	if (!GetClassNameW(root, className, ARRAYSIZE(className)))
		return false;

	return !StrCmpW(className, L"CabinetWClass") || !StrCmpW(className, L"ExploreWClass");
}

static bool ShouldTreatDwmAsDisabledForExplorerFrame(HWND hwnd)
{
	return ShouldForceExplorerFrameDwmOff() && IsExplorerFrameWindow(hwnd);
}

static void RefreshExplorerFrameNcArea(HWND hwnd);

static bool IsCoreWrapperWindow(HWND hwnd)
{
	if (!hwnd)
		return false;

	HWND taskbar = GetTaskbarWnd();
	if (taskbar && (hwnd == taskbar || IsChild(taskbar, hwnd)))
		return true;

	HWND startMenu = GetStartMenuWnd();
	if (startMenu && (hwnd == startMenu || IsChild(startMenu, hwnd)))
		return true;

	HWND thumbnail = GetThumbnailWnd();
	if (thumbnail && (hwnd == thumbnail || IsChild(thumbnail, hwnd)))
		return true;

	return IsExplorerFrameWindow(hwnd);
}

static bool IsShellDialogWindow(HWND hwnd)
{
	if (!hwnd)
		return false;

	HWND root = GetAncestor(hwnd, GA_ROOT);
	if (!root)
		root = hwnd;

	WCHAR className[64] = {};
	if (!GetClassNameW(root, className, ARRAYSIZE(className)))
		return false;

	bool isDialogClass = !StrCmpW(className, L"#32770") || !StrCmpW(className, L"Shell_Dialog") || !StrCmpW(className, L"Shell_Dim") || !StrCmpW(className, L"NotifyIconOverflowWindow");
	if (!isDialogClass)
		return false;

	DWORD processId = 0;
	GetWindowThreadProcessId(root, &processId);
	return processId == GetCurrentProcessId();
}

static const LPCWSTR CLASSIC_DIALOG_PROP = L"Explorer7ClassicDialog";
static const LPCWSTR THEME_SUBAPP_PROP = (LPCWSTR)0xA911;
static const LPCWSTR THEME_SUBID_PROP = (LPCWSTR)0xA910;
static const LPCWSTR CLASSIC_FRAME_PROP = L"Explorer7ClassicFrame";
static const LPCWSTR CLASSIC_SUBAPP_PROP = L"Explorer7ClassicSubApp";
static const LPCWSTR CLASSIC_SUBID_PROP = L"Explorer7ClassicSubId";
static const LPCWSTR EXPLORER_FRAME_PREVPROC_PROP = L"Explorer7FramePrevProc";
static LRESULT CALLBACK ExplorerFrameProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void ClearForcedActiveWindowAppearance(HWND hwnd);
void DisableWindowNcRendering(HWND hwnd);
void RestoreWindowNcRendering(HWND hwnd);
void NotifyWindowCompositionChanged(HWND wnd);

static void ApplyDialogWindowTheme(HWND hwnd, LPCWSTR pszSubApp, LPCWSTR pszSubId)
{
	LPCWSTR themeArgs[2] = { pszSubApp, pszSubId };
	SetWindowTheme(hwnd, pszSubApp, pszSubId);
	EnumChildWindows(hwnd, [](HWND child, LPARAM lParam) -> BOOL
	{
		LPCWSTR* themeArgs = reinterpret_cast<LPCWSTR*>(lParam);
		SetWindowTheme(child, themeArgs[0], themeArgs[1]);
		return TRUE;
	}, (LPARAM)themeArgs);
}

static void RefreshShellDialogVisuals(HWND hwnd)
{
	RefreshExplorerFrameNcArea(hwnd);
	RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static void SyncShellDialogTheme(HWND hwnd)
{
	if (!IsShellDialogWindow(hwnd))
		return;

	if (IsClassicTheme())
	{
		if (!GetPropW(hwnd, CLASSIC_DIALOG_PROP))
		{
			SetPropW(hwnd, CLASSIC_DIALOG_PROP, (HANDLE)1);
			ClearForcedActiveWindowAppearance(hwnd);
			ApplyDialogWindowTheme(hwnd, L"", L"");
			RefreshShellDialogVisuals(hwnd);
		}
	}
	else if (GetPropW(hwnd, CLASSIC_DIALOG_PROP))
	{
		RemovePropW(hwnd, CLASSIC_DIALOG_PROP);
		ApplyDialogWindowTheme(hwnd, NULL, NULL);
		RefreshShellDialogVisuals(hwnd);
	}
}

static void CacheExplorerThemeAtom(HWND hwnd, LPCWSTR sourceProp, LPCWSTR cacheProp)
{
	ATOM atom = (ATOM)(ULONG_PTR)GetPropW(hwnd, sourceProp);
	if (!atom || GetPropW(hwnd, cacheProp))
		return;

	WCHAR buffer[260];
	UINT copied = GetAtomNameW(atom, buffer, ARRAYSIZE(buffer));
	if (!copied)
		return;

	ATOM cachedAtom = AddAtomW(buffer);
	if (cachedAtom)
	{
		SetPropW(hwnd, cacheProp, (HANDLE)(ULONG_PTR)cachedAtom);
	}
}

static void ReleaseExplorerThemeAtom(HWND hwnd, LPCWSTR cacheProp)
{
	ATOM atom = (ATOM)(ULONG_PTR)RemovePropW(hwnd, cacheProp);
	if (atom)
	{
		DeleteAtom(atom);
	}
}

static LPCWSTR RestoreExplorerThemeString(HWND hwnd, LPCWSTR cacheProp, WCHAR(&buffer)[260])
{
	ATOM atom = (ATOM)(ULONG_PTR)GetPropW(hwnd, cacheProp);
	if (!atom)
		return NULL;

	if (!GetAtomNameW(atom, buffer, ARRAYSIZE(buffer)))
		return NULL;

	return buffer;
}

static void EnsureExplorerFrameSubclass(HWND hwnd)
{
	if (!hwnd || !IsExplorerFrameWindow(hwnd) || GetPropW(hwnd, EXPLORER_FRAME_PREVPROC_PROP))
		return;

	WNDPROC prevProc = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
	if (!prevProc)
		return;

	SetPropW(hwnd, EXPLORER_FRAME_PREVPROC_PROP, (HANDLE)prevProc);
	SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)ExplorerFrameProc);
}

static void RestoreExplorerFrameTheme(HWND hwnd)
{
	WCHAR subApp[260];
	WCHAR subId[260];
	LPCWSTR pszSubApp = RestoreExplorerThemeString(hwnd, CLASSIC_SUBAPP_PROP, subApp);
	LPCWSTR pszSubId = RestoreExplorerThemeString(hwnd, CLASSIC_SUBID_PROP, subId);
	SetWindowTheme(hwnd, pszSubApp, pszSubId);
}

static void ReleaseExplorerFrameState(HWND hwnd)
{
	ReleaseExplorerThemeAtom(hwnd, CLASSIC_SUBAPP_PROP);
	ReleaseExplorerThemeAtom(hwnd, CLASSIC_SUBID_PROP);

	WNDPROC prevProc = (WNDPROC)RemovePropW(hwnd, EXPLORER_FRAME_PREVPROC_PROP);
	if (prevProc)
	{
		SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)prevProc);
	}
}

static LRESULT CALLBACK ExplorerFrameProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	WNDPROC prevProc = (WNDPROC)GetPropW(hwnd, EXPLORER_FRAME_PREVPROC_PROP);
	if (!prevProc)
	{
		return DefWindowProcW(hwnd, uMsg, wParam, lParam);
	}

	if (!IsClassicTheme() && (uMsg == WM_NCACTIVATE || uMsg == WM_ACTIVATE || uMsg == WM_SETFOCUS))
	{
		RestoreExplorerFrameTheme(hwnd);
	}

	if (uMsg == WM_NCDESTROY)
	{
		LRESULT ret = CallWindowProcW(prevProc, hwnd, uMsg, wParam, lParam);
		ReleaseExplorerFrameState(hwnd);
		return ret;
	}

	return CallWindowProcW(prevProc, hwnd, uMsg, wParam, lParam);
}

static void RefreshExplorerFrameNcArea(HWND hwnd)
{
	SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOSENDCHANGING | SWP_ASYNCWINDOWPOS);
	RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
}

static void SyncExplorerFrameTheme(HWND hwnd)
{
	if (!IsExplorerFrameWindow(hwnd))
		return;

	if (IsClassicTheme())
	{
		if (!GetPropW(hwnd, CLASSIC_FRAME_PROP))
		{
			CacheExplorerThemeAtom(hwnd, THEME_SUBAPP_PROP, CLASSIC_SUBAPP_PROP);
			CacheExplorerThemeAtom(hwnd, THEME_SUBID_PROP, CLASSIC_SUBID_PROP);
			EnsureExplorerFrameSubclass(hwnd);
			SetPropW(hwnd, CLASSIC_FRAME_PROP, (HANDLE)1);
			ClearForcedActiveWindowAppearance(hwnd);
			SetWindowTheme(hwnd, L"", L"");
			DisableWindowNcRendering(hwnd);
			RefreshExplorerFrameNcArea(hwnd);
		}
	}
	else if (GetPropW(hwnd, CLASSIC_FRAME_PROP))
	{
		RemovePropW(hwnd, CLASSIC_FRAME_PROP);
		RestoreExplorerFrameTheme(hwnd);
		RestoreWindowNcRendering(hwnd);
		RefreshExplorerFrameNcArea(hwnd);
	}
}

static bool IsWrapperManagedWindow(HWND hwnd)
{
	if (!hwnd)
		return false;

	return IsCoreWrapperWindow(hwnd) || IsShellDialogWindow(hwnd);
}

int g_fDPIAware = 0;
int g_nScreenDpi = 0;
int g_fForcedDpi = 0;
__int64 GetScreenDpi(void)
{
	int v0; // eax
	HDC DC; // rax
	HDC v3; // rbx

	if (!g_fForcedDpi)
	{
		v0 = IsProcessDPIAware();
		if (g_fDPIAware != v0 || !g_nScreenDpi)
		{
			g_fDPIAware = v0;
			g_nScreenDpi = 96;
			DC = GetDC(0LL);
			v3 = DC;
			if (DC)
			{
				g_nScreenDpi = GetDeviceCaps(DC, 88);
				ReleaseDC(0LL, v3);
			}
		}
	}
	return (unsigned int)g_nScreenDpi;
}


// Ittr: Forcing this change fixes colorization on aero.msstyles for 1809+ on taskbar and start menu ONLY.
void EnsureWindowColorization()
{
	if (g_osVersion.BuildNumber() >= 17763)
	{
		DWORD value = 0; // initialise in memory
		DWORD colorVal = 1; // doesn't work when reduced to a single string, annoying but atleast we can use it here
		RegGetDWORD(HKEY_CURRENT_USER, sz_DesktopWindowManagerKey, L"EnableWindowColorization", &value); // output the data from attributes key...

		if (value != colorVal) // basically if the attribute value doesn't exist or is the wrong value...
		{
			RegSetDWORD(HKEY_CURRENT_USER, sz_DesktopWindowManagerKey, L"EnableWindowColorization", &colorVal); // apply folder attributes, arguably the most important part
		}
	}
}

DWORD GetColorizationColor()
{
	DWMCOLORIZATIONPARAMS colors;
	CHAR buffer[0x28];
	memset(buffer, 0, 0x28);
	DwmGetColorizationParametersOrig(&buffer);
	memcpy(&colors, (PVOID)buffer, sizeof(DWMCOLORIZATIONPARAMS));

	int a = (colors.ColorizationColor >> 24) & 0xFF;
	int r = (colors.ColorizationColor >> 16) & 0xFF;
	int g = (colors.ColorizationColor >> 8) & 0xFF;
	int b = (colors.ColorizationColor) & 0xFF;

	// Automatic colorization can report alpha as 0 on Windows 10.
	if (s_ColorizationOptions != 3 && a == 0x00 && (r != 0x00 || g != 0x00 || b != 0x00)) // only apply if it appears that the user is trying to set an actual colour - full transparency remains possible!
	{
		a = 0xC4; // we default to this as it's used by the majority of win10/11 default colours
	}

	// Approximate default Windows 8.1 translucency if user has regular 10/11 colours used and has not manually set to 0xC4
	if (a == 0xC4)
	{
		a = 0x74;
	}

	// mode 4 (gradient non-transparent is buggy) + current thumbnail edge case 
	if (s_ColorizationOptions == 4) 
	{
		a = 0xFF;
	}

	// Windows 10 and 11 users specifically without glass tools may struggle to adjust color opacity, this optional override fixes this
	if (s_OverrideAlpha && (s_ColorizationOptions == 1 || s_ColorizationOptions == 2))
	{
		a = (s_AlphaValue) & 0xFF;
	}

	if (s_ColorizationOptions == 3)
	{
		GetThemeName = (GetThemeName_t)GetProcAddress(LoadLibrary(L"uxtheme.dll"), (LPSTR)74);
		RefreshImmersiveColorPolicyState = (RefreshImmersiveColorPolicyState_t)GetProcAddress(LoadLibrary(L"uxtheme.dll"), (LPSTR)104);
		GetIsImmersiveColorUsingHighContrast = (GetIsImmersiveColorUsingHighContrast_t)GetProcAddress(LoadLibrary(L"uxtheme.dll"), (LPSTR)106);
		GetUserColorPreference = (GetUserColorPreference_t)GetProcAddress(LoadLibrary(L"uxtheme.dll"), (LPSTR)120);
		GetColorFromPreference = (GetColorFromPreference_t)GetProcAddress(LoadLibrary(L"uxtheme.dll"), (LPSTR)121);
	}

	IMMERSIVE_COLOR_TYPE imclr;

	switch (s_AcrylicAlt)
	{
		case 1:
			imclr = IMCLR_SystemAccentDark2;
			break;
		case 2:
			imclr = IMCLR_SystemAccentLight2;
			break;
		default:
			imclr = IMCLR_HardwareGutterRest;
			break;
	}

	DWORD color = (s_ColorizationOptions != 3 || s_AcrylicAlt == 3) ? ((a << 24) | (b << 16) | (g << 8) | r) : ((s_OverrideAlpha ? ((s_AlphaValue & 0xFF) << 24) : 0xCC000000) | (CImmersiveColor::GetColor(imclr) & 0xFFFFFF));
	return color;
}

ACCENT_STATE GetAccentState(bool isThumbnail)
{
	if (s_ColorizationOptions == 3) // acrylic (1803-)
		return ACCENT_ENABLE_ACRYLICBLURBEHIND;
	else if (s_ColorizationOptions == 2) // blurbehind (1507 until 11 21h2)
		return ACCENT_ENABLE_BLURBEHIND;

	if (isThumbnail) // run this block after the other ones, to ensure that pseudo-aero mode uses opaque thumbnail. using the option definition causes extreme visual bugs for some reason.
		return ACCENT_ENABLE_GRADIENT;

	// pseudo-aero & solid-color (all versions) - the replacements for option 0 & fallback for other values entered > 4
	return ACCENT_ENABLE_TRANSPARENTGRADIENT; // we use transparentgradient for both 1 and 4, as gradient has some weird hrgn side-effects on start menu

}

__forceinline WINDOWCOMPOSITIONATTRIBDATA GetTrayAccentProperties(bool isThumbnail)
{
	WINDOWCOMPOSITIONATTRIBDATA attrData;
	ACCENT_POLICY accentPolicy;

	accentPolicy.AccentState = GetAccentState(isThumbnail);
	accentPolicy.AccentFlags = (isThumbnail) ? (0x1 | 0x2 | 0x200) : (0x13);
	accentPolicy.GradientColor = GetColorizationColor();

	attrData.Attrib = WCA_ACCENT_POLICY;
	attrData.pvData = &accentPolicy;
	attrData.cbData = sizeof(accentPolicy);
	return attrData;
}

__forceinline WINDOWCOMPOSITIONATTRIBDATA GetDisabledTrayAccentProperties()
{
	WINDOWCOMPOSITIONATTRIBDATA attrData;
	ACCENT_POLICY accentPolicy = {};
	accentPolicy.AccentState = ACCENT_DISABLED;

	attrData.Attrib = WCA_ACCENT_POLICY;
	attrData.pvData = &accentPolicy;
	attrData.cbData = sizeof(accentPolicy);
	return attrData;
}

void DisableShellWindowBlur(HWND hwnd)
{
	if (!hwnd || !IsWindow(hwnd))
		return;

	DWM_BLURBEHIND blurBehind = {};
	blurBehind.dwFlags = DWM_BB_ENABLE;
	blurBehind.fEnable = FALSE;
	DwmEnableBlurBehindWindow(hwnd, &blurBehind);
}

void UpdateShellWindowAccent(HWND hwnd, bool isThumbnail)
{
	if (ShouldApplyShellWindowAccent())
	{
		SetWindowCompositionAttribute(hwnd, &GetTrayAccentProperties(isThumbnail));
		return;
	}

	if (!ShouldDisableShellWindowTransparency())
	{
		return;
	}

	SetWindowCompositionAttribute(hwnd, &GetDisabledTrayAccentProperties());
	ClearForcedActiveWindowAppearance(hwnd);
	DisableShellWindowBlur(hwnd);
}

void ForceActiveWindowAppearance(HWND hwnd)
{
	BOOL bForceActiveWindowAppearance = true;
	WINDOWCOMPOSITIONATTRIBDATA attrData;
	attrData.Attrib = WCA_FORCE_ACTIVEWINDOW_APPEARANCE;
	attrData.pvData = &bForceActiveWindowAppearance;
	attrData.cbData = sizeof(bForceActiveWindowAppearance);
	SetWindowCompositionAttribute(hwnd, &attrData);
}
void ClearForcedActiveWindowAppearance(HWND hwnd)
{
	BOOL bForceActiveWindowAppearance = false;
	WINDOWCOMPOSITIONATTRIBDATA attrData;
	attrData.Attrib = WCA_FORCE_ACTIVEWINDOW_APPEARANCE;
	attrData.pvData = &bForceActiveWindowAppearance;
	attrData.cbData = sizeof(bForceActiveWindowAppearance);
	SetWindowCompositionAttribute(hwnd, &attrData);
}

void DisableWindowNcRendering(HWND hwnd)
{
	if (!hwnd || !IsWindow(hwnd))
		return;

	const MARGINS margins = { 0 };
	if (DwmExtendFrameIntoClientAreaOrig)
	{
		DwmExtendFrameIntoClientAreaOrig(hwnd, &margins);
	}
	else
	{
		static auto fn = reinterpret_cast<DwmExtendFrameIntoClientAreaAPI>(GetProcAddress(GetModuleHandleW(L"dwmapi.dll"), "DwmExtendFrameIntoClientArea"));
		if (fn)
			fn(hwnd, &margins);
	}
	int bNCRenderingPolicy = DWMNCRP_DISABLED;
	WINDOWCOMPOSITIONATTRIBDATA attrData;
	attrData.Attrib = WCA_NCRENDERING_POLICY;
	attrData.pvData = &bNCRenderingPolicy;
	attrData.cbData = sizeof(bNCRenderingPolicy);
	SetWindowCompositionAttribute(hwnd, &attrData);
	DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &bNCRenderingPolicy, sizeof(bNCRenderingPolicy));
}
void RestoreWindowNcRendering(HWND hwnd)
{
	if (!hwnd || !IsWindow(hwnd))
		return;

	int bNCRenderingPolicy = DWMNCRP_USEWINDOWSTYLE;
	WINDOWCOMPOSITIONATTRIBDATA attrData;
	attrData.Attrib = WCA_NCRENDERING_POLICY;
	attrData.pvData = &bNCRenderingPolicy;
	attrData.cbData = sizeof(bNCRenderingPolicy);
	SetWindowCompositionAttribute(hwnd, &attrData);
	DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &bNCRenderingPolicy, sizeof(bNCRenderingPolicy));
}

void RestoreManagedWindowComposition(HWND hwnd)
{
	if (IsExplorerFrameWindow(hwnd) && GetPropW(hwnd, CLASSIC_FRAME_PROP))
	{
		SyncExplorerFrameTheme(hwnd);
	}

	RestoreWindowNcRendering(hwnd);
	if (IsExplorerFrameWindow(hwnd))
	{
		SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOSENDCHANGING);
		RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
	}
	PostMessage(hwnd, WM_DWMCOMPOSITIONCHANGED, 0, 0);
}

BOOL CALLBACK RestoreExplorerComposition(HWND wnd, LPARAM prm)
{
	UNREFERENCED_PARAMETER(prm);
	if (IsExplorerFrameWindow(wnd))
	{
		RestoreManagedWindowComposition(wnd);
	}
	return TRUE;
}

void RestoreShellWindowComposition(HWND wnd)
{
	if (!wnd || !IsWindow(wnd))
		return;

	RestoreManagedWindowComposition(wnd);
}

void RestoreShellWindowsComposition(HWND excludeWnd)
{
	HWND taskbar = GetTaskbarWnd();
	if (taskbar && taskbar != excludeWnd)
	{
		RestoreShellWindowComposition(taskbar);
	}

	HWND startMenu = GetStartMenuWnd();
	if (startMenu && startMenu != excludeWnd)
	{
		RestoreShellWindowComposition(startMenu);
	}

	HWND thumbnail = GetThumbnailWnd();
	if (thumbnail && thumbnail != excludeWnd)
	{
		RestoreShellWindowComposition(thumbnail);
	}
}

void RestoreShellDialogComposition(HWND wnd)
{
	if (!wnd || !IsWindow(wnd) || !IsShellDialogWindow(wnd))
		return;

	SyncShellDialogTheme(wnd);
	RestoreWindowNcRendering(wnd);
	RefreshShellDialogVisuals(wnd);
	NotifyWindowCompositionChanged(wnd);
}

BOOL CALLBACK RestoreShellDialogWindowsComposition(HWND wnd, LPARAM prm)
{
	UNREFERENCED_PARAMETER(prm);
	RestoreShellDialogComposition(wnd);
	return TRUE;
}

const UINT ThemeChangeMessage = WM_USER + 69420;

void RefreshWindowTheme(HWND wnd)
{
	PostMessage(wnd, WM_THEMECHANGED, 0, 0);
	dbgprintf(L"themechanged sent to %i", wnd);
}
void RefreshWindowThemeChildren(HWND wnd)
{
	if (!wnd || !IsWindow(wnd))
		return;

	EnumChildWindows(wnd, [](HWND child, LPARAM) -> BOOL
	{
		RedrawWindow(child, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
		return TRUE;
	}, 0);

	RedrawWindow(wnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void NotifyWindowCompositionChanged(HWND wnd)
{
	PostMessage(wnd, WM_DWMCOMPOSITIONCHANGED, 0, 0);
}

void RefreshExplorerFrameTheme(HWND wnd)
{
	if (!wnd || !IsWindow(wnd) || !IsExplorerFrameWindow(wnd))
		return;

	SyncExplorerFrameTheme(wnd);
	RefreshWindowTheme(wnd);
}

BOOL CALLBACK RefreshExplorerFrameWindows(HWND wnd, LPARAM prm)
{
	UNREFERENCED_PARAMETER(prm);
	RefreshExplorerFrameTheme(wnd);
	return TRUE;
}

void RefreshShellDialogTheme(HWND wnd)
{
	if (!wnd || !IsWindow(wnd) || !IsShellDialogWindow(wnd))
		return;

	SyncShellDialogTheme(wnd);
	RefreshWindowTheme(wnd);
}

BOOL CALLBACK RefreshShellDialogWindows(HWND wnd, LPARAM prm)
{
	UNREFERENCED_PARAMETER(prm);
	RefreshShellDialogTheme(wnd);
	return TRUE;
}

BOOL CALLBACK NotifyShellDialogCompositionChanged(HWND wnd, LPARAM prm)
{
	UNREFERENCED_PARAMETER(prm);
	if (IsShellDialogWindow(wnd))
	{
		NotifyWindowCompositionChanged(wnd);
	}
	return TRUE;
}

BOOL CALLBACK NotifyExplorerCompositionChanged(HWND wnd, LPARAM prm)
{
	UNREFERENCED_PARAMETER(prm);
	if (IsExplorerFrameWindow(wnd))
	{
		NotifyWindowCompositionChanged(wnd);
	}
	return TRUE;
}

void RefreshShellWindow(HWND wnd)
{
	if (!wnd || !IsWindow(wnd))
		return;

	RefreshWindowTheme(wnd);
}

void NotifyShellCompositionChanged(HWND wnd)
{
	if (!wnd || !IsWindow(wnd))
		return;

	NotifyWindowCompositionChanged(wnd);
}

void RefreshShellWindows(HWND excludeWnd)
{
	HWND taskbar = GetTaskbarWnd();
	if (taskbar && taskbar != excludeWnd)
	{
		RefreshShellWindow(taskbar);
	}

	HWND startMenu = GetStartMenuWnd();
	if (startMenu && startMenu != excludeWnd)
	{
		RefreshShellWindow(startMenu);
	}

	HWND thumbnail = GetThumbnailWnd();
	if (thumbnail && thumbnail != excludeWnd)
	{
		RefreshShellWindow(thumbnail);
	}
}

void NotifyShellWindowsCompositionChanged(HWND excludeWnd)
{
	HWND taskbar = GetTaskbarWnd();
	if (taskbar && taskbar != excludeWnd)
	{
		NotifyShellCompositionChanged(taskbar);
	}

	HWND startMenu = GetStartMenuWnd();
	if (startMenu && startMenu != excludeWnd)
	{
		NotifyShellCompositionChanged(startMenu);
	}

	HWND thumbnail = GetThumbnailWnd();
	if (thumbnail && thumbnail != excludeWnd)
	{
		NotifyShellCompositionChanged(thumbnail);
	}
}

BOOL WINAPI GetWindowBandNew(HWND hwnd, DWORD* out);

BOOL __stdcall GetWindowBandHelper(HWND hwnd, ZBID* out)
{
	if (GetWindowBandOrig)
	{
		return GetWindowBandNew(hwnd, (DWORD*)out);
	}

	static BOOL(__stdcall * fn)(HWND, ZBID*) = nullptr;
	if (!fn)
	{
		HMODULE h = GetModuleHandleW(L"user32.dll");
		if (h)
			fn = (decltype(fn))GetProcAddress(h, "GetWindowBand");
		//FAIL_FAST_IF_NULL(fn);
		if (!fn)
			return 0;
	}
	return fn(hwnd, out);
}

BOOL IsShellManagedWindow(HWND hwnd)
{
	static IsShellManagedWindow_t fn = nullptr;
	if (!fn)
	{
		HMODULE h = GetModuleHandleW(L"user32.dll");
		if (h)
			fn = (IsShellManagedWindow_t)GetProcAddress(h, MAKEINTRESOURCEA(2574));
		//FAIL_FAST_IF_NULL(fn);
		if (!fn)
			return 0;
	}
	return fn(hwnd);
}

bool ShouldExcludeFromTaskbar(HWND hwnd)
{
	wchar_t text[256];
	GetWindowTextW(hwnd, text, 255);

	if (!StrCmpW(text, L"Microsoft Text Input Application") || !StrCmpW(text, L"Windows Shell Experience Host") || !StrCmpW(text, L"Start") || !StrCmpW(text, L"Search"))
		return true;

	return false;
}

bool IsValidDesktopZOrderBand(HWND hwnd, BOOL bCheckShellManagedWindow)
{
	bool bValid = false;

	ZBID band;
	if (GetWindowBandHelper(hwnd, &band))
	{
		bValid = s_bandInclusionData[band].bInclude;

		//if (Feature_WindowTabHost && (HWND)GetPropW(hwnd, (LPCWSTR)0xA920))
		//	bValid = true;

		if (bValid && bCheckShellManagedWindow)
			bValid = !IsShellManagedWindow(hwnd) || ShellManagedWindowHelper::ShouldTreatShellManagedWindowAsNotShellManaged(hwnd);
	}

	if (bValid)
		bValid = !ShouldExcludeFromTaskbar(hwnd);

	return bValid;
}

bool IsWindowNotDesktopOrTray(HWND hwnd)
{
	if (!IsWindow(hwnd) || !IsValidDesktopZOrderBand(hwnd, TRUE) || hwnd == hwnd_taskbar || (v_hwndDesktop && hwnd == *v_hwndDesktop))
		return false;

	return true;
}

BOOL WINAPI IsWindowVisibleNEW(HWND hWnd)
{
	if (!IsWindowVisible(hWnd) || !IsValidDesktopZOrderBand(hWnd, TRUE))
		return FALSE;

	BOOL bCloaked;
	DwmGetWindowAttribute(hWnd, DWMWA_CLOAKED, &bCloaked, sizeof(BOOL));
	if (bCloaked)
		return FALSE;

	if ((ShouldForceExplorerFrameDwmOff() || GetPropW(hWnd, CLASSIC_FRAME_PROP)) && IsExplorerFrameWindow(hWnd))
	{
		SyncExplorerFrameTheme(hWnd);
	}
	else if (IsShellDialogWindow(hWnd))
	{
		SyncShellDialogTheme(hWnd);
	}

	if (IsShellFrameWindow && GhostWindowFromHungWindow)
	{
		if (IsShellFrameWindow(hWnd) && !GhostWindowFromHungWindow(hWnd))
			return TRUE;
	}

	if (IsShellManagedWindow(hWnd) && GetPropW(hWnd, L"Microsoft.Windows.ShellManagedWindowAsNormalWindow") == NULL)
		return FALSE;

	return TRUE;
}

__int64 ShouldAddWindowToTray(HWND hwnd)
{
	BOOL ret = IsWindowNotDesktopOrTray(hwnd) && IsWindowVisibleNEW(hwnd) && ShouldAddWindowToTrayHelper(hwnd);
	//dbgprintf(L"ShouldAddWindowToTray %i", (int)ret);
	return ret;
}

// Create all programs shellfolder on 1607+ where it doesn't already exist
void CreateShellFolder()
{
	//addendum: using the regular HKLM location is not viable for non-administrator users so we store in HKCU, which causes it to turn up in HKEY_USERS somewhere. 
	//this shouldn't work, but it does :P
	if (g_osVersion.BuildNumber() >= 14393) // Ittr: byebye shellfolder.reg
	{
		DWORD value = 0; // initialise in memory
		DWORD attrVal = 0x28100000; // doesn't work when reduced to a single string, annoying but atleast we can use it here
		RegGetDWORD(HKEY_CURRENT_USER, sz_ShellFolder3, L"Attributes", &value); // output the data from attributes key...

		if (value != attrVal) // basically if the attribute value doesn't exist or is the wrong value...
		{
			// we create all the relevant values. issue solved for new users - program list works out of the box now
			RegSetSZ(HKEY_CURRENT_USER, sz_ShellFolder, NULL, (DWORD*)L"Programs Folder and Fast Items"); // create clsid name
			RegSetExpandSZ(HKEY_CURRENT_USER, sz_ShellFolder2, NULL, (DWORD*)L"%SystemRoot%\\system32\\shell32.dll"); // point it to shell32
			RegSetSZ(HKEY_CURRENT_USER, sz_ShellFolder2, L"ThreadingModel", (DWORD*)L"Apartment"); // regular threading model criteria...
			RegSetDWORD(HKEY_CURRENT_USER, sz_ShellFolder3, L"Attributes", &attrVal); // apply folder attributes, arguably the most important part
		}
	}
}


// Warn and exit on unsupported OS builds
void UnsupportedBuildWarningAndExit()
{
	ULONG build = g_osVersion.BuildNumber();
	if (build < 9999 || build > 20000)
	{
		MessageBoxW(NULL, L"This build of Windows is not supported.", L"explorer7", MB_ICONEXCLAMATION);
		ExitProcess(0);
	}
}

// One-off warning for pre-release version
void FirstRunPrereleaseWarning()
{
#ifdef PRERELEASE_COPY // do nothing if this isn't defined
	DWORD value = 0;
	RegGetDWORD(HKEY_CURRENT_USER, c_szSubkey, L"FirstRunPrereleaseCheck", &value);
	if (value != 1)
	{
		MessageBoxW(NULL, L"Evaluation copy.\nFor testing purposes only.", L"explorer7", MB_ICONEXCLAMATION);
		DWORD newValue = 1;
		RegSetDWORD(HKEY_CURRENT_USER, c_szSubkey, L"FirstRunPrereleaseCheck", &newValue);
	}
#endif
}

// Ittr: The following 3 functions are here rather than any specific imports header because they are used by 2 different patch types
HWND WINAPI CreateWindowInBandNew(DWORD dwExStyle,
	LPCWSTR lpClassName,
	LPCWSTR lpWindowName,
	DWORD dwStyle,
	int x,
	int y,
	int nWidth,
	int nHeight,
	HWND hwndParent,
	HMENU hMenu,
	HINSTANCE hInstance,
	LPVOID lpParam,
	DWORD dwBand)
{
	if (s_EnableImmersiveShellStack == 1) // immersive enabled
	{
		DWORD p0 = (DWORD)_ReturnAddress();
		dwExStyle = dwExStyle | WS_EX_TOOLWINDOW; // TODO is this needed?
		HWND ret = CreateWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hwndParent, hMenu, hInstance, lpParam);

		// Ittr: Emulate always-on-top behaviour for Windows 10 toasts
		BOOL excludeFromPeek = true;
		WCHAR className[MAX_PATH];
		GetClassName(ret, className, ARRAYSIZE(className));
		if (lstrcmp(className, L"Windows.UI.Core.CoreWindow") == 0 || lstrcmp(className, L"Shell_Dialog") == 0 || lstrcmp(className, L"Shell_Dim") == 0 || lstrcmp(className, L"NotifyIconOverflowWindow") == 0)
		{
			SetWindowPos(ret, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION);
		}

		// We do this to eliminate the ghost window
		BOOL shouldCloak = true;
		WCHAR titleBuffer[MAX_PATH];
		GetClassName(ret, titleBuffer, ARRAYSIZE(titleBuffer));
		if (lstrcmp(titleBuffer, L"ApplicationFrameWindow") == 0)
		{
			DwmSetWindowAttribute(ret, DWMWA_CLOAK, &shouldCloak, sizeof(shouldCloak));
		}

		dbgprintf(L"CREATEWINDOWINBANDNEW %i", dwBand);

		if (ret)
		{
			SetProp(ret, L"UIA_WindowVisibilityOverriden", (HANDLE)2);
			SetProp(ret, L"explorer7.WindowBand", (HANDLE)dwBand);
		}
		
		return ret;
	}
	else // Preserve legacy codepath for Windows 8.1 and non-immersive users
	{
		DWORD p0 = (DWORD)_ReturnAddress();
		dwStyle = dwStyle | WS_EX_TOOLWINDOW;
		HWND ret = CreateWindowInBandOrig(dwExStyle, (LPWSTR)lpClassName, (PVOID)lpWindowName, (PVOID)dwStyle, (PVOID)x, (PVOID)y, (PVOID)nWidth, (PVOID)nHeight, hwndParent, hMenu, hInstance, lpParam, dwBand & 1);
		dbgprintf(L"%p: CreateWindowInBand %p %s %p %p %p %p %p %p %p %p %p %p %p = %p %p", p0, dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hwndParent, hMenu, hInstance, lpParam, dwBand, ret, GetLastError());
		SetProp(ret, L"explorer7.WindowBand", (HANDLE)dwBand);
		return ret;
	}
}

HWND WINAPI CreateWindowInBandExNew(DWORD exStyle, LPWSTR szClassName, PVOID p3, PVOID p4, PVOID p5, PVOID p6, PVOID p7, PVOID p8, PVOID p9, PVOID p10, PVOID p11, PVOID p12, DWORD p13, DWORD dwTypeFlags)
{
	DWORD p0 = (DWORD)_ReturnAddress();
	exStyle = exStyle | WS_EX_TOOLWINDOW;
	HWND ret = CreateWindowInBandExOrig(exStyle, szClassName, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13 & 1, dwTypeFlags);

	// Ittr: Emulate always-on-top behaviour for Windows 10 toasts
	BOOL excludeFromPeek = true;
	WCHAR className[MAX_PATH];
	GetClassName(ret, className, ARRAYSIZE(className));
	if (lstrcmp(className, L"Windows.UI.Core.CoreWindow") == 0 || lstrcmp(className, L"Shell_Dialog") == 0 || lstrcmp(className, L"Shell_Dim") == 0 || lstrcmp(className, L"NotifyIconOverflowWindow") == 0)
	{
		SetWindowPos(ret, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION);
	}

	// We do this to eliminate the ghost window
	BOOL shouldCloak = true;
	WCHAR titleBuffer[MAX_PATH];
	GetClassName(ret, titleBuffer, ARRAYSIZE(titleBuffer));
	if (lstrcmp(titleBuffer, L"ApplicationFrameWindow") == 0)
	{
		DwmSetWindowAttribute(ret, DWMWA_CLOAK, &shouldCloak, sizeof(shouldCloak));
	}

	dbgprintf(L"%p: CreateWindowInBandEx %p %s %p %p %p %p %p %p %p %p %p %p %p = %p %p", p0, exStyle, szClassName, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, ret, GetLastError());
	dbgprintf(L"CreateWindowInBandExOrig %i", p13);

	SetProp(ret, L"UIA_WindowVisibilityOverriden", (HANDLE)2);
	SetProp(ret, L"explorer7.WindowBand", (HANDLE)p13);
	return ret;
}

BOOL WINAPI SetWindowBandNew(HWND hwnd, HWND hwndInsertAfter, DWORD flags)
{
	// Ittr: Emulate always-on-top behaviour for Windows 10 toasts
	BOOL excludeFromPeek = true;
	WCHAR className[MAX_PATH];
	GetClassName(hwnd, className, ARRAYSIZE(className));
	if (lstrcmp(className, L"Windows.UI.Core.CoreWindow") == 0 || lstrcmp(className, L"Shell_Dialog") == 0 || lstrcmp(className, L"Shell_Dim") == 0 || lstrcmp(className, L"NotifyIconOverflowWindow") == 0)
	{
		SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOREPOSITION);
	}

	SetProp(hwnd, L"explorer7.WindowBand", (HANDLE)flags);
	dbgprintf(L"SetWindowBandNew %i", flags);
	return TRUE;
}

BOOL WINAPI RegisterWindowHotkeyNew(HWND hwnd, int id, UINT mod, UINT vk)
{
	BOOL res = RegisterHotKeyApiOrg(hwnd, id, mod, vk);

	if (!res)
	{
		return TRUE;
	}

	return TRUE;
}
