# FSH: C Edition

Rules:

- Configure once: `cmake -G Ninja -S . -B build`
- Every time you edit source code: `ninja -C build`
- Run it: `./build/src/fsh`
- Exit the shell: `exit` or `exit <code>`
- History and config are shared with the TypeScript fsh via `~/.fsh_history`
  and `~/.fshrc` (same `$HOME`). Use `HOME=/tmp/fsh-test ./build/src/fsh` to
  run with an isolated, throwaway home directory instead.

## Phase status

**Phase 1** (core shell): lexer/parser, pipelines, redirects, job control,
PTY passthrough, builtins, `.fshrc`, raw-mode line editor, history. Done.

**Phase 2** (prompt/editor polish): syntax highlighting while typing,
git-aware prompt. Done.

**Phase 3a** (interactive `ls`/`dir` file browser): grid navigation, fuzzy
search, sort picker, bookmarks, clipboard (copy/cut/paste), mkdir/touch/
rename dialogs, delete-to-trash, split/overlay preview panel with syntax
coloring, per-file git status badges, open-in-$EDITOR. Done.

**Phase 3b** (tab completion): command/subcommand/file candidates, common-
prefix auto-complete, grid completion picker, Tab-on-empty-line and
Tab-inside-picker both open the time-bucketed command history manager
(`~/.fsh_history` grouped into Last hour/Today/Yesterday/This week/Older,
with select/delete/delete-all), Ctrl+B opens the bookmark picker directly
from the line editor. Done.

**Phase 3c** (search + activity log + critical bug fixes): Ctrl+R opens
fuzzy multi-category search (command history, files, directories,
executables, builtins, aliases) with dir-browse/delete and file-preview
with editor-picker sub-screens; Ctrl+H opens the general activity log
viewer (3 collapsible categories: commands, file mutations, trash ops,
with per-entry detail view and select/delete/delete-all). Done.

Two critical bugs were found and fixed during this phase:
- **Raw-mode corruption on nested pickers**: any picker invoked from
  inside another already-raw-mode session (e.g. opening the history
  manager via Tab while the line editor was reading input) disabled raw
  mode globally on exit instead of just for itself, leaving the terminal
  in cooked/canonical mode and making Enter/Backspace behave erratically
  until Ctrl+C forced a SIGINT. Fixed by making
  `platform_raw_mode_enable/disable` reference-counted instead of a
  simple boolean guard.
- **Bare Escape hung waiting for a second byte**: every picker screen
  read one extra byte after ESC (to detect arrow-key sequences like
  `ESC [ A`) using a blocking `read()`. Pressing Escape alone with no
  follow-up bytes caused that read to block indefinitely, so it took two
  Escape presses to actually close a picker. Fixed with a shared
  `tui_read_byte_after_esc()` helper that uses a short `select()` timeout
  (~35ms) to distinguish a bare Escape from the start of an arrow-key
  sequence, applied consistently across every picker screen.

**Deferred to a later step** (same shape, not yet built): the `fileOpsLog`
and `trashLs` standalone TUI viewers (list/browse/undo screens for the
file-ops log and trash — their backend stores already exist and are fully
populated), the `helps` documentation screen, and the full system-info
`neofetch` splash.

### Known simplifications vs the TypeScript version

- Image/video preview: intentionally unimplemented per project decision.
  Opening an image/video shows a "not supported yet" status message instead
  of shelling out to `feh`/`ffprobe`. Image *dimensions* are still parsed
  (PNG/JPEG/GIF/BMP headers) and shown in the preview panel.
- "Move" is done via cut (`x`) + navigate + paste (`v`) rather than the
  original's dedicated in-browser directory-picker overlay for move targets.
- Opening a file always uses `$EDITOR` (or the first of `nano`/`vim`/`vi`
  found on `$PATH`) directly — no separate installed-editor picker menu.
