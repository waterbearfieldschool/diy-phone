v41 is a restructuring of v40 into modules -- same phone, same keys, same SD card
files. See README.md.

Builds clean (`-Wall -Wextra`, no warnings). Flash 113,748 bytes vs v40's 149,052.
Logic tests pass on the host: `./tools/hosttest/run.sh`.

Fixed from v40:
- long last message no longer cut off at the bottom (the issue in v40/latest.md)
- UP/DOWN navigate the contact list and scroll the conversation again; v40 had
  reassigned them to volume, leaving no way to navigate
- contacts past the 20th are reachable
- timestamp sort keys no longer overflow 32 bits
- modem notifications arriving during an AT command are no longer dropped

Not yet verified on hardware -- built and unit tested only.
