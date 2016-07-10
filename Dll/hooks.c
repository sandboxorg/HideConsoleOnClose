#include "../Shared/stdafx.h"
#include "../Shared/api.h"
#include "../Shared/trace.h"
#include "dll.h"

#define ConsoleWindowClass L"ConsoleWindowClass"

DWORD WINAPI FindConhostUIThreadId(HWND ConsoleWindow);

BOOL WINAPI IsConsoleWindow(HWND hWnd)
{
	HideConsoleTrace(
		L"IsConsoleWindow: hWnd=%1!p!",
		hWnd
	);

	WCHAR ClassName[20];
	INT32 ClassNameCch = GetClassNameW(hWnd, ClassName, ARRAYSIZE(ClassName));

	if (ClassNameCch == 0)
	{
		HideConsoleTraceLastError(L"IsConsoleWindow: GetClassNameW");
		return FALSE;
	}

	if (ClassNameCch != ARRAYSIZE(ConsoleWindowClass) - 1)
	{
		HideConsoleTrace(
			L"IsConsoleWindow: hWnd=%1!p! ClassName=%2 Result=FALSE",
			hWnd,
			ClassName
		);

		return FALSE;
	}

	INT32 CompareResult = CompareStringW(
		LOCALE_INVARIANT,
		0,
		ClassName,
		ClassNameCch,
		ConsoleWindowClass,
		ARRAYSIZE(ConsoleWindowClass) - 1
	);

	HideConsoleTrace(
		L"IsConsoleWindow: hWnd=%1!p! ClassName=%2 Result=%3",
		hWnd,
		ClassName,
		(CompareResult == CSTR_EQUAL) ? L"TRUE" : L"FALSE"
	);

	return (CompareResult == CSTR_EQUAL);
}

LRESULT CALLBACK HookCbt(INT32 nCode, WPARAM wParam, LPARAM lParam)
{
	if ((nCode != HCBT_SYSCOMMAND) || (wParam != SC_CLOSE))
	{
		goto NextHook;
	}

	HideConsoleTrace(L"HookCbt: HCBT_SYSCOMMAND SC_CLOSE");

	HWND ConsoleWindowBeingClosed = TlsGetValue(
		g_TlsIndex
	);

	HideConsoleTrace(
		L"HookCbt: ConsoleWindowBeingClosed=%1!p!",
		ConsoleWindowBeingClosed
	);

	if (!ConsoleWindowBeingClosed)
	{
		goto NextHook;
	}

	ShowWindow(ConsoleWindowBeingClosed, SW_HIDE);

	HideConsoleTrace(L"HookCbt: Result=1");
	return 1; // prevent action

NextHook:

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK HookWndProc(INT32 Code, WPARAM wParam, LPARAM lParam)
{
	if (Code != HC_ACTION)
	{
		goto NextHook;
	}

	LPCWPSTRUCT Msg = (LPCWPSTRUCT)lParam;

	if ((Msg->message != WM_SYSCOMMAND) || (Msg->wParam != SC_CLOSE))
	{
		goto NextHook;
	}

	HideConsoleTrace(
		L"HookWndProc: WM_SYSCOMMAND SC_CLOSE hWnd=%1!p!",
		Msg->hwnd
	);

	if (!IsConsoleWindow(Msg->hwnd))
	{
		goto NextHook;
	}

	if (!TlsSetValue(g_TlsIndex, Msg->hwnd))
	{
		HideConsoleTraceLastError(L"HookWndProc: TlsSetValue");
		goto NextHook;
	}

NextHook:

	return CallNextHookEx(NULL, Code, wParam, lParam);
}

LRESULT CALLBACK HookWndProcRet(INT32 Code, WPARAM wParam, LPARAM lParam)
{
	if (Code != HC_ACTION)
	{
		goto NextHook;
	}

	LPCWPRETSTRUCT Msg = (LPCWPRETSTRUCT)lParam;

	if ((Msg->message != WM_SYSCOMMAND) || (Msg->wParam != SC_CLOSE))
	{
		goto NextHook;
	}

	HideConsoleTrace(
		L"HookWndProcRet: WM_SYSCOMMAND SC_CLOSE hWnd=%1!p!",
		Msg->hwnd
	);

	HWND ConsoleWindowBeingClosed = TlsGetValue(g_TlsIndex);

	HideConsoleTrace(
		L"HookWndProcRet: ConsoleWindowBeingClosed=%1!p!",
		ConsoleWindowBeingClosed
	);

	if (ConsoleWindowBeingClosed != Msg->hwnd)
	{
		goto NextHook;
	}

	if (!TlsSetValue(g_TlsIndex, NULL))
	{
		HideConsoleTraceLastError(L"HookWndProcRet: TlsSetValue");
		goto NextHook;
	}

NextHook:

	return CallNextHookEx(NULL, Code, wParam, lParam);
}

LRESULT CALLBACK HookGetMessage(INT32 Code, WPARAM wParam, LPARAM lParam)
{
	if (Code != HC_ACTION)
	{
		goto NextHook;
	}

	LPMSG Msg = (LPMSG)lParam;

	if ((Msg->message != WM_SYSCOMMAND) || (Msg->wParam != SC_CLOSE))
	{
		goto NextHook;
	}

	HideConsoleTrace(
		L"HookGetMessage: WM_SYSCOMMAND SC_CLOSE hWnd=%1!p!",
		Msg->hwnd
	);

	if (!IsConsoleWindow(Msg->hwnd))
	{
		goto NextHook;
	}

	Msg->message = WM_NULL;
	ShowWindow(Msg->hwnd, SW_HIDE);

	HideConsoleTrace(
		L"HookGetMessage: WM_SYSCOMMAND changed into WM_NULL"
	);

NextHook:

	return CallNextHookEx(NULL, Code, wParam, lParam);

}

//
// Unregisters the wait for conhost's UI thread, unhooks the hooks,
// and frees the bookkeeping structure.
//
// Must not be called from the registered wait callback, would deadlock.
//
BOOL WINAPI CleanupHideConsole(PHIDE_CONSOLE HideConsole, PBOOL WasLastHook)
{
	HideConsoleTrace(
		L"CleanupHideConsole: HideConsole=%1!p! WasLastHook=%2!p!",
		HideConsole,
		WasLastHook
	);

	if (!HideConsole)
		return FALSE;

	LONG HookCount = InterlockedDecrement(&g_HookCount);

	HideConsoleTrace(
		L"CleanupHideConsole: HookCount=%1!i!",
		HookCount
	);

	if (WasLastHook)
	{
		*WasLastHook = (HookCount == 0) ? TRUE : FALSE;
	}

	BOOL Result = TRUE;

	if (HideConsole->ConhostThreadHandle)
	{
		if (!CloseHandle(HideConsole->ConhostThreadHandle))
		{
			HideConsoleTraceLastError(
				L"CleanupHideConsole: CloseHandle"
			);

			Result = FALSE;
		}
	}

	if (HideConsole->GetMessageHook)
	{
		if (!UnhookWindowsHookEx(HideConsole->GetMessageHook) &&
			GetLastError() != ERROR_SUCCESS)
		{
			HideConsoleTraceLastError(
				L"CleanupHideConsole: UnhookWindowsHookEx(GetMessageHook)"
			);

			Result = FALSE;
		}
	}

	if (HideConsole->WndProcRetHook)
	{
		if (!UnhookWindowsHookEx(HideConsole->WndProcRetHook) &&
			GetLastError() != ERROR_SUCCESS)
		{
			HideConsoleTraceLastError(
				L"CleanupHideConsole: UnhookWindowsHookEx(WndProcRetHook)"
			);

			Result = FALSE;
		}
	}

	if (HideConsole->WndProcHook)
	{
		if (!UnhookWindowsHookEx(HideConsole->WndProcHook) &&
			GetLastError() != ERROR_SUCCESS)
		{
			HideConsoleTraceLastError(
				L"CleanupHideConsole: UnhookWindowsHookEx(WndProcHook)"
			);

			Result = FALSE;
		}
	}

	if (HideConsole->CbtHook)
	{
		if (!UnhookWindowsHookEx(HideConsole->CbtHook) &&
			GetLastError() != ERROR_SUCCESS)
		{
			HideConsoleTraceLastError(
				L"CleanupHideConsole: UnhookWindowsHookEx(CbtHook)"
			);

			Result = FALSE;
		}
	}

	if (!HeapFree(GetProcessHeap(), 0, HideConsole))
	{
		HideConsoleTraceLastError(
			L"CleanupHideConsole: HeapFree"
		);

		Result = FALSE;
	}

	return Result;
}

PHIDE_CONSOLE WINAPI SetupHideConsole(HWND hWnd)
{
	HideConsoleTrace(
		L"SetupHideConsole: hWnd=%1!p!",
		hWnd
	);

	if (!hWnd)
	{
		return NULL;
	}

	DWORD ThreadId = FindConhostUIThreadId(hWnd);

	if (!ThreadId)
	{
		HideConsoleTrace(
			L"SetupHideConsole: Thread for hWnd 0x%1!p! could not be found",
			hWnd
		);

		return NULL;
	}

	PHIDE_CONSOLE Result = HeapAlloc(
		GetProcessHeap(),
		HEAP_ZERO_MEMORY,
		sizeof(HIDE_CONSOLE)
	);

	if (!Result)
	{
		HideConsoleTraceLastError(L"SetupHideConsole: HeapAlloc");
		return NULL;
	}

	// We've allocated memory and need to cleanup after this point, so let's
	// increment the global hook count.

	LONG HookCount = InterlockedIncrement(&g_HookCount);

	HideConsoleTrace(
		L"SetupHideConsole: HookCount=%1!i!",
		HookCount
	);

	Result->ConhostThreadHandle = OpenThread(SYNCHRONIZE, FALSE, ThreadId);

	if (!Result->ConhostThreadHandle)
	{
		HideConsoleTraceLastError(
			L"SetupHideConsole: OpenThread"
		);

		goto Cleanup;
	}

	Result->CbtHook = SetWindowsHookExW(
		WH_CBT,
		HookCbt,
		g_ModuleHandle,
		ThreadId
	);

	if (!Result->CbtHook)
	{
		HideConsoleTraceLastError(
			L"SetupHideConsole: SetWindowsHookExW(WH_CBT)"
		);

		goto Cleanup;
	}

	Result->WndProcHook = SetWindowsHookExW(
		WH_CALLWNDPROC,
		HookWndProc,
		g_ModuleHandle,
		ThreadId
	);

	if (!Result->WndProcHook)
	{
		HideConsoleTraceLastError(
			L"SetupHideConsole: SetWindowsHookExW(WH_CALLWNDPROC)"
		);

		goto Cleanup;
	}

	Result->WndProcRetHook = SetWindowsHookExW(
		WH_CALLWNDPROCRET,
		HookWndProcRet,
		g_ModuleHandle,
		ThreadId
	);

	if (!Result->WndProcRetHook)
	{
		HideConsoleTraceLastError(
			L"SetupHideConsole: SetWindowsHookExW(WH_CALLWNDPROCRET)"
		);

		goto Cleanup;
	}

	Result->GetMessageHook = SetWindowsHookExW(
		WH_GETMESSAGE,
		HookGetMessage,
		g_ModuleHandle,
		ThreadId
	);

	if (!Result->GetMessageHook)
	{
		HideConsoleTraceLastError(
			L"SetupHideConsole: SetWindowsHookExW(WH_GETMESSAGE)"
		);

		goto Cleanup;
	}

	return Result;

Cleanup:

	CleanupHideConsole(Result, NULL);
	return NULL;
}
