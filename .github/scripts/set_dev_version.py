#!/usr/bin/env python3
"""Pin bindings/python/pyproject.toml to a static dev version for TestPyPI.

The wheel version is normally single-sourced from the VERSION file at the repo
root via scikit-build-core's regex metadata provider (see [tool.scikit-build.
metadata.version]). That file is numeric-only, so it cannot carry a PEP 440
`.devN` suffix. For staging uploads we therefore replace the dynamic
version with a static one for the duration of the build only. This edit is
never committed.

Usage: set_dev_version.py X.Y.Z.devN [path/to/pyproject.toml]
"""
import re
import sys
from pathlib import Path

version = sys.argv[1]
path = Path(sys.argv[2] if len(sys.argv) > 2 else "bindings/python/pyproject.toml")

text = path.read_text()

# 1. dynamic = ["version"]  ->  version = "X.Y.Z.devN"
new, n = re.subn(r'^dynamic = \["version"\]\s*$',
                 f'version = "{version}"', text, count=1, flags=re.MULTILINE)
if n != 1:
    sys.exit(f"error: did not find `dynamic = [\"version\"]` in {path}")
text = new

# 2. drop the regex metadata provider block (header + its 3 keys), which is
#    invalid once the version is static.
text = re.sub(
    r'^\[tool\.scikit-build\.metadata\.version\]\n'
    r'(?:(?:provider|input|regex) = .*\n)+',
    '', text, count=1, flags=re.MULTILINE)

path.write_text(text)
print(f"pinned {path} to version {version}")
