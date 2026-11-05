#pragma once
#include "common.h"

DEFINE_GUID(CLSID_ProgramsFolder, 0x7be9d83c, 0xa729, 0x4d97, 0xb5, 0xa7, 0x1b, 0x73, 0x13, 0xc3, 0x9e, 0x0a);
DEFINE_GUID(CLSID_ProgramsFolderCommon, 0xfc1ee10b, 0x7ef6, 0x41b5, 0xbb, 0x60, 0x98, 0xd2, 0x6d, 0xd9, 0xfc, 0xd1);
DEFINE_GUID(CLSID_ProgramsFolderAndFastItems, 0x865e5e76, 0xad83, 0x4dca, 0xa1, 0x09, 0x50, 0xdc, 0x21, 0x13, 0xce, 0x9a);
DEFINE_GUID(CLSID_MergedFolder, 0x26fdc864, 0xbe88, 0x46e7, 0x92, 0x35, 0x03, 0x2d, 0x8e, 0xa5, 0x16, 0x2e);
DEFINE_GUID(CLSID_StartMenu, 0x4622ad11, 0xff23, 0x11d0, 0x8d, 0x34, 0x00, 0xa0, 0xc9, 0x0f, 0x27, 0x19);
DEFINE_GUID(CLSID_StartMenuFolder, 0x48e7caab, 0xb918, 0x4e58, 0xa9, 0x4d, 0x50, 0x55, 0x19, 0xc7, 0x95, 0xdc);

struct ProgramsFolderSource
{
    const GUID* namespaceId;
    PCIDLIST_ABSOLUTE root;
    ULONG flags;
    ULONG group;
};

// Native merged-folder composition, shared by the legacy root factory and isolated UI fixtures.
// Requires initialized MinHook; installs the native Programs shortcut-data hook once.
HRESULT CreateMergedProgramsFolder(const ProgramsFolderSource* sources, UINT count, REFIID iid, void** result);
bool IsProgramsFolderClass(REFCLSID clsid);

class CProgramsFolderClassFactory : public IClassFactory
{
public:
    CProgramsFolderClassFactory(REFCLSID clsid, bool showStoreApps);
    STDMETHODIMP QueryInterface(REFIID iid, void** result) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** result) override;
    STDMETHODIMP LockServer(BOOL lock) override;

private:
    LONG m_references = 1;
    CLSID m_clsid;
    bool m_showStoreApps;
};
