from pathlib import Path
from reqtool.checklist import build_checklist
from reqtool.trace import build_traceability_matrix

FIX = Path(__file__).parent / "fixtures" / "sample_repo"


def test_checklist_includes_hil_reqs_only():
    matrix = build_traceability_matrix(FIX / "docs" / "requirements", FIX)
    md = build_checklist(matrix, tag="v0.1.0", products=["hitscan"])
    assert "REQ-DPI-001" in md  # verification=HIL
    assert "REQ-RGB-001" not in md  # verification=code-review
    assert "v0.1.0" in md
    assert "- [ ]" in md


def test_checklist_filters_by_product():
    matrix = build_traceability_matrix(FIX / "docs" / "requirements", FIX)
    md = build_checklist(matrix, tag="v0.1.0", products=["nonexistent"])
    assert "REQ-DPI-001" not in md  # not in product 'nonexistent'
