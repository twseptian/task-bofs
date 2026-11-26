# task-bofs release-validated — Live-Tested Sliver Extensions

The subset of the task-bofs suite that has been **executed and verified on live beacons** (Windows 10 workstation + Windows Server 2022), packaged for distribution. Full test results: [`../VALIDATION.md`](../VALIDATION.md). Related package: [`../release/`](../release/) (taskhijack/taskrestore — built, awaiting full live matrix).

**Version:** 1.0.0 · **Author:** twseptian · **Target:** Windows x64, Sliver 1.7.x+

> For use only on systems you are authorized to test.

## Packages

| Folder | Extension | Commands | Live status |
|---|---|---|---|
| `taskenum/` | taskenum | `taskenum`, `taskenum -full 1` | ✅ live on 2 hosts, both modes |
| `privkit/` | 10 × `priv-*` | privesc checks (AlwaysInstallElevated, autologon, credman, PATH-hijack, autorun, modifiable services, PS history, token privileges, UAC status, unquoted service paths) | ✅ 3 of 10 executed live (uacstatus, alwaysinstall, tokenprivs); remaining 7 same design |
| `getsystem/` | `bof-getsystem` | Control test: winlogon token duplication | designed to FAIL for standard users (requires SeDebug) — proves host hardening |

## Install

```
sliver > extensions load /path/to/release-validated/taskenum
sliver > extensions load /path/to/release-validated/privkit
sliver > extensions load /path/to/release-validated/getsystem
```

`extensions load` is rejected while a beacon is selected — load first, then `use <beacon>`.

## Usage

```
taskenum                 # detail blocks for user-owned tasks (elevation targets)
taskenum -full 1         # compact table of all tasks, candidates flagged '>>>'

priv-uacstatus           # UAC config + current integrity level
priv-tokenprivs          # token privileges (SeDebug/SeImpersonate presence = what you can do next)
priv-alwaysinstall       # AlwaysInstallElevated check
priv-svcmod / priv-unquotedsvc / priv-hijackable / priv-autorun / priv-credman / priv-autologon / priv-pshist

bof-getsystem            # control test: fails on standard users (expected), succeeds on elevated admin beacons
```

## Reading the results

- `priv-tokenprivs` is the decision point: **SeDebug enabled** → LSASS dump tools work; **SeImpersonate enabled** → potato-family SYSTEM escalation works; **SeLoadDriver present** → BYOVD becomes possible (on-demand enable).
- `taskenum` candidates (user SID suffix in task name) are tasks owned by the current account — modifiable and runnable by that same principal. On hosts where candidates exist, the related taskhijack/taskrestore package applies.
- `bof-getsystem` failing on a standard-user beacon is the expected, correct result — it demonstrates that host hardening holds.

## Build

All `.o` files are prebuilt (mingw-w64, CS beacon convention: `MODULE$Function` imports, `go` entry). Rebuild instructions for each upstream project are in the sibling `nukesuite/` documentation of this repository.

## Notes

- `bof-getsystem` command name intentionally differs from Sliver's built-in `getsystem` to avoid a load conflict.
- Standard-user beacons: every privesc check is expected to report closed paths on a hardened host — that result is the finding, not a failure.
