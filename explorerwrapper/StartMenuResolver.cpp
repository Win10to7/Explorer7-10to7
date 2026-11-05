#include "StartMenuResolver.h"
#include "StartMenuPin.h"
#include "dbgprint.h"
#include "PinnedList.h"
#include <wrl/client.h>
#pragma function(memset)

extern "C" HRESULT WINAPI Explorer_CoCreateInstance(
	__in   REFCLSID rclsid,
	__in   LPUNKNOWN pUnkOuter,
	__in   DWORD dwClsContext,
	__in   REFIID riid,
	__out  LPVOID* ppv
);

//constructor
CStartMenuResolver::CStartMenuResolver(IAppResolver8* newresolver)
{
	m_cRef = 0; //?
	m_resolver8 = newresolver;
	m_startmenuitemscache8 = nullptr;
	m_startmenuitemscache10 = nullptr;
}

CStartMenuResolver::CStartMenuResolver(IStartMenuItemsCache8 *newcache)
{
	m_cRef = 0;
	m_startmenuitemscache8 = newcache;
	m_startmenuitemscache10 = nullptr;
	CoCreateInstance(
		CLSID_StartMenuCacheAndAppResolver,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_IAppResolver8,
		(LPVOID *)&m_resolver8
	);
}

CStartMenuResolver::CStartMenuResolver(IStartMenuItemsCache10 *newcache)
{
	m_cRef = 0;
	m_startmenuitemscache10 = newcache;
	m_startmenuitemscache8 = nullptr;
	CoCreateInstance(
		CLSID_StartMenuCacheAndAppResolver,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_IAppResolver8,
		(LPVOID*)&m_resolver8
	);
}

CStartMenuResolver::~CStartMenuResolver()
{
	HRESULT hr = RegisterSMNotify(nullptr);
	if (FAILED(hr))
		dbgprintf(L"Start menu notification removal failed: %08X", hr);
	if (m_resolver8)
		m_resolver8->Release();

	if (m_startmenuitemscache8)
		m_startmenuitemscache8->Release();

	if (m_startmenuitemscache10)
		m_startmenuitemscache10->Release();
}

HRESULT STDMETHODCALLTYPE CStartMenuResolver::QueryInterface(REFIID riid, void** ppvObject)
{
	if (riid == IID_IAppResolver7)
	{
		//dbgprintf(L"IID_IAppResolver7\n");
		*ppvObject = static_cast<IAppResolver7*>(this);
		AddRef();
		return S_OK;
	}
	if (riid == IID_IStartMenuItemsCache7)
	{
		dbgprintf(L"IID_IStartMenuItemsCache7\n");
		HRESULT ret = E_NOINTERFACE;
		if (m_startmenuitemscache8)
		{
			ret = m_startmenuitemscache8->QueryInterface(IID_IStartMenuItemsCache8, (PVOID *)&m_startmenuitemscache8);
			if (ret == S_OK)
			{
				dbgprintf(L"S_OK\n");
				*ppvObject = static_cast<IStartMenuItemsCache7 *>(this);
				AddRef();
			}
		}
		else if (m_startmenuitemscache10)
		{
			ret = m_startmenuitemscache10->QueryInterface(IID_IStartMenuItemsCache10, (PVOID*)&m_startmenuitemscache10);
			if (ret == S_OK)
			{
				dbgprintf(L"S_OK 2\n");
				*ppvObject = static_cast<IStartMenuItemsCache7 *>(this);
				AddRef();
			}
		}
		return ret;
	}
	return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CStartMenuResolver::AddRef(void)
{
	return InterlockedIncrement(&m_cRef);
}

ULONG STDMETHODCALLTYPE CStartMenuResolver::Release(void)
{
	if (InterlockedDecrement(&m_cRef) == 0)
	{
		delete this;
		return 0;
	}
	return m_cRef;
}

//IAppResolver7
HRESULT STDMETHODCALLTYPE CStartMenuResolver::GetAppIDForShortcut(IShellItem* p1, LPWSTR* p2)
{
	dbgprintf(L"GetAppIDForShortcut");
	return m_resolver8->GetAppIDForShortcut(p1, p2);
}

HRESULT STDMETHODCALLTYPE CStartMenuResolver::GetAppIDForWindow(HWND* p1, DWORD* p2, DWORD* p3, DWORD* p4, DWORD* p5)
{
	dbgprintf(L"GetAppIDForWindow");
	return m_resolver8->GetAppIDForWindow(p1, p2, p3, p4, p5);
}

HRESULT STDMETHODCALLTYPE CStartMenuResolver::GetAppIDForProcess(ULONG_PTR p1, DWORD* p2, DWORD* p3, DWORD* p4, DWORD* p5)
{
	dbgprintf(L"GetAppIDForProcess");
	return m_resolver8->GetAppIDForProcess(p1, p2, p3, p4, p5);
}

HRESULT STDMETHODCALLTYPE CStartMenuResolver::GetShortcutForProcess(ULONG_PTR p1, IUnknown* p2)
{
	dbgprintf(L"GetShortcutForProcess");
	return m_resolver8->GetShortcutForProcess(p1, p2);
}

HRESULT STDMETHODCALLTYPE CStartMenuResolver::GetBestShortcutForAppID(DWORD* p1, IUnknown* p2)
{
	dbgprintf(L"GetBestShortcutForAppID");

	// Ittr: Basically, immersive applications normally fail when calling this function
	// Settings will only succeed, because of the existence of the extra shortcut in the start menu folders
	// This ensures that it fails and is treated like all other immersive applications
	// A more comprehensive solution will be shipped in Milestone 3
	if (lstrcmp((LPWSTR)p1, L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel") == 0)
	{
		return E_OUTOFMEMORY;
	}

	return m_resolver8->GetBestShortcutForAppID(p1, p2);
}

HRESULT STDMETHODCALLTYPE CStartMenuResolver::GetBestShortcutAndAppIDForAppPath(DWORD* p1, IUnknown* p2, DWORD* p3)
{
	dbgprintf(L"GetBestShortcutAndAppIDForAppPath");
	return m_resolver8->GetBestShortcutAndAppIDForAppPath(p1, p2, p3);
}

HRESULT STDMETHODCALLTYPE CStartMenuResolver::CanPinApp(IUnknown* p1)
{
	dbgprintf(L"CanPinApp");
	return m_resolver8->CanPinApp(p1);
}

HRESULT STDMETHODCALLTYPE CStartMenuResolver::GetRelaunchProperties(HWND* p1, DWORD* p2, DWORD* p3, DWORD* p4, DWORD* p5, DWORD* p6)
{
	//dbgprintf(L"GetRelaunchProperties");
	return m_resolver8->GetRelaunchProperties(p1, p2, p3, p4, p5, p6, nullptr);
}

HRESULT STDMETHODCALLTYPE CStartMenuResolver::GenerateShortcutFromWindowProperties(HWND* p1, IUnknown* p2)
{
	dbgprintf(L"GenerateShortcutFromWindowProperties");
	return m_resolver8->GenerateShortcutFromWindowProperties(p1, p2);
}

HRESULT STDMETHODCALLTYPE CStartMenuResolver::GenerateShortcutFromItemProperties(IUnknown* p1, IUnknown* p2)
{
	dbgprintf(L"GenerateShortcutFromItemProperties");
	return m_resolver8->GenerateShortcutFromItemProperties(p1, p2);
}

//IStartMenuItemsCache7
HRESULT STDMETHODCALLTYPE CStartMenuResolver::OnChangeNotify(unsigned int p1, long p2, PVOID* p3, PVOID* p4)
{
	HRESULT rslt = E_NOINTERFACE;
	if (m_startmenuitemscache8)
		rslt = m_startmenuitemscache8->OnChangeNotify(p1, p2, p3, p4);
	else if (m_startmenuitemscache10)
		rslt = m_startmenuitemscache10->OnChangeNotify(p1, p2, p3, p4);
	if (SUCCEEDED(rslt) && p1 < 8)
		NotifyStartMenu(FALSE);
	dbgprintf(L"CStartMenuResolver::OnChangeNotify %p %p %p %p = %p", p1, p2, p3, p4, rslt);
	return rslt;
}

HRESULT STDMETHODCALLTYPE CStartMenuResolver::PinListChanged(void)
{
	dbgprintf(L"CStartMenuResolver::PinListChanged");
	NotifyStartMenu(FALSE);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CStartMenuResolver::GetPinnedItemsCount(int* pCount)
{
	dbgprintf(L"GetPinnedItemsCount");
	*pCount = 0;
	IPinnedList2* pinList2 = 0;
	HRESULT rslt = Explorer_CoCreateInstance(CLSID_StartMenuPin, NULL, CLSCTX_INPROC_SERVER, IID_IPinnedList2, (PVOID*)&pinList2);
	if (SUCCEEDED(rslt))
	{
		IEnumFullIDList* enumidlist;
		rslt = pinList2->EnumObjects(&enumidlist);
		if (SUCCEEDED(rslt))
		{
			LPITEMIDLIST pidl;
			ULONG wat;
			while (enumidlist->Next(1, &pidl, &wat) == S_OK)
			{
				(*pCount)++;
				CoTaskMemFree(pidl);
			}
			dbgprintf(L"CStartMenuResolver::GetPinnedItemsCount = %d", *pCount);
			enumidlist->Release();
		}
		pinList2->Release();
	}
	return rslt;
}

static bool NewProgramHighlightingEnabled()
{
	const WCHAR advanced[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
	DWORD notify = 1;
	DWORD track = 1;
	DWORD size = sizeof(DWORD);
	RegGetValueW(HKEY_CURRENT_USER, advanced, L"Start_NotifyNewApps", RRF_RT_REG_DWORD, nullptr, &notify, &size);
	size = sizeof(DWORD);
	RegGetValueW(HKEY_CURRENT_USER, advanced, L"Start_TrackProgs", RRF_RT_REG_DWORD, nullptr, &track, &size);
	return notify && track;
}

static HRESULT GetNewProgramHighlightCutoff(FILETIME& cutoff)
{
	const WCHAR startPage[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartPage";
	DWORD size = sizeof(cutoff);
	LSTATUS error = RegGetValueW(HKEY_CURRENT_USER, startPage, L"StartMenu_Start_Time",
		RRF_RT_REG_BINARY, nullptr, &cutoff, &size);
	if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
	{
		cutoff = FILETIME();
		return S_OK;
	}
	if (error != ERROR_SUCCESS)
		return HRESULT_FROM_WIN32(error);
	return size == sizeof(cutoff) ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}


HRESULT STDMETHODCALLTYPE CStartMenuResolver::GetStartMenuMFUList(unsigned int limit, IEnumStartMenuItem** penumStart, IEnumString** penumStrings, FILETIME* pNewFileTime)
{
	using Microsoft::WRL::ComPtr;
	if (!penumStart || !penumStrings || !pNewFileTime)
		return E_POINTER;
	*penumStart = nullptr;
	*penumStrings = nullptr;
	*pNewFileTime = FILETIME();
	if (!m_resolver8)
		return E_NOINTERFACE;

	FILETIME newestInstalled = {};
	bool highlightNew = NewProgramHighlightingEnabled();
	FILETIME cutoff = {};
	if (highlightNew)
	{
		HRESULT cutoffResult = GetNewProgramHighlightCutoff(cutoff);
		if (FAILED(cutoffResult))
			return cutoffResult;
	}

	ComPtr<CEnumStartMenu> items;
	ComPtr<CEnumNewPrograms> paths;
	items.Attach(new CEnumStartMenu);
	paths.Attach(new CEnumNewPrograms);
	if (!items || !paths)
		return E_OUTOFMEMORY;

	ComPtr<IPinnedList2> startPins;
	ComPtr<IPinnedList2> taskbarPins;
	HRESULT hr = Explorer_CoCreateInstance(CLSID_StartMenuPin, nullptr, CLSCTX_INPROC_SERVER,
		IID_IPinnedList2, reinterpret_cast<void**>(startPins.GetAddressOf()));
	if (FAILED(hr))
		return hr;
	hr = Explorer_CoCreateInstance(CLSID_TaskbarPin, nullptr, CLSCTX_INPROC_SERVER,
		IID_IPinnedList2, reinterpret_cast<void**>(taskbarPins.GetAddressOf()));
	if (FAILED(hr))
		return hr;

	unsigned int pinnedCount = 0;
	ComPtr<IEnumFullIDList> pinned;
	hr = startPins->EnumObjects(&pinned);
	if (FAILED(hr))
		return hr;
	STARTMENUITEM item = {};
	while ((hr = pinned->Next(1, &item.pidlRelative, nullptr)) == S_OK)
	{
		ComPtr<IShellItem> shellItem;
		hr = SHCreateItemFromIDList(item.pidlRelative, IID_PPV_ARGS(&shellItem));
		if (SUCCEEDED(hr))
		{
			hr = m_resolver8->GetAppIDForShortcut(shellItem.Get(), &item.pszAppID);
			if (FAILED(hr))
				hr = shellItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &item.pszAppID);
		}
		if (SUCCEEDED(hr))
		{
			item.iPinPos = pinnedCount;
			hr = items->AddItem(&item);
			if (SUCCEEDED(hr))
			{
				++pinnedCount;
				item = STARTMENUITEM();
				continue;
			}
		}
		ILFree(item.pidlRelative);
		CoTaskMemFree(item.pszAppID);
		return hr;
	}
	if (FAILED(hr))
		return hr;
	if (pinnedCount)
	{
		item = STARTMENUITEM();
		item.iPinPos = -2;
		hr = items->AddItem(&item);
		if (FAILED(hr))
			return hr;
	}

	auto addRecentProgram = [&](PCIDLIST_ABSOLUTE pidl, IShellItem2* shellItem, PWSTR appId) -> HRESULT
	{
		PWSTR path = nullptr;
		HRESULT result = shellItem->GetDisplayName(SIGDN_FILESYSPATH, &path);
		if (FAILED(result))
			return S_FALSE;

		UEMINFO shortcutUsage = {};
		UEMINFO appUsage = {};
		HRESULT shortcutResult = UAQueryEntry(UAIID_SHORTCUTS, path, &shortcutUsage);
		HRESULT appResult = UAQueryApplication(appId, &appUsage);
		STARTMENUITEM recent = {};
		recent.ueminfo = shortcutUsage.R > appUsage.R || appUsage.fExcludeFromMFU ? shortcutUsage : appUsage;
		bool excluded = shortcutUsage.fExcludeFromMFU || appUsage.fExcludeFromMFU;
		if (FAILED(shortcutResult) || FAILED(appResult))
			dbgprintf(L"UserAssist query failed for %s: shortcut=%08X application=%08X", path, shortcutResult, appResult);

		FILETIME installed = {};
		if (highlightNew && shortcutResult == S_OK && appResult == S_OK &&
			GetNewShortcutInstallTime(path, shortcutUsage, appUsage, cutoff, nullptr, &installed) &&
			m_resolver8->CanPinApp(shellItem) == S_OK)
		{
			recent.fNewApp = TRUE;
			recent.ueminfo.ftExecute = installed;
		}
		if (excluded || (!recent.ueminfo.R && !recent.fNewApp) ||
			startPins->IsPinned(pidl) != S_FALSE || taskbarPins->IsPinned(pidl) != S_FALSE)
		{
			CoTaskMemFree(path);
			return S_FALSE;
		}
		if (recent.fNewApp && CompareFileTime(&installed, &newestInstalled) > 0)
			newestInstalled = installed;

		recent.pidlRelative = ILClone(pidl);
		result = recent.pidlRelative ? SHStrDupW(appId, &recent.pszAppID) : E_OUTOFMEMORY;
		if (SUCCEEDED(result) && recent.fNewApp)
			result = paths->AddPath(path);
		CoTaskMemFree(path);
		if (SUCCEEDED(result))
		{
			recent.iPinPos = -1;
			result = items->AddItem(&recent);
		}
		if (FAILED(result))
		{
			ILFree(recent.pidlRelative);
			CoTaskMemFree(recent.pszAppID);
		}
		return result;
	};

	auto visitShortcut = [&](PCIDLIST_ABSOLUTE pidl, IShellItem2* shellItem) -> HRESULT
	{
		DWORD hostEnvironment = 0;
		BOOL dualMode = FALSE;
		if (SUCCEEDED(shellItem->GetUInt32(PKEY_AppUserModel_HostEnvironment, &hostEnvironment)) && hostEnvironment)
		{
			shellItem->GetBool(PKEY_AppUserModel_IsDualMode, &dualMode);
			if (!dualMode)
				return S_FALSE;
		}
		PWSTR appId = nullptr;
		HRESULT result = m_resolver8->GetAppIDForShortcut(shellItem, &appId);
		if (FAILED(result))
			result = shellItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &appId);
		if (SUCCEEDED(result))
			result = addRecentProgram(pidl, shellItem, appId);
		CoTaskMemFree(appId);
		return result;
	};

	// Enumerate every new path, including programs below the visible recent-list limit.
	if (limit > pinnedCount || highlightNew)
	{
		hr = EnsureRoots();
		if (FAILED(hr))
			return hr;
		hr = VisitStartMenuShortcuts(m_roots,
			[](PCIDLIST_ABSOLUTE pidl, IShellItem2* item, void* context) -> HRESULT
			{
				return (*static_cast<decltype(visitShortcut)*>(context))(pidl, item);
			}, &visitShortcut);
		if (FAILED(hr))
			return hr;

		ComPtr<IShellFolder> desktop;
		hr = SHGetDesktopFolder(&desktop);
		if (FAILED(hr))
			return hr;
		ComPtr<IEnumIDList> desktopItems;
		hr = desktop->EnumObjects(nullptr, SHCONTF_NONFOLDERS | SHCONTF_FASTITEMS, &desktopItems);
		if (FAILED(hr))
			return hr;
		PITEMID_CHILD child = nullptr;
		while (desktopItems && (hr = desktopItems->Next(1, &child, nullptr)) == S_OK)
		{
			SFGAOF attributes = SFGAO_LINK;
			PCUITEMID_CHILD children[] = { child };
			if (SUCCEEDED(desktop->GetAttributesOf(1, children, &attributes)) &&
				(attributes & SFGAO_LINK))
			{
				PIDLIST_ABSOLUTE absolute = nullptr;
				ComPtr<IShellItem2> shellItem;
				if (SUCCEEDED(SHGetRealIDL(desktop.Get(), child, &absolute)) &&
					SUCCEEDED(SHCreateItemFromIDList(absolute, IID_PPV_ARGS(&shellItem))))
				{
					hr = visitShortcut(absolute, shellItem.Get());
				}
				ILFree(absolute);
			}
			ILFree(child);
			if (FAILED(hr))
				return hr;
		}
		if (FAILED(hr))
			return hr;
	}
	items->Sort();
	items->SetLimit(limit);
	items->RemoveDuplicates();
	*penumStart = items.Detach();
	*penumStrings = paths.Detach();
	*pNewFileTime = newestInstalled;
	return S_OK;
}

BOOL CALLBACK CStartMenuResolver::InitializeRoots(PINIT_ONCE, void* context, void**)
{
	CStartMenuResolver* self = static_cast<CStartMenuResolver*>(context);
	self->m_rootsResult = self->m_roots.Initialize();
	return TRUE;
}

HRESULT CStartMenuResolver::EnsureRoots()
{
	if (!InitOnceExecuteOnce(&m_rootsOnce, InitializeRoots, this, nullptr))
		return HRESULT_FROM_WIN32(GetLastError());
	return m_rootsResult;
}

void CStartMenuResolver::FolderChanged(void* context, LONG, PCIDLIST_ABSOLUTE, PCIDLIST_ABSOLUTE)
{
	// An installer changes directories, binaries, and shortcuts in one batch.
	// Let the native MFU refresh timer coalesce it rather than caching an intermediate scan.
	static_cast<CStartMenuResolver*>(context)->NotifyStartMenu(TRUE);
}

void CStartMenuResolver::NotifyStartMenu(BOOL deferred)
{
	AcquireSRWLockShared(&m_notifyLock);
	if (m_startMenuNotify)
		m_startMenuNotify->ChangeNotify(deferred);
	ReleaseSRWLockShared(&m_notifyLock);
}

int CALLBACK CStartMenuResolver::UserAssistChanged(void* context, const GUID* category, PCWSTR, int event)
{
	if (category && (event == 0 || event == 3) &&
		(*category == UAIID_APPLICATIONS || *category == UAIID_SHORTCUTS || *category == UAIID_AUTOMATIC))
	{
		static_cast<CStartMenuResolver*>(context)->NotifyStartMenu(TRUE);
	}
	return 0;
}

HRESULT STDMETHODCALLTYPE CStartMenuResolver::RegisterSMNotify(IUnknown* notify)
{
	if (!notify)
	{
		AcquireSRWLockExclusive(&m_notifyLock);
		m_startMenuNotify = nullptr;
		ReleaseSRWLockExclusive(&m_notifyLock);
		m_folderMonitor.Stop();
		if (m_usageNotifyRegistered)
		{
			HRESULT hr = UARegisterNotify(UserAssistChanged, this, FALSE);
			if (FAILED(hr))
				return hr;
			m_usageNotifyRegistered = false;
		}
		return S_OK;
	}
	HRESULT hr = EnsureRoots();
	if (FAILED(hr))
		return hr;
	if (!m_folderMonitor.Active())
	{
		hr = m_folderMonitor.Start(m_roots.Items(), m_roots.Count(), FolderChanged, this);
		if (FAILED(hr))
			return hr;
	}
	if (!m_usageNotifyRegistered)
	{
		hr = UARegisterNotify(UserAssistChanged, this, TRUE);
		if (FAILED(hr))
		{
			m_folderMonitor.Stop();
			return hr;
		}
		m_usageNotifyRegistered = true;
	}
	AcquireSRWLockExclusive(&m_notifyLock);
	m_startMenuNotify = static_cast<IStartMenuItemsNotify*>(notify);
	ReleaseSRWLockExclusive(&m_notifyLock);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CStartMenuResolver::RegisterARNotify(IUnknown* p1)
{
	dbgprintf(L"RegisterARNotify");
	if (m_startmenuitemscache8)
		return m_startmenuitemscache8->RegisterARNotify(new CAppResolverNotify8((IAppResolverNotify7*)p1));
	else if (m_startmenuitemscache10)
		return m_startmenuitemscache10->RegisterARNotify(new CAppResolverNotify8((IAppResolverNotify7*)p1));

	return E_ABORT;
}

HRESULT STDMETHODCALLTYPE CStartMenuResolver::SetAltName(PVOID* p1, DWORD* p2, PVOID* p3)
{
	dbgprintf(L"CStartMenuResolver::SetAltName %p %p %p", p1, p2, p3);
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CStartMenuResolver::GetAltName(PVOID* p1, DWORD* p2)
{
	dbgprintf(L"CStartMenuResolver::GetAltName %p %p", p1, p2);
	return E_NOTIMPL;
}
