# Madire OS backlog

## 1. Purpose

This is the single list of work that is planned but not done. Every stage
updates it in its own commit (items done are removed, new items added), and
every stage report says whether it changed. At 1.5 stage 18a its long-term
items are folded into [roadmap.md](roadmap.md); the list then continues for
the next milestone.

Each item names its target (a stage of the current milestone, a later
milestone, or "later" when no milestone is chosen yet), its prerequisites,
and its origin: an owner decision with its date, or a note of
[plans/1.5.md](plans/1.5.md) (its notes 1-14 are at the top of that file).
The approved plan and the notes win over this list; this list only makes
sure nothing is forgotten.

**The desktop completion gate (the owner's rule, 2026-11-20):** the phone
version starts only after the desktop OS is complete: every desktop item in
this backlog (including everything skipped or outstanding since the
beginning) is done, or explicitly deferred by the owner in writing.
Milestones after v1.5 work through this backlog in the agreed order. The
gate's state is the Gate checklist at the end of this file, updated at
every milestone release; the rule goes into [roadmap.md](roadmap.md) at 1.5
stage 18a.

Started in 1.5 stage 13 (2026-11-20).

## 2. Version 1.5: the stages left

| Stage | What it delivers | Prerequisites | Origin |
|---|---|---|---|
| S13 | The Trash in Live and Disk mode (Move to Trash, the Trash view with Name / Original location / Deleted, Restore, Restore all, Empty Trash; the FreeDesktop Trash rules; `.trashinfo` read only through the bounded parser); 1.4's visible `/home/user/Trash` removed only if empty (kept as a normal folder otherwise; never on `test=` boots); six home folders; Cut, Copy, Paste (64-item clipboard), Ctrl+X/C/V/A, multi-select actions; rule SL for every action (links never followed; fast and slow link image); the rename field opens with the whole name selected; the Files right-click menus (item, empty space, Trash) with the Menu key, Shift+F10 and key hints; Alt+Enter opens Properties; the in-window question panel | S12 | plan row S13; the owner's S13 decisions D1-D5 and Q1-Q9 (2026-11-20); note (13) |
| S14 | This PC: each storage area's name, used and free space, a free-space bar with "X free of Y", opening a drive | S13 | plan row S14 and C5 |
| S14 | The v1.5 pointer set: resize (four), move, I-beam, hand, busy (animated); the drag pointers move, copy and not allowed | S13 | note (11), (12)(b); note (14) adds the drag pointers |
| S14 | Box (rubber-band) selection by dragging on empty space, in the List and Grid views | S13 | note (14); the owner, 2026-11-20 |
| S14 | Drag and drop inside Files: selected items onto a folder (list, grid or sidebar) moves them; onto the sidebar Trash moves them to the Trash; Ctrl copies; another filesystem copies; Esc cancels; never a folder into itself; read-only targets and files refuse; name clashes get " 2"; links never followed | S13 (paste and Trash cores) | note (14) |
| S14 | The timing decision: an A3 pure move (about 160 s from part 5 to static) or a sixth numbered part, so no part exceeds about 1,030 s | S13's measured part 5 seconds | the owner's S13 approval (2026-11-20); note (14) |
| S15 | The desktop's icons (the items of `/home/user/Desktop` and the Trash icon, empty or full) and their menus | S13, S14 | plan row S15 and C7 |
| S15 | The M menu's Files row | S12 | plan row S15; the owner, 2026-11-20 |
| S15 | The desktop's right-click menu: New Folder, New File, Paste (with Open Console and About Madire OS) | S13 | plan C6b and C7 |
| S15 | Drag between the desktop and Files in both directions, and onto the desktop's Trash icon | S14 | note (14) |
| S15 | Where the clipboard lives so the desktop's Paste works with Files closed (S13 keeps it in Files' memory) | S13 | the owner's S13 approval, Q8 |
| S16 | The full run that uploads the ISO for S17; the VirtualBox `desktop` and `storage` groups and the Kali Trash check prepared | S15 | plan row S16 |
| S16 | Cleanup: `tests/checks.sh` says "kernel not built (MAKE EXIT n)" when the kernel is missing | - | the owner, S12 |
| S16 | Cleanup: the unit loop runs every suite (it stops at the first failing one today) | - | S8 notes |
| S16 | Cleanup: `ui_button_draw`'s colour-pair recorder ignores the clip (make it clip-aware or document it) | - | S8 notes |
| S16 | Cleanup: a bounded retry for `scripts/remote-regress.ps1`'s SSH and SCP (the banner timeout recurred in S4 and S9) | - | S9 notes |
| S16 | Cleanup: the wordings "VFS lock hold" / "per hold of the VFS lock"; review the VirtualBox "at most 20 ticks" row (save-piece holders now yield; `blkslow` showed 400 ticks) | - | the owner, S4-S6 |
| S16 | Cleanup: `scripts/vbox-storage.py`'s STEPS still use `to:row@Restart` (Power... since S6); the VirtualBox rows that name Restart (`vbox-acceptance.ps1`, the Restart log line) and `tests/screen_ref.py`'s shot names | - | the owner's S6 decisions |
| S16 | Cleanup: rename the remaining "Windows key" texts to "Super key" (new texts already say Super) | - | S9b notes |
| S16 | Cleanup: modifier keys alone do not open the Console window | - | the owner, S9b |
| S16 | Cleanup: consider a reference scene at 125 % for the grid | - | the owner, S10 |
| S16 | Candidate: Tab completion of file and folder names in the Console (it completes command names only) | - | the owner, 2026-11-20 |
| S16 | The offline replay simulator (the desktop sessions replayed through `qmp_desktop.py` against a simulated kernel, kept outside the repository until now) committed into the repository (for example `tests/replay/`) with a short README, so it is versioned, reviewable and used by every later session; it follows the kernel's real behaviour, each rule cited from the code or a run's log; every run writes into a fresh, empty output folder, and stopping a run kills all its child processes (the README says both) | - | the owner, 2026-11-21 (1.5 S13: the simulator had missed the state line logged after a job step; a stopped run's child processes went on writing into the next run's folder) |
| S17 | Owner-run VirtualBox acceptance (every group; `storage` with the pristine disk copies), the Kali check of the used disk, `risk13` (30 boots with the phase lines) | S16 | plan row S17; the owner's answer 4 |
| S18a | `docs/shortcuts.md`, the Madire Keyboard Shortcuts Standard (section 7 below) | - | note (6) |
| S18a | `docs/pointers.md`, the Madire Pointer Standard (section 7 below) | - | note (11) |
| S18a | The Madire AI removal: the case-insensitive search first, every hit listed with file:line and a STOP for the owner's review; then `roadmap.md` and `references.md` edited | - | note (7) |
| S18a | `docs/references.md`: the Enterprise directory section (item 5), the Desktop and system list (item 8; KDE entries pointed at invent.kde.org; Open WebUI added to AVOID), the Android references (item 9); every licence and official repository verified | - | notes (5), (8), (9); the owner, 2026-11-19/20 |
| S18a | `docs/roadmap.md`: the variable console grid; the window interface milestone; Enterprise directory and sign-in (v2.x); Linux GUI app compatibility (a Wayland compositor); Android apps; the Madire Linux environment; "Madire AI removed"; the desktop completion gate (section 1) | - | the owner's answer 2; decision 14; notes (5), (7), (8), (9), (10); the owner, 2026-11-20 (the gate) |
| S18a | AGENTS.md (protected: full diff, STOP, approval): T1-T4; new C files with Write, existing ones only with Edit; the naming rule (a file prefix on new statics) and the name-clash scan; the Kali readiness checklist (commit pushed, tree clean, HEAD = the commit; scans; host memory and C: disk reported); the pre-commit scan "no `??` (no trigraph sequence) in string literals" (the owner, 1.5 S13: gcc 15's -Werror=trigraphs); the pre-commit rule "a unit case that checks every new kernel hook is connected" (each new callback or environment field proven wired as the kernel wires it, not only modelled; the owner, 1.5 S13: `files_app_setup` did not copy `env->menu`); `make test-shard` comment lists part 5; every stage report says whether `docs/backlog.md` changed | - | note (9) T1-T4; the owner's S11-S13 rules |
| S18a | PROGRESS.md (all of 1.5's changes happen here only), README and screenshots from S17, architecture, testing, `filesystem.md`, the `commands.md` final check; the tooltips' texts ("Back (Alt+Left)") against plan C6b's bare names | S17 | plan row S18; the owner, S12 |
| S18b | The release: full, sharded and stress runs from `main`, then the tag `v1.5` | S18a | plan row S18 |

## 3. The Files app, later

None of these is in 1.5. Target "later" unless a milestone is named.

| Item | Prerequisites | Origin |
|---|---|---|
| Undo | - | the owner, S12 (S18a list) |
| Compress to ZIP | a ZIP writer (Madire's own, or an approved permissive library) | the owner, 2026-11-20 (note (13)) |
| Pin to sidebar | saved settings | note (13) |
| Copy path (Copy as path) | a text clipboard | note (13) |
| Select none; Invert selection | - | the owner, 2026-11-20 |
| Open Console here | the Console's start folder | note (13) |
| Options; Folder Options: start location, the same or its own window, single or double click, file name extensions SHOWN by default (security), the full path in the title bar, hide empty drives, protected system files | saved settings | the owner, 2026-11-20 |
| Refresh (F5) | - | the owner, 2026-11-20; plan section N |
| Sorting by Size, Type and Modified (name only in 1.5) | - | the owner, 2026-11-20; S11 Q1 |
| Icon sizes; Tiles, Content and compact views; item check boxes | - | the owner, 2026-11-20 |
| A details pane and a preview pane | - | the owner, 2026-11-20; plan section N |
| Tabs; search; filter | - | plan section N; the owner, 2026-11-20 |
| Share | networking | the owner, 2026-11-20 |
| The path bar's chevrons as icons; typing a path into the path bar | - | the owner, S12; plan section N |
| A Properties window (1.5 has the read-only info panel) | - | plan C5 and section N |
| Recent and frequent items, local only, with Clear history | - | the owner, 2026-11-20 |
| A friendlier `cd` error hint in the Console ("Did you mean 'Documents'?") | - | note (13) |
| Several Files windows | memory per window | plan section N |
| Typed names outside printable ASCII | keyboard layouts | plan section N; risk 57 |
| Watching folders for changes made elsewhere; saved desktop icon positions | - | plan section N |
| Per-volume `.Trash-$uid` folders; the `directorysizes` cache | more disks | plan section N |
| View, Sort by and Refresh on the desktop; the taskbar's own menu | - | plan section N (the owner's screenshots) |

## 4. The Console

| Item | Target | Prerequisites | Origin |
|---|---|---|---|
| Tab completion of file and folder names | S16 candidate, or later | - | the owner, 2026-11-20 |
| The variable console grid (the Console window resized or maximised) | later (roadmap at S18a) | - | the owner's answer 2 (1.5 plan) |
| Keyboard polish (the gaps found 2026-10-16) | later; needs its own plan | - | post-1.0 note, not approved |

## 5. After 1.5 (proposed "features first"; awaiting the owner's final confirmation)

| Milestone | What | Prerequisites | Origin |
|---|---|---|---|
| v1.6 | UEFI boot | - | the owner's proposal, 2026-11-20 |
| v1.7 | The window interface for programs (decision 14; its blockers: no FPU/SSE state saved, no `mmap`, no input-event system calls or window protocol) and Madire Settings: Display; Time & language (date, time, time zone); About; Personalization (font choice); Accessibility (text size, pointer size and contrast) | v1.6 | the owner's proposal, 2026-11-20; plan section N; note (11) |
| v1.8 | The installer (virtual disks only, AGENTS.md section 6) | v1.7 | the owner's proposal, 2026-11-20 |
| v1.9 | Networking (lwIP behind Madire's own socket layer, only with the owner's approval of the library) | v1.8 | the owner's proposal, 2026-11-20 |
| v2.0 | Users, login, Lock, permissions | v1.9 | the owner's proposal, 2026-11-20; plan section N |

## 6. Later milestones

| Item | Prerequisites | Origin |
|---|---|---|
| Fonts: a proportional UI font from an OFL family (such as Inter or Noto Sans), a permissive rasterizer (such as stb_truetype), each with the owner's approval; never Microsoft fonts | v1.7 | the owner, 2026-11-20 |
| A full start menu: pinned apps, search, recent items | v1.7 | the owner, 2026-11-20 |
| Updates; Madire Store | networking, installer | the owner, 2026-11-20 |
| A browser path | networking, window interface | the owner, 2026-11-20 |
| Sound; media apps: Picture Viewer, Music, Video (PipeWire and WirePlumber as library candidates, note (8)) | window interface; audio drivers | the owner, 2026-11-20; note (8) |
| USB, Wi-Fi, Bluetooth | drivers | the owner, 2026-11-20 |
| The Madire Calculator (Standard, Scientific, Financial) | window interface | 1.3 checkpoint; plan section N |
| Enterprise directory and sign-in (v2.x): LDAP and Kerberos clients, Active Directory domain join, Settings -> Accounts -> Access work or school | networking, musl/POSIX, users and permissions, true UTC time, TLS | note (5) |
| The Madire Linux environment (Debian or Kali userlands from the Console) | Linux compatibility layer, networking, users (root), installer, packet capture | note (10) |
| Linux GUI app compatibility (a Wayland compositor) | musl/POSIX, window interface, shared memory, input and font stacks | note (8) |
| Android apps (a VM route or a container route; vanilla AOSP or LineageOS only) | networking, users and permissions, installer | note (9) |
| The Madire Security Platform | the Linux environment | note (10) |
| Real-hardware boot | UEFI, drivers | the owner, 2026-11-20 |
| A phone version | the desktop completion gate (section 1): every desktop item done or deferred by the owner in writing | the owner, 2026-11-20 |
| Lock and Sleep (full ACPI, an AML interpreter; any ACPI library only after a licence check and approval) | users; ACPI | plan section N |
| Disk reads outside the VFS lock (interrupt-driven AHCI) | - | plan C1 and section N |
| True UTC file times (risk 35's option B; the RTC kept on local time for dual boot) | Settings' time zone | 1.4 risk 35; plan section J |
| The splash's contrast (a 1.3 follow-up) | - | plan section J |
| Risk 13 (VirtualBox boots): open; 30 more boots in S17 | - | plan section J |

## 7. Standards still to write (S18a)

| Standard | Contents | Origin |
|---|---|---|
| `docs/pointers.md`, the Madire Pointer Standard | every v1.5 pointer with its milestone (the arrow; resize four; move; I-beam; hand; busy; drag move, copy, not allowed); later: wait (app not responding), crosshair and location/target, precision select, zoom in/out, help, pen; pointer size and contrast in Settings -> Accessibility | notes (11), (12)(b), (14) |
| `docs/shortcuts.md`, the Madire Keyboard Shortcuts Standard | the Super key and its shortcuts (Super+Up, Super+Down, Super+D, Super+M, Super+Shift+M, Super+Pause, Super+E, Super+Left/Right halves), Alt+F4, Alt+Tab and Shift+Alt+Tab, Files' Ctrl+X/C/V/A, Delete, Shift+Delete, F2, Alt+Enter, the Menu key and Shift+F10, Alt+Left/Right/Up and Backspace; the rest of the owner's list mapped to later milestones; no AI shortcuts | note (6); the owner's S13 approval (Alt+Enter) |

## Gate checklist

The desktop completion gate (section 1): each desktop section and its
status (open, done, or deferred by the owner in writing, with the date).
Updated at every milestone release. The phone version is not a desktop
section; it waits until every row below is done or deferred.

| Section | Status |
|---|---|
| 2. Version 1.5: the stages left | open |
| 3. The Files app, later | open |
| 4. The Console | open |
| 5. After 1.5 (v1.6-v2.0) | open |
| 6. Later milestones (every item but the phone version) | open |
| 7. Standards still to write | open |
