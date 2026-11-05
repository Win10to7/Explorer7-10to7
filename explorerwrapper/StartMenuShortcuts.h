#pragma once
#include "common.h"
#include "UserAssist.h"

// User/common Start Menu and Programs roots. Enumeration collapses overlapping trees;
// namespace translation retains every distinct source, including nested Programs roots.
class StartMenuRoots
{
public:
	StartMenuRoots() = default;
	~StartMenuRoots();
	StartMenuRoots(const StartMenuRoots&) = delete;
	StartMenuRoots& operator=(const StartMenuRoots&) = delete;

	HRESULT Initialize(bool collapseOverlaps = true);
	HRESULT Add(PCIDLIST_ABSOLUTE root, bool collapseOverlaps = true);
	UINT Count() const { return m_count; }
	PCIDLIST_ABSOLUTE const* Items() const { return m_roots; }

private:
	PIDLIST_ABSOLUTE m_roots[4] = {};
	UINT m_count = 0;
};

// The PIDL and shell item are borrowed for the duration of the callback.
using StartMenuShortcutVisitor = HRESULT (*)(PCIDLIST_ABSOLUTE, IShellItem2*, void*);
HRESULT VisitStartMenuShortcuts(const StartMenuRoots& roots, StartMenuShortcutVisitor visitor, void* context);

// A cutoff acknowledges shortcuts created at or before it; zero means no cutoff.
// Read filesystem FILETIME precision; shell/PIDL creation properties can round fresh links into the future.
// A null referenceTime samples the precise clock after reading this shortcut's attributes.
bool GetNewShortcutInstallTime(PCWSTR path, const UEMINFO& shortcutUsage,
	const UEMINFO& appUsage, const FILETIME& cutoff, const FILETIME* referenceTime, FILETIME* installed);
