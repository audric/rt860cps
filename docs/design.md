# RT-860 CPS: design and radio format

Date: 2026-11-25. Status: approved in conversation ("ok for now, go").

## Intent

A native Linux program that does what Radtel's Windows CPS (`RT-860 Radio CPS V1.06`)
does for the Radtel RT-860: read the radio, edit channels, zones and settings, write
them back. Built like K5 Editor (`../../Quansheng/k5editor`): C++20, Qt 6, a tested core
library separate from the window, `.deb`/`.tar.gz` packages.

## Goals, non-goals

- **In:** 1024 channels, 256 zones (name and member channels), the settings groups,
  20 DTMF codes, open/save of Radtel `.88dat` files.
- **Out for now:** firmware flashing, boot logo, tuning data, GPS records, FM broadcast
  presets, VFO A/B editing, alarms/memos (`bufTime`, never sent to the radio by the CPS).
- **Must not happen:** a write that leaves the radio in a state the CPS would not have
  produced. Everything the editor does not understand is carried through unchanged.

## Approaches considered

1. **Port to C++/Qt from the decompiled CPS** (chosen). Same stack and safety model as
   K5 Editor; no Windows runtime needed.
2. Run the CPS under Wine. No work, but WinForms serial under Wine is fragile and it is
   not what was asked.
3. A CHIRP driver. Reaches more people, but CHIRP has no model for 256 zones and the
   request was for a program like K5 Editor.

## Safety model

- Before a write: read the radio, save that image as a dated backup, stop on conflicts
  (radio changed since it was read).
- Write every block of every area (see "Found on a real radio"), then end the session.
  Reading back to compare is a separate step the user starts once the radio has
  restarted.
- Bytes `CFG[0..11]` (read/write password) are always taken from the radio, never from
  the edited image or a file.

## Serial protocol (from `MainForm.RevDateOperation`)

115200 baud, 8N1, DTR asserted. All multi-byte numbers little-endian unless noted.
Checksum = sum of all preceding bytes of the packet, modulo 256.

| Step | PC sends | Radio answers |
|---|---|---|
| Connect | `34 52 05 10 9B` (retry up to 3 times, 200 ms apart) | `06`; `00` bytes are ignored |
| Read block | `52 hi lo sum` | 1028 bytes: `52 hi lo`, 1024 data, sum of the first 1027 |
| Write block | `cmd hi lo`, 1024 data, sum (1028 bytes) | `06` |
| End | `34 52 05 EE 79` | nothing |

Block numbers (1 KiB each) differ between reading and writing:

| Area | Size | Read blocks (`52`) | Write command and index |
|---|---|---|---|
| CFG (settings, VFO, DTMF) | 4 KiB | 8..11 | `90`, 0..3 |
| Channels | 48 KiB | 16..63 | `91`, 0..47 |
| Zones | 128 KiB | 112..239 | `92`, 0..127 |
| FM presets | 4 KiB | 856..859 | `99`, 0..3 |

CPS write order: connect, read block 0, read block 8 (password check, client-side),
write `90/0`, then `90/1..3`, `91/0..47`, `92/0..127`, `99/0..3`, End. The CPS always
writes everything, and so does this program, in that order and with the two reads.

## Found on a real radio (2026-11-26)

- **A write erases the whole area.** An early version sent only the changed blocks:
  the blocks it left out came back as `FF` (zones 27..256, CFG blocks 1..3). Every
  block of every area must be sent.
- **After the end command of a write** the radio shows "Backing up... Please wait",
  copies CFG to blocks 12..15, channels to 64..111 and zones to 240..367, and restarts.
  A connect command sent 1.5 s after the end command was answered with `FF`, and the
  radio then stayed on that screen until it was switched off and on. Whether the
  connect caused that or the partial write did is not known; the program no longer
  reconnects by itself.
- **Flash map** (1 KiB blocks, read with `52 hi lo sum`; 4096 blocks, then it wraps):
  0..3 tuning data with a copy in 4..7, 8..11 CFG, 12..15 CFG copy, 16..63 channels,
  64..111 channels copy, 112..239 zones, 240..367 zones copy, 856..859 FM presets.
  The CPS has no command for the tuning data.
- **Zones are named from the factory** "Zone-001" to "Zone-256", with no channels.

## Image and file format

In memory: CFG (4096) + Channels (49152) + FM (4096) + Time (32768) + Zones (131072)
= 221184 bytes. This is exactly Radtel's `.88dat` file (unencrypted, same order). The
Time area is not transferred to or from the radio.

## Channel record (48 bytes, channel n at Channels + 48 n)

| Offset | Meaning |
|---|---|
| 0 | Mode: 0 FM, 1 AM, 2 SSB |
| 1 | always 1 |
| 2 | 1 = channel in use (anything else: empty; empty channels are all `FF`) |
| 3 | Bandwidth: 0 wide, 1 narrow |
| 4-5 | RX tone |
| 6-9 | RX frequency, 10 Hz units |
| 10-13 | TX frequency, 10 Hz units |
| 14-15 | TX tone |
| 16 | Power: 0 low, 1 high |
| 17 | Busy lock: 0 off, 1 carrier, 2 CT/DCS |
| 18 | bits 0-4 TOT index, bits 5-7 tone mode (0 normal, 1-3 Enc 1-3, 4 mute code) |
| 19 | bits 0-3 scrambler (0 off, 1-8), bits 4-6 tail tone (off, 55 Hz, 120°, 180°, 240°), bit 7: 0 = in scan, 1 = skipped |
| 20-23 | mute code, 32-bit |
| 24-31 | 0 |
| 32-47 | name, GBK, padded with `FF` |

Tone: `0x1000 | tenths of Hz` CTCSS; `0x2000 | n` DCS normal; `0x3000 | n` DCS inverted,
with n = the code's digits read as octal (D023 -> 19); 0 = none.

Time lists (TOT and the settings timers): Off, 5, 10, 15, then 30..600 s in steps of 15
(43 entries). The channel TOT field has 5 bits, so only the first 32 can be stored.

## Zone record (512 bytes, zone z at Zones + 512 z)

| Offset | Meaning |
|---|---|
| 0, 1 | number of member channels (the CPS writes the low byte to both) |
| 2-3 | untouched |
| 4-19 | name, GBK, `FF` padded |
| 20.. | 200 member slots, 16-bit channel index (0-based), `FFFF` = empty |

## CFG area

`12-13` magic `CD AB`. `1024 + 48 v`: VFO A/B, same record as a channel (not edited).
`256 + 16 i` (i = 0..19): DTMF code, up to 14 characters `0-9 A-D * #`, `FF` padded,
length at +15. `2048..4095`: zero. Settings are single bytes holding a list position
unless noted; the table lives in `src/core/layout.cpp` and mirrors
`MainForm.DataToPanel` of the CPS, offsets 14..255.

## Not checked by this program

The CPS compares a password typed by the user with `CFG[0..11]` before reading or
writing. The radio itself does not ask for it. This program does not ask either and
never changes those bytes.
