#!/usr/bin/env python3
"""Every Wappo level is solvable, and its par is its optimal solution length.

The levels are generated (tools_local/wappo/gen_levels.py); its --check mode
re-solves the committed file breadth-first under the game's rules.
"""
import os
import subprocess
import sys

gen = os.path.join(os.path.dirname(__file__), "..", "..", "tools_local", "wappo", "gen_levels.py")
sys.exit(subprocess.run([sys.executable, gen, "--check"]).returncode)
