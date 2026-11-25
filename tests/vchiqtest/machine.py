# SPDX-License-Identifier: BSD-3-Clause
"""
machine.py - the Amiga as the suite uses it: emu68test's Device, with the rig
run by name (uploaded to RAM:hw/bin on its first use), and what the firmware
says of the Pi's power.
"""
from __future__ import annotations

import re
from pathlib import Path

from emu68test.machine import Device, Job

from .suite import BIN, TOOLS

POWER_EVENTS = {0x10000: "under-voltage", 0x20000: "ARM frequency capped", 0x40000: "throttled",
                0x80000: "soft temperature limit"}


class VchiqDevice(Device):
    """One Amiga with vchiq.device on it."""

    install_dir: Path                   # the stack's install tree, where the rig comes from
    install_mark: int | None = None     # where the debug log was before the install's reboot; None without one

    # -- the rig

    def command(self, tool: str) -> str:
        """How @tool is invoked: a rig from RAM:hw/bin (uploaded once per boot), anything else as it is."""
        if tool in TOOLS:
            remote = f"{BIN}/{tool}"
            self.upload_once(self.install_dir / TOOLS[tool], remote)
            return remote
        return tool

    def run_text(self, tool: str, args: str = "", timeout: float = 120) -> tuple[int, str]:
        """@tool (by name) run to its end: (rc, text)."""
        return super().run_text(self.command(tool), args, timeout)

    def start(self, tool: str, args: str = "") -> Job:
        """@tool (by name) started in the background: a Job."""
        return super().start(self.command(tool), args)

    # -- the machine

    def ram_dirs(self, *more: str) -> None:
        """RAM:hw with its out and bin drawers."""
        super().ram_dirs("bin", *more)

    def power(self) -> list[str]:
        """What the firmware saw of the Pi's power and temperature since the Pi started (get_throttled's
        lasting bits)."""
        _, text = self.exec("VCHIQInfo GCMD get_throttled", timeout=15)
        word = re.search(r"throttled=(0x[0-9a-fA-F]+)", text)
        return [what for bit, what in POWER_EVENTS.items() if word and int(word.group(1), 16) & bit]
