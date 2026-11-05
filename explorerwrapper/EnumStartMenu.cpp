#include "EnumStartMenu.h"
#include "dbgprint.h"

int CALLBACK CleanStartMenuItem(void *p,void *pData)
{
	PSTARTMENUITEM item = (PSTARTMENUITEM)p;
	ILFree(item->pidlRelative);
	CoTaskMemFree(item->pszAppID);
	return 1;
}

CEnumStartMenu::CEnumStartMenu()
{
	m_cRef = 1;
	m_enumidx = 0;
	m_count = 0;
	m_limit = 1000;
	hArrItems = DSA_Create(sizeof(STARTMENUITEM),4);	
	dbgprintf(L"CEnumStartMenu::CEnumStartMenu\n");
}

CEnumStartMenu::~CEnumStartMenu()
{
	DSA_DestroyCallback(hArrItems,CleanStartMenuItem,NULL);
}

HRESULT STDMETHODCALLTYPE CEnumStartMenu::QueryInterface(REFIID riid,void **ppvObject)
{
	dbgprintf(L"CEnumStartMenu::QueryInterface NOT IMPLEMENTED\n");
	return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CEnumStartMenu::AddRef(void)
{
	return InterlockedIncrement(&m_cRef);
}

ULONG STDMETHODCALLTYPE CEnumStartMenu::Release(void)
{
	if (InterlockedDecrement(&m_cRef) == 0)
	{
		delete this;
		return 0;
	}
	return m_cRef;
}

HRESULT STDMETHODCALLTYPE CEnumStartMenu::Clone(IEnumStartMenuItem **ppenum)
{
	dbgprintf(L"CEnumStartMenu::Clone\n");
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CEnumStartMenu::Next(ULONG celt,PSTARTMENUITEM rgelt,ULONG *pceltFetched)
{
	if ( celt != 1 )
		return E_NOTIMPL;
	if ( m_enumidx >= m_count || m_enumidx >= m_limit )
	{
		if (pceltFetched) *pceltFetched = 0;
		return S_FALSE;
	}
	DSA_GetItem(hArrItems,m_enumidx,rgelt);
	// dbgprintf uses wvsprintfW, which does not consume floating-point arguments.
	DWORD scoreBits;
	CopyMemory(&scoreBits, &rgelt->ueminfo.R, sizeof(scoreBits));
	dbgprintf(L"Pos %d Rbits %08X FT %X_%X Item %s",rgelt->iPinPos,scoreBits,
		rgelt->ueminfo.ftExecute.dwHighDateTime,rgelt->ueminfo.ftExecute.dwLowDateTime,rgelt->pszAppID);
	m_enumidx++;
	if (pceltFetched) *pceltFetched = 1;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CEnumStartMenu::Reset()
{
	dbgprintf(L"CEnumStartMenu::Reset\n");
	m_enumidx = 0;
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CEnumStartMenu::Skip(ULONG celt)
{
	dbgprintf(L"CEnumStartMenu::Skip\n");
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CEnumStartMenu::AddItem(PSTARTMENUITEM item)
{
	if (!hArrItems || DSA_InsertItem(hArrItems, DSA_APPEND, item) == -1)
		return E_OUTOFMEMORY;
	++m_count;
	return S_OK;
}

int inline PosForCompare(int pos)
{
	if (pos == -2) return 1000;
	if (pos == -1) return 2000;
	return pos;
}

int CALLBACK CompareItems(void *p1,void *p2,LPARAM lParam)
{
	PSTARTMENUITEM item1 = (PSTARTMENUITEM)p1;
	PSTARTMENUITEM item2 = (PSTARTMENUITEM)p2;
	int pinpos1 = PosForCompare(item1->iPinPos);
	int pinpos2 = PosForCompare(item2->iPinPos);
	int compare = pinpos1 - pinpos2; //pin pos - ascending
	if (compare == 0)
		compare = (item2->ueminfo.R > item1->ueminfo.R) - (item2->ueminfo.R < item1->ueminfo.R); //R - descending
	if (compare == 0)
		compare = CompareFileTime(&item2->ueminfo.ftExecute,&item1->ueminfo.ftExecute); //filetime - descending
	return compare;
}

void STDMETHODCALLTYPE CEnumStartMenu::Sort()
{
	DSA_Sort(hArrItems,CompareItems,0);
}

void STDMETHODCALLTYPE CEnumStartMenu::RemoveDuplicates()
{
	int i1 = 0;
	PSTARTMENUITEM item1;
	PSTARTMENUITEM item2;
	while (i1 < m_count && (item1 = (PSTARTMENUITEM)DSA_GetItemPtr(hArrItems, i1)))
	{		
		int i2 = i1+1;
		BOOL duplicate = FALSE;
		if (item1->iPinPos == -1)
		{
			while (i2 < m_count && (item2 = (PSTARTMENUITEM)DSA_GetItemPtr(hArrItems, i2)))
			{
				if (lstrcmp(item1->pszAppID,item2->pszAppID) == 0)
				{
					duplicate = TRUE;
					break;					
				}
				i2++;
			}
		}
		if (duplicate)
		{
			CleanStartMenuItem(item2,NULL);
			DSA_DeleteItem(hArrItems,i2);
			m_count--;
		}
		else
		i1++;
	}
}

void STDMETHODCALLTYPE CEnumStartMenu::SetLimit(long limit)
{
	m_limit = limit;
}

static int CALLBACK FreeNewProgramPath(void* path, void*)
{
	CoTaskMemFree(path);
	return 1;
}

static PWSTR CopyNewProgramPath(PCWSTR path)
{
	SIZE_T length = static_cast<SIZE_T>(lstrlenW(path)) + 1;
	if (length > static_cast<SIZE_T>(-1) / sizeof(WCHAR))
		return nullptr;
	PWSTR copy = static_cast<PWSTR>(CoTaskMemAlloc(length * sizeof(WCHAR)));
	if (copy)
		CopyMemory(copy, path, length * sizeof(WCHAR));
	return copy;
}

CEnumNewPrograms::CEnumNewPrograms() : m_cRef(1), m_index(0), m_paths(nullptr)
{
}

CEnumNewPrograms::~CEnumNewPrograms()
{
	if (m_paths)
		DPA_DestroyCallback(m_paths, FreeNewProgramPath, nullptr);
}

HRESULT STDMETHODCALLTYPE CEnumNewPrograms::QueryInterface(REFIID riid, void** ppvObject)
{
	if (!ppvObject)
		return E_POINTER;
	*ppvObject = nullptr;
	if (riid == IID_IUnknown || riid == IID_IEnumString)
	{
		*ppvObject = static_cast<IEnumString*>(this);
		AddRef();
		return S_OK;
	}
	return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CEnumNewPrograms::AddRef()
{
	return InterlockedIncrement(&m_cRef);
}

ULONG STDMETHODCALLTYPE CEnumNewPrograms::Release()
{
	ULONG count = InterlockedDecrement(&m_cRef);
	if (!count)
		delete this;
	return count;
}

HRESULT CEnumNewPrograms::AddPath(PCWSTR path)
{
	if (!path)
		return E_INVALIDARG;
	if (!m_paths)
		m_paths = DPA_Create(8);
	if (!m_paths)
		return E_OUTOFMEMORY;
	PWSTR copy = CopyNewProgramPath(path);
	if (!copy)
		return E_OUTOFMEMORY;
	if (DPA_InsertPtr(m_paths, DPA_APPEND, copy) == -1)
	{
		CoTaskMemFree(copy);
		return E_OUTOFMEMORY;
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CEnumNewPrograms::Next(ULONG celt, LPOLESTR* rgelt, ULONG* pceltFetched)
{
	if (!pceltFetched && celt != 1)
		return E_POINTER;
	if (celt && !rgelt)
		return E_POINTER;
	if (pceltFetched)
		*pceltFetched = 0;
	if (!celt)
		return S_OK;

	ULONG count = m_paths ? static_cast<ULONG>(DPA_GetPtrCount(m_paths)) : 0;
	ULONG fetched = 0;
	while (fetched < celt && fetched < count - m_index)
	{
		rgelt[fetched] = CopyNewProgramPath(static_cast<PCWSTR>(DPA_GetPtr(m_paths, m_index + fetched)));
		if (!rgelt[fetched])
		{
			while (fetched)
			{
				--fetched;
				CoTaskMemFree(rgelt[fetched]);
				rgelt[fetched] = nullptr;
			}
			return E_OUTOFMEMORY;
		}
		++fetched;
	}
	m_index += fetched;
	if (pceltFetched)
		*pceltFetched = fetched;
	return fetched == celt ? S_OK : S_FALSE;
}

HRESULT STDMETHODCALLTYPE CEnumNewPrograms::Skip(ULONG celt)
{
	ULONG count = m_paths ? static_cast<ULONG>(DPA_GetPtrCount(m_paths)) : 0;
	ULONG remaining = count - m_index;
	if (celt > remaining)
	{
		m_index = count;
		return S_FALSE;
	}
	m_index += celt;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CEnumNewPrograms::Reset()
{
	m_index = 0;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CEnumNewPrograms::Clone(IEnumString** ppenum)
{
	if (!ppenum)
		return E_POINTER;
	*ppenum = nullptr;
	CEnumNewPrograms* copy = new CEnumNewPrograms;
	if (!copy)
		return E_OUTOFMEMORY;
	HRESULT hr = S_OK;
	int count = m_paths ? DPA_GetPtrCount(m_paths) : 0;
	for (int i = 0; SUCCEEDED(hr) && i < count; ++i)
		hr = copy->AddPath(static_cast<PCWSTR>(DPA_GetPtr(m_paths, i)));
	if (SUCCEEDED(hr))
	{
		copy->m_index = m_index;
		*ppenum = copy;
	}
	else
		copy->Release();
	return hr;
}