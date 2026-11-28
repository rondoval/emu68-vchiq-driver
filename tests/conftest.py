# SPDX-License-Identifier: BSD-3-Clause
"""
conftest.py - the suite of the vchiq library (tests/README.md): emu68-common's
harness (emu68test, from the stack's install tree) and this repository's part
of it (vchiqtest/).
"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

TESTS = Path(__file__).resolve().parent
sys.path.insert(0, str(TESTS))      # vchiqtest, from this tree

pytest.register_assert_rewrite("emu68test", "vchiqtest")    # before the first import: their asserts explain themselves

from emu68test import plugin  # noqa: E402
from vchiqtest import suite  # noqa: E402

plugin.configure(suite.suite("emu68-vchiq-driver", suite.COMPONENT, TESTS))
pytest_plugins = ["emu68test.plugin", "vchiqtest.plugin"]
