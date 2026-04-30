#include "TrayNotify.h"
#include <Shlwapi.h>
#include "dbgprint.h"

const LPWSTR sz_TrayNotify = L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\TrayNotify";
const LPWSTR sz_TrayNotify7 = L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\TrayNotify7";

static BOOL verchecked;

extern "C" IStream* WINAPI SHOpenRegStream2WNEW(
  _In_      HKEY hkey,
  _In_opt_  LPCWSTR pszSubkey,
  _In_opt_  LPCWSTR pszValue,
  _In_      DWORD grfMode
)
{
	if (lstrcmp(pszSubkey,sz_TrayNotify) == 0)
	{		
		pszSubkey = sz_TrayNotify7;
		//wipe cache
		if (!verchecked)
		{
			WCHAR ourpath[MAX_PATH];
			WCHAR regpath[MAX_PATH];
			LONG regsz = MAX_PATH;
			GetModuleFileName(NULL,ourpath,MAX_PATH);
			if ( RegQueryValue(hkey,pszSubkey,regpath,&regsz) != ERROR_SUCCESS || lstrcmpi(ourpath,regpath) != 0 )
			{
				RegDeleteKey(hkey,pszSubkey);
				RegSetValue(hkey,pszSubkey,REG_SZ,ourpath,lstrlen(ourpath));
				dbgprintf(L"wiped traynotify cache");
			}
			verchecked = TRUE;
		}
	}
	return SHOpenRegStream2(hkey,pszSubkey,pszValue,grfMode);
}

/*CTRAYNOTIFICATIONCALLBACK*/

CTrayNotificationCallback::CTrayNotificationCallback(INotificationCB* callback)
{
	m_cRef = 1;
	m_callback = callback;
	m_forwardCallbacks = TRUE;
}

CTrayNotificationCallback::~CTrayNotificationCallback()
{
	if (m_callback)
		m_callback->Release();
}

HRESULT STDMETHODCALLTYPE CTrayNotificationCallback::QueryInterface(REFIID riid,void **ppvObject)
{
	if ( !ppvObject ) return E_POINTER;
	*ppvObject = NULL;

	if (riid == IID_IUnknown)
	{
		*ppvObject = static_cast<INotificationCB*>(this);
		AddRef();
		return S_OK;
	}
	if (riid == __uuidof(INotificationCB))
	{
		*ppvObject = static_cast<INotificationCB*>(this);
		AddRef();
		return S_OK;
	}
	return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CTrayNotificationCallback::AddRef(void)
{
	return InterlockedIncrement(&m_cRef);
}

ULONG STDMETHODCALLTYPE CTrayNotificationCallback::Release(void)
{
	if (InterlockedDecrement(&m_cRef) == 0)
	{
		delete this;
		return 0;
	}
	return m_cRef;
}

HRESULT STDMETHODCALLTYPE CTrayNotificationCallback::Notify(DWORD dwMessage, NOTIFYITEM* pNotifyItem)
{
	if (!m_forwardCallbacks || !m_callback || !pNotifyItem || !pNotifyItem->hWnd)
		return S_OK;

	return m_callback->Notify(dwMessage,pNotifyItem);
}

void CTrayNotificationCallback::StopForwarding()
{
	m_forwardCallbacks = FALSE;
}


/*CTRAYNOTIFYFACTORY*/

CTrayNotifyFactory::CTrayNotifyFactory(IClassFactory* origfactory)
{
	m_cRef = 1;
	m_origfactory = origfactory;
}

CTrayNotifyFactory::~CTrayNotifyFactory()
{
	m_origfactory->Release();
}

HRESULT STDMETHODCALLTYPE CTrayNotifyFactory::QueryInterface(REFIID riid,void **ppvObject)
{
	if (riid == IID_IUnknown)
	{
		*ppvObject = static_cast<IUnknown*>(this);
		AddRef();
		return S_OK;
	}
	if (riid == IID_IClassFactory)
	{
		*ppvObject = static_cast<IClassFactory*>(this);
		AddRef();
		return S_OK;
	}
	return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CTrayNotifyFactory::AddRef(void)
{
	return InterlockedIncrement(&m_cRef);
}

ULONG STDMETHODCALLTYPE CTrayNotifyFactory::Release(void)
{
	if (InterlockedDecrement(&m_cRef) == 0)
	{
		m_origfactory->Release();
		free((void*)this);
		return 0;
	}
	return m_cRef;
}

HRESULT STDMETHODCALLTYPE CTrayNotifyFactory::CreateInstance( IUnknown * pUnkOuter, REFIID riid, void ** ppvObject )
{
	if ( ppvObject ) *ppvObject = NULL;
	if ( !ppvObject ) return E_POINTER;
	if ( pUnkOuter ) return CLASS_E_NOAGGREGATION;

	IUnknown* obj;
	HRESULT ret = m_origfactory->CreateInstance(pUnkOuter,IID_IUnknown,(PVOID*)&obj);
	if (FAILED(ret)) return ret;

	ITrayNotify7* oldnotify;
	ret = obj->QueryInterface(IID_ITrayNotify7,(PVOID*)&oldnotify);
	obj->Release();
	if (FAILED(ret)) return ret;

	CTrayNotifyWrapper* wrapper = new CTrayNotifyWrapper(oldnotify);
	ret = wrapper->QueryInterface(riid,ppvObject);
	wrapper->Release();
	return ret;
}

HRESULT STDMETHODCALLTYPE CTrayNotifyFactory::LockServer( BOOL fLock )
{
	return m_origfactory->LockServer(fLock);
}

/*CTRAYNOTIFYWRAPPER*/

CTrayNotifyWrapper::CTrayNotifyWrapper(ITrayNotify7* notify7)
{
	m_cRef = 1;
	m_notify7 = notify7;
	m_callback = NULL;
	m_marshaler = NULL;
	CoCreateFreeThreadedMarshaler(static_cast<ITrayNotify8*>(this), &m_marshaler);
}

CTrayNotifyWrapper::~CTrayNotifyWrapper()
{
	if (m_callback)
	{
		m_notify7->RegisterCallback(NULL);
		m_callback->Release();
	}
	if (m_marshaler)
		m_marshaler->Release();
	m_notify7->Release();
}

HRESULT STDMETHODCALLTYPE CTrayNotifyWrapper::QueryInterface(REFIID riid,void **ppvObject)
{
	if ( !ppvObject ) return E_POINTER;
	*ppvObject = NULL;

	if (riid == IID_IUnknown)
	{
		*ppvObject = static_cast<ITrayNotify8*>(this);
		AddRef();
		return S_OK;
	}
	if (riid == IID_ITrayNotify7)
	{
		*ppvObject = static_cast<ITrayNotify7*>(this);
		AddRef();
		return S_OK;
	}
	if (riid == IID_ITrayNotify8)
	{
		*ppvObject = static_cast<ITrayNotify8*>(this);
		AddRef();
		return S_OK;
	}
	if (riid == IID_IMarshal && m_marshaler)
	{
		return m_marshaler->QueryInterface(riid,ppvObject);
	}
	return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CTrayNotifyWrapper::AddRef(void)
{
	return InterlockedIncrement(&m_cRef);
}

ULONG STDMETHODCALLTYPE CTrayNotifyWrapper::Release(void)
{
	if (InterlockedDecrement(&m_cRef) == 0)
	{
		delete this;
		return 0;
	}
	return m_cRef;
}

HRESULT STDMETHODCALLTYPE CTrayNotifyWrapper::RegisterCallback(IUnknown* p1)
{
	return m_notify7->RegisterCallback(p1);
}

HRESULT STDMETHODCALLTYPE CTrayNotifyWrapper::RegisterCallback(IUnknown* p1,ULONG* p2)
{
	if (p2) *p2 = 0;

	if (m_callback)
	{
		m_notify7->RegisterCallback(NULL);
		m_callback->Release();
		m_callback = NULL;
	}

	if (!p1)
		return S_OK;

	INotificationCB* clientCallback;
	HRESULT ret = p1->QueryInterface(__uuidof(INotificationCB),(void**)&clientCallback);
	if (FAILED(ret))
		return ret;

	CTrayNotificationCallback* callback = new CTrayNotificationCallback(clientCallback);
	ret = m_notify7->RegisterCallback(callback);
	callback->StopForwarding();
	if (FAILED(ret))
	{
		callback->Release();
		return ret;
	}

	m_callback = callback;
	if (p2) *p2 = 1;
	return ret;
}

HRESULT STDMETHODCALLTYPE CTrayNotifyWrapper::UnregisterCallback(ULONG*)
{
	HRESULT ret = m_notify7->RegisterCallback(NULL);
	if (m_callback)
	{
		m_callback->Release();
		m_callback = NULL;
	}
	return ret;
}
	
HRESULT STDMETHODCALLTYPE CTrayNotifyWrapper::SetPreference(const NOTIFYITEM* p1)
{
	return m_notify7->SetPreference(p1);
}

HRESULT STDMETHODCALLTYPE CTrayNotifyWrapper::EnableAutoTray(int p1)
{
	return m_notify7->EnableAutoTray(p1);
}

HRESULT STDMETHODCALLTYPE CTrayNotifyWrapper::DoAction(BOOL)
{
	dbgprintf(L"DOACTION");
	return E_NOTIMPL;
}

HRESULT __stdcall CTrayNotifyWrapper::SetWindowingEnvironmentConfig(IUnknown*)
{
	dbgprintf(L"SetWindowingEnvironmentConfig");
	return E_NOTIMPL;
}
