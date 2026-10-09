# Working rules for Madire OS

These rules apply to every coding agent (Codex, Claude Code and others) and every person changing this repository. The project owner approves every plan, stage and commit.

## 1. Before you start

- **Only one coding agent works on the repository at a time.** Never start while another agent may still be working on it.
- **Read [docs/PROGRESS.md](docs/PROGRESS.md) first**, then this file: current state, decisions, known limitations and what comes next.
- **Verify the state before acting**, and report any difference instead of changing it:
  - the repository: branch, `git status`, the last commits on `main` and on the `wip/<version>` branch, against what the last report or handover says;
  - the VirtualBox VM `MadireOS-Test`: its settings against [docs/testing.md](docs/testing.md) (`scripts/vbox-acceptance.ps1` checks them and refuses to run on any difference).

## 2. Milestone discipline

- Plan → owner's review → approval → incremental stages with stop points → unit tests → QEMU → VirtualBox → one commit → push → tag → **STOP**.
- Never start the next milestone, or the next stage after a stop point, without approval.
- **Design changes are raised before they are implemented**, even when a test finds the need for them. A short stop is fine.

## 3. Commands

- Whenever a milestone adds, changes or removes a command, [docs/commands.md](docs/commands.md) (the Madire OS Command Standard, MOCS) is updated **in the same milestone**: status, level, purpose and foundation. The MOCS version goes up by one step each time (v0.1 → v0.2 → …).
- New commands follow the MOCS rules: one meaning per name, a level (CORE, SYSTEM or APPLICATION), support for `--help`, and a confirmation or a clear warning before any destructive action.
- **Secret input (binding):** No milestone may add a password or other secret prompt without implementing secret-input mode in the same milestone: key events never logged (even with `keylog`), the line not echoed to serial or `dmesg`, not stored in history, and shown as `*` on screen.
- **Key logging stays off by default** (since 0.9.2): key events reach the serial log and `dmesg` only with the boot option `keylog` (a whole word) or during the keyboard diagnostics, for tests. Never log secret input, never add another path that logs typed text, and never use `keylog` where secrets are typed.

## 4. Protected files

- Each milestone plan lists the protected files it must change, with a justification for each; never change a protected file without that justification and approval.
- Check every stage with a diff of the protected files against the previous milestone's commit.
- Protected in 0.9 (unchanged unless noted): `kernel/memory/memory.c` (changed in 0.9 by an approved, mechanical preemption guard), `heap.c`, `reserve.c`, `pmm.c`, `mmap.c/.h`, `memtest.c`, `timer.c`, `irq.c`, `pic.c`, `devtest.c`, `bootscreen.c/.h`, all of `drivers/`, `kernel/input/`, `textbuf.c`, `lineedit.c`, `parser.c`, `cpuinfo.c`, `logo.c`, `cmos.c`, `io.h`, `multiboot.h`, `tests/unit/test_shell.c`, `test_mmap.c`, `test_reserve.c`, `tests/memory_check.py`, `tests/qmp_keys.py`.

## 5. Tests

- Every milestone plan must list **every** existing check that contains a version string (not only the named version checks), since a version bump changes them all.
- Never reduce existing test coverage. Explain every changed check (what it checked before, what it checks now, and why it still proves the same fact). Lists of expected failures are exact and agreed in advance.
- **Run the full suite detached in the Codespace, on a hash-verified snapshot** (it takes about 15 minutes; a tool's time limit must never cut it off silently). The procedure is in [docs/development.md](docs/development.md): snapshot with SHA-256 of every file, upload, compare the hashes, `nohup make unit test iso`, poll for `MAKE EXIT`, then confirm the working tree still equals the snapshot.
- Report results by tag (`[0.4]` … and the new milestone's), with the unit suites, `make repro`, the symbol and SSE checks and the protected-file diff.
- **The build must stay reproducible:** `make repro` gives byte-identical `madire.elf` and `madire.kernel`.

## 6. VirtualBox

- **Never change the VM's settings without approval.** Report a difference instead. The only change a test makes is attaching the ISO under test.
- Use `scripts/vbox-acceptance.ps1`; keep its screenshots and serial logs as evidence, and compare the serial logs with QEMU's.

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

## 11. External code

- No external code, text or assets are copied without the owner's approval and a licence check. Madire OS has no licence yet; one must be decided first.
- External references are studied outside the repository; see [docs/references.md](docs/references.md).

## 12. Build and test commands

```powershell
.\scripts\build.ps1                 # Windows: build and test in the Codespace, fetch the ISO
.\scripts\vbox-acceptance.ps1       # Windows: VirtualBox acceptance test
```

```sh
make            # kernel
make iso        # bootable ISO (also runs scripts/check-bootscreen.sh)
make unit       # host unit tests
make repro      # reproducible-build check
make test       # unit tests + repro + QEMU tests (run detached, see section 5)
```
