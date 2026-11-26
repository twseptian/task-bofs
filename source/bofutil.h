/*
 * bofutil.h - shared helpers. No CRT: unresolved CRT symbols break COFF
 * loading, so everything here is hand-written and build.sh uses -fno-builtin
 * (GCC -O2 rewrites strlen-like loops into CRT calls even when the source
 * avoids them; -fno-builtin then exposes ZeroMemory->memset, hence my_memzero).
 */
#ifndef BOFUTIL_H
#define BOFUTIL_H

#include <windows.h>
#include "beacon.h"

static inline SIZE_T my_strlen(const char * s)
{
    SIZE_T n = 0;
    while (s[n]) n++;
    return n;
}

static inline void my_strcpy(char * dst, const char * src, SIZE_T max)
{
    SIZE_T i = 0;
    while (src[i] && i + 1 < max) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static inline void my_strcat(char * dst, const char * src, SIZE_T max)
{
    SIZE_T d = my_strlen(dst), i = 0;
    while (src[i] && d + 1 < max) { dst[d++] = src[i++]; }
    dst[d] = 0;
}

static inline const char * my_strstr(const char * hay, const char * needle)
{
    const char * h, * n;
    if (!needle || !*needle) return hay;
    for (; *hay; hay++) {
        h = hay; n = needle;
        while (*h && *n && (*h == *n)) { h++; n++; }
        if (!*n) return hay;
    }
    return NULL;
}

static inline void my_sanitize(char * dst, const char * src, SIZE_T max)
{
    SIZE_T i = 0;
    for (; src[i] && i + 1 < max; i++) {
        char c = src[i];
        dst[i] = (c == '\\' || c == '/' || c == ':') ? '_' : c;
    }
    dst[i] = 0;
}

static inline void my_memzero(void * p, SIZE_T n)
{
    unsigned char * b = (unsigned char *)p;
    while (n--) *b++ = 0;
}

/*
 * Run a command and capture its stdout. CreateProcessA + anonymous pipes,
 * no cmd.exe child. Two things that bite in the field:
 * - stdin is a pipe holding a blank line, then EOF. schtasks /change asks
 *   for the run-as password when /tr has spaces; with hStdInput=NULL its CRT
 *   falls back to CONIN$ of the invisible console and waits forever.
 * - every command gets a 15s budget (PeekNamedPipe poll + TerminateProcess).
 *   A child that never exits must not wedge the beacon; partial output is
 *   still delivered.
 */
static inline void my_drainpipe(HANDLE rd, char * out, SIZE_T * len, SIZE_T * cap)
{
    DWORD n = 0;
    while (ReadFile(rd, out + *len, (DWORD)(*cap - *len - 1), &n, NULL) && n > 0) {
        *len += (SIZE_T)n;
        out[*len] = 0;
        if (*cap - *len - 1 < 4096) {
            SIZE_T ncap = *cap * 2;
            char * tmp = (char *)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, out, ncap);
            if (!tmp) break;
            out = tmp;
            *cap = ncap;
        }
    }
}

static inline char * CaptureCmdA(const char * cmdline_src)
{
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;
    HANDLE              rd = NULL, wr = NULL;
    HANDLE              rin = NULL, win = NULL;
    char              * out = NULL;
    char                cmdline[1024];
    SIZE_T              cap = 64 * 1024, len = 0;
    DWORD               n = 0;
    DWORD               deadline;
    DWORD               timed_out = 0;

    my_strcpy(cmdline, cmdline_src, sizeof(cmdline));

    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle       = TRUE;

    if (!CreatePipe(&rd, &wr, &sa, 0))
        return NULL;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&rin, &win, &sa, 0)) {
        CloseHandle(rd); CloseHandle(wr);
        return NULL;
    }
    SetHandleInformation(win, HANDLE_FLAG_INHERIT, 0);

    my_memzero(&si, sizeof(si));
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput   = rin;
    si.hStdOutput  = wr;
    si.hStdError   = wr;

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(rd); CloseHandle(wr);
        CloseHandle(rin); CloseHandle(win);
        return NULL;
    }
    CloseHandle(rin);
    {
        DWORD w = 0;
        WriteFile(win, "\r\n", 2, &w, NULL);
    }
    CloseHandle(win);
    CloseHandle(wr);

    out = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cap);
    if (!out) {
        CloseHandle(rd);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return NULL;
    }

    deadline = GetTickCount() + 15000;
    for (;;) {
        DWORD avail = 0;

        if (GetTickCount() > deadline) {
            timed_out = 1;
            TerminateProcess(pi.hProcess, 1);
            my_drainpipe(rd, out, &len, &cap);
            break;
        }

        if (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL)) {
            my_drainpipe(rd, out, &len, &cap);
            break;
        }

        if (avail > 0) {
            if (!ReadFile(rd, out + len, (DWORD)(cap - len - 1), &n, NULL) || n == 0)
                break;
            len += (SIZE_T)n;
            out[len] = 0;
            if (cap - len - 1 < 4096) {
                SIZE_T ncap = cap * 2;
                char * tmp = (char *)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, out, ncap);
                if (!tmp) break;
                out = tmp;
                cap = ncap;
            }
        } else {
            Sleep(100);
        }
    }

    if (timed_out)
        my_strcat(out, "\n[!] TIMEOUT 15s - process killed (partial output above)\n", cap);

    WaitForSingleObject(pi.hProcess, 2000);
    CloseHandle(rd);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return out;
}

static inline void RunAndReport(const char * cmdline, const char * label)
{
    char * out = CaptureCmdA(cmdline);
    if (out && *out) {
        BeaconPrintf(CALLBACK_OUTPUT, "[+] %s:\n%s\n", label, out);
    } else if (out) {
        BeaconPrintf(CALLBACK_OUTPUT, "[+] %s: (no output)\n", label);
    } else {
        BeaconPrintf(CALLBACK_ERROR, "[!] %s: launch failed (gle=%u)\n",
                     label, (unsigned int)GetLastError());
    }
    if (out) HeapFree(GetProcessHeap(), 0, out);
}

static inline BOOL WriteFileTextA(const char * path, const char * data, DWORD len)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    if (!WriteFile(h, data, len, &written, NULL)) {
        CloseHandle(h);
        return FALSE;
    }
    CloseHandle(h);
    return TRUE;
}

#endif /* BOFUTIL_H */
