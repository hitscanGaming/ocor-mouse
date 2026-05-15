"""Lint command: validate frontmatter schema and uniqueness of IDs."""
from __future__ import annotations

from pathlib import Path

from reqtool.schema import RequirementError, parse_requirement


def lint_directory(reqs_dir: Path) -> list[str]:
    """Return a list of error strings. Empty list = lint passed."""
    errors: list[str] = []
    seen_ids: dict[str, Path] = {}

    if not reqs_dir.exists():
        return [f"{reqs_dir}: directory does not exist"]

    for md in sorted(reqs_dir.rglob("REQ-*.md")):
        try:
            req = parse_requirement(md.read_text(encoding="utf-8"), source=str(md))
        except RequirementError as e:
            errors.append(str(e))
            continue
        if req.id in seen_ids:
            errors.append(
                f"{md}: duplicate id '{req.id}' (also in {seen_ids[req.id]})"
            )
        else:
            seen_ids[req.id] = md
    return errors
