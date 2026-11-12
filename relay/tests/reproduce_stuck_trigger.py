#!/usr/bin/env python3
"""The historical reproduction is recorded in output/stuck-trigger-audit.json.
Run the fixed sender regression; the preserved original is checked to fail too.
"""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name('test_trigger_release.py')), run_name='__main__')
