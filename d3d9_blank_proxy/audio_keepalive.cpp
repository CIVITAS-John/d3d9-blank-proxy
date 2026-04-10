#include "audio_keepalive.h"

#include <windows.h>
#include <commctrl.h>

#include "minhook/include/MinHook.h"

#pragma comment(lib, "comctl32.lib")

// File-scope state. All access happens from the game's main thread (the
// thread that calls Present), so no synchronization is needed beyond the
// idempotence guard.
static HWND g_civHwnd = nullptr;
static bool g_installed = false;

// MinHook trampolines for the three user32 focus-detection APIs. We never
// actually call them - the detours unconditionally return g_civHwnd - but
// MinHook requires an out parameter for MH_CreateHookApi.
static decltype(&GetForegroundWindow) g_origGetForegroundWindow = nullptr;
static decltype(&GetActiveWindow)     g_origGetActiveWindow     = nullptr;
static decltype(&GetFocus)            g_origGetFocus            = nullptr;

static const UINT_PTR kSubclassId = 0xA5D10BEE; // "audio-bee"

// --- Detours ---------------------------------------------------------------

static HWND WINAPI Hook_GetForegroundWindow()
{
	return g_civHwnd;
}

static HWND WINAPI Hook_GetActiveWindow()
{
	return g_civHwnd;
}

static HWND WINAPI Hook_GetFocus()
{
	return g_civHwnd;
}

// --- Window subclass -------------------------------------------------------

static LRESULT CALLBACK CivSubclassProc(HWND hWnd, UINT msg,
	WPARAM wParam, LPARAM lParam, UINT_PTR /*idSubclass*/, DWORD_PTR /*refData*/)
{
	switch (msg)
	{
	case WM_ACTIVATEAPP:
		// Tell the game it's always the active app.
		wParam = TRUE;
		break;

	case WM_ACTIVATE:
		// Force LOWORD(wParam) to WA_ACTIVE while preserving the minimized
		// flag in HIWORD.
		wParam = MAKEWPARAM(WA_ACTIVE, HIWORD(wParam));
		break;

	case WM_NCACTIVATE:
		// Tell the non-client area to render as active.
		wParam = TRUE;
		break;

	case WM_KILLFOCUS:
		// Swallow focus-loss entirely. The game will still receive paint
		// and input messages normally.
		return 0;

	default:
		break;
	}

	return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// --- Public API ------------------------------------------------------------

void AudioKeepalive_Install(HWND hwnd)
{
	if (g_installed)
	{
		OutputDebugStringA("AudioKeepalive: already installed\n");
		return;
	}

	if (hwnd == nullptr)
	{
		OutputDebugStringA("AudioKeepalive: install called with null HWND\n");
		return;
	}

	g_civHwnd = hwnd;

	MH_STATUS st = MH_Initialize();
	if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED)
	{
		char buf[128];
		wsprintfA(buf, "AudioKeepalive: MH_Initialize failed: %s\n",
			MH_StatusToString(st));
		OutputDebugStringA(buf);
		return;
	}

	st = MH_CreateHookApi(L"user32", "GetForegroundWindow",
		(LPVOID)&Hook_GetForegroundWindow, (LPVOID*)&g_origGetForegroundWindow);
	if (st != MH_OK)
	{
		char buf[128];
		wsprintfA(buf, "AudioKeepalive: hook GetForegroundWindow failed: %s\n",
			MH_StatusToString(st));
		OutputDebugStringA(buf);
	}

	st = MH_CreateHookApi(L"user32", "GetActiveWindow",
		(LPVOID)&Hook_GetActiveWindow, (LPVOID*)&g_origGetActiveWindow);
	if (st != MH_OK)
	{
		char buf[128];
		wsprintfA(buf, "AudioKeepalive: hook GetActiveWindow failed: %s\n",
			MH_StatusToString(st));
		OutputDebugStringA(buf);
	}

	st = MH_CreateHookApi(L"user32", "GetFocus",
		(LPVOID)&Hook_GetFocus, (LPVOID*)&g_origGetFocus);
	if (st != MH_OK)
	{
		char buf[128];
		wsprintfA(buf, "AudioKeepalive: hook GetFocus failed: %s\n",
			MH_StatusToString(st));
		OutputDebugStringA(buf);
	}

	st = MH_EnableHook(MH_ALL_HOOKS);
	if (st != MH_OK)
	{
		char buf[128];
		wsprintfA(buf, "AudioKeepalive: MH_EnableHook failed: %s\n",
			MH_StatusToString(st));
		OutputDebugStringA(buf);
	}

	if (!SetWindowSubclass(hwnd, CivSubclassProc, kSubclassId, 0))
	{
		OutputDebugStringA("AudioKeepalive: SetWindowSubclass failed\n");
	}

	g_installed = true;
	OutputDebugStringA("AudioKeepalive: installed\n");
}

void AudioKeepalive_Uninstall()
{
	if (!g_installed)
	{
		return;
	}

	if (g_civHwnd != nullptr)
	{
		RemoveWindowSubclass(g_civHwnd, CivSubclassProc, kSubclassId);
	}

	// MH_Uninitialize disables and removes every hook MinHook owns, so we
	// don't need per-target MH_RemoveHook calls (and couldn't make them
	// easily anyway - &GetForegroundWindow is the IAT thunk, not the
	// user32 export address that MinHook actually patched).
	MH_DisableHook(MH_ALL_HOOKS);
	MH_Uninitialize();

	g_origGetForegroundWindow = nullptr;
	g_origGetActiveWindow     = nullptr;
	g_origGetFocus            = nullptr;

	g_installed = false;
	g_civHwnd = nullptr;
	OutputDebugStringA("AudioKeepalive: uninstalled\n");
}
