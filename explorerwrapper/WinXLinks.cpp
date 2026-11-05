#include "WinXLinks.h"
#include "dbgprint.h"
#include <shellapi.h>
#include <powrprof.h>

#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "powrprof.lib")

// CLauncherTipContextMenu in Windows 10 19044 identifies built-in commands by
// their validated hash, not their filename or translated display name.
static const PROPERTYKEY kWinXHash = {
	{ 0xFB8D2D7B, 0x90D1, 0x4E34, { 0xBF, 0x60, 0x6E, 0xAC, 0x09, 0x92, 0x2B, 0xBF } }, 2
};
static const KNOWNFOLDERID* const kHashFolders[] = {
	&FOLDERID_ProgramFiles, &FOLDERID_System, &FOLDERID_Windows
};
static const DWORD kSkippedHashes[] = { 0x9A606DF5, 0x5E250185, 0x2E711FBD, 0x4D4EBE49 };
static const DWORD kDuplicateHashes[] = { 0x631C4996, 0xD4F6A551, 0x90A64483, 0xA2EDB372 };
static const struct { DWORD hash; DWORD flags; UINT text; } kBuiltInLinks[] = {
	{ 0x495A90B2, 0x283, 0x2AA8 },
	{ 0x495A90B2, 0x282, 0x2AA7 },
	{ 0x6861712C, 0x002, 0x2AA6 },
	{ 0xEEF7BF44, 0x002, 0x2AA5 },
	{ 0x631C4996, 0x002, 0x2AA0 },
	{ 0xBDD12552, 0x002, 0x2AA4 },
	{ 0xD4F6A551, 0x002, 0x2AA3 },
	{ 0xD0AD8FC0, 0x002, 0x2AA2 },
	{ 0x90A64483, 0x002, 0x2AA1 },
	{ 0xA2F62CDE, 0x012, 0x2AAF },
	{ 0xA2EDB372, 0x002, 0x2A9F },
	{ 0x1DA07E32, 0x006, 0x2AAD },
	{ 0x8E5B9DE2, 0x020, 0x2AAC },
	{ 0xF24FFAFC, 0x002, 0x2AAB },
	{ 0x3C9C1396, 0x002, 0x2AAA },
	{ 0x9632551C, 0x002, 0x2AA9 },
	{ 0x36B68C0A, 0x00C, 0x2AAE },
	{ 0x1A60D163, 0x343, 0x2AB1 },
	{ 0x1A60D163, 0x342, 0x2AB0 }
};

static HRESULT ReadString(IPropertyStore* store, REFPROPERTYKEY key, PWSTR* text)
{
	*text = nullptr;
	PROPVARIANT value = {};
	HRESULT hr = store->GetValue(key, &value);
	if (SUCCEEDED(hr))
		hr = value.vt == VT_EMPTY ? HRESULT_FROM_WIN32(ERROR_NOT_FOUND) : PropVariantToStringAlloc(value, text);
	PropVariantClear(&value);
	return hr;
}

// Use the shell's filename comparison, then reverse both groups and links as
// _EnumerateAndBuildMenu does. Display names must not determine menu order.
class ShellChildren
{
public:
	ShellChildren() : items(nullptr) {}
	~ShellChildren()
	{
		if (items)
			DPA_DestroyCallback(items, FreeItem, nullptr);
	}
	HRESULT Load(IShellFolder* folder, SHCONTF flags)
	{
		items = DPA_Create(8);
		if (!items)
			return E_OUTOFMEMORY;
		IEnumIDList* list = nullptr;
		HRESULT hr = folder->EnumObjects(nullptr, flags, &list);
		if (hr == S_FALSE)
			return S_OK;
		if (FAILED(hr))
			return hr;
		PITEMID_CHILD child = nullptr;
		while ((hr = list->Next(1, &child, nullptr)) == S_OK)
		{
			if (DPA_InsertPtr(items, DPA_APPEND, child) == -1)
			{
				CoTaskMemFree(child);
				hr = E_OUTOFMEMORY;
				break;
			}
		}
		list->Release();
		if (SUCCEEDED(hr) && !DPA_Sort(items, CompareItems, reinterpret_cast<LPARAM>(folder)))
			hr = E_FAIL;
		return hr;
	}
	HDPA items;

private:
	static int CALLBACK FreeItem(void* item, void*)
	{
		CoTaskMemFree(item);
		return 1;
	}
	static int CALLBACK CompareItems(void* left, void* right, LPARAM context)
	{
		IShellFolder* folder = reinterpret_cast<IShellFolder*>(context);
		return static_cast<short>(HRESULT_CODE(folder->CompareIDs(SHCIDS_CANONICALONLY,
			static_cast<PCUIDLIST_RELATIVE>(left), static_cast<PCUIDLIST_RELATIVE>(right))));
	}
};

static HRESULT LoadMenuPolicy(bool* useCmd, bool* mobilityEnabled)
{
	// SHCORE!SHWindowsPolicy, the same policy reader used by twinui.
	HMODULE shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!shcore)
		return HRESULT_FROM_WIN32(GetLastError());
	typedef DWORD(WINAPI* SHWindowsPolicy_t)(REFGUID);
	auto policy = reinterpret_cast<SHWindowsPolicy_t>(GetProcAddress(shcore, MAKEINTRESOURCEA(190)));
	if (!policy)
	{
		HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
		FreeLibrary(shcore);
		return hr;
	}
	static const GUID showCommandPrompt = {
		0x84E0D974, 0x5DFD, 0x4810, { 0xA6, 0x80, 0x8D, 0xEB, 0xDA, 0xBB, 0x28, 0xB7 }
	};
	static const GUID noMobilityCenter = {
		0xA55F91DC, 0xAADC, 0x4038, { 0x82, 0xFC, 0x84, 0x80, 0x6D, 0x92, 0x89, 0x78 }
	};
	if (policy(showCommandPrompt))
		*useCmd = true;
	*mobilityEnabled = false;
	POWER_PLATFORM_ROLE role = PowerDeterminePlatformRole();
	SYSTEM_POWER_CAPABILITIES capabilities = {};
	bool laptop = role == PlatformRoleMobile ||
		(role == PlatformRoleDesktop && GetPwrCapabilities(&capabilities) &&
			capabilities.SystemBatteriesPresent && !capabilities.BatteriesAreShortTerm);
	if (!GetSystemMetrics(SM_CLEANBOOT) && laptop && !policy(noMobilityCenter))
	{
		HKEY key;
		if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
			L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\explorer\\ControlPanel\\NameSpace\\{5ea4f148-308c-46d7-98a9-49041b1dd468}",
			0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
		{
			*mobilityEnabled = true;
			RegCloseKey(key);
		}
	}
	FreeLibrary(shcore);
	return S_OK;
}

static HRESULT OpenWinXFolder(IShellFolder** folder)
{
	*folder = nullptr;
	PWSTR local = nullptr;
	HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local);
	WCHAR path[MAX_PATH + 1] = {};
	if (SUCCEEDED(hr))
		hr = StringCchCopyW(path, MAX_PATH, local);
	if (SUCCEEDED(hr))
		hr = StringCchCatW(path, MAX_PATH, L"\\Microsoft\\Windows\\WinX");
	CoTaskMemFree(local);
	if (FAILED(hr))
		return hr;

	PIDLIST_ABSOLUTE pidl = nullptr;
	hr = SHParseDisplayName(path, nullptr, &pidl, 0, nullptr);
	if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
	{
		// Windows seeds a missing per-user WinX directory from the default profile.
		PWSTR profiles = nullptr;
		hr = SHGetKnownFolderPath(FOLDERID_UserProfiles, 0, nullptr, &profiles);
		WCHAR source[MAX_PATH + 1] = {};
		if (SUCCEEDED(hr))
			hr = StringCchCopyW(source, MAX_PATH, profiles);
		if (SUCCEEDED(hr))
			hr = StringCchCatW(source, MAX_PATH, L"\\Default\\AppData\\Local\\Microsoft\\Windows\\WinX");
		CoTaskMemFree(profiles);
		if (SUCCEEDED(hr))
		{
			SHFILEOPSTRUCTW copy = {};
			copy.wFunc = FO_COPY;
			copy.pFrom = source;
			copy.pTo = path;
			copy.fFlags = FOF_SILENT | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_NOCOPYSECURITYATTRIBS;
			int error = SHFileOperationW(&copy);
			hr = error ? HRESULT_FROM_WIN32(error) : copy.fAnyOperationsAborted ? E_ABORT : S_OK;
		}
		if (SUCCEEDED(hr))
			hr = SHParseDisplayName(path, nullptr, &pidl, 0, nullptr);
	}
	if (SUCCEEDED(hr))
		hr = SHBindToObject(nullptr, pidl, nullptr, IID_PPV_ARGS(folder));
	CoTaskMemFree(pidl);
	return hr;
}

WinXLinks::WinXLinks() : m_links(nullptr), m_strings(nullptr), m_hashFolders{}, m_duplicates{}, m_mobilityEnabled(false)
{
}

WinXLinks::~WinXLinks()
{
	if (m_links)
	{
		for (int i = 0; i < DSA_GetItemCount(m_links); ++i)
			CoTaskMemFree(static_cast<WinXLink*>(DSA_GetItemPtr(m_links, i))->path);
		DSA_Destroy(m_links);
	}
	for (auto path : m_hashFolders)
		CoTaskMemFree(path);
	if (m_strings)
		FreeLibrary(m_strings);
}

HRESULT WinXLinks::CheckHash(IPropertyStore* store, DWORD* hash) const
{
	PROPVARIANT value = {};
	HRESULT hr = store->GetValue(kWinXHash, &value);
	ULONG stored = 0;
	if (SUCCEEDED(hr))
		hr = PropVariantToUInt32(value, &stored);
	PropVariantClear(&value);
	if (FAILED(hr))
		return hr;

	PWSTR target = nullptr, arguments = nullptr;
	hr = ReadString(store, PKEY_Link_TargetParsingPath, &target);
	if (SUCCEEDED(hr))
	{
		hr = ReadString(store, PKEY_Link_Arguments, &arguments);
		if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
			hr = S_OK;
	}
	WCHAR generalized[MAX_PATH];
	if (SUCCEEDED(hr))
	{
		hr = StringCchCopyW(generalized, ARRAYSIZE(generalized), target);
		for (int i = 0; SUCCEEDED(hr) && i < ARRAYSIZE(kHashFolders); ++i)
		{
			if (!m_hashFolders[i])
				continue;
			int prefix = PathCommonPrefixW(m_hashFolders[i], target, nullptr);
			if (prefix && prefix == lstrlenW(m_hashFolders[i]))
			{
				if (!StringFromGUID2(*kHashFolders[i], generalized, ARRAYSIZE(generalized)))
					hr = E_FAIL;
				else
					hr = StringCchCatW(generalized, ARRAYSIZE(generalized), target + prefix);
				break;
			}
		}
	}
	if (SUCCEEDED(hr))
	{
		static const WCHAR salt[] = L"do not prehash links.  this should only be done by the user.";
		size_t capacity = lstrlenW(generalized) + (arguments ? lstrlenW(arguments) : 0) + ARRAYSIZE(salt);
		PWSTR input = static_cast<PWSTR>(CoTaskMemAlloc(capacity * sizeof(WCHAR)));
		if (!input)
			hr = E_OUTOFMEMORY;
		else
		{
			hr = StringCchCopyW(input, capacity, generalized);
			if (SUCCEEDED(hr) && arguments)
				hr = StringCchCatW(input, capacity, arguments);
			if (SUCCEEDED(hr))
				hr = StringCchCatW(input, capacity, salt);
			int length = static_cast<int>(capacity - 1);
			int lowerLength = SUCCEEDED(hr) ? LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
				input, length, nullptr, 0, nullptr, nullptr, 0) : 0;
			if (SUCCEEDED(hr) && !lowerLength)
				hr = HRESULT_FROM_WIN32(GetLastError());
			if (SUCCEEDED(hr) && lowerLength > static_cast<int>(capacity))
			{
				PWSTR resized = static_cast<PWSTR>(CoTaskMemRealloc(input, lowerLength * sizeof(WCHAR)));
				if (resized)
					input = resized;
				else
					hr = E_OUTOFMEMORY;
			}
			if (SUCCEEDED(hr))
			{
				if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, input, length,
					input, lowerLength, nullptr, nullptr, 0))
					hr = HRESULT_FROM_WIN32(GetLastError());
				else
					hr = HashData(reinterpret_cast<BYTE*>(input), lowerLength * sizeof(WCHAR),
						reinterpret_cast<BYTE*>(hash), sizeof(*hash));
			}
			CoTaskMemFree(input);
			if (SUCCEEDED(hr) && stored != *hash)
				hr = E_FAIL;
		}
	}
	CoTaskMemFree(arguments);
	CoTaskMemFree(target);
	return hr;
}

HRESULT WinXLinks::AppendLink(HMENU menu, IShellItem2* item, bool useCmd)
{
	SFGAOF attributes = 0;
	HRESULT hr = item->GetAttributes(SFGAO_LINK, &attributes);
	if (FAILED(hr) || !(attributes & SFGAO_LINK))
		return S_FALSE;

	IPropertyStore* store = nullptr;
	const PROPERTYKEY keys[] = { kWinXHash, PKEY_Link_TargetParsingPath, PKEY_Link_Arguments, PKEY_Comment };
	hr = item->GetPropertyStoreForKeys(keys, ARRAYSIZE(keys), GPS_DEFAULT, IID_PPV_ARGS(&store));
	if (FAILED(hr))
		return hr == E_OUTOFMEMORY ? hr : S_FALSE;
	DWORD hash = 0;
	hr = CheckHash(store, &hash);
	if (FAILED(hr))
	{
		store->Release();
		return hr == E_OUTOFMEMORY ? hr : S_FALSE;
	}
	for (auto skipped : kSkippedHashes)
	{
		if (hash == skipped)
		{
			store->Release();
			return S_FALSE;
		}
	}
	for (int i = 0; i < ARRAYSIZE(kDuplicateHashes); ++i)
	{
		if (hash == kDuplicateHashes[i])
		{
			if (m_duplicates[i])
			{
				store->Release();
				return S_FALSE;
			}
			m_duplicates[i] = true;
		}
	}

	IShellLinkW* shellLink = nullptr;
	IShellLinkDataList* data = nullptr;
	DWORD linkFlags = 0;
	hr = item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&shellLink));
	if (SUCCEEDED(hr))
		hr = shellLink->QueryInterface(IID_PPV_ARGS(&data));
	if (SUCCEEDED(hr))
		hr = data->GetFlags(&linkFlags);
	if (data)
		data->Release();
	if (shellLink)
		shellLink->Release();
	if (FAILED(hr))
	{
		store->Release();
		return hr == E_OUTOFMEMORY ? hr : S_FALSE;
	}
	UINT resource = 0;
	DWORD flags = 0;
	if (SUCCEEDED(hr))
	{
		for (const auto& builtIn : kBuiltInLinks)
		{
			if (builtIn.hash == hash && ((builtIn.flags & 1) != 0) == ((linkFlags & SLDF_RUNAS_USER) != 0))
			{
				flags = builtIn.flags;
				resource = builtIn.text;
				break;
			}
		}
		// The console pair is selected by command identity, never by link name.
		if (((flags & 0x40) && useCmd) || ((flags & 0x80) && !useCmd))
			hr = S_FALSE;
		if ((flags & 0x10) && !m_mobilityEnabled)
			hr = S_FALSE;
	}

	PWSTR text = nullptr;
	WinXLink link = { nullptr, hash, flags, (linkFlags & SLDF_RUNAS_USER) != 0 };
	if (hr == S_OK)
	{
		if (FAILED(ReadString(store, PKEY_Comment, &text)) || !text || !*text)
		{
			CoTaskMemFree(text);
			text = nullptr;
			hr = resource ? LoadNativeText(resource, &text) : HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
			if (FAILED(hr) && hr != E_OUTOFMEMORY)
				hr = item->GetDisplayName(SIGDN_NORMALDISPLAY, &text);
		}
		if (SUCCEEDED(hr))
			hr = item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &link.path);
		if (SUCCEEDED(hr))
		{
			if (DSA_GetItemCount(m_links) >= WINX_CHOICE_FIRST - 1)
				hr = HRESULT_FROM_WIN32(ERROR_TOO_MANY_CMDS);
			else if (DSA_InsertItem(m_links, DSA_APPEND, &link) == -1)
				hr = E_OUTOFMEMORY;
			else
			{
				link.path = nullptr;
				if (!InsertMenuW(menu, 0, MF_BYPOSITION | MF_STRING, DSA_GetItemCount(m_links), text))
					hr = HRESULT_FROM_WIN32(GetLastError());
			}
		}
	}
	CoTaskMemFree(link.path);
	CoTaskMemFree(text);
	store->Release();
	return hr;
}

HRESULT WinXLinks::AppendFolder(HMENU menu, IShellFolder* folder, bool useCmd)
{
	ShellChildren children;
	HRESULT hr = children.Load(folder, SHCONTF_NONFOLDERS);
	if (FAILED(hr))
		return hr;
	for (int i = 0; i < DPA_GetPtrCount(children.items); ++i)
	{
		IShellItem2* item = nullptr;
		hr = SHCreateItemWithParent(nullptr, folder, static_cast<PCUITEMID_CHILD>(DPA_GetPtr(children.items, i)), IID_PPV_ARGS(&item));
		if (SUCCEEDED(hr))
		{
			hr = AppendLink(menu, item, useCmd);
			item->Release();
		}
		if (FAILED(hr))
			return hr;
	}
	return S_OK;
}

HRESULT WinXLinks::AppendToMenu(HMENU menu, bool useCmd)
{
	HRESULT policyResult = LoadMenuPolicy(&useCmd, &m_mobilityEnabled);
	if (FAILED(policyResult))
		return policyResult;
	m_links = DSA_Create(sizeof(WinXLink), 8);
	if (!m_links)
		return E_OUTOFMEMORY;
	m_strings = LoadLibraryExW(L"twinui.dll", nullptr,
		LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_SEARCH_SYSTEM32);
	for (int i = 0; i < ARRAYSIZE(kHashFolders); ++i)
		SHGetKnownFolderPath(*kHashFolders[i], KF_FLAG_DEFAULT_PATH | KF_FLAG_DONT_VERIFY, nullptr, &m_hashFolders[i]);

	IShellFolder* root = nullptr;
	HRESULT hr = OpenWinXFolder(&root);
	if (FAILED(hr))
		return hr;
	ShellChildren groups;
	hr = groups.Load(root, SHCONTF_FOLDERS);
	if (SUCCEEDED(hr))
	{
		for (int i = 0; i < DPA_GetPtrCount(groups.items); ++i)
		{
			IShellFolder* folder = nullptr;
			hr = root->BindToObject(static_cast<PCUIDLIST_RELATIVE>(DPA_GetPtr(groups.items, i)), nullptr, IID_PPV_ARGS(&folder));
			if (FAILED(hr))
				break;
			int before = GetMenuItemCount(menu);
			hr = AppendFolder(menu, folder, useCmd);
			folder->Release();
			if (FAILED(hr))
				break;
			if (before && GetMenuItemCount(menu) != before &&
				!InsertMenuW(menu, GetMenuItemCount(menu) - before, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr))
			{
				hr = HRESULT_FROM_WIN32(GetLastError());
				break;
			}
		}
	}
	root->Release();
	return hr;
}

HRESULT WinXLinks::LoadNativeText(UINT resource, PWSTR* text) const
{
	*text = nullptr;
	if (!m_strings)
		return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
	LPCWSTR nativeText = nullptr;
	int length = LoadStringW(m_strings, resource, reinterpret_cast<LPWSTR>(&nativeText), 0);
	if (!length)
		return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
	*text = static_cast<PWSTR>(CoTaskMemAlloc((length + 1) * sizeof(WCHAR)));
	if (!*text)
		return E_OUTOFMEMORY;
	CopyMemory(*text, nativeText, length * sizeof(WCHAR));
	(*text)[length] = 0;
	return S_OK;
}

HRESULT WinXLinks::LoadShutdownMenuText(PWSTR* text) const
{
	return LoadNativeText(0x2AB2, text);
}

const WinXLink* WinXLinks::Get(UINT command) const
{
	if (!m_links || !command || command > static_cast<UINT>(DSA_GetItemCount(m_links)))
		return nullptr;
	return static_cast<const WinXLink*>(DSA_GetItemPtr(m_links, command - 1));
}
