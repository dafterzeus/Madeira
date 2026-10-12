#!/usr/bin/env python3
"""Keep the API-set host required by Unity IL2CPP games in the default Wine farm build."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
script = (root / "build/wine-pe/build-modules.sh").read_text()
match = re.search(r"^\[ \$# -gt 0 \] \|\| set -- (.+)$", script, re.M)
assert match, "build-modules.sh must declare its no-argument default module set"
modules = match.group(1).split()
assert "wintypes" in modules, "the default ARM64EC DLL farm build must install wintypes.dll"
assert "api-ms-win-core-winrt-robuffer-l1-1-0" in script, "document why wintypes.dll is required"
print("PASS: the default ARM64EC farm build includes wintypes.dll for Unity IL2CPP")
