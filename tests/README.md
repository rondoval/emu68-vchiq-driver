# The hardware regression suite

pytest tests that drive one Amiga through amimcp, with the rig `VCHIQTest` on
the Amiga printing facts and the host judging them. The full account is
developer-guide.md section 6; this is the short form. The video libraries'
suite is videocodec-library's.

## Once

    ./scripts/docker-build.sh            # from the stack root: the build, and the harness it installs

The suite builds on emu68-common's test harness (`emu68test`: the machine,
the install, the debug log, the grammar, the report), which the stack build
installs to `install/share/emu68-testing/`. `tests/run.sh` finds it there
(`$EMU68_TESTING` names another place) and makes `tests/.venv` on its first
run from the harness's requirements. What is this suite's own is the package
`vchiqtest/`.

[amimcp](https://github.com/thomas-luebker/amimcp) is checked out beside the
stack, or named with `--amimcp`. The Amiga's address and token come from its
entry in `~/.claude.json` (`--server amiga-pi4` by default), or from
`AMIGA_HOST` and `AMIGA_TOKEN`.
The stack must be built (debug flavour, test hooks on) with its `install/`
and `build/` in place: the suite replaces `DEVS:vchiq.device`, `C:VCHIQInfo`,
`DEVS:AHI/videocore.audio` and `DEVS:AudioModes/VIDEOCORE` from the install
tree and reboots; nothing else on the Amiga changes, and everything temporary
goes to `RAM:hw`. Before a file is first replaced, the machine's own goes to
`tests/.machine/<server>/` and stays there.

## Running

    tests/run.sh -m smoke            # one of every binary and path
    tests/run.sh                     # everything automatic
    tests/run.sh --collect-only -q
    tests/run.sh host                # no Amiga: the code gates, traceability

Options: `--server`, `--install-dir`, `--build-dir`, `--run-dir`,
`--no-install`, `--no-reboot`, `--listen`, `--hands`, `--serial DEV|none`,
`--require-debug-log`, `--keep-ram`; `tests/run.sh --help` lists them under
"the stack's common test harness" and "the vchiq suite".

Markers: `smoke`, `reboot` (run last; `--no-reboot` drops them), `hooks`
(skipped unless the build has the test hooks), `listen` and `hands` (only with
`--listen`, `--hands`: someone at the machine answers in the terminal).

## The host tier

`tests/run.sh host` needs no Amiga and runs in seconds: the gates (`gates/`):

- **Kernel-doc:** every function of the component's C has its comment, with
  each parameter and the return value.
- **Complexity:** a function's cyclomatic complexity is 15 at most (the
  harness's default is 10; `gates/test_gates.py` sets this suite's).
  `complexity-allow.txt` lists the functions that were over when the gate came
  (0.6), each at the complexity it had; the gate fails on a ceiling higher than
  its function needs, so the list only goes down. A new entry may be 20 at
  most, with a reason.
- **Traceability:** `TRACEABILITY.md` is rewritten on every run. Its items are
  the commands `vchiq.device` serves, read from its source. A test says what it
  covers with `@pytest.mark.covers("CMD_FLUSH")`. An item no test covers fails
  the gate, unless `gates/trace_items.py` names it a known gap with the reason
  (`NSCMD_VQ_BULKREAD`: only MMAL sends bulks to the host, and the video
  libraries' suite reads them); a known gap that a test covers after all fails
  too, until it is taken off that list.

## What comes out

`tests/runs/<timestamp>/`: `report.md`, `run.json`, `serial.log`, and one
directory per test with the rig's outputs and that test's slice of the debug
log. `-ra` on the command line (the default) lists every failure and skip with
its reason.

## The grammar

`VCHIQTest` prints the harness's grammar (`Emu68Common::kv`; emu68-common's
`testing/README.md` describes it, with the return codes): `format=1`, the
`command`, `key=value` facts (groups by number: `run.0.chunks`), prose indented
two spaces, and `PASS` or `FAIL` last. The keys are in the rig's sources
(`tools/vchiqtest/`) and developer-guide section 4.2.

## Layout

    run.sh             the entry point: the harness on the path, the venv
    conftest.py        says which suite this is and loads the two plugins
    vchiqtest/         this suite's part of the harness:
      suite.py           the rig, the files replaced, RAM:hw
      machine.py         the device: the rig by name, the Pi's power
      plugin.py          the options, the tiers, the fixtures (device, operator, driver_log)
      tools.py           what the tests share: STATUS, the services open
    test_*.py          the tests, one module per area
    gates/             the code gates and the traceability items
    complexity-allow.txt, TRACEABILITY.md (written by the host tier)
