# Madire OS QEMU tests, part 6, "the Files part" (1.5 stage 14a; the
# owner's decision Q1 of the stage 14 report: a seventh job, since part
# static has no room for the A3 move and parts 2 and 3 none for stages 14
# and 15). Sourced by tests/qemu-tests.sh (a full run, or --shard 6).
# In stage 14a, apart from this header, every line was moved unchanged from
# part 5: the Trash, Cut, Copy and Paste and the Files menus (1.5 stage
# 13). The Files sessions of stages 14 and 15 follow it here. Each section
# boots or builds what it reads itself (no file or variable from another
# part).

section "[1.5] The Trash, Cut, Copy and Paste, the rename field's selection and the Files menus: Live boots E1-E3, links and the Trash on disk (boot F) (test=desktop, 1024x768x32; stage 13)"
# docs/plans/1.5.md, C5, C6, C6b, C6c (rule SL), notes (13) and (14) and the
# S13 decisions D1-D5, Q1-Q9 and the multi-select rules; files_app.h,
# ui_files.h, ui_menu.h, ui_textfield.h. Each boot as stage 12's (desktoplog,
# mouselog; tests/qmp_desktop.py follows every packet and key with
# tests/desktop_ref.py's model and draws the Files window from the kernel's
# "files:" lines; a Files menu's rows greyed or ticked come from its
# "opened" line, fmenu:). E1 (livehome): the rename field opens with the
# whole name selected, a typed character replaces it, Right ends it, Ctrl
# keys stay in the field; Paste greyed with an empty clipboard; Copy and
# Paste, the " 2" name, Cut and Paste (the clipboard emptied), Ctrl+A. E2:
# a selection of a folder and a file copied, cut and moved to the Trash
# (its folders made when first needed); the Trash view; Restore from its
# menu; Restore with "Keep both"; Restore all; Empty Trash cancelled, then
# done. E3: an item in /tmp deleted permanently after its question; Shift+
# Delete answered by the keyboard; Paste into itself refused; the item and
# empty-space menus (Copy, Paste), the Menu key and Shift+F10, Esc and a
# click outside; Properties of a folder. F: a marked read-write disk with a
# fast and a slow link (one to a folder outside /home/user), two planted
# .trashinfo files (a Path outside the home folder; a Path through the
# link) and a big file: the link moved to the Trash and restored as a
# link; the refused info file; the restore through a link refused; the
# slow link deleted permanently (its target unchanged); copying a link
# refused; a paste that fills the disk leaves no partial file; the info
# file Madire writes (cat); Empty Trash; sync; debugfs and e2fsck -fn.
F13_T0=$(date +%s)
F13_W=$OUT/files13
rm -rf "${F13_W:?}" && mkdir -p "$F13_W"
F13_DOC=/home/user/Documents
F13_E1=$OUT/serial-files13-e1.log
F13_E2=$OUT/serial-files13-e2.log
F13_E3=$OUT/serial-files13-e3.log
F13_F=$OUT/serial-files13-f.log
# f13_ws <n> <qmp_desktop output>: screen n equals the reference.
f13_ws() { grep -qE "^WSCREEN $1 size=1024x768 differ=0 " <<< "$2"; }
# f13_order <log> <line>...: the lines, each exactly, in this order.
f13_order() {
    local log=$1
    shift
    awk -v want="$(printf '%s\n' "$@")" 'BEGIN { n = split(want, w, "\n"); if (w[n] == "") n--; k = 1 }
         k <= n && $0 == w[k] { k++ } END { exit !(k > n) }' "$log"
}
# f13_health <log> <output>: the model's window and menu lines equal the
# kernel's, every packet exact, no panic.
f13_health() {
    grep -qE "^EVENTS predicted=([0-9]+) logged=\1 same=yes$" <<< "$2" &&
        grep -qE "^MENUS predicted=([0-9]+) logged=\1 same=yes$" <<< "$2" &&
        grep -qE "^PACKETS sent=([0-9]+) exact=\1$" <<< "$2" && ! grep -q "qmp_desktop: ERROR" <<< "$2" &&
        [ -s "$1" ] && ! grep -qE "PANIC|FAILED" "$1"
}
# f13_en_sel <log> <after line> <sel> <button>: the same in the first state
# line after <after line> whose selection is exactly <sel>.
f13_en_sel() {
    awk -v a="$2" -v s="sel=$3" -v b="$4" '$0 == a { on = 1; next }
        on && /^files: state / { ok = 0; for (i = 1; i <= NF; i++) if ($i == s) ok = 1
            if (ok) for (i = 1; i <= NF; i++) if ($i ~ /^en=/) { print substr($i, 4 + b, 1); exit } }' "$1"
}
# f13_en <log> <after line> <button>: the state line's en flag of button
# (ui_files.h's part number) in the first state line after <after line>.
f13_en() {
    awk -v a="$2" -v b="$3" '$0 == a { on = 1; next }
        on && /^files: state / { for (i = 1; i <= NF; i++) if ($i ~ /^en=/) { print substr($i, 4 + b, 1); exit } }' "$1"
}
F13_SETUP1="type:mkdir $F13_DOC/Sub{ret}{wait:1}echo alpha > $F13_DOC/a.txt{ret}{wait:1}echo beta > $F13_DOC/b.txt{ret}{wait:1}"
desktop_iso "files13" "test=desktop livehome desktoplog mouselog" ""
DT_OUT=""
if [ -n "$DT_ISO" ]; then
    DT_UNTIL="" desktop_boot "$F13_E1" "$DT_ISO" 1024x768 ready rel:-1000,-1000 "type:x" "$F13_SETUP1" \
        to:min@Console click \
        "kseq:meta_l:down,e,meta_l:up" "wait:Files: read /home/user: 6 entries in 1 holds of at most 16, 6 shown" \
        "kseq:down,ret" "wait:Files: read $F13_DOC: 3 entries in 1 holds of at most 16, 3 shown" fsync \
        to:fempty rpress rrelease fmenu:mouse "kseq:esc" fsync \
        to:fitem@1 click fsync "kseq:f2" fsync to:desktop fsync wshot \
        "kseq:ctrl:down,a,ctrl:up" fsync "type:x" fsync "kseq:esc" fsync "kseq:f2" fsync "kseq:right" fsync \
        "kseq:esc" fsync \
        "kseq:ctrl:down,c,ctrl:up" "wait:Files: 1 items copied from $F13_DOC" \
        to:fitem@0 dclick "wait:Files: read $F13_DOC/Sub: 0 entries in 1 holds of at most 16, 0 shown" \
        "kseq:ctrl:down,v,ctrl:up" "wait:Files: pasted 1 items into $F13_DOC/Sub (copy)" \
        "wait:Files: read $F13_DOC/Sub: 1 entries in 1 holds of at most 16, 1 shown" fsync to:desktop fsync wshot \
        "kseq:ctrl:down,v,ctrl:up" "wait:Files: read $F13_DOC/Sub: 2 entries in 1 holds of at most 16, 2 shown" \
        "kseq:alt:down,left,alt:up" "wait:Files: read $F13_DOC: 3 entries in 1 holds of at most 16, 3 shown" \
        to:fitem@2 click "kseq:ctrl:down,x,ctrl:up" "wait:Files: 1 items cut from $F13_DOC" \
        to:fitem@0 dclick "wait:Files: read $F13_DOC/Sub: 2 entries in 1 holds of at most 16, 2 shown" \
        "kseq:ctrl:down,v,ctrl:up" "wait:Files: pasted 1 items into $F13_DOC/Sub (move)" \
        "wait:Files: read $F13_DOC/Sub: 3 entries in 1 holds of at most 16, 3 shown" \
        "kseq:ctrl:down,a,ctrl:up" fsync \
        "kseq:alt:down,left,alt:up" "wait:Files: read $F13_DOC: 2 entries in 1 holds of at most 16, 2 shown" fsync
else : > "$F13_E1"; fi
F13_O1=$DT_OUT
grep -E "^(Files: |desktop: menu Files)" "$F13_E1" | head -n 40 | sed 's/^/      /'
grep -E "^(WSCREEN|EVENTS|MENUS|PACKETS|qmp_desktop)" <<< "$F13_O1" | sed 's/^/      E1: /'
if f13_order "$F13_E1" "files: field 5 0 1 a.txt" && f13_ws 1 "$F13_O1"; then
    pass "[1.5] Files (D1): F2 opens the rename field with the whole name selected (files: field 5 0 1 a.txt; the name on the accent box, no cursor; the screen pixel for pixel)"
else
    fail "[1.5] Files (D1): the selected rename field: $(grep -E '^files: field ' "$F13_E1" | head -n 4 | tr '\n' ' ') $(grep -E '^(WSCREEN 1 |qmp_desktop)' <<< "$F13_O1" | tr '\n' ' ')"
fi
if f13_order "$F13_E1" "files: field 5 0 1 a.txt" "files: field 1 0 0 x" "files: field 5 0 1 a.txt" "files: field 5 0 0 a.txt" &&
   ! grep -qE "^Files: 'a\.txt' renamed" "$F13_E1"; then
    pass "[1.5] Files (D1): a typed character replaces the selected name (x), Esc cancels the rename (a.txt kept); F2 again selects the whole name and Right ends the selection with the cursor at the end"
else
    fail "[1.5] Files (D1): typing over the selection, Right, Esc: $(grep -E '^files: field ' "$F13_E1" | head -n 6 | tr '\n' ' ')"
fi
if f13_order "$F13_E1" "files: field 5 0 1 a.txt" "files: field 1 0 0 x" &&
   ! awk '/^files: field 5 0 1 a\.txt$/ { on = 1 } on && /^files: field 1 0 0 x$/ { exit } on && /^files: state .* sel=0,1/ { bad = 1 }
          END { exit !bad }' "$F13_E1"; then
    pass "[1.5] Files (S10 D2 kept): Ctrl+A while the rename field is open stays in the field (the items' selection unchanged, the name still selected: the next character replaced it whole)"
else
    fail "[1.5] Files: Ctrl+A in the rename field: $(grep -E '^files: (field|state) ' "$F13_E1" | sed -n 1,8p | tr '\n' ' ')"
fi
F13_RD1="Files: read $F13_DOC: 3 entries in 1 holds of at most 16, 3 shown"
if grep -qE "^desktop: menu Files space opened by the mouse at \([0-9]+, [0-9]+\) 264x170, greyed 0010 ticked 0000$" "$F13_E1" &&
   [ "$(f13_en "$F13_E1" "$F13_RD1" 14)" = 0 ] && grep -qxF "desktop: menu Files space closed (Esc)" "$F13_E1"; then
    pass "[1.5] Files: with an empty clipboard Paste is greyed, the button (its en flag 0) and the empty space's right-click menu (264x170 dp, greyed 0010: Paste); Esc closes the menu"
else
    fail "[1.5] Files: Paste with an empty clipboard: $(grep -E '^desktop: menu Files' "$F13_E1" | head -n 3 | tr '\n' ' ') en[14]=$(f13_en "$F13_E1" "$F13_RD1" 14)"
fi
if f13_order "$F13_E1" "Files: 1 items copied from $F13_DOC" "Files: read $F13_DOC/Sub: 0 entries in 1 holds of at most 16, 0 shown" \
       "Files: pasted 1 items into $F13_DOC/Sub (copy)" "Files: read $F13_DOC/Sub: 1 entries in 1 holds of at most 16, 1 shown" &&
   grep -qE '^files: item 0 F 6 [0-9]+ a\.txt$' "$F13_E1" && f13_ws 2 "$F13_O1"; then
    pass "[1.5] Files: Ctrl+C copies the selected file, Ctrl+V in another folder pastes a copy (its 6 bytes; the screen pixel for pixel)"
else
    fail "[1.5] Files: Copy and Paste: $(grep -E '^Files: (1 items|pasted|read .*Sub)' "$F13_E1" | tr '\n' ' ') $(grep -E '^(WSCREEN 2 |qmp_desktop)' <<< "$F13_O1" | tr '\n' ' ')"
fi
if f13_order "$F13_E1" "Files: pasted 1 items into $F13_DOC/Sub (copy)" "Files: pasted 1 items into $F13_DOC/Sub (copy)" \
       "Files: read $F13_DOC/Sub: 2 entries in 1 holds of at most 16, 2 shown" &&
   grep -qE '^files: item 0 F 6 [0-9]+ a 2\.txt$' "$F13_E1"; then
    pass "[1.5] Files: pasting where the name is used gives 'a 2.txt' (C5)"
else
    fail "[1.5] Files: the ' 2' name: $(grep -E '^files: item [01] ' "$F13_E1" | tail -n 4 | tr '\n' ' ')"
fi
F13_RD2="Files: read $F13_DOC: 2 entries in 1 holds of at most 16, 2 shown"
if f13_order "$F13_E1" "Files: 1 items cut from $F13_DOC" "Files: pasted 1 items into $F13_DOC/Sub (move)" \
       "Files: read $F13_DOC/Sub: 3 entries in 1 holds of at most 16, 3 shown" "$F13_RD2" &&
   [ "$(f13_en "$F13_E1" "$F13_RD2" 14)" = 0 ]; then
    pass "[1.5] Files: Ctrl+X and Ctrl+V move the file (b.txt gone from Documents, in Sub: a rename on one filesystem); the clipboard is emptied after the move (Paste greyed again)"
else
    fail "[1.5] Files: Cut and Paste: $(grep -E '^Files: (1 items cut|pasted|read )' "$F13_E1" | tail -n 4 | tr '\n' ' ') en[14]=$(f13_en "$F13_E1" "$F13_RD2" 14)"
fi
if awk -v r="Files: read $F13_DOC/Sub: 3 entries in 1 holds of at most 16, 3 shown" '$0 == r { on = 1 }
        on && /^files: state .* sel=0,1,2( |$)/ { ok = 1; exit } END { exit !ok }' "$F13_E1"; then
    pass "[1.5] Files: Ctrl+A selects every item (sel=0,1,2)"
else
    fail "[1.5] Files: Ctrl+A: $(grep -E '^files: state ' "$F13_E1" | tail -n 3 | grep -oE 'sel=[^ ]*' | tr '\n' ' ')"
fi
if f13_health "$F13_E1" "$F13_O1"; then
    pass "[1.5] Files: boot E1's window and menu lines equal the model's (EVENTS, MENUS), every packet exact, no panic"
else
    fail "[1.5] Files: boot E1's lines: $(grep -E '^(EVENTS|MENUS|PACKETS|qmp_desktop)' <<< "$F13_O1" | tr '\n' ' ')"
fi
# E2: the Trash.
F13_SETUP2="type:mkdir $F13_DOC/Box{ret}{wait:1}echo gamma > $F13_DOC/Box/c.txt{ret}{wait:1}echo alpha > $F13_DOC/a.txt{ret}{wait:1}"
F13_TR="Files: the Trash's info files read"
F13_TF=/home/user/.local/share/Trash/files
# Files' own state line once a question is shown (mode=ask): the driver
# judges Esc (files_esc_closes) and finds the question's buttons from the
# lines read before it acts, and "Files: asks ..." comes before this line
# (full run 44882570155, boot E2: lines 1515 and 1521), so a key or click
# after a question waits for it.
F13_ASK_E2="files: state mode=ask view=list bar=live place=7 en=0000000000000000000011 chosen=0000100000000000000000 hover=- pressed=- tip=- top=0 cursor=0 ring=0 sb=0 rename=0 reading=0 sel=- trash=1 app=0 job=0 askfocus=0 askring=0 askst=00"
F13_ASK_E3="files: state mode=ask view=list bar=live place=2 en=0000000000000000000011 chosen=0000100000000000000000 hover=- pressed=- tip=- top=0 cursor=2 ring=0 sb=0 rename=0 reading=0 sel=2 trash=0 app=0 job=0 askfocus=0 askring=1 askst=00"
F13_ASK_F="files: state mode=ask view=list bar=none place=7 en=0000000000000000000011 chosen=0000100000000000000000 hover=- pressed=- tip=- top=0 cursor=0 ring=0 sb=0 rename=0 reading=0 sel=- trash=1 app=0 job=0 askfocus=0 askring=0 askst=00"
DT_OUT=""
if [ -n "$DT_ISO" ]; then
    DT_UNTIL="" desktop_boot "$F13_E2" "$DT_ISO" 1024x768 ready rel:-1000,-1000 "type:x" "$F13_SETUP2" \
        to:min@Console click \
        "kseq:meta_l:down,e,meta_l:up" "wait:Files: read /home/user: 6 entries in 1 holds of at most 16, 6 shown" \
        "kseq:down,ret" "wait:Files: read $F13_DOC: 2 entries in 1 holds of at most 16, 2 shown" fsync \
        to:fitem@0 click "kseq:ctrl:down" to:fitem@1 click "kseq:ctrl:up" \
        "kseq:ctrl:down,c,ctrl:up" "wait:Files: 2 items copied from $F13_DOC" \
        to:fplace@3 click "wait:Files: read /home/user/Downloads: 0 entries in 1 holds of at most 16, 0 shown" \
        "kseq:ctrl:down,v,ctrl:up" "wait:Files: pasted 2 items into /home/user/Downloads (copy)" \
        "wait:Files: read /home/user/Downloads: 2 entries in 1 holds of at most 16, 2 shown" fsync \
        "kseq:ctrl:down,a,ctrl:up" "kseq:ctrl:down,x,ctrl:up" "wait:Files: 2 items cut from /home/user/Downloads" \
        to:fplace@5 click "wait:Files: read /home/user/Music: 0 entries in 1 holds of at most 16, 0 shown" \
        "kseq:ctrl:down,v,ctrl:up" "wait:Files: pasted 2 items into /home/user/Music (move)" \
        "wait:Files: read /home/user/Music: 2 entries in 1 holds of at most 16, 2 shown" fsync \
        "kseq:ctrl:down,a,ctrl:up" "kseq:delete" "wait:Files: 2 items moved to the Trash" \
        "wait:Files: read /home/user/Music: 0 entries in 1 holds of at most 16, 0 shown" fsync \
        to:fplace@7 click "wait:$F13_TR: 2 items, 0 without a known original location" \
        "wait:Files: read $F13_TF: 2 entries in 1 holds of at most 16, 2 shown" fsync to:desktop fsync wshot \
        to:fitem@0 rpress rrelease fmenu:mouse to:row@Restore click "wait:Files: 'Box' restored to /home/user/Music" \
        "wait:Files: read $F13_TF: 1 entries in 1 holds of at most 16, 1 shown" fsync \
        to:task@Console click "type:echo again > /home/user/Music/a.txt{ret}{wait:1}" to:min@Console click fsync \
        to:fitem@0 click "to:fbtn@Restore" click \
        "wait:Files: asks ''a.txt' already exists in Home > Music.' (Keep both, Cancel)" fsync to:desktop fsync wshot \
        "to:fbtn@Ask OK" click "wait:Files: 'a (restored).txt' restored to /home/user/Music" \
        "wait:Files: read $F13_TF: 0 entries in 1 holds of at most 16, 0 shown" fsync \
        to:fplace@5 click "wait:Files: read /home/user/Music: 3 entries in 1 holds of at most 16, 3 shown" \
        "kseq:ctrl:down,a,ctrl:up" "kseq:delete" "wait:Files: 3 items moved to the Trash" \
        "wait:Files: read /home/user/Music: 0 entries in 1 holds of at most 16, 0 shown" fsync \
        to:fplace@7 click "wait:$F13_TR: 3 items, 0 without a known original location" \
        "wait:Files: read $F13_TF: 3 entries in 1 holds of at most 16, 3 shown" fsync \
        "to:fbtn@Restore all" click "wait:Files: 3 items restored, 0 without a known original location" \
        "wait:Files: read $F13_TF: 0 entries in 1 holds of at most 16, 0 shown" fsync \
        to:fplace@5 click "wait:Files: read /home/user/Music: 3 entries in 1 holds of at most 16, 3 shown" \
        to:fitem@0 click "kseq:delete" "wait:Files: 1 items moved to the Trash" \
        "wait:Files: read /home/user/Music: 2 entries in 1 holds of at most 16, 2 shown" fsync \
        to:fplace@7 click "wait:$F13_TR: 1 items, 0 without a known original location" \
        "wait:Files: read $F13_TF: 1 entries in 1 holds of at most 16, 1 shown" fsync \
        "to:fbtn@Empty Trash" click "wait:Files: asks 'Empty the Trash? 1 item will be deleted permanently.' (Empty Trash, Cancel)" \
        "wait:$F13_ASK_E2" \
        "kseq:esc" "wait:Files: the question answered: Cancel" fsync \
        "to:fbtn@Empty Trash" click "wait:Files: asks 'Empty the Trash? 1 item will be deleted permanently.' (Empty Trash, Cancel)" \
        "wait:$F13_ASK_E2" \
        "to:fbtn@Ask OK" click "wait:Files: the Trash emptied: 1 items deleted" fsync
else : > "$F13_E2"; fi
F13_O2=$DT_OUT
grep -E "^(Files: |files: t(loc|date) |desktop: menu (Files|Trash))" "$F13_E2" | head -n 60 | sed 's/^/      /'
grep -E "^(WSCREEN|EVENTS|MENUS|PACKETS|qmp_desktop)" <<< "$F13_O2" | sed 's/^/      E2: /'
if f13_order "$F13_E2" "Files: 2 items copied from $F13_DOC" "Files: pasted 2 items into /home/user/Downloads (copy)" \
       "Files: read /home/user/Downloads: 2 entries in 1 holds of at most 16, 2 shown" &&
   grep -qE '^files: item 0 D 0 [0-9]+ Box$' "$F13_E2" && grep -qE '^files: item 1 F 6 [0-9]+ a\.txt$' "$F13_E2"; then
    pass "[1.5] Files (multi-select): a folder and a file selected (a click, then Ctrl+click) are copied together (Ctrl+C, Ctrl+V: the folder with its file, and the file)"
else
    fail "[1.5] Files: copying a selection of a folder and a file: $(grep -E '^Files: (2 items|pasted|read /home/user/Downloads)' "$F13_E2" | tr '\n' ' ')"
fi
if f13_order "$F13_E2" "Files: 2 items cut from /home/user/Downloads" "Files: pasted 2 items into /home/user/Music (move)" \
       "Files: read /home/user/Music: 2 entries in 1 holds of at most 16, 2 shown"; then
    pass "[1.5] Files (multi-select): Ctrl+A and Ctrl+X cut the folder and the file, Ctrl+V moves both"
else
    fail "[1.5] Files: cutting a selection: $(grep -E '^Files: (2 items cut|pasted 2|read /home/user/Music)' "$F13_E2" | tr '\n' ' ')"
fi
if f13_order "$F13_E2" "Files: the Trash folders made in /home/user/.local/share/Trash" \
       "Files: 'Box' moved to the Trash as 'Box' (info file first)" "Files: 'a.txt' moved to the Trash as 'a.txt' (info file first)" \
       "Files: 2 items moved to the Trash" &&
   [ "$(grep -c '^Files: the Trash folders made' "$F13_E2")" -eq 1 ]; then
    pass "[1.5] Files (multi-select, D3): Delete moves the selected folder and file to the Trash, each its info file first; the Trash's folders are made when first needed (once)"
else
    fail "[1.5] Files: moving a selection to the Trash: $(grep -E "^Files: (the Trash folders|'.*' moved|[0-9]+ items moved)" "$F13_E2" | head -n 4 | tr '\n' ' ')"
fi
if grep -qE '^files: tloc 0 Home > Music$' "$F13_E2" && grep -qE '^files: tdate 0 1 [0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}$' "$F13_E2" &&
   grep -qE '^files: state .* place=7 .* trash=1 ' "$F13_E2" && f13_ws 1 "$F13_O2"; then
    pass "[1.5] Files: the Trash view (the sidebar's Trash): Name, Original location ('Home > Music', the path bar's words) and Deleted (the info file's date), its own command row (the screen pixel for pixel)"
else
    fail "[1.5] Files: the Trash view: $(grep -E '^files: t(loc|date) ' "$F13_E2" | head -n 4 | tr '\n' ' ') $(grep -E '^(WSCREEN 1 |qmp_desktop)' <<< "$F13_O2" | tr '\n' ' ')"
fi
if grep -qE "^desktop: menu Trash item opened by the mouse at \([0-9]+, [0-9]+\) 264x134, greyed 000 ticked 000$" "$F13_E2" &&
   f13_order "$F13_E2" "desktop: menu Trash item: Restore chosen" "Files: 'Box' restored to /home/user/Music"; then
    pass "[1.5] Files (D5): the Trash item's right-click menu (Restore, Delete permanently, Properties; 264x134 dp) restores the folder to its original location"
else
    fail "[1.5] Files: the Trash menu's Restore: $(grep -E "^(desktop: menu Trash|Files: 'Box' restored)" "$F13_E2" | head -n 4 | tr '\n' ' ')"
fi
if f13_order "$F13_E2" "Files: asks ''a.txt' already exists in Home > Music.' (Keep both, Cancel)" "files: ask 'a.txt' already exists in Home > Music." \
       "Files: the question answered: Keep both" "Files: 'a (restored).txt' restored to /home/user/Music" &&
   f13_ws 2 "$F13_O2"; then
    pass "[1.5] Files: Restore where the name is used asks \"'a.txt' already exists in Home > Music.\" (Cancel, Keep both; the question panel pixel for pixel); Keep both restores it as 'a (restored).txt'"
else
    fail "[1.5] Files: Keep both: $(grep -E "^Files: (asks|the question|'a)" "$F13_E2" | head -n 4 | tr '\n' ' ') $(grep -E '^(WSCREEN 2 |qmp_desktop)' <<< "$F13_O2" | tr '\n' ' ')"
fi
if f13_order "$F13_E2" "Files: 3 items moved to the Trash" "$F13_TR: 3 items, 0 without a known original location" \
       "Files: 3 items restored, 0 without a known original location" "Files: read /home/user/Music: 3 entries in 1 holds of at most 16, 3 shown"; then
    pass "[1.5] Files: Restore all puts every item of the Trash back"
else
    fail "[1.5] Files: Restore all: $(grep -E '^Files: (3 items|read /home/user/Music)' "$F13_E2" | tr '\n' ' ')"
fi
if f13_order "$F13_E2" "Files: asks 'Empty the Trash? 1 item will be deleted permanently.' (Empty Trash, Cancel)" \
       "Files: the question answered: Cancel" "Files: asks 'Empty the Trash? 1 item will be deleted permanently.' (Empty Trash, Cancel)" \
       "Files: the question answered: Empty Trash" "Files: the Trash emptied: 1 items deleted" &&
   f13_health "$F13_E2" "$F13_O2"; then
    pass "[1.5] Files (D4): Empty Trash asks 'Empty the Trash? 1 item will be deleted permanently.'; Esc cancels (nothing deleted), Empty Trash deletes; the model's window and menu lines equal the kernel's"
else
    fail "[1.5] Files: Empty Trash: $(grep -E '^Files: (asks .Empty|the question|the Trash emptied)' "$F13_E2" | tr '\n' ' ') $(grep -E '^(EVENTS|MENUS|PACKETS|qmp_desktop)' <<< "$F13_O2" | tr '\n' ' ')"
fi
# E3: Delete permanently, Paste into itself, the menus.
F13_SETUP3="type:mkdir $F13_DOC/Sub{ret}{wait:1}mkdir $F13_DOC/Box{ret}{wait:1}echo alpha > $F13_DOC/a.txt{ret}{wait:1}echo tmp > /tmp/t.txt{ret}{wait:1}"
DT_OUT=""
if [ -n "$DT_ISO" ]; then
    DT_UNTIL="" desktop_boot "$F13_E3" "$DT_ISO" 1024x768 ready rel:-1000,-1000 "type:x" "$F13_SETUP3" \
        to:min@Console click \
        "kseq:meta_l:down,e,meta_l:up" "wait:Files: read /home/user: 6 entries in 1 holds of at most 16, 6 shown" \
        "kseq:alt:down,up,alt:up" "wait:Files: read /home: 1 entries in 1 holds of at most 16, 1 shown" \
        "kseq:alt:down,up,alt:up" "wait:Files: read /: 2 entries in 1 holds of at most 16, 2 shown" \
        to:fitem@1 dclick "wait:Files: read /tmp: 1 entries in 1 holds of at most 16, 1 shown" \
        to:fitem@0 click "kseq:delete" "wait:Files: asks 'Delete 't.txt' permanently? It can't go to the Trash.' (Delete, Cancel)" \
        fsync to:desktop fsync wshot "to:fbtn@Ask OK" click "wait:Files: 't.txt' deleted permanently" \
        "wait:Files: read /tmp: 0 entries in 1 holds of at most 16, 0 shown" fsync \
        to:fplace@2 click "wait:Files: read $F13_DOC: 3 entries in 1 holds of at most 16, 3 shown" \
        to:fitem@2 click "kseq:shift:down,delete,shift:up" "wait:Files: asks 'Delete 'a.txt' permanently?' (Delete, Cancel)" \
        "wait:$F13_ASK_E3" \
        "kseq:esc" "wait:Files: the question answered: Cancel" \
        "kseq:shift:down,delete,shift:up" "wait:Files: asks 'Delete 'a.txt' permanently?' (Delete, Cancel)" \
        "kseq:tab" fsync "kseq:ret" "wait:Files: 'a.txt' deleted permanently" \
        "wait:Files: read $F13_DOC: 2 entries in 1 holds of at most 16, 2 shown" fsync \
        to:fitem@1 click "kseq:ctrl:down,c,ctrl:up" "wait:Files: 1 items copied from $F13_DOC" \
        to:fitem@1 dclick "wait:Files: read $F13_DOC/Sub: 0 entries in 1 holds of at most 16, 0 shown" \
        "kseq:ctrl:down,v,ctrl:up" "wait:files: status 1 Can't paste 'Sub' into itself." \
        "wait:Files: read $F13_DOC/Sub: 0 entries in 1 holds of at most 16, 0 shown" \
        "kseq:alt:down,left,alt:up" "wait:Files: read $F13_DOC: 2 entries in 1 holds of at most 16, 2 shown" fsync \
        to:fitem@1 rpress rrelease fmenu:mouse to:desktop fsync wshot to:row@Copy click \
        "wait:Files: 1 items copied from $F13_DOC" \
        to:fempty rpress rrelease fmenu:mouse to:row@Paste click "wait:Files: pasted 1 items into $F13_DOC (copy)" \
        "wait:Files: read $F13_DOC: 3 entries in 1 holds of at most 16, 3 shown" fsync \
        to:fitem@0 click "kseq:compose" fmenu:key "kseq:esc" fsync \
        "kseq:shift:down,f10,shift:up" fmenu:key to:desktop click fsync \
        to:fitem@0 rpress rrelease fmenu:mouse to:row@Properties click fsync to:desktop fsync wshot "kseq:esc" fsync
else : > "$F13_E3"; fi
F13_O3=$DT_OUT
grep -E "^(Files: |desktop: menu Files)" "$F13_E3" | head -n 50 | sed 's/^/      /'
grep -E "^(WSCREEN|EVENTS|MENUS|PACKETS|qmp_desktop)" <<< "$F13_O3" | sed 's/^/      E3: /'
if f13_order "$F13_E3" "Files: asks 'Delete 't.txt' permanently? It can't go to the Trash.' (Delete, Cancel)" \
       "files: ask Delete 't.txt' permanently?" "files: ask2 It can't go to the Trash." "Files: the question answered: Delete" \
       "Files: 't.txt' deleted permanently" "Files: read /tmp: 0 entries in 1 holds of at most 16, 0 shown" &&
   f13_ws 1 "$F13_O3"; then
    pass "[1.5] Files (Q7): an item outside /home/user (in /tmp) can't go to the Trash: the question \"Delete 't.txt' permanently? It can't go to the Trash.\" (the panel pixel for pixel), then Delete deletes it"
else
    fail "[1.5] Files: deleting outside the home folder: $(grep -E "^Files: (asks .Delete .t|the question|'t\.txt')" "$F13_E3" | tr '\n' ' ') $(grep -E '^(WSCREEN 1 |qmp_desktop)' <<< "$F13_O3" | tr '\n' ' ')"
fi
if f13_order "$F13_E3" "Files: asks 'Delete 'a.txt' permanently?' (Delete, Cancel)" "Files: the question answered: Cancel" \
       "Files: asks 'Delete 'a.txt' permanently?' (Delete, Cancel)" "Files: the question answered: Delete" "Files: 'a.txt' deleted permanently" &&
   grep -qE '^files: state mode=ask .* askfocus=1 askring=1 ' "$F13_E3"; then
    pass "[1.5] Files: Shift+Delete asks \"Delete 'a.txt' permanently?\" with the focus ring (the keyboard); Esc cancels; Tab moves the focus to Delete and Enter deletes"
else
    fail "[1.5] Files: Shift+Delete: $(grep -E "^Files: (asks .Delete .a|the question|'a\.txt')" "$F13_E3" | tr '\n' ' ')"
fi
if grep -qxF "files: status 1 Can't paste 'Sub' into itself." "$F13_E3" && ! grep -qE "^Files: pasted [0-9]+ items into $F13_DOC/Sub " "$F13_E3"; then
    pass "[1.5] Files: pasting a folder into itself is refused: \"Can't paste 'Sub' into itself.\" (nothing made)"
else
    fail "[1.5] Files: Paste into itself: $(grep -E "^(files: status 1|Files: pasted)" "$F13_E3" | tr '\n' ' ')"
fi
if grep -qE "^desktop: menu Files item opened by the mouse at \([0-9]+, [0-9]+\) 264x287, greyed 0000000 ticked 0000000$" "$F13_E3" &&
   f13_order "$F13_E3" "desktop: menu Files item: Copy chosen" "desktop: menu Files item closed (a row chosen)" \
       "Files: 1 items copied from $F13_DOC" && f13_ws 2 "$F13_O3"; then
    pass "[1.5] Files (D5): a right click on an item opens its menu (Open, Cut, Copy, Paste, Rename, Move to Trash, Properties with their key hints; 264x287 dp; the screen pixel for pixel); Copy copies it"
else
    fail "[1.5] Files: the item's menu: $(grep -E '^desktop: menu Files item' "$F13_E3" | head -n 3 | tr '\n' ' ') $(grep -E '^(WSCREEN 2 |qmp_desktop)' <<< "$F13_O3" | tr '\n' ' ')"
fi
if f13_order "$F13_E3" "desktop: menu Files space: Paste chosen" "Files: pasted 1 items into $F13_DOC (copy)" &&
   grep -qE '^files: item 2 D 0 [0-9]+ Sub 2$' "$F13_E3"; then
    pass "[1.5] Files (D5): a right click on empty space opens its menu (New Folder, New File, Paste, Show hidden files); Paste pastes into the folder shown ('Sub 2')"
else
    fail "[1.5] Files: the empty space's menu: $(grep -E '^(desktop: menu Files space|Files: pasted)' "$F13_E3" | tr '\n' ' ')"
fi
if grep -qE "^desktop: menu Files item opened by the keyboard at \([0-9]+, [0-9]+\) 264x287, greyed 0000000 ticked 0000000, Open selected$" "$F13_E3" &&
   f13_order "$F13_E3" "desktop: menu Files item closed (Esc)"; then
    pass "[1.5] Files (D5): the Menu key opens the selected item's menu at the item (Open selected, the focus ring); Esc closes it"
else
    fail "[1.5] Files: the Menu key: $(grep -E '^desktop: menu Files item (opened by the keyboard|closed)' "$F13_E3" | tr '\n' ' ')"
fi
if [ "$(grep -cE '^desktop: menu Files item opened by the keyboard ' "$F13_E3")" -eq 2 ] &&
   f13_order "$F13_E3" "desktop: menu Files item closed (Esc)" "desktop: menu Files item closed (a click outside)"; then
    pass "[1.5] Files (D5): Shift+F10 opens the same menu; a click outside closes it"
else
    fail "[1.5] Files: Shift+F10: $(grep -E '^desktop: menu Files item (opened by the keyboard|closed)' "$F13_E3" | tr '\n' ' ')"
fi
if f13_order "$F13_E3" "desktop: menu Files item: Properties chosen" && grep -qE '^files: panel D 0 [0-9]+ Box$' "$F13_E3" &&
   grep -qE '^files: state mode=panel .* app=1 ' "$F13_E3" && f13_ws 3 "$F13_O3" && f13_health "$F13_E3" "$F13_O3"; then
    pass "[1.5] Files (D5): Properties opens the info panel for a folder too, without 'No app can open this file yet.' (the screen pixel for pixel); boot E3's window and menu lines equal the model's, no panic"
else
    fail "[1.5] Files: Properties, or boot E3's lines: $(grep -E '^(files: panel|desktop: menu Files item: Prop)' "$F13_E3" | tr '\n' ' ') $(grep -E '^(WSCREEN 3 |EVENTS|MENUS|qmp_desktop)' <<< "$F13_O3" | tr '\n' ' ')"
fi
# Boot F: Disk mode, a marked read-write disk.
F13_T=$F13_W/tree
mkdir -p "$F13_T/outside" "$F13_T/user/Docs" "$F13_T/user/.local/share/Trash/files" "$F13_T/user/.local/share/Trash/info"
for d in Desktop Documents Downloads Pictures Music Videos; do mkdir -p "$F13_T/user/$d"; done
printf 'target\n' > "$F13_T/outside/target.txt"
printf 'keep me\n' > "$F13_T/user/Docs/keep.txt"
head -c 2600000 /dev/zero | tr '\0' 'b' > "$F13_T/user/Docs/big.bin"
ln -s /home/outside "$F13_T/user/Docs/fast"
F13_SLOWT="/home/outside/$(printf '%060d' 0)/../target.txt"
ln -s "$F13_SLOWT" "$F13_T/user/Docs/slow"
printf 'bad\n' > "$F13_T/user/.local/share/Trash/files/bad.txt"
printf 'via\n' > "$F13_T/user/.local/share/Trash/files/via.txt"
printf '[Trash Info]\nPath=/tmp/evil\nDeletionDate=2026-11-20T10:00:00\n' > "$F13_T/user/.local/share/Trash/info/bad.txt.trashinfo"
printf '[Trash Info]\nPath=/home/user/Docs/fast/via.txt\nDeletionDate=2026-11-20T10:00:00\n' > "$F13_T/user/.local/share/Trash/info/via.txt.trashinfo"
F13_FOK=1
disk_mkfs "$F13_W/fs.img" 4096 1024 256 256 "$F13_T" > "$F13_W/mkfs.txt" 2>&1 &&
    disk_image mbr "$F13_W/fs.img" "$F13_W/disk.img" > /dev/null 2>&1 || F13_FOK=0
F13_TARGET=$(sha256sum < "$F13_T/outside/target.txt" | cut -d' ' -f1)
F13_TI=/home/user/.local/share/Trash/info
desktop_iso "files13disk" "test=desktop desktoplog mouselog" ""
DT_OUT=""
if [ -n "$DT_ISO" ] && [ "$F13_FOK" -eq 1 ]; then
    DT_DISK="$F13_W/disk.img" DT_UNTIL="" desktop_boot "$F13_F" "$DT_ISO" 1024x768 ready rel:-1000,-1000 "type:x" \
        to:min@Console click \
        "kseq:meta_l:down,e,meta_l:up" "wait:Files: read /home/user: 8 entries in 1 holds of at most 16, 7 shown" \
        to:fitem@1 dclick "wait:Files: read /home/user/Docs: 4 entries in 1 holds of at most 16, 4 shown" \
        to:fitem@1 click "kseq:delete" "wait:Files: 'fast' moved to the Trash as 'fast' (info file first)" \
        "wait:Files: read /home/user/Docs: 3 entries in 1 holds of at most 16, 3 shown" \
        to:fplace@7 click "wait:Files: the Trash's info files read: 3 items, 1 without a known original location" \
        "wait:Files: read $F13_TF: 3 entries in 1 holds of at most 16, 3 shown" \
        to:fitem@0 click fsync \
        to:fitem@1 click "to:fbtn@Restore" click "wait:Files: 'fast' restored to /home/user/Docs" \
        "wait:Files: read $F13_TF: 2 entries in 1 holds of at most 16, 2 shown" fsync \
        to:fitem@1 click "to:fbtn@Restore" click \
        "wait:files: status 1 Can't restore 'via.txt': /home/user/Docs/fast is not a folder." \
        "wait:Files: read /home/user/.local/share/Trash/files: 2 entries in 1 holds of at most 16, 2 shown" \
        "kseq:alt:down,left,alt:up" "wait:Files: read /home/user/Docs: 4 entries in 1 holds of at most 16, 4 shown" \
        to:fitem@3 click "kseq:shift:down,delete,shift:up" "wait:Files: asks 'Delete 'slow' permanently?' (Delete, Cancel)" \
        "kseq:tab" fsync "kseq:ret" "wait:Files: 'slow' deleted permanently" \
        "wait:Files: read /home/user/Docs: 3 entries in 1 holds of at most 16, 3 shown" \
        to:fitem@1 click "kseq:ctrl:down,c,ctrl:up" "wait:Files: 1 items copied from /home/user/Docs" \
        to:fplace@1 click "wait:Files: read /home/user/Desktop: 0 entries in 1 holds of at most 16, 0 shown" \
        "kseq:ctrl:down,v,ctrl:up" "wait:files: status 1 Can't copy 'fast': it is a link, and links can't be copied yet." \
        "wait:Files: read /home/user/Desktop: 0 entries in 1 holds of at most 16, 0 shown" \
        "kseq:alt:down,left,alt:up" "wait:Files: read /home/user/Docs: 3 entries in 1 holds of at most 16, 3 shown" \
        to:fitem@0 click "kseq:ctrl:down,c,ctrl:up" "wait:Files: 1 items copied from /home/user/Docs" \
        to:fplace@1 click "wait:Files: read /home/user/Desktop: 0 entries in 1 holds of at most 16, 0 shown" \
        "kseq:ctrl:down,v,ctrl:up" "wait:files: status 1 Can't paste 'big.bin': the disk is full." \
        "wait:Files: read /home/user/Desktop: 0 entries in 1 holds of at most 16, 0 shown" \
        "kseq:alt:down,left,alt:up" "wait:Files: read /home/user/Docs: 3 entries in 1 holds of at most 16, 3 shown" \
        to:fitem@2 click "kseq:delete" "wait:Files: 'keep.txt' moved to the Trash as 'keep.txt' (info file first)" \
        "wait:Files: read /home/user/Docs: 2 entries in 1 holds of at most 16, 2 shown" fsync \
        to:task@Console click "type:cat $F13_TI/keep.txt.trashinfo{ret}{wait:2}" to:min@Console click fsync \
        to:fplace@7 click "wait:Files: the Trash's info files read: 3 items, 1 without a known original location" \
        "wait:Files: read $F13_TF: 3 entries in 1 holds of at most 16, 3 shown" \
        "to:fbtn@Empty Trash" click "wait:Files: asks 'Empty the Trash? 3 items will be deleted permanently.' (Empty Trash, Cancel)" \
        "wait:$F13_ASK_F" \
        "to:fbtn@Ask OK" click "wait:Files: the Trash emptied: 3 items deleted" fsync \
        to:task@Console click "type:sync{ret}{wait:4}"
else : > "$F13_F"; fi
F13_OF=$DT_OUT
grep -E "^(Files: |files: t(loc|date) |\[Trash Info\]|Path=|DeletionDate=)" "$F13_F" | head -n 60 | sed 's/^/      /'
grep -E "^(WSCREEN|EVENTS|MENUS|PACKETS|qmp_desktop)" <<< "$F13_OF" | sed 's/^/      F: /'
F13_P=$F13_W/part.img
disk_image extract "$F13_W/disk.img" "$F13_P" > /dev/null 2>&1 || F13_FOK=0
F13_FAST=$(disk_debugfs "$F13_P" "stat /user/Docs/fast" 2>/dev/null | grep -E 'Fast link dest|Type:' | tr '\n' ' ')
F13_DOCS=$(disk_debugfs "$F13_P" "ls -p /user/Docs" | awk -F/ 'NF >= 7 && $6 != "." && $6 != ".." { print $6 }' | LC_ALL=C sort | tr '\n' ' ')
F13_DESK=$(disk_debugfs "$F13_P" "ls -p /user/Desktop" | awk -F/ 'NF >= 7 && $6 != "." && $6 != ".." { print $6 }' | tr '\n' ' ')
F13_TF=$(disk_debugfs "$F13_P" "ls -p /user/.local/share/Trash/files" | awk -F/ 'NF >= 7 && $6 != "." && $6 != ".." { print $6 }' | tr '\n' ' ')
F13_TIN=$(disk_debugfs "$F13_P" "ls -p /user/.local/share/Trash/info" | awk -F/ 'NF >= 7 && $6 != "." && $6 != ".." { print $6 }' | tr '\n' ' ')
F13_TGT=$(disk_debugfs "$F13_P" "cat /outside/target.txt" | sha256sum | cut -d' ' -f1)
F13_FSCK=$(disk_fsck "$F13_P"); F13_FRC=$?
F13_FX=$(grep -vE '^(e2fsck [0-9]+\.[0-9]+.*|Pass [1-5]: .*|madire-test: [0-9]+/[0-9]+ files \(.*\), [0-9]+/[0-9]+ blocks|)$' <<< "$F13_FSCK")
echo "      F on disk: fast: $F13_FAST; Docs: $F13_DOCS; Desktop: [$F13_DESK]; Trash files: [$F13_TF] info: [$F13_TIN]; e2fsck -fn exit $F13_FRC${F13_FX:+: $F13_FX}"
if [ "$F13_FOK" -eq 1 ] && f13_order "$F13_F" "Files: 'fast' moved to the Trash as 'fast' (info file first)" &&
   grep -qE '^files: item 1 L [0-9]+ [0-9]+ fast$' "$F13_F"; then
    pass "[1.5] Files (rule SL): a link (to a folder outside /home/user) moves to the Trash as a link, its info file first, and shows in the Trash as a link"
else
    fail "[1.5] Files: a link to the Trash: $(grep -E "^Files: 'fast'" "$F13_F" | tr '\n' ' ')"
fi
if grep -qE '^files: tloc 0 \(unknown original location\)$' "$F13_F" && grep -qE '^files: tdate 0 0 2026-11-20 10:00$' "$F13_F" &&
   grep -qE "^Files: Trash item 'bad\.txt': its info file was refused \([^)]*\): it can't be restored$" "$F13_F" &&
   grep -qE '^files: tloc 2 Home > Docs > fast$' "$F13_F" &&
   [ "$(f13_en_sel "$F13_F" "Files: the Trash's info files read: 3 items, 1 without a known original location" 0 16)" = 0 ]; then
    pass "[1.5] Files (addition a): a .trashinfo whose Path is outside /home/user is refused: '(unknown original location)', its date kept, Restore greyed for it (the bounded parser)"
else
    fail "[1.5] Files: a refused .trashinfo: $(grep -E "^(files: t(loc|date) |Files: Trash item)" "$F13_F" | head -n 6 | tr '\n' ' ')"
fi
if f13_order "$F13_F" "Files: 'fast' moved to the Trash as 'fast' (info file first)" "Files: 'fast' restored to /home/user/Docs" &&
   grep -q 'Fast link dest: "/home/outside"' <<< "$F13_FAST"; then
    pass "[1.5] Files (rule SL): Restore puts the link back as the same fast link (debugfs: 'Fast link dest: \"/home/outside\"'), never its target"
else
    fail "[1.5] Files: restoring a link: $(grep -E "^Files: 'fast' restored" "$F13_F" | tr '\n' ' ') debugfs: $F13_FAST"
fi
if grep -qxF "files: status 1 Can't restore 'via.txt': /home/user/Docs/fast is not a folder." "$F13_F" &&
   ! grep -qE "^Files: 'via\.txt' restored" "$F13_F"; then
    pass "[1.5] Files (rule SL): a Path through a link is refused: \"Can't restore 'via.txt': /home/user/Docs/fast is not a folder.\" (nothing restored)"
else
    fail "[1.5] Files: Restore through a link: $(grep -E "^(files: status 1 Can.t restore|Files: 'via)" "$F13_F" | tr '\n' ' ')"
fi
if f13_order "$F13_F" "Files: asks 'Delete 'slow' permanently?' (Delete, Cancel)" "Files: 'slow' deleted permanently" &&
   ! grep -qw slow <<< "$F13_DOCS" && [ "$F13_TGT" = "$F13_TARGET" ]; then
    pass "[1.5] Files (rule SL): Delete permanently removes a slow link as a link: gone from the disk, its target (target.txt outside /home/user) unchanged"
else
    fail "[1.5] Files: deleting a link: Docs [$F13_DOCS] target ${F13_TGT:0:12} (want ${F13_TARGET:0:12})"
fi
if grep -qxF "files: status 1 Can't copy 'fast': it is a link, and links can't be copied yet." "$F13_F" && ! grep -qw fast <<< "$F13_DESK"; then
    pass "[1.5] Files (rule SL): copying a link is refused: \"Can't copy 'fast': it is a link, and links can't be copied yet.\" (nothing made)"
else
    fail "[1.5] Files: copying a link: $(grep -E "^files: status 1 Can.t copy" "$F13_F" | tr '\n' ' ') Desktop [$F13_DESK]"
fi
if grep -qxF "files: status 1 Can't paste 'big.bin': the disk is full." "$F13_F" && [ -z "${F13_DESK// /}" ]; then
    pass "[1.5] Files (D4): a paste that fills the disk stops with \"Can't paste 'big.bin': the disk is full.\" and leaves no partial file (debugfs: the Desktop folder empty)"
else
    fail "[1.5] Files: a full disk: $(grep -E "^files: status 1 Can.t paste" "$F13_F" | tr '\n' ' ') Desktop [$F13_DESK]"
fi
if f13_order "$F13_F" "Files: 'keep.txt' moved to the Trash as 'keep.txt' (info file first)" "[Trash Info]" "Path=/home/user/Docs/keep.txt" &&
   grep -qE '^DeletionDate=[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}$' "$F13_F"; then
    pass "[1.5] Files (C6): the info file Madire writes reads back: '[Trash Info]', 'Path=/home/user/Docs/keep.txt', 'DeletionDate=YYYY-MM-DDThh:mm:ss' (cat at the console)"
else
    fail "[1.5] Files: the info file written: $(grep -E '^(\[Trash Info\]|Path=|DeletionDate=)' "$F13_F" | tr '\n' ' ')"
fi
if f13_order "$F13_F" "Files: asks 'Empty the Trash? 3 items will be deleted permanently.' (Empty Trash, Cancel)" \
       "Files: the Trash emptied: 3 items deleted" &&
   [ -z "${F13_TF// /}" ] && [ -z "${F13_TIN// /}" ] && [ "$F13_TGT" = "$F13_TARGET" ] &&
   [ "$F13_FRC" -eq 0 ] && [ -z "$F13_FX" ] && f13_health "$F13_F" "$F13_OF"; then
    pass "[1.5] Files: Empty Trash in Disk mode deletes every item and every info file (the orphan and refused ones too: debugfs, files/ and info/ empty), the link's target unchanged; e2fsck -fn reports nothing; the model's lines equal the kernel's, no panic"
else
    fail "[1.5] Files: Empty Trash on disk: files [$F13_TF] info [$F13_TIN] e2fsck $F13_FRC${F13_FX:+: $F13_FX} $(grep -E '^(EVENTS|MENUS|qmp_desktop)' <<< "$F13_OF" | tr '\n' ' ')"
fi
echo "      [1.5] the Trash and the clipboard (part 5): $(( $(date +%s) - F13_T0 )) s (report only)"

section "[1.5] This PC, the v1.5 pointer set, box selection, drag and drop in Files: Live boots G1, G2, Disk boots H (a CD drive), H2 (after a reboot) and I (read-only) (test=desktop, 1024x768x32; stage 14b)"
# docs/plans/1.5.md, C5, notes (11), (12), (14), (15) and item 7 of the
# stage 14b approval; files_app.h, ui_files.h, ui_list.h, pointer_core.h.
# Each boot as stage 13's (desktoplog, mouselog; tests/qmp_desktop.py follows
# every packet and key with tests/desktop_ref.py's model, draws the Files
# window from the kernel's "files:" lines, the pointer of the kernel's last
# "desktop: pointer" line and the drag's badge from "files: drag start").
# G1 (livehome): This PC in Live mode (two cards), opened by Enter and by a
# double click, Back; the hand over the path bar; the four resize pointers
# at the Files window's edges; the move pointer during a title drag; the
# I-beam over the rename field; a box selection, one with Ctrl (added to
# the selection), one with Ctrl cancelled by Esc (the selection put back).
# G2: a drag onto a file refused, cancelled by Esc; a file dragged onto a
# folder (move), one with Ctrl (copy); a selection of two dragged from one
# of them (press on a selected item) onto the sidebar's Desktop; a folder
# onto itself refused; a folder dropped on the sidebar's Trash; a drag out
# of the Trash refused; Restore. H: a marked read-write disk and an empty CD
# drive (AHCI port 1): This PC's three cards (the disk's figures from its
# superblock), the disk's card opens /home; a file of /tmp dropped on the
# sidebar's Documents (another filesystem: a copy, the file stays); an
# 8,000,000-byte copy by a Ctrl drop: the busy pointer, its frames, packets
# taken while it runs; a move onto a folder; sync. H2: the same disk after
# a reboot shows the move. I: a read-only disk: its card "(read-only)",
# drops refused, the image unchanged. Every value below was worked out from
# the rules.
F14_T0=$(date +%s)
F14_W=$OUT/files14
rm -rf "${F14_W:?}" && mkdir -p "$F14_W"
F14_DOC=/home/user/Documents
F14_TF=/home/user/.local/share/Trash/files
F14_G1=$OUT/serial-files14-g1.log
F14_G2=$OUT/serial-files14-g2.log
F14_H=$OUT/serial-files14-h.log
F14_H2=$OUT/serial-files14-h2.log
F14_I=$OUT/serial-files14-i.log
F14_XY='\(-?[0-9]+, -?[0-9]+\)'
# f14_ws <n> <qmp_desktop output>: screen n equals the reference.
f14_ws() { grep -qE "^WSCREEN $1 size=1024x768 differ=0 " <<< "$2"; }
# f14_order <log> <line>...: the lines, each exactly, in this order (given
# through the environment: awk -v would change their backslashes).
f14_order() {
    local log=$1
    shift
    F14_WANT="$(printf '%s\n' "$@")" awk 'BEGIN { n = split(ENVIRON["F14_WANT"], w, "\n"); if (w[n] == "") n--; k = 1 }
         k <= n && $0 == w[k] { k++ } END { exit !(k > n) }' "$log"
}
# f14_rorder <log> <ERE>...: lines matching the expressions, in this order.
f14_rorder() {
    local log=$1
    shift
    F14_WANT="$(printf '%s\n' "$@")" awk 'BEGIN { n = split(ENVIRON["F14_WANT"], w, "\n"); if (w[n] == "") n--; k = 1 }
         k <= n && $0 ~ w[k] { k++ } END { exit !(k > n) }' "$log"
}
# f14_health <log> <output>: the model's window and menu lines equal the
# kernel's, every packet exact, no panic.
f14_health() {
    grep -qE "^EVENTS predicted=([0-9]+) logged=\1 same=yes$" <<< "$2" &&
        grep -qE "^MENUS predicted=([0-9]+) logged=\1 same=yes$" <<< "$2" &&
        grep -qE "^PACKETS sent=([0-9]+) exact=\1$" <<< "$2" && ! grep -q "qmp_desktop: ERROR" <<< "$2" &&
        [ -s "$1" ] && ! grep -qE "PANIC|FAILED" "$1"
}
# f14_budget <log>: the RAM filesystem's budget in bytes (vfs.c's line).
f14_budget() {
    sed -nE 's/^Filesystem: live RAM filesystem, [0-9]+ nodes, [0-9]+ MiB per file, budget ([0-9]+) pages$/\1/p' "$1" |
        head -n 1 | awk '{ print $1 * 4096 }'
}
# f14_card <log> <k> <icon> <bar> <ro> <name>: card k's line (the first one)
# with these values; its used and total bytes on stdout.
f14_card() {
    awk -v k="$2" -v ic="$3" -v bar="bar=$4" -v ro="ro=$5" -v name="$6" '
        $1 == "files:" && $2 == "card" && $3 == k {
            rest = $0; sub(/^files: card [0-9]+ [a-z]+ bar=[01] ro=[01] used=[0-9]+ total=[0-9]+ /, "", rest)
            if ($4 == ic && $5 == bar && $6 == ro && rest == name) { print substr($7, 6), substr($8, 7); ok = 1 }
            exit }
        END { exit !ok }' "$1"
}
F14_SET="type:mkdir $F14_DOC/Box{ret}{wait:1}echo alpha > $F14_DOC/a.txt{ret}{wait:1}"
F14_SET="${F14_SET}echo beta > $F14_DOC/b.txt{ret}{wait:1}echo gamma > $F14_DOC/c.txt{ret}{wait:1}"
F14_RD4="Files: read $F14_DOC: 4 entries in 1 holds of at most 16, 4 shown"
F14_RD3="Files: read $F14_DOC: 3 entries in 1 holds of at most 16, 3 shown"
desktop_iso "files14" "test=desktop livehome desktoplog mouselog" ""
DT_OUT=""
if [ -n "$DT_ISO" ]; then
    DT_UNTIL="" desktop_boot "$F14_G1" "$DT_ISO" 1024x768 ready rel:-1000,-1000 "type:x" "$F14_SET" \
        to:min@Console click \
        "kseq:meta_l:down,e,meta_l:up" "wait:Files: read /home/user: 6 entries in 1 holds of at most 16, 6 shown" \
        to:fplace@8 click "wait:Files: This PC: 2 storage areas" fsync to:desktop fsync wshot \
        "kseq:down" fsync "kseq:ret" "wait:Files: This PC: 'Memory (Live): /, /tmp' opened at /" \
        "wait:Files: read /: 2 entries in 1 holds of at most 16, 2 shown" \
        "kseq:alt:down,left,alt:up" "wait:Files: This PC: 2 storage areas" fsync \
        to:fcard@1 dclick fsync to:fcard@0 dclick "wait:Files: This PC: 'Memory (Live): /, /tmp' opened at /" \
        "wait:Files: read /: 2 entries in 1 holds of at most 16, 2 shown" \
        to:fplace@2 click "wait:$F14_RD4" fsync \
        to:fpath@0 fsync wshot \
        to:edge-bottomleft@Files fsync wshot to:edge-right@Files fsync to:edge-bottom@Files fsync \
        to:edge-bottomright@Files fsync \
        to:title@Files drag:-40,-20 fsync \
        to:fitem@1 click to:desktop "kseq:f2" fsync to:ffield fsync wshot "kseq:esc" fsync \
        to:fempty press to:fitem@1 fsync wshot release "wait:Files: box selection done: 3 items selected" fsync \
        to:fitem@0 click fsync "kseq:ctrl:down" to:fempty press to:fitem@2 release \
        "wait:Files: box selection done: 3 items selected" fsync \
        to:fempty press to:fitem@1 fsync "kseq:esc" "wait:Files: box selection cancelled: 3 items selected" \
        release "kseq:ctrl:up" fsync to:desktop fsync
else : > "$F14_G1"; fi
F14_O1=$DT_OUT
grep -E "^(Files: |files: (pc|card|band [a-z]) |desktop: pointer )" "$F14_G1" | head -n 50 | sed 's/^/      /'
grep -E "^(WSCREEN|DRAG|EVENTS|MENUS|PACKETS|qmp_desktop)" <<< "$F14_O1" | sed 's/^/      G1: /'
F14_BUD=$(f14_budget "$F14_G1")
F14_C0=$(f14_card "$F14_G1" 0 memory 1 0 "Memory (Live): /, /tmp")
if [ -n "$F14_BUD" ] && [ -n "$F14_C0" ] && [ "${F14_C0#* }" = "$F14_BUD" ] && [ "${F14_C0% *}" -gt 0 ] &&
   [ "${F14_C0% *}" -le "$F14_BUD" ] && [ $(( ${F14_C0% *} % 4096 )) -eq 0 ] &&
   f14_card "$F14_G1" 1 disk 0 0 "No disk in use" > /dev/null && ! grep -qE '^files: card 2 ' "$F14_G1" &&
   grep -qxF "files: pc 2 -1 -1" "$F14_G1" && grep -qE '^files: state .* place=8 ' "$F14_G1" && f14_ws 1 "$F14_O1"; then
    pass "[1.5] Files (note 15, Q2, Q3): This PC in Live mode shows two cards: 'Memory (Live): /, /tmp' (used ${F14_C0% *} of the budget $F14_BUD bytes, a bar) and 'No disk in use'; no CD card without an ATAPI device; This PC active in the sidebar (the screen pixel for pixel)"
else
    fail "[1.5] Files: This PC in Live mode: budget [$F14_BUD] card 0 [$F14_C0] $(grep -E '^files: (pc|card) ' "$F14_G1" | head -n 3 | tr '\n' ' ') $(grep -E '^(WSCREEN 1 |qmp_desktop)' <<< "$F14_O1" | tr '\n' ' ')"
fi
if f14_order "$F14_G1" "Files: This PC: 2 storage areas" "files: pc 2 0 -1" "Files: This PC: 'Memory (Live): /, /tmp' opened at /" \
       "Files: read /: 2 entries in 1 holds of at most 16, 2 shown" "Files: This PC: 2 storage areas" \
       "Files: This PC: 'Memory (Live): /, /tmp' opened at /" "Files: read /: 2 entries in 1 holds of at most 16, 2 shown" &&
   [ "$(grep -cE "^Files: This PC: '" "$F14_G1")" -eq 2 ] && grep -qE '^files: pc 2 1 ' "$F14_G1"; then
    pass "[1.5] Files (C5): Down chooses the first card and Enter opens the memory at /; Alt+Left goes back to This PC; a double click on 'No disk in use' selects it and opens nothing, a double click on the memory card opens /"
else
    fail "[1.5] Files: opening This PC's cards: $(grep -E "^(Files: This PC|files: pc |Files: read /:)" "$F14_G1" | head -n 10 | tr '\n' ' ')"
fi
if f14_rorder "$F14_G1" "^$F14_RD4\$" "^desktop: pointer hand at $F14_XY\$" && f14_ws 2 "$F14_O1"; then
    pass "[1.5] Pointers (note 15): over a part of the path bar that jumps (Home) the pointer is the hand, its hotspot at the fingertip (the screen pixel for pixel)"
else
    fail "[1.5] Pointers: the hand: $(grep -E '^desktop: pointer ' "$F14_G1" | head -n 4 | tr '\n' ' ') $(grep -E '^(WSCREEN 2 |qmp_desktop)' <<< "$F14_O1" | tr '\n' ' ')"
fi
if f14_rorder "$F14_G1" "^desktop: pointer resize-nesw at $F14_XY\$" "^desktop: pointer resize-ew at $F14_XY\$" \
       "^desktop: pointer resize-ns at $F14_XY\$" "^desktop: pointer resize-nwse at $F14_XY\$" && f14_ws 3 "$F14_O1"; then
    pass "[1.5] Pointers (note 15): the Files window's grab band shows the resize pointer of its edge: bottom-left resize-nesw (the screen pixel for pixel), right resize-ew, bottom resize-ns, bottom-right resize-nwse"
else
    fail "[1.5] Pointers: the edges: $(grep -E '^desktop: pointer resize' "$F14_G1" | head -n 5 | tr '\n' ' ') $(grep -E '^(WSCREEN 3 |qmp_desktop)' <<< "$F14_O1" | tr '\n' ' ')"
fi
if f14_rorder "$F14_G1" "^desktop: pointer resize-nwse at $F14_XY\$" "^desktop: pointer move at $F14_XY\$" \
       "^desktop: drag Files to \(" "^desktop: pointer arrow at $F14_XY\$" &&
   grep -qE "^DRAG 1 asked \((-?[0-9]+), (-?[0-9]+)\) got \(\1, \2\) " <<< "$F14_O1"; then
    pass "[1.5] Pointers (note 15): a title drag shows the move pointer while the button is held, the arrow after the release"
else
    fail "[1.5] Pointers: the move pointer: $(grep -E '^(desktop: pointer (move|arrow)|desktop: drag Files)' "$F14_G1" | head -n 4 | tr '\n' ' ') $(grep -E '^DRAG 1' <<< "$F14_O1")"
fi
if f14_rorder "$F14_G1" "^files: field 5 0 1 a\.txt\$" "^desktop: pointer ibeam at $F14_XY\$" && f14_ws 4 "$F14_O1"; then
    pass "[1.5] Pointers (note 15): over the rename field the pointer is the I-beam (the screen pixel for pixel)"
else
    fail "[1.5] Pointers: the I-beam: $(grep -E '^(files: field|desktop: pointer ibeam)' "$F14_G1" | head -n 3 | tr '\n' ' ') $(grep -E '^(WSCREEN 4 |qmp_desktop)' <<< "$F14_O1" | tr '\n' ' ')"
fi
if f14_order "$F14_G1" "Files: box selection done: 3 items selected" "files: band done 3" &&
   grep -qE '^files: band [0-9]+ [0-9]+ [0-9]+ [0-9]+$' "$F14_G1" &&
   awk '/^files: band done 3$/ { on = 1 } on && /^files: state .* sel=1,2,3( |$)/ { ok = 1; exit } END { exit !ok }' "$F14_G1" &&
   f14_ws 5 "$F14_O1"; then
    pass "[1.5] Files (note 14): a press on empty space and a drag draws the box (the accent frame over its tint, the rows it touches selected: the screen pixel for pixel); the release keeps 3 items selected (sel=1,2,3)"
else
    fail "[1.5] Files: box selection: $(grep -E '^(files: band|Files: box)' "$F14_G1" | head -n 4 | tr '\n' ' ') $(grep -E '^(WSCREEN 5 |qmp_desktop)' <<< "$F14_O1" | tr '\n' ' ')"
fi
if [ "$(grep -cxF "Files: box selection done: 3 items selected" "$F14_G1")" -eq 2 ] &&
   awk 'n == 2 && /^files: state .* sel=0,2,3( |$)/ { ok = 1; exit } /^files: band done 3$/ { n++ } END { exit !ok }' "$F14_G1"; then
    pass "[1.5] Files (note 14): with Ctrl the box adds to the selection (Box kept, b.txt and c.txt added: sel=0,2,3)"
else
    fail "[1.5] Files: a box with Ctrl: $(grep -E '^files: band (done|cancelled)' "$F14_G1" | tr '\n' ' ') $(grep -E '^files: state ' "$F14_G1" | tail -n 2 | grep -oE 'sel=[^ ]*' | tr '\n' ' ')"
fi
if f14_order "$F14_G1" "Files: box selection cancelled: 3 items selected" "files: band cancelled 3" &&
   awk '/^files: band cancelled 3$/ { on = 1 } on && /^files: state / { ok = / sel=0,2,3( |$)/; exit } END { exit !ok }' "$F14_G1" &&
   grep -qE '^files: state .* sel=0,1,2,3( |$)' "$F14_G1" && ! grep -qxF "desktop: close Files" "$F14_G1"; then
    pass "[1.5] Files (item 7): Esc while drawing a box cancels it: the selection it started from is put back (sel=0,1,2,3 while drawn, sel=0,2,3 after); Esc does not close Files then"
else
    fail "[1.5] Files: Esc during a box: $(grep -E '^(files: band (done|cancelled)|desktop: close Files)' "$F14_G1" | tr '\n' ' ')"
fi
if f14_health "$F14_G1" "$F14_O1"; then
    pass "[1.5] Files: boot G1's window and menu lines equal the model's (EVENTS, MENUS), every packet exact, no panic"
else
    fail "[1.5] Files: boot G1's lines: $(grep -E '^(EVENTS|MENUS|PACKETS|qmp_desktop)' <<< "$F14_O1" | tr '\n' ' ')"
fi
# G2: drag and drop (Live mode, one filesystem: a drop moves, Ctrl copies).
F14_RD1="Files: read $F14_DOC: 1 entries in 1 holds of at most 16, 1 shown"
DT_OUT=""
if [ -n "$DT_ISO" ]; then
    DT_UNTIL="" desktop_boot "$F14_G2" "$DT_ISO" 1024x768 ready rel:-1000,-1000 "type:x" "$F14_SET" \
        to:min@Console click \
        "kseq:meta_l:down,e,meta_l:up" "wait:Files: read /home/user: 6 entries in 1 holds of at most 16, 6 shown" \
        to:fplace@2 click "wait:$F14_RD4" fsync \
        to:fitem@0 press to:fitem@3 fsync "kseq:esc" "wait:Files: drag cancelled (Esc): nothing changed" release fsync \
        to:fitem@1 press to:fitem@0 fsync wshot release "wait:Files: 1 item dropped into $F14_DOC/Box (move)" \
        "wait:Files: dropped 1 items into $F14_DOC/Box (move)" "wait:$F14_RD3" fsync \
        "kseq:ctrl:down" to:fitem@1 press to:fitem@0 fsync wshot release \
        "wait:Files: 1 item dropped into $F14_DOC/Box (copy)" "wait:Files: dropped 1 items into $F14_DOC/Box (copy)" \
        "wait:$F14_RD3" "kseq:ctrl:up" fsync \
        to:fitem@1 click "kseq:ctrl:down" to:fitem@2 click "kseq:ctrl:up" fsync \
        to:fitem@1 press to:fplace@1 fsync wshot release "wait:Files: 2 items dropped into /home/user/Desktop (move)" \
        "wait:Files: dropped 2 items into /home/user/Desktop (move)" "wait:$F14_RD1" fsync \
        to:fitem@0 press rel:24,0 fsync wshot release "wait:Files: drop refused (into itself): nothing changed" fsync \
        to:fitem@0 press to:fplace@7 fsync release "wait:Files: 1 item dropped on the Trash" \
        "wait:Files: 'Box' moved to the Trash as 'Box' (info file first)" \
        "wait:Files: read $F14_DOC: 0 entries in 1 holds of at most 16, 0 shown" fsync \
        to:fplace@7 click "wait:Files: the Trash's info files read: 1 items, 0 without a known original location" \
        "wait:Files: read $F14_TF: 1 entries in 1 holds of at most 16, 1 shown" fsync \
        to:fitem@0 press to:fplace@2 fsync release "wait:Files: drop refused (from the Trash): nothing changed" fsync \
        to:fitem@0 click "to:fbtn@Restore" click "wait:Files: 'Box' restored to $F14_DOC" \
        "wait:Files: read $F14_TF: 0 entries in 1 holds of at most 16, 0 shown" fsync \
        to:fplace@2 click "wait:$F14_RD1" to:fitem@0 dclick \
        "wait:Files: read $F14_DOC/Box: 2 entries in 1 holds of at most 16, 2 shown" fsync to:desktop fsync
else : > "$F14_G2"; fi
F14_O2=$DT_OUT
grep -E "^(Files: |files: (drag|drop [a-z]) )" "$F14_G2" | head -n 50 | sed 's/^/      /'
grep -E "^(WSCREEN|EVENTS|MENUS|PACKETS|qmp_desktop)" <<< "$F14_O2" | sed 's/^/      G2: /'
if f14_order "$F14_G2" "files: drag start 1 Box" "files: drag over c.txt no (not a folder)" "Files: drag cancelled (Esc): nothing changed" \
       "files: drag cancelled (Esc)" "files: drag start 1 a.txt" &&
   ! grep -qxF "Files: drop refused (not a folder): nothing changed" "$F14_G2" && ! grep -qxF "desktop: close Files" "$F14_G2"; then
    pass "[1.5] Files (note 15): a drag over a file is refused (\"files: drag over c.txt no (not a folder)\"); Esc cancels the drag with nothing changed and does not close Files"
else
    fail "[1.5] Files: a drag onto a file, Esc: $(grep -E '^(files: drag|Files: drag)' "$F14_G2" | head -n 5 | tr '\n' ' ')"
fi
if f14_order "$F14_G2" "files: drag start 1 a.txt" "files: drag over $F14_DOC/Box move" "Files: 1 item dropped into $F14_DOC/Box (move)" \
       "files: drop move 1 $F14_DOC/Box" "Files: dropped 1 items into $F14_DOC/Box (move)" "$F14_RD3" &&
   f14_rorder "$F14_G2" "^files: drag start 1 a\.txt\$" "^desktop: pointer drag-move at $F14_XY\$" "^files: drop move 1 " &&
   f14_ws 1 "$F14_O2"; then
    pass "[1.5] Files (note 15): a file dragged onto a folder moves it (the drag-move pointer, the badge 'a.txt', the folder marked as the target: the screen pixel for pixel); the drop runs Paste's move"
else
    fail "[1.5] Files: a drop that moves: $(grep -E '^(files: (drag|drop)|Files: (1 item|dropped))' "$F14_G2" | sed -n 3,9p | tr '\n' ' ') $(grep -E '^(WSCREEN 1 |qmp_desktop)' <<< "$F14_O2" | tr '\n' ' ')"
fi
if f14_order "$F14_G2" "files: drag start 1 b.txt" "files: drag over $F14_DOC/Box copy" "Files: 1 item dropped into $F14_DOC/Box (copy)" \
       "files: drop copy 1 $F14_DOC/Box" "Files: dropped 1 items into $F14_DOC/Box (copy)" "$F14_RD3" &&
   f14_rorder "$F14_G2" "^files: drag start 1 b\.txt\$" "^desktop: pointer drag-copy at $F14_XY\$" "^files: drop copy 1 " &&
   f14_ws 2 "$F14_O2"; then
    pass "[1.5] Files (note 15): with Ctrl held the drop copies (the drag-copy pointer: the screen pixel for pixel); b.txt stays in Documents (3 items)"
else
    fail "[1.5] Files: a drop that copies: $(grep -E '^(files: drag over .* copy|files: drop copy|Files: (1 item|dropped) .*copy)' "$F14_G2" | tr '\n' ' ') $(grep -E '^(WSCREEN 2 |qmp_desktop)' <<< "$F14_O2" | tr '\n' ' ')"
fi
if f14_order "$F14_G2" "files: drag start 2 2 items" "files: drag over /home/user/Desktop move" \
       "Files: 2 items dropped into /home/user/Desktop (move)" "Files: dropped 2 items into /home/user/Desktop (move)" "$F14_RD1" &&
   grep -qE '^files: drop -1 1$' "$F14_G2" && f14_ws 3 "$F14_O2"; then
    pass "[1.5] Files (notes 14, 15): a press on an item already selected keeps the selection, so two items drag together (the badge '2 items') onto the sidebar's Desktop (the place marked: the screen pixel for pixel)"
else
    fail "[1.5] Files: dragging a selection: $(grep -E '^(files: drag start 2|files: drag over /home/user/Desktop|Files: 2 items)' "$F14_G2" | tr '\n' ' ') $(grep -E '^(WSCREEN 3 |qmp_desktop)' <<< "$F14_O2" | tr '\n' ' ')"
fi
if f14_order "$F14_G2" "files: drag over $F14_DOC/Box no (into itself)" "Files: drop refused (into itself): nothing changed" \
       "files: drop no (into itself)" &&
   f14_rorder "$F14_G2" "^files: drag over .*/Box no \(into itself\)\$" "^desktop: pointer no-drop at $F14_XY\$" &&
   f14_ws 4 "$F14_O2"; then
    pass "[1.5] Files (note 15): a folder dragged onto itself is refused (the no-drop pointer: the screen pixel for pixel); nothing changes"
else
    fail "[1.5] Files: a folder onto itself: $(grep -E '^(files: drag over .*itself|files: drop no|Files: drop refused)' "$F14_G2" | tr '\n' ' ') $(grep -E '^(WSCREEN 4 |qmp_desktop)' <<< "$F14_O2" | tr '\n' ' ')"
fi
if f14_order "$F14_G2" "files: drag over Trash trash" "Files: 1 item dropped on the Trash" "files: drop trash 1" \
       "Files: 'Box' moved to the Trash as 'Box' (info file first)" "Files: read $F14_DOC: 0 entries in 1 holds of at most 16, 0 shown"; then
    pass "[1.5] Files (note 15): a folder dropped on the sidebar's Trash moves to the Trash (its info file first, as the Delete key)"
else
    fail "[1.5] Files: a drop on the Trash: $(grep -E "^(files: drag over Trash|files: drop trash|Files: (1 item dropped on|'Box' moved))" "$F14_G2" | tr '\n' ' ')"
fi
if f14_order "$F14_G2" "Files: read $F14_TF: 1 entries in 1 holds of at most 16, 1 shown" \
       "files: drag over /home/user/Documents no (from the Trash)" "Files: drop refused (from the Trash): nothing changed" \
       "Files: 'Box' restored to $F14_DOC" &&
   f14_rorder "$F14_G2" "^Files: read $F14_DOC/Box: 2 entries in 1 holds of at most 16, 2 shown\$" \
       "^files: item 0 F 6 [0-9]+ a\.txt\$" "^files: item 1 F 5 [0-9]+ b\.txt\$" &&
   f14_health "$F14_G2" "$F14_O2"; then
    pass "[1.5] Files (Q6, item 7): nothing drags out of the Trash (refused: from the Trash); the folder dropped on the Trash is restored with what it held (a.txt moved, b.txt copied); boot G2's lines equal the model's, no panic"
else
    fail "[1.5] Files: the Trash after a drop: $(grep -E "^(files: drag over .*from the Trash|Files: drop refused .from|Files: 'Box' restored|Files: read .*Box:)" "$F14_G2" | tr '\n' ' ') $(grep -E '^(EVENTS|MENUS|qmp_desktop)' <<< "$F14_O2" | tr '\n' ' ')"
fi
# Boot H: Disk mode, a marked read-write disk on AHCI port 0, an empty CD
# drive on port 1.
F14_T=$F14_W/tree
mkdir -p "$F14_T/user/Documents/Box"
for d in Desktop Downloads Pictures Music Videos; do mkdir -p "$F14_T/user/$d"; done
printf 'alpha\n' > "$F14_T/user/Documents/a.txt"
head -c 8000000 /dev/zero | tr '\0' 'b' > "$F14_T/user/Documents/big.bin"
F14_HOK=1
disk_mkfs "$F14_W/fs.img" 24576 1024 256 256 "$F14_T" > "$F14_W/mkfs.txt" 2>&1 &&
    disk_image mbr "$F14_W/fs.img" "$F14_W/disk.img" > /dev/null 2>&1 || F14_HOK=0
F14_STATS=$(disk_debugfs "$F14_W/fs.img" "stats" 2>/dev/null)
F14_BC=$(sed -nE 's/^Block count: +([0-9]+)$/\1/p' <<< "$F14_STATS")
F14_FB=$(sed -nE 's/^Free blocks: +([0-9]+)$/\1/p' <<< "$F14_STATS")
F14_DTOT=$(( ${F14_BC:-0} * 1024 ))
F14_DUSED=$(( (${F14_BC:-0} - ${F14_FB:-0}) * 1024 ))
F14_DISK="Disk 0 'madire-test': /home"
desktop_iso "files14disk" "test=desktop desktoplog mouselog" ""
F14_DISO=$DT_ISO
DT_OUT=""
if [ -n "$F14_DISO" ] && [ "$F14_HOK" -eq 1 ]; then
    DT_DISK="$F14_W/disk.img" DT_CD=1 DT_UNTIL="" desktop_boot "$F14_H" "$F14_DISO" 1024x768 ready rel:-1000,-1000 \
        "type:x" "type:echo tdata > /tmp/t.txt{ret}{wait:1}" to:min@Console click \
        "kseq:meta_l:down,e,meta_l:up" "wait:Files: read /home/user: 6 entries in 1 holds of at most 16, 6 shown" \
        to:fplace@8 click "wait:Files: This PC: 3 storage areas" fsync to:desktop fsync wshot \
        to:fcard@1 dclick "wait:Files: This PC: '$F14_DISK' opened at /home" \
        "wait:Files: read /home: 2 entries in 1 holds of at most 16, 2 shown" fsync \
        "kseq:alt:down,left,alt:up" "wait:Files: This PC: 3 storage areas" fsync \
        to:fcard@0 dclick "wait:Files: This PC: 'Memory: /, /tmp' opened at /" \
        "wait:Files: read /: 2 entries in 1 holds of at most 16, 2 shown" fsync \
        to:fitem@1 dclick "wait:Files: read /tmp: 1 entries in 1 holds of at most 16, 1 shown" fsync \
        to:fitem@0 press to:fplace@2 fsync release "wait:Files: 1 item dropped into $F14_DOC (copy)" \
        "wait:Files: dropped 1 items into $F14_DOC (copy)" \
        "wait:Files: read /tmp: 1 entries in 1 holds of at most 16, 1 shown" fsync \
        to:fplace@2 click "wait:$F14_RD4" fsync \
        "kseq:ctrl:down" to:fitem@2 press to:fitem@0 fsync release "wait:Files: 1 item dropped into $F14_DOC/Box (copy)" \
        rel:6,0 rel:-6,0 "kseq:ctrl:up" "wait:Files: dropped 1 items into $F14_DOC/Box (copy)" "wait:$F14_RD4" fsync \
        to:fitem@1 press to:fitem@0 fsync release "wait:Files: 1 item dropped into $F14_DOC/Box (move)" \
        "wait:Files: dropped 1 items into $F14_DOC/Box (move)" "wait:$F14_RD3" fsync \
        to:task@Console click "type:sync{ret}sync{ret}" "wait:sync: nothing to save."
else : > "$F14_H"; fi
F14_OH=$DT_OUT
grep -E "^(Files: |files: (pc|card|drag|drop [a-z]) |desktop: (pointer|busy) |ahci: port 1|sync: )" "$F14_H" | head -n 50 | sed 's/^/      /'
grep -E "^(WSCREEN|EVENTS|MENUS|PACKETS|qmp_desktop)" <<< "$F14_OH" | sed 's/^/      H: /'
F14_HBUD=$(f14_budget "$F14_H")
F14_HC0=$(f14_card "$F14_H" 0 memory 1 0 "Memory: /, /tmp")
F14_HC1=$(f14_card "$F14_H" 1 disk 1 0 "$F14_DISK")
if [ "$F14_HOK" -eq 1 ] && [ -n "$F14_HBUD" ] && [ "${F14_HC0#* }" = "$F14_HBUD" ] && [ "$F14_DTOT" -gt 0 ] &&
   [ "$F14_HC1" = "$F14_DUSED $F14_DTOT" ] && f14_card "$F14_H" 2 cd 0 0 "CD drive: not used" > /dev/null &&
   grep -qxF "ahci: port 1: signature 0xeb140101: ATAPI device, ignored (no command sent)" "$F14_H" && f14_ws 1 "$F14_OH"; then
    pass "[1.5] Files (note 15, Q2-Q4): This PC in Disk mode: 'Memory: /, /tmp' (the budget $F14_HBUD bytes), '$F14_DISK' with its superblock's figures ($F14_DUSED used of $F14_DTOT bytes) and 'CD drive: not used' (the ATAPI device on port 1) (the screen pixel for pixel)"
else
    fail "[1.5] Files: This PC in Disk mode: superblock $F14_DUSED/$F14_DTOT cards [$F14_HC0] [$F14_HC1] $(grep -E '^(files: card|ahci: port 1)' "$F14_H" | head -n 4 | tr '\n' ' ') $(grep -E '^(WSCREEN 1 |qmp_desktop)' <<< "$F14_OH" | tr '\n' ' ')"
fi
if f14_order "$F14_H" "Files: This PC: '$F14_DISK' opened at /home" "Files: read /home: 2 entries in 1 holds of at most 16, 2 shown" \
       "Files: This PC: 'Memory: /, /tmp' opened at /" "Files: read /: 2 entries in 1 holds of at most 16, 2 shown"; then
    pass "[1.5] Files (C5): the disk's card opens /home (lost+found and user), the memory's card opens /"
else
    fail "[1.5] Files: opening the disk's card: $(grep -E "^(Files: This PC: '|Files: read /home:|Files: read /:)" "$F14_H" | tr '\n' ' ')"
fi
if f14_order "$F14_H" "files: drag start 1 t.txt" "files: drag over $F14_DOC copy" "Files: 1 item dropped into $F14_DOC (copy)" \
       "files: drop copy 1 $F14_DOC" "Files: dropped 1 items into $F14_DOC (copy)" \
       "Files: read /tmp: 1 entries in 1 holds of at most 16, 1 shown" "$F14_RD4"; then
    pass "[1.5] Files (owner's resolution 1): a file of /tmp dropped on the sidebar's Documents (on the disk) is copied without Ctrl (another filesystem); it stays in /tmp"
else
    fail "[1.5] Files: a drop across filesystems: $(grep -E '^(files: drag (start|over) .*t\.txt|files: drag over /home/user/Documents |files: drop copy 1 /home/user/Documents$|Files: (1 item|dropped) .* \(copy\))' "$F14_H" | tr '\n' ' ')"
fi
F14_BUSY=$(awk -v a="files: drop copy 1 $F14_DOC/Box" '$0 == a { on = 1; next }
    on && /^desktop: pointer busy at / { b = 1; next }
    on && b && /^mouse: packet [0-9]+: / { p++ ; next }
    on && b && /^desktop: busy pointer: [0-9]+ ticks, [0-9]+ frames drawn$/ { print p + 0, $4, $6; exit }' "$F14_H")
if [ -n "$F14_BUSY" ] && [ "$(cut -d' ' -f1 <<< "$F14_BUSY")" -ge 2 ] && [ "$(cut -d' ' -f3 <<< "$F14_BUSY")" -ge 2 ] &&
   f14_order "$F14_H" "files: drop copy 1 $F14_DOC/Box" "Files: dropped 1 items into $F14_DOC/Box (copy)"; then
    pass "[1.5] Pointers (note 15): during an 8,000,000-byte copy by a Ctrl drop the pointer is busy and animated (packets, ticks, frames: $F14_BUSY), and the pointer's packets are taken while it runs"
else
    fail "[1.5] Pointers: the busy pointer: [$F14_BUSY] $(grep -E '^desktop: (pointer busy|busy pointer)' "$F14_H" | head -n 3 | tr '\n' ' ')"
fi
F14_P=$F14_W/part.img
disk_image extract "$F14_W/disk.img" "$F14_P" > /dev/null 2>&1 || F14_HOK=0
# f14_ls <folder>: its names in the partition (debugfs ls -p), a file's with its size.
f14_ls() {
    disk_debugfs "$F14_P" "ls -p $1" |
        awk -F/ 'NF >= 7 && $6 != "." && $6 != ".." { print $6 ($3 ~ /^10/ ? " " $7 : "") }' | LC_ALL=C sort | tr '\n' ' '
}
F14_DOCS=$(f14_ls /user/Documents)
F14_BOX=$(f14_ls /user/Documents/Box)
F14_FSCK=$(disk_fsck "$F14_P"); F14_FRC=$?
F14_FX=$(grep -vE '^(e2fsck [0-9]+\.[0-9]+.*|Pass [1-5]: .*|madire-test: [0-9]+/[0-9]+ files \(.*\), [0-9]+/[0-9]+ blocks|)$' <<< "$F14_FSCK")
echo "      H on disk: Documents: [$F14_DOCS] Box: [$F14_BOX] e2fsck -fn exit $F14_FRC${F14_FX:+: $F14_FX}"
if [ "$F14_HOK" -eq 1 ] && f14_order "$F14_H" "files: drag start 1 a.txt" "files: drag over $F14_DOC/Box move" \
       "Files: 1 item dropped into $F14_DOC/Box (move)" "Files: dropped 1 items into $F14_DOC/Box (move)" "$F14_RD3" &&
   [ "$F14_DOCS" = "Box big.bin 8000000 t.txt 6 " ] && [ "$F14_BOX" = "a.txt 6 big.bin 8000000 " ] &&
   [ "$F14_FRC" -eq 0 ] && [ -z "$F14_FX" ] && f14_health "$F14_H" "$F14_OH"; then
    pass "[1.5] Files (item 7): in Disk mode a drop moves a file onto a folder; after sync the disk holds the result (debugfs: Documents Box, big.bin, t.txt; Box a.txt, big.bin, 8,000,000 bytes); e2fsck -fn reports nothing; boot H's lines equal the model's, no panic"
else
    fail "[1.5] Files: a move on the disk: Documents [$F14_DOCS] Box [$F14_BOX] e2fsck $F14_FRC${F14_FX:+: $F14_FX} $(grep -E '^(EVENTS|MENUS|qmp_desktop)' <<< "$F14_OH" | tr '\n' ' ')"
fi
# H2: the same disk after a reboot.
DT_OUT=""
if [ -n "$F14_DISO" ] && [ "$F14_HOK" -eq 1 ]; then
    DT_DISK="$F14_W/disk.img" DT_UNTIL="" desktop_boot "$F14_H2" "$F14_DISO" 1024x768 ready rel:-1000,-1000 \
        "kseq:meta_l:down,e,meta_l:up" "wait:Files: read /home/user: 6 entries in 1 holds of at most 16, 6 shown" \
        to:fplace@2 click "wait:$F14_RD3" to:fitem@0 dclick \
        "wait:Files: read $F14_DOC/Box: 2 entries in 1 holds of at most 16, 2 shown" fsync to:desktop fsync
else : > "$F14_H2"; fi
F14_OH2=$DT_OUT
grep -E "^(WSCREEN|EVENTS|PACKETS|qmp_desktop)" <<< "$F14_OH2" | sed 's/^/      H2: /'
if f14_order "$F14_H2" "$F14_RD3" &&
   f14_rorder "$F14_H2" "^Files: read $F14_DOC/Box: 2 entries in 1 holds of at most 16, 2 shown\$" \
       "^files: item 0 F 6 [0-9]+ a\.txt\$" "^files: item 1 F 8000000 [0-9]+ big\.bin\$" &&
   f14_health "$F14_H2" "$F14_OH2"; then
    pass "[1.5] Files (item 7): the drag's move survives a reboot: the same disk shows a.txt (6 bytes) and the copied big.bin (8,000,000 bytes) in Box; no panic"
else
    fail "[1.5] Files: after the reboot: $(grep -E '^(Files: read |files: item [01] )' "$F14_H2" | tail -n 4 | tr '\n' ' ') $(grep -E '^(EVENTS|qmp_desktop)' <<< "$F14_OH2" | tr '\n' ' ')"
fi
# Boot I: Disk mode, read-only (an unknown RO_COMPAT flag, D11).
F14_TR=$F14_W/rotree
mkdir -p "$F14_TR/user/Documents/Sub"
for d in Desktop Downloads Pictures Music Videos; do mkdir -p "$F14_TR/user/$d"; done
printf 'alpha\n' > "$F14_TR/user/Documents/a.txt"
F14_IOK=1
disk_mkfs "$F14_W/ro.img" 4096 1024 256 256 "$F14_TR" > "$F14_W/romkfs.txt" 2>&1 &&
    python3 -I -B "$(dirname "$0")/ext2_view.py" set "$F14_W/ro.img" "$F14_W/ro-fs.img" $((1024 + 100)) 4 0x8 or > /dev/null 2>&1 &&
    disk_image mbr "$F14_W/ro-fs.img" "$F14_W/rodisk.img" > /dev/null 2>&1 || F14_IOK=0
F14_RSTATS=$(disk_debugfs "$F14_W/ro.img" "stats" 2>/dev/null)
F14_RBC=$(sed -nE 's/^Block count: +([0-9]+)$/\1/p' <<< "$F14_RSTATS")
F14_RFB=$(sed -nE 's/^Free blocks: +([0-9]+)$/\1/p' <<< "$F14_RSTATS")
F14_RSUM0=$(sha256sum "$F14_W/rodisk.img" 2>/dev/null | cut -c1-64)
DT_OUT=""
if [ -n "$F14_DISO" ] && [ "$F14_IOK" -eq 1 ]; then
    DT_DISK="$F14_W/rodisk.img" DT_UNTIL="" desktop_boot "$F14_I" "$F14_DISO" 1024x768 ready rel:-1000,-1000 \
        "kseq:meta_l:down,e,meta_l:up" "wait:Files: read /home/user: 6 entries in 1 holds of at most 16, 6 shown" \
        to:fplace@8 click "wait:Files: This PC: 2 storage areas" fsync \
        to:fplace@2 click "wait:Files: read $F14_DOC: 2 entries in 1 holds of at most 16, 2 shown" fsync \
        to:fitem@1 press to:fitem@0 fsync release "wait:Files: drop refused (read-only): nothing changed" fsync \
        to:fitem@1 press to:fplace@7 fsync release "wait:Files: drop refused (read-only): nothing changed" fsync \
        to:desktop fsync
else : > "$F14_I"; fi
F14_OI=$DT_OUT
F14_RSUM1=$(sha256sum "$F14_W/rodisk.img" 2>/dev/null | cut -c1-64)
grep -E "^(Files: |files: (card|drag over|drop no) |ext2: )" "$F14_I" | head -n 20 | sed 's/^/      /'
grep -E "^(WSCREEN|EVENTS|PACKETS|qmp_desktop)" <<< "$F14_OI" | sed 's/^/      I: /'
F14_IC1=$(f14_card "$F14_I" 1 disk 1 1 "$F14_DISK (read-only)")
if [ "$F14_IOK" -eq 1 ] && [ -n "$F14_RBC" ] && [ "$F14_IC1" = "$(( (F14_RBC - F14_RFB) * 1024 )) $(( F14_RBC * 1024 ))" ] &&
   ! grep -qE '^files: card 2 ' "$F14_I"; then
    pass "[1.5] Files (note 15): a disk mounted read-only shows its card as '$F14_DISK (read-only)' with its figures (no CD card without a CD drive)"
else
    fail "[1.5] Files: the read-only card: [$F14_IC1] superblock $F14_RBC/$F14_RFB $(grep -E '^files: card ' "$F14_I" | head -n 3 | tr '\n' ' ')"
fi
if f14_order "$F14_I" "files: drag over $F14_DOC/Sub no (read-only)" "Files: drop refused (read-only): nothing changed" \
       "files: drag over Trash no (read-only)" "Files: drop refused (read-only): nothing changed" &&
   [ -n "$F14_RSUM0" ] && [ "$F14_RSUM0" = "$F14_RSUM1" ] && f14_health "$F14_I" "$F14_OI"; then
    pass "[1.5] Files (note 15): on a read-only disk a drop onto a folder or the Trash is refused (read-only), nothing changes (the disk image's SHA-256 unchanged); boot I's lines equal the model's, no panic"
else
    fail "[1.5] Files: drops on a read-only disk: $(grep -E '^(files: drag over|Files: drop refused)' "$F14_I" | head -n 4 | tr '\n' ' ') image ${F14_RSUM0:0:12} -> ${F14_RSUM1:0:12}"
fi
echo "      [1.5] This PC, pointers, box selection, drag and drop (part 6): $(( $(date +%s) - F14_T0 )) s (report only)"
