from pathlib import Path
from reqtool.orphans import find_orphans
from reqtool.trace import build_traceability_matrix

FIX = Path(__file__).parent / "fixtures" / "sample_repo"


def test_orphans_finds_unreferenced_implemented():
    """REQ-DPI-001 (implemented, referenced) is not orphan; REQ-BTN-001 (implemented, no refs) is."""
    # The fixture has implemented REQ-DPI-001 (referenced), approved REQ-RGB-001 (unreferenced),
    # and implemented REQ-BTN-001 (unreferenced — this one IS the orphan).
    matrix = build_traceability_matrix(FIX / "docs" / "requirements", FIX)
    result = find_orphans(matrix)
    assert result.unreferenced_implemented == ["REQ-BTN-001"]


def test_orphans_finds_dangling():
    matrix = build_traceability_matrix(FIX / "docs" / "requirements", FIX)
    result = find_orphans(matrix)
    assert "REQ-NOSUCH-999" in result.dangling_ids


def test_orphans_finds_unreferenced_implemented_positive():
    """REQ-BTN-001 is implemented but has no code references — should be flagged."""
    matrix = build_traceability_matrix(FIX / "docs" / "requirements", FIX)
    result = find_orphans(matrix)
    assert "REQ-BTN-001" in result.unreferenced_implemented
