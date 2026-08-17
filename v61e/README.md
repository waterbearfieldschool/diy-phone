# DIY Phone v61e — current version

A from-scratch mobile phone — SMS, voice calls, contacts, dial pad — on a
1.54" e-ink display driven with a refresh technique that makes this panel
look better than its own datasheet modes allow.

## Hardware

| Part | Model | Bus |
| --- | --- | --- |
| MCU | ProMicro nRF52840 (Nologo) | — |
| Display | Waveshare 1.54" e-Paper rev2.1 (200x200, SSD1681) | SPI (SPIM2) |
| Cellular | DFRobot Gravity SIM7600G (TEL0161) | UART @115200 |
| Keyboard | I2C keyboard @ 0x5F (CardKB-style) | I2C |
| Speaker | 8 ohm ~1W on the SIM7600 board's PH2.0 jack | — |

Display: DIN=2/P0.17, CLK=9/P1.06, CS=11/P0.10, DC=10/P0.09, RST=5/P0.24,
BUSY=16/P0.29. Modem: MCU TX=3/P0.20 -> RX, MCU RX=4/P0.22 <- TX.
Keyboard: SDA=8/P1.04, SCL=7/P0.11. All 3.3V; the SIM7600 needs its own
supply (~2A peaks), grounds common. Full map: `final_mapping.txt`.

## The display technique: all-driven partials

This panel's stock refresh modes each fail somewhere, established by
bench experiment:

- **Full refresh**: true black, but a hard inversion blink.
- **Partial refresh**: quiet, but every *undriven* black pixel relaxes
  toward a gray "equilibrium" -- crisp text visibly fades the moment any
  partial runs, and ghosts accumulate.
- **Window refresh** (`displayWindow`): grays the entire panel outside the
  window. Never used.

The escape is to make sure **no pixel is ever undriven**. The SSD1681
decides what to drive by comparing its current RAM plane (0x24) against a
"previous image" plane (0x26). So each partial refresh here writes the
frame to 0x24 and *its bitwise inverse* to 0x26 -- every pixel then
compares as changed, and the waveform drives all of them: black pushed
fully black, white actively held white. The result is a hand-synthesized
"fast full refresh": **crisp text on every update, no fading, no ghost
accumulation, no blink.** (Both planes must be written individually --
the library's `writeImageAgain` writes the inverse to *both* planes,
which displays garbage; that bug cost an afternoon.)

With all-driven partials, full refreshes are no longer needed for image
quality at all -- page changes are quiet partials too, so **the phone
never blinks during use**. The only full refreshes left are boot and a
panel-care cycle after 30 idle minutes (e-ink vendors recommend
occasional full waveform cycles for the health of the film itself; with
all-driven partials the blink has no visual after-effects).

The technique needs the frame buffer and per-plane writes, which GxEPD2
keeps private -- so the library is **vendored in `lib/GxEPD2`** (v1.6.4,
demo bitmaps removed) with two one-line access patches, both marked with
`diy-phone patch` comments. Everything else about the library is stock.

## UI

Inbox rows: `HH:MM name preview...`, newest first, one row each. The
selection is a **dot in the left gutter** (uniform position, easy to
track); unread messages carry a **rectangle at the right edge** (easy to
spot at a glance). New messages simply appear as a new row -- no banner.
Scrolling page-jumps a screenful at a time; `ESC` returns to the newest.
Reading a message shows the body at 2x text size.

| Where | Key | Action |
| --- | --- | --- |
| Inbox | `UP`/`DOWN` | move selection (page-jumps at the edges) |
| Inbox | `ENTER` | read the selected message |
| Inbox | `M` | compose (resumes a saved draft) |
| Inbox | `C` | call picker: contacts or a typed number |
| Inbox | `P` | call the selected message's sender |
| Inbox | `0-9` | free-form dial pad |
| Inbox | `D` / `R` | delete / reload |
| Inbox | `ESC` / `TAB` | jump to newest / status screen |
| Read | `UP/DN`, `D`, `R`, `P`, `ESC` | scroll, delete, reply, call, back |
| Compose | `ENTER` | next / send; `ESC` saves the draft; arrows move the caret |
| Call | `ENTER`/`ESC` | answer / reject; in-call `UP/DN` volume, `L/R` mic, `ESC`/`H` hang up |

Contacts live in `CONTACTS[]` in `main.cpp` (emilie, liz, don, mom,
mike beach); known numbers show as names everywhere. Bare 10-digit
numbers get `+1`. Incoming calls ring through the speaker (AT+CPTONE,
probed) and unanswered ones land in the missed list with a `!N` badge.

## Serial console (USB, 115200)

Local echo with backspace. `AT...` passthrough, `status`, `debug` (raw AT
tracing), `ram`, `help`. Every panel refresh logs as `[epd] ...`.

## Build

```
pio run                # build
./build_and_copy.sh    # build + bin/uf2conv/new_firmware.uf2 (auto-flashes
                       # if the UF2 bootloader drive is mounted)
```

## Lineage

v41 (TFT, refactored) -> v44 (contacts, missed calls, timezone) -> v45e
(e-ink port) -> v46e-54e (big-font line; refresh forensics) -> v55e-59e
(small-font line + calls; more forensics) -> v60e (equilibrium mode, then
the all-driven breakthrough) -> **v61e 
(cleanup: vendored lib, no access
hacks, dead refresh machinery removed)**.
