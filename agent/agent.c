// Mewgenics RE agent: version.dll proxy that exposes a localhost TCP console (127.0.0.1:47800)
// for autonomous testing / reverse engineering. Line protocol, one reply line per command.
// Build: agent\build.ps1   Deploy (game CLOSED): agent\deploy.ps1
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")

// Forward every real version.dll export to the system copy.
#pragma comment(linker, "/export:GetFileVersionInfoA=C:\\Windows\\System32\\version.GetFileVersionInfoA")
#pragma comment(linker, "/export:GetFileVersionInfoW=C:\\Windows\\System32\\version.GetFileVersionInfoW")
#pragma comment(linker, "/export:GetFileVersionInfoExA=C:\\Windows\\System32\\version.GetFileVersionInfoExA")
#pragma comment(linker, "/export:GetFileVersionInfoExW=C:\\Windows\\System32\\version.GetFileVersionInfoExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeExA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeExW")
#pragma comment(linker, "/export:VerQueryValueA=C:\\Windows\\System32\\version.VerQueryValueA")
#pragma comment(linker, "/export:VerQueryValueW=C:\\Windows\\System32\\version.VerQueryValueW")
#pragma comment(linker, "/export:VerFindFileA=C:\\Windows\\System32\\version.VerFindFileA")
#pragma comment(linker, "/export:VerFindFileW=C:\\Windows\\System32\\version.VerFindFileW")
#pragma comment(linker, "/export:VerInstallFileA=C:\\Windows\\System32\\version.VerInstallFileA")
#pragma comment(linker, "/export:VerInstallFileW=C:\\Windows\\System32\\version.VerInstallFileW")
#pragma comment(linker, "/export:VerLanguageNameA=C:\\Windows\\System32\\version.VerLanguageNameA")
#pragma comment(linker, "/export:VerLanguageNameW=C:\\Windows\\System32\\version.VerLanguageNameW")
#pragma comment(linker, "/export:GetFileVersionInfoByHandle=C:\\Windows\\System32\\version.GetFileVersionInfoByHandle")

static HMODULE g_self;
static uint64_t g_base;
static FILE *g_log;

static void logf_(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap); fputc('\n', g_log); fflush(g_log);
}

// ---------------- game-thread job execution via WH_GETMESSAGE hook ----------------
typedef uint64_t (__fastcall *fn6)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static struct { fn6 fn; uint64_t a[6]; uint64_t ret; DWORD exc; volatile LONG pending; HANDLE done; } g_job;
static uint64_t g_exc_rip, g_exc_addr;
static int exc_filter(EXCEPTION_POINTERS *ep) { g_job.exc = ep->ExceptionRecord->ExceptionCode; g_exc_rip = (uint64_t)ep->ExceptionRecord->ExceptionAddress; g_exc_addr = ep->ExceptionRecord->NumberParameters > 1 ? ep->ExceptionRecord->ExceptionInformation[1] : 0; return EXCEPTION_EXECUTE_HANDLER; }
static HHOOK g_hook;
static HWND g_hwnd;
static DWORD g_gameTid;
static CRITICAL_SECTION g_jobLock;

static int run_job_seh(void) {
    __try { g_job.ret = g_job.fn(g_job.a[0], g_job.a[1], g_job.a[2], g_job.a[3], g_job.a[4], g_job.a[5]); g_job.exc = 0; }
    __except (exc_filter(GetExceptionInformation())) { return 0; }
    return 1;
}

static LRESULT CALLBACK hook_proc(int code, WPARAM w, LPARAM l) {
    if (code >= 0 && InterlockedCompareExchange(&g_job.pending, 0, 1) == 1) {
        run_job_seh();
        SetEvent(g_job.done);
    }
    return CallNextHookEx(g_hook, code, w, l);
}

static BOOL CALLBACK enum_cb(HWND h, LPARAM l) {
    DWORD pid; GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h)) { char t[64]; GetWindowTextA(h, t, 64); if (strstr(t, "Mewgenics")) { g_hwnd = h; return FALSE; } }
    return TRUE;
}

static int ensure_hook(void) {
    if (g_hook) return 1;
    EnumWindows(enum_cb, 0);
    if (!g_hwnd) return 0;
    g_gameTid = GetWindowThreadProcessId(g_hwnd, NULL);
    g_hook = SetWindowsHookExA(WH_GETMESSAGE, hook_proc, g_self, g_gameTid);
    logf_("hook %p tid %lu hwnd %p", (void *)g_hook, g_gameTid, (void *)g_hwnd);
    return g_hook != NULL;
}

// returns 1 ok, 0 exception, -1 timeout/no hook
static volatile LONG g_frame_driven;
static int call_on_game_thread(fn6 fn, uint64_t *a, uint64_t *ret, DWORD *exc) {
    if (!g_frame_driven && !ensure_hook()) return -1;
    EnterCriticalSection(&g_jobLock);
    g_job.fn = fn; memcpy(g_job.a, a, sizeof(g_job.a)); g_job.exc = 0; g_job.ret = 0;
    ResetEvent(g_job.done);
    InterlockedExchange(&g_job.pending, 1);
    for (int i = 0; i < 20; i++) { if (g_hwnd) PostMessageA(g_hwnd, WM_NULL, 0, 0); if (WaitForSingleObject(g_job.done, 100) == WAIT_OBJECT_0) break; }
    int ok = (WaitForSingleObject(g_job.done, 0) == WAIT_OBJECT_0);
    if (!ok) { InterlockedExchange(&g_job.pending, 0); LeaveCriticalSection(&g_jobLock); return -1; }
    *ret = g_job.ret; *exc = g_job.exc;
    LeaveCriticalSection(&g_jobLock);
    return g_job.exc ? 0 : 1;
}

// ---------------- helpers ----------------
static uint64_t parse_addr(const char *s) {
    if (s[0] == 'r' && s[1] == ':') return g_base + _strtoui64(s + 2, NULL, 16);
    return _strtoui64(s, NULL, 16);
}
static int safe_read(uint64_t a, void *dst, size_t n) {
    __try { memcpy(dst, (void *)a, n); return 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static int safe_write(uint64_t a, const void *src, size_t n) {
    DWORD old; if (!VirtualProtect((void *)a, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    int ok = 1; __try { memcpy((void *)a, src, n); } __except (EXCEPTION_EXECUTE_HANDLER) { ok = 0; }
    VirtualProtect((void *)a, n, old, &old); FlushInstructionCache(GetCurrentProcess(), (void *)a, n); return ok;
}
static int hexval(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; }

static void sendstr(SOCKET s, const char *str) { send(s, str, (int)strlen(str), 0); send(s, "\n", 1, 0); }

// Scan the main module for a pattern like "48 8B ?? 10 E8". Returns up to max RVAs.
static int scan_module(const char *pat, uint64_t *out, int max) {
    uint8_t bytes[256]; uint8_t mask[256]; int n = 0;
    for (const char *p = pat; *p && n < 256;) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (p[0] == '?') { bytes[n] = 0; mask[n++] = 0; p += (p[1] == '?') ? 2 : 1; }
        else { int h = hexval(p[0]), l = hexval(p[1]); if (h < 0 || l < 0) return -1; bytes[n] = (uint8_t)(h * 16 + l); mask[n++] = 1; p += 2; }
    }
    if (!n) return -1;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)g_base; IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(g_base + dos->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt); int found = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections && found < max; i++) {
        uint8_t *s = (uint8_t *)(g_base + sec[i].VirtualAddress); size_t sz = sec[i].Misc.VirtualSize;
        if (sz < (size_t)n) continue;
        __try {
            for (size_t o = 0; o + n <= sz && found < max; o++) {
                int m = 1; for (int k = 0; k < n; k++) if (mask[k] && s[o + k] != bytes[k]) { m = 0; break; }
                if (m) out[found++] = (uint64_t)(s + o) - g_base;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return found;
}


// ---------------- SDL input injection (RVAs for build 1.1.21239; override with `sdl <name> <rva>`) ----------------
static uint64_t RVA_SDL_PushEvent = 0xBA77E0, RVA_SDL_WarpMouseInWindow = 0xBA8D30, RVA_SDL_GetKeyboardFocus = 0xBA61F0;
static uint64_t RVA_ONSYSEV = 0xA2D0E0;
static uint64_t RVA_SETTINGS_PTR = 0x13C4A30;   // *(base+this)+3536 == SDL_Window*
typedef int (__fastcall *push_t)(void *);
typedef void (__fastcall *warp_t)(void *, float, float);
typedef void *(__fastcall *focus_t)(void);


// ---- polled-state injection: patch the dynapi jump-table slots of SDL_GetMouseState / SDL_GetKeyboardState ----
typedef uint32_t (__fastcall *getmouse_t)(float *, float *);
typedef const uint8_t *(__fastcall *getkbd_t)(int *);
static uint64_t RVA_SDL_GetMouseState = 0xBA62D0, RVA_SDL_GetKeyboardState = 0xBA6210;
static volatile uint32_t g_btnmask; static volatile uint8_t g_keyheld[512];
static getmouse_t o_mouse; static getkbd_t o_kbd; static int g_hooked;
static volatile float g_mx, g_my; static volatile int g_mvalid;
static uint32_t __fastcall h_mouse(float *x, float *y) { float a, b; uint32_t r = o_mouse(&a, &b); if (g_mvalid) { a = g_mx; b = g_my; } if (x) *x = a; if (y) *y = b; return r | g_btnmask; }
static const uint8_t *__fastcall h_kbd(int *n) {
    uint8_t *a = (uint8_t *)o_kbd(n);
    if (a) for (int i = 0; i < 512; i++) if (g_keyheld[i]) a[i] = 1;
    return a;
}
static uint64_t *slot_of(uint64_t stub) { int32_t d; if (!safe_read(stub + 3, &d, 4)) return NULL; return (uint64_t *)(stub + 7 + d); }
static int install_state_hooks(void) {
    if (g_hooked) return 1;
    uint64_t *sm = slot_of(g_base + RVA_SDL_GetMouseState), *sk = slot_of(g_base + RVA_SDL_GetKeyboardState); if (!sm || !sk) return 0;
    DWORD old; VirtualProtect(sm, 8, PAGE_READWRITE, &old); o_mouse = (getmouse_t)*sm; *sm = (uint64_t)h_mouse; VirtualProtect(sm, 8, old, &old);
    VirtualProtect(sk, 8, PAGE_READWRITE, &old); o_kbd = (getkbd_t)*sk; *sk = (uint64_t)h_kbd; VirtualProtect(sk, 8, old, &old);
    g_hooked = 1; logf_("state hooks installed"); return 1;
}
static void *get_sdl_window(void) {
    uint64_t obj = 0, win = 0;
    if (safe_read(g_base + RVA_SETTINGS_PTR, &obj, 8) && obj && safe_read(obj + 3536, &win, 8) && win) return (void *)win;
    return ((focus_t)(g_base + RVA_SDL_GetKeyboardFocus))();
}
static int push_event(uint8_t *ev) { __try { return ((push_t)(g_base + RVA_SDL_PushEvent))(ev); } __except (EXCEPTION_EXECUTE_HANDLER) { return -99; } }
static int inject_key(uint32_t scancode, uint32_t keycode, int down) { (void)keycode; install_state_hooks(); if (scancode < 512) { g_keyheld[scancode] = (uint8_t)down; if (!down && o_kbd) { uint8_t *a = (uint8_t *)o_kbd(NULL); if (a) a[scancode] = 0; } } return 1; }
static void client_size(float *w, float *h) {
    RECT r = {0}; if (!g_hwnd) EnumWindows(enum_cb, 0); if (g_hwnd) GetClientRect(g_hwnd, &r); *w = (float)r.right; *h = (float)r.bottom;
}
static int inject_motion(float x, float y) {
    install_state_hooks(); g_mx = x; g_my = y; g_mvalid = 1;
    
    uint8_t ev[128]; memset(ev, 0, sizeof ev); *(uint32_t *)ev = 0x400; *(float *)(ev + 28) = x; *(float *)(ev + 32) = y; return push_event(ev);
}
static int inject_button(uint8_t button, int down, float x, float y) { (void)x; (void)y; install_state_hooks(); uint32_t m = 1u << (button - 1); if (down) g_btnmask |= m; else g_btnmask &= ~m; return 1; }

// ---------------- diagnostics: thread dump + in-engine screenshot ----------------
#include <tlhelp32.h>
static void thread_dump(char *out, size_t cap) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)g_base; IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(g_base + dos->e_lfanew);
    uint64_t imgsz = nt->OptionalHeader.SizeOfImage; size_t n = 0; out[0] = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); THREADENTRY32 te; te.dwSize = sizeof te;
    if (snap == INVALID_HANDLE_VALUE) return;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
        HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID); if (!th) continue;
        if (SuspendThread(th) != (DWORD)-1) {
            CONTEXT ctx; memset(&ctx, 0, sizeof ctx); ctx.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(th, &ctx)) {
                n += snprintf(out + n, cap - n, "T%lu%s rip=", te.th32ThreadID, te.th32ThreadID == g_gameTid ? "(main)" : "");
                if (ctx.Rip >= g_base && ctx.Rip < g_base + imgsz) n += snprintf(out + n, cap - n, "r:%llX", (unsigned long long)(ctx.Rip - g_base));
                else { HMODULE m = NULL; char nm[MAX_PATH] = "?"; if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)ctx.Rip, &m)) GetModuleFileNameA(m, nm, MAX_PATH); char *b = strrchr(nm, '\\'); n += snprintf(out + n, cap - n, "%s+%llX", b ? b + 1 : nm, (unsigned long long)(ctx.Rip - (uint64_t)m)); }
                n += snprintf(out + n, cap - n, " bt:");
                int cnt = 0; for (int i = 0; i < 400 && cnt < 14; i++) { uint64_t v = 0; if (!safe_read(ctx.Rsp + i * 8, &v, 8)) break; if (v >= g_base && v < g_base + imgsz) { n += snprintf(out + n, cap - n, " %llX", (unsigned long long)(v - g_base)); cnt++; } }
                n += snprintf(out + n, cap - n, " | ");
            }
            ResumeThread(th);
        }
        CloseHandle(th);
    }
    CloseHandle(snap);
}

static uint32_t crc_tab[256];
static uint32_t crc32_(uint32_t c, const uint8_t *p, size_t n) { if (!crc_tab[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t k = i; for (int j = 0; j < 8; j++) k = k & 1 ? 0xEDB88320u ^ (k >> 1) : k >> 1; crc_tab[i] = k; } c = ~c; while (n--) c = crc_tab[(c ^ *p++) & 255] ^ (c >> 8); return ~c; }
static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }
static void png_chunk(FILE *f, const char *t, const uint8_t *d, uint32_t n) { uint8_t h[8]; be32(h, n); memcpy(h + 4, t, 4); fwrite(h, 1, 8, f); if (n) fwrite(d, 1, n, f); uint32_t c = crc32_(0, (const uint8_t *)t, 4); c = crc32_(c, d, n); uint8_t e[4]; be32(e, c); fwrite(e, 1, 4, f); }
static int write_png(const char *path, const uint8_t *rgb, int w, int h) {
    size_t raw = (size_t)(w * 3 + 1) * h; uint8_t *buf = (uint8_t *)malloc(raw); if (!buf) return 0;
    for (int y = 0; y < h; y++) { buf[(size_t)y * (w * 3 + 1)] = 0; memcpy(buf + (size_t)y * (w * 3 + 1) + 1, rgb + (size_t)y * w * 3, (size_t)w * 3); }
    size_t zs = 2 + raw + (raw / 65535 + 1) * 5 + 4; uint8_t *z = (uint8_t *)malloc(zs); size_t zp = 0; z[zp++] = 0x78; z[zp++] = 1;
    uint32_t a = 1, b = 0; for (size_t i = 0; i < raw; i++) { a = (a + buf[i]) % 65521; b = (b + a) % 65521; }
    for (size_t o = 0; o < raw;) { size_t n = raw - o > 65535 ? 65535 : raw - o; z[zp++] = (o + n >= raw); z[zp++] = n & 255; z[zp++] = n >> 8; z[zp++] = ~n & 255; z[zp++] = (~n >> 8) & 255; memcpy(z + zp, buf + o, n); zp += n; o += n; }
    be32(z + zp, (b << 16) | a); zp += 4;
    FILE *f = fopen(path, "wb"); if (!f) { free(buf); free(z); return 0; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f); uint8_t ih[13]; be32(ih, w); be32(ih + 4, h); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    png_chunk(f, "IHDR", ih, 13); png_chunk(f, "IDAT", z, (uint32_t)zp); png_chunk(f, "IEND", NULL, 0); fclose(f); free(buf); free(z); return 1;
}
// Runs on the game thread (GL context current there). a0=path a1=downscale a2=0 back / 1 front buffer. Returns packed w<<32|h or 0.
typedef void (__stdcall *glReadPixels_t)(int, int, int, int, unsigned, unsigned, void *);
typedef void (__stdcall *glReadBuffer_t)(unsigned);
typedef void (__stdcall *glPixelStorei_t)(unsigned, int);
static uint64_t __fastcall shot_job(uint64_t path, uint64_t ds, uint64_t front, uint64_t d, uint64_t e, uint64_t f) {
    (void)d; (void)e; (void)f; HMODULE gl = GetModuleHandleA("opengl32.dll"); if (!gl) return 0;
    glReadPixels_t rp = (glReadPixels_t)GetProcAddress(gl, "glReadPixels"); glReadBuffer_t rb = (glReadBuffer_t)GetProcAddress(gl, "glReadBuffer"); glPixelStorei_t ps = (glPixelStorei_t)GetProcAddress(gl, "glPixelStorei");
    if (!g_hwnd) EnumWindows(enum_cb, 0);   // frame-driven (core) mode never installs the message hook that sets it
    RECT r; GetClientRect(g_hwnd, &r); int w = r.right, h = r.bottom; if (w <= 0 || h <= 0) return 0; if (ds < 1) ds = 1;
    uint8_t *px = (uint8_t *)malloc((size_t)w * h * 3); ps(0x0D05, 1); rb(front ? 0x0404 : 0x0405); rp(0, 0, w, h, 0x1907, 0x1401, px);
    int ow = w / (int)ds, oh = h / (int)ds; uint8_t *o = (uint8_t *)malloc((size_t)ow * oh * 3);
    for (int y = 0; y < oh; y++) for (int x = 0; x < ow; x++) memcpy(o + ((size_t)y * ow + x) * 3, px + ((size_t)(h - 1 - y * (int)ds) * w + x * (int)ds) * 3, 3);
    int ok = write_png((const char *)path, o, ow, oh); free(px); free(o); return ok ? ((uint64_t)ow << 32 | (uint32_t)oh) : 0;
}

// ---- list every window (and child text) owned by the game: reveals hidden modal/assert dialogs ----
static char *g_wbuf; static size_t g_wn, g_wcap;
static BOOL CALLBACK child_cb(HWND h, LPARAM l) { (void)l; char t[1500] = "", c[64] = ""; GetWindowTextA(h, t, sizeof t); GetClassNameA(h, c, sizeof c); for (char *p = t; *p; p++) if (*p == '\n' || *p == '\r') *p = ' '; if (g_wn + 1700 < g_wcap) g_wn += snprintf(g_wbuf + g_wn, g_wcap - g_wn, "  child[%s]='%s'", c, t); return TRUE; }
static BOOL CALLBACK top_cb(HWND h, LPARAM l) { (void)l; DWORD pid; GetWindowThreadProcessId(h, &pid); if (pid != GetCurrentProcessId()) return TRUE; char t[300] = "", c[64] = ""; GetWindowTextA(h, t, sizeof t); GetClassNameA(h, c, sizeof c); if (g_wn + 400 < g_wcap) g_wn += snprintf(g_wbuf + g_wn, g_wcap - g_wn, " | TOP %llx vis=%d cls=%s title='%s'", (unsigned long long)h, IsWindowVisible(h), c, t); EnumChildWindows(h, child_cb, 0); return TRUE; }
static int (*g_ext_handler)(SOCKET s, char *line);   // set by the core: returns 1 when it handled the line
static void handle(SOCKET s, char *line) {
    if (g_ext_handler && g_ext_handler(s, line)) return;
    char out[65536]; char *tok[16]; int nt = 0;
    for (char *p = strtok(line, " \t\r\n"); p && nt < 16; p = strtok(NULL, " \t\r\n")) tok[nt++] = p;
    if (!nt) { sendstr(s, "ok"); return; }
    const char *c = tok[0];
    if (!strcmp(c, "ping")) { snprintf(out, sizeof out, "ok pid=%lu base=%llx", GetCurrentProcessId(), (unsigned long long)g_base); sendstr(s, out); }
    else if (!strcmp(c, "rd") && nt >= 3) {
        uint64_t a = parse_addr(tok[1]); int n = atoi(tok[2]); if (n < 1 || n > 16384) n = 16;
        uint8_t *buf = (uint8_t *)malloc(n);
        if (!safe_read(a, buf, n)) sendstr(s, "err unreadable");
        else { char *o = (char *)malloc(n * 2 + 8); strcpy(o, "ok "); for (int i = 0; i < n; i++) sprintf(o + 3 + i * 2, "%02x", buf[i]); sendstr(s, o); free(o); }
        free(buf);
    }
    else if (!strcmp(c, "wr") && nt >= 3) {
        uint64_t a = parse_addr(tok[1]); size_t n = strlen(tok[2]) / 2; uint8_t *buf = (uint8_t *)malloc(n);
        for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)(hexval(tok[2][i * 2]) * 16 + hexval(tok[2][i * 2 + 1]));
        sendstr(s, safe_write(a, buf, n) ? "ok" : "err write"); free(buf);
    }
    else if (!strcmp(c, "call") && nt >= 2) {   // call <addr|r:rva> [a1..a6]   (runs on the game thread)
        uint64_t a[6] = {0}; for (int i = 2; i < nt && i < 8; i++) a[i - 2] = parse_addr(tok[i]);
        uint64_t ret = 0; DWORD exc = 0; int r = call_on_game_thread((fn6)parse_addr(tok[1]), a, &ret, &exc);
        if (r == 1) { snprintf(out, sizeof out, "ok %llx", (unsigned long long)ret); sendstr(s, out); }
        else if (r == 0) { snprintf(out, sizeof out, "err exception %lx at %llx (r:%llx) addr %llx", exc, (unsigned long long)g_exc_rip, (unsigned long long)(g_exc_rip - g_base), (unsigned long long)g_exc_addr); sendstr(s, out); }
        else sendstr(s, "err gamethread-timeout");
    }
    else if (!strcmp(c, "callhere") && nt >= 2) {   // same but on the server thread (for pure functions)
        uint64_t a[6] = {0}; for (int i = 2; i < nt && i < 8; i++) a[i - 2] = parse_addr(tok[i]);
        g_job.fn = (fn6)parse_addr(tok[1]); memcpy(g_job.a, a, sizeof a); int ok = run_job_seh();
        if (ok) { snprintf(out, sizeof out, "ok %llx", (unsigned long long)g_job.ret); sendstr(s, out); }
        else { snprintf(out, sizeof out, "err exception %lx", g_job.exc); sendstr(s, out); }
    }
    else if (!strcmp(c, "alloc") && nt >= 2) { void *p = VirtualAlloc(NULL, atoi(tok[1]), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE); snprintf(out, sizeof out, "ok %llx", (unsigned long long)p); sendstr(s, out); }
    else if (!strcmp(c, "scan") && nt >= 2) {
        char pat[512] = ""; for (int i = 1; i < nt; i++) { strcat(pat, tok[i]); strcat(pat, " "); }
        uint64_t res[64]; int n = scan_module(pat, res, 64);
        if (n < 0) sendstr(s, "err bad pattern"); else { strcpy(out, "ok"); for (int i = 0; i < n; i++) sprintf(out + strlen(out), " %llx", (unsigned long long)res[i]); sendstr(s, out); }
    }
    else if (!strcmp(c, "hscan") && nt >= 2) {   // hscan <qword|r:rva> [maxoff] : find 8-aligned occurrences in private RW memory (e.g. object vtables)
        // with maxoff: only the first maxoff bytes of each allocation (game pools put singletons at base+8) -> ~100x faster
        uint64_t v = parse_addr(tok[1]); int n = 0; strcpy(out, "ok"); uint64_t maxoff = nt >= 3 ? strtoull(tok[2], NULL, 16) : 0;
        ULONG_PTR slo, shi; GetCurrentThreadStackLimits(&slo, &shi);   // skip our own stack (holds v)
        MEMORY_BASIC_INFORMATION mbi; uint8_t *p = NULL;
        while (n < 64 && VirtualQuery(p, &mbi, sizeof mbi)) {
            if (mbi.State == MEM_COMMIT && !((uint64_t)mbi.BaseAddress >= slo && (uint64_t)mbi.BaseAddress < shi) && mbi.Type == MEM_PRIVATE && (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) && !(mbi.Protect & PAGE_GUARD)) {
                uint64_t *q = (uint64_t *)mbi.BaseAddress, *e = (uint64_t *)((uint8_t *)mbi.BaseAddress + mbi.RegionSize);
                if (maxoff) { if (mbi.BaseAddress != mbi.AllocationBase) q = e; else if (mbi.RegionSize > maxoff) e = (uint64_t *)((uint8_t *)mbi.BaseAddress + maxoff); }
                __try { for (; q < e && n < 64; q++) if (*q == v) { sprintf(out + strlen(out), " %llx", (unsigned long long)(uint64_t)q); n++; } } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            p = (uint8_t *)mbi.BaseAddress + mbi.RegionSize;
        }
        sendstr(s, out);
    }
    else if (!strcmp(c, "rtti") && nt >= 2) {   // rtti <obj> : MSVC RTTI class name of a polymorphic object
        uint64_t o = parse_addr(tok[1]), vt = 0, col = 0; int32_t td = 0; char nm[200] = "";
        if (safe_read(o, &vt, 8) && safe_read(vt - 8, &col, 8) && safe_read(col + 12, &td, 4)) safe_read(g_base + (uint32_t)td + 16, nm, sizeof nm - 1);
        nm[sizeof nm - 1] = 0; snprintf(out, sizeof out, "ok vt=r:%llx %s", (unsigned long long)(vt - g_base), nm); sendstr(s, out);
    }    else if (!strcmp(c, "tree")) {   // tree [entity|root] [depth=3] : glaiel::Entity tree (children vector at +32/+36/+40), class names, live flags
        uint64_t root = 0, app = 0, sc = 0; int maxd = nt >= 3 ? atoi(tok[2]) : 3;
        if (nt >= 2 && strcmp(tok[1], "root")) root = parse_addr(tok[1]);
        else if (safe_read(g_base + 0x13C5BA8, &app, 8) && safe_read(app + 24, &sc, 8)) safe_read(sc + 8, &root, 8);   // *(*(App+24)+8)
        char *buf = (char *)malloc(60000); size_t n = (size_t)snprintf(buf, 60000, "ok");
        uint64_t stack[64]; int depth[64], sp = 0; stack[sp] = root; depth[sp++] = 0;
        while (sp > 0 && n < 59000) {
            uint64_t e = stack[--sp]; int d = depth[sp]; uint64_t vt = 0, col = 0, arr = 0; int32_t td = 0; uint32_t cnt = 0; uint8_t fl = 0; char nm[120] = "?";
            if (safe_read(e, &vt, 8) && safe_read(vt - 8, &col, 8) && safe_read(col + 12, &td, 4)) { safe_read(g_base + (uint32_t)td + 16, nm, sizeof nm - 1); nm[sizeof nm - 1] = 0; }
            char *p = strstr(nm, "@glaiel"); if (p) *p = 0;
            safe_read(e + 12, &fl, 1); safe_read(e + 36, &cnt, 4); safe_read(e + 40, &arr, 8);
            n += (size_t)snprintf(buf + n, 60000 - n, " | %d:%llX %s f=%02X n=%u", d, (unsigned long long)e, nm[0] == '.' ? nm + 4 : nm, fl, cnt);
            if (d < maxd && cnt < 400) for (int i = (int)cnt - 1; i >= 0 && sp < 64; i--) { uint64_t ch = 0; if (safe_read(arr + 8ull * i, &ch, 8) && ch) { stack[sp] = ch; depth[sp++] = d + 1; } }
        }
        sendstr(s, buf); free(buf);}    else if (!strcmp(c, "wstr") && nt >= 2) {   // read msvc std::wstring / glaiel::wstring at addr (as utf-8)
        uint64_t a = parse_addr(tok[1]); uint64_t size = 0, cap = 0, p = a; safe_read(a + 16, &size, 8); safe_read(a + 24, &cap, 8);
        if (cap > 7) safe_read(a, &p, 8); if (size > 400) size = 400;
        wchar_t w[401] = {0}; char u[1300] = ""; safe_read(p, w, size * 2); WideCharToMultiByte(CP_UTF8, 0, w, (int)size, u, sizeof u - 1, NULL, NULL);
        snprintf(out, sizeof out, "ok %s", u); sendstr(s, out);
    }    else if (!strcmp(c, "str") && nt >= 2) {   // read msvc std::string / glaiel::string at addr
        uint64_t a = parse_addr(tok[1]); uint64_t size = 0, cap = 0; safe_read(a + 16, &size, 8); safe_read(a + 24, &cap, 8);
        if (size > 8000) { sendstr(s, "err not-a-string"); return; }
        uint64_t p = a; if (cap > 15) safe_read(a, &p, 8);
        char *b = (char *)malloc((size_t)size + 8); if (!safe_read(p, b, (size_t)size)) sendstr(s, "err unreadable");
        else { b[size] = 0; for (size_t i = 0; i < size; i++) if (b[i] == '\n' || b[i] == '\r') b[i] = ' '; snprintf(out, sizeof out, "ok %s", b); sendstr(s, out); }
        free(b);
    }
    else if (!strcmp(c, "key") && nt >= 4) {   // key <scancode> <keycode> <down|up|tap>  (decimal)
        uint32_t sc = (uint32_t)atoi(tok[1]), kc = (uint32_t)atoi(tok[2]); int r = 0;
        if (!strcmp(tok[3], "down")) r = inject_key(sc, kc, 1); else if (!strcmp(tok[3], "up")) r = inject_key(sc, kc, 0);
        else { r = inject_key(sc, kc, 1); Sleep(150); r = inject_key(sc, kc, 0); }
        snprintf(out, sizeof out, "ok push=%d", r); sendstr(s, out);
    }
    else if ((!strcmp(c, "mouse") || !strcmp(c, "click") || !strcmp(c, "clickr")) && nt >= 3) {   // fractions 0..1 of the client area
        float w, h; client_size(&w, &h); float x = (float)atof(tok[1]) * w, y = (float)atof(tok[2]) * h;
        int r = inject_motion(x, y);
        if (strcmp(c, "mouse")) { uint8_t b = !strcmp(c, "clickr") ? 3 : 1; Sleep(80); inject_motion(x, y); Sleep(40); inject_button(b, 1, x, y); Sleep(150); r = inject_button(b, 0, x, y); }
        snprintf(out, sizeof out, "ok push=%d at %.0f,%.0f (client %.0fx%.0f)", r, x, y, w, h); sendstr(s, out);
    }
    else if (!strcmp(c, "sdl") && nt >= 3) {   // sdl push|warp|focus <rva-hex>
        uint64_t v = _strtoui64(tok[2], NULL, 16);
        if (!strcmp(tok[1], "push")) RVA_SDL_PushEvent = v; else if (!strcmp(tok[1], "warp")) RVA_SDL_WarpMouseInWindow = v;
        else if (!strcmp(tok[1], "focus")) RVA_SDL_GetKeyboardFocus = v; else if (!strcmp(tok[1], "winptr")) RVA_SETTINGS_PTR = v;
        sendstr(s, "ok");
    }    else if (!strcmp(c, "sysev") && nt >= 2) {   // sysev <hex of SDL_Event (<=128 bytes)> : feed InputCore::OnSystemEvent on the game thread
        static uint8_t evbuf[256]; memset(evbuf, 0, sizeof evbuf); size_t n = strlen(tok[1]) / 2; if (n > 128) n = 128;
        for (size_t i = 0; i < n; i++) evbuf[i] = (uint8_t)(hexval(tok[1][i * 2]) * 16 + hexval(tok[1][i * 2 + 1]));
        uint64_t a[6] = {0, (uint64_t)evbuf, 0, 0, 0, 0}; uint64_t ret = 0; DWORD exc = 0;
        int r = call_on_game_thread((fn6)(g_base + RVA_ONSYSEV), a, &ret, &exc);
        snprintf(out, sizeof out, r == 1 ? "ok" : (r == 0 ? "err exception %lx" : "err timeout"), exc); sendstr(s, out);
    }    else if (!strcmp(c, "threads")) { char *tb = (char *)malloc(65536); thread_dump(tb, 65536); for (char *p = tb; *p; p++) if (*p == '\n') *p = ' '; snprintf(out, sizeof out, "ok %.60000s", tb); sendstr(s, out); free(tb); }
    else if (!strcmp(c, "shot") && nt >= 2) {   // shot <path> [downscale=2] [front=0]  (game thread, glReadPixels -> PNG)
        static char pth[MAX_PATH]; strncpy(pth, tok[1], MAX_PATH - 1); uint64_t a[6] = {(uint64_t)pth, nt > 2 ? (uint64_t)atoi(tok[2]) : 2, nt > 3 ? (uint64_t)atoi(tok[3]) : 0, 0, 0, 0};
        uint64_t ret = 0; DWORD exc = 0; int r = call_on_game_thread((fn6)shot_job, a, &ret, &exc);
        snprintf(out, sizeof out, r == 1 ? (ret ? "ok %llux%llu" : "err shot-failed") : "err exc/timeout", (unsigned long long)(ret >> 32), (unsigned long long)(ret & 0xffffffff)); sendstr(s, out);
    }    else if (!strcmp(c, "windows")) { g_wcap = 30000; g_wbuf = (char *)malloc(g_wcap); g_wn = 0; g_wbuf[0] = 0; EnumWindows(top_cb, 0); snprintf(out, sizeof out, "ok%s", g_wbuf); sendstr(s, out); free(g_wbuf); }    else if (!strcmp(c, "quit")) { sendstr(s, "ok bye"); ExitProcess(0); }
    else if (!strcmp(c, "help")) sendstr(s, "ok ping rd wr call callhere alloc scan hscan rtti tree wstr str threads windows shot key mouse click clickr sdl quit  (addr: hex or r:RVAHEX)");
    else sendstr(s, "err unknown command");
}

static DWORD WINAPI server(LPVOID p) {
    (void)p; WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    SOCKET ls = socket(AF_INET, SOCK_STREAM, 0); int one = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof one);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons(47800); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(ls, (struct sockaddr *)&a, sizeof a) != 0 || listen(ls, 2) != 0) { logf_("bind failed %d", WSAGetLastError()); return 1; }
    logf_("listening 47800");
    for (;;) {
        SOCKET c = accept(ls, NULL, NULL); if (c == INVALID_SOCKET) continue;
        char buf[8192]; int len = 0;
        for (;;) {
            int n = recv(c, buf + len, (int)sizeof buf - len - 1, 0); if (n <= 0) break; len += n;
            char *nl; buf[len] = 0;
            while ((nl = strchr(buf, '\n')) != NULL) { *nl = 0; handle(c, buf); int rest = len - (int)(nl - buf) - 1; memmove(buf, nl + 1, rest); len = rest; buf[len] = 0; }
            if (len >= (int)sizeof buf - 1) len = 0;
        }
        closesocket(c);
    }
}

// Start the TCP console. Called from DllMain (standalone agent) or from the core.
static void agent_init(HMODULE h, const char *logname) {
    g_self = h; g_base = (uint64_t)GetModuleHandleA(NULL);
    char path[MAX_PATH]; GetModuleFileNameA(h, path, MAX_PATH); char *sl = strrchr(path, '\\'); if (sl) strcpy(sl + 1, logname);
    if (!g_log) g_log = fopen(path, "w");
    logf_("agent loaded, base %llx", (unsigned long long)g_base);
    InitializeCriticalSection(&g_jobLock); g_job.done = CreateEventA(NULL, TRUE, FALSE, NULL);
    CreateThread(NULL, 0, server, NULL, 0, NULL);
}
// Run a pending game-thread job; the core calls this every frame (more reliable than the message hook).
static void agent_frame_tick(void) {
    if (InterlockedCompareExchange(&g_job.pending, 0, 1) == 1) { run_job_seh(); SetEvent(g_job.done); }
}

#ifndef AGENT_LIB
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        char exe[MAX_PATH]; GetModuleFileNameA(NULL, exe, MAX_PATH);
        if (!strstr(exe, "Mewgenics")) return TRUE;   // only act inside the game
        agent_init(h, "agent.log");
    }
    return TRUE;
}
#endif