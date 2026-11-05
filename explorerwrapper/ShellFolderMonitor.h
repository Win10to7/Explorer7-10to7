#pragma once
#include "common.h"

// Thread-bound Shell subscriptions. Start and Stop run on the window/message-loop owner.
// Event PIDLs are borrowed until the callback returns.
class ShellFolderMonitor
{
public:
	using Callback = void (*)(void*, LONG, PCIDLIST_ABSOLUTE, PCIDLIST_ABSOLUTE);
	ShellFolderMonitor() = default;
	~ShellFolderMonitor();
	ShellFolderMonitor(const ShellFolderMonitor&) = delete;
	ShellFolderMonitor& operator=(const ShellFolderMonitor&) = delete;

	HRESULT Start(PCIDLIST_ABSOLUTE const* roots, UINT count, Callback callback, void* context);
	void Stop();
	bool Active() const { return m_window != nullptr; }

private:
	static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
	void Unregister();
	HWND m_window = nullptr;
	ULONG m_registrations[8] = {};
	Callback m_callback = nullptr;
	void* m_context = nullptr;
};
