#pragma once
#include "common.h"
#include <commctrl.h>

// Command IDs below this value belong to the shortcut collection.
#define WINX_CHOICE_FIRST 0xF000

struct WinXLink
{
	PWSTR path;
	DWORD hash;
	DWORD flags;
	bool runAs;
};

class WinXLinks
{
public:
	WinXLinks();
	~WinXLinks();
	HRESULT AppendToMenu(HMENU menu, bool useCmd);
	const WinXLink* Get(UINT command) const;
	// The caller frees the localized, null-terminated heading with CoTaskMemFree.
	HRESULT LoadShutdownMenuText(PWSTR* text) const;

private:
	HRESULT AppendFolder(HMENU menu, IShellFolder* folder, bool useCmd);
	HRESULT AppendLink(HMENU menu, IShellItem2* item, bool useCmd);
	HRESULT CheckHash(IPropertyStore* store, DWORD* hash) const;
	HRESULT LoadNativeText(UINT resource, PWSTR* text) const;

	HDSA m_links;
	HMODULE m_strings;
	PWSTR m_hashFolders[3];
	bool m_duplicates[4];
	bool m_mobilityEnabled;

	WinXLinks(const WinXLinks&) = delete;
	WinXLinks& operator=(const WinXLinks&) = delete;
};
