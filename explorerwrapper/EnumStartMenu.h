#pragma once
#include "common.h"
#include "startmenupin.h"
#include "userassist.h"

typedef struct { 
	LPITEMIDLIST pidlParent;	//+0	//+0
	LPITEMIDLIST pidlRelative;	//+4	//+8
	LPWSTR pszAppID;	//+8	//+10
	UEMINFO ueminfo;	//+Ch	//+18
	int iPinPos;		//+30h	//+3C
	DWORD_PTR fNewApp;	//+34h	//+40h
} STARTMENUITEM, *PSTARTMENUITEM;	//38h	//48h
static_assert(sizeof(STARTMENUITEM) == (sizeof(void*) == 8 ? 72 : 56), "Windows 7 Start menu item ABI");

MIDL_INTERFACE("00000000-0000-0000-0000-000000000000")
IEnumStartMenuItem: public IUnknown
{
public:
	// Windows 7 ByUsage clones the returned PIDLs/AppID; these fields remain enumerator-owned.
	STDMETHOD(Next)( ULONG celt, PSTARTMENUITEM rgelt, ULONG * pceltFetched ) PURE;
	STDMETHOD(Skip)( ULONG celt ) PURE;
	STDMETHOD(Reset)(void) PURE;
	STDMETHOD(Clone)(IEnumStartMenuItem**) PURE;
};

class CEnumStartMenu : public IEnumStartMenuItem
{
public:
	CEnumStartMenu();
	~CEnumStartMenu();

	//IUnknown
    STDMETHODIMP QueryInterface(REFIID riid,void **ppvObject);    
	STDMETHODIMP_(ULONG) AddRef( void);
	STDMETHODIMP_(ULONG) Release( void);

	//IEnumUnknown
	STDMETHODIMP Clone(IEnumStartMenuItem **ppenum);
	STDMETHODIMP Next(ULONG celt,PSTARTMENUITEM rgelt,ULONG *pceltFetched);
	STDMETHODIMP Reset();
	STDMETHODIMP Skip(ULONG celt);

	//our methods
	STDMETHODIMP AddItem(PSTARTMENUITEM item);
	STDMETHODIMP_(void) Sort();
	STDMETHODIMP_(void) RemoveDuplicates();
	STDMETHODIMP_(void) SetLimit(long limit);
private:
	long m_cRef;
	long m_count;
	long m_enumidx;
	long m_limit;
	HDSA hArrItems;
};

// Enumerates owned copies of full shortcut parsing paths.
class CEnumNewPrograms : public IEnumString
{
public:
	CEnumNewPrograms();
	~CEnumNewPrograms();

	STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	STDMETHODIMP Next(ULONG celt, LPOLESTR* rgelt, ULONG* pceltFetched);
	STDMETHODIMP Skip(ULONG celt);
	STDMETHODIMP Reset();
	STDMETHODIMP Clone(IEnumString** ppenum);

	HRESULT AddPath(PCWSTR path);

private:
	long m_cRef;
	ULONG m_index;
	HDPA m_paths;
};