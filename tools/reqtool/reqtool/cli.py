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
    click.echo(f"lint: {reqs_dir} (stub)")
    sys.exit(0)


@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--root", default=".", help="Repo root to grep for REQ-ID references.")
@click.option("--diff/--no-diff", default=False, help="Output a diff vs. base ref.")
@click.option("--base", default="origin/main", help="Base ref for diff mode.")
def trace(reqs_dir: str, root: str, diff: bool, base: str) -> None:
    """Build traceability matrix linking REQs to code and tests."""
    click.echo(f"trace: {reqs_dir} root={root} diff={diff} base={base} (stub)")
    sys.exit(0)


@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--root", default=".")
def orphans(reqs_dir: str, root: str) -> None:
    """Report implemented REQs with no code references, and dangling code refs."""
    click.echo(f"orphans: {reqs_dir} root={root} (stub)")
    sys.exit(0)


@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--tag", required=True, help="Release tag (e.g. v1.0.0).")
@click.option("--products", default="hitscan", help="Comma-separated products in this release.")
def checklist(reqs_dir: str, tag: str, products: str) -> None:
    """Emit a QA checklist (markdown) for verification=HIL REQs in this release."""
    click.echo(f"checklist: {reqs_dir} tag={tag} products={products} (stub)")
    sys.exit(0)


@main.command()
@click.option("--reqs-dir", default="docs/requirements")
@click.option("--root", default=".")
@click.option("--tag", required=True)
def report(reqs_dir: str, root: str, tag: str) -> None:
    """Emit the full release traceability report (markdown)."""
    click.echo(f"report: {reqs_dir} root={root} tag={tag} (stub)")
    sys.exit(0)


if __name__ == "__main__":
    main()
