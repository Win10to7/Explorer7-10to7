#include "AuthUI.h"
#include "WinXMenu.h"
#include "WinXLinks.h"
#include "OptionConfig.h"
#include "dbgprint.h"
#include <shellapi.h>

// Backported from repensky/explorer7-fork, commit f9616d7.

UINT g_winXRegisterMsg = 0;

//---Tray commands and shutdown choices---------------------

// CTray::_Command ids, the same in 7601 and 7850
#define TRAYCMD_RUN      401
#define TRAYCMD_LOGOFF   402
#define TRAYCMD_DESKTOP  407
#define TRAYCMD_EXIT     506

// shutdownux choice values, the Start menu posts them to the tray as they are
#define CHOICE_LOGOFF     0x1
#define CHOICE_UNUSABLE   0xC0000
#define CHOICE_SEPARATOR  0x400000

// Properties in CStartButton::OnContextMenu, 7601 and 7850 alike
#define STARTBUTTON_CMD_PROPERTIES 0x7FF3

// Only Next is called, the Windows 7 Start menu uses the same slot
struct DECLSPEC_NOVTABLE IEnumShutdownChoices: public IUnknown
{
	STDMETHOD(Next)(ULONG celt, ULONG* rgelt, ULONG* pceltFetched) PURE;
};

//---Menu---------------------------------------------------

// One context per popup keeps nested menu tracking from changing another menu's commands.
struct WinXMenuContext
{
	ULONG choices[16] = {};
	int choiceCount = 0;
	WinXLinks links;
};

// Command Prompt or PowerShell, decided the way 19044 twinui decides it
static bool UseCommandPrompt()
{
	// With no value x86 and x64 get PowerShell, any other machine gets Command Prompt
	bool useCmd = true;
	typedef BOOL(WINAPI* IsWow64Process2_t)(HANDLE, USHORT*, USHORT*);
	IsWow64Process2_t isWow64Process2 = (IsWow64Process2_t)GetProcAddress(
		GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2");
	USHORT process = 0, native = 0;
	if (isWow64Process2 && isWow64Process2(GetCurrentProcess(), &process, &native))
		useCmd = !(native == IMAGE_FILE_MACHINE_I386 || native == IMAGE_FILE_MACHINE_AMD64);

	// Nonzero picks Command Prompt, read on every open like twinui does
	DWORD value = 0, cb = sizeof(value);
	if (RegGetValueW(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
		L"DontUsePowerShellOnWinX", RRF_RT_REG_DWORD, NULL, &value, &cb) == ERROR_SUCCESS)
		useCmd = value != 0;
	return useCmd;
}

static HRESULT AddChoice(HMENU menu, IShutdownChoices10* choices, ULONG choice, WinXMenuContext& context)
{
	if (context.choiceCount >= ARRAYSIZE(context.choices))
		return HRESULT_FROM_WIN32(ERROR_TOO_MANY_CMDS);

	// Index 1 is the menu form with its accelerator, as the Start menu asks for it.
	WCHAR name[128];
	HRESULT hr = choices->GetChoiceName(choice, 1, name, ARRAYSIZE(name));
	if (FAILED(hr))
		return hr;
	if (!AppendMenuW(menu, MF_STRING, WINX_CHOICE_FIRST + context.choiceCount, name))
		return HRESULT_FROM_WIN32(GetLastError());
	context.choices[context.choiceCount++] = choice;
	return S_OK;
}

// The same list the Start menu power button shows, so sleep and hibernate follow Power Options
static HMENU BuildShutdownMenu(WinXMenuContext& context)
{
	HMENU menu = CreatePopupMenu();
	if (!menu)
		return nullptr;
	context.choiceCount = 0;

	IShutdownChoices10* choices = nullptr;
	IEnumShutdownChoices* list = nullptr;
	HRESULT hr = CoCreateInstance(CLSID_AuthUIShutdownChoices, NULL, CLSCTX_INPROC_SERVER,
		IID_IShutdownChoices10, (void**)&choices);
	// shutdownux only rereads sleep and hibernate on Refresh.
	if (SUCCEEDED(hr))
		hr = choices->Refresh();
	// The default choice mask leaves log off out, so it goes in by hand.
	if (SUCCEEDED(hr))
		hr = AddChoice(menu, choices, CHOICE_LOGOFF, context);
	if (SUCCEEDED(hr))
		hr = choices->GetChoiceEnumerator((IUnknown**)&list);
	if (SUCCEEDED(hr) && !list)
		hr = E_UNEXPECTED;
	if (SUCCEEDED(hr))
	{
		ULONG choice = 0;
		while ((hr = list->Next(1, &choice, NULL)) == S_OK)
		{
			// Separators, greyed out choices and a second log off are left out.
			if ((choice & (CHOICE_SEPARATOR | CHOICE_UNUSABLE)) || choice == CHOICE_LOGOFF)
				continue;
			hr = AddChoice(menu, choices, choice, context);
			if (FAILED(hr))
				break;
		}
	}
	if (list)
		list->Release();
	if (choices)
		choices->Release();
	dbgprintf(L"Win+X shutdown choices hr %X, %d listed", hr, context.choiceCount);
	if (FAILED(hr))
	{
		context.choiceCount = 0;
		DestroyMenu(menu);
		return nullptr;
	}
	return menu;
}

static HMENU BuildWinXMenu(WinXMenuContext& context)
{
	HMENU menu = CreatePopupMenu();
	if (!menu)
		return nullptr;

	HRESULT hr = context.links.AppendToMenu(menu, UseCommandPrompt());
	if (FAILED(hr))
	{
		dbgprintf(L"Win+X shortcut enumeration failed, hr %X", hr);
		DestroyMenu(menu);
		return nullptr;
	}

	HMENU shutdown = BuildShutdownMenu(context);
	if (shutdown)
	{
		PWSTR heading = nullptr;
		hr = context.links.LoadShutdownMenuText(&heading);
		if (FAILED(hr))
			dbgprintf(L"Win+X shutdown heading resource failed, hr %X", hr);
		// Native WinX inserts shutdown immediately before the bottom command.
		int count = GetMenuItemCount(menu);
		BOOL inserted = SUCCEEDED(hr) && InsertMenuW(menu, count ? count - 1 : 0,
			MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(shutdown), heading);
		CoTaskMemFree(heading);
		if (!inserted)
		{
			DestroyMenu(shutdown);
			DestroyMenu(menu);
			return nullptr;
		}
	}
	return menu;
}

//---Running a pick-----------------------------------------

static void RunWinXCommand(UINT cmd, HWND tray, const WinXMenuContext& context)
{
	if (const WinXLink* link = context.links.Get(cmd))
	{
		// These shell namespace commands belong to the Windows 7 tray.
		if (link->hash == 0x1DA07E32)
			PostMessageW(tray, WM_COMMAND, TRAYCMD_RUN, 0);
		else if (link->hash == 0x36B68C0A)
			PostMessageW(tray, WM_COMMAND, TRAYCMD_DESKTOP, 0);
		else
		{
			bool elevate = link->runAs || (!(link->flags & 4) &&
				GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_SHIFT) < 0);
			SHELLEXECUTEINFOW sei = { sizeof(sei) };
			sei.fMask = SEE_MASK_UNICODE | SEE_MASK_FLAG_LOG_USAGE;
			sei.hwnd = tray;
			sei.lpVerb = elevate ? L"runas" : L"open";
			sei.lpFile = link->path;
			sei.nShow = SW_SHOWNORMAL;
			BOOL ok = ShellExecuteExW(&sei);
			dbgprintf(L"Win+X launch %s, admin %d, ok %d error %u",
				link->path, elevate, ok, ok ? 0 : GetLastError());
		}
		return;
	}

	int index = static_cast<int>(cmd) - WINX_CHOICE_FIRST;
	if (index < 0 || index >= context.choiceCount)
		return;

	// Exactly what CLogoffPane::PostTrayCommand sends, so nothing is forced closed
	ULONG choice = context.choices[index];
	UINT trayCmd = choice == CHOICE_LOGOFF ? TRAYCMD_LOGOFF : TRAYCMD_EXIT;
	PostMessageW(tray, WM_COMMAND, trayCmd, (LPARAM)choice);
	dbgprintf(L"Win+X shutdown choice %X sent to the tray as %u", choice, trayCmd);
}

//---Start button menu hooks--------------------------------

typedef BOOL(WINAPI* TrackPopupMenu_t)(HMENU, UINT, int, int, int, HWND, const RECT*);
typedef BOOL(WINAPI* TrackPopupMenuEx_t)(HMENU, UINT, int, int, HWND, LPTPMPARAMS);
static TrackPopupMenu_t   g_realTrackPopupMenu;
static TrackPopupMenuEx_t g_realTrackPopupMenuEx;

// The owner test comes first, it is one property read for every other menu
static bool IsStartButtonMenu(HMENU menu, HWND owner)
{
	return owner && GetPropW(owner, L"StartButtonTag")
		&& GetMenuState(menu, STARTBUTTON_CMD_PROPERTIES, MF_BYCOMMAND) != (UINT)-1;
}

// Docks the menu on the taskbar edge beside the Start button, where Windows 10 puts it
// Explorer keeps it clear of the whole Start button, which left a gap above the taskbar
static bool PlaceOnTaskbar(HWND owner, HWND tray, int* x, int* y, TPMPARAMS* params, UINT* placement)
{
	RECT trayRect, buttonRect;
	if (!tray || !GetWindowRect(tray, &trayRect) || !GetWindowRect(owner, &buttonRect))
		return 0;
	MONITORINFO mi = { sizeof(mi) };
	if (!GetMonitorInfoW(MonitorFromWindow(tray, MONITOR_DEFAULTTONEAREST), &mi))
		return 0;

	params->cbSize = sizeof(*params);
	params->rcExclude = trayRect;

	const RECT& screen = mi.rcMonitor;
	if (trayRect.right - trayRect.left >= trayRect.bottom - trayRect.top)
	{
		bool atTop = trayRect.top + trayRect.bottom < screen.top + screen.bottom;
		*x = buttonRect.left > screen.left ? buttonRect.left : screen.left;
		*y = atTop ? trayRect.bottom : trayRect.top;
		*placement = TPM_VERTICAL | TPM_LEFTALIGN | (atTop ? TPM_TOPALIGN : TPM_BOTTOMALIGN);
		return true;
	}

	bool atLeft = trayRect.left + trayRect.right < screen.left + screen.right;
	*x = atLeft ? trayRect.right : trayRect.left;
	*y = buttonRect.top > screen.top ? buttonRect.top : screen.top;
	*placement = TPM_TOPALIGN | (atLeft ? TPM_LEFTALIGN : TPM_RIGHTALIGN);
	return true;
}

// Shown against the taskbar, then explorer is told nothing was picked
static void TrackWinXMenu(UINT flags, int x, int y, HWND owner, LPTPMPARAMS params)
{
	WinXMenuContext context;
	HMENU menu = BuildWinXMenu(context);
	if (!menu)
		return;

	HWND tray = GetWindow(owner, GW_OWNER);
	if (!tray)
		tray = FindWindowW(L"Shell_TrayWnd", NULL);

	// Explorer's own place is kept only if the taskbar cannot be measured
	TPMPARAMS docked;
	UINT place = 0;
	bool positioned = PlaceOnTaskbar(owner, tray, &x, &y, &docked, &place);
	if (positioned)
	{
		flags = (flags & (TPM_LAYOUTRTL | TPM_RIGHTBUTTON)) | place;
		params = &docked;
	}

	// A menu only closes on an outside click while its owner holds the foreground
	BOOL foreground = SetForegroundWindow(owner);
	UINT cmd = (UINT)g_realTrackPopupMenuEx(menu, flags | TPM_RETURNCMD | TPM_NONOTIFY, x, y, owner, params);
	PostMessageW(owner, WM_NULL, 0, 0);
	DestroyMenu(menu);

	dbgprintf(L"Win+X menu at %d,%d flags %X, docked %d, foreground %d, picked %u", x, y, flags, positioned, foreground, cmd);
	if (cmd && tray)
		RunWinXCommand(cmd, tray, context);
}

// Right click on the Start button, CStartButton::OnContextMenu
static BOOL WINAPI TrackPopupMenu_WinX(HMENU menu, UINT flags, int x, int y, int reserved, HWND owner, const RECT* rect)
{
	if (!IsStartButtonMenu(menu, owner))
		return g_realTrackPopupMenu(menu, flags, x, y, reserved, owner, rect);
	TrackWinXMenu(flags, x, y, owner, nullptr);
	return FALSE;
}

// Keyboard route, from CStartButton::TrackMenu
static BOOL WINAPI TrackPopupMenuEx_WinX(HMENU menu, UINT flags, int x, int y, HWND owner, LPTPMPARAMS params)
{
	if (!IsStartButtonMenu(menu, owner))
		return g_realTrackPopupMenuEx(menu, flags, x, y, owner, params);
	TrackWinXMenu(flags, x, y, owner, params);
	return FALSE;
}

void InstallWinXMenu()
{
	if (!s_EnableWinXMenu)
		return;

	HMODULE user32 = GetModuleHandleW(L"user32.dll");
	g_realTrackPopupMenu = (TrackPopupMenu_t)GetProcAddress(user32, "TrackPopupMenu");
	g_realTrackPopupMenuEx = (TrackPopupMenuEx_t)GetProcAddress(user32, "TrackPopupMenuEx");
	if (!g_realTrackPopupMenu || !g_realTrackPopupMenuEx)
		return;

	HMODULE explorer = GetModuleHandleW(NULL);
	BOOL mouse = ChangeImportedAddress(explorer, "user32.dll", g_realTrackPopupMenu, TrackPopupMenu_WinX);
	BOOL keyboard = ChangeImportedAddress(explorer, "user32.dll", g_realTrackPopupMenuEx, TrackPopupMenuEx_WinX);

	// Only set while the menu is on, so the tray subclass ignores it otherwise
	g_winXRegisterMsg = RegisterWindowMessageW(L"explorer7.WinXMenu.Register");
	dbgprintf(L"Win+X menu on, Start button hooks right click %d keyboard %d", mouse, keyboard);
}

//---Win+X hotkey-------------------------------------------

void RegisterWinXHotkey(HWND tray)
{
	BOOL ok = RegisterHotKeyUnhooked(tray, WINX_HOTKEY_ID, MOD_WIN | MOD_NOREPEAT, 'X');
	dbgprintf(L"Win+X hotkey on the tray %p, registered %d error %u", tray, ok, ok ? 0 : GetLastError());
}

// The Start button is a popup owned by the tray, tagged when CTray::_CreateWindows makes it
static HWND FindStartButton(HWND tray)
{
	HWND button = NULL;
	while ((button = FindWindowExW(NULL, button, L"Button", NULL)) != NULL)
	{
		if (GetWindow(button, GW_OWNER) == tray && GetPropW(button, L"StartButtonTag"))
			return button;
	}
	return NULL;
}

// A keyboard context menu on the Start button, the hook then swaps and docks it
void OpenWinXMenuFromKeyboard(HWND tray)
{
	HWND start = FindStartButton(tray);
	dbgprintf(L"Win+X pressed, Start button %p", start);
	if (start)
		PostMessageW(start, WM_CONTEXTMENU, (WPARAM)start, (LPARAM)-1);
}