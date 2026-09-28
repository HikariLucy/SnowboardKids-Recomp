#!/usr/bin/env python3
"""Build a local SnowboardKidsGame dynamic module from a user-supplied ROM image."""

import sys
from pathlib import Path

# Add scripts directory to sys.path so module_builder is importable
ROOT_SCRIPTS = Path(__file__).resolve().parent
if str(ROOT_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(ROOT_SCRIPTS))

from module_builder.cli import main

if __name__ == "__main__":
    sys.exit(main())
