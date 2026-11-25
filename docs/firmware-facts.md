# emu68-vchiq-driver: firmware facts

What the VideoCore firmware and the platform under it actually do for the link
and the sound: measured on a Pi 4 and a CM4 unless a source is named. The
firmware documents little of this and refuses almost nothing, so these are the
facts behind the rules in [architecture.md](architecture.md) section 6.
`AGENTS.md` lists what must never be done. Add a fact here when a measurement
finds one. The codecs', the ISP's and the image decoder's facts, and what each
takes of `gpu_mem`, are in videocodec-library's `docs/firmware-facts.md`.

---

## 1. Memory: `gpu_mem`

`gpu_mem` (config.txt) is the VPU's own memory, the codecs' and their pictures'.
It is unrelated to the RTG VRAM (`vc4-mem`, 0x32000000-0x34000000 on the PI4, sized
by the `videocore` overlay's `mem=`), and to the window `vchiq.device` shares with
the VPU, which comes from the `cma` zone. The firmware's own heap (16 MB) does not
move. The relocatable heap the codecs use is `gpu_mem` less 20 MB, 5 MB of it in
use idle:

| `gpu_mem` | Heap | Free idle |
|---|---|---|
| 192 | 172 MB | 167 MB |
| 88 | 68 MB | 63 MB |
| 76 (the Pi 4's default) | 56 MB | 51 MB |

Sound through `AUDS` plays at the default of 76.

---

## 2. Services

### `AUDS` (`tests/test_audio.py`)

Twenty-byte little-endian messages (`shared/include/auds_proto.h`, after userland's
`vc_vchi_audioserv_defs.h`); the firmware speaks version 3 and takes our 2.

* It answers CONTROL, CONFIG and CLOSE with a RESULT, and OPEN, START, STOP and
  WRITE with nothing.
* A WRITE with `max_packet` 0 announces one bulk transmit of the samples. The
  firmware copies them at once (the bulk is back in about 1 ms) and queues them
  itself.
* COMPLETE counts the bytes played, at the configured rate exactly (8000 to
  48000 Hz on HDMI0, HDMI1 and the jack alike, within 0.3%); bit 30 says it ran
  dry. With one 10 ms chunk in flight every COMPLETE carries it and 3 s of sound
  take 6.3 s; two in flight play without a gap.
* STOP (`draining` 0) with four chunks queued still reports three quarters of them
  played within 300 ms.
* HDMI1 with nothing attached plays as if it were there. The jack's PWM pins (GPIO
  40, 41) are already set to ALT0 on the Pi 4, so nothing needs muxing. Volume is an
  attenuation in 1/256 dB.
* CONFIG takes 1 to 8 channels on every output and they are consumed at the
  rate. Nothing sets their order (CONFIG's `channelmap` is version 3's, and
  Linux's driver never sends one), so videocore.audio sends a 7.1 frame in
  HDMI's order. On a stereo display only the first two channels are heard; the
  other six are dropped, not mixed in.

### `ril.audio_decode`

It takes the format of every encoding it lists (MP3, AAC, AC-3, DTS, Vorbis,
WMA, AMR) but refuses to start for any of them, so compressed sound is decoded
on the 68k.

---

## 3. The platform under VCHIQ

* **Doorbell.** `/soc/mailbox@7e00b840`: read BELL0 (bit 2 pending, the read
  acknowledges), write BELL2 to ring the VPU. Interrupt: GIC SPI 34. The init call
  is mailbox tag `0x00048010` with the slot area's bus address.
* **A second init tag** is answered with `0xFFFFFFFF`, and Raspberry Pi's engineers
  describe the Linux `kexec` equivalent as unsupported.
* **Emu68's memory types.** Device for MMIO, write-back for Fast RAM, write-through
  for the VideoCore windows (`vc4-mem`, `unicam-mem`, `gpu_mem`), and an unused
  non-cacheable type. On the Cortex-A72 the TRM (table 6-1) maps every write-through
  combination to the internal type Non-cacheable; the A53 does the same (section
  7.5).
* **Hardware resets.** Emu68's housekeeper core reboots the whole Pi (watchdog,
  `PM_RSTC` full reset) on any RESET it did not drive itself, so whether
  Ctrl-Amiga-Amiga restarts the VPU depends on the PiStorm firmware: the newer
  firmware watches `_RESET`, where the keyboard controller's reset arrives
  through Gayle (measured both ways).
* **Costs.** Opening and closing a service takes about 5 ms (measured on `mmal`).
