#!/usr/bin/env python3
"""Produce a release manifest.json from a directory of artifacts."""
import argparse
import hashlib
import json
import sys
from pathlib import Path
from datetime import datetime, timezone


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", required=True, help="Release tag, e.g. v1.0.0")
    ap.add_argument("--git-sha", required=True)
    ap.add_argument("--protocol-version", required=True, type=int)
    ap.add_argument("--artifacts-dir", required=True)
    ap.add_argument("--output", required=True)
    args = ap.parse_args()

    art_dir = Path(args.artifacts_dir)
    artifacts: dict[str, dict] = {}
    for f in sorted(art_dir.rglob("*")):
        if not f.is_file():
            continue
        rel = f.relative_to(art_dir).as_posix()
        artifacts[rel] = {
            "sha256": sha256(f),
            "size": f.stat().st_size,
            "signed_with": "dev-key" if rel.endswith("-update.bin") else None,
        }

    manifest = {
        "release": args.release,
        "git_sha": args.git_sha,
        "build_time": datetime.now(timezone.utc).isoformat(),
        "protocol_version": args.protocol_version,
        "artifacts": artifacts,
    }

    Path(args.output).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {args.output}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
