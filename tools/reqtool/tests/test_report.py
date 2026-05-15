from pathlib import Path
from reqtool.report import build_report
from reqtool.trace import build_traceability_matrix

FIX = Path(__file__).parent / "fixtures" / "sample_repo"


def test_report_has_header_and_counts():
    matrix = build_traceability_matrix(FIX / "docs" / "requirements", FIX)
    md = build_report(matrix, tag="v0.1.0")
    assert "# Traceability Report — v0.1.0" in md
    assert "## Summary" in md
    assert "## Per-requirement" in md
    assert "REQ-DPI-001" in md
