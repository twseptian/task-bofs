/*
 * taskrestore.c - restore a task from the XML backup written by taskhijack.
 *
 * taskrestore -task "\\Some Task-S-1-5-21-..."
 *
 * Runs "schtasks /create /tn <task> /xml <backup> /f" and deletes the backup
 * on success. Re-registering from the original XML also drops any stored
 * credential (/rp) from the task.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "bofutil.h"

void go(char * args, int alen)
{
    datap parser;
    char * taskname;
    char   cmdline[1024];
    char   xmlpath[MAX_PATH];
    char   tmpdir[MAX_PATH];
    char   sanitized[128];
    char * out;
    SIZE_T d;

    (void)alen;

    BeaconDataParse(&parser, args, alen);
    taskname = BeaconDataExtract(&parser, NULL);

    if (!taskname || !*taskname) {
        BeaconPrintf(CALLBACK_ERROR, "[!] usage: taskrestore -task <task name>\n");
        return;
    }

    GetEnvironmentVariableA("TEMP", tmpdir, (DWORD)sizeof(tmpdir));
    if (!tmpdir[0]) my_strcpy(tmpdir, "C:\\Windows\\Temp", sizeof(tmpdir));
    my_sanitize(sanitized, taskname, sizeof(sanitized));
    d = my_strlen(tmpdir);
    my_strcpy(xmlpath, tmpdir, sizeof(xmlpath));
    if (d && xmlpath[d - 1] != '\\') my_strcat(xmlpath, "\\", sizeof(xmlpath));
    my_strcat(xmlpath, "bof-", sizeof(xmlpath));
    my_strcat(xmlpath, sanitized, sizeof(xmlpath));
    my_strcat(xmlpath, "-orig.xml", sizeof(xmlpath));

    BeaconPrintf(CALLBACK_OUTPUT, "[*] taskrestore: '%s' from %s\n", taskname, xmlpath);

    my_strcpy(cmdline, "schtasks.exe /create /tn \"", sizeof(cmdline));
    my_strcat(cmdline, taskname, sizeof(cmdline));
    my_strcat(cmdline, "\" /xml \"", sizeof(cmdline));
    my_strcat(cmdline, xmlpath, sizeof(cmdline));
    my_strcat(cmdline, "\" /f", sizeof(cmdline));

    out = CaptureCmdA(cmdline);
    if (out && my_strstr(out, "SUCCESS")) {
        BeaconPrintf(CALLBACK_OUTPUT, "[+] restore OK:\n%s\n", out);
        if (DeleteFileA(xmlpath)) {
            BeaconPrintf(CALLBACK_OUTPUT, "[+] backup XML removed\n");
        } else {
            BeaconPrintf(CALLBACK_ERROR, "[!] failed to delete the backup XML (gle=%u) - remove it manually\n",
                         (unsigned int)GetLastError());
        }
        BeaconPrintf(CALLBACK_OUTPUT,
            "[*] verify: schtasks /query /tn <task> /xml shows the original action, no <Password>\n"
            "[*] kill any payload process the hijacked run started\n");
    } else {
        BeaconPrintf(CALLBACK_ERROR,
            "[!] restore failed: %s\n    (backup present? check: cmd /c type %s)\n",
            out ? out : "(launch failed)", xmlpath);
    }
    if (out) HeapFree(GetProcessHeap(), 0, out);
}
