# emu68-vchiq-driver

Lets 68k code reach the VideoCore VPU firmware, and plays sound through it. The
stack-wide rules in the superbuild's `AGENTS.md` apply (container builds only,
`__NOLIBBASE__` with a local `SysBase` in freestanding code, C99 declarations at
first use, zero warnings, the user owns git).

| Binary | Source | Installed in | What it is |
|---|---|---|---|
| `vchiq.device` | `vchiq/` | `DEVS:` | the VCHIQ transport, one unit per service |
| `videocore.audio` | `ahi/` | `DEVS:AHI/`, mode file `DEVS:AudioModes/VIDEOCORE` | AHI sub-driver on the firmware's `AUDS` service |
| `VCHIQInfo` | `tools/VCHIQInfo.c` | `C:` | the user's check of the link |
| `VCHIQTest` | `tools/vchiqtest/` | `Developer/Tools` (archive only) | the suite's rig |

The video libraries (`videocodec.library`, `videocore.codec`, `vcjpeg.datatype`,
VCGrab, VCInfo, their rigs and suite) are videocodec-library's since 0.7, built
and shipped by Kinora. `videocore.codec` is a client of `vchiq.device` like any
program: nothing here may depend on it.

## Docs

Each document has one reader; put new text where its reader looks.

| File | Reader | Holds |
|---|---|---|
| `docs/architecture.md` | whoever changes the design | how it works and why, and the firmware's limits as design rules. Read it before touching protocol code. |
| `docs/firmware-facts.md` | whoever touches the firmware path | what the firmware does, as measured, and the sources. New measurements go here. |
| `developer-guide.md` | people using or working on the component | the device's interface, the tools, building, the test suite |
| `README.md`, `RELEASE-NOTES.md` | end users | installing and using it; the current release only |
| this file | agents | what must not break; no history, no how-it-works |

## Build and validate

From the stack root, `./scripts/docker-build.sh --target emu68-vchiq-driver`
or `./build.sh --build`. Zero warnings.

From the component root:

- After any change to C or to the tests: `tests/run.sh host` (no Amiga, seconds): the code gates over all of the component's C (kernel-doc on every function, cyclomatic complexity at most 15; `tests/complexity-allow.txt` holds the 0.6 code's exceptions at a ceiling that only goes down, a new entry 20 at most with a reason) and traceability (`tests/TRACEABILITY.md`, rewritten each run: every command `vchiq.device` serves with the tests that cover it, or as a known gap in `tests/gates/trace_items.py`). A new function needs its kernel-doc, a new device command a test that covers it. The suite builds on
  emu68-common's harness (`emu68test`, from the stack's install tree): shared test code
  belongs there, this suite's part in `tests/vchiqtest/`.
- On the hardware: `tests/run.sh -m smoke` after a change, the
  full suite before a release, on the suite's build flavour (developer-guide
  6.2). The machines are shared: the user runs these or says when to. Stop your
  own serial capture and anything polling the Amiga's agent first; the suite
  needs both to itself.
- Report the installed files a run replaces (`DEVS:vchiq.device`, `C:VCHIQInfo`,
  `DEVS:AHI/videocore.audio`, `DEVS:AudioModes/VIDEOCORE`).

## Rules

1. **BSD-3-Clause.** GPL code is forbidden.
2. **Layering.** `ahi/` and the tools include only `devices/vchiq.h` of `vchiq/`
   (VCHIQInfo's window dump and VCHIQTest's `vchiq/vchiq_test.h` excepted).
   `devices/vchiq.h` includes nothing but Exec headers: the toolchain image ships
   it to the device's clients.
3. **The device's interface is append-only.** New commands get new numbers;
   `struct VchiqInfo` grows at its end, and a reader trusts `io_Actual`.
4. **One version for the component**, set in the root `CMakeLists.txt`; each
   binary takes `${CMAKE_PROJECT_VERSION}`. No source or header fallback carries
   a number. Protocol versions (VCHIQ, AUDS) are separate.
5. **Functions carry kernel-doc comments** (summary, every parameter,
   `Return:`). Documents stay ASCII.
6. **The rig prints facts, the host judges them.** `tools/vchiqtest/` speaks the
   grammar of emu68-common's `<emu68test/kv.h>`; a test never greps prose.
7. **Test hooks only in test builds.** `vchiq/vchiq_test.h` commands exist only
   with `VCHIQ_TEST_HOOKS` (the stack's `EMU68_VCHIQ_TEST_HOOKS`, which the
   suite's build sets and no release does).
8. **Tests are pytest functions in `tests/`** (developer-guide 6.6): inputs from
   fixtures, never from another test's side effects; temporary files in
   `RAM:hw` only; a module that opens services ends in a check that it closed
   them all (the services open, against a fixture taken at its start).

## The firmware: what never to do

- Never send a second init tag: the VPU answers `0xFFFFFFFF` and never takes it
  (architecture.md section 6). The connection is persistent for that reason.

## Invariants

What a change must not break, per binary. The reasons are in
`docs/architecture.md`, in the sections named.

### vchiq.device (sections 3, 4)

- Every window access goes through `le32()`/`le16()` on `volatile` pointers. No
  cache maintenance on the slot area; `cache_pre_dma()`/`cache_post_dma()` on
  bulk payloads and on imported buffers only.
- vchiq must stay the first `cma.resource` client after a reset: otherwise the
  window moves while the VPU still writes the old one.
- The window's record outlives a VPU restart, so `vchiq_connect()` trusts it
  only after the VPU has answered on it (`probe_alive()`). The session API is
  `vchiq_connect()` and `vchiq_shutdown()`; the steps stay private to
  `vchiq_session.c`.
- Every ring access is under `base->lock` and inside `vchiq_ring_enter()` /
  `vchiq_ring_leave()`. The reset path cannot lock: it reads the busy count.
- The reset guard cannot `Wait()`: it clears `ctx->timer` and spins.
- A client's reset guard, installed after the device's, runs before it: never
  install the device's guard so that it runs first. videocore.codec stops its
  components in its own guard while the link still works; a service's close
  leaves them on the VPU, and the next `SMEM` open then hangs the link.
- `BeginIO` only queues on the unit and kicks the task; only the task's round
  touches the ring. The device never calls into a client or waits for one:
  replies are `ReplyMsg()`s.
- Never merge `sendq` (writes and bulk transmits) and `bulkreadq` (bulk
  receives), and keep each in order: a message and its bulk must never be
  reordered.
- OpenDevice and CloseDevice wait through `drv_wait_event()` with the lock
  released, on a signal bit of their own, never `SIGF_SINGLE`. A close never
  gives up while the VPU makes progress.
- SoC differences live in `struct vchiq_platform` only.

### videocore.audio (section 5)

- The slave task owns every request and buffer. `AHIsub_Stop()` returns only
  once every chunk is home: no hook runs and nothing is freed while the VPU can
  still read it.
- `AHIsub_Update()` and `AHIsub_HardwareControl()` stay interrupt-safe: store
  and `Signal()`, nothing else.
- `AHIsub_Disable()`/`Enable()` are asm stubs that preserve every register.
  `lib_Version` stays 4, so AHI 4 opens the driver.
- Programs never hang: a failed service makes the stream dead, and the timer
  goes on pacing the hooks.

## Where things are

Every source file opens with what it does: `head -3` before reading it whole.
Headers live in each binary's `include/`, sources in its `src/`. A binary's
public headers reach the others only through a copy in the build tree
(`public-include/`).

| Path | What |
|---|---|
| `vchiq/include/devices/vchiq.h` | the device's public interface |
| `vchiq/include/vchiq/` | the wire format (`vchiq_protocol.h`, size-asserted), the core's interfaces, the test-only commands (`vchiq_test.h`) |
| `vchiq/src/vchiq_{session,rx,tx,service,bulk}.c` | the transport core, lock-free itself: the caller serialises |
| `vchiq/src/` | `device*.c`, `unit.c`, `unit_task.c` (the one task), `irq.c` (the doorbell server), `vchiq_link.c` (up, down, the reset quiesce) |
| `ahi/` | `audio_lib.c`, `audio_sub.c` (the `AHIsub_*` calls), `audio_slave.c`; `modefile.py` writes the mode file in the build |
| `shared/include/auds_proto.h` | the AUDS wire format, for `videocore.audio` and VCHIQTest |
| `tools/` | `VCHIQInfo.c`; `vchiqtest/` one file per command; `common/` the tools' shared code (`vchiq_client.c` a service as Exec I/O, `debugport.c`, `waiter.c`, `args.h`) |
| `tests/` | `run.sh` the entry point, `conftest.py`, `vchiqtest/` the suite's fixtures and helpers, `test_*.py` one module per area; `.venv/`, `.machine/` and `runs/` are not checked in |
