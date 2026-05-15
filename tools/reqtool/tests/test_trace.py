from pathlib import Path

from reqtool.trace import build_traceability_matrix

FIX = Path(__file__).parent / "fixtures" / "sample_repo"


def test_trace_finds_code_references():
    matrix = build_traceability_matrix(
        reqs_dir=FIX / "docs" / "requirements",
        root=FIX,
    )
    dpi = matrix["REQ-DPI-001"]
    refs = {r.path.name for r in dpi.references}
    assert "dpi.c" in refs
    assert "test_dpi.c" in refs


def test_trace_marks_unreferenced_req():
    matrix = build_traceability_matrix(
        reqs_dir=FIX / "docs" / "requirements",
        root=FIX,
    )
    rgb = matrix["REQ-RGB-001"]
    assert rgb.references == []


def test_trace_returns_dangling_refs():
    matrix = build_traceability_matrix(
        reqs_dir=FIX / "docs" / "requirements",
        root=FIX,
    )
    assert "REQ-NOSUCH-999" in matrix.dangling
