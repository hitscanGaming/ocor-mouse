import pytest
from reqtool.schema import parse_requirement, RequirementError


VALID = """---
id: REQ-DPI-001
title: DPI adjustable 100-32000 in 50-step increments
status: approved
verification: HIL
priority: P0
owner: fw-team
products: [hitscan]
---

## Description
Body text.
"""


def test_parse_valid_requirement():
    req = parse_requirement(VALID, source="REQ-DPI-001.md")
    assert req.id == "REQ-DPI-001"
    assert req.title.startswith("DPI adjustable")
    assert req.status == "approved"
    assert req.verification == "HIL"
    assert req.priority == "P0"
    assert req.owner == "fw-team"
    assert req.products == ["hitscan"]
    assert "Body text" in req.body


def test_missing_id_raises():
    bad = VALID.replace("id: REQ-DPI-001\n", "")
    with pytest.raises(RequirementError, match="missing required field: id"):
        parse_requirement(bad, source="bad.md")


def test_unknown_status_raises():
    bad = VALID.replace("status: approved", "status: pending")
    with pytest.raises(RequirementError, match="invalid status"):
        parse_requirement(bad, source="bad.md")


def test_unknown_verification_raises():
    bad = VALID.replace("verification: HIL", "verification: vibes")
    with pytest.raises(RequirementError, match="invalid verification"):
        parse_requirement(bad, source="bad.md")


def test_id_format_enforced():
    bad = VALID.replace("REQ-DPI-001", "DPI-001")
    with pytest.raises(RequirementError, match="invalid id format"):
        parse_requirement(bad, source="bad.md")


def test_no_frontmatter_raises():
    with pytest.raises(RequirementError, match="missing frontmatter"):
        parse_requirement("plain body, no frontmatter", source="bad.md")
