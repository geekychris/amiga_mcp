"""Tests for new protocol features: parsing and formatting.

Tests cover: CAPABILITIES, PROCLIST, PROCSTAT, TAILDATA, CHECKSUM,
ASSIGNS, PROTECT, SIGNAL, TAIL, STOPTAIL, RENAME, SETCOMMENT, COPY, APPEND
"""

import pytest
from amiga_devbench.protocol import parse_message, format_command


# ─── CAPABILITIES ───

class TestCapabilities:
    def test_parse_capabilities(self):
        line = "CAPABILITIES|AmigaBridge 1.0|1|1024|PING,INSPECT,GETVAR"
        msg = parse_message(line)
        assert msg is not None
        assert msg["type"] == "CAPABILITIES"
        assert msg["version"] == "AmigaBridge 1.0"
        assert msg["protocolLevel"] == 1
        assert msg["maxLine"] == 1024
        assert msg["commands"] == ["PING", "INSPECT", "GETVAR"]

    def test_parse_capabilities_empty_commands(self):
        line = "CAPABILITIES|AmigaBridge 1.0|1|1024|"
        msg = parse_message(line)
        assert msg is not None
        assert msg["commands"] == [""]

    def test_parse_capabilities_many_commands(self):
        cmds = ",".join(["CMD" + str(i) for i in range(50)])
        line = f"CAPABILITIES|v2.0|2|2048|{cmds}"
        msg = parse_message(line)
        assert msg is not None
        assert len(msg["commands"]) == 50

    def test_format_capabilities(self):
        result = format_command({"type": "CAPABILITIES"})
        assert result == "CAPABILITIES"


# ─── PROCLIST ───

class TestProcList:
    def test_parse_proclist_with_processes(self):
        line = "PROCLIST|2|1:DH2:Dev/hello:running,2:DH2:Dev/test:running"
        msg = parse_message(line)
        assert msg is not None
        assert msg["type"] == "PROCLIST"
        assert msg["count"] == 2
        assert len(msg["processes"]) == 2
        assert msg["processes"][0]["id"] == 1
        assert msg["processes"][0]["command"] == "DH2"
        assert msg["processes"][0]["status"] == "Dev/hello:running"

    def test_parse_proclist_empty(self):
        line = "PROCLIST|0|"
        msg = parse_message(line)
        assert msg is not None
        assert msg["count"] == 0
        assert msg["processes"] == []

    def test_format_proclist(self):
        result = format_command({"type": "PROCLIST"})
        assert result == "PROCLIST"


# ─── PROCSTAT ───

class TestProcStat:
    def test_parse_procstat(self):
        line = "PROCSTAT|5|DH2:Dev/hello|running"
        msg = parse_message(line)
        assert msg is not None
        assert msg["type"] == "PROCSTAT"
        assert msg["id"] == 5
        assert msg["command"] == "DH2:Dev/hello"
        assert msg["status"] == "running"

    def test_format_procstat(self):
        result = format_command({"type": "PROCSTAT", "id": 3})
        assert result == "PROCSTAT|3"


# ─── SIGNAL ───

class TestSignal:
    def test_format_signal_ctrl_c(self):
        result = format_command({"type": "SIGNAL", "id": 1, "sigType": 0})
        assert result == "SIGNAL|1|0"

    def test_format_signal_ctrl_d(self):
        result = format_command({"type": "SIGNAL", "id": 5, "sigType": 1})
        assert result == "SIGNAL|5|1"


# ─── TAILDATA ───

class TestTailData:
    def test_parse_taildata(self):
        line = "TAILDATA|T:mylog.txt|48656c6c6f"
        msg = parse_message(line)
        assert msg is not None
        assert msg["type"] == "TAILDATA"
        assert msg["path"] == "T:mylog.txt"
        assert msg["data"] == "48656c6c6f"

    def test_parse_taildata_truncated(self):
        line = "TAILDATA|T:mylog.txt|TRUNCATED"
        msg = parse_message(line)
        assert msg is not None
        assert msg["data"] == "TRUNCATED"

    def test_format_tail(self):
        result = format_command({"type": "TAIL", "path": "T:mylog.txt"})
        assert result == "TAIL|T:mylog.txt"

    def test_format_stoptail(self):
        result = format_command({"type": "STOPTAIL"})
        assert result == "STOPTAIL"


# ─── CHECKSUM ───

class TestChecksum:
    def test_parse_checksum(self):
        line = "CHECKSUM|DH2:Dev/hello|d87f7e0c|12345"
        msg = parse_message(line)
        assert msg is not None
        assert msg["type"] == "CHECKSUM"
        assert msg["path"] == "DH2:Dev/hello"
        assert msg["crc32"] == "d87f7e0c"
        assert msg["size"] == 12345

    def test_parse_checksum_zero_size(self):
        line = "CHECKSUM|T:empty.txt|00000000|0"
        msg = parse_message(line)
        assert msg is not None
        assert msg["size"] == 0

    def test_format_checksum(self):
        result = format_command({"type": "CHECKSUM", "path": "DH2:Dev/hello"})
        assert result == "CHECKSUM|DH2:Dev/hello"


# ─── ASSIGNS ───

class TestAssigns:
    def test_parse_assigns(self):
        line = "ASSIGNS|3|LIBS:SYS/Libs:A,DEVS:SYS/Devs:A,FONTS:SYS/Fonts:L"
        msg = parse_message(line)
        assert msg is not None
        assert msg["type"] == "ASSIGNS"
        assert msg["count"] == 3
        assert len(msg["assigns"]) == 3
        assert msg["assigns"][0]["name"] == "LIBS"
        assert msg["assigns"][0]["path"] == "SYS/Libs"
        assert msg["assigns"][0]["assignType"] == "A"
        assert msg["assigns"][2]["assignType"] == "L"

    def test_parse_assigns_empty(self):
        line = "ASSIGNS|0|"
        msg = parse_message(line)
        assert msg is not None
        assert msg["count"] == 0
        assert msg["assigns"] == []

    def test_format_assigns(self):
        result = format_command({"type": "ASSIGNS"})
        assert result == "ASSIGNS"

    def test_format_assign_replace(self):
        result = format_command({"type": "ASSIGN", "name": "DEVTOOLS", "path": "DH2:Dev/Tools"})
        assert result == "ASSIGN|DEVTOOLS|DH2:Dev/Tools"

    def test_format_assign_add(self):
        result = format_command({"type": "ASSIGN", "name": "LIBS", "path": "Work:Libs", "mode": "ADD"})
        assert result == "ASSIGN|LIBS|Work:Libs|ADD"

    def test_format_assign_remove(self):
        result = format_command({"type": "ASSIGN", "name": "MYASSIGN", "path": "", "mode": "REMOVE"})
        assert result == "ASSIGN|MYASSIGN||REMOVE"


# ─── PROTECT ───

class TestProtect:
    def test_parse_protect(self):
        line = "PROTECT|DH2:Dev/hello|00000040"
        msg = parse_message(line)
        assert msg is not None
        assert msg["type"] == "PROTECT"
        assert msg["path"] == "DH2:Dev/hello"
        assert msg["bits"] == "00000040"

    def test_format_protect_get(self):
        result = format_command({"type": "PROTECT", "path": "DH2:test"})
        assert result == "PROTECT|DH2:test"

    def test_format_protect_set(self):
        result = format_command({"type": "PROTECT", "path": "DH2:test", "bits": "00000040"})
        assert result == "PROTECT|DH2:test|00000040"


# ─── RENAME ───

class TestRename:
    def test_format_rename(self):
        result = format_command({"type": "RENAME", "oldPath": "T:old.txt", "newPath": "T:new.txt"})
        assert result == "RENAME|T:old.txt|T:new.txt"


# ─── SETCOMMENT ───

class TestSetComment:
    def test_format_setcomment(self):
        result = format_command({"type": "SETCOMMENT", "path": "T:test.txt", "comment": "hello world"})
        assert result == "SETCOMMENT|T:test.txt|hello world"


# ─── COPY ───

class TestCopy:
    def test_format_copy(self):
        result = format_command({"type": "COPY", "src": "DH2:Dev/a", "dst": "DH2:Dev/b"})
        assert result == "COPY|DH2:Dev/a|DH2:Dev/b"


# ─── APPEND ───

class TestAppend:
    def test_format_append(self):
        result = format_command({"type": "APPEND", "path": "T:log.txt", "hexData": "48656c6c6f"})
        assert result == "APPEND|T:log.txt|48656c6c6f"


# ─── OK/ERR responses for new commands ───

class TestResponses:
    def test_parse_ok_signal(self):
        line = "OK|SIGNAL|Signal 0 sent to proc 1"
        msg = parse_message(line)
        assert msg is not None
        assert msg["type"] == "OK"
        assert msg["context"] == "SIGNAL"

    def test_parse_ok_rename(self):
        line = "OK|RENAME|T:new.txt"
        msg = parse_message(line)
        assert msg is not None
        assert msg["context"] == "RENAME"

    def test_parse_ok_copy(self):
        line = "OK|COPY|DH2:Dev/b"
        msg = parse_message(line)
        assert msg is not None
        assert msg["context"] == "COPY"

    def test_parse_err_checksum(self):
        line = "ERR|CHECKSUM|file not found"
        msg = parse_message(line)
        assert msg is not None
        assert msg["type"] == "ERR"
        assert msg["context"] == "CHECKSUM"

    def test_parse_ok_tail(self):
        line = "OK|TAIL|T:mylog.txt"
        msg = parse_message(line)
        assert msg is not None
        assert msg["context"] == "TAIL"

    def test_parse_ok_stoptail(self):
        line = "OK|STOPTAIL|stopped"
        msg = parse_message(line)
        assert msg is not None
        assert msg["context"] == "STOPTAIL"

    def test_parse_ok_append(self):
        line = "OK|APPEND|5"
        msg = parse_message(line)
        assert msg is not None
        assert msg["context"] == "APPEND"

    def test_parse_ok_setcomment(self):
        line = "OK|SETCOMMENT|T:test.txt"
        msg = parse_message(line)
        assert msg is not None
        assert msg["context"] == "SETCOMMENT"

    def test_parse_ok_assign(self):
        line = "OK|ASSIGN|Set DEVTOOLS -> DH2:Dev/Tools"
        msg = parse_message(line)
        assert msg is not None
        assert msg["context"] == "ASSIGN"

    def test_parse_err_protect(self):
        line = "ERR|PROTECT|failed to read"
        msg = parse_message(line)
        assert msg is not None
        assert msg["type"] == "ERR"
        assert msg["context"] == "PROTECT"


# ─── Round-trip: format then parse ───

class TestRoundTrip:
    """Test that format_command output can be parsed back by parse_message
    for response messages that come back from the daemon."""

    def test_capabilities_roundtrip(self):
        # Simulate: host sends CAPABILITIES, daemon responds
        cmd = format_command({"type": "CAPABILITIES"})
        assert cmd == "CAPABILITIES"
        # Daemon would respond with:
        resp = "CAPABILITIES|AmigaBridge 1.0|1|1024|PING,INSPECT"
        msg = parse_message(resp)
        assert msg["version"] == "AmigaBridge 1.0"

    def test_checksum_roundtrip(self):
        cmd = format_command({"type": "CHECKSUM", "path": "T:test"})
        assert "T:test" in cmd
        resp = "CHECKSUM|T:test|aabbccdd|100"
        msg = parse_message(resp)
        assert msg["crc32"] == "aabbccdd"
        assert msg["size"] == 100
