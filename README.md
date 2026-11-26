# task-bofs

Sliver BOFs for Windows scheduled task enumeration and hijacking. All child output is captured over pipes; schtasks is spawned directly, no cmd.exe, no dropped scripts.

Commands:

- taskenum - list scheduled tasks, flag per-user ones (machine SID suffix in the name)
- taskhijack - back up a task's XML, replace its action, optionally store a credential, run it
- taskrestore - re-register the task from that backup

Windows x64, Sliver 1.7.x. Use only on systems you have permission to test.

## why

- **One call per job.** A hijack that normally takes four or five manual `schtasks` invocations (or a stack of batch files) is a single command, and so is putting the task back.
- **Reversible by construction.** Every `taskhijack` writes the original task XML to the console and `%TEMP%` before touching anything; `taskrestore` re-registers from that backup, which also drops any stored credential from the task.
- **It survives schtasks' bad habits.** A spaced action value makes schtasks ask for the run-as password mid-command; the suite merges `/tr` and `/rp` into one call to avoid the prompt, answers stdin so prompts fail fast instead of hanging, and gives every step a 15-second budget so a stuck child can never wedge the beacon.
- **Readable results.** taskenum parses the CSV properly (quote-aware fields like `"One Time Only, Hourly"`, columns matched by name so locale changes don't break it) and shows only what matters — or all of it, one line per task, with `-full 1`.
- **Small footprint.** A few KB of CRT-free COFF objects. No cmd.exe child, no helper scripts, no .NET runtime. Enumeration works from a filtered-token context; no elevation needed to survey.

## install

`extensions load` is rejected while a beacon is selected. Load first, then `use`:

```
sliver > extensions load /path/to/taskenum
sliver > extensions load /path/to/tasksuite
sliver > use <beacon-id>
```

## usage

```
taskenum
taskenum -full 1
```

Default prints one block per user-owned task. `-full 1` prints every task in a fixed-width table, candidates prefixed with `>>>`.

```
taskhijack -task "\\OneDrive Standalone Update Task-S-1-5-21-422339986-568025100-1833951960-1001" -cmd C:\\Windows\\Temp\\update.bat -pw <current-password>
taskrestore -task "\\OneDrive Standalone Update Task-S-1-5-21-422339986-568025100-1833951960-1001"
```

taskhijack: dumps the task XML to the console and `%TEMP%\bof-<task>-orig.xml`, then `schtasks /change /tr ...` (+ `/rp` merged into the same call), `taskkill /f /im <old image>` (parsed from the backup; a stale instance blocks `/run` via IgnoreNew), then `/run`. taskrestore re-registers from the backup and deletes it; the original action comes back and any stored credential is dropped. Check with `schtasks /query /tn <task> /xml`.

Argument notes:

- spaced `-cmd`/`-args` values need `-pw`. A spaced `/tr` makes schtasks ask for the run-as password; without `-pw` in the same call the change fails
- `-pw` must be the task owner's current password, blank is rejected
- InteractiveToken tasks run with a UAC-filtered token; `/rp` removes the filter for admins. It does not elevate non-admin users
- console escaping: double backslashes, quote spaced values, avoid inner quotes; point `-cmd` at an uploaded script for anything complex

## build

mingw-w64 required:

```
cd source && ./build.sh
```

The build audits its own output: unresolved symbols must only be `Beacon*` and `__imp_*`, each object must export `go`. A CRT function leaking through (GCC rewrites `strlen`-like loops into CRT calls under `-O2`) fails the build, because the BOF would not load.

## notes

- no CRT in the objects: string helpers are hand-written, `-fno-builtin` is mandatory, `ZeroMemory` is avoided (it is `memset`)
- child stdin is a pipe with one blank line, then EOF. This exists because `schtasks /change` asks for the run-as password on spaced `/tr` values; with no stdin it waits on the invisible console forever
- every spawned command has a 15-second budget, then it is terminated. A stuck child must not wedge the beacon
- `CreateProcessA` may modify its command-line buffer, so it is always copied before use

## files

```
taskenum/extension.json  taskenum.x64.o
tasksuite/extension.json taskhijack.x64.o taskrestore.x64.o
source/beacon.h bofutil.h taskenum.c taskhijack.c taskrestore.c build.sh
```

twseptian
