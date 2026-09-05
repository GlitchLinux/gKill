/*
 * gKill v5 - Aggressive Task Killer
 * (c) 2026 Glitch Linux - https://glitchlinux.com
 *
 * PURE DARK MODE - two colors only: #353535 bg, #898989 text
 * No ListView (its scrollbar/checkboxes leak light theme).
 * Full owner-draw on a plain window.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Resource IDs ── */
#define IDI_APP        1
#define IDI_FIND     100
#define IDI_REFRESH  101
#define IDI_MIN_TB   102
#define IDI_MIN_TRAY 103
#define IDI_EXIT     104
#define IDI_CPU      105
#define IDI_RAM      106
#define IDI_TASKS    107
#define IDI_UP       108
#define IDI_DOWN     109
#define IDI_KILL     110
#define IDI_KILL_GREY 111

/* ── Base layout (at 96 DPI / 1.0x scale) ── */
#define BASE_CLIENT_W   820
#define BASE_CLIENT_H   680
#define BASE_TOOLBAR_H   48
#define BASE_HEADER_H    30
#define BASE_ROW_H       28
#define BASE_SEARCH_H    36
#define BASE_CHK_SZ      16
#define BASE_ICON_SZ     18
#define BASE_SCROLL_W    14

/* Scaled values - computed at runtime */
static int S_CLIENT_W, S_CLIENT_H, S_TOOLBAR_H, S_HEADER_H;
static int S_ROW_H, S_SEARCH_H, S_CHK_SZ, S_ICON_SZ, S_SCROLL_W;
static float g_scale = 1.0f;
static int g_dpi = 96;

/* Unified toolbar icon draw size */
#define TB_ICON_SZ  28

static int SC(int base) { return (int)(base * g_scale + 0.5f); }

static void ApplyScale(void) {
    S_CLIENT_W  = SC(BASE_CLIENT_W);
    S_CLIENT_H  = SC(BASE_CLIENT_H);
    S_TOOLBAR_H = SC(BASE_TOOLBAR_H);
    S_HEADER_H  = SC(BASE_HEADER_H);
    S_ROW_H     = SC(BASE_ROW_H);
    S_SEARCH_H  = SC(BASE_SEARCH_H);
    S_CHK_SZ    = SC(BASE_CHK_SZ);
    S_ICON_SZ   = SC(BASE_ICON_SZ);
    S_SCROLL_W  = SC(BASE_SCROLL_W);
}

/* ── Control IDs ── */
#define IDC_BTN_KILL       201
#define IDC_BTN_FIND       202
#define IDC_BTN_REFRESH    203
#define IDC_BTN_MIN_TB     204
#define IDC_BTN_MIN_TRAY   205
#define IDC_BTN_EXIT       206
#define IDC_EDIT_SEARCH    207
#define IDC_BTN_SEARCH_X   208
#define IDC_SORT_RAM       210
#define IDC_SORT_CPU       211
#define IDC_BTN_GROUP      212
#define IDC_BTN_ZOOM_IN    213
#define IDC_BTN_ZOOM_OUT   214

#define IDT_REFRESH  300
#define IDM_TRAY_SHOW 400
#define IDM_TRAY_EXIT 401
#define WM_TRAYICON  (WM_USER + 1)

/* ══ WIN11 DARK MODE PALETTE ══ */
#define C_BG         RGB(0x19, 0x19, 0x19)  /* #191919 backdrop */
#define C_BG_BAR     RGB(0x2C, 0x2C, 0x2C)  /* #2C2C2C menubar/toolbar */
#define C_BG_ALT     RGB(0x1F, 0x1F, 0x1F)  /* alt rows */
#define C_BG_HOVER   RGB(0x35, 0x35, 0x35)  /* hover */
#define C_BG_SEL     RGB(0x33, 0x33, 0x38)  /* checked row */
#define C_FG         RGB(0xFF, 0xFF, 0xFF)  /* #FFFFFF main text */
#define C_FG_SEC     RGB(0xDE, 0xDE, 0xDE)  /* #DEDEDE secondary/path/low values */
#define C_FG_DIM     RGB(0x89, 0x89, 0x89)  /* dim labels */
/* RAM color tiers */
#define C_RAM_LOW    RGB(0xDE, 0xDE, 0xDE)  /* 0-50 MB: secondary */
#define C_RAM_MED    RGB(0x69, 0xFF, 0x42)  /* 50-100 MB: green */
#define C_RAM_WARN   RGB(0xFF, 0xAF, 0x42)  /* 100-250 MB: orange */
#define C_RAM_HIGH   RGB(0xFF, 0x6F, 0x42)  /* 250-500 MB: dark orange */
#define C_RAM_CRIT   RGB(0xFF, 0x53, 0x42)  /* 500-1000 MB: red-orange */
#define C_RAM_MEGA   RGB(0xFF, 0x00, 0x00)  /* 1000+ MB: pure red */
/* CPU colors */
#define C_CPU_LOW    RGB(0xDE, 0xDE, 0xDE)  /* <5% */
#define C_CPU_MED    RGB(0xFF, 0xAF, 0x42)  /* 5-30% */
#define C_CPU_HIGH   RGB(0xFF, 0x00, 0x00)  /* >30% */
/* Kill button */
#define C_KILL_GREY  RGB(0x2C, 0x2C, 0x2C)  /* dormant - same as toolbar */
#define C_KILL_BG    RGB(0x80, 0x20, 0x20)  /* active (checkboxes ticked) */
#define C_KILL_HOT   RGB(0xA0, 0x30, 0x30)  /* hover */
/* Other */
#define C_ACCENT     RGB(0x8c, 0xef, 0x17)  /* checkbox checked fill */
#define C_FOCUS      RGB(0x3a, 0x3a, 0x45)  /* keyboard focus row */
#define C_SCROLLBAR  RGB(0x2C, 0x2C, 0x2C)
#define C_SCROLL_THB RGB(0x55, 0x55, 0x55)

/* ── Process info ── */
typedef struct {
    DWORD pid;
    WCHAR name[260];
    WCHAR exePath[MAX_PATH];
    double cpuPercent;
    SIZE_T ramBytes;
    ULONGLONG lastKernel;
    ULONGLONG lastUser;
    ULONGLONG lastTime;
    HICON hIcon;
    BOOL isSystem;
    BOOL checked;
} ProcInfo;

/* ── Globals ── */
static HINSTANCE g_hInst;
static HWND g_hWnd, g_hSearch, g_hSearchX;
static BOOL g_searchVisible = FALSE;
static WCHAR g_filterText[256] = {0};

static HICON g_ico[12];
static HICON g_icoKillGrey;
static HICON g_icoApp32;
static HFONT g_font, g_fontBold, g_fontHeader;
static HBRUSH g_brBg, g_brBgDark, g_brBgAlt;
static NOTIFYICONDATAW g_nid = {0};
static BOOL g_inTray = FALSE;

static ProcInfo *g_procs = NULL;
static int g_procCount = 0;
static ProcInfo *g_prevProcs = NULL;
static int g_prevCount = 0;

/* Visible (filtered) process list */
static ProcInfo **g_visible = NULL;
static int g_visCount = 0;

static int g_sortCol = 2;
static BOOL g_sortAsc = FALSE;
static int g_scrollPos = 0;
static int g_hoverRow = -1;
static int g_focusRow = -1;  /* keyboard navigation cursor */
static int g_lastCheckedRow = -1; /* for shift-select range */
static BOOL g_paused = FALSE; /* Ctrl+S freeze */
static BOOL g_grouped = FALSE; /* Ctrl+G group toggle */

/* Grouped view entry */
typedef struct {
    WCHAR name[260];
    int count;
    SIZE_T totalRam;
    double maxCpu;
    HICON hIcon;
    BOOL checked;
    DWORD *pids;     /* array of PIDs in this group */
    int pidCount;
} GroupInfo;

static GroupInfo *g_groups = NULL;
static int g_groupCount = 0;

static WNDPROC g_origSearchProc = NULL;

static void BuildGroups(void);  /* forward decl */
static void RemoveTray(void);  /* forward decl */
static void DeleteReviverTask(void); /* forward decl */

/* NtDll */
typedef LONG (NTAPI *pfnNtSuspendProcess)(HANDLE);
typedef LONG (NTAPI *pfnNtTerminateProcess)(HANDLE, LONG);
static pfnNtSuspendProcess  pNtSuspendProcess  = NULL;
static pfnNtTerminateProcess pNtTerminateProcess = NULL;

static const WCHAR *SYS_PROCS[] = {
    L"System", L"Registry", L"smss.exe", L"csrss.exe",
    L"wininit.exe", L"services.exe", L"lsass.exe",
    L"svchost.exe", L"winlogon.exe", L"dwm.exe",
    L"fontdrvhost.exe", L"Memory Compression",
    L"Idle", L"[System Process]", NULL
};

/* ── Helpers ── */
static inline ULONGLONG FT2U(FILETIME ft) {
    return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}
static BOOL IsSysProc(const WCHAR *n) {
    for (int i = 0; SYS_PROCS[i]; i++)
        if (_wcsicmp(n, SYS_PROCS[i]) == 0) return TRUE;
    return FALSE;
}
static COLORREF CpuColor(double c) {
    if (c < 5.0) return C_CPU_LOW; if (c < 30.0) return C_CPU_MED; return C_CPU_HIGH;
}
static COLORREF RamColor(SIZE_T r) {
    SIZE_T mb = r/(1024*1024);
    if (mb < 50)   return C_RAM_LOW;
    if (mb < 100)  return C_RAM_MED;
    if (mb < 250)  return C_RAM_WARN;
    if (mb < 500)  return C_RAM_HIGH;
    if (mb < 1000) return C_RAM_CRIT;
    return C_RAM_MEGA;
}
static BOOL AnyChecked(void) {
    for (int i = 0; i < g_visCount; i++)
        if (g_visible[i]->checked) return TRUE;
    return FALSE;
}
static HICON GetProcIcon(const WCHAR *p) {
    HICON h = NULL;
    if (p[0]) ExtractIconExW(p, 0, NULL, &h, 1);
    if (!h) h = LoadIconW(NULL, IDI_APPLICATION);
    return h;
}
static void EnableDebugPriv(void) {
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp; tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValueW(NULL, SE_DEBUG_NAME, &tp.Privileges[0].Luid))
            AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
        CloseHandle(hToken);
    }
}
static void LoadNtFuncs(void) {
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    if (h) {
        pNtSuspendProcess = (pfnNtSuspendProcess)(void*)GetProcAddress(h, "NtSuspendProcess");
        pNtTerminateProcess = (pfnNtTerminateProcess)(void*)GetProcAddress(h, "NtTerminateProcess");
    }
}

static void RebuildFonts(void) {
    if (g_font) DeleteObject(g_font);
    if (g_fontBold) DeleteObject(g_fontBold);
    if (g_fontHeader) DeleteObject(g_fontHeader);
    g_font = CreateFontW(-SC(17), 0,0,0, FW_NORMAL, 0,0,0,
        DEFAULT_CHARSET,0,0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fontBold = CreateFontW(-SC(16), 0,0,0, FW_BOLD, 0,0,0,
        DEFAULT_CHARSET,0,0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fontHeader = CreateFontW(-SC(15), 0,0,0, FW_BOLD, 0,0,0,
        DEFAULT_CHARSET,0,0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

static void RebuildIcons(void) {
    for (int i = 0; i < 11; i++) {
        if (g_ico[i]) DestroyIcon(g_ico[i]);
        /* Load at largest available size (256x256) for clean downscaling.
         * DrawIconEx will scale from this to the display size in one step. */
        g_ico[i] = (HICON)LoadImageW(g_hInst,
            MAKEINTRESOURCEW(100+i), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
        /* Fallback to 48 if 256 not available (kill-button.ico is 48 max) */
        if (!g_ico[i])
            g_ico[i] = (HICON)LoadImageW(g_hInst,
                MAKEINTRESOURCEW(100+i), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
    }
    if (g_icoKillGrey) DestroyIcon(g_icoKillGrey);
    g_icoKillGrey = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_KILL_GREY),
                    IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
    if (!g_icoKillGrey)
        g_icoKillGrey = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_KILL_GREY),
                        IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
}

/* ── How many rows fit in the list area ── */
static int VisibleRows(void) {
    RECT rc; GetClientRect(g_hWnd, &rc);
    int listH = rc.bottom - S_TOOLBAR_H - S_HEADER_H - (g_searchVisible ? S_SEARCH_H : 0);
    return listH / S_ROW_H;
}

/* ── Column widths (proportional) ── */
typedef struct { int x, w; } ColInfo;
static void GetCols(ColInfo cols[4]) {
    RECT rc; GetClientRect(g_hWnd, &rc);
    int w = rc.right - S_SCROLL_W;
    int chkCol = S_CHK_SZ + 8 + S_ICON_SZ + 6; /* checkbox + icon space */
    cols[0].x = 0;        cols[0].w = w * 28 / 100;
    cols[1].x = cols[0].w; cols[1].w = w * 10 / 100;
    cols[2].x = cols[1].x + cols[1].w; cols[2].w = w * 14 / 100;
    cols[3].x = cols[2].x + cols[2].w; cols[3].w = w - cols[3].x;
}

/* ── Process enumeration ── */
static int ProcCmp(const void *a, const void *b) {
    const ProcInfo *pa = *(const ProcInfo **)a;
    const ProcInfo *pb = *(const ProcInfo **)b;
    int r = 0;
    switch (g_sortCol) {
        case 0: r = _wcsicmp(pa->name, pb->name); break;
        case 1: r = (pa->cpuPercent < pb->cpuPercent) ? -1 :
                     (pa->cpuPercent > pb->cpuPercent) ? 1 : 0; break;
        case 2: r = (pa->ramBytes < pb->ramBytes) ? -1 :
                     (pa->ramBytes > pb->ramBytes) ? 1 : 0; break;
    }
    return g_sortAsc ? r : -r;
}

static void RefreshProcs(void) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe = { .dwSize = sizeof(pe) };
    int count = 0;
    if (Process32FirstW(hSnap, &pe)) { do { count++; } while (Process32NextW(hSnap, &pe)); }

    ProcInfo *np = (ProcInfo *)calloc(count, sizeof(ProcInfo));
    if (!np) { CloseHandle(hSnap); return; }

    int idx = 0;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID == 0) continue;
            ProcInfo *p = &np[idx];
            p->pid = pe.th32ProcessID;
            wcsncpy(p->name, pe.szExeFile, 259);
            p->isSystem = IsSysProc(pe.szExeFile);

            /* Carry over checked state from old list */
            if (g_procs) {
                for (int j = 0; j < g_procCount; j++) {
                    if (g_procs[j].pid == pe.th32ProcessID) {
                        p->checked = g_procs[j].checked;
                        break;
                    }
                }
            }

            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ,
                FALSE, pe.th32ProcessID);
            if (hProc) {
                DWORD pathLen = MAX_PATH;
                QueryFullProcessImageNameW(hProc, 0, p->exePath, &pathLen);
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc)))
                    p->ramBytes = pmc.WorkingSetSize;
                FILETIME ftC, ftE, ftK, ftU;
                if (GetProcessTimes(hProc, &ftC, &ftE, &ftK, &ftU)) {
                    ULONGLONG kernel=FT2U(ftK), user=FT2U(ftU);
                    FILETIME ftNow; GetSystemTimeAsFileTime(&ftNow);
                    ULONGLONG now=FT2U(ftNow);
                    for (int j = 0; j < g_prevCount; j++) {
                        if (g_prevProcs[j].pid == pe.th32ProcessID) {
                            ULONGLONG dk=kernel-g_prevProcs[j].lastKernel;
                            ULONGLONG du=user-g_prevProcs[j].lastUser;
                            ULONGLONG dt=now-g_prevProcs[j].lastTime;
                            if (dt > 0) {
                                SYSTEM_INFO si; GetSystemInfo(&si);
                                p->cpuPercent=(double)(dk+du)*100.0/(double)dt/si.dwNumberOfProcessors;
                                if (p->cpuPercent > 100.0) p->cpuPercent = 100.0;
                            }
                            break;
                        }
                    }
                    p->lastKernel=kernel; p->lastUser=user; p->lastTime=now;
                }
                p->hIcon = GetProcIcon(p->exePath);
                CloseHandle(hProc);
            } else {
                p->hIcon = LoadIconW(NULL, IDI_APPLICATION);
            }
            idx++;
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);

    if (g_prevProcs) free(g_prevProcs);
    g_prevProcs = g_procs; g_prevCount = g_procCount;
    g_procs = np; g_procCount = idx;

    /* Build visible list */
    if (g_visible) free(g_visible);
    g_visible = (ProcInfo **)calloc(idx, sizeof(ProcInfo *));
    g_visCount = 0;
    BOOL hasFilter = (g_filterText[0] != L'\0');

    for (int i = 0; i < idx; i++) {
        ProcInfo *p = &np[i];
        if (!hasFilter && p->isSystem) continue;
        if (!hasFilter && p->ramBytes < (512*1024) && p->cpuPercent < 0.1) continue;
        if (hasFilter) {
            WCHAR lo[260], fl[256];
            wcsncpy(lo, p->name, 259); wcsncpy(fl, g_filterText, 255);
            _wcslwr(lo); _wcslwr(fl);
            if (!wcsstr(lo, fl)) continue;
        }
        g_visible[g_visCount++] = p;
    }

    qsort(g_visible, g_visCount, sizeof(ProcInfo *), ProcCmp);

    /* Build grouped view */
    if (g_grouped) BuildGroups();

    /* Clamp scroll */
    int totalRows = g_grouped ? g_groupCount : g_visCount;
    int maxScroll = totalRows - VisibleRows();
    if (maxScroll < 0) maxScroll = 0;
    if (g_scrollPos > maxScroll) g_scrollPos = maxScroll;
}

/* ── Build grouped view ── */
static void BuildGroups(void) {
    /* Free old groups */
    if (g_groups) {
        for (int i = 0; i < g_groupCount; i++)
            if (g_groups[i].pids) free(g_groups[i].pids);
        free(g_groups);
        g_groups = NULL;
    }
    g_groupCount = 0;

    if (g_visCount == 0) return;

    /* Allocate max possible groups (one per visible proc) */
    g_groups = (GroupInfo *)calloc(g_visCount, sizeof(GroupInfo));

    for (int i = 0; i < g_visCount; i++) {
        ProcInfo *p = g_visible[i];

        /* Find existing group */
        int gIdx = -1;
        for (int j = 0; j < g_groupCount; j++) {
            if (_wcsicmp(g_groups[j].name, p->name) == 0) {
                gIdx = j;
                break;
            }
        }

        if (gIdx < 0) {
            /* New group */
            gIdx = g_groupCount++;
            GroupInfo *g = &g_groups[gIdx];
            wcsncpy(g->name, p->name, 259);
            g->hIcon = p->hIcon;
            g->pids = (DWORD *)calloc(g_visCount, sizeof(DWORD));
            g->pidCount = 0;
            g->checked = FALSE;
        }

        GroupInfo *g = &g_groups[gIdx];
        g->totalRam += p->ramBytes;
        if (p->cpuPercent > g->maxCpu) g->maxCpu = p->cpuPercent;
        g->pids[g->pidCount++] = p->pid;
        g->count++;
        if (p->checked) g->checked = TRUE;
    }
}

/* ── Aggressive kill ── */
static void KillProcAggressive(DWORD pid) {
    if (pid <= 4) return;
    HANDLE h;
    h = OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE, FALSE, pid);
    if (h) { TerminateProcess(h,1);
        if (WaitForSingleObject(h,100)==WAIT_OBJECT_0) { CloseHandle(h); return; }
        CloseHandle(h); }
    if (pNtTerminateProcess) {
        h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (h) { pNtTerminateProcess(h,1); CloseHandle(h); Sleep(50);
            h=OpenProcess(SYNCHRONIZE,FALSE,pid); if(!h) return;
            if(WaitForSingleObject(h,0)==WAIT_OBJECT_0){CloseHandle(h);return;}
            CloseHandle(h); } }
    if (pNtSuspendProcess) {
        h = OpenProcess(PROCESS_SUSPEND_RESUME|PROCESS_TERMINATE, FALSE, pid);
        if (h) { pNtSuspendProcess(h); Sleep(30); TerminateProcess(h,1);
            CloseHandle(h); Sleep(50);
            h=OpenProcess(SYNCHRONIZE,FALSE,pid); if(!h) return;
            if(WaitForSingleObject(h,0)==WAIT_OBJECT_0){CloseHandle(h);return;}
            CloseHandle(h); } }
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe2 = { .dwSize=sizeof(pe2) };
        if (Process32FirstW(hSnap, &pe2)) {
            do { if (pe2.th32ParentProcessID==pid && pe2.th32ProcessID!=pid)
                    KillProcAggressive(pe2.th32ProcessID);
            } while (Process32NextW(hSnap, &pe2)); }
        CloseHandle(hSnap); }
    WCHAR cmd[128];
    _snwprintf(cmd, 128, L"taskkill /F /T /PID %lu", pid);
    STARTUPINFOW si = { .cb=sizeof(si), .dwFlags=STARTF_USESHOWWINDOW, .wShowWindow=SW_HIDE };
    PROCESS_INFORMATION pi = {0};
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
}

static void KillChecked(void) {
    DWORD pids[512]; int n = 0;
    BOOL killingSelf = FALSE;
    BOOL killingExplorer = FALSE;
    DWORD myPid = GetCurrentProcessId();

    if (g_grouped) {
        for (int i = 0; i < g_groupCount && n < 500; i++) {
            if (g_groups[i].checked) {
                for (int j = 0; j < g_groups[i].pidCount && n < 512; j++) {
                    DWORD pid = g_groups[i].pids[j];
                    pids[n++] = pid;
                    if (pid == myPid) killingSelf = TRUE;
                }
                if (_wcsicmp(g_groups[i].name, L"explorer.exe") == 0)
                    killingExplorer = TRUE;
            }
        }
    } else {
        for (int i = 0; i < g_visCount && n < 512; i++) {
            if (g_visible[i]->checked) {
                pids[n++] = g_visible[i]->pid;
                if (g_visible[i]->pid == myPid) killingSelf = TRUE;
                if (_wcsicmp(g_visible[i]->name, L"explorer.exe") == 0)
                    killingExplorer = TRUE;
            }
        }
    }

    if (n == 0) return;

    /* If killing explorer: spawn a helper to restart it after 5 sec */
    if (killingExplorer && !killingSelf) {
        WCHAR cmd[256];
        _snwprintf(cmd, 256,
            L"cmd.exe /c \"ping -n 6 127.0.0.1 >nul & explorer.exe\"");
        STARTUPINFOW si = { .cb=sizeof(si), .dwFlags=STARTF_USESHOWWINDOW,
                            .wShowWindow=SW_HIDE };
        PROCESS_INFORMATION pi = {0};
        CreateProcessW(NULL, cmd, NULL, NULL, FALSE,
            CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread) CloseHandle(pi.hThread);
    }

    if (killingSelf) {
        /* Remove own PID from the kill list */
        for (int i = 0; i < n; i++) {
            if (pids[i] == myPid) {
                pids[i] = pids[--n];
                break;
            }
        }
    }

    /* Kill everything else first */
    for (int i = 0; i < n; i++) KillProcAggressive(pids[i]);

    if (killingSelf) {
        /* Just exit cleanly. The reviver scheduled task will
         * detect gkill is gone within 60 seconds and restart it. */
        RemoveTray();
        ExitProcess(0);
        return;
    }

    Sleep(150);
    RefreshProcs();
    InvalidateRect(g_hWnd, NULL, TRUE);
    InvalidateRect(GetDlgItem(g_hWnd, IDC_BTN_KILL), NULL, TRUE);
}

/* ── Row hit test ── */
static int HitTestRow(int mouseY) {
    int top = S_TOOLBAR_H + S_HEADER_H + (g_searchVisible ? S_SEARCH_H : 0);
    if (mouseY < top) return -1;
    int row = (mouseY - top) / S_ROW_H + g_scrollPos;
    if (row >= g_visCount) return -1;
    return row;
}

static BOOL HitTestCheckbox(int mouseX, int mouseY) {
    (void)mouseY;
    return (mouseX >= 6 && mouseX < 6 + S_CHK_SZ);
}

/* ── Draw the custom list area ── */
static void PaintList(HDC hdc) {
    RECT rc; GetClientRect(g_hWnd, &rc);
    int w = rc.right;
    int top = S_TOOLBAR_H + S_HEADER_H + (g_searchVisible ? S_SEARCH_H : 0);
    int listH = rc.bottom - top;
    int visRows = listH / S_ROW_H;
    ColInfo cols[4]; GetCols(cols);

    /* ── HEADER ── */
    int hy = S_TOOLBAR_H + (g_searchVisible ? S_SEARCH_H : 0);
    RECT hdr = { 0, hy, w, hy + S_HEADER_H };
    HBRUSH brHdr = CreateSolidBrush(C_BG_BAR);
    FillRect(hdc, &hdr, brHdr); DeleteObject(brHdr);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, C_FG);
    SelectObject(hdc, g_fontBold);

    const WCHAR *hdrs[] = { L"Process", L"CPU", L"RAM", L"Path" };
    UINT fmts[] = { DT_LEFT, DT_RIGHT, DT_RIGHT, DT_LEFT };
    for (int c = 0; c < 4; c++) {
        RECT tr = { cols[c].x + 8, hy, cols[c].x + cols[c].w - 4, hy + S_HEADER_H };
        if (c == 0) tr.left = 6 + S_CHK_SZ + 8 + S_ICON_SZ + 6;
        DrawTextW(hdc, hdrs[c], -1, &tr, fmts[c] | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    /* Header bottom line */
    HPEN hpen = CreatePen(PS_SOLID, 1, C_FG);
    HPEN oldPen = (HPEN)SelectObject(hdc, hpen);
    MoveToEx(hdc, 0, hy + S_HEADER_H - 1, NULL);
    LineTo(hdc, w, hy + S_HEADER_H - 1);
    SelectObject(hdc, oldPen); DeleteObject(hpen);

    /* ── ROWS ── */
    SelectObject(hdc, g_font);

    int totalRows = g_grouped ? g_groupCount : g_visCount;

    for (int r = 0; r < visRows; r++) {
        int idx = g_scrollPos + r;
        int ry = top + r * S_ROW_H;
        RECT rowRc = { 0, ry, w - S_SCROLL_W, ry + S_ROW_H };

        if (idx >= totalRows) {
            HBRUSH br = CreateSolidBrush(C_BG);
            FillRect(hdc, &rowRc, br); DeleteObject(br);
            continue;
        }

        /* Extract row data depending on mode */
        WCHAR *name;
        double cpu;
        SIZE_T ram;
        HICON icon;
        BOOL checked;
        WCHAR *path;
        WCHAR nameBuf[280];
        WCHAR pathBuf[32];

        if (g_grouped) {
            GroupInfo *g = &g_groups[idx];
            if (g->count > 1)
                _snwprintf(nameBuf, 280, L"%s (%d)", g->name, g->count);
            else
                wcsncpy(nameBuf, g->name, 279);
            name = nameBuf;
            cpu = g->maxCpu;
            ram = g->totalRam;
            icon = g->hIcon;
            checked = g->checked;
            _snwprintf(pathBuf, 32, L"[%d instances]", g->count);
            path = g->count > 1 ? pathBuf : L"";
            /* Try to get path from first PID for single instances */
            if (g->count == 1) {
                for (int i = 0; i < g_visCount; i++) {
                    if (g_visible[i]->pid == g->pids[0]) {
                        path = g_visible[i]->exePath;
                        break;
                    }
                }
            }
        } else {
            ProcInfo *p = g_visible[idx];
            name = p->name;
            cpu = p->cpuPercent;
            ram = p->ramBytes;
            icon = p->hIcon;
            checked = p->checked;
            path = p->exePath;
        }

        /* Row background */
        COLORREF rowBg;
        if (checked) rowBg = C_BG_SEL;
        else if (idx == g_focusRow) rowBg = C_FOCUS;
        else if (idx == g_hoverRow) rowBg = C_BG_HOVER;
        else rowBg = (idx % 2 == 0) ? C_BG : C_BG_ALT;

        HBRUSH brRow = CreateSolidBrush(rowBg);
        FillRect(hdc, &rowRc, brRow); DeleteObject(brRow);

        /* Focus indicator */
        if (idx == g_focusRow) {
            RECT fb = { 0, ry, 3, ry + S_ROW_H };
            HBRUSH fBr = CreateSolidBrush(C_ACCENT);
            FillRect(hdc, &fb, fBr); DeleteObject(fBr);
        }

        int x = 6;

        /* Checkbox */
        RECT chk = { x, ry + (S_ROW_H - S_CHK_SZ) / 2,
                     x + S_CHK_SZ, ry + (S_ROW_H + S_CHK_SZ) / 2 };
        HPEN chkPen = CreatePen(PS_SOLID, 1, C_FG);
        HPEN oldP2 = (HPEN)SelectObject(hdc, chkPen);
        HBRUSH chkBr = CreateSolidBrush(checked ? C_ACCENT : rowBg);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, chkBr);
        Rectangle(hdc, chk.left, chk.top, chk.right, chk.bottom);
        SelectObject(hdc, oldP2); DeleteObject(chkPen);
        SelectObject(hdc, oldBr); DeleteObject(chkBr);

        if (checked) {
            HPEN cmPen = CreatePen(PS_SOLID, 2, C_BG_BAR);
            SelectObject(hdc, cmPen);
            MoveToEx(hdc, chk.left + 3, chk.top + S_CHK_SZ/2, NULL);
            LineTo(hdc, chk.left + S_CHK_SZ/2 - 1, chk.bottom - 4);
            LineTo(hdc, chk.right - 3, chk.top + 3);
            SelectObject(hdc, oldP2);
            DeleteObject(cmPen);
        }

        x += S_CHK_SZ + 6;

        /* Icon */
        if (icon)
            DrawIconEx(hdc, x, ry + (S_ROW_H - S_ICON_SZ) / 2,
                       icon, S_ICON_SZ, S_ICON_SZ, 0, NULL, DI_NORMAL);
        x += S_ICON_SZ + 6;

        /* Name */
        SetTextColor(hdc, C_FG);
        SelectObject(hdc, g_font);
        RECT nr = { x, ry, cols[0].x + cols[0].w - 4, ry + S_ROW_H };
        DrawTextW(hdc, name, -1, &nr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        /* CPU */
        WCHAR cpuStr[32];
        if (cpu < 0.1) wcscpy(cpuStr, L"0%");
        else _snwprintf(cpuStr, 32, L"%.1f%%", cpu);
        SetTextColor(hdc, CpuColor(cpu));
        RECT cr = { cols[1].x + 4, ry, cols[1].x + cols[1].w - 8, ry + S_ROW_H };
        DrawTextW(hdc, cpuStr, -1, &cr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        /* RAM */
        WCHAR ramStr[32];
        SIZE_T mb = ram / (1024*1024);
        if (mb >= 1024)
            _snwprintf(ramStr, 32, L"%.1f GB", (double)ram/(1024.0*1024.0*1024.0));
        else
            _snwprintf(ramStr, 32, L"%llu MB", (unsigned long long)mb);
        SetTextColor(hdc, RamColor(ram));
        SelectObject(hdc, g_fontBold);
        RECT rr = { cols[2].x + 4, ry, cols[2].x + cols[2].w - 8, ry + S_ROW_H };
        DrawTextW(hdc, ramStr, -1, &rr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        /* Path */
        SetTextColor(hdc, C_FG_SEC);
        SelectObject(hdc, g_font);
        RECT pr = { cols[3].x + 8, ry, w - S_SCROLL_W - 4, ry + S_ROW_H };
        DrawTextW(hdc, path, -1, &pr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    /* ── CUSTOM SCROLLBAR ── */
    int sbX = w - S_SCROLL_W;
    RECT sbRc = { sbX, top, w, rc.bottom };
    HBRUSH sbBr = CreateSolidBrush(C_BG_BAR);
    FillRect(hdc, &sbRc, sbBr); DeleteObject(sbBr);

    if (totalRows > visRows) {
        int trackH = listH;
        int thumbH = (visRows * trackH) / totalRows;
        if (thumbH < 20) thumbH = 20;
        int maxScroll2 = totalRows - visRows;
        int thumbY = top + (maxScroll2 > 0 ? (g_scrollPos * (trackH - thumbH)) / maxScroll2 : 0);

        RECT thumbRc = { sbX + 2, thumbY, w - 2, thumbY + thumbH };
        HBRUSH thBr = CreateSolidBrush(C_SCROLL_THB);
        /* Rounded feel via simple rect for now */
        FillRect(hdc, &thumbRc, thBr); DeleteObject(thBr);
    }
}

/* ── Ensure focus row is visible ── */
static void EnsureFocusVisible(void) {
    if (g_focusRow < 0) return;
    int vis = VisibleRows();
    if (g_focusRow < g_scrollPos) g_scrollPos = g_focusRow;
    else if (g_focusRow >= g_scrollPos + vis) g_scrollPos = g_focusRow - vis + 1;
    int maxS = g_visCount - vis;
    if (maxS < 0) maxS = 0;
    if (g_scrollPos > maxS) g_scrollPos = maxS;
    if (g_scrollPos < 0) g_scrollPos = 0;
}

/* ── Search ── */
static void ToggleSearch(void) {
    g_searchVisible = !g_searchVisible;
    ShowWindow(g_hSearch, g_searchVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hSearchX, g_searchVisible ? SW_SHOW : SW_HIDE);
    if (g_searchVisible) SetFocus(g_hSearch);
    else { g_filterText[0]=L'\0'; SetWindowTextW(g_hSearch, L"");
           RefreshProcs(); }
    RECT rc; GetClientRect(g_hWnd, &rc);
    SendMessageW(g_hWnd, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
    InvalidateRect(g_hWnd, NULL, TRUE);
}

static LRESULT CALLBACK SearchSubProc(HWND hWnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) { ToggleSearch(); return 0; }
    LRESULT r = CallWindowProcW(g_origSearchProc, hWnd, msg, wParam, lParam);
    if (msg == WM_CHAR || msg == WM_KEYUP) {
        GetWindowTextW(hWnd, g_filterText, 255);
        g_scrollPos = 0;
        RefreshProcs();
        InvalidateRect(g_hWnd, NULL, TRUE);
    }
    return r;
}

/* ── Tray ── */
static void AddTray(void) {
    g_nid.cbSize=sizeof(g_nid); g_nid.hWnd=g_hWnd; g_nid.uID=1;
    g_nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;
    g_nid.uCallbackMessage=WM_TRAYICON;
    g_nid.hIcon=g_icoApp32?g_icoApp32:LoadIconW(NULL,IDI_APPLICATION);
    wcscpy(g_nid.szTip, L"gKill");
    Shell_NotifyIconW(NIM_ADD, &g_nid); g_inTray=TRUE;
}
static void RemoveTray(void) {
    if (g_inTray) { Shell_NotifyIconW(NIM_DELETE, &g_nid); g_inTray=FALSE; }
}

/* ── Owner-draw toolbar buttons ── */
static void DrawIconBtn(DRAWITEMSTRUCT *di, int icoIdx, BOOL isKill) {
    BOOL hot = (di->itemState & ODS_SELECTED);
    if (!hot) { POINT pt; GetCursorPos(&pt); ScreenToClient(di->hwndItem, &pt);
        if (PtInRect(&di->rcItem, pt)) hot=TRUE; }

    COLORREF bg = hot ? C_BG_HOVER : C_BG_BAR;
    HBRUSH br = CreateSolidBrush(bg);
    FillRect(di->hDC, &di->rcItem, br); DeleteObject(br);

    HICON ico;
    if (isKill) {
        BOOL active = AnyChecked();
        ico = (active || hot) ? g_ico[10] : g_icoKillGrey;
    } else {
        ico = (icoIdx >= 0 && icoIdx < 12) ? g_ico[icoIdx] : NULL;
    }

    if (ico) {
        int sz = SC(TB_ICON_SZ);
        int bw = di->rcItem.right - di->rcItem.left;
        int bh = di->rcItem.bottom - di->rcItem.top;
        int x = di->rcItem.left + (bw - sz) / 2;
        int y = di->rcItem.top + (bh - sz) / 2;
        DrawIconEx(di->hDC, x, y, ico, sz, sz, 0, NULL, DI_NORMAL);
    }
}
static void DrawSortBtn(DRAWITEMSTRUCT *di, int icoIdx, const WCHAR *label, int sortTarget) {
    BOOL hot = (di->itemState & ODS_SELECTED);
    if (!hot) { POINT pt; GetCursorPos(&pt); ScreenToClient(di->hwndItem, &pt);
        if (PtInRect(&di->rcItem, pt)) hot=TRUE; }
    HBRUSH br = CreateSolidBrush(hot ? C_BG_HOVER : C_BG_BAR);
    FillRect(di->hDC, &di->rcItem, br); DeleteObject(br);
    int bw = di->rcItem.right - di->rcItem.left;
    int bh = di->rcItem.bottom - di->rcItem.top;
    int icoSz = SC(20);
    int arrowSz = SC(12);
    int lPad = SC(6);
    if (g_ico[icoIdx])
        DrawIconEx(di->hDC, di->rcItem.left + lPad,
                   di->rcItem.top + (bh - icoSz) / 2,
                   g_ico[icoIdx], icoSz, icoSz, 0, NULL, DI_NORMAL);
    HICON arrow = (g_sortCol==sortTarget) ? (g_sortAsc ? g_ico[8] : g_ico[9]) : NULL;
    if (arrow)
        DrawIconEx(di->hDC, di->rcItem.right - arrowSz - SC(4),
                   di->rcItem.top + (bh - arrowSz) / 2,
                   arrow, arrowSz, arrowSz, 0, NULL, DI_NORMAL);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, (g_sortCol==sortTarget) ? C_ACCENT : C_FG);
    SelectObject(di->hDC, g_fontBold);
    RECT tr = di->rcItem;
    tr.left += lPad + icoSz + SC(4);
    tr.right -= arrowSz + SC(6);
    DrawTextW(di->hDC, label, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

/* ── Window proc ── */
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hWnd = hWnd;

        /* Force dark title bar on Win10 20H1+ / Win11 */
        {
            HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
            if (hDwm) {
                typedef HRESULT (WINAPI *pDwmSWA)(HWND,DWORD,LPCVOID,DWORD);
                pDwmSWA dwmSet = (pDwmSWA)(void*)
                    GetProcAddress(hDwm, "DwmSetWindowAttribute");
                if (dwmSet) {
                    BOOL useDark = TRUE;
                    dwmSet(hWnd, 20, &useDark, sizeof(useDark));
                }
            }
        }

        g_brBg = CreateSolidBrush(C_BG);
        g_brBgDark = CreateSolidBrush(C_BG_BAR);
        g_brBgAlt = CreateSolidBrush(C_BG_ALT);

        RebuildFonts();
        RebuildIcons();

        g_icoApp32 = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_APP),
                        IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);

        /* Toolbar - all buttons created at (0,0), positioned in WM_SIZE */
        int tbIds[] = {IDC_BTN_KILL, IDC_SORT_RAM, IDC_SORT_CPU,
                       IDC_BTN_GROUP, IDC_BTN_ZOOM_OUT, IDC_BTN_ZOOM_IN,
                       IDC_BTN_FIND, IDC_BTN_REFRESH,
                       IDC_BTN_MIN_TB, IDC_BTN_MIN_TRAY, IDC_BTN_EXIT};
        for (int i = 0; i < 11; i++)
            CreateWindowW(L"BUTTON", L"", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                0, 0, 10, 10, hWnd, (HMENU)(LONG_PTR)tbIds[i], g_hInst, NULL);

        /* Search */
        g_hSearch = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|ES_AUTOHSCROLL, 0,0,200,SC(30), hWnd,
            (HMENU)IDC_EDIT_SEARCH, g_hInst, NULL);
        SendMessageW(g_hSearch, WM_SETFONT, (WPARAM)g_font, TRUE);
        g_origSearchProc = (WNDPROC)SetWindowLongPtrW(g_hSearch,
            GWLP_WNDPROC, (LONG_PTR)SearchSubProc);
        g_hSearchX = CreateWindowW(L"BUTTON", L"",
            WS_CHILD|BS_OWNERDRAW, 0,0,SC(30),SC(30), hWnd,
            (HMENU)IDC_BTN_SEARCH_X, g_hInst, NULL);

        /* Tooltips */
        HWND hTip = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL,
            WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX, 0,0,0,0, hWnd, NULL, g_hInst, NULL);
        struct { int id; const WCHAR *t; } tips[] = {
            {IDC_BTN_KILL,L"Kill checked (Del)"},{IDC_BTN_FIND,L"Find (Ctrl+F)"},
            {IDC_BTN_REFRESH,L"Refresh"},{IDC_BTN_MIN_TB,L"Taskbar"},
            {IDC_BTN_MIN_TRAY,L"Tray"},{IDC_BTN_EXIT,L"Exit"},
            {IDC_SORT_RAM,L"Sort RAM"},{IDC_SORT_CPU,L"Sort CPU"},
            {IDC_BTN_GROUP,L"Group view (Ctrl+G)"},
            {IDC_BTN_ZOOM_OUT,L"Zoom out (Ctrl+-)"},
            {IDC_BTN_ZOOM_IN,L"Zoom in (Ctrl++)"},
        };
        for (int i=0;i<11;i++) {
            TOOLINFOW ti={0}; ti.cbSize=sizeof(ti);
            ti.uFlags=TTF_IDISHWND|TTF_SUBCLASS; ti.hwnd=hWnd;
            ti.uId=(UINT_PTR)GetDlgItem(hWnd,tips[i].id);
            ti.lpszText=(LPWSTR)tips[i].t;
            SendMessageW(hTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        }

        EnableDebugPriv(); LoadNtFuncs();
        RefreshProcs();
        SetTimer(hWnd, IDT_REFRESH, 2000, NULL);
        return 0;
    }

    case WM_TIMER:
        if (wParam==IDT_REFRESH && !g_paused) { RefreshProcs(); InvalidateRect(hWnd,NULL,FALSE); }
        return 0;

    case WM_SIZE: {
        RECT rc; GetClientRect(hWnd, &rc);
        int w = rc.right;

        /* ── Unified toolbar layout ──
         * All buttons same height. Icon-only buttons = square.
         * Sort buttons = wider (icon + text + arrow).
         * Flow left-to-right for left group, right-to-left for right group.
         */
        int btnH = SC(42);                    /* uniform height */
        int btnSq = SC(44);                   /* square icon button width */
        int btnSort = SC(86);                 /* sort button width (icon+text) */
        int gap = SC(2);                      /* gap between buttons */
        int pad = SC(4);                      /* edge padding */
        int y = (S_TOOLBAR_H - btnH) / 2;    /* vertical center */

        /* Left group: Kill, RAM, CPU, Group, ZoomOut, ZoomIn */
        int lx = pad;
        MoveWindow(GetDlgItem(hWnd, IDC_BTN_KILL), lx, y, btnSq, btnH, TRUE);
        lx += btnSq + gap;
        MoveWindow(GetDlgItem(hWnd, IDC_SORT_RAM), lx, y, btnSort, btnH, TRUE);
        lx += btnSort + gap;
        MoveWindow(GetDlgItem(hWnd, IDC_SORT_CPU), lx, y, btnSort, btnH, TRUE);
        lx += btnSort + gap;
        MoveWindow(GetDlgItem(hWnd, IDC_BTN_GROUP), lx, y, btnSq, btnH, TRUE);
        lx += btnSq + gap;
        MoveWindow(GetDlgItem(hWnd, IDC_BTN_ZOOM_OUT), lx, y, btnSq, btnH, TRUE);
        lx += btnSq + gap;
        MoveWindow(GetDlgItem(hWnd, IDC_BTN_ZOOM_IN), lx, y, btnSq, btnH, TRUE);

        /* Right group: Exit, MinTray, MinTB, Refresh, Find (right to left) */
        int rx = w - pad;
        int rids[] = {IDC_BTN_EXIT, IDC_BTN_MIN_TRAY, IDC_BTN_MIN_TB,
                      IDC_BTN_REFRESH, IDC_BTN_FIND};
        for (int i = 0; i < 5; i++) {
            rx -= btnSq;
            MoveWindow(GetDlgItem(hWnd, rids[i]), rx, y, btnSq, btnH, TRUE);
            rx -= gap;
        }

        /* Search bar */
        if (g_searchVisible) {
            int sy = S_TOOLBAR_H + SC(3);
            int searchH = SC(28);
            MoveWindow(g_hSearch, pad, sy, w - pad*2 - SC(32), searchH, TRUE);
            MoveWindow(g_hSearchX, w - pad - SC(28), sy, SC(28), searchH, TRUE);
        }

        /* Listview area */
        int searchOff = g_searchVisible ? S_SEARCH_H : 0;
        int listTop = S_TOOLBAR_H + searchOff;
        int listH = rc.bottom - listTop;
        if (listH < 50) listH = 50;
        /* (list is painted, not a control - just invalidate) */

        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        /* Double buffer */
        RECT rc; GetClientRect(hWnd, &rc);
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBm = (HBITMAP)SelectObject(memDC, memBm);

        /* Toolbar bg */
        RECT tbRc = { 0, 0, rc.right, S_TOOLBAR_H };
        FillRect(memDC, &tbRc, g_brBgDark);
        /* Toolbar separator */
        HPEN sep = CreatePen(PS_SOLID, 1, C_FG);
        HPEN oldPen = (HPEN)SelectObject(memDC, sep);
        MoveToEx(memDC, 0, S_TOOLBAR_H-1, NULL);
        LineTo(memDC, rc.right, S_TOOLBAR_H-1);
        SelectObject(memDC, oldPen); DeleteObject(sep);

        /* Search area bg */
        if (g_searchVisible) {
            RECT sr = { 0, S_TOOLBAR_H, rc.right, S_TOOLBAR_H + S_SEARCH_H };
            FillRect(memDC, &sr, g_brBgDark);
        }

        /* Main list */
        PaintList(memDC);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBm);
        DeleteObject(memBm);
        DeleteDC(memDC);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND: return 1; /* handled in WM_PAINT */

    case WM_LBUTTONDOWN: {
        int mx = LOWORD(lParam), my = HIWORD(lParam);
        int row = HitTestRow(my);
        int totalRows2 = g_grouped ? g_groupCount : g_visCount;
        if (row >= 0 && row < totalRows2) {
            if (HitTestCheckbox(mx, my)) {
                BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                if (shift && g_lastCheckedRow >= 0 && g_lastCheckedRow < totalRows2) {
                    /* Shift-select: check all rows between lastChecked and current */
                    int from = g_lastCheckedRow < row ? g_lastCheckedRow : row;
                    int to   = g_lastCheckedRow > row ? g_lastCheckedRow : row;
                    for (int r = from; r <= to; r++) {
                        if (g_grouped) {
                            GroupInfo *g = &g_groups[r];
                            g->checked = TRUE;
                            for (int i = 0; i < g->pidCount; i++)
                                for (int j = 0; j < g_visCount; j++)
                                    if (g_visible[j]->pid == g->pids[i])
                                        g_visible[j]->checked = TRUE;
                        } else {
                            g_visible[r]->checked = TRUE;
                        }
                    }
                } else {
                    /* Normal single toggle */
                    if (g_grouped) {
                        GroupInfo *g = &g_groups[row];
                        g->checked = !g->checked;
                        for (int i = 0; i < g->pidCount; i++)
                            for (int j = 0; j < g_visCount; j++)
                                if (g_visible[j]->pid == g->pids[i])
                                    g_visible[j]->checked = g->checked;
                    } else {
                        g_visible[row]->checked = !g_visible[row]->checked;
                    }
                }
                g_lastCheckedRow = row;
                InvalidateRect(GetDlgItem(hWnd, IDC_BTN_KILL), NULL, TRUE);
            }
            g_focusRow = row;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        SetFocus(hWnd); /* ensure keyboard works after click */
        return 0;
    }

    case WM_MOUSEMOVE: {
        int my = HIWORD(lParam);
        int oldHover = g_hoverRow;
        g_hoverRow = HitTestRow(my);
        if (g_hoverRow != oldHover)
            InvalidateRect(hWnd, NULL, FALSE);
        /* Track mouse leave */
        TRACKMOUSEEVENT tme = { .cbSize=sizeof(tme), .dwFlags=TME_LEAVE, .hwndTrack=hWnd };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        g_hoverRow = -1;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int lines = delta / 120 * 3;
        g_scrollPos -= lines;
        int totalR = g_grouped ? g_groupCount : g_visCount;
        int maxS = totalR - VisibleRows();
        if (maxS < 0) maxS = 0;
        if (g_scrollPos < 0) g_scrollPos = 0;
        if (g_scrollPos > maxS) g_scrollPos = maxS;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BTN_KILL: KillChecked(); break;
        case IDC_BTN_FIND: ToggleSearch(); break;
        case IDC_BTN_SEARCH_X: ToggleSearch(); break;
        case IDC_BTN_REFRESH: RefreshProcs(); InvalidateRect(hWnd,NULL,TRUE); break;
        case IDC_BTN_MIN_TB: ShowWindow(hWnd,SW_MINIMIZE); break;
        case IDC_BTN_MIN_TRAY: AddTray(); ShowWindow(hWnd,SW_HIDE); break;
        case IDC_BTN_EXIT: DeleteReviverTask(); RemoveTray(); DestroyWindow(hWnd); break;
        case IDC_SORT_RAM:
            if (g_sortCol==2) g_sortAsc=!g_sortAsc; else {g_sortCol=2;g_sortAsc=FALSE;}
            RefreshProcs(); InvalidateRect(hWnd,NULL,TRUE);
            InvalidateRect(GetDlgItem(hWnd,IDC_SORT_RAM),NULL,TRUE);
            InvalidateRect(GetDlgItem(hWnd,IDC_SORT_CPU),NULL,TRUE);
            break;
        case IDC_SORT_CPU:
            if (g_sortCol==1) g_sortAsc=!g_sortAsc; else {g_sortCol=1;g_sortAsc=FALSE;}
            RefreshProcs(); InvalidateRect(hWnd,NULL,TRUE);
            InvalidateRect(GetDlgItem(hWnd,IDC_SORT_RAM),NULL,TRUE);
            InvalidateRect(GetDlgItem(hWnd,IDC_SORT_CPU),NULL,TRUE);
            break;
        case IDC_BTN_GROUP:
            g_grouped = !g_grouped;
            g_focusRow = -1; g_scrollPos = 0;
            RefreshProcs(); InvalidateRect(hWnd,NULL,TRUE);
            InvalidateRect(GetDlgItem(hWnd,IDC_BTN_GROUP),NULL,TRUE);
            break;
        case IDC_BTN_ZOOM_IN:
            g_scale += 0.1f;
            if (g_scale > 3.0f) g_scale = 3.0f;
            ApplyScale(); RebuildFonts();
            SendMessageW(g_hSearch, WM_SETFONT, (WPARAM)g_font, TRUE);
            SendMessageW(hWnd, WM_SIZE, 0, 0);
            InvalidateRect(hWnd, NULL, TRUE);
            break;
        case IDC_BTN_ZOOM_OUT:
            g_scale -= 0.1f;
            if (g_scale < 0.5f) g_scale = 0.5f;
            ApplyScale(); RebuildFonts();
            SendMessageW(g_hSearch, WM_SETFONT, (WPARAM)g_font, TRUE);
            SendMessageW(hWnd, WM_SIZE, 0, 0);
            InvalidateRect(hWnd, NULL, TRUE);
            break;
        case IDM_TRAY_SHOW: ShowWindow(hWnd,SW_SHOW); SetForegroundWindow(hWnd); RemoveTray(); break;
        case IDM_TRAY_EXIT: DeleteReviverTask(); RemoveTray(); DestroyWindow(hWnd); break;
        }
        return 0;

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di=(DRAWITEMSTRUCT *)lParam;
        switch((int)wParam) {
        case IDC_BTN_KILL:     DrawIconBtn(di,10,TRUE); break;
        case IDC_BTN_FIND:     DrawIconBtn(di,0,FALSE); break;
        case IDC_BTN_REFRESH:  DrawIconBtn(di,1,FALSE); break;
        case IDC_BTN_MIN_TB:   DrawIconBtn(di,2,FALSE); break;
        case IDC_BTN_MIN_TRAY: DrawIconBtn(di,3,FALSE); break;
        case IDC_BTN_EXIT:     DrawIconBtn(di,4,FALSE); break;
        case IDC_SORT_RAM:     DrawSortBtn(di,6,L"RAM",2); break;
        case IDC_SORT_CPU:     DrawSortBtn(di,5,L"CPU",1); break;
        case IDC_BTN_GROUP: {
            BOOL hot2 = (di->itemState & ODS_SELECTED);
            if (!hot2) { POINT pt2; GetCursorPos(&pt2); ScreenToClient(di->hwndItem, &pt2);
                if (PtInRect(&di->rcItem, pt2)) hot2=TRUE; }
            HBRUSH br2 = CreateSolidBrush(hot2 ? C_BG_HOVER : C_BG_BAR);
            FillRect(di->hDC, &di->rcItem, br2); DeleteObject(br2);
            if (g_ico[7]) {
                int sz2 = SC(TB_ICON_SZ);
                int bw2 = di->rcItem.right - di->rcItem.left;
                int bh2 = di->rcItem.bottom - di->rcItem.top;
                DrawIconEx(di->hDC, di->rcItem.left + (bw2-sz2)/2,
                           di->rcItem.top + (bh2-sz2)/2,
                           g_ico[7], sz2, sz2, 0, NULL, DI_NORMAL);
            }
            if (g_grouped) {
                RECT ind = { di->rcItem.left + 2, di->rcItem.bottom - 3,
                             di->rcItem.right - 2, di->rcItem.bottom - 1 };
                HBRUSH indBr = CreateSolidBrush(C_ACCENT);
                FillRect(di->hDC, &ind, indBr); DeleteObject(indBr);
            }
            break;
        }
        case IDC_BTN_SEARCH_X: {
            HBRUSH br=CreateSolidBrush(C_BG_HOVER);
            FillRect(di->hDC,&di->rcItem,br); DeleteObject(br);
            SetBkMode(di->hDC,TRANSPARENT); SetTextColor(di->hDC,C_FG);
            SelectObject(di->hDC,g_fontBold);
            DrawTextW(di->hDC,L"\xD7",1,&di->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            break;
        }
        case IDC_BTN_ZOOM_OUT:
        case IDC_BTN_ZOOM_IN: {
            BOOL hot3 = (di->itemState & ODS_SELECTED);
            if (!hot3) { POINT pt3; GetCursorPos(&pt3); ScreenToClient(di->hwndItem, &pt3);
                if (PtInRect(&di->rcItem, pt3)) hot3=TRUE; }
            HBRUSH br3 = CreateSolidBrush(hot3 ? C_BG_HOVER : C_BG_BAR);
            FillRect(di->hDC, &di->rcItem, br3); DeleteObject(br3);
            SetBkMode(di->hDC, TRANSPARENT);
            SetTextColor(di->hDC, C_FG);
            SelectObject(di->hDC, g_fontBold);
            const WCHAR *label = ((int)wParam == IDC_BTN_ZOOM_IN) ? L"+" : L"\x2013";
            DrawTextW(di->hDC, label, -1, &di->rcItem,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            break;
        }
        }
        return TRUE;
    }

    case WM_CTLCOLOREDIT:
        if ((HWND)lParam==g_hSearch) {
            HDC hdc=(HDC)wParam;
            SetTextColor(hdc, C_FG); SetBkColor(hdc, C_BG_BAR);
            static HBRUSH hBrS=NULL;
            if (!hBrS) hBrS=CreateSolidBrush(C_BG_BAR);
            return (LRESULT)hBrS;
        }
        break;

    case WM_KEYDOWN: {
        BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        /* Ctrl+F = search */
        if (ctrl && wParam == 'F') {
            if (!g_searchVisible) ToggleSearch(); return 0;
        }
        /* Ctrl+K = kill checked */
        if (ctrl && wParam == 'K') { KillChecked(); return 0; }
        /* Ctrl+R = force refresh */
        if (ctrl && wParam == 'R') {
            g_paused = FALSE;
            RefreshProcs(); InvalidateRect(hWnd, NULL, TRUE);
            /* Update title to remove [PAUSED] */
            SetWindowTextW(hWnd, L"gKill");
            return 0;
        }
        /* Ctrl+S = pause/resume refresh */
        if (ctrl && wParam == 'S') {
            g_paused = !g_paused;
            SetWindowTextW(hWnd, g_paused ? L"gKill [PAUSED]" : L"gKill");
            return 0;
        }
        /* Ctrl+G = toggle grouped view */
        if (ctrl && wParam == 'G') {
            g_grouped = !g_grouped;
            g_focusRow = -1; g_scrollPos = 0;
            RefreshProcs(); InvalidateRect(hWnd, NULL, TRUE);
            InvalidateRect(GetDlgItem(hWnd, IDC_BTN_GROUP), NULL, TRUE);
            return 0;
        }
        /* Ctrl+Plus/Minus/0 = zoom */
        if (ctrl && (wParam == VK_OEM_PLUS || wParam == VK_ADD)) {
            g_scale += 0.1f;
            if (g_scale > 3.0f) g_scale = 3.0f;
            ApplyScale(); RebuildFonts(); RebuildIcons();
            SendMessageW(g_hSearch, WM_SETFONT, (WPARAM)g_font, TRUE);
            InvalidateRect(hWnd, NULL, TRUE);
            return 0;
        }
        if (ctrl && (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT)) {
            g_scale -= 0.1f;
            if (g_scale < 0.5f) g_scale = 0.5f;
            ApplyScale(); RebuildFonts(); RebuildIcons();
            SendMessageW(g_hSearch, WM_SETFONT, (WPARAM)g_font, TRUE);
            InvalidateRect(hWnd, NULL, TRUE);
            return 0;
        }
        if (ctrl && (wParam == '0' || wParam == VK_NUMPAD0)) {
            g_scale = 1.0f;
            ApplyScale(); RebuildFonts(); RebuildIcons();
            SendMessageW(g_hSearch, WM_SETFONT, (WPARAM)g_font, TRUE);
            InvalidateRect(hWnd, NULL, TRUE);
            return 0;
        }
        /* Delete = kill checked */
        if (wParam == VK_DELETE) { KillChecked(); return 0; }

        /* Arrow keys = navigate focus */
        if (wParam == VK_DOWN) {
            int total = g_grouped ? g_groupCount : g_visCount;
            if (total > 0) {
                if (g_focusRow < 0) g_focusRow = 0;
                else if (g_focusRow < total - 1) g_focusRow++;
                EnsureFocusVisible();
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }
        if (wParam == VK_UP) {
            int total = g_grouped ? g_groupCount : g_visCount;
            if (total > 0) {
                if (g_focusRow < 0) g_focusRow = 0;
                else if (g_focusRow > 0) g_focusRow--;
                EnsureFocusVisible();
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }
        /* Page Down / Page Up */
        if (wParam == VK_NEXT) {
            int vis = VisibleRows();
            if (g_focusRow < 0) g_focusRow = 0;
            g_focusRow += vis;
            if (g_focusRow >= g_visCount) g_focusRow = g_visCount - 1;
            { int total = g_grouped ? g_groupCount : g_visCount;
              if (g_focusRow >= total) g_focusRow = total - 1; }
            EnsureFocusVisible();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        if (wParam == VK_PRIOR) {
            int vis = VisibleRows();
            if (g_focusRow < 0) g_focusRow = 0;
            g_focusRow -= vis;
            if (g_focusRow < 0) g_focusRow = 0;
            EnsureFocusVisible();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        /* Home / End */
        if (wParam == VK_HOME) {
            g_focusRow = 0; EnsureFocusVisible();
            InvalidateRect(hWnd, NULL, FALSE); return 0;
        }
        if (wParam == VK_END) {
            int total = g_grouped ? g_groupCount : g_visCount;
            g_focusRow = total - 1; EnsureFocusVisible();
            InvalidateRect(hWnd, NULL, FALSE); return 0;
        }
        /* Space = toggle checkbox on focused row */
        if (wParam == VK_SPACE) {
            int total = g_grouped ? g_groupCount : g_visCount;
            if (g_focusRow >= 0 && g_focusRow < total) {
                BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                if (shift && g_lastCheckedRow >= 0 && g_lastCheckedRow < total) {
                    int from = g_lastCheckedRow < g_focusRow ? g_lastCheckedRow : g_focusRow;
                    int to   = g_lastCheckedRow > g_focusRow ? g_lastCheckedRow : g_focusRow;
                    for (int r = from; r <= to; r++) {
                        if (g_grouped) {
                            GroupInfo *gg = &g_groups[r];
                            gg->checked = TRUE;
                            for (int i = 0; i < gg->pidCount; i++)
                                for (int j = 0; j < g_visCount; j++)
                                    if (g_visible[j]->pid == gg->pids[i])
                                        g_visible[j]->checked = TRUE;
                        } else {
                            g_visible[r]->checked = TRUE;
                        }
                    }
                } else {
                    if (g_grouped) {
                        GroupInfo *gg = &g_groups[g_focusRow];
                        gg->checked = !gg->checked;
                        for (int i = 0; i < gg->pidCount; i++)
                            for (int j = 0; j < g_visCount; j++)
                                if (g_visible[j]->pid == gg->pids[i])
                                    g_visible[j]->checked = gg->checked;
                    } else {
                        g_visible[g_focusRow]->checked = !g_visible[g_focusRow]->checked;
                    }
                }
                g_lastCheckedRow = g_focusRow;
                InvalidateRect(GetDlgItem(hWnd, IDC_BTN_KILL), NULL, TRUE);
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }
        break;
    }

    case WM_TRAYICON:
        if (lParam==WM_LBUTTONUP) {
            ShowWindow(hWnd,SW_SHOW); SetForegroundWindow(hWnd); RemoveTray();
        } else if (lParam==WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            HMENU hm=CreatePopupMenu();
            AppendMenuW(hm,MF_STRING,IDM_TRAY_SHOW,L"Show gKill");
            AppendMenuW(hm,MF_SEPARATOR,0,NULL);
            AppendMenuW(hm,MF_STRING,IDM_TRAY_EXIT,L"Exit");
            SetForegroundWindow(hWnd);
            TrackPopupMenu(hm,TPM_RIGHTBUTTON,pt.x,pt.y,0,hWnd,NULL);
            DestroyMenu(hm);
        }
        return 0;

    case WM_CLOSE: ShowWindow(hWnd, SW_MINIMIZE); return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        mmi->ptMinTrackSize.x = 500;
        mmi->ptMinTrackSize.y = 300;
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hWnd,IDT_REFRESH); RemoveTray();
        if (g_procs) free(g_procs);
        if (g_prevProcs) free(g_prevProcs);
        if (g_visible) free(g_visible);
        if (g_groups) {
            for (int i = 0; i < g_groupCount; i++)
                if (g_groups[i].pids) free(g_groups[i].pids);
            free(g_groups);
        }
        DeleteObject(g_font); DeleteObject(g_fontBold); DeleteObject(g_fontHeader);
        DeleteObject(g_brBg); DeleteObject(g_brBgDark); DeleteObject(g_brBgAlt);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

/* ── CLI kill mode ──
 * Usage: gkill.exe <name_or_path> [name_or_path2] ...
 * Kills all matching processes aggressively, then exits.
 * Matches by exe name (e.g. "firefox.exe") or full path.
 * Excludes own PID so "gkill.exe gkill.exe" works safely.
 */
static int CliKill(int argc, LPWSTR *argv) {
    EnableDebugPriv();
    LoadNtFuncs();
    DWORD myPid = GetCurrentProcessId();
    int killed = 0;

    for (int a = 1; a < argc; a++) {
        WCHAR *target = argv[a];
        /* Strip quotes if present */
        if (target[0] == L'"') {
            target++;
            int len = (int)wcslen(target);
            if (len > 0 && target[len-1] == L'"') target[len-1] = L'\0';
        }

        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap == INVALID_HANDLE_VALUE) continue;

        PROCESSENTRY32W pe = { .dwSize = sizeof(pe) };
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID == myPid) continue;
                if (pe.th32ProcessID == 0) continue;

                BOOL match = FALSE;

                /* Match by exe name */
                if (_wcsicmp(pe.szExeFile, target) == 0) match = TRUE;

                /* Match by filename extracted from target path */
                if (!match) {
                    const WCHAR *tFilename = wcsrchr(target, L'\\');
                    if (!tFilename) tFilename = wcsrchr(target, L'/');
                    if (tFilename) tFilename++; else tFilename = target;
                    if (_wcsicmp(pe.szExeFile, tFilename) == 0) match = TRUE;
                }

                /* Match by full path */
                if (!match) {
                    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                        FALSE, pe.th32ProcessID);
                    if (hProc) {
                        WCHAR path[MAX_PATH] = {0};
                        DWORD pathLen = MAX_PATH;
                        if (QueryFullProcessImageNameW(hProc, 0, path, &pathLen)) {
                            if (_wcsicmp(path, target) == 0) match = TRUE;
                        }
                        CloseHandle(hProc);
                    }
                }

                if (match) {
                    KillProcAggressive(pe.th32ProcessID);
                    killed++;
                    /* If killing explorer, schedule restart */
                    if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) {
                        WCHAR cmd2[256];
                        _snwprintf(cmd2, 256,
                            L"cmd.exe /c \"ping -n 6 127.0.0.1 >nul & explorer.exe\"");
                        STARTUPINFOW si2 = { .cb=sizeof(si2),
                            .dwFlags=STARTF_USESHOWWINDOW, .wShowWindow=SW_HIDE };
                        PROCESS_INFORMATION pi2 = {0};
                        CreateProcessW(NULL, cmd2, NULL, NULL, FALSE,
                            CREATE_NO_WINDOW, NULL, NULL, &si2, &pi2);
                        if (pi2.hProcess) CloseHandle(pi2.hProcess);
                        if (pi2.hThread) CloseHandle(pi2.hThread);
                    }
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    return killed;
}

/* ── Reviver task helpers ── */
static void RunSilentCmd(const WCHAR *cmdStr) {
    WCHAR cmd[1024];
    wcsncpy(cmd, cmdStr, 1023);
    STARTUPINFOW si = { .cb=sizeof(si), .dwFlags=STARTF_USESHOWWINDOW,
                        .wShowWindow=SW_HIDE };
    PROCESS_INFORMATION pi = {0};
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE,
            CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

static void CreateReviverTask(void) {
    WCHAR myPath[MAX_PATH] = {0};
    DWORD pathLen = GetModuleFileNameW(NULL, myPath, MAX_PATH);
    if (pathLen == 0 || pathLen >= MAX_PATH) return;

    /* Delete old task */
    WCHAR delCmd[256];
    _snwprintf(delCmd, 256, L"schtasks /delete /tn \"gKill-Reviver\" /f");
    STARTUPINFOW si1 = { .cb=sizeof(si1), .dwFlags=STARTF_USESHOWWINDOW,
                         .wShowWindow=SW_HIDE };
    PROCESS_INFORMATION pi1 = {0};
    if (CreateProcessW(NULL, delCmd, NULL, NULL, FALSE,
            CREATE_NO_WINDOW, NULL, NULL, &si1, &pi1)) {
        WaitForSingleObject(pi1.hProcess, 3000);
        CloseHandle(pi1.hProcess); CloseHandle(pi1.hThread);
    }

    /* Create task that runs gkill.exe --systray-check directly.
     * No bat file = no cmd.exe flash. */
    WCHAR cmd[1024];
    _snwprintf(cmd, 1024,
        L"schtasks /create /tn \"gKill-Reviver\" "
        L"/tr \"'%s' --systray-check\" "
        L"/sc minute /mo 1 /f /rl highest",
        myPath);
    RunSilentCmd(cmd);
}

static void DeleteReviverTask(void) {
    RunSilentCmd(L"schtasks /delete /tn \"gKill-Reviver\" /f");
}

static BOOL IsGkillRunning(void) {
    DWORD myPid = GetCurrentProcessId();
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return FALSE;
    PROCESSENTRY32W pe = { .dwSize = sizeof(pe) };
    BOOL found = FALSE;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID != myPid &&
                _wcsicmp(pe.szExeFile, L"gkill.exe") == 0) {
                found = TRUE;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return found;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmdLine, int nShow) {
    (void)hPrev;
    g_hInst = hInst;

    /* CLI mode: if arguments given, check for flags or kill targets */
    BOOL startToTray = FALSE;
    if (cmdLine && cmdLine[0] != L'\0') {
        int argc;
        LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc > 1) {
            BOOL hasKillTargets = FALSE;
            BOOL systrayCheck = FALSE;
            for (int i = 1; i < argc; i++) {
                if (_wcsicmp(argv[i], L"--systray") == 0 ||
                    _wcsicmp(argv[i], L"-systray") == 0 ||
                    _wcsicmp(argv[i], L"--tray") == 0) {
                    startToTray = TRUE;
                } else if (_wcsicmp(argv[i], L"--systray-check") == 0) {
                    systrayCheck = TRUE;
                } else {
                    hasKillTargets = TRUE;
                }
            }
            /* --systray-check: only start if no other gkill is running */
            if (systrayCheck) {
                if (IsGkillRunning()) {
                    LocalFree(argv);
                    return 0; /* Already running, exit silently */
                }
                startToTray = TRUE;
            }
            /* If there are kill targets (not just flags), do CLI kill */
            if (hasKillTargets && !startToTray) {
                int n = CliKill(argc, argv);
                LocalFree(argv);
                return (n > 0) ? 0 : 1;
            }
        }
        if (argv) LocalFree(argv);
    }

    /* GUI mode */
    SetProcessDPIAware();

    /* Detect system DPI for reference, but start at 1.0x scale.
     * SetProcessDPIAware() means Windows gives us real pixels,
     * so our base sizes are already correct - no need to multiply by DPI. */
    {
        HDC hdc = GetDC(NULL);
        if (hdc) {
            g_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(NULL, hdc);
        }
        g_scale = 1.0f;  /* Base scale - DPI-aware apps get real pixels */
        ApplyScale();
    }

    INITCOMMONCONTROLSEX icc = { .dwSize=sizeof(icc), .dwICC=ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {0};
    wc.cbSize=sizeof(wc); wc.lpfnWndProc=WndProc; wc.hInstance=hInst;
    wc.hCursor=LoadCursorW(NULL,IDC_ARROW); wc.hbrBackground=NULL;
    wc.lpszClassName=L"gKillClass";
    wc.hIcon=(HICON)LoadImageW(hInst,MAKEINTRESOURCEW(IDI_APP),IMAGE_ICON,64,64,LR_DEFAULTCOLOR);
    wc.hIconSm=(HICON)LoadImageW(hInst,MAKEINTRESOURCEW(IDI_APP),IMAGE_ICON,32,32,LR_DEFAULTCOLOR);
    if (!wc.hIcon) wc.hIcon=LoadIconW(NULL,IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm=LoadIconW(NULL,IDI_APPLICATION);
    RegisterClassExW(&wc);

    DWORD style=WS_OVERLAPPEDWINDOW;
    RECT rc={0,0,S_CLIENT_W,S_CLIENT_H};
    AdjustWindowRect(&rc,style,FALSE);
    int winW=rc.right-rc.left, winH=rc.bottom-rc.top;
    int sx=GetSystemMetrics(SM_CXSCREEN), sy=GetSystemMetrics(SM_CYSCREEN);

    /* Create reviver BEFORE window - doesn't depend on GUI */
    CreateReviverTask();

    CreateWindowExW(0, L"gKillClass", L"gKill", style,
        (sx-winW)/2,(sy-winH)/2,winW,winH, NULL,NULL,hInst,NULL);
    if (startToTray) {
        AddTray();
        ShowWindow(g_hWnd, SW_HIDE);
    } else {
        ShowWindow(g_hWnd, nShow);
    }
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessageW(&msg,NULL,0,0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return (int)msg.wParam;
}
