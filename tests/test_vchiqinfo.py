# SPDX-License-Identifier: BSD-3-Clause
"""VCHIQInfo, the user's check of the link to the VideoCore, as a product: its prose and return codes."""
import re

import pytest

from emu68test.results import expect



@pytest.mark.smoke
def test_summary(device, record_property):
    """VCHIQInfo alone: the setup in plain words, every part there, PASS (warnings allowed: they are the machine's)."""
    rc, text = device.run_text("VCHIQInfo", "")
    assert rc in (0, 5), f"return code {rc}:\n{text}"
    for regex in (r"^  VCHIQ node: ", r"^  vchiq\.device: .*connected", r"^  firmware: ", r"^  temperature: ", r"^  gpu_mem: ",
                  r"^PASS"):
        expect(text, regex, "the summary")
    assert not re.search(r"videocodec|codec driver|JPEG datatype", text), f"the video side in the link's check:\n{text}"
    for what in ("throttling", "gpu_mem", "vchiq\\.device"):
        m = re.search(rf"^  {what}: .*$", text, re.M)
        record_property(what, m.group(0).strip() if m else "no line")


def test_window_dump(device):
    """VCHIQInfo DUMP: the shared window, read only."""
    rc, text = device.run_text("VCHIQInfo", "DUMP")
    expect(text, r"^PASS$", "DUMP")
    expect(text, r"host: flags 0x", "the host side of the window")
    expect(text, r"VPU debug\[\]", "the VPU side of the window")


def test_gencmd(device):
    """VCHIQInfo GCMD with a command of two words: gencmd's answer, status 0."""
    rc, text = device.run_text("VCHIQInfo", 'GCMD "measure_clock arm"')
    expect(text, r"^PASS$", "GCMD")
    expect(text, r'"measure_clock arm" -> status 0, .*: frequency\(\d+\)=\d+$', "gencmd's answer")


def test_counters(device):
    """VCHIQInfo COUNTERS: the connection's window, session, positions and reach; no codec driver's."""
    rc, text = device.run_text("VCHIQInfo", "COUNTERS")
    assert rc == 0, f"return code {rc}:\n{text}"
    for regex in (r"^window 0x[0-9a-f]+ \(\d+ KiB\), session \d+", r"^positions: rx \d+, tx \d+", r"^reach: 0x", r"^PASS$"):
        expect(text, regex, "COUNTERS")
    assert ".codec:" not in text, text
