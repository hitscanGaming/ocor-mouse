"""Markdown rendering for traceability matrix and reports."""
from __future__ import annotations

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
