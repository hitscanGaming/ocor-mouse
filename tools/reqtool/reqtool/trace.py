"""Trace command: cross-reference REQ-IDs in code/tests against requirement files."""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

from reqtool.schema import Requirement, RequirementError, parse_requirement


REQ_ID_RE = re.compile(r"\bREQ-[A-Z][A-Z0-9]+-\d{3}\b")

SCAN_EXTS = {".c", ".h", ".cpp", ".hpp", ".rs", ".py", ".js", ".ts", ".mjs", ".html", ".md"}
SKIP_DIRS = {"build", "build_*", "node_modules", ".west", "dist", "__pycache__", ".git", ".venv"}


@dataclass
class Reference:
    path: Path
    line: int
    snippet: str


@dataclass
class RequirementRow:
    req: Requirement
    references: list[Reference] = field(default_factory=list)


@dataclass
class Matrix:
    rows: dict[str, RequirementRow] = field(default_factory=dict)
    dangling: dict[str, list[Reference]] = field(default_factory=dict)

    def __getitem__(self, key: str) -> RequirementRow:
        return self.rows[key]

    def __contains__(self, key: str) -> bool:
        return key in self.rows


def _iter_source_files(root: Path):
    for p in root.rglob("*"):
        if not p.is_file():
            continue
        if p.suffix not in SCAN_EXTS:
            continue
        parts = set(p.parts)
        if parts & SKIP_DIRS:
            continue
        if "requirements" in p.parts:
            # don't grep the req files themselves as "references" to themselves
            continue
        yield p


def build_traceability_matrix(reqs_dir: Path, root: Path) -> Matrix:
    matrix = Matrix()
    for md in sorted(reqs_dir.rglob("REQ-*.md")):
        try:
            req = parse_requirement(md.read_text(encoding="utf-8"), source=str(md))
        except RequirementError:
            continue
        matrix.rows[req.id] = RequirementRow(req=req)

    for src in _iter_source_files(root):
        try:
            content = src.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        for lineno, line in enumerate(content.splitlines(), start=1):
            for match in REQ_ID_RE.finditer(line):
                rid = match.group(0)
                ref = Reference(path=src, line=lineno, snippet=line.strip())
                if rid in matrix.rows:
                    matrix.rows[rid].references.append(ref)
                else:
                    matrix.dangling.setdefault(rid, []).append(ref)
    return matrix
