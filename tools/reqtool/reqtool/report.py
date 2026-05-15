"""Full release traceability report — bundled into release artifacts."""
from __future__ import annotations

from collections import Counter

from reqtool.trace import Matrix


_MAX_REFS_PER_REQ = 20  # cap per-req listing to keep reports human-readable


def build_report(matrix: Matrix, tag: str) -> str:
    status_counts: Counter[str] = Counter()
    verif_counts: Counter[str] = Counter()
    referenced = 0
    for row in matrix.rows.values():
        status_counts[row.req.status] += 1
        verif_counts[row.req.verification] += 1
        if row.references:
            referenced += 1
    total = len(matrix.rows)

    lines = [
        f"# Traceability Report — {tag}",
        "",
        "## Summary",
        "",
        f"- Total requirements: **{total}**",
        f"- Referenced from code/tests: **{referenced}**",
        f"- Dangling REQ-IDs in code: **{len(matrix.dangling)}**",
        "",
        "### By status",
        "",
    ]
    for s in sorted(status_counts):
        lines.append(f"- {s}: {status_counts[s]}")
    lines += ["", "### By verification method", ""]
    for v in sorted(verif_counts):
        lines.append(f"- {v}: {verif_counts[v]}")

    lines += ["", "## Per-requirement", ""]
    for rid in sorted(matrix.rows):
        row = matrix.rows[rid]
        req = row.req
        lines += [
            f"### {req.id} — {req.title}",
            "",
            f"- Status: `{req.status}` · Verification: `{req.verification}` · "
            f"Priority: `{req.priority}` · Owner: `{req.owner}`",
            f"- Products: {', '.join(req.products) or '—'}",
            f"- References: {len(row.references)}",
            "",
        ]
        for ref in row.references[:_MAX_REFS_PER_REQ]:
            lines.append(f"  - `{ref.path}:{ref.line}` — {ref.snippet[:80]}")
        if len(row.references) > _MAX_REFS_PER_REQ:
            lines.append(f"  - ... ({len(row.references) - _MAX_REFS_PER_REQ} more)")
        lines.append("")

    if matrix.dangling:
        lines += ["## Dangling references", ""]
        for rid in sorted(matrix.dangling):
            for ref in matrix.dangling[rid]:
                lines.append(f"- `{rid}` in `{ref.path}:{ref.line}`")
    return "\n".join(lines) + "\n"
