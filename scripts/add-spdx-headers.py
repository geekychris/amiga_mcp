#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins
"""
Add SPDX-License-Identifier + copyright headers to source files.

Idempotent: if the file already has an SPDX line, skip.
Third-party safe: if the file starts with a copyright line that doesn't
mention Chris Collins, skip and report for manual review.
Shebang-aware: `#!...` on line 1 stays on line 1; header goes on line 2.
"""

import argparse
import os
import re
import sys
from pathlib import Path

HEADER_LINES = [
    "SPDX-License-Identifier: MIT",
    "Copyright (c) 2026 Chris Collins",
]

# Per-extension comment style: (line_prefix, line_suffix)
COMMENT_STYLES = {
    # C-family — line comments preferred over /* */ so multi-line
    # nesting warnings never fire.
    ".c":       ("// ", ""),
    ".cpp":     ("// ", ""),
    ".cc":      ("// ", ""),
    ".h":       ("// ", ""),
    ".hpp":     ("// ", ""),
    ".hh":      ("// ", ""),
    # Shell-family / config
    ".py":      ("# ", ""),
    ".sh":      ("# ", ""),
    ".bash":    ("# ", ""),
    ".zsh":     ("# ", ""),
    ".toml":    ("# ", ""),
    ".yaml":    ("# ", ""),
    ".yml":     ("# ", ""),
    ".mk":      ("# ", ""),
    ".fs-uae":  ("# ", ""),
    ".cfg":     ("# ", ""),
    ".ini":     ("# ", ""),
    # Markdown — HTML comments so nothing renders in the output.
    ".md":      ("<!-- ", " -->"),
}

# Exact filename → comment style (extension-less files).
FILENAME_STYLES = {
    "Makefile":         ("# ", ""),
    "makefile":         ("# ", ""),
    "GNUmakefile":      ("# ", ""),
    "Dockerfile":       ("# ", ""),
}

# Directories we never descend into.
# `.claude` holds Claude Code slash-command / skill definitions whose first
# non-blank line is displayed as the command description in the UI —
# prepending a comment there breaks the listing.
SKIP_DIRS = {
    ".git", ".venv", ".idea", "node_modules", "__pycache__",
    "build", "dist", ".pytest_cache", ".mypy_cache", ".ruff_cache",
    ".tox", "target", ".gradle", "vendor",
    ".claude", ".vscode", ".github",
}

# Files we never touch.
SKIP_FILES = {
    "LICENSE", "LICENSE.txt", "LICENSE.md", "COPYING", "COPYING.txt",
    "NOTICE", "NOTICE.txt", ".DS_Store", ".gitignore", ".gitattributes",
    ".gitmodules",
}

# Extensions of files we never touch (binaries + no-comment formats).
SKIP_EXTS = {
    ".json",  # no comment syntax
    ".o", ".a", ".so", ".dylib", ".dll", ".exe",
    ".png", ".jpg", ".jpeg", ".gif", ".webp", ".ico", ".svg",
    ".zip", ".tar", ".gz", ".bz2", ".xz", ".lha", ".lzh", ".lzx",
    ".hdf", ".adf", ".ipf", ".rom", ".uss", ".mod",
    ".pdf", ".mp3", ".wav", ".mp4", ".mov", ".webm",
    ".pyc", ".pyo", ".d",
    ".lock",
}

# If any of these regexes match in the first N lines, treat as
# third-party and skip. Matches "Copyright (c) 2019 Someone Else"
# but permits variants mentioning Chris.
COPYRIGHT_RE = re.compile(
    r"copyright\s*(?:\(c\)|©|©)", re.IGNORECASE
)
CHRIS_RE = re.compile(r"chris\s*(?:j\s*)?collins|collins.*chris", re.IGNORECASE)

FIRST_LINES_TO_CHECK = 20


def choose_style(path: Path):
    ext = path.suffix
    if ext in COMMENT_STYLES:
        return COMMENT_STYLES[ext]
    if path.name in FILENAME_STYLES:
        return FILENAME_STYLES[path.name]
    return None


def looks_binary(head: bytes) -> bool:
    return b"\x00" in head


def has_existing_spdx(text: str) -> bool:
    return "SPDX-License-Identifier" in text.splitlines()[0:5].__str__() or \
           any("SPDX-License-Identifier" in ln for ln in text.splitlines()[:20])


def has_third_party_copyright(text: str) -> bool:
    """True if the first N lines carry a copyright line NOT mentioning Chris."""
    head = "\n".join(text.splitlines()[:FIRST_LINES_TO_CHECK])
    if not COPYRIGHT_RE.search(head):
        return False
    return not CHRIS_RE.search(head)


def is_shebang_or_directive(line: str) -> bool:
    """Lines that must stay on line 1 (shebang, XML decl, coding decl)."""
    if line.startswith("#!"):
        return True
    if line.startswith("<?xml"):
        return True
    if line.startswith("<!DOCTYPE"):
        return True
    # Python PEP-263 encoding declaration on line 1 or 2.
    if re.match(r"^#.*coding[:=]\s*[-\w.]+", line):
        return True
    return False


def build_header(prefix: str, suffix: str) -> str:
    parts = [f"{prefix}{h}{suffix}" for h in HEADER_LINES]
    return "\n".join(parts) + "\n"


def process_file(path: Path, dry_run: bool):
    """Returns ('modified'|'skipped-spdx'|'skipped-third-party'|'skipped-binary'|
                'skipped-unsupported'|'skipped-empty', message_or_None)."""
    style = choose_style(path)
    if style is None:
        return ("skipped-unsupported", None)

    try:
        raw = path.read_bytes()
    except OSError as exc:
        return ("skipped-error", f"read failed: {exc}")

    if not raw:
        return ("skipped-empty", None)

    if looks_binary(raw[:512]):
        return ("skipped-binary", None)

    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        # Try latin-1 as a fallback for old Amiga assets.
        try:
            text = raw.decode("latin-1")
            encoding = "latin-1"
        except Exception as exc:
            return ("skipped-error", f"decode failed: {exc}")
    else:
        encoding = "utf-8"

    if has_existing_spdx(text):
        return ("skipped-spdx", None)

    if has_third_party_copyright(text):
        return ("skipped-third-party", None)

    prefix, suffix = style
    header = build_header(prefix, suffix)

    lines = text.split("\n")
    # Determine where the header goes: after any leading shebang / directive
    # on line 1 (and possibly line 2 for Python coding decls).
    insert_at = 0
    while insert_at < min(2, len(lines)) and \
          lines[insert_at] and is_shebang_or_directive(lines[insert_at]):
        insert_at += 1

    # Assemble: preserved prefix + blank line (if any prefix) + header
    # + blank line + rest of file.
    prefix_lines = lines[:insert_at]
    rest_lines = lines[insert_at:]

    header_block = header
    # Ensure exactly one blank line between header and the rest, unless
    # the rest already starts with a blank line.
    if rest_lines and rest_lines[0] != "":
        header_block += "\n"

    if prefix_lines:
        new_text = "\n".join(prefix_lines) + "\n" + header_block + \
                   "\n".join(rest_lines)
    else:
        new_text = header_block + "\n".join(rest_lines)

    if dry_run:
        return ("modified", None)

    try:
        path.write_text(new_text, encoding=encoding)
    except OSError as exc:
        return ("skipped-error", f"write failed: {exc}")

    return ("modified", None)


def walk(root: Path):
    for dirpath, dirnames, filenames in os.walk(root):
        # Prune skip dirs in-place so we don't descend.
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for fname in filenames:
            if fname in SKIP_FILES:
                continue
            ext = Path(fname).suffix
            if ext in SKIP_EXTS:
                continue
            yield Path(dirpath) / fname


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("root", nargs="?", default=".",
                    help="Root directory to process (default: cwd)")
    ap.add_argument("--dry-run", action="store_true",
                    help="Report what would change without writing")
    ap.add_argument("--list-third-party", action="store_true",
                    help="Print every file skipped for third-party copyright")
    ap.add_argument("--list-modified", action="store_true",
                    help="Print every file that would be / was modified")
    args = ap.parse_args()

    root = Path(args.root).resolve()
    counts = {}
    third_party = []
    modified = []
    errors = []

    for f in walk(root):
        result, msg = process_file(f, args.dry_run)
        counts[result] = counts.get(result, 0) + 1
        if result == "skipped-third-party":
            third_party.append(str(f.relative_to(root)))
        elif result == "modified":
            modified.append(str(f.relative_to(root)))
        elif result == "skipped-error":
            errors.append(f"{f}: {msg}")

    print(f"Root: {root}")
    print(f"Dry run: {args.dry_run}")
    print("Summary:")
    for k in sorted(counts):
        print(f"  {k:30s} {counts[k]:5d}")

    if args.list_third_party and third_party:
        print("\nThird-party (skipped for existing non-Chris copyright):")
        for p in third_party:
            print(f"  {p}")

    if args.list_modified and modified:
        print(f"\nWould modify ({len(modified)}):" if args.dry_run
              else f"\nModified ({len(modified)}):")
        for p in modified:
            print(f"  {p}")

    if errors:
        print("\nErrors:")
        for e in errors:
            print(f"  {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
