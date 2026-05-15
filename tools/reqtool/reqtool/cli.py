"""reqtool — requirements & traceability CLI for ocor-mouse."""
from __future__ import annotations

import sys
import click


@click.group()
@click.version_option()
def main() -> None:
    """Requirements & traceability for the ocor-mouse monorepo."""


@main.command()
@click.option("--reqs-dir", default="docs/requirements", help="Requirements directory.")
def lint(reqs_dir: str) -> None:
    """Validate requirement frontmatter (schema, unique IDs, allowed enum values)."""
    from pathlib import Path
    from reqtool.lint import lint_directory

    errors = lint_directory(Path(reqs_dir))
    if errors:
        for e in errors:
            click.echo(f"ERROR: {e}", err=True)
        click.echo(f"\n{len(errors)} error(s) in {reqs_dir}", err=True)
        sys.exit(1)
    click.echo(f"OK: {reqs_dir} (lint passed)")


@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--root", default=".")
@click.option("--diff/--no-diff", default=False)
@click.option("--base", default="origin/main")
@click.option("--output", default="-")
def trace(reqs_dir: str, root: str, diff: bool, base: str, output: str) -> None:
    """Build traceability matrix linking REQs to code and tests."""
    from pathlib import Path
    import subprocess
    import tempfile
    from reqtool.trace import build_traceability_matrix
    from reqtool.render import render_matrix_markdown, render_diff_markdown
    from reqtool.diff import diff_matrices, matrix_to_refset

    matrix_after = build_traceability_matrix(Path(reqs_dir), Path(root))

    if not diff:
        out = render_matrix_markdown(matrix_after)
    else:
        # git-worktree out the base ref to a temp dir, build matrix there, diff
        with tempfile.TemporaryDirectory() as td:
            subprocess.check_call(["git", "worktree", "add", "--detach", td, base])
            try:
                matrix_before = build_traceability_matrix(
                    Path(td) / reqs_dir, Path(td)
                )
            finally:
                subprocess.check_call(["git", "worktree", "remove", "--force", td])
        d = diff_matrices(matrix_to_refset(matrix_before), matrix_to_refset(matrix_after))
        out = render_diff_markdown(d, base)

    if output == "-":
        click.echo(out)
    else:
        Path(output).write_text(out, encoding="utf-8")
        click.echo(f"wrote {output}", err=True)


@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--root", default=".")
def orphans(reqs_dir: str, root: str) -> None:
    """Report implemented REQs with no code references, and dangling code refs."""
    from pathlib import Path
    from reqtool.trace import build_traceability_matrix
    from reqtool.orphans import find_orphans

    matrix = build_traceability_matrix(Path(reqs_dir), Path(root))
    rep = find_orphans(matrix)
    if rep.unreferenced_implemented:
        click.echo("Implemented but unreferenced:")
        for rid in rep.unreferenced_implemented:
            click.echo(f"  - {rid}")
    if rep.dangling_ids:
        click.echo("Dangling REQ-IDs in code (no matching requirement):")
        for rid in rep.dangling_ids:
            click.echo(f"  - {rid}")
    if not rep.unreferenced_implemented and not rep.dangling_ids:
        click.echo("OK: no orphans")


@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--root", default=".")
@click.option("--tag", required=True)
@click.option("--products", default="hitscan")
@click.option("--output", default="-")
def checklist(reqs_dir: str, root: str, tag: str, products: str, output: str) -> None:
    """Emit a QA checklist (markdown) for verification=HIL REQs in this release."""
    from pathlib import Path
    from reqtool.trace import build_traceability_matrix
    from reqtool.checklist import build_checklist

    matrix = build_traceability_matrix(Path(reqs_dir), Path(root))
    md = build_checklist(matrix, tag=tag, products=products.split(","))
    if output == "-":
        click.echo(md)
    else:
        Path(output).write_text(md, encoding="utf-8")
        click.echo(f"wrote {output}", err=True)


@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--root", default=".")
@click.option("--tag", required=True)
@click.option("--output", default="-")
def report(reqs_dir: str, root: str, tag: str, output: str) -> None:
    """Emit the full release traceability report (markdown)."""
    from pathlib import Path
    from reqtool.trace import build_traceability_matrix
    from reqtool.report import build_report

    matrix = build_traceability_matrix(Path(reqs_dir), Path(root))
    md = build_report(matrix, tag=tag)
    if output == "-":
        click.echo(md)
    else:
        Path(output).write_text(md, encoding="utf-8")
        click.echo(f"wrote {output}", err=True)


if __name__ == "__main__":
    main()
