#include "ThemeManager.h"
#include "dbgprint.h"
#include "pathcch.h"
#include "OSVersion.h"

struct UXTHEMEFILE
{
	char header[7]; // must be "thmfile"
	LPVOID sharableSectionView;
	HANDLE hSharableSection;
	LPVOID nsSectionView;
	HANDLE hNsSection;
	char end[3]; // must be "end"
};

typedef HRESULT(WINAPI *GetThemeDefaults_t)(
	LPCWSTR pszThemeFileName,
	LPWSTR  pszColorName,
	DWORD   dwColorNameLen,
	LPWSTR  pszSizeName,
	DWORD   dwSizeNameLen
);

typedef HRESULT(WINAPI *LoaderLoadTheme_t)(
	HANDLE      hThemeFile,
	HINSTANCE   hThemeLibrary,
	LPCWSTR     pszThemeFileName,
	LPCWSTR     pszColorParam,
	LPCWSTR     pszSizeParam,
	OUT HANDLE *hSharableSection,
	LPWSTR      pszSharableSectionName,
	int         cchSharableSectionName,
	OUT HANDLE *hNonsharableSection,
	LPWSTR      pszNonsharableSectionName,
	int         cchNonsharableSectionName,
	PVOID       pfnCustomLoadHandler,
	OUT HANDLE *hReuseSection,
	int         a,
	int         b,
	BOOL        fEmulateGlobal
);

typedef HTHEME(WINAPI *OpenThemeDataFromFile_t)(
	UXTHEMEFILE *lpThemeFile,
	HWND         hWnd,
	LPCWSTR      pszClassList,
	DWORD        dwFlags
);

static GetThemeDefaults_t GetThemeDefaults = 0;
static LoaderLoadTheme_t LoaderLoadTheme = 0;
static OpenThemeDataFromFile_t OpenThemeDataFromFile = 0;
static UXTHEMEFILE g_loadedTheme = {};
static bool g_hasLoadedTheme = false;
static HTHEME *g_themeHandles = NULL;
static int g_themeHandleCount = 0;
bool g_highContrastThemeActive = false;

static void FreeLoadedTheme()
{
	if (!g_hasLoadedTheme)
		return;

	if (g_loadedTheme.sharableSectionView)
		UnmapViewOfFile(g_loadedTheme.sharableSectionView);

	if (g_loadedTheme.nsSectionView)
		UnmapViewOfFile(g_loadedTheme.nsSectionView);

	if (g_loadedTheme.hNsSection)
		CloseHandle(g_loadedTheme.hNsSection);

	if (g_loadedTheme.hSharableSection)
		CloseHandle(g_loadedTheme.hSharableSection);

	ZeroMemory(&g_loadedTheme, sizeof(g_loadedTheme));
	g_hasLoadedTheme = false;
}

static const LPCWSTR c_szExplorerAdvancedSubkey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
static const LPCWSTR c_szExplorer7Theme = L"Theme";
static const LPCWSTR c_szExplorer7LastTheme = L"ThemeCache";

static bool IsExplorer7ThemeFileName(LPCWSTR fileName)
{
	return lstrcmpiW(fileName, L"aero.msstyles") == 0 ||
		lstrcmpiW(fileName, L"aerodark.msstyles") == 0 ||
		lstrcmpiW(fileName, L"aerolite.msstyles") == 0;
}

static bool ReadExplorer7LastTheme(LPWSTR fileName, DWORD cchFileName)
{
	DWORD cbFileName = cchFileName * sizeof(WCHAR);
	if (RegGetValueW(HKEY_CURRENT_USER, c_szExplorerAdvancedSubkey, c_szExplorer7LastTheme, RRF_RT_REG_SZ, NULL, fileName, &cbFileName) != ERROR_SUCCESS)
		return false;

	fileName[cchFileName - 1] = L'\0';
	return IsExplorer7ThemeFileName(fileName);
}

static void WriteExplorer7LastTheme(LPCWSTR fileName)
{
	RegSetKeyValueW(HKEY_CURRENT_USER, c_szExplorerAdvancedSubkey, c_szExplorer7LastTheme, REG_SZ, fileName, (DWORD)((lstrlenW(fileName) + 1) * sizeof(WCHAR)));
}

static bool ReadExplorer7ThemeOverride(LPWSTR theme, DWORD cchTheme)
{
	DWORD type = 0;
	DWORD cbTheme = cchTheme * sizeof(WCHAR);
	if (RegGetValueW(HKEY_CURRENT_USER, c_szExplorerAdvancedSubkey, c_szExplorer7Theme, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, theme, &cbTheme) != ERROR_SUCCESS)
		return false;

	theme[cchTheme - 1] = L'\0';
	if (!*theme)
		return false;

	if (type == REG_EXPAND_SZ)
	{
		WCHAR expandedTheme[MAX_PATH * 2];
		if (ExpandEnvironmentStringsW(theme, expandedTheme, ARRAYSIZE(expandedTheme)))
			StringCchCopyW(theme, cchTheme, expandedTheme);
	}

	return *theme != L'\0';
}



static void GetExplorer7ThemeFileName(LPWSTR fileName, DWORD cchFileName)
{
	WCHAR szCurrentTheme[MAX_PATH];
	if (SUCCEEDED(GetCurrentThemeName(szCurrentTheme, ARRAYSIZE(szCurrentTheme), NULL, 0, NULL, 0)))
	{
		if (StrStrIW(szCurrentTheme, L"AeroDark"))
			StringCchCopyW(fileName, cchFileName, L"aerodark.msstyles");
		else if (StrStrIW(szCurrentTheme, L"Aerolite"))
			StringCchCopyW(fileName, cchFileName, L"aerolite.msstyles");
		else
			StringCchCopyW(fileName, cchFileName, L"aero.msstyles");

		WriteExplorer7LastTheme(fileName);
		return;
	}

	if (ReadExplorer7LastTheme(fileName, cchFileName))
		return;

	StringCchCopyW(fileName, cchFileName, L"aero.msstyles");
}

static void GetExplorer7ThemePath(LPWSTR szThemePath, DWORD cchThemePath)
{
	if (ReadExplorer7ThemeOverride(szThemePath, cchThemePath))
	{
		if (StrChrW(szThemePath, L'\\') || StrChrW(szThemePath, L'/') || StrChrW(szThemePath, L':'))
			return;

		WCHAR szThemeFileName[MAX_PATH];
		StringCchCopyW(szThemeFileName, ARRAYSIZE(szThemeFileName), szThemePath);

		GetModuleFileNameW(NULL, szThemePath, cchThemePath);
		WCHAR *backslash = StrRChrW(szThemePath, NULL, L'\\');
		if (backslash && *backslash == L'\\')
			*backslash = L'\0';

		StringCchCatW(szThemePath, cchThemePath, L"\\Theme\\");
		StringCchCatW(szThemePath, cchThemePath, szThemeFileName);
		if (!*PathFindExtensionW(szThemePath))
			StringCchCatW(szThemePath, cchThemePath, L".msstyles");
		return;
	}

	// get directory of explorer.exe (NOT the working directory)
	GetModuleFileNameW(NULL, szThemePath, cchThemePath);
	WCHAR *backslash = StrRChrW(szThemePath, NULL, L'\\');
	if (backslash && *backslash == L'\\')
		*backslash = L'\0';

	StringCchCatW(szThemePath, cchThemePath, L"\\Theme\\");

	WCHAR szThemeFileName[MAX_PATH];
	GetExplorer7ThemeFileName(szThemeFileName, ARRAYSIZE(szThemeFileName));
	StringCchCatW(szThemePath, cchThemePath, szThemeFileName);
}


static HRESULT LoadThemeFile(wchar_t *Path)
{
	FreeLoadedTheme();

	WCHAR szColor[MAX_PATH];
	WCHAR szSize[MAX_PATH];

	HRESULT hr = GetThemeDefaults(
		Path,
		szColor,
		ARRAYSIZE(szColor),
		szSize,
		ARRAYSIZE(szSize)
	);
	if (FAILED(hr))
	{
		dbgprintf(L"LoadThemeFile failed to read defaults: %x", hr);
		return hr;
	}

	HANDLE hSharable = 0;
	HANDLE hNonSharable = 0;
	hr = LoaderLoadTheme(0LL, 0LL, Path, szColor, szSize, &hSharable, 0LL, 0, &hNonSharable, 0LL, 0, 0LL, 0LL, 0, 0, 0);
	if (FAILED(hr))
	{
		dbgprintf(L"LoadThemeFile failed to load theme: %x", hr);
		return hr;
	}

	memcpy(g_loadedTheme.header, "thmfile", 7);
	memcpy(g_loadedTheme.end, "end", 3);
	g_loadedTheme.sharableSectionView = MapViewOfFile(hSharable, FILE_MAP_READ, 0, 0, 0);
	g_loadedTheme.hSharableSection = hSharable;
	g_loadedTheme.nsSectionView = MapViewOfFile(hNonSharable, FILE_MAP_READ, 0, 0, 0);
	g_loadedTheme.hNsSection = hNonSharable;
	g_hasLoadedTheme = true;

	if (!g_loadedTheme.sharableSectionView || !g_loadedTheme.nsSectionView)
	{
		FreeLoadedTheme();
		return E_FAIL;
	}

	return S_OK;
}

bool IsHighContrastEnabled()
{
	HIGHCONTRASTW highContrast = { sizeof(highContrast) };
	return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, 0) &&
		(highContrast.dwFlags & HCF_HIGHCONTRASTON);
}

bool HasLoadedInactiveTheme()
{
	return g_hasLoadedTheme;
}

HTHEME OpenLoadedInactiveTheme(HWND hwnd, LPCWSTR pszClassList, DWORD dwFlags)
{
	if (!g_hasLoadedTheme || !OpenThemeDataFromFile)
		return NULL;

	HTHEME theme = OpenThemeDataFromFile(&g_loadedTheme, hwnd, pszClassList, dwFlags);
	if (theme)
	{
		void *newThemeHandles = realloc(g_themeHandles, sizeof(HTHEME) * (g_themeHandleCount + 1));
		if (newThemeHandles)
		{
			g_themeHandles = (HTHEME*)newThemeHandles;
			g_themeHandles[g_themeHandleCount++] = theme;
		}
	}

	return theme;
}

void CloseLoadedInactiveThemeHandles()
{
	for (int i = 0; i < g_themeHandleCount; ++i)
	{
		if (g_themeHandles[i])
			CloseThemeData(g_themeHandles[i]);
	}

	free(g_themeHandles);
	g_themeHandles = NULL;
	g_themeHandleCount = 0;
}

void ThemeManagerInitialize()
{
	//dont bother error checking, if u dont got uxtheme, ur system is prob already messed up and theres no saving u
	HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
	if (!hUxTheme)
	{
		FreeLoadedTheme();
		dbgprintf(L"uxtheme.dll unavailable");
		return;
	}
	GetThemeDefaults = (GetThemeDefaults_t)GetProcAddress(hUxTheme, (LPCSTR)7);
	LoaderLoadTheme = (LoaderLoadTheme_t)GetProcAddress(hUxTheme, (LPCSTR)92);
	OpenThemeDataFromFile = (OpenThemeDataFromFile_t)GetProcAddress(hUxTheme, (LPCSTR)16);

	dbgprintf(L"GetThemeDefaults %x LoaderLoadTheme %x OpenThemeDataFromFile %x\n", GetThemeDefaults, LoaderLoadTheme, OpenThemeDataFromFile);

	if (!GetThemeDefaults || !LoaderLoadTheme || !OpenThemeDataFromFile)
	{
		FreeLoadedTheme();
		dbgprintf(L"Inactive theme APIs unavailable");
		return;
	}

	g_highContrastThemeActive = IsHighContrastEnabled();
	if (g_highContrastThemeActive)
	{
		FreeLoadedTheme();
		dbgprintf(L"High contrast is enabled; inactive theme not loaded");
		return;
	}

	WCHAR szThemePath[MAX_PATH * 2] = {};
	GetExplorer7ThemePath(szThemePath, ARRAYSIZE(szThemePath));

	dbgprintf(L"theme path: %s", szThemePath);

	HRESULT hr = LoadThemeFile(szThemePath);
	if (FAILED(hr))
		dbgprintf(L"LOADTHEMEFILE FAILED %x\n", hr);
}

void ThemeManagerUninitialize()
{
	CloseLoadedInactiveThemeHandles();
	FreeLoadedTheme();
}
