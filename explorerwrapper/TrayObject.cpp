#include "TrayObject.h"
#include "dbgprint.h"
#include <commctrl.h>

static HMODULE s_volumeTrayIconModule;

static INIT_ONCE s_taskbarDpiInit = INIT_ONCE_STATIC_INIT;
static UINT(WINAPI* s_getDpiForWindow)(HWND);

static BOOL CALLBACK ResolveTaskbarDpiApi(PINIT_ONCE, PVOID, PVOID*)
{
	HMODULE user32 = GetModuleHandleW(L"user32.dll");
	s_getDpiForWindow = reinterpret_cast<decltype(s_getDpiForWindow)>(
		GetProcAddress(user32, "GetDpiForWindow"));
	return TRUE;
}

void PublishNotificationAreaDpi(HWND hwnd)
{
	if (!hwnd)
		return;

	InitOnceExecuteOnce(&s_taskbarDpiInit, ResolveTaskbarDpiApi, NULL, NULL);
	UINT dpi;
	if (s_getDpiForWindow)
	{
		dpi = s_getDpiForWindow(hwnd);
		if (!dpi)
		{
			dbgprintf(L"Cannot read taskbar window DPI: %u\n", GetLastError());
			return;
		}
	}
	else
	{
		HDC screen = GetDC(NULL);
		if (!screen)
		{
			dbgprintf(L"Cannot read system DPI for taskbar: %u\n", GetLastError());
			return;
		}
		dpi = GetDeviceCaps(screen, LOGPIXELSX);
		ReleaseDC(NULL, screen);
		if (!dpi)
		{
			dbgprintf(L"Cannot read taskbar system DPI\n");
			return;
		}
	}

	if (reinterpret_cast<ULONG_PTR>(GetPropW(hwnd, L"TaskbarDPI_NotificationArea")) != dpi &&
		!SetPropW(hwnd, L"TaskbarDPI_NotificationArea", reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(dpi))))
		dbgprintf(L"Cannot publish taskbar notification area DPI %u: %u\n", dpi, GetLastError());
}

static HANDLE WINAPI LoadVolumeTrayImageW(HINSTANCE instance, LPCWSTR name, UINT type,
	int width, int height, UINT flags)
{
	constexpr UINT exactSizeOnly = 0x10000; // Private LR_EXACTSIZEONLY flag used by SndVolSSO.
	if (type != IMAGE_ICON || !IS_INTRESOURCE(name) ||
		reinterpret_cast<ULONG_PTR>(name) < 120 || reinterpret_cast<ULONG_PTR>(name) > 125 ||
		width <= 0 || height <= 0 || (flags & ~exactSizeOnly) ||
		!instance || instance != s_volumeTrayIconModule)
	{
		return LoadImageW(instance, name, type, width, height, flags);
	}

	HICON icon = NULL;
	HRESULT result = LoadIconWithScaleDown(instance, name, width, height, &icon);
	if (FAILED(result))
	{
		dbgprintf(L"Volume tray icon %u at %dx%d failed: 0x%08X\n",
			static_cast<UINT>(reinterpret_cast<ULONG_PTR>(name)), width, height, result);
		SetLastError(HRESULT_FACILITY(result) == FACILITY_WIN32 ?
			HRESULT_CODE(result) : ERROR_GEN_FAILURE);
		return NULL;
	}
	return icon;
}

static void PatchVolumeTrayIcons()
{
	if (s_volumeTrayIconModule)
		return;

	HMODULE sndVolSSO = LoadLibraryW(L"SndVolSSO.dll");
	if (!sndVolSSO)
	{
		dbgprintf(L"Cannot load SndVolSSO for volume tray icon scaling: %u\n", GetLastError());
		return;
	}
	s_volumeTrayIconModule = sndVolSSO;
	if (!ChangeImportedAddress(sndVolSSO, "user32.dll", LoadImageW, LoadVolumeTrayImageW))
	{
		dbgprintf(L"Cannot redirect SndVolSSO volume tray icon loads\n");
		FreeLibrary(sndVolSSO);
		s_volumeTrayIconModule = NULL;
	}
}

CSysTrayWrapper::CSysTrayWrapper(IOleCommandTarget *stobject8)
{
	m_cRef = 1;
	m_stobject8 = stobject8;
	initialized = 0;
	PublishNotificationAreaDpi(FindWindowW(L"Shell_TrayWnd", NULL));
	PatchVolumeTrayIcons();
}

CSysTrayWrapper::~CSysTrayWrapper()
{
	m_stobject8->Release();
}

HRESULT STDMETHODCALLTYPE CSysTrayWrapper::QueryInterface(REFIID riid,void **ppvObject)
{
	dbgprintf(L"CSysTrayWrapper::QueryInterface NOT IMPLEMENTED");
	return E_NOTIMPL;
}

ULONG STDMETHODCALLTYPE CSysTrayWrapper::AddRef(void)
{
	return InterlockedIncrement(&m_cRef);
}

ULONG STDMETHODCALLTYPE CSysTrayWrapper::Release(void)
{
	if (InterlockedDecrement(&m_cRef) == 0)
	{
		delete this;
		return 0;
	}
	return m_cRef;
}

HRESULT STDMETHODCALLTYPE CSysTrayWrapper::QueryStatus( const GUID *pguidCmdGroup, ULONG cCmds, OLECMD prgCmds[  ], OLECMDTEXT *pCmdText)
{	
	return m_stobject8->QueryStatus(pguidCmdGroup,cCmds,prgCmds,pCmdText);
}

HRESULT STDMETHODCALLTYPE CSysTrayWrapper::Exec( const GUID *pguidCmdGroup, DWORD nCmdID, DWORD nCmdexecopt, VARIANT *pvaIn, VARIANT *pvaOut)
{	
	PublishNotificationAreaDpi(FindWindowW(L"Shell_TrayWnd", NULL));
	//command #2 differs 7->8; also command #4 must be issued later
	if (pguidCmdGroup && *pguidCmdGroup == CGID_ShellServiceObject && nCmdID == 2)
	{
		VARIANT v;
		InitVariantFromUInt32(0,&v);
		HRESULT ret = m_stobject8->Exec(pguidCmdGroup,2,0,&v,NULL);
		return ret;
	}
	else
		DoInitSysTray();

	HRESULT ret = m_stobject8->Exec(pguidCmdGroup,nCmdID,nCmdexecopt,pvaIn,pvaOut);
	return ret;
}

void CSysTrayWrapper::DoInitSysTray( void)
{	
	if (initialized) return;
	VARIANT v;
	InitVariantFromUInt32(3,&v);
	HRESULT ret = m_stobject8->Exec(&CGID_ShellServiceObject,4,0,&v,NULL); //start
	initialized = (ret == S_OK);
}