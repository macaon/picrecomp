#!/usr/bin/env bash
set -euo pipefail
tools=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if ! python3 -c 'import tkinter' 2>/dev/null; then
    echo "Install Python Tk support (python3-tk on Debian/Ubuntu) to open PicRecomp Setup." >&2
    exit 1
fi
exec python3 "$tools/launcher.py"
