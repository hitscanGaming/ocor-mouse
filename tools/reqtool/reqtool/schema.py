"""Frontmatter parsing and validation for requirement markdown files."""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Any

import yaml


ID_RE = re.compile(r"^REQ-[A-Z][A-Z0-9]+-\d{3}$")
ALLOWED_STATUS = {"draft", "approved", "implemented", "verified", "deprecated"}
ALLOWED_VERIFICATION = {"code-review", "unit-test", "native-sim", "HIL"}
ALLOWED_PRIORITY = {"P0", "P1", "P2"}
REQUIRED_FIELDS = ("id", "title", "status", "verification", "priority", "owner", "products")


class RequirementError(Exception):
    """Raised when a requirement file fails schema validation."""


@dataclass
class Requirement:
    id: str
    title: str
    status: str
    verification: str
    priority: str
    owner: str
    products: list[str]
    body: str
    source: str = ""
    extra: dict[str, Any] = field(default_factory=dict)


def parse_requirement(text: str, source: str) -> Requirement:
    if not text.startswith("---"):
        raise RequirementError(f"{source}: missing frontmatter (file must start with '---')")
    parts = text.split("---", 2)
    if len(parts) < 3:
        raise RequirementError(f"{source}: missing frontmatter close ('---')")
    front_raw, body = parts[1], parts[2]
    try:
        front = yaml.safe_load(front_raw) or {}
    except yaml.YAMLError as e:
        raise RequirementError(f"{source}: invalid YAML frontmatter: {e}") from e
    if not isinstance(front, dict):
        raise RequirementError(f"{source}: frontmatter must be a mapping")

    for fld in REQUIRED_FIELDS:
        if fld not in front:
            raise RequirementError(f"{source}: missing required field: {fld}")

    rid = str(front["id"])
    if not ID_RE.match(rid):
        raise RequirementError(
            f"{source}: invalid id format: '{rid}' (expected REQ-AREA-NNN)"
        )
    status = str(front["status"])
    if status not in ALLOWED_STATUS:
        raise RequirementError(
            f"{source}: invalid status '{status}' (allowed: {sorted(ALLOWED_STATUS)})"
        )
    verification = str(front["verification"])
    if verification not in ALLOWED_VERIFICATION:
        raise RequirementError(
            f"{source}: invalid verification '{verification}' "
            f"(allowed: {sorted(ALLOWED_VERIFICATION)})"
        )
    priority = str(front["priority"])
    if priority not in ALLOWED_PRIORITY:
        raise RequirementError(
            f"{source}: invalid priority '{priority}' (allowed: {sorted(ALLOWED_PRIORITY)})"
        )

    products = front["products"]
    if not isinstance(products, list) or not all(isinstance(p, str) for p in products):
        raise RequirementError(f"{source}: products must be a list of strings")

    known = set(REQUIRED_FIELDS)
    extra = {k: v for k, v in front.items() if k not in known}

    return Requirement(
        id=rid,
        title=str(front["title"]),
        status=status,
        verification=verification,
        priority=priority,
        owner=str(front["owner"]),
        products=products,
        body=body.lstrip("\n"),
        source=source,
        extra=extra,
    )
