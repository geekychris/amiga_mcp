# Protocol fixtures

Golden lines as the bridge daemon emits them, used by `tests/test_characterisation.py`.
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

## `bridge_v1.16.txt`

Same structure, for the daemon binary the repository tracks (`amiga-bridge/amiga-bridge`).

| Line | Provenance |
|------|------------|
| `CAPABILITIES\|AmigaBridge v1.16\|...` | verbatim from `strings amiga-bridge/amiga-bridge` with `%ld` -> 8192; 105 names, identical to the v1.20 68k list |
| `VERSION\|AmigaBridge\|%ld\|%ld\|%s` | shape verbatim from the binary's strings; 1/16 and build date **synthetic** |
| `READY`, `SYSINFO`, `HB` | as for 68k above (the binary is a 68k build) |

## `mcp_tools_v94a8888.txt`

The 129 tool names registered on the `FastMCP("amiga-dev")` instance in
`amiga_devbench/mcp_tools.py`, sorted. Generated, not hand-typed:

```python
import asyncio
from amiga_devbench.mcp_tools import mcp
print("\n".join(sorted(t.name for t in asyncio.run(mcp.list_tools()))))
```
