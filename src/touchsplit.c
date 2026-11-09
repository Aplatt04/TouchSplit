// TouchSplit - use a touchscreen and a mouse independently on Windows.
//
// What it does
//  * Touch input no longer "steals" the mouse pointer: after each touch the
//    pointer goes straight back to where your mouse left it.
//  * Touch input no longer steals keyboard focus from your game/app: windows on
//    the touch screen are made non-activating, and if focus does get pulled
//    across anyway it is handed back as soon as the finger lifts.
//
// How it works
//  * A low-level mouse hook (WH_MOUSE_LL) sees every mouse event. Mouse events
//    that Windows synthesises from touch/pen carry the MI_WP_SIGNATURE in
//    dwExtraInfo, so real-mouse and touch traffic can be told apart.
//    The hook only reads flags and returns - it adds no measurable latency.
//  * Raw Input on the HID digitizer usage page is used as a second touch signal.
//  * WS_EX_NOACTIVATE is added to top-level windows on the touch monitor (and
//    removed again when they leave it or when TouchSplit exits).
//
// Build: see Makefile (Linux/macOS cross-compile) or build.bat (Windows + MinGW-w64).
//
// SPDX-License-Identifier: MIT

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

#define APP_NAME        L"TouchSplit"
#include "version.h"
#define APP_VERSION     TS_VERSION_W
#define WM_TRAY         (WM_APP + 1)
#define WM_TS_RESTORE   (WM_APP + 2)
#define TIMER_SESSION   1
#define TIMER_SCAN      2
#define HOTKEY_TOGGLE   1

#define IDM_ENABLE      100
#define IDM_CURSOR      101
#define IDM_FOCUS       102
#define IDM_NOACT       103
#define IDM_STARTUP     104
#define IDM_REDETECT    105
#define IDM_FOLDER      106
#define IDM_EXIT        107
#define IDM_INFO        108

#define MI_WP_SIGNATURE 0xFF515700u
#define SIGNATURE_MASK  0xFFFFFF00u

#define MAX_TRACKED 512

typedef struct {
    DWORD    displayOrientation;
    HANDLE   device;
    int      pointerDeviceType;   // 1 = integrated pen, 2 = external pen, 3 = touch, 4 = touchpad
    HMONITOR monitor;
    ULONG    startingCursorId;
    USHORT   maxActiveContacts;
    WCHAR    productString[520];
} TS_POINTER_DEVICE_INFO;
typedef BOOL (WINAPI *PFN_GetPointerDevices)(UINT32 *, TS_POINTER_DEVICE_INFO *);
typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(HANDLE);

typedef struct {
    int  enabled;
    int  restoreCursor;
    int  restoreFocus;
    int  noActivate;
    int  releaseMs;
    int  logSessions;
    WCHAR touchMonitor[64];   // "auto" or e.g. \\.\DISPLAY3
} Config;

static struct {
    HINSTANCE hInst;
    HWND      hwnd;
    HHOOK     mouseHook;
    HWINEVENTHOOK fgHook;
    UINT      msgTaskbarCreated;
    Config    cfg;
    WCHAR     dir[MAX_PATH];
    WCHAR     iniPath[MAX_PATH];
    WCHAR     logPath[MAX_PATH];
    WCHAR     exePath[MAX_PATH];

    HMONITOR  touchMon;
    WCHAR     touchMonName[160];
    int       touchMonLearned;   // learned from a touch rather than detected

    // session state (all touched only on the UI thread: hooks run on it too)
    BOOL      sessionActive;
    BOOL      touchBtnDown;
    DWORD     lastTouchTick;
    DWORD     sessionStartTick;
    DWORD     sessionEndTick;
    POINT     lastMousePos;
    POINT     savedPos;
    HWND      lastRealFg;
    HWND      savedFg;
    unsigned  sessionCount;

    HWND      tracked[MAX_TRACKED];
    int       trackedCount;
    BOOL      rawTouchOk;
} g;

// ------------------------------------------------------------------ logging
static void Log(const WCHAR *fmt, ...)
{
    FILE *f = _wfopen(g.logPath, L"a, ccs=UTF-8");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fwprintf(f, L"%04d-%02d-%02d %02d:%02d:%02d.%03d  ", st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfwprintf(f, fmt, ap); va_end(ap);
    fputwc(L'\n', f);
    fclose(f);
}

// ------------------------------------------------------------------ config
static void LoadConfig(void)
{
    Config *c = &g.cfg;
    c->enabled       = GetPrivateProfileIntW(L"TouchSplit", L"Enabled", 1, g.iniPath);
    c->restoreCursor = GetPrivateProfileIntW(L"TouchSplit", L"RestoreCursor", 1, g.iniPath);
    c->restoreFocus  = GetPrivateProfileIntW(L"TouchSplit", L"RestoreFocus", 1, g.iniPath);
    c->noActivate    = GetPrivateProfileIntW(L"TouchSplit", L"NoActivateOnTouchScreen", 1, g.iniPath);
    c->releaseMs     = GetPrivateProfileIntW(L"TouchSplit", L"ReleaseDelayMs", 80, g.iniPath);
    c->logSessions   = GetPrivateProfileIntW(L"TouchSplit", L"LogTouches", 1, g.iniPath);
    GetPrivateProfileStringW(L"TouchSplit", L"TouchMonitor", L"auto", c->touchMonitor, 64, g.iniPath);
    if (c->releaseMs < 20) c->releaseMs = 20;
    if (c->releaseMs > 2000) c->releaseMs = 2000;
}

static void WriteInt(const WCHAR *key, int v)
{
    WCHAR b[16]; swprintf(b, 16, L"%d", v);
    WritePrivateProfileStringW(L"TouchSplit", key, b, g.iniPath);
}

static void SaveConfig(void)
{
    WriteInt(L"Enabled", g.cfg.enabled);
    WriteInt(L"RestoreCursor", g.cfg.restoreCursor);
    WriteInt(L"RestoreFocus", g.cfg.restoreFocus);
    WriteInt(L"NoActivateOnTouchScreen", g.cfg.noActivate);
    WriteInt(L"ReleaseDelayMs", g.cfg.releaseMs);
    WriteInt(L"LogTouches", g.cfg.logSessions);
    WritePrivateProfileStringW(L"TouchSplit", L"TouchMonitor", g.cfg.touchMonitor, g.iniPath);
}

// ------------------------------------------------------------------ monitors
static void DescribeMonitor(HMONITOR mon, WCHAR *out, int cch)
{
    MONITORINFOEXW mi; mi.cbSize = sizeof(mi);
    out[0] = 0;
    if (!mon || !GetMonitorInfoW(mon, (MONITORINFO *)&mi)) { wcsncpy(out, L"(none)", cch); return; }
    DISPLAY_DEVICEW dd; dd.cb = sizeof(dd);
    WCHAR friendly[128] = L"";
    if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, 0)) wcsncpy(friendly, dd.DeviceString, 127);
    swprintf(out, cch, L"%ls %ldx%ld %ls", mi.szDevice,
             mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, friendly);
}

typedef struct { const WCHAR *name; HMONITOR found; } FindMonCtx;
static BOOL CALLBACK FindMonProc(HMONITOR m, HDC dc, LPRECT r, LPARAM lp)
{
    (void)dc; (void)r;
    FindMonCtx *c = (FindMonCtx *)lp;
    MONITORINFOEXW mi; mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(m, (MONITORINFO *)&mi) && _wcsicmp(mi.szDevice, c->name) == 0) { c->found = m; return FALSE; }
    return TRUE;
}

static void DetectTouchMonitor(void)
{
    g.touchMon = NULL;
    g.touchMonLearned = 0;
    if (_wcsicmp(g.cfg.touchMonitor, L"auto") != 0 && g.cfg.touchMonitor[0]) {
        FindMonCtx c = { g.cfg.touchMonitor, NULL };
        EnumDisplayMonitors(NULL, NULL, FindMonProc, (LPARAM)&c);
        g.touchMon = c.found;
        Log(L"Touch monitor forced by config: %ls -> %ls", g.cfg.touchMonitor, c.found ? L"found" : L"NOT FOUND");
    }
    if (!g.touchMon) {
        PFN_GetPointerDevices pGPD = (PFN_GetPointerDevices)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetPointerDevices");
        if (pGPD) {
            UINT32 n = 0;
            if (pGPD(&n, NULL) && n > 0) {
                TS_POINTER_DEVICE_INFO *d = (TS_POINTER_DEVICE_INFO *)calloc(n, sizeof(*d));
                if (d && pGPD(&n, d)) {
                    for (UINT32 i = 0; i < n; i++) {
                        WCHAR desc[160]; DescribeMonitor(d[i].monitor, desc, 160);
                        Log(L"Pointer device %u: type=%d contacts=%u monitor=%ls product=\"%ls\"",
                            i, d[i].pointerDeviceType, d[i].maxActiveContacts, desc, d[i].productString);
                        if (!g.touchMon && d[i].pointerDeviceType == 3 && d[i].monitor) g.touchMon = d[i].monitor;
                    }
                    if (!g.touchMon)
                        for (UINT32 i = 0; i < n; i++)
                            if ((d[i].pointerDeviceType == 1 || d[i].pointerDeviceType == 2) && d[i].monitor) { g.touchMon = d[i].monitor; break; }
                }
                free(d);
            } else Log(L"GetPointerDevices: no pointer devices reported");
        }
    }
    DescribeMonitor(g.touchMon, g.touchMonName, 160);
    Log(L"Touch monitor: %ls", g.touchMon ? g.touchMonName : L"not detected yet (will learn it from the first touch)");
}

static BOOL OnTouchMonitor(HWND h)
{
    return g.touchMon && h && MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST) == g.touchMon;
}

static BOOL PointOnTouchMonitor(POINT p)
{
    return g.touchMon && MonitorFromPoint(p, MONITOR_DEFAULTTONULL) == g.touchMon;
}

// ------------------------------------------------------------------ focus
static BOOL IsShellWindow(HWND h)
{
    WCHAR cls[64];
    if (!h || h == g.hwnd) return TRUE;
    if (!GetClassNameW(h, cls, 64)) return TRUE;
    return !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd") ||
           !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") ||
           !wcscmp(cls, L"NotifyIconOverflowWindow") || !wcscmp(cls, L"TaskListThumbnailWnd") ||
           !wcscmp(cls, L"Windows.UI.Core.CoreWindow") || !wcscmp(cls, L"XamlExplorerHostIslandWindow") ||
           !wcscmp(cls, L"ForegroundStaging") || !wcscmp(cls, L"MultitaskingViewFrame") ||
           !wcscmp(cls, L"TopLevelWindowForOverflowXamlIsland");
}

static void ForceForeground(HWND h)
{
    if (!h || !IsWindow(h)) return;
    HWND fg = GetForegroundWindow();
    if (fg == h) return;
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
    DWORD me = GetCurrentThreadId();
    DWORD fgT = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    BOOL attached = FALSE;
    if (fgT && fgT != me) attached = AttachThreadInput(me, fgT, TRUE);
    BringWindowToTop(h);
    SetForegroundWindow(h);
    if (attached) AttachThreadInput(me, fgT, FALSE);
    if (GetForegroundWindow() != h) {
        // Last resort: a synthetic ALT tap unlocks SetForegroundWindow for this process.
        INPUT in[2]; ZeroMemory(in, sizeof(in));
        in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_MENU;
        in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = VK_MENU; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
        SetForegroundWindow(h);
    }
}

static void RestoreFocusIfStolen(void)
{
    if (!g.cfg.restoreFocus) return;
    HWND fg = GetForegroundWindow();
    if (!g.savedFg || !IsWindow(g.savedFg) || fg == g.savedFg) return;
    if (fg && !OnTouchMonitor(fg)) return;    // focus went somewhere else on purpose
    if (OnTouchMonitor(g.savedFg)) return;    // the app you were using IS on the touch screen
    ForceForeground(g.savedFg);
    if (g.cfg.logSessions && g.sessionCount <= 300) Log(L"  focus handed back to %p", (void *)g.savedFg);
}

// ------------------------------------------------------------------ sessions
static void StartSession(void)
{
    POINT cur; GetCursorPos(&cur);
    g.sessionActive = TRUE;
    g.touchBtnDown = FALSE;
    g.sessionStartTick = GetTickCount();
    // The LL hook runs before the cursor moves, so if the pointer is still off
    // the touch screen right now, this is the most accurate "where the mouse was".
    g.savedPos = (g.touchMon && !PointOnTouchMonitor(cur)) ? cur : g.lastMousePos;
    g.savedFg = g.lastRealFg;
    g.sessionCount++;
    SetTimer(g.hwnd, TIMER_SESSION, 15, NULL);
}

static void EndSession(void)
{
    if (!g.sessionActive) return;
    KillTimer(g.hwnd, TIMER_SESSION);
    g.sessionActive = FALSE;
    g.touchBtnDown = FALSE;
    g.sessionEndTick = GetTickCount();
    if (g.cfg.restoreCursor) {
        POINT cur; GetCursorPos(&cur);
        if (cur.x != g.savedPos.x || cur.y != g.savedPos.y) SetCursorPos(g.savedPos.x, g.savedPos.y);
    }
    g.lastMousePos = g.savedPos;
    RestoreFocusIfStolen();
    if (g.cfg.logSessions && g.sessionCount <= 300)
        Log(L"touch #%u: %lu ms, pointer back to %ld,%ld", g.sessionCount,
            g.sessionEndTick - g.sessionStartTick, g.savedPos.x, g.savedPos.y);
}

static void NoteTouch(POINT *where)
{
    if (where && !g.touchMon) {
        g.touchMon = MonitorFromPoint(*where, MONITOR_DEFAULTTONEAREST);
        g.touchMonLearned = 1;
        DescribeMonitor(g.touchMon, g.touchMonName, 160);
        Log(L"Touch monitor learned from first touch: %ls", g.touchMonName);
    }
    if (!g.sessionActive) StartSession();
    g.lastTouchTick = GetTickCount();
}

// ------------------------------------------------------------------ hooks
static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wp, LPARAM lp)
{
    if (nCode == HC_ACTION && g.cfg.enabled) {
        MSLLHOOKSTRUCT *m = (MSLLHOOKSTRUCT *)lp;
        if (((DWORD)m->dwExtraInfo & SIGNATURE_MASK) == MI_WP_SIGNATURE) {
            // Mouse event synthesised from touch or pen: let it through so the
            // touch screen app gets its click, but remember this is a touch.
            NoteTouch(&m->pt);
            switch (wp) {
            case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: g.touchBtnDown = TRUE; break;
            case WM_LBUTTONUP:   case WM_RBUTTONUP:   case WM_MBUTTONUP:   g.touchBtnDown = FALSE; break;
            }
        } else if (g.sessionActive) {
            // Real mouse used while the pointer is still parked on the touch screen.
            if (!g.touchBtnDown) {
                if (wp == WM_MOUSEMOVE) {
                    // swallow this one move and snap back first, so movement resumes
                    // from where the mouse was rather than from the touch point
                    PostMessageW(g.hwnd, WM_TS_RESTORE, 0, 0);
                    return 1;
                }
                EndSession();   // a click: put the pointer back before it lands
            }
        } else {
            g.lastMousePos = m->pt;
        }
    }
    return CallNextHookEx(NULL, nCode, wp, lp);
}

static void CALLBACK ForegroundProc(HWINEVENTHOOK h, DWORD ev, HWND hwnd, LONG idObj, LONG idChild, DWORD th, DWORD t)
{
    (void)h; (void)idObj; (void)idChild; (void)th; (void)t;
    if (ev != EVENT_SYSTEM_FOREGROUND || !hwnd) return;
    DWORD now = GetTickCount();
    BOOL touchRecent = g.sessionActive || (now - g.sessionEndTick < 500);
    if (g.cfg.enabled && touchRecent && OnTouchMonitor(hwnd)) {
        // focus was pulled onto the touch screen by a touch
        if (!g.sessionActive) RestoreFocusIfStolen();
        return;
    }
    if (!IsShellWindow(hwnd)) g.lastRealFg = hwnd;
}

// ------------------------------------------------------------------ no-activate
static void Untrack(int i, BOOL clearStyle)
{
    HWND h = g.tracked[i];
    if (clearStyle && IsWindow(h)) {
        LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
        if (ex & WS_EX_NOACTIVATE) SetWindowLongPtrW(h, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_NOACTIVATE);
    }
    g.tracked[i] = g.tracked[--g.trackedCount];
}

static void ClearAllNoActivate(void)
{
    while (g.trackedCount) Untrack(g.trackedCount - 1, TRUE);
}

static BOOL CALLBACK ScanProc(HWND h, LPARAM lp)
{
    (void)lp;
    if (!IsWindowVisible(h) || IsIconic(h) || IsShellWindow(h) || GetWindow(h, GW_OWNER)) return TRUE;
    LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE);
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if ((st & WS_CHILD) || (ex & WS_EX_TOOLWINDOW)) return TRUE;
    if (MonitorFromWindow(h, MONITOR_DEFAULTTONULL) != g.touchMon) return TRUE;
    if (ex & WS_EX_NOACTIVATE) return TRUE;          // already non-activating (ours or theirs)
    if (g.trackedCount >= MAX_TRACKED) return TRUE;
    SetLastError(0);
    SetWindowLongPtrW(h, GWL_EXSTYLE, ex | WS_EX_NOACTIVATE);
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_NOACTIVATE) {
        g.tracked[g.trackedCount++] = h;
        WCHAR title[128] = L""; GetWindowTextW(h, title, 128);
        WCHAR cls[64] = L""; GetClassNameW(h, cls, 64);
        Log(L"Touch-screen window set to not take focus: \"%ls\" [%ls]", title, cls);
    }
    return TRUE;
}

static void ScanTouchWindows(void)
{
    // forget / revert windows that closed or moved off the touch screen
    for (int i = g.trackedCount - 1; i >= 0; i--) {
        HWND h = g.tracked[i];
        if (!IsWindow(h)) { Untrack(i, FALSE); continue; }
        // minimised windows are released too, so they restore normally from the taskbar
        if (!g.touchMon || IsIconic(h) || MonitorFromWindow(h, MONITOR_DEFAULTTONULL) != g.touchMon) Untrack(i, TRUE);
    }
    if (g.cfg.enabled && g.cfg.noActivate && g.touchMon) EnumWindows(ScanProc, 0);
}

// ------------------------------------------------------------------ raw input
static void RegisterRawTouch(HWND hwnd)
{
    RAWINPUTDEVICE rid[2];
    rid[0].usUsagePage = 0x0D; rid[0].usUsage = 0x04; rid[0].dwFlags = RIDEV_INPUTSINK; rid[0].hwndTarget = hwnd; // touch screen
    rid[1].usUsagePage = 0x0D; rid[1].usUsage = 0x02; rid[1].dwFlags = RIDEV_INPUTSINK; rid[1].hwndTarget = hwnd; // pen
    g.rawTouchOk = RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
    if (!g.rawTouchOk) g.rawTouchOk = RegisterRawInputDevices(rid, 1, sizeof(RAWINPUTDEVICE));
    Log(L"Raw digitizer input: %ls", g.rawTouchOk ? L"registered" : L"not available (mouse-hook detection only)");
}

// ------------------------------------------------------------------ startup registry
static const WCHAR *RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static BOOL StartupEnabled(void)
{
    HKEY k; WCHAR v[MAX_PATH + 8]; DWORD sz = sizeof(v), type = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &k) != ERROR_SUCCESS) return FALSE;
    LONG r = RegQueryValueExW(k, APP_NAME, NULL, &type, (BYTE *)v, &sz);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

static void SetStartup(BOOL on)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        WCHAR v[MAX_PATH + 8]; swprintf(v, MAX_PATH + 8, L"\"%ls\"", g.exePath);
        RegSetValueExW(k, APP_NAME, 0, REG_SZ, (const BYTE *)v, (DWORD)((wcslen(v) + 1) * sizeof(WCHAR)));
    } else RegDeleteValueW(k, APP_NAME);
    RegCloseKey(k);
}

// ------------------------------------------------------------------ tray
static void TrayUpdate(DWORD msg, const WCHAR *balloon)
{
    NOTIFYICONDATAW nid; ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid); nid.hWnd = g.hwnd; nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = (HICON)LoadImageW(g.hInst, MAKEINTRESOURCEW(g.cfg.enabled ? 1 : 2), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (!nid.hIcon) nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    swprintf(nid.szTip, 128, L"TouchSplit - %ls", g.cfg.enabled ? L"on" : L"paused");
    if (balloon) {
        nid.uFlags |= NIF_INFO;
        wcsncpy(nid.szInfoTitle, APP_NAME, 63);
        wcsncpy(nid.szInfo, balloon, 255);
        nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    }
    Shell_NotifyIconW(msg, &nid);
    if (nid.hIcon) DestroyIcon(nid.hIcon);
}

static void SetEnabled(BOOL on, BOOL announce)
{
    g.cfg.enabled = on;
    if (!on) { EndSession(); ClearAllNoActivate(); }
    else ScanTouchWindows();
    SaveConfig();
    TrayUpdate(NIM_MODIFY, announce ? (on ? L"On - touch and mouse are independent." : L"Paused - Windows default touch behaviour.") : NULL);
    Log(L"%ls", on ? L"Enabled" : L"Paused");
}

static void ShowMenu(void)
{
    HMENU m = CreatePopupMenu();
    WCHAR info[220]; swprintf(info, 220, L"Touch screen: %ls", g.touchMon ? g.touchMonName : L"not found yet");
    AppendMenuW(m, MF_STRING | MF_GRAYED, IDM_INFO, L"TouchSplit " APP_VERSION);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (g.cfg.enabled ? MF_CHECKED : 0), IDM_ENABLE, L"TouchSplit on\tCtrl+Alt+F12");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (g.cfg.restoreCursor ? MF_CHECKED : 0), IDM_CURSOR, L"Put the mouse pointer back after a touch");
    AppendMenuW(m, MF_STRING | (g.cfg.restoreFocus ? MF_CHECKED : 0), IDM_FOCUS, L"Give focus back to my game/app after a touch");
    AppendMenuW(m, MF_STRING | (g.cfg.noActivate ? MF_CHECKED : 0), IDM_NOACT, L"Stop touch-screen windows taking focus");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | MF_GRAYED, IDM_INFO, info);
    AppendMenuW(m, MF_STRING, IDM_REDETECT, L"Re-detect touch screen");
    AppendMenuW(m, MF_STRING | (StartupEnabled() ? MF_CHECKED : 0), IDM_STARTUP, L"Start with Windows");
    AppendMenuW(m, MF_STRING, IDM_FOLDER, L"Open settings && log folder");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_EXIT, L"Exit");
    POINT p; GetCursorPos(&p);
    SetForegroundWindow(g.hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, g.hwnd, NULL);
    PostMessageW(g.hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

// ------------------------------------------------------------------ window
static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TS_RESTORE:
        EndSession();
        return 0;
    case WM_TIMER:
        if (wp == TIMER_SESSION && g.sessionActive) {
            DWORD idle = GetTickCount() - g.lastTouchTick;
            if ((!g.touchBtnDown && idle >= (DWORD)g.cfg.releaseMs) || idle > 15000) EndSession();
        } else if (wp == TIMER_SCAN) {
            ScanTouchWindows();
        }
        return 0;
    case WM_INPUT: {
        if (g.cfg.enabled) {
            RAWINPUTHEADER hdr; UINT sz = sizeof(hdr);
            if (GetRawInputData((HRAWINPUT)lp, RID_HEADER, &hdr, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
                hdr.dwType == RIM_TYPEHID)
                NoteTouch(NULL);
        }
        break;   // DefWindowProc must still run for WM_INPUT
    }
    case WM_HOTKEY:
        if (wp == HOTKEY_TOGGLE) SetEnabled(!g.cfg.enabled, TRUE);
        return 0;
    case WM_DISPLAYCHANGE:
    case 0x024D: /* WM_POINTERDEVICECHANGE */
        if (_wcsicmp(g.cfg.touchMonitor, L"auto") == 0 || g.touchMonLearned == 0) {
            ClearAllNoActivate();
            DetectTouchMonitor();
            ScanTouchWindows();
        }
        break;
    case WM_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) ShowMenu();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_ENABLE:  SetEnabled(!g.cfg.enabled, FALSE); break;
        case IDM_CURSOR:  g.cfg.restoreCursor = !g.cfg.restoreCursor; SaveConfig(); break;
        case IDM_FOCUS:   g.cfg.restoreFocus = !g.cfg.restoreFocus; SaveConfig(); break;
        case IDM_NOACT:   g.cfg.noActivate = !g.cfg.noActivate; SaveConfig();
                          if (!g.cfg.noActivate) ClearAllNoActivate(); else ScanTouchWindows(); break;
        case IDM_STARTUP: SetStartup(!StartupEnabled()); break;
        case IDM_REDETECT:
            ClearAllNoActivate(); DetectTouchMonitor(); ScanTouchWindows();
            { WCHAR b[220]; swprintf(b, 220, L"Touch screen: %ls", g.touchMon ? g.touchMonName : L"not found - touch it once and I'll learn it"); TrayUpdate(NIM_MODIFY, b); }
            break;
        case IDM_FOLDER:  ShellExecuteW(NULL, L"open", g.dir, NULL, NULL, SW_SHOWNORMAL); break;
        case IDM_EXIT:    DestroyWindow(h); break;
        }
        return 0;
    case WM_ENDSESSION:
        if (wp) ClearAllNoActivate();   // Windows is shutting down / logging off
        return 0;
    case WM_DESTROY:
        EndSession();
        ClearAllNoActivate();
        if (g.mouseHook) UnhookWindowsHookEx(g.mouseHook);
        if (g.fgHook) UnhookWinEvent(g.fgHook);
        UnregisterHotKey(h, HOTKEY_TOGGLE);
        TrayUpdate(NIM_DELETE, NULL);
        Log(L"Exit");
        PostQuitMessage(0);
        return 0;
    default:
        if (msg == g.msgTaskbarCreated && g.msgTaskbarCreated) { TrayUpdate(NIM_ADD, NULL); return 0; }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmd, int show)
{
    (void)hPrev; (void)cmd; (void)show;
    g.hInst = hInst;

    HANDLE mtx = CreateMutexW(NULL, TRUE, L"Local\\TouchSplit.SingleInstance");
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"TouchSplit is already running - look for its icon in the system tray.", APP_NAME, MB_ICONINFORMATION);
        return 0;
    }

    // Per-monitor DPI awareness so hook coordinates, GetCursorPos and SetCursorPos
    // all agree in physical pixels across mixed-scaling monitors.
    PFN_SetProcessDpiAwarenessContext pDpi = (PFN_SetProcessDpiAwarenessContext)(void *)
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    if (pDpi) pDpi((HANDLE)(INT_PTR)-4); else SetProcessDPIAware();

    GetModuleFileNameW(NULL, g.exePath, MAX_PATH);
    wcscpy(g.dir, g.exePath);
    WCHAR *slash = wcsrchr(g.dir, L'\\'); if (slash) *slash = 0;
    swprintf(g.iniPath, MAX_PATH, L"%ls\\TouchSplit.ini", g.dir);
    swprintf(g.logPath, MAX_PATH, L"%ls\\TouchSplit.log", g.dir);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(g.logPath, GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 512 * 1024) DeleteFileW(g.logPath);

    LoadConfig();
    SaveConfig();   // writes defaults on first run so the file is easy to edit
    Log(L"TouchSplit %ls starting", APP_VERSION);

    WNDCLASSEXW wc; ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc); wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.lpszClassName = L"TouchSplitWnd";
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    // A hidden top-level window (not message-only) so it receives broadcasts like TaskbarCreated.
    g.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, APP_NAME, WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInst, NULL);
    if (!g.hwnd) return 1;
    g.msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    GetCursorPos(&g.lastMousePos);
    g.lastRealFg = GetForegroundWindow();
    DetectTouchMonitor();
    RegisterRawTouch(g.hwnd);

    g.mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, hInst, 0);
    if (!g.mouseHook) {
        Log(L"FATAL: could not install mouse hook (error %lu)", GetLastError());
        MessageBoxW(NULL, L"Could not install the mouse hook.", APP_NAME, MB_ICONERROR);
        return 1;
    }
    g.fgHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, ForegroundProc, 0, 0,
                               WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!RegisterHotKey(g.hwnd, HOTKEY_TOGGLE, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F12))
        Log(L"Ctrl+Alt+F12 hotkey is taken by another app - use the tray menu instead");

    SetTimer(g.hwnd, TIMER_SCAN, 1000, NULL);
    ScanTouchWindows();
    TrayUpdate(NIM_ADD, NULL);
    {
        WCHAR b[256];
        swprintf(b, 256, L"Running. Touch screen: %ls", g.touchMon ? g.touchMonName : L"not detected yet - it will be learned from your first touch");
        TrayUpdate(NIM_MODIFY, b);
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
