from pathlib import Path

from reqtool.lint import lint_directory

FIX = Path(__file__).parent / "fixtures"


def test_lint_passes_valid():
    errors = lint_directory(FIX / "valid_reqs")
    assert errors == []


def test_lint_detects_bad_status():
    errors = lint_directory(FIX / "bad_reqs")
    assert len(errors) == 1
    assert "invalid status" in errors[0]


def test_lint_detects_duplicate_ids():
    errors = lint_directory(FIX / "dup_reqs")
    assert any("duplicate id" in e for e in errors)
