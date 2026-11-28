# emu68-vchiq-driver: traceability

Written by the suite's host tier (`run.sh host` rewrites it): every item the suite must cover, with the tests that cover it.

**Uncovered: 0** of 8 items; known gaps: 1.

## vchiq.device commands

| Item | Tests |
|---|---|
| NSCMD_DEVICEQUERY | `test_device.py::test_device_query` |
| NSCMD_VQ_GETINFO | `test_device.py::test_connection_up` |
| NSCMD_VQ_TEST_ABANDON | `test_resets.py::test_abandon_then_repair` |
| CMD_FLUSH | `test_device.py::test_flush_gives_back_the_reads` |
| CMD_READ | `test_device.py::test_audio_bulk` |
| CMD_WRITE | `test_device.py::test_audio_bulk` |
| NSCMD_VQ_BULKWRITE | `test_device.py::test_audio_bulk` |
| NSCMD_VQ_BULKREAD | **known gap:** only MMAL sends bulks to the host: videocodec-library's suite reads them (test_decoder.py::test_bit_exact), on this device |
