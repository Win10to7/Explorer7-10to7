#include "StartMenuShortcuts.h"
#include <shlguid.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

StartMenuRoots::~StartMenuRoots()
{
	for (UINT i = 0; i < m_count; ++i)
		CoTaskMemFree(m_roots[i]);
}

HRESULT StartMenuRoots::Add(PCIDLIST_ABSOLUTE root, bool collapseOverlaps)
{
	if (!root)
		return E_INVALIDARG;
	UINT descendants = 0;
	for (UINT i = 0; i < m_count; ++i)
	{
		if (ILIsEqual(m_roots[i], root) || (collapseOverlaps && ILIsParent(m_roots[i], root, FALSE)))
			return S_FALSE;
		if (collapseOverlaps && ILIsParent(root, m_roots[i], FALSE))
			++descendants;
	}
	if (m_count - descendants == ARRAYSIZE(m_roots))
		return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
	PIDLIST_ABSOLUTE copy = ILCloneFull(root);
	if (!copy)
		return E_OUTOFMEMORY;
	UINT kept = 0;
	for (UINT i = 0; i < m_count; ++i)
	{
		if (collapseOverlaps && ILIsParent(root, m_roots[i], FALSE))
			CoTaskMemFree(m_roots[i]);
		else
			m_roots[kept++] = m_roots[i];
	}
	m_roots[kept] = copy;
	m_count = kept + 1;
	return S_OK;
}

HRESULT StartMenuRoots::Initialize(bool collapseOverlaps)
{
	const KNOWNFOLDERID* folders[] = {
		&FOLDERID_StartMenu, &FOLDERID_CommonStartMenu,
		&FOLDERID_Programs, &FOLDERID_CommonPrograms
	};
	for (const KNOWNFOLDERID* folder : folders)
	{
		PIDLIST_ABSOLUTE pidl = nullptr;
		HRESULT hr = SHGetKnownFolderIDList(*folder, KF_FLAG_DONT_VERIFY, nullptr, &pidl);
		if (SUCCEEDED(hr))
			hr = Add(pidl, collapseOverlaps);
		CoTaskMemFree(pidl);
		if (FAILED(hr))
			return hr;
	}
	return S_OK;
}

namespace
{
	bool IsMissing(HRESULT hr)
	{
		return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
			hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND) ||
			hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
	}

	struct DirectoryAncestor
	{
		DWORD volume;
		DWORD indexHigh;
		DWORD indexLow;
		const DirectoryAncestor* parent;
	};

	HRESULT VisitFolder(PCIDLIST_ABSOLUTE root, IShellFolder* folder,
		StartMenuShortcutVisitor visitor, void* context, const DirectoryAncestor* ancestors)
	{
		ComPtr<IShellItem> directory;
		HRESULT hr = SHCreateItemFromIDList(root, IID_PPV_ARGS(&directory));
		if (FAILED(hr))
			return hr;
		PWSTR path = nullptr;
		hr = directory->GetDisplayName(SIGDN_FILESYSPATH, &path);
		if (FAILED(hr))
			return hr;
		HANDLE handle = CreateFileW(path, FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		DWORD error = handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
		CoTaskMemFree(path);
		if (error != ERROR_SUCCESS)
			return IsMissing(HRESULT_FROM_WIN32(error)) ? S_OK : HRESULT_FROM_WIN32(error);
		BY_HANDLE_FILE_INFORMATION identity;
		BOOL haveIdentity = GetFileInformationByHandle(handle, &identity);
		error = haveIdentity ? ERROR_SUCCESS : GetLastError();
		CloseHandle(handle);
		if (error != ERROR_SUCCESS)
			return HRESULT_FROM_WIN32(error);
		for (const DirectoryAncestor* ancestor = ancestors; ancestor; ancestor = ancestor->parent)
		{
			if (ancestor->volume == identity.dwVolumeSerialNumber &&
				ancestor->indexHigh == identity.nFileIndexHigh && ancestor->indexLow == identity.nFileIndexLow)
				return S_OK; // A directory link points back into the current traversal.
		}
		const DirectoryAncestor current = {
			identity.dwVolumeSerialNumber, identity.nFileIndexHigh, identity.nFileIndexLow, ancestors
		};
		ComPtr<IEnumIDList> children;
		hr = folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &children);
		if (hr == S_FALSE)
			return S_OK;
		if (FAILED(hr))
			return hr;
		PITEMID_CHILD child = nullptr;
		while ((hr = children->Next(1, &child, nullptr)) == S_OK)
		{
			SFGAOF attributes = SFGAO_LINK | SFGAO_FOLDER | SFGAO_FILESYSTEM;
			PCUITEMID_CHILD items[] = { child };
			HRESULT result = folder->GetAttributesOf(1, items, &attributes);
			PIDLIST_ABSOLUTE absolute = nullptr;
			if (SUCCEEDED(result) && (attributes & SFGAO_FILESYSTEM))
			{
				absolute = ILCombine(root, child);
				result = absolute ? S_OK : E_OUTOFMEMORY;
				if (SUCCEEDED(result) && (attributes & SFGAO_LINK))
				{
					ComPtr<IShellItem2> item;
					result = SHCreateItemWithParent(root, folder, child, IID_PPV_ARGS(&item));
					if (SUCCEEDED(result))
						result = visitor(absolute, item.Get(), context);
				}
				else if (SUCCEEDED(result) && (attributes & SFGAO_FOLDER))
				{
					ComPtr<IShellFolder> descendant;
					result = folder->BindToObject(child, nullptr, IID_PPV_ARGS(&descendant));
					if (SUCCEEDED(result))
						result = VisitFolder(absolute, descendant.Get(), visitor, context, &current);
				}
			}
			CoTaskMemFree(absolute);
			CoTaskMemFree(child);
			if (FAILED(result) && !IsMissing(result))
				return result;
		}
		return FAILED(hr) ? hr : S_OK;
	}
}

HRESULT VisitStartMenuShortcuts(const StartMenuRoots& roots, StartMenuShortcutVisitor visitor, void* context)
{
	if (!visitor)
		return E_INVALIDARG;
	for (UINT i = 0; i < roots.Count(); ++i)
	{
		ComPtr<IShellFolder> folder;
		HRESULT hr = SHBindToObject(nullptr, roots.Items()[i], nullptr, IID_PPV_ARGS(&folder));
		if (SUCCEEDED(hr))
			hr = VisitFolder(roots.Items()[i], folder.Get(), visitor, context, nullptr);
		if (FAILED(hr) && !IsMissing(hr))
			return hr;
	}
	return S_OK;
}

bool GetNewShortcutInstallTime(PCWSTR path, const UEMINFO& shortcutUsage,
	const UEMINFO& appUsage, const FILETIME& cutoff, const FILETIME* referenceTime, FILETIME* installed)
{
	if (shortcutUsage.fExcludeFromMFU || appUsage.fExcludeFromMFU ||
		shortcutUsage.cLaunches || appUsage.cLaunches ||
		shortcutUsage.ftExecute.dwLowDateTime || shortcutUsage.ftExecute.dwHighDateTime ||
		appUsage.ftExecute.dwLowDateTime || appUsage.ftExecute.dwHighDateTime)
		return false;
	WIN32_FILE_ATTRIBUTE_DATA data;
	if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data))
		return false;
	FILETIME sampledTime;
	if (!referenceTime)
	{
		GetSystemTimePreciseAsFileTime(&sampledTime);
		referenceTime = &sampledTime;
	}
	const FILETIME& now = *referenceTime;
	const FILETIME& created = data.ftCreationTime;
	const ULONGLONG creation = (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
	const ULONGLONG current = (static_cast<ULONGLONG>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
	const ULONGLONG clearedThrough = (static_cast<ULONGLONG>(cutoff.dwHighDateTime) << 32) | cutoff.dwLowDateTime;
	const ULONGLONG week = 7ULL * 24 * 60 * 60 * 10000000;
	if (creation <= clearedThrough || creation > current || current - creation >= week)
		return false;
	*installed = created;
	return true;
}
