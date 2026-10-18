# SPDX-License-Identifier: MIT
"""Bounded, read-only m68k DWARF analysis. Guest reads go through DevBench."""
import base64
import io
import json
import re
import struct
import sys


CALLEE_SAVED = (2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14)


def emit(value):
    print(json.dumps(value, allow_nan=False), flush=True)


def checked(value, maximum=0xffffffff):
    if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= maximum:
        raise ValueError('Invalid address, size or DWARF constant')
    return value


class Analysis:
    def __init__(self, request):
        from elftools.elf.elffile import ELFFile
        from elftools.dwarf.dwarf_expr import DWARFExprParser
        from elftools.dwarf.locationlists import LocationParser
        self.req = request
        raw = base64.b64decode(request['elf'], validate=True)
        if len(raw) > 16 * 1024 * 1024:
            raise ValueError('ELF exceeds 16 MiB')
        self.elf = ELFFile(io.BytesIO(raw))
        if self.elf.elfclass != 32 or self.elf.little_endian or self.elf['e_machine'] != 'EM_68K' or self.elf['e_type'] != 'ET_EXEC':
            raise ValueError('DWARF tools require a linked ELF32 big-endian m68k executable')
        if any(s['sh_flags'] & 0x800 or s.name.startswith('.zdebug') for s in self.elf.iter_sections()):
            raise ValueError('Compressed debug sections are unsupported')
        if any(s.name.startswith('.debug') and s['sh_size'] > 4 * 1024 * 1024 for s in self.elf.iter_sections()):
            raise ValueError('Debug section exceeds 4 MiB')
        # ET_EXEC debug addresses are already linked, including files built
        # with --emit-relocs. Applying their retained relocations again is
        # incorrect and pyelftools does not implement m68k relocations.
        self.dwarf = self.elf.get_dwarf_info(relocate_dwarf_sections=False)
        self.expr_parser = DWARFExprParser(self.dwarf.structs)
        self.locations = LocationParser(self.dwarf.location_lists())
        self.maps = request['mappings']
        self.regs = {i: request['registers'][('D' if i < 8 else 'A') + str(i % 8)] for i in range(16)}
        self.pc = request['registers']['PC']
        self.units = list(self.dwarf.iter_CUs())
        if len(self.units) > 256 or any(cu['address_size'] != 4 or cu['version'] not in (2, 3, 4, 5) for cu in self.units):
            raise ValueError('Unsupported compilation units')
        self.dies = []
        for cu in self.units:
            for die in cu.iter_DIEs():
                if len(self.dies) >= 100000:
                    raise ValueError('DWARF DIE budget exceeded')
                self.dies.append(die)
        self.cfi = []
        for enabled, method in ((self.dwarf.has_CFI(), self.dwarf.CFI_entries), (self.dwarf.has_EH_CFI(), self.dwarf.EH_CFI_entries)):
            if enabled:
                self.cfi += [e for e in method() if 'initial_location' in e.header]
        if len(self.cfi) > 65536:
            raise ValueError('CFI budget exceeded')
        self.read_bytes = 0
        self.value_nodes = 0

    def translate(self, address, runtime=False):
        matches = []
        for m in self.maps:
            src, dst = (m['address'], m['linked']) if runtime else (m['linked'], m['address'])
            if src <= address < src + m['size']:
                matches.append(dst + address - src)
        if len(matches) != 1:
            raise ValueError('Address has no unique explicit section mapping')
        return checked(matches[0])

    def read(self, address, size):
        checked(address); checked(size, 4096)
        self.read_bytes += size
        if address + size > 0x100000000 or self.read_bytes > 65536:
            raise ValueError('Guest read budget exceeded')
        if not size:
            return b''
        emit({'read': {'address': address, 'length': size}})
        reply = json.loads(sys.stdin.readline(16384))
        if 'error' in reply:
            raise ValueError(reply['error'])
        data = bytes.fromhex(reply['hex'])
        if len(data) != size:
            raise ValueError('Short guest memory reply')
        return data

    def attribute(self, die, name, depth=0):
        if depth > 16:
            raise ValueError('Cyclic abstract origin or specification')
        if name in die.attributes:
            return die.attributes[name]
        for key in ('DW_AT_abstract_origin', 'DW_AT_specification'):
            if key in die.attributes:
                attr = self.attribute(die.get_DIE_from_attribute(key), name, depth + 1)
                if attr is not None:
                    return attr
        return None

    def name(self, die):
        attr = self.attribute(die, 'DW_AT_name')
        return attr.value.decode('utf-8', 'replace') if attr else None

    def contains(self, die, pc):
        from elftools.dwarf.descriptions import describe_form_class
        lo = self.attribute(die, 'DW_AT_low_pc')
        hi = self.attribute(die, 'DW_AT_high_pc')
        if lo and hi:
            end = hi.value if describe_form_class(hi.form) == 'address' else lo.value + hi.value
            return lo.value <= pc < end
        ranges = self.attribute(die, 'DW_AT_ranges')
        if ranges:
            base = die.cu.get_top_DIE().attributes.get('DW_AT_low_pc')
            base = base.value if base else 0
            entries = self.dwarf.range_lists().get_range_list_at_offset(ranges.value, cu=die.cu)
            if len(entries) > 65536:
                raise ValueError('Range-list budget exceeded')
            for entry in entries:
                if hasattr(entry, 'base_address'):
                    base = entry.base_address
                else:
                    start = entry.begin_offset + (0 if entry.is_absolute else base)
                    end = entry.end_offset + (0 if entry.is_absolute else base)
                    if start <= pc < end:
                        return True
            return False
        return None

    def source(self, runtime_pc):
        pc = self.translate(runtime_pc, True)
        functions = [d for d in self.dies if d.tag in ('DW_TAG_subprogram', 'DW_TAG_inlined_subroutine') and self.contains(d, pc)]
        result = {'address': runtime_pc, 'linked_address': pc, 'functions': [self.name(d) for d in functions]}
        found = []
        for cu in self.units:
            program = self.dwarf.line_program_for_CU(cu)
            if not program:
                continue
            previous = None
            entries = program.get_entries()
            if len(entries) > 200000:
                raise ValueError('Line-table budget exceeded')
            for entry in entries:
                state = entry.state
                if not state:
                    continue
                if previous and previous.address <= pc < state.address:
                    files = program.header['file_entry']
                    version5 = program.header['version'] >= 5
                    file_index = previous.file if version5 else previous.file - 1
                    if not 0 <= file_index < len(files):
                        raise ValueError('Invalid source file index')
                    item = files[file_index]
                    filename = item.name.decode('utf-8', 'replace')
                    directory = ''
                    dirs = program.header['include_directory']
                    if version5 or item.dir_index:
                        dir_index = (item.dir_index or 0) if version5 else item.dir_index - 1
                        if not 0 <= dir_index < len(dirs):
                            raise ValueError('Invalid source directory index')
                        directory = dirs[dir_index].decode('utf-8', 'replace')
                    else:
                        attr = cu.get_top_DIE().attributes.get('DW_AT_comp_dir')
                        directory = attr.value.decode('utf-8', 'replace') if attr else ''
                    found.append({'file': filename, 'directory': directory, 'line': previous.line, 'column': previous.column})
                previous = None if state.end_sequence else state
        result['locations'] = found
        return result

    def frame_rule(self, pc, regs):
        linked = self.translate(pc, True)
        entries = [e for e in self.cfi if e['initial_location'] <= linked < e['initial_location'] + e['address_range']]
        if not entries:
            raise ValueError('No CFI for this instruction; no stack-scan fallback')
        entry = entries[0]
        rows = entry.get_decoded().table
        if len(rows) > 65536:
            raise ValueError('CFI row budget exceeded')
        eligible = [r for r in rows if r['pc'] <= linked]
        if not eligible:
            raise ValueError('No CFI row for this instruction')
        row = eligible[-1]
        rule = row['cfa']
        if rule.expr is not None:
            cfa, kind = self.evaluate(rule.expr, regs, None, None)
            if kind != 'address':
                raise ValueError('Invalid CFA expression')
        else:
            if rule.reg not in regs:
                raise ValueError('CFA uses an unavailable register')
            cfa = regs[rule.reg] + rule.offset
        checked(cfa)
        if cfa % 2:
            raise ValueError('Unaligned CFA')
        return row, cfa, entry.cie['return_address_register']

    def evaluate(self, expression, regs, cfa, frame_base):
        operations = self.expr_parser.parse_expr(expression)
        if len(operations) > 128:
            raise ValueError('DWARF expression budget exceeded')
        stack, kind = [], 'address'
        for operation in operations:
            op, args = operation.op_name, operation.args
            if op == 'DW_OP_addr': stack.append(self.translate(args[0]))
            elif op.startswith('DW_OP_breg'):
                reg, offset = (args[0], args[1]) if op == 'DW_OP_bregx' else (int(op[10:]), args[0])
                if reg not in regs: raise ValueError('Unavailable DWARF register')
                stack.append(regs[reg] + offset)
            elif op.startswith('DW_OP_reg'):
                reg = args[0] if op == 'DW_OP_regx' else int(op[9:])
                if reg not in regs: raise ValueError('Unavailable DWARF register')
                if len(operations) != 1: raise ValueError('Compound register locations are unsupported')
                stack.append(regs[reg]); kind = 'register'
            elif op == 'DW_OP_fbreg':
                if frame_base is None: raise ValueError('Unavailable frame base')
                stack.append(frame_base + args[0])
            elif op == 'DW_OP_call_frame_cfa':
                if cfa is None: raise ValueError('Unavailable CFA')
                stack.append(cfa)
            elif op == 'DW_OP_plus_uconst': stack[-1] += args[0]
            elif op.startswith('DW_OP_const'): stack.append(args[0])
            elif op.startswith('DW_OP_lit'): stack.append(int(op[9:]))
            elif op == 'DW_OP_plus': stack.append(stack.pop() + stack.pop())
            elif op == 'DW_OP_minus':
                right = stack.pop(); stack[-1] -= right
            elif op == 'DW_OP_deref': stack.append(int.from_bytes(self.read(checked(stack.pop()), 4), 'big'))
            elif op == 'DW_OP_stack_value': kind = 'value'
            elif op == 'DW_OP_nop': pass
            else: raise ValueError('Unsupported DWARF operation: ' + op)
            if len(stack) > 32: raise ValueError('DWARF stack budget exceeded')
        if len(stack) != 1:
            raise ValueError('Invalid DWARF expression result')
        return stack[0], kind

    def expression(self, die, name, pc):
        attr = self.attribute(die, name)
        if not attr:
            raise ValueError('Variable is optimized out or has no ' + name)
        parsed = self.locations.parse_from_attribute(attr, die.cu['version'], die=die)
        if hasattr(parsed, 'loc_expr'):
            return parsed.loc_expr
        base_attr = die.cu.get_top_DIE().attributes.get('DW_AT_low_pc')
        base = base_attr.value if base_attr else 0
        matches = []
        if len(parsed) > 65536: raise ValueError('Location-list budget exceeded')
        for item in parsed:
            if hasattr(item, 'base_address'): base = item.base_address
            elif hasattr(item, 'begin_offset'):
                start = item.begin_offset + (0 if item.is_absolute else base)
                end = item.end_offset + (0 if item.is_absolute else base)
                if start <= pc < end: matches.append(item.loc_expr)
        if len(matches) != 1: raise ValueError('No unique variable location at this PC')
        return matches[0]

    def type_of(self, die, depth=0):
        if depth > 16: raise ValueError('Cyclic type reference')
        if 'DW_AT_type' in die.attributes:
            return die.get_DIE_from_attribute('DW_AT_type')
        for key in ('DW_AT_abstract_origin', 'DW_AT_specification'):
            if key in die.attributes: return self.type_of(die.get_DIE_from_attribute(key), depth + 1)
        raise ValueError('Missing type information')

    def canonical(self, die):
        seen = set()
        while die.tag in ('DW_TAG_typedef', 'DW_TAG_const_type', 'DW_TAG_volatile_type', 'DW_TAG_restrict_type', 'DW_TAG_atomic_type'):
            if die.offset in seen or len(seen) > 16: raise ValueError('Cyclic type')
            seen.add(die.offset); die = self.type_of(die)
        return die

    def array(self, die):
        element = self.type_of(die)
        ranges = [d for d in die.iter_children() if d.tag == 'DW_TAG_subrange_type']
        if len(ranges) != 1: raise ValueError('Only one-dimensional array types are supported')
        sub = ranges[0]
        def constant(node, name, default=None):
            from elftools.dwarf.descriptions import describe_form_class
            attr = node.attributes.get(name)
            if not attr: return default
            if describe_form_class(attr.form) != 'constant': raise ValueError('Dynamic array bound or stride is unsupported')
            return attr.value
        low = constant(sub, 'DW_AT_lower_bound', 0)
        count = constant(sub, 'DW_AT_count')
        if count is None:
            high = constant(sub, 'DW_AT_upper_bound')
            if high is None: raise ValueError('Unknown array bound')
            count = high - low + 1
        checked(count, 1048576)
        stride = constant(die, 'DW_AT_byte_stride')
        if 'DW_AT_bit_stride' in die.attributes: raise ValueError('Bit-strided arrays are unsupported')
        if stride is None: stride = self.size(element)
        if stride < self.size(element): raise ValueError('Invalid array stride')
        return element, low, count, stride

    def size(self, die, depth=0):
        if depth > 16: raise ValueError('Type nesting budget exceeded')
        die = self.canonical(die)
        attr = die.attributes.get('DW_AT_byte_size')
        if attr: return checked(attr.value, 16 * 1024 * 1024)
        if die.tag == 'DW_TAG_pointer_type': return 4
        if die.tag == 'DW_TAG_array_type':
            element, low, count, stride = self.array(die)
            return checked(count * stride, 16 * 1024 * 1024)
        raise ValueError('Unknown type size')

    def member(self, die, name):
        members = [d for d in die.iter_children() if d.tag == 'DW_TAG_member' and self.name(d) == name]
        if len(members) != 1: raise ValueError('No unique named member: ' + name)
        member = members[0]
        if any(k in member.attributes for k in ('DW_AT_bit_size', 'DW_AT_bit_offset', 'DW_AT_data_bit_offset')):
            raise ValueError('Bitfield inspection is unsupported')
        attr = member.attributes.get('DW_AT_data_member_location')
        if attr is None and die.tag == 'DW_TAG_union_type': offset = 0
        elif attr is not None and isinstance(attr.value, int): offset = checked(attr.value)
        else: raise ValueError('Nonconstant member location is unsupported')
        child = self.type_of(member)
        if offset + self.size(child) > self.size(die): raise ValueError('Member exceeds containing type')
        return child, offset

    def decode(self, die, data, depth=0):
        self.value_nodes += 1
        if depth > 16 or self.value_nodes > 256: raise ValueError('Value expansion budget exceeded')
        die = self.canonical(die)
        size = self.size(die)
        if len(data) != size: raise ValueError('Type size disagrees with value bytes')
        tag = die.tag
        if tag in ('DW_TAG_base_type', 'DW_TAG_enumeration_type'):
            attr = die.attributes.get('DW_AT_encoding')
            if tag == 'DW_TAG_enumeration_type' and not attr:
                if 'DW_AT_type' in die.attributes: return self.decode(self.type_of(die), data, depth + 1)
                raise ValueError('Enumeration signedness is unavailable')
            encoding = attr.value if attr else None
            if encoding in (2, 5, 6, 7, 8):
                value = int.from_bytes(data, 'big', signed=encoding in (5, 6))
                return str(value) if size > 4 else value
            if encoding == 4 and size in (4, 8):
                value = struct.unpack('>f' if size == 4 else '>d', data)[0]
                return value if value == value and abs(value) != float('inf') else str(value)
            raise ValueError('Unsupported base-type encoding')
        if tag == 'DW_TAG_pointer_type': return {'address': int.from_bytes(data, 'big')}
        if tag == 'DW_TAG_array_type':
            element, low, count, stride = self.array(die)
            if count > 64: raise ValueError('Array exceeds 64 elements; select an index')
            return {'lower_bound': low, 'elements': [self.decode(element, data[i*stride:i*stride+self.size(element)], depth+1) for i in range(count)]}
        if tag in ('DW_TAG_structure_type', 'DW_TAG_union_type'):
            members = [d for d in die.iter_children() if d.tag == 'DW_TAG_member']
            if len(members) > 64: raise ValueError('Too many members; select a member')
            result = {}
            for member in members:
                name = self.name(member)
                if not name: raise ValueError('Anonymous member expansion is unsupported')
                child, offset = self.member(die, name)
                result[name] = self.decode(child, data[offset:offset+self.size(child)], depth+1)
            return result
        raise ValueError('Unsupported type: ' + tag)

    def variable(self, expression):
        if len(expression) > 256 or not re.fullmatch(r'[A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*|\[[0-9]+\])*', expression):
            raise ValueError('Use a variable name with .member, ->member or [index] selectors')
        name = re.match(r'\w+', expression).group()
        pc = self.translate(self.pc, True)
        candidates = []
        for die in self.dies:
            if die.tag not in ('DW_TAG_variable', 'DW_TAG_formal_parameter') or self.name(die) != name: continue
            parent, function, depth, valid = die.get_parent(), None, 0, True
            while parent:
                depth += 1
                if depth > 128: raise ValueError('Scope nesting budget exceeded')
                if parent.tag in ('DW_TAG_subprogram', 'DW_TAG_inlined_subroutine', 'DW_TAG_lexical_block'):
                    if self.contains(parent, pc) is False: valid = False
                    if parent.tag == 'DW_TAG_subprogram': function = parent
                parent = parent.get_parent()
            if valid and not die.attributes.get('DW_AT_declaration'): candidates.append((depth, die, function))
        if not candidates: raise ValueError('Variable not found in the current scope')
        deepest = max(c[0] for c in candidates); candidates = [c for c in candidates if c[0] == deepest]
        if len(candidates) != 1: raise ValueError('Ambiguous variable name')
        _, die, function = candidates[0]
        location_expression = self.expression(die, 'DW_AT_location', pc)
        needs_frame_base = any(op.op_name == 'DW_OP_fbreg' for op in self.expr_parser.parse_expr(location_expression))
        cfa = frame_base = None
        if function:
            try: _, cfa, _ = self.frame_rule(self.pc, self.regs)
            except ValueError: pass
            if needs_frame_base:
                frame_expr = self.expression(function, 'DW_AT_frame_base', pc)
                frame_base, _ = self.evaluate(frame_expr, self.regs, cfa, None)
        location, kind = self.evaluate(location_expression, self.regs, cfa, frame_base)
        type_die = self.type_of(die)
        tokens = re.findall(r'(->|\.)([A-Za-z_]\w*)|\[([0-9]+)\]', expression[len(name):])
        if tokens and kind != 'address': raise ValueError('Selectors on register values are unsupported')
        for op, member_name, index_text in tokens:
            type_die = self.canonical(type_die)
            if op == '->':
                if type_die.tag != 'DW_TAG_pointer_type' or self.size(type_die) != 4: raise ValueError('Expected a 32-bit pointer')
                location = int.from_bytes(self.read(checked(location), 4), 'big')
                if not location: raise ValueError('Null pointer')
                type_die = self.canonical(self.type_of(type_die))
            if op:
                if type_die.tag not in ('DW_TAG_structure_type', 'DW_TAG_union_type'): raise ValueError('Expected aggregate type')
                type_die, offset = self.member(type_die, member_name); location += offset
            else:
                if type_die.tag != 'DW_TAG_array_type': raise ValueError('Expected array type')
                element, low, count, stride = self.array(type_die); index = int(index_text)
                if not low <= index < low + count: raise ValueError('Array index out of bounds')
                location += (index - low) * stride; type_die = element
        size = self.size(type_die)
        checked(size, 4096)
        if kind == 'register' and size > 4: raise ValueError('Value exceeds the register width')
        data = self.read(checked(location), size) if kind == 'address' else (location % (1 << (size*8))).to_bytes(size, 'big')
        return {'expression': expression, 'address': location if kind == 'address' else None, 'location_kind': kind,
                'type': self.name(type_die) or self.canonical(type_die).tag, 'size': size, 'hex': data.hex(), 'value': self.decode(type_die, data)}

    def backtrace(self, limit):
        checked(limit, 64)
        pc, regs, frames, seen, reason = self.pc, dict(self.regs), [], set(), 'frame limit'
        for index in range(limit):
            if not pc: reason = 'null return address'; break
            if (pc, regs.get(15)) in seen: reason = 'cyclic frame'; break
            seen.add((pc, regs.get(15)))
            frame = {'index': index, 'pc': pc, 'sp': regs.get(15)}
            # A saved return PC denotes the instruction after the call.
            lookup = pc if index == 0 else pc - 1
            try: frame['source'] = self.source(lookup)
            except ValueError as error: frame['source_error'] = str(error)
            frames.append(frame)
            try:
                row, cfa, return_column = self.frame_rule(lookup, regs)
                if cfa <= regs[15] or cfa - regs[15] > 1048576: raise ValueError('Invalid caller stack progression')
                if return_column not in (24, 25): raise ValueError('Unsupported m68k return column')
                # Callee-saved registers (D2-D7, A2-A6 in the m68k ABI) keep their
                # value unless the CFI says where they were saved.
                caller = {reg: regs[reg] for reg in CALLEE_SAVED if reg in regs}
                for reg, rule in row.items():
                    if not isinstance(reg, int) or reg not in list(range(16)) + [return_column]: continue
                    if rule.type == 'OFFSET': caller[reg] = int.from_bytes(self.read(checked(cfa + rule.arg), 4), 'big')
                    elif rule.type == 'VAL_OFFSET': caller[reg] = checked(cfa + rule.arg)
                    elif rule.type == 'SAME_VALUE':
                        if reg not in regs: raise ValueError('Unavailable preserved register')
                        caller[reg] = regs[reg]
                    elif rule.type == 'REGISTER':
                        if rule.arg not in regs: raise ValueError('Unavailable saved register')
                        caller[reg] = regs[rule.arg]
                    elif rule.type == 'UNDEFINED': caller.pop(reg, None)
                    else: raise ValueError('Unsupported unwind rule: ' + rule.type)
                if return_column not in caller: raise ValueError('Return address is unavailable')
                pc = caller.pop(return_column)
                if pc % 2: raise ValueError('Unaligned return address')
                caller[15] = cfa; regs = caller; frame['unwind'] = 'dwarf-cfi'; frame['cfa'] = cfa
            except (ValueError, KeyError) as error:
                reason = str(error); break
        return {'frames': frames, 'stop_reason': reason, 'truncated': reason == 'frame limit'}


def main():
    try:
        import elftools
    except ImportError:
        raise ValueError('Install the DWARF extra (pip install amiga-devbench[dwarf]) to use DWARF tools')
    if elftools.__version__ != '0.32':
        raise ValueError('DWARF tools require pyelftools==0.32')
    request = json.loads(sys.stdin.readline(24 * 1024 * 1024))
    analysis = Analysis(request)
    if request['action'] == 'source': result = analysis.source(request.get('address', analysis.pc))
    elif request['action'] == 'variable': result = analysis.variable(request['expression'])
    elif request['action'] == 'backtrace': result = analysis.backtrace(request.get('max_frames', 32))
    else: raise ValueError('Unknown DWARF action')
    emit({'result': result})


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        emit({'error': str(error)})
        sys.exit(1)
