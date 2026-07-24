# python-os4

CPython 3.12.7 port for AmigaOS 4.1 PowerPC (sam460ex / QEMU).

**Status:** Phase 1 bootstrap in progress. Configure works; ~73 core `.o` files
compile cleanly; disabled-modules list working; POSIX shim layer stubbed. Not yet
producing a linked interpreter binary — see "What blocks Phase 1" below.

## Layout

```
python-os4/
├── Dockerfile           # walkero PPC toolchain + Python 3.12 for build
├── build.sh             # main iteration script (configure / make / clean / shell)
├── setup.local          # Modules/Setup.local — disables ~30 unshimmed stdlib mods
├── amiga_shim.c/h       # POSIX gap-fillers (unsetenv, initgroups, setrlimit...)
├── Python-3.12.7/       # CPython source (unpacked from tarball, patched)
└── build-ppc-amigaos/   # out-of-tree build dir (git-ignored)
```

Only two upstream files patched: `Python-3.12.7/configure` gets ~10 lines of new
cases so `powerpc-unknown-amigaos` is recognised as a cross-compile target. All
other adaptations live in `amiga_shim.h/c` (force-included via `-include` in
CFLAGS) so we don't fork CPython.

## Usage

```bash
docker build -t amiga-python-build:local .   # once (or when Dockerfile changes)
./build.sh configure                          # runs ./configure
./build.sh make                               # configure + make
./build.sh clean                              # nuke build dir
./build.sh shell                              # interactive container
```

## What works (in this snapshot)

- `configure` accepts `--host=powerpc-unknown-amigaos` (2-block patch to
  `configure` — see the `*-*-amigaos*` cases)
- Cross-compile via walkero's `ppc-amigaos-gcc` inside Docker
- `PYTHON_FOR_BUILD` = deadsnakes Python 3.12 (needed by `configure` for
  freeze/regen consistency)
- Hard-float ABI selected (`-mhard-float`) matching newlib's libc
- `SSIZE_MAX` defined via CFLAGS
- ~73 core interpreter `.o` files build cleanly (Object system, ceval,
  parser, compile, import, marshal, unicode, symtab, tracemalloc, GIL...)
- `Modules/Setup.local` overrides after configure to skip ~30 modules with
  hard POSIX deps we haven't shimmed
- POSIX shim layer wired via `-include /work/amiga_shim.h`:
  - `unsetenv()`, `initgroups()`, `setrlimit()`, `getrlimit()` — real stubs
  - `fileno()`, `fdopen()`, `popen()`, `pclose()` — prototypes only (newlib
    has the code but doesn't expose the decls in default feature-test mode)
  - `O_NOFOLLOW`, `O_CLOEXEC`, `O_DIRECTORY` — defined as 0

## What blocks Phase 1

Each build iteration reveals 1-2 more POSIX-shim needs. Current known gaps
(from latest failed build):

- `fork` / `execv` / `execve` — deep problem, needs SystemTagList-based
  replacement (`_posixsubprocess` module disabled for now to advance)
- `PyOS_BeforeFork` / `PyOS_AfterFork_Child/Parent` — related to fork
- `INET_ADDRSTRLEN` — bsdsocket.library exposes sockets differently
  (`_socket` module disabled)
- Various `RLIMIT_*`, `LOG_*`, `SIGQUIT`, etc. — Amiga doesn't have most
  POSIX signal/rlimit machinery

Realistic remaining Phase-1 work: **1-2 weeks focused** to shim/skip enough
that `python3 -c 'print(1)'` actually links + runs. Then Phase 2 (stdlib
freeze + install) is another 3-5 weeks per the original assessment.

## Development inner loop

1. `./build.sh make`
2. Read `/tmp/py-build.log` or scroll output for first `error:` /
   `undeclared` / `implicit declaration of function`
3. If a POSIX function is missing → add stub to `amiga_shim.c/h`
4. If a POSIX constant is missing → `#define` it in `amiga_shim.h`
5. If a whole module can't be shimmed → add its name to `setup.local`
6. If a core CPython file has an Amiga-hostile call — future work: create
   `Python-3.12.7/Amiga/` platform dir with proper `pyconfig.h` overrides
   (patterned after `PC/` for Windows)
7. `./build.sh make` again — usually fixes 1-2 issues at a time

## Phased roadmap (recap of the up-front assessment)

- **Phase 1** — Bootstrap. Get `python3 -c "print(1)"` running. ~3-5 weeks.
- **Phase 2** — Stdlib freeze + install. ~3-5 weeks.
- **Phase 3** — Sockets + SSL via bsdsocket.library + AmiSSL. ~2-3 weeks.
- **Phase 4** — Threading + subprocess (pthread shim, SystemTagList). ~4-6 weeks.
- **Phase 5** — pip install. ~2-3 weeks.
- **Phase 6** — `_amiga` bindings (Intuition, DOS, bridge client). ongoing.

Half-time single-engineer estimate: 4-6 months to "usable for scripts",
8-12 months to "pip-installs-things-and-they-mostly-work".
