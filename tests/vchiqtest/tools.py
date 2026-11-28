# SPDX-License-Identifier: BSD-3-Clause
"""
tools.py - what the tests share: the rig's runs by name and the status checks.
"""
from __future__ import annotations

from emu68test.results import Result

from .machine import VchiqDevice


def vchiqtest(device: VchiqDevice, args: str, timeout: float = 120) -> Result:
    """VCHIQTest with @args, run to its end."""
    return device.run("VCHIQTest", args, timeout)


def status(device: VchiqDevice) -> Result:
    """VCHIQTest STATUS."""
    r = vchiqtest(device, "STATUS", 30)
    assert r.passed, f"STATUS failed: {r.kv.get('fail')}"
    return r


def services_open(device: VchiqDevice) -> int:
    """The services open on vchiq.device now."""
    return int(status(device).num("vchiq.services"))


def assert_clean(r: Result, services: int) -> None:
    """Nothing left behind: the services open as @services says (a STATUS result)."""
    assert r.num("vchiq.services") == services, f"{r.num('vchiq.services')} services open, {services} before"
