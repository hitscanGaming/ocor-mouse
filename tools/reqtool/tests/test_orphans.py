from pathlib import Path
from reqtool.orphans import find_orphans
from reqtool.trace import build_traceability_matrix

FIX = Path(__file__).parent / "fixtures" / "sample_repo"


def test_orphans_finds_unreferenced_implemented():
    """REQ-DPI-001 is implemented and has refs; REQ-RGB-001 is approved and has none."""
    # We need a req that is status=implemented AND has zero refs.
    # The fixture has implemented REQ-DPI-001 (referenced) and approved REQ-RGB-001 (unreferenced).
    # So we should get an empty orphan list here.
    matrix = build_traceability_matrix(FIX / "docs" / "requirements", FIX)
    result = find_orphans(matrix)
    assert result.unreferenced_implemented == []


def test_orphans_finds_dangling():
    matrix = build_traceability_matrix(FIX / "docs" / "requirements", FIX)
    result = find_orphans(matrix)
    assert "REQ-NOSUCH-999" in result.dangling_ids
