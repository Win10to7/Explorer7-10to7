#define INITGUID
#include "ProgramsFolder.h"
#include "AugmentedShellFolder.h"
#include "MinHook.h"
#include "dbgprint.h"
#include <shlguid.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr ULONG DefaultNamespace = 0xFF00;
    constexpr ULONG MergeCommon = 0x0006;
    constexpr ULONG ProgramsItems = 0x000A;

    struct KnownSource
    {
        const KNOWNFOLDERID* folder;
        const GUID* namespaceId;
        ULONG flags;
        ULONG group;
    };

    const KnownSource ProgramsAndFastItems[] = {
        { &FOLDERID_Programs, &CLSID_ProgramsFolder, DefaultNamespace | ProgramsItems, 2 },
        { &FOLDERID_CommonPrograms, &CLSID_ProgramsFolderCommon, MergeCommon | ProgramsItems, 2 },
        { &FOLDERID_StartMenu, &CLSID_StartMenu, ProgramsItems, 1 },
        { &FOLDERID_CommonStartMenu, &CLSID_StartMenu, 4 | ProgramsItems, 1 },
    };
    const KnownSource Programs[] = {
        { &FOLDERID_Programs, &CLSID_ProgramsFolder, DefaultNamespace | 2, 2 },
        { &FOLDERID_CommonPrograms, &CLSID_ProgramsFolderCommon, MergeCommon, 2 },
    };
    const KnownSource StartMenu[] = {
        { &FOLDERID_StartMenu, &CLSID_StartMenu, DefaultNamespace, 1 },
        { &FOLDERID_CommonStartMenu, &CLSID_StartMenu, 4, 1 },
    };
    // AppsFolder contributes Store entries; the physical Programs namespace must
    // remain authoritative for the legacy tree's attributes and change handling.
    const KnownSource StoreApps = { &FOLDERID_AppsFolder, &FOLDERID_AppsFolder, ProgramsItems, 2 };

    using GetUIObjectProc = HRESULT(STDMETHODCALLTYPE*)(IShellFolder*, HWND, UINT,
        PCUITEMID_CHILD_ARRAY, REFIID, UINT*, void**);
    GetUIObjectProc originalGetUIObject = nullptr;
    INIT_ONCE dragHookOnce = INIT_ONCE_STATIC_INIT;
    HRESULT dragHookResult = E_UNEXPECTED;

    HRESULT STDMETHODCALLTYPE GetProgramsUIObject(IShellFolder* folder, HWND owner, UINT count,
        PCUITEMID_CHILD_ARRAY children, REFIID iid, UINT* reserved, void** result)
    {
        if (iid != IID_IDataObject || !count || !children || !result)
            return originalGetUIObject(folder, owner, count, children, iid, reserved, result);
        *result = nullptr;

        ComPtr<IAugmentedShellFolder> merged;
        HRESULT hr = folder->QueryInterface(IID_PPV_ARGS(&merged));
        if (FAILED(hr))
            return hr;
        for (UINT i = 0; i < count; ++i)
        {
            GUID source;
            hr = merged->GetNameSpaceID(children[i], &source);
            if (FAILED(hr))
                return hr;
            if (source != CLSID_ProgramsFolder && source != CLSID_ProgramsFolderCommon &&
                source != CLSID_StartMenu)
                return originalGetUIObject(folder, owner, count, children, iid, reserved, result);
            SFGAOF attributes = SFGAO_LINK | SFGAO_FILESYSTEM;
            hr = folder->GetAttributesOf(1, children + i, &attributes);
            if (FAILED(hr))
                return hr;
            if (attributes != (SFGAO_LINK | SFGAO_FILESYSTEM))
                return originalGetUIObject(folder, owner, count, children, iid, reserved, result);
        }

        // Native merged-folder CIDA keeps virtual Programs PIDLs. Windows 10's
        // file-operation engine rejects those even when CF_HDROP has valid paths.
        // Publish the same physical identities used by the native highlight reader.
        PIDLIST_ABSOLUTE single = nullptr;
        PIDLIST_ABSOLUTE* physical = &single;
        if (count > 1)
        {
            if (count > SIZE_MAX / sizeof(*physical))
                return E_OUTOFMEMORY;
            physical = static_cast<PIDLIST_ABSOLUTE*>(CoTaskMemAlloc(count * sizeof(*physical)));
            if (!physical)
                return E_OUTOFMEMORY;
            ZeroMemory(physical, count * sizeof(*physical));
        }
        for (UINT i = 0; SUCCEEDED(hr) && i < count; ++i)
        {
            ComPtr<IIdentityName> identity;
            hr = folder->BindToObject(children[i], nullptr, IID_PPV_ARGS(&identity));
            if (SUCCEEDED(hr))
                hr = identity->GetItemIDList(&physical[i]);
        }
        if (SUCCEEDED(hr) && count == 1)
        {
            ComPtr<IShellFolder> parent;
            PCUITEMID_CHILD child = nullptr;
            hr = SHBindToParent(single, IID_PPV_ARGS(&parent), &child);
            if (SUCCEEDED(hr))
                hr = parent->GetUIObjectOf(owner, 1, &child, iid, reserved, result);
        }
        else if (SUCCEEDED(hr))
        {
            ComPtr<IShellItemArray> items;
            hr = SHCreateShellItemArrayFromIDLists(count,
                const_cast<PCIDLIST_ABSOLUTE*>(physical), &items);
            if (SUCCEEDED(hr))
                hr = items->BindToHandler(nullptr, BHID_DataObject, iid, result);
        }
        for (UINT i = 0; i < count; ++i)
            CoTaskMemFree(physical[i]);
        if (physical != &single)
            CoTaskMemFree(physical);
        return hr;
    }

    BOOL CALLBACK InitializeDragHook(PINIT_ONCE, void* context, void**)
    {
        // Discover the native implementation through the public IShellFolder ABI,
        // rather than an OS-build-specific shell32 signature or address.
        void* method = (*static_cast<void***>(context))[10];
        MH_STATUS status = MH_CreateHook(method, GetProgramsUIObject,
            reinterpret_cast<void**>(&originalGetUIObject));
        if (status == MH_OK)
        {
            status = MH_EnableHook(method);
            if (status != MH_OK)
                MH_RemoveHook(method);
        }
        dragHookResult = status == MH_OK ? S_OK : E_FAIL;
        if (FAILED(dragHookResult))
            dbgprintf(L"All Programs drag data hook failed: MinHook status %d", status);
        return TRUE;
    }

    HRESULT CreateKnownSources(const KnownSource* definitions, UINT count, bool includeApps, REFIID iid, void** result)
    {
        ProgramsFolderSource sources[ARRAYSIZE(ProgramsAndFastItems) + 1] = {};
        PIDLIST_ABSOLUTE roots[ARRAYSIZE(sources)] = {};
        HRESULT hr = S_OK;
        UINT added = 0;
        for (UINT i = 0; i < count + (includeApps ? 1u : 0u); ++i)
        {
            const KnownSource& source = i < count ? definitions[i] : StoreApps;
            hr = SHGetKnownFolderIDList(*source.folder, KF_FLAG_DONT_VERIFY, nullptr, &roots[added]);
            if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
            {
                hr = S_OK;
                continue;
            }
            if (FAILED(hr))
                break;
            sources[added] = { source.namespaceId, roots[added], source.flags, source.group };
            ++added;
        }
        if (SUCCEEDED(hr))
            hr = CreateMergedProgramsFolder(sources, added, iid, result);
        for (PIDLIST_ABSOLUTE root : roots)
            CoTaskMemFree(root);
        return hr;
    }
}

HRESULT CreateMergedProgramsFolder(const ProgramsFolderSource* sources, UINT count, REFIID iid, void** result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!sources || !count)
        return E_INVALIDARG;
    ComPtr<IAugmentedShellFolder> merged;
    HRESULT hr = CoCreateInstance(CLSID_MergedFolder, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&merged));
    for (UINT i = 0; SUCCEEDED(hr) && i < count; ++i)
    {
        ComPtr<IShellFolder> folder;
        hr = SHBindToObject(nullptr, sources[i].root, nullptr, IID_PPV_ARGS(&folder));
        if (SUCCEEDED(hr))
            hr = merged->AddNameSpace(const_cast<GUID*>(sources[i].namespaceId), folder.Get(), sources[i].root,
                sources[i].flags, sources[i].group);
    }
    if (SUCCEEDED(hr))
    {
        ComPtr<IShellFolder> folder;
        hr = merged.As(&folder);
        if (SUCCEEDED(hr))
        {
            if (!InitOnceExecuteOnce(&dragHookOnce, InitializeDragHook, folder.Get(), nullptr))
                hr = HRESULT_FROM_WIN32(GetLastError());
            else
                hr = dragHookResult;
        }
    }
    return SUCCEEDED(hr) ? merged->QueryInterface(iid, result) : hr;
}

bool IsProgramsFolderClass(REFCLSID clsid)
{
    return clsid == CLSID_ProgramsFolderAndFastItems || clsid == CLSID_ProgramsFolder || clsid == CLSID_StartMenuFolder;
}

CProgramsFolderClassFactory::CProgramsFolderClassFactory(REFCLSID clsid, bool showStoreApps)
    : m_clsid(clsid), m_showStoreApps(showStoreApps)
{
}

HRESULT CProgramsFolderClassFactory::QueryInterface(REFIID iid, void** result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (iid != IID_IUnknown && iid != IID_IClassFactory)
        return E_NOINTERFACE;
    *result = static_cast<IClassFactory*>(this);
    AddRef();
    return S_OK;
}

ULONG CProgramsFolderClassFactory::AddRef()
{
    return InterlockedIncrement(&m_references);
}

ULONG CProgramsFolderClassFactory::Release()
{
    ULONG references = InterlockedDecrement(&m_references);
    if (!references)
        delete this;
    return references;
}

HRESULT CProgramsFolderClassFactory::CreateInstance(IUnknown* outer, REFIID iid, void** result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    if (!IsProgramsFolderClass(m_clsid))
        return CLASS_E_CLASSNOTAVAILABLE;
    if (m_showStoreApps)
        return CreateKnownSources(ProgramsAndFastItems, ARRAYSIZE(ProgramsAndFastItems), true, iid, result);
    if (m_clsid == CLSID_ProgramsFolderAndFastItems)
        return CreateKnownSources(ProgramsAndFastItems, ARRAYSIZE(ProgramsAndFastItems), false, iid, result);
    if (m_clsid == CLSID_ProgramsFolder)
        return CreateKnownSources(Programs, ARRAYSIZE(Programs), false, iid, result);
    return CreateKnownSources(StartMenu, ARRAYSIZE(StartMenu), false, iid, result);
}

HRESULT CProgramsFolderClassFactory::LockServer(BOOL)
{
    return S_OK;
}
