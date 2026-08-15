# DIY Phone v42

A fresh start, grown outward from the v41 hardware diagnostics instead of
inherited from the full v41 phone. Today it is a hardware bring-up console:
display + SIM7600 health check + live call/SMS events, with an interactive
serial port. Phone features get added back on top of this base.

Carried over from v41 unchanged: `Config.h` (pins), `Log`, `Modem`, and the
board/variant files for the ProMicro nRF52840.

```
pio run                # build
./build_and_copy.sh    # build + produce bin/uf2conv/new_firmware.uf2
```

Flash: double-tap reset, copy `bin/uf2conv/new_firmware.uf2` to the UF2 drive.

## What it does on boot

1. Display comes up and becomes a scrolling console (also mirrored to USB
   serial at 115200).
2. Modem bring-up with retries (the SIM7600 can take ~10-20s after power-on).
3. Health check: ATI (model), AT+CPIN? (SIM present), AT+CREG? (registration).
4. Header updates every 5s with signal strength and network time.
5. RING / +CMTI events print in yellow as they arrive -- call or text the SIM
   to test end-to-end.

## Keyboard

Boot lands in the **console** (keyboard echo test -- every key prints its
character and code, which is the keyboard wiring check). From there:

| Mode | Keys |
| --- | --- |
| Console | any key echoes; `TAB` opens the message list |
| List | `UP`/`DOWN` select, `ENTER` view, `d` delete, `c` compose, `r` reload, `ESC`/`TAB` console |
| View | `d` delete, `r` reply, `ESC` back to list |
| Compose | type number, `ENTER`, type body, `ENTER` sends; `BACKSPACE` edits, `ESC` cancels |

Messages live on the SIM (read via `AT+CMGL`/`AT+CMGR`, kept until you delete
them with `d`). A text arriving while the list is open refreshes it live.

## Serial commands (115200 baud)

| Command | Action |
| --- | --- |
| `AT...` | Send any AT command to the modem, print the reply |
| `status` | Re-run the health check |
| `debug` | Toggle raw AT TX/RX tracing |
| `ram` | Show free RAM |
| `help` | List commands |
