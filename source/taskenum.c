/*
 * taskenum.c - enumerate scheduled tasks, flag user-owned ones.
 *
 * Runs "schtasks /query /fo csv /v" (pipe capture, no cmd.exe), parses the
 * CSV and reports tasks whose name contains a user SID (-S-1-5-21-...).
 * Those are per-user tasks owned by the current account, so they can be
 * modified and run by the same principal. Default prints one block per
 * candidate; -full 1 prints every task in a compact table.
 *
 * The CSV parser is quote-aware (fields like "One Time Only, Hourly" contain
 * commas) and columns are matched by header name, not position.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "bofutil.h"

#define MAX_FIELDS 40

/* column indexes, mapped from the header row */
static int i_task = -1, i_status = -1, i_logon = -1;
static int i_runas = -1, i_cmd = -1, i_sched = -1, i_next = -1;

static inline int my_streq(const char * a, const char * b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (*a == *b);
}

/* split one CSV row in place. Fields are quoted; commas inside quotes are
 * safe. Mutates the buffer - print raw lines before calling this. */
static inline int parse_csv_row(char * line, char * fields[], int maxf)
{
    int n = 0;
    char * p = line;
    while (*p && n < maxf) {
        while (*p && *p != '"') p++;
        if (!*p) break;
        p++;
        fields[n++] = p;
        while (*p && *p != '"') p++;
        if (*p) { *p = 0; p++; }
        if (*p == ',') p++;
    }
    return n;
}

static inline int find_col(char * fields[], int nf, const char * name)
{
    int i;
    for (i = 0; i < nf; i++) {
        if (fields[i] && my_streq(fields[i], name)) return i;
    }
    return -1;
}

/* pad/truncate to a fixed width: longer input gets cut to width-2 + ".." */
static inline void my_strfit(char * dst, SIZE_T max, const char * src, int width)
{
    SIZE_T n = my_strlen(src), i;
    if (width < 4 || max < (SIZE_T)width + 1) return;
    if (n <= (SIZE_T)width) {
        my_strcpy(dst, src, max);
        while (n < (SIZE_T)width) dst[n++] = ' ';
        dst[n] = 0;
    } else {
        for (i = 0; i < (SIZE_T)width - 2; i++) dst[i] = src[i];
        dst[width - 2] = '.';
        dst[width - 1] = '.';
        dst[width] = 0;
    }
}

static inline const char * F(char * f[], int nf, int idx)
{
    return (idx >= 0 && idx < nf && f[idx] && *f[idx]) ? f[idx] : "-";
}

static inline void PrintField(const char * label, char * fields[], int nf, int idx)
{
    BeaconPrintf(CALLBACK_OUTPUT, "    %s: %s\n", label,
        (idx >= 0 && idx < nf && fields[idx] && *fields[idx]) ? fields[idx] : "N/A");
}

/* one compact table row: [>>>] STATUS | NEXT RUN | COMMAND | TASKNAME */
static inline void PrintCompactRow(char * f[], int nf, int is_candidate)
{
    char row[512];
    char col[64];

    row[0] = 0;
    my_strcat(row, is_candidate ? ">>> " : "    ", sizeof(row));
    my_strfit(col, sizeof(col), F(f, nf, i_status), 10);
    my_strcat(row, col, sizeof(row));
    my_strfit(col, sizeof(col), F(f, nf, i_next), 21);
    my_strcat(row, col, sizeof(row));
    my_strfit(col, sizeof(col), F(f, nf, i_cmd), 50);
    my_strcat(row, col, sizeof(row));
    my_strcat(row, F(f, nf, i_task), sizeof(row));
    BeaconPrintf(CALLBACK_OUTPUT, "%s\n", row);
}

void go(char * args, int alen)
{
    datap parser;
    int    full = 0;
    char * out;
    char * line;
    const char * needle = "-S-1-5-21-";
    int    candidates = 0;
    int    total = 0;
    int    header_mapped = 0;

    BeaconDataParse(&parser, args, alen);
    full = BeaconDataInt(&parser);

    BeaconPrintf(CALLBACK_OUTPUT,
        "[*] taskenum: enumerating scheduled tasks (schtasks csv /v, in-process)...\n");
    if (full)
        BeaconPrintf(CALLBACK_OUTPUT,
            "[*] full mode: ALL tasks, one line - '>>>' = hijack candidate\n"
            "[*]     STATUS     | NEXT RUN             | COMMAND(..=truncated)           | TASK\n");

    out = CaptureCmdA("schtasks.exe /query /fo csv /v");
    if (!out) {
        BeaconPrintf(CALLBACK_ERROR,
            "[!] taskenum: failed to launch/capture schtasks.exe (gle=%u)\n",
            (unsigned int)GetLastError());
        return;
    }

    line = out;
    while (line && *line) {
        char * p = line;
        char * next = NULL;

        while (*p && *p != '\n') p++;
        if (*p == '\n') next = p + 1;
        if (p > line && *(p - 1) == '\r') p--;
        *p = 0;

        if (*line) {
            if (!header_mapped) {
                /* first row = header. parse_csv_row mutates the buffer, so
                 * raw lines must be printed before this. */
                char * hf[MAX_FIELDS];
                int nf = parse_csv_row(line, hf, MAX_FIELDS);
                i_task  = find_col(hf, nf, "TaskName");
                i_status= find_col(hf, nf, "Status");
                i_logon = find_col(hf, nf, "Logon Mode");
                i_runas = find_col(hf, nf, "Run As User");
                i_cmd   = find_col(hf, nf, "Task To Run");
                i_sched = find_col(hf, nf, "Schedule Type");
                i_next  = find_col(hf, nf, "Next Run Time");
                header_mapped = 1;
            } else {
                total++;
                if (my_strstr(line, needle)) {
                    candidates++;
                    if (full) {
                        char * f[MAX_FIELDS];
                        int nf = parse_csv_row(line, f, MAX_FIELDS);
                        PrintCompactRow(f, nf, 1);
                    } else {
                        char * f[MAX_FIELDS];
                        int nf = parse_csv_row(line, f, MAX_FIELDS);
                        BeaconPrintf(CALLBACK_OUTPUT,
                            "\n[%d] %s\n", candidates,
                            F(f, nf, i_task));
                        PrintField("Status    ", f, nf, i_status);
                        PrintField("Logon     ", f, nf, i_logon);
                        PrintField("Run As    ", f, nf, i_runas);
                        PrintField("Schedule  ", f, nf, i_sched);
                        PrintField("Next Run  ", f, nf, i_next);
                        PrintField("Command   ", f, nf, i_cmd);
                    }
                } else if (full) {
                    char * f[MAX_FIELDS];
                    int nf = parse_csv_row(line, f, MAX_FIELDS);
                    PrintCompactRow(f, nf, 0);
                }
            }
        }
        line = next;
    }

    BeaconPrintf(CALLBACK_OUTPUT,
        "\n[*] taskenum done: %d user-owned task(s) out of %d total\n"
        "[*] Candidates = user-owned tasks (modifiable/runnable by this account)\n",
        candidates, total);
    if (full) {
        BeaconPrintf(CALLBACK_OUTPUT,
            "[*] detailed candidate blocks: run taskenum without flags\n");
    } else {
        BeaconPrintf(CALLBACK_OUTPUT,
            "[*] compact list of ALL tasks: run taskenum -full 1\n");
    }

    HeapFree(GetProcessHeap(), 0, out);
}
