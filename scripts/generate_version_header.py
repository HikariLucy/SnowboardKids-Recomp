#!/usr/bin/env python3
"""Generate a C header with project version and Git commit metadata."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_VERSION = "0.1.0"


def get_git_commit(cwd=ROOT) -> str:
    env_commit = os.environ.get("SBK_COMMIT")
    if env_commit:
        return env_commit.strip()[:12]
    try:
        commit = subprocess.check_output(
            ["git", "rev-parse", "--short=12", "HEAD"],
            cwd=str(cwd),
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
        if commit:
            return commit
    except (OSError, subprocess.CalledProcessError):
        pass
    return "unknown"


def generate_header(version: str, commit: str) -> str:
    return (
        "/* Auto-generated version header. Do not edit directly. */\n"
        "#ifndef SBK_VERSION_H\n"
        "#define SBK_VERSION_H\n\n"
        f'#define SBK_VERSION "{version}"\n'
        f'#define SBK_COMMIT "{commit}"\n\n'
        "#endif /* SBK_VERSION_H */\n"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="Output header path")
    parser.add_argument("--version", default=DEFAULT_VERSION, help="Project version")
    parser.add_argument("--root", type=Path, default=ROOT, help="Project repository root")
    parser.add_argument("--commit", default=None, help="Explicit commit hash override")
    args = parser.parse_args()

    commit = args.commit if args.commit is not None else get_git_commit(args.root)
    content = generate_header(args.version, commit)

    if args.out.is_file() and args.out.read_text(encoding="utf-8") == content:
        return 0

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
