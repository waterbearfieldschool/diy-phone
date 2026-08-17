# DIY Phone v49e

v45e with the body text at ~1.5x size: FreeMono 9pt (11x18 cells, 17
columns) for all content, while the chrome (status bar, footer, toasts,
boot checklist) stays at the small 6px font. The selected message is always featured at the top of the inbox with its
full text: an inverted bold name|time header bar, then up to 4 wrapped
lines (`...` if longer). The messages after it follow as flowing two-row
`name|time|preview` entries. Scrolling down just changes which message is
featured; a small `^N` at the right of the header bar counts the messages
above the selection. The name is bold (FreeMonoBold shares FreeMono's cell
width, so weights mix on the grid); time and preview are bold too while the
message is unread. Selection inverts the whole two-line entry.

E-ink specifics: drawing goes into a RAM buffer and flush() pushes it --
quick partial refreshes normally, a full (blinking) refresh every ~24
updates to clear ghosting. The status bar repaints only when a number on it
changes. Color collapses to ink-on-paper: the selection bar inverts, unread
is a `*`, alerts are `!`, and toasts are an inverted strip.

## Wiring (e-ink module)

| Module wire | Pro Micro pin | nRF GPIO |
| --- | --- | --- |
| DIN (MOSI) | 2 | P0.17 |
| CLK | 9 | P1.06 |
| CS | 11 | P0.10 |
| DC | 10 | P0.09 |
| RST | 5 | P0.24 |
| BUSY | 16 | P0.29 |
| VCC | 3.3V | |
| GND | GND | |

Same pins as the TFT, plus BUSY. The panel class is `GxEPD2_154_D67` (the
V2 module); a V1 panel needs `GxEPD2_154` in main.cpp instead. Boot runs a visible checklist
(keyboard, modem, SIM, network) and then drops straight into the inbox. Debug
output goes only to USB serial; the screen is all phone.

Carried over: `Config.h` (pins), `Log`, `Modem` from v42, `Timestamp` from
v41 (for sorting the inbox newest-first).

```
pio run                # build
./build_and_copy.sh    # build + produce bin/uf2conv/new_firmware.uf2
```

## Chrome (every screen)

- **Status bar** (top): signal bars, `*N` unread count in cyan, `!N` missed
  calls in red, screen title, and SIM storage `used/total` -- dim normally, orange at 80% full, red with
  `!` when full (a full SIM silently rejects incoming texts).
- **Footer** (bottom): the keys that work on the current screen.
- **Toasts**: the status bar becomes a green banner for ~4s when a message
  arrives ("New message from +1617..."), when a send completes, or red when
  a call rings in.

## Screens

| Screen | Keys |
| --- | --- |
| Inbox (home) | `UP`/`DOWN` select, `ENTER` read, `C` compose, `D` delete, `R` reload, `TAB` status |
| Read | `UP`/`DOWN` scroll, `D` delete, `R` reply, `ESC` back |
| Compose (To) | type a name to filter contacts or a number; `TAB`/`UP`/`DOWN` pick a contact; `ENTER` next; `ESC` cancels |
| Compose (body) | type body (`n/160` counter), `ENTER` sends, `BACKSPACE` edits, `ESC` cancels |
| Status | device/network info, missed-call list, keyboard echo test; `ESC` back |

Inbox rows: `* 14:22 +16175551234  preview...` -- unread messages are bright
with a cyan `*`, read ones dim. Newest first. Opening a message marks it read
(AT+CMGR does that on the SIM itself). A text arriving mid-compose doesn't
interrupt typing; the inbox refreshes when you leave compose.

Boot notes: the keyboard is required (the UI is unusable without it), so boot
waits for it, reprobing every second. The modem gets 30 seconds, then the UI
starts anyway and keeps retrying in the background -- "modem online" toasts
when it appears.

## Address book

Contacts are baked into `main.cpp` (`CONTACTS[]`): emilie and liz. Anywhere a
known number appears -- inbox, read view, toasts, missed calls -- the name is
shown instead. The compose To screen lists contacts under the input field;
typing letters filters by name, typing digits means a manual number. A bare
10-digit manual number is assumed US and gets `+1`.

## Missed calls

The SIM7600 announces an unanswered call with a `MISSED_CALL: <time> <number>`
notification. v44 records the last 8 in RAM (they aren't stored on the SIM, so
they reset on reboot), shows a red toast when one happens, keeps a red `!N`
badge in the status bar, and lists them on the Status screen -- new ones in
orange, already-seen ones dim. Opening the Status screen marks them seen and
clears the badge.

## Serial commands (115200 baud)

| Command | Action |
| --- | --- |
| `AT...` | Send any AT command to the modem, print the reply |
| `status` | Modem health check (ATI, CPIN, CREG) |
| `debug` | Toggle raw AT TX/RX tracing |
| `ram` | Show free RAM |
| `help` | List commands |
