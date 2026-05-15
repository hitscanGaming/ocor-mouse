"""Orphan / dangling-reference detection."""
from __future__ import annotations

from dataclasses import dataclass, field

from reqtool.trace import Matrix


@dataclass
class OrphanReport:
    unreferenced_implemented: list[str] = field(default_factory=list)
    dangling_ids: list[str] = field(default_factory=list)


def find_orphans(matrix: Matrix) -> OrphanReport:
    report = OrphanReport()
    for rid, row in matrix.rows.items():
        if row.req.status == "implemented" and not row.references:
            report.unreferenced_implemented.append(rid)
    report.unreferenced_implemented.sort()
    report.dangling_ids = sorted(matrix.dangling.keys())
    return report
