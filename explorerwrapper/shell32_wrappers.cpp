#include "common.h"
#include "AutoPlay.h"
#include "dbgprint.h"
#include "OSVersion.h"
#include "shell32_wrappers.h"
#include "MinHook.h"
#include "KnownFolders.h"
#include "RegistryManager.h"

typedef PVOID (WINAPI *ResolveDelayLoadedAPIAPI)(PVOID ParentModuleBase, PVOID DelayloadDescriptor, PVOID FailureDllHook, PVOID FailureSystemHook,PIMAGE_THUNK_DATA ThunkAddress,ULONG Flags);
static ResolveDelayLoadedAPIAPI ResolveDelayLoadedAPI;
static PVOID CoCreateInstanceBase;
static PVOID SHGetValueWSHCore;

//remove pintostart verb
LSTATUS WINAPI SHGetValueNEW(
  _In_         HKEY hkey,
  _In_opt_     LPCWSTR pszSubKey,
  _In_opt_     LPCWSTR pszValue,
  _Out_opt_    LPDWORD pdwType,
  _Out_opt_    LPVOID pvData,
  _Inout_opt_  LPDWORD pcbData
)
{	
	WCHAR buf[100];
	DWORD bufsize = sizeof(buf);
	if ( lstrcmp(pszValue,L"LegacyDisable") == 0 )
	if ( (RegQueryValueEx(hkey,L"MUIVerb",NULL,NULL,(LPBYTE)buf,&bufsize) == ERROR_SUCCESS) && (lstrcmpi(buf,L"@shell32.dll,-51201") == 0) ) return ERROR_SUCCESS;
	return SHGetValueW(hkey,pszSubKey,pszValue,pdwType,pvData,pcbData);
}

bool(__fastcall* IsSearchEnabled)();
extern "C" bool WINAPI IsSearchEnabledNEW()
{
	//dbgprintf(L"IsSearchEnabledNEW\n");
	return 1;
}

PVOID WINAPI ResolveDelayLoadedAPINEW(PVOID ParentModuleBase, PVOID DelayloadDescriptor, PVOID FailureDllHook, PVOID FailureSystemHook,PIMAGE_THUNK_DATA ThunkAddress,ULONG Flags)
{
	dbgprintf(L"ResolveDelayLoadedAPINEW\n");
	PVOID retfunc = ResolveDelayLoadedAPI(ParentModuleBase,DelayloadDescriptor,FailureDllHook,FailureSystemHook,ThunkAddress,Flags);
	if (retfunc == CoCreateInstanceBase)
	{
		retfunc = Shell32_CoCreateInstance;
		ThunkAddress->u1.Function = (DWORD_PTR)retfunc;
	}
	if (retfunc == SHGetValueWSHCore)
	{
		retfunc = SHGetValueNEW;
		ThunkAddress->u1.Function = (DWORD_PTR)retfunc;
	}
	return retfunc;
}

BOOL __stdcall ILIsEqualNEW(LPCITEMIDLIST pidl1, LPCITEMIDLIST pidl2)
{
	dbgprintf(L"ILIsEqualNEW\n");
	IShellFolder* ppshf = 0;
	HRESULT v4 = SHGetDesktopFolder(&ppshf);
	if (v4 >= 0)
	{
		v4 = ppshf->CompareIDs(0x10000000i64, pidl1, pidl2);
		ppshf->Release();
	}
	return v4 == 0;
}

HRESULT __stdcall SHEvaluateSystemCommandTemplateNEW(PCWSTR pszCmdTemplate, PWSTR* ppszApplication, PWSTR* ppszCommandLine, PWSTR* ppszParameters)
{
	dbgprintf(L"SHEvaluateSystemCommandTemplateNEW\n");
	return S_OK;
	//return SHEvaluateSystemCommandTemplateWithOptions((unsigned __int16*)pszCmdTemplate, ppszParameters);
}

HRESULT(__stdcall* Shell32_DllGetClassObject)(REFCLSID rclsid, const IID* const riid, LPVOID* ppv);
HRESULT __stdcall Shell32_DllGetClassObject_Hook(REFCLSID rclsid, const IID* const riid, LPVOID* ppv)
{
	if (!IsProgramsFolderClass(rclsid))
		return Shell32_DllGetClassObject(rclsid, riid, ppv);
	CProgramsFolderClassFactory* factory = new CProgramsFolderClassFactory(rclsid, s_ShowStoreAppsInStart);
	if (!factory)
		return E_OUTOFMEMORY;
	HRESULT result = factory->QueryInterface(*riid, ppv);
	factory->Release();
	return result;
}

void HookShell32()
{
	HMODULE shell32 = LoadLibrary(L"shell32.dll");

	dbgprintf(L"1\n");
	ResolveDelayLoadedAPI = (ResolveDelayLoadedAPIAPI)GetProcAddress(GetModuleHandle(L"kernel32.dll"),"ResolveDelayLoadedAPI");
	ChangeImportedAddress(GetModuleHandle(L"shell32.dll"), "API-MS-WIN-CORE-DELAYLOAD-L1-1-1.DLL", ResolveDelayLoadedAPI, ResolveDelayLoadedAPINEW);
	//ResolveDelayLoadedAPI = (ResolveDelayLoadedAPIAPI)GetProcAddress(GetModuleHandle(L"api-ms-win-core-delayload-l1-1-1.dll"),"ResolveDelayLoadedAPI");
	dbgprintf(L"%i\n",(unsigned long long)ResolveDelayLoadedAPI);
	dbgprintf(L"2\n");
	CoCreateInstanceBase = GetProcAddress(GetModuleHandle(L"combase.dll"),"CoCreateInstance");
	Shell32_DllGetClassObject = (decltype(Shell32_DllGetClassObject))GetProcAddress(GetModuleHandle(L"shell32.dll"),"DllGetClassObject");
	MH_CreateHook(Shell32_DllGetClassObject, Shell32_DllGetClassObject_Hook,(LPVOID*)&Shell32_DllGetClassObject);
	dbgprintf(L"3\n");

	SHGetValueWSHCore = GetProcAddress(LoadLibrary(L"shcore.dll"),"SHGetValueW");

	dbgprintf(L"5\n");
	//auto ordinal902 = GetProcAddress(LoadLibrary(L"shell32.dll"),(LPSTR)902);
	//ChangeImportedAddress(LoadLibrary(L"shell32.dll"),"shlwapi.DLL", GetProcAddress(LoadLibrary(L"shlwapi.dll"), "SHAboutInfo"), SHAboutInfoWNEW);
	ChangeImportedAddress(GetModuleHandle(0),"shell32.DLL", GetProcAddress(shell32, (LPSTR)902), IsSearchEnabledNEW);

	//ChangeImportedAddress(GetModuleHandle(0),"shell32.DLL", GetProcAddress(LoadLibrary(L"shell32.DLL"), (LPSTR)719), SHParseDarwinIDFromCacheWNew);

	//todo: evaluate if this is needed
	ChangeImportedAddress(GetModuleHandle(0),"shell32.DLL", GetProcAddress(shell32, "ILIsEqual"), ILIsEqualNEW);

	uintptr_t Win32PinCheck = FindPattern((uintptr_t)shell32, "41 8B E9 49 8B F0 48 8B DA 48 8B F9 48 8D 0D ?? ?? ?? ?? E8");
	if (g_osVersion.BuildNumber() >= 19041)
	{
		if (Win32PinCheck)
		{
			dbgprintf(L"Win32PinCheck %i", Win32PinCheck);
			Win32PinCheck += 19;
			uint8_t* bytes = (uint8_t*)(Win32PinCheck + 5 + *reinterpret_cast<int32_t*>(Win32PinCheck + 1));
			DWORD old;
			VirtualProtect(bytes, 3, PAGE_EXECUTE_READWRITE, &old);
			bytes[0] = 0xB0;
			bytes[1] = 0x00;
			bytes[2] = 0xC3;
			VirtualProtect(bytes, 3, old, 0);
		}
		else // Microsoft added a new signature for this in later 23H2 updates
		{
			uintptr_t Win32PinCheck2 = FindPattern((uintptr_t)shell32, "45 8B F1 49 8B F0 48 8B DA 48 8B F9 48 8D 0D ?? ?? ?? ?? E8");

			if (Win32PinCheck2)
			{
				dbgprintf(L"Win32PinCheck2 %i", Win32PinCheck2);
				Win32PinCheck2 += 19;
				uint8_t* bytes = (uint8_t*)(Win32PinCheck2 + 5 + *reinterpret_cast<int32_t*>(Win32PinCheck2 + 1));
				DWORD old;
				VirtualProtect(bytes, 3, PAGE_EXECUTE_READWRITE, &old);
				bytes[0] = 0xB0;
				bytes[1] = 0x00;
				bytes[2] = 0xC3;
				VirtualProtect(bytes, 3, old, 0);
			}
		}
	}
	
	dbgprintf(L"6\n");
}


HRESULT WINAPI Shell32_CoCreateInstance(
	__in   REFCLSID rclsid,
	__in   LPUNKNOWN pUnkOuter,
	__in   DWORD dwClsContext,
	__in   REFIID riid,
	__out  LPVOID* ppv
)
{
	if (IsProgramsFolderClass(rclsid))
	{
		CProgramsFolderClassFactory factory(rclsid, s_ShowStoreAppsInStart);
		return factory.CreateInstance(pUnkOuter, riid, ppv);
	}
	HRESULT result = CoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, ppv);

	
	if (result == S_OK && rclsid == CLSID_AutoPlayUI)
	{
		*ppv = new CAutoPlayWrapper((IAutoPlayUI*)*ppv);
	}

	return result;
}


