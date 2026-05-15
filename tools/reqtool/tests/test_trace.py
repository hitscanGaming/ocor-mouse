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


def test_trace_skips_build_underscore_dirs(tmp_path):
    """build_release and build_debug should be skipped, not scanned."""
    # NOTE: ID uses REQ-XX-001 (not REQ-X-001 as in the original review snippet)
    # because the schema ID regex requires at least 2 chars in the AREA segment.
    (tmp_path / "docs" / "requirements").mkdir(parents=True)
    (tmp_path / "docs" / "requirements" / "REQ-XX-001.md").write_text(
        '---\nid: REQ-XX-001\ntitle: t\nstatus: approved\nverification: HIL\n'
        'priority: P0\nowner: o\nproducts: [hitscan]\n---\nbody\n'
    )
    (tmp_path / "build_release").mkdir()
    (tmp_path / "build_release" / "stub.c").write_text("/* REQ-XX-001 */")
    matrix = build_traceability_matrix(tmp_path / "docs" / "requirements", tmp_path)
    assert matrix["REQ-XX-001"].references == []
