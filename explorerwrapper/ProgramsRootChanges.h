#pragma once
#include "common.h"

// Mixed AppsFolder/filesystem roots need item events, not a root UPDATEDIR.
// Keeps native PIDLs so removals never need to parse deleted shortcuts.
class ProgramsRootChanges
{
public:
    ProgramsRootChanges() = default;
    ~ProgramsRootChanges();
    ProgramsRootChanges(const ProgramsRootChanges&) = delete;
    ProgramsRootChanges& operator=(const ProgramsRootChanges&) = delete;
    HRESULT Refresh(IShellFolder* folder, PCIDLIST_ABSOLUTE root);

private:
    HDSA m_items = nullptr;
    int m_count = 0;
};
