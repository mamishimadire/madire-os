# Working rules for Madire OS

These rules apply to every coding agent (Codex, Claude Code and others) and every person changing this repository. The project owner approves every plan, stage and commit.

## 1. Before you start

- **Only one coding agent works on the repository at a time.** Never start while another agent may still be working on it.
- **Optional read-only reviewer.** The owner may ask a second agent to review plans, diffs or results. A reviewer only reads (the repository, logs, reports) and reports its findings to the owner; it never edits files, runs builds or tests that change anything, commits, pushes, or uses the Codespace or the VM. Its findings are proposals: the builder acts on them only after the owner's approval.
- **When the builder changes** (for example from one coding agent to another), the outgoing builder commits and backs up everything with an honest TESTED / NOT TESTED message and leaves a handover note (state, plan and decisions, next stage, watch items); the incoming builder reads it, verifies the state, and reports before acting.
- **Read [docs/PROGRESS.md](docs/PROGRESS.md) first**, then this file: current state, decisions, known limitations and what comes next.
- **Verify the state before acting**, and report any difference instead of changing it:
  - the repository: branch, `git status`, the last commits on `main` and on the `wip/<version>` branch, against what the last report or handover says;
  - the VirtualBox VM `MadireOS-Test`: its settings against [docs/testing.md](docs/testing.md) (`scripts/vbox-acceptance.ps1` checks them and refuses to run on any difference).

## 2. Milestone discipline

- Plan → owner's review → approval → incremental stages with stop points → unit tests → QEMU → VirtualBox → one commit → push → tag → **STOP**.
- Never start the next milestone, or the next stage after a stop point, without approval.
- **Design changes are raised before they are implemented**, even when a test finds the need for them. A short stop is fine.
- **Every approved milestone plan is saved in `docs/plans/`** (`docs/plans/<version>.md`) when it is approved: the plan text as approved, with the decisions taken on it and later approved changes noted at the top.
- **README and screenshots updated to the new version** (since 0.13): every milestone updates README.md (the current version, what the milestone added, the test figures, the roadmap summary) and its screenshots, taken from the milestone's VirtualBox acceptance run and showing the guest screen only, in the milestone commit.

## 3. Commands

- Whenever a milestone adds, changes or removes a command, [docs/commands.md](docs/commands.md) (the Madire OS Command Standard, MOCS) is updated **in the same milestone**: status, level, purpose and foundation. The MOCS version goes up by one step each time (v0.1 → v0.2 → …).
- New commands follow the MOCS rules: one meaning per name, a level (CORE, SYSTEM or APPLICATION), support for `--help`, and a confirmation or a clear warning before any destructive action.
- **Secret input (binding):** No milestone may add a password or other secret prompt without implementing secret-input mode in the same milestone: key events never logged (even with `keylog`), the line not echoed to serial or `dmesg`, not stored in history, and shown as `*` on screen.
- **Key logging stays off by default** (since 0.9.2): key events reach the serial log and `dmesg` only with the boot option `keylog` (a whole word) or during the keyboard diagnostics, for tests. Never log secret input, never add another path that logs typed text, and never use `keylog` where secrets are typed.

## 4. Protected files

- Each milestone plan lists the protected files it must change, with a justification for each; never change a protected file without that justification and approval.
- Check every stage with the protected-file check: `git diff --name-only <baseline> -- <listed paths>`, where the baseline is the latest milestone tag (for 0.12 it stays `3a347a2`, the `v0.11` commit; from 0.13 on, it is `v0.12`).
- Protected in 0.9 (unchanged unless noted): `kernel/memory/memory.c` (changed in 0.9 by an approved, mechanical preemption guard), `heap.c`, `reserve.c`, `pmm.c`, `mmap.c/.h`, `memtest.c`, `timer.c` (changed in 0.11 by the approved lock-free `timer_ticks()` read), `irq.c`, `pic.c`, `devtest.c`, `bootscreen.c/.h`, all of `drivers/`, `kernel/input/`, `textbuf.c`, `lineedit.c`, `parser.c`, `cpuinfo.c`, `logo.c`, `cmos.c`, `io.h`, `multiboot.h`, `tests/unit/test_shell.c`, `test_mmap.c`, `test_reserve.c`, `tests/memory_check.py`, `tests/qmp_keys.py`. Also protected since 0.12: `AGENTS.md` itself and `docs/plans/` (all approved milestone plans).

## 5. Tests

- Every milestone plan must list **every** existing check that contains a version string (not only the named version checks), since a version bump changes them all.
- Never reduce existing test coverage. Explain every changed check (what it checked before, what it checks now, and why it still proves the same fact). Lists of expected failures are exact and agreed in advance.
- **The official regression runs on GitHub Actions (since 0.12)**, on a committed and pushed commit given as a full SHA: `scripts/actions-regress.ps1 -Dispatch` (sharded by default, about 20 minutes), then `-Collect <run id>` once it has finished, which downloads the artifact into `build\regression\<run id>\` (**collect every run used as evidence**). Don't poll a run for long inside a turn. The workflow (`.github/workflows/regression.yml`, on `main`) is started by hand only, reads the repository only, uses no secrets, emulates with TCG only and pins every action to a commit SHA; changing it needs the owner's approval. The procedure is in [docs/development.md](docs/development.md).
- **No Actions run is started while a decision from the owner is open.** Wait for the answer first, even if the run seems independent of it.
- **QEMU timing evidence comes from GitHub Actions only:** QEMU timing, stress and kernel-margin results count only from GitHub Actions (QEMU 8.2.2, gcc 13).
- **The Codespace is the fallback** (for environmental failures on Actions, or on the owner's request): `scripts/remote-regress.ps1` runs the full suite detached on a hash-verified snapshot (about 50 minutes; snapshot with SHA-256 of every file, upload, its own hash-guard proof, `make unit test iso` detached, polling for `MAKE EXIT`, the checks, the final hash comparison, stopping the Codespace). Then confirm the working tree still equals the snapshot. A tool's time limit must never cut a run off silently.
- **The agreed counts:** `tests/expected-checks.txt` holds the number of checks per tag and of unit suites; a full and a sharded run fail on any difference. Every stage that adds or changes checks updates it in the same diff.
- **Local Kali target (optional): builds and unit tests only.** `scripts/remote-regress.ps1 -Target Kali` runs the same procedure on the local Kali VM (key login only; it starts and stops the VM), but its QEMU timing, stress and kernel-margin results are never evidence (Kali runs other QEMU and gcc versions in a starved VM). It isn't an official environment, because VirtualBox runs on the Windows Hypervisor Platform here (see docs/testing.md). Never run it while `MadireOS-Test` runs, and never run the VirtualBox acceptance while the Kali VM runs (both scripts refuse). Never put a password or credential in a script, a file or the VM.
- **Codespaces is a paid resource: use it efficiently.** Start a Codespace only for a task, stop it as soon as the results are collected, keep extra sessions short, and report the Codespace time with each run.
- Report results by tag (`[0.4]` … and the new milestone's), with the unit suites, `make repro`, the symbol and SSE checks and the protected-file diff.
- **The build must stay reproducible:** `make repro` gives byte-identical `madire.elf` and `madire.kernel`.

## 6. VirtualBox

- **Never change the VM's settings without approval.** Report a difference instead. The only change a test makes is attaching the ISO under test.
- Use `scripts/vbox-acceptance.ps1`; keep its screenshots and serial logs as evidence, and compare the serial logs with QEMU's.
- **Virtual disks only (binding, since 0.12):** the installer and any code that writes to a disk may only ever run against virtual disks (VirtualBox or QEMU disk images), never against the real disks of the development (work) laptop.
- **Host memory for VMs (Kali, `MadireOS-Test`, scratch VMs; since 1.1):** if steady available memory is below the stage's threshold, the builder may close **only** these apps without asking: Google Chrome (incl. background processes), Microsoft Edge (incl. WebView/background), Grammarly, OneDrive, VoyceMobile. Edge's WebView2 processes (`msedgewebview2.exe`) only when started by `msedge.exe`; never those owned by another app (e.g. Teams, Outlook, Widgets). Close gracefully first (normal close, so Chrome/Edge can restore tabs); force-close only if a process is still running after 30 s. **Never** close Word, Excel, PowerPoint, Outlook, Teams, VS Code, or any other app: report those and ask the owner. After closing, wait for memory to settle, re-measure (three samples), and list in the report exactly what was closed and the memory before/after. Never reopen apps, change startup settings, or uninstall anything.

## 7. Backups, commits and tags

- Back up to a `wip/<version>` branch at every stop point, with a message saying whether the state is TESTED.
- One commit on `main` per milestone, tagged `vX.Y` (or `vX.Y.Z`); delete the `wip` branch after the final push.
- **`wip/` branches are backups only (binding, since 0.10):** never open or merge a pull request from a `wip/` branch into `main`; `main` receives only the single milestone commit, after acceptance. (Branch protection isn't available for this repository, so this rule is the protection.)
- Commit and tag messages are UTF-8 **without a byte order mark**; verify their first bytes (`57 49 50` for "WIP", for example). PowerShell's default output adds a BOM: write message files with `UTF8Encoding($false)`.
- `.vscode/` is never committed.

## 8. Files

- LF line endings, no BOM, no trailing whitespace (follow `.gitattributes`).
- No personal paths in scripts.

## 9. Permissions

- If a command is refused for lack of a permission verdict ("no verdict"), **retry once only**, then stop and report. Never bypass a permission check.

## 10. User memory (binding, since 0.10)

- The kernel reads user memory only through `copy_from_user` (or checks it with `user_readable`), never by dereferencing a user address.
- **Check, then copy:** any milestone that adds memory-unmapping system calls or threads inside a process must make the user-pointer check and the copy atomic (or copy through the physical-access window), with a test that changes the mapping between check and copy.

## 11. Code origin (binding, since 0.12)

1. **Madire OS's own code is written from scratch.** Reference projects (with any licence, or none) may be read for ideas, architecture and design only; their code, text or assets are never copied or closely reproduced. How far each project may be read (READ FULLY, STUDY ONLY, INSPIRATION ONLY) is set in [docs/references.md](docs/references.md); references are studied outside the repository.
2. **Third-party libraries** are allowed only with a permissive licence (MIT, BSD, Apache 2.0, zlib, 0BSD, CC0-1.0; the last two since 0.13), only with the owner's explicit approval per library, kept in `third_party/<name>/` with its licence file, and listed in `THIRD_PARTY_NOTICES` (created with the first library).
3. **Never:** code with no licence; GPL, LGPL or APSL code; snippets from forums, blogs or tutorials. **The only exception is GRUB**, used only as a separate, unmodified build and boot tool, never as Madire code, until 0.13 is tagged (item 5).
4. **Every stage report states "No third-party code added"**, or lists each addition with its licence and approval.
5. **The separate boot tool (since 0.13): Limine** (BSD-2-Clause) replaces GRUB as the only separate-tool exception once 0.13 is tagged; during 0.13 both are used, for the comparison. It is an unmodified release of a pinned version (the binary package), fetched at build time and checked against its pinned SHA-256 on every fetch; only its host tool is built, from the package's own `limine.c`. It is never kept in the repository or built into Madire code, so item 2's `third_party/` and licence-first conditions don't apply to it. Its `LICENSE` and `3RDPARTY.md` go into every ISO and into `THIRD_PARTY_NOTICES` (created in 0.13). Its `3RDPARTY.md` attributes the parts it bundles (among them a decoder of CC-BY-3.0 origin); the one part with an unconfirmed licence (the cursor code in its `vga_textmode.c`) is a known item, recorded in `THIRD_PARTY_NOTICES` and [docs/evaluations/limine.md](docs/evaluations/limine.md).
6. **Asset origin:** assets (images, fonts, sounds and the like) are by default only original work, CC0, public domain, CC-BY or SIL OFL; CC BY-SA only with the owner's approval; never assets with no licence or of unknown origin. `THIRD_PARTY_ASSETS.md` is created when the first third-party asset ships.

**Licence decided: Apache 2.0 + trademark note; contributions via DCO.** See [LICENSE](LICENSE), [NOTICE](NOTICE), [TRADEMARKS.md](TRADEMARKS.md) and [CONTRIBUTING.md](CONTRIBUTING.md).

## 12. Build and test commands

```powershell
.\scripts\actions-regress.ps1 -Dispatch     # Windows: the official regression on GitHub Actions (sharded)
.\scripts\actions-regress.ps1 -Collect <run id>   # download a finished run into build\regression\<run id>\
.\scripts\remote-regress.ps1        # Windows: the fallback, the full regression in the Codespace (starts and stops it)
.\scripts\remote-regress.ps1 -Target Kali   # the same on the local Kali VM (starts and stops it)
.\scripts\build.ps1                 # Windows: build and test in the Codespace, fetch the ISO
.\scripts\vbox-acceptance.ps1       # Windows: VirtualBox acceptance test
```

```sh
make            # kernel
make iso        # bootable ISO (also runs scripts/check-bootscreen.sh)
make unit       # host unit tests
make repro      # reproducible-build check
make test       # unit tests + repro + QEMU tests (run detached, see section 5)
make test-shard SHARD=<part>   # one part of the QEMU tests (static, 1-4, final)
```
