"""Use the real GUI interpreter, bypassing the venv's console redirector."""
from pathlib import Path
import runpy
import site
import sys

root = Path(__file__).resolve().parents[1]
site.addsitedir(str(root / 'companion/.venv/Lib/site-packages'))
sys.path.insert(0, str(root))
# Child workers still need the project's virtual environment.
sys.executable = str(root / 'companion/.venv/Scripts/pythonw.exe')
runpy.run_module('companion.codex_display.tray', run_name='__main__')
