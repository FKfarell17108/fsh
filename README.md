# FSH: C Edition

Rules:

- Configure once: `cmake -G Ninja -S . -B build`
- Every time you edit source code: `ninja -C build`
- Run it: `./build/src/fsh`
- Exit the shell: `exit` or `exit <code>`
- History and config are shared with the TypeScript fsh via `~/.fsh_history`
  and `~/.fshrc` (same `$HOME`). Use `HOME=/tmp/fsh-test ./build/src/fsh` to
  run with an isolated, throwaway home directory instead.
