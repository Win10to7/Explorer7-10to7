#include "ProgramsTreeUpdates.h"
#include "ProgramsRootChanges.h"
#include "ShellFolderMonitor.h"
#include "StartMenuShortcuts.h"
#include "dbgprint.h"
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{
    BOOL WINAPI PostStartMenuRefresh(HWND window, UINT message, WPARAM flags, LPARAM parameter)
    {
        WCHAR className[32];
        if (GetClassNameW(window, className, ARRAYSIZE(className)) &&
            !lstrcmpW(className, L"DesktopProgramsMFU"))
        {
            // Completion of an older scan may have made the native cache valid again.
            // Match the pin-list refresh path, which bypasses that validity gate.
            flags |= 0x8; // ENUMERATEFLAG_BACKGROUND
        }
        return PostMessageW(window, message, flags, parameter);
    }

    int CALLBACK FreeDirectory(void* directory, void*)
    {
        CoTaskMemFree(directory);
        return 1;
    }

    class ProgramsTreeUpdates
    {
    public:
        ~ProgramsTreeUpdates()
        {
            m_monitor.Stop();
            if (m_directories)
                DPA_DestroyCallback(m_directories, FreeDirectory, nullptr);
            CoTaskMemFree(m_root);
            for (PWSTR path : m_sources)
                CoTaskMemFree(path);
        }

        static HRESULT Attach(INameSpaceTreeControl2* control, const StartMenuRoots& sources)
        {
            if (!control)
                return E_POINTER;
            ComPtr<IOleWindow> oleWindow;
            HRESULT hr = control->QueryInterface(IID_PPV_ARGS(&oleWindow));
            HWND window = nullptr;
            if (SUCCEEDED(hr))
                hr = oleWindow->GetWindow(&window);
            if (FAILED(hr))
                return hr;
            if (GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId())
                return RPC_E_WRONG_THREAD;
            DWORD_PTR existing = 0;
            if (GetWindowSubclass(window, WindowProc, SubclassId, &existing))
                return S_OK;
            ProgramsTreeUpdates* self = new ProgramsTreeUpdates;
            if (!self)
                return E_OUTOFMEMORY;
            hr = self->Initialize(control, sources);
            if (SUCCEEDED(hr))
            {
                if (!SetWindowSubclass(window, WindowProc, SubclassId, reinterpret_cast<DWORD_PTR>(self)))
                    hr = E_FAIL;
                else
                {
                    self->m_window = window;
                    self->AddRef(); // The window owns one reference until WM_NCDESTROY.
                    hr = self->Start(sources);
                    if (FAILED(hr))
                        self->Detach();
                }
            }
            self->Release();
            return hr;
        }

    private:
        // UI-thread references protect initialization/refresh if COM pumps WM_NCDESTROY.
        void AddRef() { ++m_references; }
        void Release() { if (!--m_references) delete this; }

        void Detach()
        {
            m_monitor.Stop();
            HWND window = m_window;
            m_window = nullptr;
            if (window)
            {
                RemoveWindowSubclass(window, WindowProc, SubclassId);
                Release();
            }
        }

        HRESULT Initialize(INameSpaceTreeControl2* control, const StartMenuRoots& sources)
        {
            if (!sources.Count() || sources.Count() > ARRAYSIZE(m_sources))
                return E_INVALIDARG;
            m_refreshMessage = RegisterWindowMessageW(L"Explorer7.ProgramsTreeUpdates.Refresh");
            if (!m_refreshMessage)
                return HRESULT_FROM_WIN32(GetLastError());
            ComPtr<IShellItemArray> items;
            HRESULT hr = control->GetRootItems(&items);
            DWORD count = 0;
            if (SUCCEEDED(hr))
                hr = items->GetCount(&count);
            if (SUCCEEDED(hr) && count != 1)
                hr = E_UNEXPECTED;
            ComPtr<IShellItem> root;
            if (SUCCEEDED(hr))
                hr = items->GetItemAt(0, &root);
            if (SUCCEEDED(hr))
                hr = SHGetIDListFromObject(root.Get(), &m_root);
            if (SUCCEEDED(hr))
                hr = SHBindToObject(nullptr, m_root, nullptr, IID_PPV_ARGS(&m_folder));
            for (UINT i = 0; SUCCEEDED(hr) && i < sources.Count(); ++i)
            {
                hr = SHGetNameFromIDList(sources.Items()[i], SIGDN_FILESYSPATH, &m_sources[i]);
                if (SUCCEEDED(hr))
                    m_lengths[i] = lstrlenW(m_sources[i]);
            }
            return hr;
        }

        HRESULT Start(const StartMenuRoots& sources)
        {
            PCIDLIST_ABSOLUTE watched[ARRAYSIZE(m_sources)] = {};
            UINT count = 0;
            for (UINT i = 0; i < sources.Count(); ++i)
            {
                bool covered = false;
                for (UINT j = 0; j < sources.Count(); ++j)
                {
                    if (i != j && ILIsParent(sources.Items()[j], sources.Items()[i], FALSE))
                    {
                        covered = true;
                        break;
                    }
                }
                if (!covered)
                    watched[count++] = sources.Items()[i];
            }
            m_processing = true;
            HRESULT hr = m_monitor.Start(watched, count, FolderChanged, this);
            // Reconcile once after subscribing. An empty first snapshot replays current
            // items idempotently, including installs during the tree's async initial fill.
            if (SUCCEEDED(hr))
                hr = Queue(sources.Items()[0], true);
            m_processing = false;
            if (SUCCEEDED(hr) && !m_window)
                hr = HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
            return hr;
        }

        int SourceForPath(PCWSTR path) const
        {
            int best = -1;
            int length = lstrlenW(path);
            for (UINT i = 0; i < ARRAYSIZE(m_sources); ++i)
            {
                int prefix = m_lengths[i];
                if (!m_sources[i] || length < prefix || (best >= 0 && prefix <= m_lengths[best]))
                    continue;
                if (CompareStringOrdinal(m_sources[i], prefix, path, prefix, TRUE) == CSTR_EQUAL &&
                    (length == prefix || path[prefix] == L'\\' || m_sources[i][prefix - 1] == L'\\'))
                    best = i;
            }
            return best;
        }

        HRESULT RefreshDirectory(PIDLIST_ABSOLUTE directory, bool* rootChanged)
        {
            do
            {
                PWSTR path = nullptr;
                HRESULT hr = SHGetNameFromIDList(directory, SIGDN_FILESYSPATH, &path);
                if (FAILED(hr))
                    return hr;
                int source = SourceForPath(path);
                if (source < 0)
                {
                    CoTaskMemFree(path);
                    return S_FALSE;
                }
                PWSTR relative = path + m_lengths[source];
                if (*relative == L'\\')
                    ++relative;
                if (!*relative)
                {
                    CoTaskMemFree(path);
                    *rootChanged = true;
                    return S_OK;
                }
                DWORD attributes = GetFileAttributesW(path);
                DWORD error = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ||
                    (error == ERROR_SUCCESS && !(attributes & FILE_ATTRIBUTE_DIRECTORY)))
                {
                    // Deleted parents climb to their surviving ancestor. UPDATEITEM can
                    // also name a file rather than the directory whose contents changed.
                    CoTaskMemFree(path);
                    continue;
                }
                PIDLIST_RELATIVE child = nullptr;
                if (error != ERROR_SUCCESS)
                    hr = HRESULT_FROM_WIN32(error);
                else
                    hr = m_folder->ParseDisplayName(nullptr, nullptr, relative, nullptr, &child, nullptr);
                CoTaskMemFree(path);
                if (FAILED(hr))
                    return hr;
                PIDLIST_ABSOLUTE absolute = ILCombine(m_root, child);
                CoTaskMemFree(child);
                if (!absolute)
                    return E_OUTOFMEMORY;
                SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, absolute, nullptr);
                CoTaskMemFree(absolute);
                return S_OK;
            } while (ILRemoveLastID(directory));
            return S_FALSE;
        }

        HRESULT Schedule()
        {
            if (m_window && m_directories && DPA_GetPtrCount(m_directories) && !m_messageQueued)
            {
                if (!PostMessageW(m_window, m_refreshMessage, 0, 0))
                {
                    DWORD error = GetLastError();
                    HRESULT hr = error ? HRESULT_FROM_WIN32(error) : E_FAIL;
                    dbgprintf(L"All Programs refresh scheduling failed: %08X", hr);
                    return hr;
                }
                m_messageQueued = true;
            }
            return S_OK;
        }

        HRESULT Queue(PCIDLIST_ABSOLUTE item, bool contents)
        {
            if (!item || !m_window)
                return S_FALSE;
            PIDLIST_ABSOLUTE directory = ILCloneFull(item);
            if (!directory)
                return E_OUTOFMEMORY;
            if (!contents && !ILRemoveLastID(directory))
            {
                CoTaskMemFree(directory);
                return S_FALSE;
            }
            if (!m_directories)
                m_directories = DPA_Create(4);
            if (m_directories)
            {
                for (int i = 0; i < DPA_GetPtrCount(m_directories); ++i)
                {
                    if (ILIsEqual(directory, static_cast<PCIDLIST_ABSOLUTE>(DPA_GetPtr(m_directories, i))))
                    {
                        CoTaskMemFree(directory);
                        return Schedule();
                    }
                }
                if (DPA_AppendPtr(m_directories, directory) != -1)
                    return Schedule();
            }
            CoTaskMemFree(directory);
            return E_OUTOFMEMORY;
        }

        void Process()
        {
            m_processing = true;
            HDPA directories = m_directories;
            m_directories = nullptr;
            bool rootChanged = false;
            if (directories)
            {
                for (int i = 0; m_window && i < DPA_GetPtrCount(directories); ++i)
                {
                    HRESULT hr = RefreshDirectory(static_cast<PIDLIST_ABSOLUTE>(DPA_GetPtr(directories, i)), &rootChanged);
                    if (FAILED(hr))
                        dbgprintf(L"All Programs directory refresh failed: %08X", hr);
                }
                DPA_DestroyCallback(directories, FreeDirectory, nullptr);
            }
            if (m_window && rootChanged)
            {
                HRESULT hr = m_rootChanges.Refresh(m_folder.Get(), m_root);
                if (FAILED(hr))
                    dbgprintf(L"All Programs root comparison failed: %08X", hr);
            }
            m_processing = false;
            Schedule();
        }

        static void FolderChanged(void* context, LONG event, PCIDLIST_ABSOLUTE first, PCIDLIST_ABSOLUTE second)
        {
            ProgramsTreeUpdates* self = static_cast<ProgramsTreeUpdates*>(context);
            // Do no COM enumeration while SHChangeNotification_Lock is held.
            HRESULT hr = self->Queue(first, event == SHCNE_UPDATEDIR || event == SHCNE_UPDATEITEM);
            if (SUCCEEDED(hr) && (event == SHCNE_RENAMEITEM || event == SHCNE_RENAMEFOLDER))
                hr = self->Queue(second, false);
            if (FAILED(hr))
                dbgprintf(L"All Programs directory queue failed: %08X", hr);
        }

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
            UINT_PTR subclassId, DWORD_PTR context)
        {
            ProgramsTreeUpdates* self = reinterpret_cast<ProgramsTreeUpdates*>(context);
            self->AddRef();
            if (message == self->m_refreshMessage)
            {
                self->m_messageQueued = false;
                if (!self->m_processing)
                    self->Process();
                self->Release();
                return 0;
            }
            if (message == WM_NCDESTROY)
                self->Detach();
            LRESULT result = DefSubclassProc(window, message, wParam, lParam);
            self->Release();
            return result;
        }

        static constexpr UINT_PTR SubclassId = 1;
        int m_references = 1;
        HWND m_window = nullptr;
        UINT m_refreshMessage = 0;
        bool m_processing = false;
        bool m_messageQueued = false;
        PIDLIST_ABSOLUTE m_root = nullptr;
        ComPtr<IShellFolder> m_folder;
        PWSTR m_sources[4] = {};
        int m_lengths[4] = {};
        ShellFolderMonitor m_monitor;
        ProgramsRootChanges m_rootChanges;
        HDPA m_directories = nullptr;
    };
}

HRESULT WatchProgramsTree(INameSpaceTreeControl2* control, const StartMenuRoots& sources)
{
    return ProgramsTreeUpdates::Attach(control, sources);
}

HRESULT HookStartMenuRefreshTimer(HMODULE explorer)
{
    auto timer = reinterpret_cast<BYTE*>(FindPattern(reinterpret_cast<uintptr_t>(explorer),
        "45 33 C9 45 33 C0 BA 0B 04 00 00 48 8B CF FF 15 ?? ?? ?? ?? EB 00 48 8B 44 24 60 4C 8B 16"));
    if (!timer)
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    BYTE* call = timer + 14;
    void* relay = _AllocatePageNearAddress(call);
    if (!relay)
        return E_OUTOFMEMORY;
    const intptr_t displacement = reinterpret_cast<intptr_t>(relay) - reinterpret_cast<intptr_t>(call + 6);
    const int32_t relative = static_cast<int32_t>(displacement);
    if (displacement != relative)
    {
        VirtualFree(relay, 0, MEM_RELEASE);
        return HRESULT_FROM_WIN32(ERROR_INVALID_ADDRESS);
    }
    DWORD relaySize = _WriteAbsoluteJump64(relay, reinterpret_cast<void*>(PostStartMenuRefresh));
    DWORD old = 0;
    if (!VirtualProtect(relay, relaySize, PAGE_EXECUTE_READ, &old) ||
        !FlushInstructionCache(GetCurrentProcess(), relay, relaySize))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        VirtualFree(relay, 0, MEM_RELEASE);
        return hr;
    }
    BYTE replacement[6] = { 0x90, 0xE8 };
    memcpy(replacement + 2, &relative, sizeof(relative));
    if (!VirtualProtect(call, sizeof(replacement), PAGE_EXECUTE_READWRITE, &old))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        VirtualFree(relay, 0, MEM_RELEASE);
        return hr;
    }
    memcpy(call, replacement, sizeof(replacement));
    DWORD ignored = 0;
    if (!VirtualProtect(call, sizeof(replacement), old, &ignored) ||
        !FlushInstructionCache(GetCurrentProcess(), call, sizeof(replacement)))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

void RefreshStartMenuHighlights(HWND notificationSource)
{
    HWND menu = GetAncestor(notificationSource, GA_ROOT);
    if (menu)
        RedrawWindow(menu, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

StartMenuNewItemsProc StartMenuHaveNewItemsOriginal = nullptr;

LRESULT __fastcall StartMenuHaveNewItemsHook(uintptr_t host, NMHDR* notification)
{
    LRESULT result = StartMenuHaveNewItemsOriginal(host, notification);
    // The paths are now installed in ByUsage. A node may have painted before they arrived.
    RefreshStartMenuHighlights(notification->hwndFrom);
    return result;
}
