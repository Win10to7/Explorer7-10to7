#include "ShellFolderMonitor.h"

namespace
{
	const WCHAR MonitorClass[] = L"Explorer7.ShellFolderMonitor";
	const UINT ChangeMessage = WM_APP + 0x3A1;
}

ShellFolderMonitor::~ShellFolderMonitor()
{
	Stop();
}

HRESULT ShellFolderMonitor::Start(PCIDLIST_ABSOLUTE const* roots, UINT count, Callback callback, void* context)
{
	if (!roots || !count || count > ARRAYSIZE(m_registrations) || !callback)
		return E_INVALIDARG;
	if (m_window)
		return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
	HMODULE module = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<PCWSTR>(WindowProc), &module))
		return HRESULT_FROM_WIN32(GetLastError());
	WNDCLASSW windowClass = {};
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = module;
	windowClass.lpszClassName = MonitorClass;
	if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return HRESULT_FROM_WIN32(GetLastError());
	m_callback = callback;
	m_context = context;
	m_window = CreateWindowExW(0, MonitorClass, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, module, this);
	if (!m_window)
		return HRESULT_FROM_WIN32(GetLastError());
	for (UINT i = 0; i < count; ++i)
	{
		SHChangeNotifyEntry entry = { roots[i], TRUE };
		m_registrations[i] = SHChangeNotifyRegister(m_window,
			SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_RecursiveInterrupt | SHCNRF_NewDelivery,
			SHCNE_DISKEVENTS, ChangeMessage, 1, &entry);
		if (!m_registrations[i])
		{
			DWORD error = GetLastError();
			Stop();
			return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
		}
	}
	return S_OK;
}

void ShellFolderMonitor::Unregister()
{
	m_callback = nullptr;
	m_context = nullptr;
	for (ULONG& registration : m_registrations)
	{
		if (registration)
		{
			SHChangeNotifyDeregister(registration);
			registration = 0;
		}
	}
}

void ShellFolderMonitor::Stop()
{
	Unregister();
	if (m_window)
		DestroyWindow(m_window);
}

LRESULT CALLBACK ShellFolderMonitor::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	ShellFolderMonitor* self = reinterpret_cast<ShellFolderMonitor*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE)
	{
		self = static_cast<ShellFolderMonitor*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
		self->m_window = window;
		SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	}
	if (self && message == ChangeMessage)
	{
		PIDLIST_ABSOLUTE* items = nullptr;
		LONG event = 0;
		HANDLE notification = SHChangeNotification_Lock(reinterpret_cast<HANDLE>(wParam),
			static_cast<DWORD>(lParam), &items, &event);
		if (notification)
		{
			if (self->m_callback)
				self->m_callback(self->m_context, event & ~SHCNE_INTERRUPT, items[0], items[1]);
			SHChangeNotification_Unlock(notification);
		}
		return 0;
	}
	if (self && message == WM_NCDESTROY)
	{
		self->Unregister();
		self->m_window = nullptr;
		SetWindowLongPtrW(window, GWLP_USERDATA, 0);
	}
	return DefWindowProcW(window, message, wParam, lParam);
}
