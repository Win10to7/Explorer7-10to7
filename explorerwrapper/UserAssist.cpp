#include "UserAssist.h"

IUserAssist* g_UserAssist;

extern "C" HRESULT WINAPI Explorer_CoCreateInstance(
	__in   REFCLSID rclsid,
	__in   LPUNKNOWN pUnkOuter,
	__in   DWORD dwClsContext,
	__in   REFIID riid,
	__out  LPVOID * ppv
);

static HRESULT EnsureUserAssist()
{
	if (!g_UserAssist)
	{
		HRESULT result = Explorer_CoCreateInstance(CLSID_UserAssist, NULL,
			CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_NO_CODE_DOWNLOAD,
			IID_IUserAssist7, (PVOID*)&g_UserAssist);
		if (FAILED(result))
			return result;
		if (!g_UserAssist)
			return E_NOINTERFACE;
	}
	return S_OK;
}

HRESULT WINAPI UAQueryEntry(REFIID iid, LPWSTR parsingname, PUEMINFO uem)
{
	if (!uem)
		return E_POINTER;
	*uem = UEMINFO();
	uem->cbSize = sizeof(UEMINFO);
	uem->dwMask = 0x33;
	HRESULT hr = EnsureUserAssist();
	return SUCCEEDED(hr) ? g_UserAssist->QueryEntry(iid, parsingname, uem) : hr;
}

HRESULT WINAPI UAQueryApplication(LPWSTR appId, PUEMINFO uem)
{
	return UAQueryEntry(UAIID_APPLICATIONS, appId, uem);
}

HRESULT WINAPI UARegisterNotify(UANOTIFYPROC callback, void* context, BOOL add)
{
	HRESULT hr = EnsureUserAssist();
	return SUCCEEDED(hr) ? g_UserAssist->RegisterNotify(callback, context, add) : hr;
}
