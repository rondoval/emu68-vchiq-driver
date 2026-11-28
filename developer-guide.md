# emu68-vchiq-driver: developer guide

For programmers using `vchiq.device`, or working on the component itself.
End-user installation is in [README.md](README.md), how it works and why in
[docs/architecture.md](docs/architecture.md), what the firmware does as measured
in [docs/firmware-facts.md](docs/firmware-facts.md).

Sections 1 and 2 are the programming interface. Sections 3 to 5 are building,
the tools and troubleshooting. Section 6 is the hardware test suite.

---

## 1. Which interface to use

| You want | Use | Header |
|---|---|---|
| a VideoCore firmware service (gencmd, audio, ...) | `vchiq.device` | `devices/vchiq.h` |
| to decode, encode or convert video and pictures | `videocodec.library`, [videocodec-library](https://github.com/rondoval/videocodec-library)'s (ships with Kinora) | `libraries/videocodec.h` |
| sound | AHI, with `videocore.audio` as the unit's driver | AHI's own |

The build installs `devices/vchiq.h` under `include/`; the stack's archive has
it in `Developer/include`, and the toolchain image (amiga-build-container)
carries it for clients built elsewhere, the video libraries among them. It
includes nothing but Exec headers.

---

## 2. vchiq.device

One `OpenDevice()` opens one VCHIQ service, named by fourcc, and waits for the
VPU's answer. Up to 16 services and any number of openers work at once; a task
shares a service by copying the opened request. Everything else is Exec I/O,
replied to the request's port. `iovq_FourCC` 0 opens no service, for the
information commands only.

| Command | Does |
|---|---|
| `CMD_READ` | the next message of the service, copied into `io_Data` (`io_Actual` its size) |
| `CMD_WRITE` | one message to the VPU (at most 4088 bytes) |
| `NSCMD_VQ_BULKWRITE` / `NSCMD_VQ_BULKREAD` | a buffer to or from the VPU by DMA, any alignment; replied when it has moved |
| `NSCMD_VQ_GETINFO` | the connection's counters and how it came up at this boot (`struct VchiqInfo`, `vi_Bringup`: fresh, resumed, repaired, re-initialised); where the VPU reaches host memory and at which bus address (`vi_ReachStart`, `vi_ReachEnd`, `vi_ReachBus`: what the `SMEM` service imports) and the VRAM, which needs no cache maintenance (`vi_UncachedStart`, `vi_UncachedEnd`) |
| `CMD_FLUSH` | every queued request of the opener's service back, `IOERR_ABORTED` |
| `NSCMD_DEVICEQUERY` | the NSD query |

A gencmd exchange:

```c
struct IOVchiqReq *io = (struct IOVchiqReq *)CreateIORequest(port, sizeof(*io));
io->iovq_FourCC = VCHIQ_FOURCC('G', 'C', 'M', 'D');
io->iovq_Version = io->iovq_VersionMin = 1;
if (OpenDevice((CONST_STRPTR)VCHIQ_NAME, 0, (struct IORequest *)io, 0) == 0)
{
    /* a copy of io with CMD_READ and a buffer, SendIO()d; then: */
    io->iovq_Req.io_Command = CMD_WRITE;
    io->iovq_Req.io_Data = "version";
    io->iovq_Req.io_Length = 8;
    DoIO((struct IORequest *)io);
    /* WaitIO() the read: the reply is in its buffer */
}
```

* **Order.** Writes and bulk transmits of one service go out in the order they
  were issued, so a message and the bulk transmit that belongs to it stay
  together. Bulk receives keep their own order and never wait behind a
  transmit: the VPU allows four bulks in flight per direction, and may have to
  finish a receive before it takes the next transmit.
* **Keep reads posted.** A service holds at most 128 unread messages, then the
  device stops reading the VPU's stream until its client reads.
* **The connection** comes up at the first open and stays up across closes and
  Amiga resets, which a client need not know about (architecture.md 3.2).
  `Avail FLUSH` gives the device's memory block back, contents intact.
* **What a client leaves on the VPU is its own.** Closing a service does not
  stop what the client set running there. A client that runs things on the VPU
  installs a reset guard after the device's: it runs first, while the link
  still works (architecture.md 3.2).

---

## 3. Building

From the stack root, inside the container as always:

```sh
./scripts/docker-build.sh --target emu68-vchiq-driver
```

or `./build.sh --build` for the whole stack. The whole component has one
version, set in its root `CMakeLists.txt`. Outputs under `install/`:

| File | What |
|---|---|
| `DEVS/vchiq.device` | the transport |
| `DEVS/AHI/videocore.audio`, `DEVS/AudioModes/VIDEOCORE` | the AHI driver and its modes |
| `C/VCHIQInfo` | the user's check |
| `Developer/Tools/VCHIQTest` | the test rig |
| `include/devices/vchiq.h` | the device's interface |

`./build.sh --upload` sends `DEVS` and `C` with the rest, not `Developer/`: the
test suite uploads the rig itself. The stack's `.lha` carries everything, and
its Install script does what README.md, Installation, says. `Developer/` stays
in the archive. The test suite has a build flavour of its own (6.2).

---

## 4. Tools

### 4.1 VCHIQInfo

The user's check of the link, installed to `C:`. One command per run, each
ending with `PASS` or `FAIL`; return codes 0 PASS, 5 PASS with warnings, 10 a
part not installed, 20 failed.

| Command | Does |
|---|---|
| (none) | the setup in plain words, each part OK or a warning or failure with its cure |
| `GCMD command` | one gencmd command through the device and its answer, as vcgencmd gives it; quote a command of several words (`GCMD "measure_clock arm"`). A missing reply dumps the shared window. |
| `COUNTERS` | the connection's state and counters, and where the VPU reaches host memory (`reach:`) |
| `DUMP` | the shared window's state, read-only, for a bug report |

Without a command it goes through Emu68's resources and the VCHIQ node (read
without the device), `cma.resource`, the device and how its connection came
up, the firmware's version, temperature, throttling and under-voltage, clocks
and `gpu_mem`. It says nothing of the video libraries: their own `VCInfo`
(videocodec-library's) checks them, and sends the user here when the
VideoCore does not answer.

gencmd's codec switches, which a firmware survey once listed, are one command
each: `VCHIQInfo GCMD "codec_enabled H264"`.

### 4.2 VCHIQTest

The rig the test suite drives (6), in the archive's `Developer/Tools`, not
installed. The first word picks the command, the rest is that command's own
ReadArgs template. `VCHIQTest ?` lists the commands and `VCHIQTest AUDIO ?`
shows a command's template; the notes at the top of
`tools/vchiqtest/vchiqtest.c` and of each command's source say what the
arguments do.

| Command | Does | Facts |
|---|---|---|
| `STATUS` | vchiq.device's connection, counters and reach, and the Pi's timer | `vchiq.*`, `timer.now` |
| `MARK text` | a line in the debug log, the capture's self-test | |
| `ABANDON` | leaves the session as a crash in mid-flight would, for the repair test; needs the test hooks (6.2) | `abandon.*` |
| `AUDIO` | silence or a tone through the firmware's audio service, driven directly; `DEST` 1 the jack, 2 HDMI0, 3 HDMI1 | `run.N.*`, `stop.N.*`, `pins.*` |
| `AHIWALK`, `AHITONE` | a channel walk or a tone through an AHI mode | `ahi.*` |
| `DEVICE` | vchiq.device's own commands on a GCMD service: `NSCMD_DEVICEQUERY`'s list, and `CMD_FLUSH` giving back the posted reads | `devicequery.*`, `flush.*` |

**Output.** The grammar of emu68-common's `<emu68test/kv.h>`: `format=1` first,
then one fact per line as `key=value` (indexed groups by number: `run.0.chunks`),
prose for people indented two spaces, and `PASS` or `FAIL` last. Every run
writes `[VCHIQTest] begin ...` and `[VCHIQTest] end rc=N` to the debug log, by
which the suite slices the log.

| Return code | Means |
|---|---|
| 0 | the command did its work and its own checks passed |
| 5 | a check failed (`fail=` says which) |
| 10 | the environment: a library, device, file or memory missing, or no test hooks |
| 20 | the device refused or failed (`error=`), nothing happened for five seconds, or the arguments made no sense (then no `format=` line comes out at all) |

---

## 5. Troubleshooting

* **`OpenDevice()` fails with `VQERR_REJECTED` (2)**: the VPU has no such
  service, or none at the versions asked for. `VCHIQInfo GCMD version` shows
  that the link itself answers.
* **`VQERR_TIMEOUT` (3) at an open or a close**, or `VCHIQInfo` says there is
  no link: the VPU does not answer. Press Ctrl-Amiga-Amiga, and if that does
  not help power off and on.
* **A service stops delivering**: its client stopped reading. Keep reads posted
  (2): at 128 unread messages the device stops reading the VPU's stream.
* **The connection does not come back after a reset**: `VCHIQInfo` says how it
  came up (`vi_Bringup`); `VCHIQInfo DUMP` shows the window for a bug report.

---

## 6. Hardware tests

The regression suite in `tests/` runs on a Linux host and drives one Amiga
through amimcp's Python client. It replaces the installed files (README,
Installation) with the install tree's and reboots, uploads the rig to
`RAM:hw`, runs the tests, captures the debug log from the serial port, and
writes a run directory.

pytest is the runner. A test is a function and its inputs come from fixtures,
so nothing depends on another test having run. Every outcome is PASS, FAIL or
SKIP with its reason; a harness defect is an ERROR, not a failure. The rig
prints facts and the host judges them (4.2).

### 6.1 Running

**What it needs:**

* on the host: the stack built in the suite's flavour (6.2) with `install/`
  and `build/` in place, and the venv below;
* on the Amiga: the stack installed by its installer, AHI with the VideoCore
  modes, `dtoverlay=cma` in config.txt;
* something on HDMI0 and the jack for the audio tests;
* the machine's serial adapter on `/dev/ttyUSB0` for the tests that read the
  debug log.

The suite builds on emu68-common's test harness (`emu68test`), which the stack
build installs to `install/share/emu68-testing/`; `tests/run.sh` finds it there
and makes `tests/.venv` on its first run from the harness's requirements (the
suite installs nothing on the host otherwise).

A run, from the component root:

```sh
tests/run.sh -m smoke                   # one of every binary and path
tests/run.sh                            # everything automatic
tests/run.sh test_audio.py -k rates     # pytest's own selection
tests/run.sh --collect-only -q          # the list of tests
tests/run.sh host                       # no Amiga: the code gates, traceability
```

The options, beside pytest's own (emu68test's plugin and `tests/vchiqtest/plugin.py`):

| Option | Does |
|---|---|
| `--server amiga-pi4\|amiga-cm4` | the machine: its amimcp entry in `~/.claude.json`, or `AMIGA_HOST` and `AMIGA_TOKEN` |
| `--amimcp path` | the amimcp checkout, by default beside the stack's |
| `--install-dir`, `--build-dir` | the stack's; the build dir's `CMakeCache.txt` says the flavour |
| `--run-dir` | where the run goes (default `tests/runs/<timestamp>`) |
| `--no-install` | check the installed files against the install tree instead of replacing them |
| (the install) | the machine's own files are copied to `tests/.machine/<server>/` before the first replacement, and never overwritten there |
| `--no-reboot` | deselect the tests that reboot |
| `--listen`, `--hands` | select those tiers (6.2) |
| `--serial /dev/ttyUSB0\|none` | the debug log's port |
| `--require-debug-log` | fail, not skip, without a capture |
| `--keep-ram` | leave `RAM:hw` on the Amiga at the end |

At the start the suite runs `VCHIQTest MARK` and looks for the mark in the log:
a port that opens but shows no mark (another machine's adapter) counts as no
capture, and the tests that read the log skip.

### 6.2 Flavours and markers

The suite's build is the debug flavour with the device's test hooks:

```sh
EMU68_CONFIGURE_ARGS="-DEMU68_DEBUG_BACKEND=pistorm -DEMU68_TIER=off \
    -DEMU68_DEBUG=emu68-vchiq-driver -DEMU68_VCHIQ_TEST_HOOKS=ON" \
    ./scripts/docker-build.sh --target emu68-vchiq-driver
```

`EMU68_VCHIQ_TEST_HOOKS` compiles `NSCMD_VQ_TEST_ABANDON` into `vchiq.device`:
the next reset leaves the session as a crash would. Any other build answers it
`IOERR_NOCMD`, and no release sets it. What the installed build enables is read
from `CMakeCache.txt`:

| Marker | Meaning |
|---|---|
| `smoke` | one of every binary and path |
| `reboot` | the test or its module reboots (`C:Reboot`); `--no-reboot` deselects them; they run last |
| `hooks` | needs `EMU68_VCHIQ_TEST_HOOKS` in the build; skipped otherwise |
| `listen` | someone at the machine says what they heard; selected only with `--listen` |
| `hands` | needs someone's hands (an external reset); selected only with `--hands` |

`--listen` runs what is judged by ear (the tones, the volume step, the channel
walks): each test plays, then asks in the terminal; `n` fails it, `s` skips it.
`--hands` runs the external resets: each test asks for Ctrl-Amiga-Amiga and
waits for the agent to go and come back, once on a clean record and once on a
record left dirty on purpose, which the device must recognise as the VPU's
restart and start afresh.

A test that reads the drivers' lines asks for the `driver_log` fixture and
skips when there is no capture or the build does not log; the first open after
the install reboot must log `[vchiq] up:` on a debug build, or the run stops.

### 6.3 What the modules cover

| Module | What |
|---|---|
| `test_vchiqinfo.py` | VCHIQInfo as a product: the summary, DUMP, GCMD, COUNTERS |
| `test_device.py` | the connection up, a bulk transfer to the audio service, resume after an expunge; the device's own NSCMD_DEVICEQUERY and CMD_FLUSH |
| `test_audio.py` | the audio service directly: rates, chunk and depth, STOP, two streams, outputs, channel counts; by ear the tone, the volume step, the channel walk |
| `test_ahi.py` | videocore.audio through AHI: the three modes, the driver's log lines, two streams, a break, the services closed; by ear the 7.1 order |
| `test_resets.py` | a reboot resumes, a reboot during sound with a sound played after it, ABANDON then repair, the external reset by hand |

A module that opens services ends with a check that every service it opened is
closed again.

The video libraries' tests, and the bulk receives they make, are
videocodec-library's suite's: `tests/TRACEABILITY.md` names
`NSCMD_VQ_BULKREAD` as covered there.

### 6.4 The run directory

`tests/runs/<timestamp>/` (or `--run-dir`) holds:

| File | What |
|---|---|
| `report.md` | every test's outcome, time and notes, the files replaced |
| `run.json` | the options and the build's flavour |
| `serial.log` | the whole capture |
| one directory per test | the rig's outputs and its slice of the debug log |

pytest's own `-ra` summary names every failure and skip.

The report's header says what the firmware saw of the Pi's power after each
module (`get_throttled`'s lasting bits): under-voltage there points at the
supply, not the code. A Pi restart clears them, so a dip that restarts the Pi
shows only as the restart (`VCHIQInfo`: the VideoCore had restarted).

Emu68 writes its own lines to the same UART. When it and a driver write at once
the bytes interleave and both lines are lost, so a few percent of the lines in
a busy run are garbled. The report says how many, and a failed log check says
when its slice was hit.

### 6.5 Timing

Where the device's time goes. Build the component at the profile tier: only the
timing lines go to the debug output.

```sh
EMU68_CONFIGURE_ARGS="-DEMU68_DEBUG_BACKEND=pistorm -DEMU68_TIER=off -DEMU68_TRACE= -DEMU68_DEBUG= -DEMU68_PROFILE=emu68-vchiq-driver" \
    ./scripts/docker-build.sh --target emu68-vchiq-driver
```

Each unit reports every 2 s while it works, and once more when it closes, as
`[vchiq] slot: n=.. sum=..us avg=..us max=..us`; emu68-common's
`scripts/perf-report.py` reduces a capture.

| Slot | What it times |
|---|---|
| `[vchiq] rx_bulk` | a receive, from queued at the VPU to done: the DMA and the device's turnaround |
| `[vchiq] rx_prep`, `rx_inval` | cache maintenance before and after a receive; none for buffers in the RTG card's VRAM |
| `[vchiq] tx_clean` | cache maintenance before a transmit |

### 6.6 Adding a test

A test is a function in the module of its area. It asks for what it needs:

| Fixture | Gives |
|---|---|
| `device` | the Amiga: `run()` for the rig, `run_text()` for VCHIQInfo, `start()` for a background job, `download()` |
| `driver_log`, `debuglog` | a slice of the log: `since(mark)`, `.expect()`, `.forbid()`, `.find()` |
| `artifacts` | the test's directory |
| `record_property` | a note for the report |
| `operator` | `ask()`, for the listen and hands tiers |

The rules:

* inputs from fixtures, never from another test's side effects;
* temporary files in `RAM:hw` only, deleted by the test; a module that opens
  services checks at its end that they are closed (`services_open()` and
  `assert_clean()` in `tests/vchiqtest/tools.py`);
* facts read from the rig's `key=value` lines, never from prose;
* verdicts as plain asserts in plain words.
