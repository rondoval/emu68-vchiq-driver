# SPDX-License-Identifier: BSD-3-Clause
"""vchiq.device: the connection, a bulk transfer, expunge and resume, the device's own commands (NSCMD_DEVICEQUERY,
CMD_FLUSH)."""
import pytest

from vchiqtest.tools import status, vchiqtest

STILL_FORMATS = "I420,RGB16,RGB24,BGR24,BGRA32,RGBA32"
# the ISP's own, a camera's, the column format and the formats tests/test_formats.py proves
CONVERTER_READS = "I420,NV12,RGB16,RGB24,BGR24,BGRA32,RGBA32,YUYV,UYVY,NV12_COL128,NV21,I422,YVYU,VYUY,RGBX32,BGRX32"
CONVERTER_WRITES = "I420,YV12,NV12,RGB16,RGB24,BGR24,BGRA32,RGBA32,YUYV,UYVY,NV21,I422,YVYU,VYUY,RGBX32,BGRX32"
# what the converter adds behind a decoder that writes STILL_FORMATS itself (VCCodecInfo.ChainOutputFormats)
DECODER_CHAINED = "YV12,NV12,YUYV,UYVY,NV21,I422,YVYU,VYUY,RGBX32,BGRX32"
VIDEOCORE_ROWS = 11
VCERR_BADPARAM = -4
DEVICE_COMMANDS = ["CMD_READ", "CMD_WRITE", "CMD_FLUSH", "NSCMD_DEVICEQUERY", "NSCMD_VQ_BULKWRITE", "NSCMD_VQ_BULKREAD",
                   "NSCMD_VQ_GETINFO"]
IOERR_BADLENGTH = -4
IOERR_ABORTED = -2


@pytest.mark.smoke
@pytest.mark.covers("NSCMD_VQ_GETINFO")
def test_connection_up(device, record_property):
    """VCHIQTest STATUS: the device opens, its connection busy and connected, how it came up recorded."""
    r = status(device)
    assert r.num("vchiq.flags") & 0x9 == 0x9, f"flags {r.kv['vchiq.flags']}: not busy and connected"
    record_property("bringup", r.kv.get("vchiq.bringup", "?"))
    record_property("services open", r.kv.get("vchiq.services"))


@pytest.mark.smoke
@pytest.mark.covers("CMD_WRITE", "CMD_READ", "NSCMD_VQ_BULKWRITE")
def test_audio_bulk(device, record_property):
    """VCHIQTest AUDIO MS 1000: ten chunks of silence by bulk transmit to the audio service, all consumed."""
    r = vchiqtest(device, "AUDIO MS 1000", 60)
    assert r.passed, r.kv.get("fail")
    assert r.num("run.0.chunks") == 10 and r.num("run.0.chunk_bytes") == 19200, "not ten chunks of 19200 bytes"
    assert r.num("run.0.completed") == 192000, f"{r.kv['run.0.completed']} bytes completed"
    record_property("COMPLETEs flagged", r.kv.get("run.0.flagged"))


def test_resume_after_expunge(device, driver_log):
    """Avail FLUSH expunges the device (it quiesces); its next open resumes the session."""
    device.flush()
    mark = driver_log.mark()
    r = status(device)
    assert r.kv.get("vchiq.bringup") == "resumed", f"bringup {r.kv.get('vchiq.bringup')}"
    driver_log.since(mark).expect(r"connection live, quiesced", "the device's log")


@pytest.mark.covers("NSCMD_DEVICEQUERY")
def test_device_query(device, build_info):
    """NSCMD_DEVICEQUERY: every command the device serves, in order, the test hook only in a build that has it; a result
    too short to fill is refused with IOERR_BADLENGTH."""
    r = vchiqtest(device, "DEVICE", 30)
    assert r.passed, r.kv.get("fail")
    assert r.num("devicequery.short_error") == IOERR_BADLENGTH, r.kv.get("devicequery.short_error")
    listed = [command["name"] for command in r.group("devicequery.command")]
    hooks = ["NSCMD_VQ_TEST_ABANDON"] if build_info.option("EMU68_VCHIQ_TEST_HOOKS") else []
    assert listed == DEVICE_COMMANDS + hooks, listed


@pytest.mark.covers("CMD_FLUSH")
def test_flush_gives_back_the_reads(device):
    """CMD_FLUSH with a service's reads posted: each comes back IOERR_ABORTED at once, and the service answers once
    they are posted again."""
    r = vchiqtest(device, "DEVICE", 30)
    assert r.passed, r.kv.get("fail")
    assert r.num("flush.error") == 0
    errors = [int(read["error"]) for read in r.group("flush.read")]
    assert errors == [IOERR_ABORTED] * r.num("flush.reads"), f"the reads came back with {errors}"
    assert r.flag("flush.answers_after") and r.num("flush.answer_bytes") > 0
