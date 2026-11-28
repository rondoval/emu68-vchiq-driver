# SPDX-License-Identifier: BSD-3-Clause
"""
plugin.py - the suite's pytest plugin, loaded after emu68test's (conftest.py
shows how): its options, the tiers, and the fixtures.

Options: --no-reboot, --listen, --hands, --keep-ram, beside emu68test's.

Fixtures:
  device        the Amiga: connected, the resident files installed (checked with --no-install), RAM:hw
                ready, the rig proven to run and to reach the debug log
  operator      the person at the machine, for the listen and hands tiers
  artifacts     this test's directory in the run dir
  driver_log    the debug log, for a test that reads the drivers' lines
"""
from __future__ import annotations

from pathlib import Path

import pytest

from emu68test import plugin
from emu68test.build import BuildInfo
from emu68test.debuglog import DebugLog
from emu68test.install import Installer
from emu68test.operator import Operator
from emu68test.results import HarnessError

from .machine import VchiqDevice
from .suite import COMPONENT, RESIDENT

HOOKS_OPTION = "EMU68_VCHIQ_TEST_HOOKS"     # the stack's cache option that builds vchiq.device's test commands
LOG_WAIT_S = 3.0                            # how long a line written on the Amiga takes to reach the capture


class State:
    """What this plugin's hooks and fixtures share."""

    def __init__(self, config):
        self.vchiq_build = BuildInfo.read(Path(config.getoption("--build-dir")), COMPONENT)
        self.device: VchiqDevice | None = None
        self.power_seen: list[str] = []


def state(config) -> State:
    """The run's State."""
    if not hasattr(config, "_vchiq"):
        config._vchiq = State(config)
    return config._vchiq


def pytest_addoption(parser):
    g = parser.getgroup("vchiqtest", "the vchiq suite")
    g.addoption("--no-reboot", action="store_true", help="deselect the tests that reboot")
    g.addoption("--listen", action="store_true", help="select the tests someone at the machine judges by ear")
    g.addoption("--hands", action="store_true", help="select the tests that need the user's hands")
    g.addoption("--keep-ram", action="store_true", help="leave RAM:hw on the Amiga at the end")


def _selected(config, item) -> bool:
    """Whether the run's options select @item's tier."""
    for marker in ("listen", "hands"):
        if item.get_closest_marker(marker) and not config.getoption(f"--{marker}"):
            return False
    return not (item.get_closest_marker("reboot") and config.getoption("--no-reboot"))


def pytest_collection_modifyitems(config, items):
    """Deselect the tiers not asked for, skip what the installed build cannot do, run the reboots last."""
    kept = [item for item in items if _selected(config, item)]
    dropped = [item for item in items if not _selected(config, item)]
    hooks = state(config).vchiq_build.option(HOOKS_OPTION)
    for item in kept:
        if item.get_closest_marker("hooks") and not hooks:
            item.add_marker(pytest.mark.skip(reason=f"the installed build has no test hooks ({HOOKS_OPTION})"))
    kept.sort(key=lambda item: (bool(item.get_closest_marker("reboot")), bool(item.get_closest_marker("hands"))))
    if dropped:
        config.hook.pytest_deselected(items=dropped)
    items[:] = kept


def resident_files(install_dir: Path, report) -> dict[str, bytes]:
    """The files the suite replaces on the Amiga (remote path -> content), from the install tree; what
    the build did not make is named in the report and left alone."""
    missing = [remote for local, remote in RESIDENT if not (install_dir / local).exists()]
    if missing:
        report.header.append("Not built, not replaced: " + ", ".join(missing))
    return {remote: (install_dir / local).read_bytes() for local, remote in RESIDENT if (install_dir / local).exists()}


def _install(device: VchiqDevice, installer: Installer, files: dict[str, bytes], report, capture: DebugLog) -> None:
    """The install tree's files into place and a boot on them; the machine's own are kept first."""
    device.install_mark = capture.mark()
    try:
        placed = installer.install(files)
        if not any(file.changed for file in placed):
            device.reboot()     # already the build's: the run still starts from a boot
    except HarnessError as e:
        pytest.exit(f"the install failed: {e}", returncode=plugin.EXIT_ENVIRONMENT)
    changed = [file.remote for file in placed if file.changed]
    report.header.append("Replaced on the Amiga: " + (", ".join(changed) if changed else "nothing, the build was there"))
    report.header.append(f"The machine's own files: {installer.backup_dir}")


def _prove_rigs(device: VchiqDevice, report, capture: DebugLog, logs: bool) -> None:
    """VCHIQTest runs, its mark reaches the capture, and a debug build's device logs its first open."""
    mark = capture.mark()
    result = device.run("VCHIQTest", "MARK hello", 30)
    if not (result.passed and result.kv.get("mark") == "hello"):
        pytest.exit(f"VCHIQTest does not run on the Amiga:\n{result.raw[-400:]}", returncode=plugin.EXIT_ENVIRONMENT)
    if capture.ok and not capture.wait_for(r"\[VCHIQTest\] mark hello", mark, LOG_WAIT_S):
        capture.ok = False
        capture.why = "the log shows no mark from the Amiga: another machine's adapter, or the wrong port"
        report.header.append("Debug log: port open but no mark from this Amiga; log checks skipped")
    if capture.ok and logs and device.install_mark is not None:
        device.run("VCHIQTest", "STATUS", 30)
        if not capture.wait_for(r"\[vchiq\] up: session", device.install_mark, LOG_WAIT_S):
            pytest.exit("the installed build does not log ('[vchiq] up:' missing after the reboot): "
                        "not the debug flavour?", returncode=plugin.EXIT_ENVIRONMENT)


@pytest.fixture(scope="session")
def device(request, rundir, capture) -> VchiqDevice:
    """The Amiga: connected, the resident files installed (checked with --no-install), RAM:hw ready, the
    rig on it."""
    config = request.config
    run = plugin.session(config)
    dev = plugin.connect(request, capture, VchiqDevice)
    dev.install_dir = Path(config.getoption("--install-dir"))
    files = resident_files(dev.install_dir, run.report)
    installer = Installer(dev, Path(run.suite.tests_dir) / ".machine" / config.getoption("--server"))
    if config.getoption("--no-install"):
        differ = installer.differing(files)
        if differ:
            pytest.exit("installed files differ from the install tree (run without --no-install): " +
                        ", ".join(differ), returncode=plugin.EXIT_ENVIRONMENT)
        run.report.header.append("Installed files checked against the install tree: " + ", ".join(files))
    else:
        _install(dev, installer, files, run.report, capture)
    _prove_rigs(dev, run.report, capture, state(config).vchiq_build.debug)
    state(config).device = dev
    yield dev
    if not config.getoption("--keep-ram"):
        dev.exec(f"Delete {dev.ram} ALL QUIET", timeout=60)


@pytest.fixture(scope="session")
def operator(request) -> Operator:
    """The person at the machine, for the listen and hands tiers."""
    enabled = request.config.getoption("--listen") or request.config.getoption("--hands")
    return Operator(enabled, request.config.pluginmanager.getplugin("capturemanager"),
                    "needs someone at the machine (--listen or --hands)")


@pytest.fixture(scope="module", autouse=True)
def power_watch(request):
    """After each module that used the Amiga, what the firmware saw of the Pi's power: dips show here,
    and a restart they caused clears them."""
    yield
    found = state(request.config)
    if found.device is None:
        return
    try:
        seen = found.device.power()
    except HarnessError:
        return      # the agent stalled: the next module asks again
    if seen and seen != found.power_seen:
        found.power_seen = seen
        plugin.session(request.config).report.header.append(
            f"Power: {', '.join(seen)} since the Pi started, seen after {request.path.name}")


@pytest.fixture
def artifacts(test_dir) -> Path:
    """This test's directory in the run dir."""
    return test_dir


@pytest.fixture
def driver_log(request, debuglog) -> DebugLog:
    """The debug log, for a test that reads the drivers' lines: needs the debug flavour installed too."""
    if not state(request.config).vchiq_build.debug:
        pytest.skip("the installed build does not log (not the debug flavour)")
    return debuglog
