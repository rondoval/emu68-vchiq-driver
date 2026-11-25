# emu68-vchiq-driver

> **Releases:** this component ships as part of the
> [emu68-driver-stack](https://github.com/rondoval/emu68-driver-stack): the downloadable
> `.lha` and bundled documentation are published there. This repository is source-only
> and versioned via git tags.

The link to the Raspberry Pi's VideoCore for AmigaOS on a PiStorm with a
Raspberry Pi 4 or CM4, and sound through it. `vchiq.device` carries the
messages between the Amiga and the VideoCore's firmware; `videocore.audio`, an
AHI driver, plays every AHI program to HDMI or the headphone jack through it.

The video libraries that used to come with it (`videocodec.library`,
`vcjpeg.datatype`, `VCGrab`, `VCInfo`) are
[videocodec-library](https://github.com/rondoval/videocodec-library)'s now, and
ship with [Kinora](https://github.com/rondoval/kinora). They need the
`vchiq.device` from here.

> **Experimental.** It has run on real hardware in a scripted test cycle, but
> expect rough edges: see [Known limitations](#known-limitations).

---

## What you get

- **The link to the VideoCore.** `vchiq.device` is what the video libraries and
  `videocore.audio` talk through. It survives Amiga resets, so a `C:Reboot`
  does not cost the connection.
- **Sound through the Pi.** `videocore.audio`, an AHI driver, plays every AHI
  program to HDMI or the headphone jack.
- **`VCHIQInfo`** checks the link and says what to fix.

---

## Requirements

- A **Raspberry Pi 4 or CM4** on a PiStorm. (Pi 3 support is written but
  untested.)
- **Emu68 newer than 1.1.0-beta.1**: the tagged releases do not have the `cma`
  overlay yet.
- **AmigaOS 3.x.**
- **AHI 4 or 6** for sound.

And this line in `config.txt` on the Pi's boot partition:

| Line | What it is for |
|---|---|
| `dtoverlay=cma` | The memory shared with the VideoCore. `vchiq.device` keeps one 336 KiB block there, and Emu68 makes the zone in whole 2 MiB, so `dtoverlay=cma,size=2` is enough and the default 4 MiB is plenty. The zone, and a 2 MiB gap beside it, come out of the Fast RAM below 1 GiB. |

Sound needs nothing else; what video needs on top (`gpu_mem`, the `videocore`
overlay) is in videocodec-library's README. Emu68 reads `config.txt` only when
the Pi starts, so press Ctrl-Amiga-Amiga or power-cycle after changing it.

---

## Installation

Tick what you want in the stack's Install script: `vchiq.device` for the link
alone (the video libraries need it), `videocore.audio` for sound, which brings
`vchiq.device` along. Take the `-rangeops` archive: an Emu68 new enough for
this has the dcache extensions.

| Tick | File | Installed to |
|---|---|---|
| `vchiq.device` | `vchiq.device` | `DEVS:` |
| | `VCHIQInfo` | `C:` |
| `videocore.audio` | `videocore.audio` | `DEVS:AHI/` |
| | `VIDEOCORE` | `DEVS:AudioModes/` |

To install by hand, copy the files as above. Then reboot, and check the setup:

```
VCHIQInfo
```

It checks each part in turn (Emu68, the memory shared with the VideoCore,
`vchiq.device`, the firmware, the Pi's temperature, power and clocks), says what
to fix, and ends with `PASS`.

An older `C:VCInfo` from an earlier archive stays where it is; Kinora's Install
script replaces it with the video libraries' own check of that name.

---

## Sound

In the AHI preferences, pick a VideoCore mode for a unit and choose its
output.

| Mode | |
|---|---|
| `VideoCore:16 bit stereo++` | The everyday mode. |
| `VideoCore:HiFi 16 bit stereo++` | Better with many channels, a little more work for the 68k. |
| `VideoCore:HiFi 16 bit 7.1 multichannel` | Eight channels for an AV receiver on HDMI. Needs AHI 6 and a program that plays 7.1. A stereo display plays only the front pair. |

| Output | Plays to |
|---|---|
| `HDMI0` (the default) | the HDMI port next to the power socket on a Pi 4 |
| `HDMI1` | the second HDMI port |
| `Headphone jack` | the headphone jack (on a CM4 it depends on the carrier board) |

All modes take 8000 to 48000 Hz, and several programs can play at once. The
unit's output volume in the AHI preferences sets the VideoCore's.

---

## Known limitations

- Sound latency is about 40 ms, and recording is not supported.
- If the VideoCore stops answering, press Ctrl-Amiga-Amiga. That restarts the
  Pi and the VideoCore with it; `C:Reboot` restarts only the 68k.
- Not tried yet: the headphone jack and the 7.1 channel order by ear.

---

## Troubleshooting

| Symptom | Cause and cure |
|---|---|
| `VCHIQInfo` says `cma.resource missing` | Add `dtoverlay=cma` to `config.txt`. |
| `VCHIQInfo` says `vchiq.device` did not load | Copy it to `DEVS:`: tick `vchiq.device` in the Install script. |
| The VideoCore connection does not come back after a reboot, or after `Avail FLUSH` | Press Ctrl-Amiga-Amiga. If it still does not come back, power off and on. The firmware starts its side of the connection only once per Pi start, and it cannot always be taken over. |
| `VCHIQInfo` warns of the supply voltage, the temperature or held-back clocks | Use a stronger power supply, or give the Pi better cooling. |
| The VideoCore modes are missing from the AHI preferences | Reboot, or run `AddAudioModes REFRESH`. |

---

## For developers

The archive's `Developer/` drawer holds `devices/vchiq.h`, the device's
interface, and the test rig (`Tools/VCHIQTest`). None of it is installed.

In the source repository, [developer-guide.md](developer-guide.md) covers the
device's interface, the tools, building and the hardware tests, and
[docs/architecture.md](docs/architecture.md) explains how it works and why.

## License

BSD-3-Clause (see `LICENSE`). The transport follows the Linux VCHIQ core
(GPL-2.0 OR BSD-3-Clause, Broadcom) and the audio service Broadcom's
BSD-3-Clause `userland`.
