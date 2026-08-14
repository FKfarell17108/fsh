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
coloring, per-file git status badges, open-in-$EDITOR. Done - see caveats
below.

**Deferred to a later step** (same shape, not yet built): the three
standalone TUI viewers (file-ops activity log, trash browser, general
history browser) and the tab-completion picker. Their backend data stores
(`~/.fsh_fileops.json`, `~/.fsh_trash/.meta.json`,
`~/.fsh_general_history.json`) are already fully implemented and populated
by every action you take in the browser - only the dedicated browsing
screens for them are outstanding.

### Known simplifications vs the TypeScript version

- Image/video preview: intentionally unimplemented per project decision.
  Opening an image/video shows a "not supported yet" status message instead
  of shelling out to `feh`/`ffprobe`. Image *dimensions* are still parsed
  (PNG/JPEG/GIF/BMP headers) and shown in the preview panel.
- "Move" is done via cut (`x`) + navigate + paste (`v`) rather than the
  original's dedicated in-browser directory-picker overlay for move targets.
- Opening a file always uses `$EDITOR` (or the first of `nano`/`vim`/`vi`
  found on `$PATH`) directly - no separate installed-editor picker menu.
