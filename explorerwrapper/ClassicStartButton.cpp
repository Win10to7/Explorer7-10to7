#include "ClassicStartButton.h"
#include "ThemeManager.h"
#include "dbgprint.h"
#include <commctrl.h>

bool IsNativeStartButtonWindow(HWND hwnd)
{
	if (reinterpret_cast<UINT_PTR>(GetPropW(hwnd, L"StartButtonTag")) != 0x130)
		return false;

	HWND owner = GetWindow(hwnd, GW_OWNER);
	WCHAR className[32];
	return owner && GetClassNameW(owner, className, ARRAYSIZE(className)) &&
		!lstrcmpW(className, L"Shell_TrayWnd");
}

namespace
{
LRESULT CALLBACK ClassicStartButtonProc(HWND hwnd, UINT message, WPARAM wParam,
	LPARAM lParam, UINT_PTR subclassId, DWORD_PTR)
{
	if (message == WM_PRINTCLIENT && wParam && (lParam & PRF_CLIENT) && IsClassicTheme())
	{
		// Explorer exposes its measured button size through WM_APP+1. Its
		// native painter resizes before printing only when the height changes,
		// leaving a width-only change painted against the previous client rect.
		LRESULT measured = SendMessageW(hwnd, WM_APP + 1, 0, 0);
		int width = LOWORD(measured);
		int height = HIWORD(measured);
		RECT rect;
		if (width && height && GetWindowRect(hwnd, &rect) &&
			(rect.right - rect.left != width || rect.bottom - rect.top != height))
		{
			if (!SetWindowPos(hwnd, NULL, 0, 0, width, height,
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW))
				dbgprintf(L"Cannot size classic Start button before painting: %u\n", GetLastError());
		}
	}
	if (message == WM_NCDESTROY)
		RemoveWindowSubclass(hwnd, ClassicStartButtonProc, subclassId);
	return DefSubclassProc(hwnd, message, wParam, lParam);
}
}

void EnsureClassicStartButtonSubclass(HWND hwnd)
{
	if (!IsNativeStartButtonWindow(hwnd))
		return;

	DWORD_PTR reference;
	if (!GetWindowSubclass(hwnd, ClassicStartButtonProc, 0, &reference) &&
		!SetWindowSubclass(hwnd, ClassicStartButtonProc, 0, 0))
		dbgprintf(L"Cannot subclass classic Start button: %u\n", GetLastError());
}
