# DIY Phone v43

v42's bring-up console grown into a real UI. Boot runs a visible checklist
(keyboard, modem, SIM, network) and then drops straight into the inbox. Debug
output goes only to USB serial; the screen is all phone.

Carried over: `Config.h` (pins), `Log`, `Modem` from v42, `Timestamp` from
v41 (for sorting the inbox newest-first).

```
pio run                # build
./build_and_copy.sh    # build + produce bin/uf2conv/new_firmware.uf2
```

## Chrome (every screen)

- **Status bar** (top): signal bars, `*N` unread count in cyan, screen title,
  and SIM storage `used/total` -- dim normally, orange at 80% full, red with
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
| Compose | type number, `ENTER`, type body (`n/160` counter), `ENTER` sends, `BACKSPACE` edits, `ESC` cancels |
| Status | device/network info + keyboard echo test; `ESC` back |

Inbox rows: `* 14:22 +16175551234  preview...` -- unread messages are bright
with a cyan `*`, read ones dim. Newest first. Opening a message marks it read
(AT+CMGR does that on the SIM itself). A text arriving mid-compose doesn't
interrupt typing; the inbox refreshes when you leave compose.

Boot notes: the keyboard is required (the UI is unusable without it), so boot
waits for it, reprobing every second. The modem gets 30 seconds, then the UI
starts anyway and keeps retrying in the background -- "modem online" toasts
when it appears.

## Serial commands (115200 baud)

| Command | Action |
| --- | --- |
| `AT...` | Send any AT command to the modem, print the reply |
| `status` | Modem health check (ATI, CPIN, CREG) |
| `debug` | Toggle raw AT TX/RX tracing |
| `ram` | Show free RAM |
| `help` | List commands |
