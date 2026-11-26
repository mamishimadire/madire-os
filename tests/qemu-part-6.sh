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
