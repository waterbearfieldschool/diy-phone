# DIY Phone v44

v43 plus missed-call tracking, a built-in address book, and a contact
picker in compose. Boot runs a visible checklist
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
