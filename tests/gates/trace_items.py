# SPDX-License-Identifier: BSD-3-Clause
"""
trace_items.py - what this suite must cover (run.sh host), for emu68test's
traceability, which finds the tests that cover it and writes TRACEABILITY.md.

The items, read from the sources so a new one cannot be missed: the commands
vchiq.device's beginIO() serves.  The video libraries' items are
videocodec-library's suite's.

KNOWN_GAPS names what no test covers yet, each with its reason; a gap that a
test covers after all fails the gate until it is taken off the list.
"""
from __future__ import annotations

from pathlib import Path

from emu68test.trace import Item, case_labels

TESTS = Path(__file__).resolve().parents[1]
COMPONENT_DIR = TESTS.parent
OUTPUT = TESTS / "TRACEABILITY.md"
TITLE = "emu68-vchiq-driver"
BEGINIO = COMPONENT_DIR / "vchiq" / "src" / "device_beginio.c"

# item -> why no test covers it yet
KNOWN_GAPS: dict[str, str] = {
    "NSCMD_VQ_BULKREAD": "only MMAL sends bulks to the host: videocodec-library's suite reads them "
                         "(test_decoder.py::test_bit_exact), on this device",
}


def device_commands() -> list[str]:
    """The commands vchiq.device's beginIO() serves."""
    return list(dict.fromkeys(case_labels(BEGINIO, "void beginIO(")))


def items() -> list[Item]:
    """Every item the suite must cover."""
    found = [Item("vchiq.device commands", name) for name in device_commands()]
    for item in found:
        item.gap = KNOWN_GAPS.get(item.name, "")
    names = {item.name for item in found}
    unknown = [name for name in KNOWN_GAPS if name not in names]
    if unknown:
        raise ValueError("KNOWN_GAPS names no item: " + ", ".join(unknown))
    return found
