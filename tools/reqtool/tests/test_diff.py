from reqtool.diff import diff_matrices


def test_diff_detects_added_ref():
    before = {"REQ-A-001": {"a.c:1"}}
    after  = {"REQ-A-001": {"a.c:1", "b.c:5"}}
    d = diff_matrices(before, after)
    assert "REQ-A-001" in d.changed
    assert "b.c:5" in d.changed["REQ-A-001"].added


def test_diff_detects_removed_req():
    before = {"REQ-A-001": {"a.c:1"}, "REQ-B-001": {"b.c:1"}}
    after  = {"REQ-A-001": {"a.c:1"}}
    d = diff_matrices(before, after)
    assert "REQ-B-001" in d.removed
