# SPDX-License-Identifier: BSD-3-Clause
"""
suite.py - what the suite is: its drawer on the Amiga, the rig and where the
install tree keeps it, the files it replaces on the Amiga.  The video libraries'
suites have their own part of the harness (videocodec-library's videocodectest).
"""
from __future__ import annotations

from pathlib import Path

from emu68test.plugin import Suite

COMPONENT = "emu68-vchiq-driver"       # the component the rigs and the device belong to
RAM = "RAM:hw"                          # everything temporary on the Amiga
BIN = f"{RAM}/bin"                      # the rigs, uploaded once per boot
KV_PROGRAMS = ("VCHIQTest",)            # the rigs that print the grammar and mark their runs in the debug log
BOOT_SETTLE_S = 10                      # Workbench and its startup

# where the install tree keeps the rig
TOOLS = {"VCHIQTest": "Developer/Tools/VCHIQTest"}

# the files the suite replaces on the Amiga, from the install tree
RESIDENT = [
    ("DEVS/vchiq.device", "DEVS:vchiq.device"),
    ("C/VCHIQInfo", "C:VCHIQInfo"),
    ("DEVS/AHI/videocore.audio", "DEVS:AHI/videocore.audio"),
    ("DEVS/AudioModes/VIDEOCORE", "DEVS:AudioModes/VIDEOCORE"),
]

RUN_OPTIONS = ("no_reboot", "listen", "hands", "keep_ram")


def suite(name: str, component: str, tests_dir: Path) -> Suite:
    """The suite for emu68test's plugin: @name is what it tests (the report's title), @component
    its component in the stack, @tests_dir its tests/ directory (baselines.json, runs/, .machine/)."""
    return Suite(name=name, component=component, tests_dir=tests_dir, ram=RAM, kv_programs=KV_PROGRAMS,
                 end_marks=True, boot_settle_s=BOOT_SETTLE_S, run_options=RUN_OPTIONS)
