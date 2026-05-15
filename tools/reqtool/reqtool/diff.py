"""Diff two traceability snapshots (used by `trace --diff` in PR CI)."""
from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class RefDiff:
    added: set[str] = field(default_factory=set)
    removed: set[str] = field(default_factory=set)


@dataclass
class MatrixDiff:
    added: list[str] = field(default_factory=list)
    removed: list[str] = field(default_factory=list)
    changed: dict[str, RefDiff] = field(default_factory=dict)


def diff_matrices(before: dict[str, set[str]], after: dict[str, set[str]]) -> MatrixDiff:
    d = MatrixDiff()
    d.added = sorted(set(after) - set(before))
    d.removed = sorted(set(before) - set(after))
    for rid in sorted(set(before) & set(after)):
        added = after[rid] - before[rid]
        removed = before[rid] - after[rid]
        if added or removed:
            d.changed[rid] = RefDiff(added=added, removed=removed)
    return d


def matrix_to_refset(matrix) -> dict[str, set[str]]:
    """Flatten Matrix → {req_id: {'path:line', ...}} for diffing."""
    return {
        rid: {f"{r.path}:{r.line}" for r in row.references}
        for rid, row in matrix.rows.items()
    }
