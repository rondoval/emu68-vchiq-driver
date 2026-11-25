# SPDX-License-Identifier: BSD-3-Clause
"""Resets with the device up, with a sound playing, and the repair of a session left as a crash would."""
import time

import pytest

from emu68test.results import expect
from vchiqtest.tools import status, vchiqtest
from test_ahi import MODE_16BIT

PRESS_WAIT_S = 1800  # someone has to come to the machine: longer than a test's limit, so these have their own


@pytest.mark.reboot
def test_reboot_resumes(device, driver_log):
    """C:Reboot with the device up: the reset guard quiesces, the next open resumes the session."""
    status(device)
    mark = driver_log.mark()
    device.reboot()
    r = status(device)
    assert r.kv.get("vchiq.bringup") == "resumed", f"bringup {r.kv.get('vchiq.bringup')}"
    driver_log.since(mark).expect(r"connection live, quiesced", "the device's log")


def after_a_reset(device, driver_log):
    """C:Reboot with the VideoCore at work: the session comes back and a sound plays on it. The bring-up it found."""
    mark = driver_log.mark()
    device.reboot()
    bringup = status(device).kv.get("vchiq.bringup")
    assert bringup in ("resumed", "repaired"), f"bringup {bringup}: {driver_log.since(mark).text[-800:]}"
    audio = vchiqtest(device, "AUDIO MS 500", 60)
    assert audio.passed, f"the sound after the reset: {audio.kv.get('fail')}"
    return bringup


def external_reset(device, operator):
    """Ctrl-Amiga-Amiga by hand, and the machine back."""
    operator.say(f"press Ctrl-Amiga-Amiga on the Amiga now (waiting up to {PRESS_WAIT_S // 60} minutes)")
    assert device.wait_reset(PRESS_WAIT_S), f"no reset seen in {PRESS_WAIT_S // 60} minutes"


@pytest.mark.reboot
def test_reboot_during_sound(device, driver_log, record_property):
    """C:Reboot with AHI playing through videocore.audio: the session comes back, and a sound plays on it."""
    tone = device.start("VCHIQTest", f"AHITONE MODE {MODE_16BIT} MS 20000")
    time.sleep(3)
    assert device.procs("VCHIQTest"), f"the tone did not start:\n{tone.output()}"
    record_property("bringup", after_a_reset(device, driver_log))


@pytest.mark.reboot
@pytest.mark.hooks
@pytest.mark.covers("NSCMD_VQ_TEST_ABANDON")
def test_abandon_then_repair(device, driver_log):
    """A session left as a crash would (ABANDON, then C:Reboot): the first open after the reset repairs it, and the
    repaired connection carries a service with bulk transfers (the audio service); a reboot leaves it resumed."""
    left = vchiqtest(device, "ABANDON", 30)
    assert left.passed and left.flag("abandon.armed"), left.kv.get("fail")
    mark = driver_log.mark()
    device.reboot()
    try:
        r = status(device)
        assert r.kv.get("vchiq.bringup") == "repaired", f"bringup {r.kv.get('vchiq.bringup')}"
        rc, text = device.run_text("VCHIQInfo", "GCMD version")
        expect(text, r"^PASS$", "gencmd after the repair")
        audio = vchiqtest(device, "AUDIO MS 500", 60)
        assert audio.passed, f"the audio service on the repaired connection: {audio.kv.get('fail')} (a VPU that stops answering " \
                             f"needs a power cycle)"
        log = driver_log.since(mark)
        for regex in (r"reset: left as a crash would", r"connection live, dirty", r"repair of the previous session: ok"):
            log.expect(regex, "the device's log")
    finally:
        device.reboot()  # the repaired session is not left behind for the next run: resumed, as every run starts


@pytest.mark.hands
@pytest.mark.timeout(PRESS_WAIT_S + 300)
@pytest.mark.reboot
@pytest.mark.hooks
def test_abandon_then_external_reset(device, operator, driver_log):
    """A session left as a crash would, then Ctrl-Amiga-Amiga, which restarts the Pi while DRAM keeps the record: the
    device finds a dirty record the VPU has forgotten, and starts afresh instead of failing until the record decays."""
    left = vchiqtest(device, "ABANDON", 30)
    assert left.passed and left.flag("abandon.armed"), left.kv.get("fail")
    mark = driver_log.mark()
    external_reset(device, operator)
    r = status(device)
    assert r.kv.get("vchiq.bringup") in ("reinit", "repaired"), f"bringup {r.kv.get('vchiq.bringup')}"
    audio = vchiqtest(device, "AUDIO MS 500", 60)
    assert audio.passed, f"the audio service after the reset: {audio.kv.get('fail')}"
    driver_log.since(mark).expect(r"re-initialised|repair of the previous session: ok", "the device's log")


@pytest.mark.hands
@pytest.mark.timeout(PRESS_WAIT_S + 300)
@pytest.mark.reboot
def test_external_reset(device, operator, capture, record_property):
    """Ctrl-Amiga-Amiga by hand: the device comes back, resumed or started afresh."""
    mark = capture.mark()
    external_reset(device, operator)
    rc, text = device.run_text("VCHIQInfo", "GCMD version")
    expect(text, r"^PASS$", "gencmd after the reset")
    if capture.ok:
        log = capture.since(mark)
        path = ("the Pi restarted, the record re-initialised" if log.find(r"record was stale|connection fresh") else
                "resumed" if log.find(r"connection live, quiesced") else "unknown")
        record_property("after the reset", path)
