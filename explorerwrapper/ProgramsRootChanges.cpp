#include "ProgramsRootChanges.h"
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{
    struct Entry
    {
        PITEMID_CHILD pidl;
        BOOL folder;
        int ordinal;
    };

    int CALLBACK FreeEntry(void* value, void*)
    {
        CoTaskMemFree(static_cast<Entry*>(value)->pidl);
        return 1;
    }

    class Snapshot
    {
    public:
        ~Snapshot()
        {
            if (items)
                DSA_DestroyCallback(items, FreeEntry, nullptr);
        }
        Entry* At(int index) const { return static_cast<Entry*>(DSA_GetItemPtr(items, index)); }
        HDSA Detach() { HDSA result = items; items = nullptr; return result; }
        HDSA items = nullptr;
        int count = 0;
    };

    HRESULT Compare(IShellFolder* folder, const Entry& first, const Entry& second, int* order)
    {
        HRESULT hr = folder->CompareIDs(SHCIDS_CANONICALONLY, first.pidl, second.pidl);
        if (SUCCEEDED(hr))
            *order = static_cast<short>(HRESULT_CODE(hr));
        return hr;
    }

    struct SortContext
    {
        IShellFolder* folder;
        HRESULT error;
    };

    int CALLBACK SortEntries(void* first, void* second, LPARAM parameter)
    {
        SortContext* context = reinterpret_cast<SortContext*>(parameter);
        Entry& left = *static_cast<Entry*>(first);
        Entry& right = *static_cast<Entry*>(second);
        int order = 0;
        HRESULT hr = Compare(context->folder, left, right, &order);
        if (FAILED(hr))
            context->error = hr;
        // Keep native namespace precedence for canonical duplicates.
        return order ? order : left.ordinal - right.ordinal;
    }

    HRESULT Read(IShellFolder* folder, Snapshot* snapshot)
    {
        snapshot->items = DSA_Create(sizeof(Entry), 32);
        if (!snapshot->items)
            return E_OUTOFMEMORY;
        ComPtr<IEnumIDList> children;
        HRESULT hr = folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &children);
        if (hr == S_FALSE)
            return S_OK;
        if (FAILED(hr))
            return hr;
        PITEMID_CHILD child = nullptr;
        while ((hr = children->Next(1, &child, nullptr)) == S_OK)
        {
            SFGAOF attributes = SFGAO_FOLDER;
            PCUITEMID_CHILD ids[] = { child };
            HRESULT result = folder->GetAttributesOf(1, ids, &attributes);
            if (SUCCEEDED(result))
            {
                Entry entry = { child, (attributes & SFGAO_FOLDER) != 0, snapshot->count };
                if (DSA_InsertItem(snapshot->items, DSA_APPEND, &entry) == -1)
                    result = E_OUTOFMEMORY;
                else
                {
                    ++snapshot->count;
                    child = nullptr;
                }
            }
            CoTaskMemFree(child);
            child = nullptr;
            if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) &&
                result != HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
                return result;
        }
        if (FAILED(hr))
            return hr;
        SortContext context = { folder, S_OK };
        if (snapshot->count > 1 && !DSA_Sort(snapshot->items, SortEntries, reinterpret_cast<LPARAM>(&context)))
            return E_FAIL;
        if (FAILED(context.error))
            return context.error;
        int kept = 0;
        for (int i = 0; i < snapshot->count; ++i)
        {
            Entry* entry = snapshot->At(i);
            int order = 1;
            if (kept)
            {
                hr = Compare(folder, *snapshot->At(kept - 1), *entry, &order);
                if (FAILED(hr))
                    return hr;
            }
            if (!order)
            {
                CoTaskMemFree(entry->pidl);
                entry->pidl = nullptr;
            }
            else
            {
                if (kept != i)
                {
                    *snapshot->At(kept) = *entry;
                    entry->pidl = nullptr;
                }
                ++kept;
            }
        }
        snapshot->count = kept;
        return S_OK;
    }

    HRESULT Send(PCIDLIST_ABSOLUTE root, const Entry& item, LONG event)
    {
        PIDLIST_ABSOLUTE absolute = ILCombine(root, item.pidl);
        if (!absolute)
            return E_OUTOFMEMORY;
        SHChangeNotify(event, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, absolute, nullptr);
        CoTaskMemFree(absolute);
        return S_OK;
    }

    bool SameData(const Entry& first, const Entry& second)
    {
        UINT size = ILGetSize(first.pidl);
        return first.folder == second.folder && size == ILGetSize(second.pidl) &&
            !memcmp(first.pidl, second.pidl, size);
    }
}

ProgramsRootChanges::~ProgramsRootChanges()
{
    if (m_items)
        DSA_DestroyCallback(m_items, FreeEntry, nullptr);
}


HRESULT ProgramsRootChanges::Refresh(IShellFolder* folder, PCIDLIST_ABSOLUTE root)
{
    Snapshot current;
    HRESULT hr = Read(folder, &current);
    if (FAILED(hr))
        return hr;
    int oldIndex = 0;
    int newIndex = 0;
    while (SUCCEEDED(hr) && (oldIndex < m_count || newIndex < current.count))
    {
        Entry* oldItem = oldIndex < m_count ? static_cast<Entry*>(DSA_GetItemPtr(m_items, oldIndex)) : nullptr;
        Entry* newItem = newIndex < current.count ? current.At(newIndex) : nullptr;
        int order = oldItem ? -1 : 1;
        if (oldItem && newItem)
        {
            hr = Compare(folder, *oldItem, *newItem, &order);
            if (FAILED(hr))
                break;
        }
        if (order < 0)
        {
            hr = Send(root, *oldItem, oldItem->folder ? SHCNE_RMDIR : SHCNE_DELETE);
            ++oldIndex;
        }
        else if (order > 0)
        {
            hr = Send(root, *newItem, newItem->folder ? SHCNE_MKDIR : SHCNE_CREATE);
            ++newIndex;
        }
        else
        {
            if (!SameData(*oldItem, *newItem))
                hr = Send(root, *newItem, SHCNE_UPDATEITEM);
            ++oldIndex;
            ++newIndex;
        }
    }
    if (SUCCEEDED(hr))
    {
        if (m_items)
            DSA_DestroyCallback(m_items, FreeEntry, nullptr);
        m_count = current.count;
        m_items = current.Detach();
    }
    return hr;
}
