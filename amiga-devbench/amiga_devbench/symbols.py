"""Symbol table loader for Amiga cross-compiled binaries.

Parses symbol information from ELF/a.out binaries produced by m68k-amigaos-gcc
using nm and objdump via Docker. Supports STABS debug info for source line
mapping, struct type info, and local variables.
"""

from __future__ import annotations

import asyncio
import bisect
import logging
import os
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

logger = logging.getLogger(__name__)

PROJECT_ROOT = os.environ.get("AMIGA_PROJECT_ROOT", str(Path(__file__).parent.parent.parent))

DOCKER_IMAGE = "amigadev/crosstools:m68k-amigaos"


@dataclass
class Symbol:
    name: str
    address: int
    sym_type: str  # "T"=text, "D"=data, "B"=bss, "t"=local text, etc.
    size: int = 0
    file: str = ""
    line: int = 0


@dataclass
class SourceLine:
    """Maps an address to a source file and line number."""
    address: int
    file: str
    line: int


@dataclass
class StructField:
    """A field within a struct type."""
    name: str
    type_name: str
    offset_bits: int  # offset in bits from struct start
    size_bits: int


@dataclass
class StructType:
    """Parsed struct type from STABS."""
    name: str
    size_bytes: int
    fields: list[StructField] = field(default_factory=list)


@dataclass
class SymbolTable:
    """Holds symbols for a loaded binary."""
    binary_path: str = ""
    symbols: list[Symbol] = field(default_factory=list)
    by_address: dict[int, Symbol] = field(default_factory=dict)
    by_name: dict[str, Symbol] = field(default_factory=dict)
    _sorted_addrs: list[int] = field(default_factory=list)

    # Source line info from STABS
    source_lines: list[SourceLine] = field(default_factory=list)
    _line_addrs: list[int] = field(default_factory=list)  # sorted for bisect

    # Struct types from STABS
    struct_types: dict[str, StructType] = field(default_factory=dict)

    # Function-to-source mapping
    func_source: dict[str, tuple[str, int]] = field(default_factory=dict)  # name -> (file, line)

    def lookup_address(self, addr: int) -> str | None:
        """Find the nearest symbol at or before addr. Returns 'name+offset' or None."""
        if not self._sorted_addrs:
            return None
        idx = bisect.bisect_right(self._sorted_addrs, addr) - 1
        if idx < 0:
            return None
        sym_addr = self._sorted_addrs[idx]
        sym = self.by_address.get(sym_addr)
        if not sym:
            return None
        offset = addr - sym_addr
        if offset == 0:
            return sym.name
        if offset < 0x10000:
            return f"{sym.name}+0x{offset:x}"
        return None

    def lookup_name(self, name: str) -> int | None:
        """Find address of a named symbol."""
        sym = self.by_name.get(name)
        return sym.address if sym else None

    def lookup_source_line(self, addr: int) -> tuple[str, int] | None:
        """Find the source file:line for an address. Returns (file, line) or None."""
        if not self._line_addrs:
            return None
        idx = bisect.bisect_right(self._line_addrs, addr) - 1
        if idx < 0:
            return None
        sl = self.source_lines[idx]
        # Only match if within reasonable distance (same function)
        if addr - sl.address > 0x1000:
            return None
        return (sl.file, sl.line)

    def annotate_address_full(self, addr: int) -> dict[str, Any]:
        """Full annotation: symbol name, source file:line, offset."""
        result: dict[str, Any] = {"address": f"{addr:08x}"}

        sym_name = self.lookup_address(addr)
        if sym_name:
            result["symbol"] = sym_name

        src = self.lookup_source_line(addr)
        if src:
            result["file"] = src[0]
            result["line"] = src[1]

        return result


# Global symbol tables per project
_tables: dict[str, SymbolTable] = {}


_DEPLOY_CANDIDATES = [
    "/Applications/AmiKit.app/Contents/SharedSupport/prefix/drive_c/AmiKit/Dropbox/Dev",
    os.path.expanduser("~/AmiKit/Dropbox/Dev"),
]


def _find_binary(project: str) -> str | None:
    """Find the binary path for a project."""
    candidates = [
        f"{PROJECT_ROOT}/{project}/{project}",
        f"{PROJECT_ROOT}/examples/{project}/{project}",
    ]
    if project == "amiga-bridge":
        candidates.append(f"{PROJECT_ROOT}/amiga-bridge/amiga-bridge")

    # Also check deploy directories (binaries deployed to AmiKit shared folder)
    for deploy_dir in _DEPLOY_CANDIDATES:
        candidates.append(f"{deploy_dir}/{project}")

    for path in candidates:
        if os.path.exists(path):
            return path
    return None


async def _run_docker_tool(binary_path: str, tool: str, *args: str) -> tuple[int, str, str]:
    """Run a cross-tools binary via Docker. Returns (returncode, stdout, stderr)."""
    parent_dir = os.path.dirname(os.path.abspath(binary_path))
    bin_name = os.path.basename(binary_path)

    cmd = [
        "docker", "run", "--rm",
        "-v", f"{parent_dir}:/work",
        "-w", "/work",
        DOCKER_IMAGE,
        tool, *args, bin_name,
    ]

    proc = await asyncio.create_subprocess_exec(
        *cmd,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    stdout, stderr = await proc.communicate()
    return proc.returncode, stdout.decode(errors="replace"), stderr.decode(errors="replace")


def _parse_nm_output(output: str, table: SymbolTable) -> None:
    """Parse nm -n output into symbols."""
    for line in output.splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) >= 3:
            try:
                addr = int(parts[0], 16)
                sym_type = parts[1]
                name = parts[2]
                # Strip leading underscore (C convention for a.out)
                display_name = name[1:] if name.startswith("_") else name
                sym = Symbol(name=display_name, address=addr, sym_type=sym_type)
                table.symbols.append(sym)
                table.by_address[addr] = sym
                table.by_name[display_name] = sym
            except ValueError:
                continue

    table._sorted_addrs = sorted(table.by_address.keys())


def _parse_stabs_output(output: str, table: SymbolTable) -> None:
    """Parse objdump --stabs output for source lines and type info."""
    current_file = ""
    type_defs: dict[str, str] = {}  # type_num -> type_name (e.g. "579" -> "Vec2")

    # Pre-process: join continuation lines (ending with backslash)
    raw_lines = output.splitlines()
    lines: list[str] = []
    i = 0
    while i < len(raw_lines):
        line = raw_lines[i].strip()
        # Join continuation lines (objdump wraps long STABS entries with \)
        while line.endswith("\\") and i + 1 < len(raw_lines):
            i += 1
            line = line[:-1] + raw_lines[i].strip()
        lines.append(line)
        i += 1

    for line in lines:
        if not line:
            continue

        # STABS format from objdump --stabs:
        # Symnum n_type n_othr n_desc n_value  n_strx String
        # Example lines:
        # 5      SO     0      0      00000000 21     main.c
        # 12     SLINE  0      42     00000024 0
        # 15     FUN    0      31     00000100 45     init_entity:F(0,1)

        # Match the tabular format
        m = re.match(
            r'(\d+)\s+([\w]+)\s+(\d+)\s+(\d+)\s+([0-9a-fA-F]+)\s+(\d+)\s*(.*)',
            line
        )
        if not m:
            continue

        stab_type = m.group(2)
        n_desc = int(m.group(4))
        n_value = int(m.group(5), 16)
        string_val = m.group(7).strip()

        if stab_type == "SO":
            # Source file: SO stab sets the compilation unit
            if string_val and not string_val.endswith("/"):
                current_file = string_val

        elif stab_type == "SOL":
            # Include file switch
            if string_val:
                current_file = string_val

        elif stab_type == "SLINE":
            # Source line: n_desc = line number, n_value = address (absolute in a.out)
            if current_file and n_value > 0:
                table.source_lines.append(SourceLine(
                    address=n_value,
                    file=current_file,
                    line=n_desc,
                ))

        elif stab_type == "FUN":
            # Function definition: string = "name:F(type)" or "name:f(type)"
            # n_value = function address, n_desc = line number
            # Empty string_val with n_value > 0 = function size marker (end of previous func)
            if string_val and ":" in string_val:
                func_name = string_val.split(":")[0]
                # Strip leading underscore
                if func_name.startswith("_"):
                    func_name = func_name[1:]
                if current_file:
                    table.func_source[func_name] = (current_file, n_desc)

                # For static functions (":f"), add to symbol table if not already there
                # ":F" = global, ":f" = static/local
                is_static = ":f" in string_val
                if func_name not in table.by_name and n_value > 0:
                    sym = Symbol(
                        name=func_name, address=n_value,
                        sym_type="t" if is_static else "T",
                        file=current_file, line=n_desc,
                    )
                    table.symbols.append(sym)
                    table.by_address[n_value] = sym
                    table.by_name[func_name] = sym

                # Note: in a.out format, SLINE addresses are already absolute
                # No fix-up needed (unlike ELF STABS where they're relative)

        elif stab_type in ("LSYM", "GSYM"):
            # Type/struct definitions
            # Format: "name:T(type_num)=s<size><field_defs>" or "name:t<num>=..."
            if string_val and (":T" in string_val or ":t" in string_val):
                # Extract type number -> name mapping
                # e.g. "Vec2:t579=580=s8..." -> type_defs["579"] = "Vec2"
                name_part = string_val.split(":")[0]
                if name_part.startswith("_"):
                    name_part = name_part[1:]
                tm = re.match(r'[Tt](?:\(([^)]+)\)|(\d+))', string_val.split(":", 1)[1])
                if tm:
                    type_num = tm.group(1) or tm.group(2)
                    type_defs[type_num] = name_part
                # Check for struct definition (contains =s<digits>)
                if re.search(r'=s\d+', string_val):
                    _parse_struct_stab(string_val, table, type_defs)

    # Sort source lines by address for binary search
    table.source_lines.sort(key=lambda sl: sl.address)
    table._line_addrs = [sl.address for sl in table.source_lines]

    # Also update symbols with source info
    for sym in table.symbols:
        if sym.name in table.func_source:
            sym.file, sym.line = table.func_source[sym.name]


def _parse_struct_stab(stab_string: str, table: SymbolTable, type_map: dict[str, str] | None = None) -> None:
    """Parse a STABS type definition for struct types.

    Format examples (GCC STABS):
      "Vec2:t579=580=s8x:20,0,32;y:20,32,32;;"
      "Entity:t581=582=s58position:579,0,64;velocity:579,64,64;..."
      "Point:T(1,1)=s8x:(0,1),0,32;y:(0,1),32,32;;"  (alternate format)
    """
    if ":T" not in stab_string and ":t" not in stab_string:
        return

    # Extract struct name
    colon_pos = stab_string.index(":")
    struct_name = stab_string[:colon_pos]
    if struct_name.startswith("_"):
        struct_name = struct_name[1:]

    rest = stab_string[colon_pos + 1:]

    # Match both formats:
    #   T(num)=s<size>...     (parenthesized type numbers)
    #   t<num>=<num>=s<size>... (plain numeric type numbers, GCC a.out)
    m = re.match(r'[Tt](?:\([^)]+\)|\d+)=(?:\d+=)?s(\d+)(.*)', rest)
    if not m:
        return

    size_bytes = int(m.group(1))
    fields_str = m.group(2)

    st = StructType(name=struct_name, size_bytes=size_bytes)

    # Parse fields: "fieldname:type_ref,bit_offset,bit_size;"
    # type_ref can be: (0,1) or just a number like 20, or complex like 579
    field_pattern = re.compile(r'(\w+):([^,;]+),(\d+),(\d+);')
    for fm in field_pattern.finditer(fields_str):
        fname = fm.group(1)
        ftype = fm.group(2)
        foffset = int(fm.group(3))
        fsize = int(fm.group(4))

        # Simplify type representation
        type_name = _simplify_type(ftype, type_map)
        st.fields.append(StructField(
            name=fname, type_name=type_name,
            offset_bits=foffset, size_bits=fsize,
        ))

    if st.fields:
        table.struct_types[struct_name] = st
        logger.debug("Parsed struct %s: %d bytes, %d fields", struct_name, size_bytes, len(st.fields))


def _simplify_type(type_str: str, type_map: dict[str, str] | None = None) -> str:
    """Simplify STABS type reference to a readable name."""
    # Parenthesized format: (0,1), (0,2), etc.
    paren_map = {
        "(0,1)": "int", "(0,2)": "char", "(0,3)": "long",
        "(0,4)": "unsigned int", "(0,5)": "unsigned long",
        "(0,6)": "long long", "(0,7)": "unsigned long long",
        "(0,8)": "short", "(0,9)": "unsigned short",
        "(0,10)": "unsigned char", "(0,11)": "float",
        "(0,12)": "double", "(0,13)": "void",
    }
    # GCC a.out numeric format: type numbers from STABS header
    # Common types: 1=int, 2=char, 3=long, 5=ulong, 10=short, 13=uchar, 20=LONG, 21=ULONG, 27=UBYTE
    num_map = {
        "1": "int", "2": "char", "3": "long int", "4": "unsigned int",
        "5": "unsigned long", "10": "short", "11": "unsigned short",
        "12": "signed char", "13": "unsigned char", "14": "float",
        "15": "double", "17": "void",
        "20": "LONG", "21": "ULONG", "23": "WORD", "24": "UWORD",
        "26": "BYTE", "27": "UBYTE",
    }
    result = paren_map.get(type_str) or num_map.get(type_str)
    if result:
        return result
    # Check if it's a known struct type number in the type_map
    if type_map and type_str in type_map:
        return type_map[type_str]
    return f"type({type_str})"


async def _run_native_tool(binary_path: str, tool: str, *args: str) -> tuple[int, str, str]:
    """Run a cross-tools binary natively (no Docker). Returns (returncode, stdout, stderr)."""
    cmd = [tool, *args, binary_path]
    try:
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        stdout, stderr = await proc.communicate()
        return proc.returncode, stdout.decode(errors="replace"), stderr.decode(errors="replace")
    except FileNotFoundError:
        return -1, "", f"{tool} not found"


async def _run_tool(binary_path: str, tool: str, *args: str) -> tuple[int, str, str]:
    """Run a cross-tools binary, trying native first then Docker."""
    # Try native first (faster, no Docker dependency)
    rc, out, err = await _run_native_tool(binary_path, tool, *args)
    if rc == 0:
        return rc, out, err

    # Fall back to Docker
    try:
        return await _run_docker_tool(binary_path, tool, *args)
    except Exception as e:
        return -1, "", f"Both native and Docker failed: {err}; Docker: {e}"


async def load_symbols(project: str) -> SymbolTable:
    """Load symbols from a compiled binary using nm + objdump --stabs.

    Tries native cross-tools first, falls back to Docker.
    """
    binary_path = _find_binary(project)
    if not binary_path:
        logger.warning("Binary not found for project %s", project)
        return SymbolTable(binary_path=f"(not found: {project})")

    table = SymbolTable(binary_path=binary_path)

    try:
        # Run nm and objdump --stabs in parallel
        nm_task = _run_tool(binary_path, "m68k-amigaos-nm", "-n")
        stabs_task = _run_tool(binary_path, "m68k-amigaos-objdump", "--stabs")

        (nm_rc, nm_out, nm_err), (stabs_rc, stabs_out, stabs_err) = await asyncio.gather(
            nm_task, stabs_task
        )

        # Parse nm output (always available)
        if nm_rc == 0:
            _parse_nm_output(nm_out, table)
        else:
            logger.warning("nm failed for %s: %s", project, nm_err)

        # Parse STABS debug info (only if compiled with -g)
        if stabs_rc == 0 and stabs_out.strip():
            _parse_stabs_output(stabs_out, table)
            # Rebuild sorted addresses (STABS may have added static function symbols)
            table._sorted_addrs = sorted(table.by_address.keys())
            logger.info("Loaded STABS: %d source lines, %d structs, %d func mappings",
                        len(table.source_lines), len(table.struct_types), len(table.func_source))
        else:
            logger.info("No STABS debug info for %s (compile with -g to enable)", project)

        logger.info("Loaded %d symbols from %s", len(table.symbols), binary_path)

    except Exception as e:
        logger.error("Failed to load symbols: %s", e)

    _tables[project] = table
    return table


def get_symbols(project: str) -> SymbolTable | None:
    """Get previously loaded symbol table."""
    return _tables.get(project)


def get_all_tables() -> dict[str, SymbolTable]:
    """Get all loaded symbol tables."""
    return _tables


def annotate_address(addr: int, project: str | None = None) -> str:
    """Try to annotate an address with a symbol name from any loaded table."""
    if project:
        table = _tables.get(project)
        if table:
            result = table.lookup_address(addr)
            if result:
                return result

    for table in _tables.values():
        result = table.lookup_address(addr)
        if result:
            return result

    return ""


def annotate_address_full(addr: int, project: str | None = None) -> dict[str, Any]:
    """Full annotation with symbol + source line from any loaded table."""
    if project:
        table = _tables.get(project)
        if table:
            ann = table.annotate_address_full(addr)
            if "symbol" in ann:
                return ann

    for table in _tables.values():
        ann = table.annotate_address_full(addr)
        if "symbol" in ann:
            return ann

    return {"address": f"{addr:08x}"}


def source_line_for_address(addr: int, project: str | None = None) -> str:
    """Get 'file:line' string for an address, or empty string."""
    if project:
        table = _tables.get(project)
        if table:
            src = table.lookup_source_line(addr)
            if src:
                return f"{src[0]}:{src[1]}"

    for table in _tables.values():
        src = table.lookup_source_line(addr)
        if src:
            return f"{src[0]}:{src[1]}"

    return ""


def list_functions(project: str) -> list[dict[str, Any]]:
    """List all function symbols for a project."""
    table = _tables.get(project)
    if not table:
        return []
    result = []
    for s in table.symbols:
        if s.sym_type in ("T", "t"):
            entry: dict[str, Any] = {
                "name": s.name, "address": f"{s.address:08x}", "type": s.sym_type,
            }
            if s.file:
                entry["file"] = s.file
            if s.line:
                entry["line"] = s.line
            result.append(entry)
    return result


def list_structs(project: str) -> list[dict[str, Any]]:
    """List all parsed struct types for a project."""
    table = _tables.get(project)
    if not table:
        return []
    result = []
    for st in table.struct_types.values():
        fields = []
        for f in st.fields:
            fields.append({
                "name": f.name,
                "type": f.type_name,
                "offset": f.offset_bits // 8,
                "size": f.size_bits // 8,
            })
        result.append({
            "name": st.name,
            "size": st.size_bytes,
            "fields": fields,
        })
    return result


def format_memory_with_struct(hex_data: str, struct_name: str, project: str | None = None) -> str:
    """Format hex memory dump annotated with struct field names."""
    st = None
    if project:
        table = _tables.get(project)
        if table:
            st = table.struct_types.get(struct_name)
    if not st:
        for table in _tables.values():
            st = table.struct_types.get(struct_name)
            if st:
                break
    if not st:
        return f"Struct '{struct_name}' not found in loaded symbols"

    data = bytes.fromhex(hex_data)
    lines = [f"struct {st.name} ({st.size_bytes} bytes):"]

    for f in st.fields:
        byte_off = f.offset_bits // 8
        byte_size = f.size_bits // 8
        if byte_off + byte_size > len(data):
            lines.append(f"  +{byte_off:3d}  {f.name}: (beyond data)")
            continue

        field_bytes = data[byte_off:byte_off + byte_size]
        hex_str = field_bytes.hex()

        # Try to interpret as integer value
        if byte_size <= 4:
            val = int.from_bytes(field_bytes, "big", signed=True)
            lines.append(f"  +{byte_off:3d}  {f.name}: {val} (0x{hex_str})")
        else:
            lines.append(f"  +{byte_off:3d}  {f.name}: 0x{hex_str}")

    return "\n".join(lines)
