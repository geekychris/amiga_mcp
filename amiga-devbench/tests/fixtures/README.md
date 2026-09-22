# Protocol fixtures

Golden lines as the bridge daemon emits them, used by `tests/test_characterisation.py` and `tests/test_protocol_level2.py`.
Reference commit: upstream `94a8888`. Nothing here was captured from a running
Amiga yet; lines marked *synthetic* should be replaced by a real capture when one
is available (values only - the shapes are taken from the source).

## `bridge_v1.20_68k.txt` / `bridge_v1.20_ppc.txt`

| Line | Provenance |
|------|------------|
| `READY\|1.0` | verbatim, `amiga-bridge/src/main.c:562` |
| `CAPABILITIES\|AmigaBridge v1.20\|1\|8192\|...` | built from the literal in `amiga-bridge/src/system_inspector.c:1242-1277` (`sys_handle_capabilities`); `%ld` -> `BRIDGE_MAX_LINE` 8192. 68k keeps `LIBFUNCS` (105 names), PPC drops it (104) per the `#ifdef __PPC__` at :1249 |
| `SYSINFO\|...` | format verbatim from `system_inspector.c:1474`; `cpuType` spellings from :1480-1510 (`68030` / `PowerPC`); memory, exec version and VBlank values **synthetic** |
| `VERSION\|AmigaBridge\|1\|20\|...` | shape from `amiga-bridge/src/protocol_handler.c:2389`; major/minor from `include/bridge_internal.h:13-14`; build date **synthetic** (it is `__DATE__ " " __TIME__`, `src/version.c:9`) |
| `HB\|tick\|chipFree\|fastFree` | shape from `protocol_handler.c:477`; values **synthetic** |

## `bridge_v1.21_68k.txt` / `bridge_v1.21_ppc.txt`

Protocol level 2 (daemon v1.21). **Generated, not hand-typed** - regenerate with

```bash
python3 amiga-devbench/tests/fixtures/gen_bridge_fixtures.py   # needs gcc/clang
```

| Line | Provenance |
|------|------------|
| `CAPABILITIES\|AmigaBridge v1.21\|2\|8192\|commands\|platform\|features\|profiles` | the four per-build fields come from `amiga-bridge/host/test_bridge --dump-caps`, which links the daemon's own `amiga-bridge/src/caps_util.c`; version and `maxLine` are read from `amiga-bridge/include/bridge_internal.h`; the line is assembled exactly as `sys_handle_capabilities()` does |
| `SYSINFO\|...\|transport` | ninth field (`serial` on the 68k fixture, `tcp` on PPC) per `system_inspector.c` `sys_handle_sysinfo()`; other values **synthetic** as for v1.20 |
| `READY`, `VERSION`, `HB` | as for v1.20 (minor = 21, build date **synthetic**) |

## `bridge_v1.16.txt`

Same structure, for the daemon binary the repository tracks (`amiga-bridge/amiga-bridge`).

| Line | Provenance |
|------|------------|
| `CAPABILITIES\|AmigaBridge v1.16\|...` | verbatim from `strings amiga-bridge/amiga-bridge` with `%ld` -> 8192; 105 names, identical to the v1.20 68k list |
| `VERSION\|AmigaBridge\|%ld\|%ld\|%s` | shape verbatim from the binary's strings; 1/16 and build date **synthetic** |
| `READY`, `SYSINFO`, `HB` | as for 68k above (the binary is a 68k build) |

## `bridge_v1.22_68k.txt` / `bridge_v1.22_ppc.txt`

Daemon v1.22: v1.21 plus `INPUTPOS` (absolute pointer position) in the `input`
profile - 121 verbs on 68k, 86 on PPC. Generated exactly like the v1.21 pair.

## `mcp_tools_v94a8888.txt`

The 130 tool names registered on the `FastMCP("amiga-dev")` instance in
`amiga_devbench/mcp_tools.py`, sorted. Generated, not hand-typed:

```python
import asyncio
from amiga_devbench.mcp_tools import mcp
print("\n".join(sorted(t.name for t in asyncio.run(mcp.list_tools()))))
```

## `amiforge_tools.txt`

The `amiforge_*` tool names (`amiga_devbench/amiforge_tools.py`), sorted, generated the
same way (filtered on the `amiforge_` prefix).
