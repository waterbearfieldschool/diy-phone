# DIY Phone v60e — current version

A pocketable GSM phone: SMS inbox, composing, voice calls with ring tone,
an address book, and an interactive serial console — on an e-ink screen
whose refresh strategy was tuned, empirically, to this exact panel.

## Hardware

| Part | Model | Bus |
| --- | --- | --- |
| MCU | ProMicro nRF52840 (Nologo) | — |
| Display | Waveshare 1.54" e-Paper Module rev2.1 (200x200, SSD1681) | SPI (SPIM2) |
| Cellular | DFRobot Gravity SIM7600G (TEL0161) | UART @115200 |
| Keyboard | I2C keyboard @ 0x5F (CardKB-style) | I2C |
| Speaker | 8 ohm ~1W on the SIM7600 board's PH2.0 jack | — |

Wiring (display): DIN=2/P0.17, CLK=9/P1.06, CS=11/P0.10, DC=10/P0.09,
RST=5/P0.24, BUSY=16/P0.29. Modem: MCU TX=3/P0.20 -> SIM7600 RX,
MCU RX=4/P0.22 <- SIM7600 TX. Keyboard: SDA=8/P1.04, SCL=7/P0.11.
All 3.3V; the SIM7600 needs its own supply (~2A peaks), grounds common.
Full pin map: `final_mapping.txt`.

## The display design approach: equilibrium mode

This panel (like many SSD1681 units) has three empirically-established
behaviors that shaped everything:

1. **Full refresh is perfect but blinks.** True black, no artifacts.
2. **Full-frame partial refresh is quiet but settles the panel into a
   slightly gray "equilibrium"** — uniform and pleasant, but the drop is
   jarring *if you just saw a crisp full refresh*. Partial-after-partial
   shows no further change.
3. **Window-limited refresh (`displayWindow`) visibly grays everything
   outside the window** and desyncs later partials. It is never used.

The conclusion, after trying crisp-at-rest designs (settle refreshes,
event-driven fulls — see v54e/v59e): **don't mix the two states while
anyone is watching.** In v60e the screen simply lives at the partial
equilibrium during use — every interaction, message arrival, send, and
call transition is a quiet partial — and full refreshes are exiled to
moments nobody sees: boot, and a ghost-cleanup pass after two minutes of
keyboard idle (plus a 200-partial backstop). Uniformity beats peak
contrast; change is what the eye notices.

Supporting choices with the same motive: the selection indicator is a
small left-gutter dot and unread is a right-edge rectangle (tiny partial
diffs, no big inverted regions); new messages appear as just a new row
with its rectangle — no banner; scrolling page-jumps so most presses move
only the dot on a stable layout. Every panel refresh is logged to serial
as `[epd] ...` so refresh behavior stays observable.

## Keys

| Where | Key | Action |
| --- | --- | --- |
| Inbox | `UP`/`DOWN` | move selection (page-jumps at screen edges) |
| Inbox | `ENTER` | read (body shown at 2x text size) |
| Inbox | `M` | compose; resumes a saved draft if one exists |
| Inbox | `C` | call picker (contacts or typed number) |
| Inbox | `P` | call the selected message's sender |
| Inbox | `0-9` | free-form dial pad |
| Inbox | `D` / `R` | delete / reload |
| Inbox | `ESC` | jump back to newest message |
| Inbox | `TAB` | status screen (device info, missed calls, key test) |
| Read | `UP`/`DOWN`, `D`, `R`, `P`, `ESC` | scroll, delete, reply, call, back |
| Compose | `ENTER` | next field / send; `ESC` saves the draft |
| Call | `ENTER`/`ESC` | answer / reject; in-call: `UP/DN` volume, `L/R` mic, `ESC`/`H` hang up |

Incoming calls ring through the speaker (AT+CPTONE fallback, probed once)
and take over the screen; unanswered calls land in the missed list with a
`!N` badge. Contacts are baked into `CONTACTS[]` in `main.cpp`; known
numbers display as names everywhere. Bare 10-digit numbers get `+1`.

## Serial console (USB, 115200)

Local echo, backspace editing. Commands: `AT...` (raw modem passthrough),
`status`, `debug` (raw AT TX/RX tracing), `ram`, `help`. All display
refreshes and events are logged.

## Build

```
pio run                # build
./build_and_copy.sh    # build + bin/uf2conv/new_firmware.uf2
```

Flash: double-tap reset, copy `new_firmware.uf2` to the UF2 drive (the
build script auto-deploys if the drive is already mounted).

## Lineage

v41 (TFT phone, refactored) -> v44 (missed calls, address book, timezone)
-> v45e (e-ink port, small font) -> v46e..v54e (big-font line: layout
experiments and the refresh-artifact hunt) -> v55e_45e..v59e (return to
the small-font base + calls, dial pad, drafts) -> **v60e (equilibrium
refresh)**. The big-font line remains in the repo; its layout ideas are
portable, but its refresh lessons are already folded in here.
