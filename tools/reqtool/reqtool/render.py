"""Markdown rendering for traceability matrix and reports."""
from __future__ import annotations

from reqtool.diff import MatrixDiff
from reqtool.trace import Matrix


def render_matrix_markdown(matrix: Matrix) -> str:
    lines = [
        "# Traceability Matrix",
        "",
        "| REQ ID | Title | Status | Verification | References |",
        "|---|---|---|---|---|",
    ]
    for rid in sorted(matrix.rows):
        row = matrix.rows[rid]
        req = row.req
        ref_count = len(row.references)
        ref_summary = f"{ref_count} ref(s)" if ref_count else "—"
        lines.append(
            f"| {req.id} | {req.title} | {req.status} | {req.verification} | {ref_summary} |"
        )

    if matrix.dangling:
        lines += ["", "## Dangling references (REQ-IDs in code with no matching requirement)", ""]
        for rid in sorted(matrix.dangling):
            for ref in matrix.dangling[rid]:
                lines.append(f"- `{rid}` in `{ref.path}:{ref.line}`")
    return "\n".join(lines) + "\n"


def render_diff_markdown(d: MatrixDiff, base: str) -> str:
    lines = [f"# Traceability diff vs `{base}`", ""]
    if d.added:
        lines += ["## New requirements referenced", ""]
        for rid in d.added:
            lines.append(f"- `{rid}`")
        lines.append("")
    if d.removed:
        lines += ["## Requirements no longer referenced", ""]
        for rid in d.removed:
            lines.append(f"- `{rid}`")
        lines.append("")
    if d.changed:
        lines += ["## Reference counts changed", ""]
        for rid, rd in d.changed.items():
            lines.append(f"### {rid}")
            for a in sorted(rd.added):
                lines.append(f"- ✚ `{a}`")
            for r in sorted(rd.removed):
                lines.append(f"- ✖ `{r}`")
            lines.append("")
    if not (d.added or d.removed or d.changed):
        lines.append("_No traceability changes in this PR._")
    return "\n".join(lines) + "\n"
