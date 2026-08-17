# diy-phone

A from-scratch mobile phone: nRF52840 + SIM7600 cellular + e-ink display +
I2C keyboard. Texts, calls, contacts, a dial pad, and a serial console.

**Current version: [`v61e/`](v61e/) — see its [README](v61e/README.md).**

Each `vNN/` directory is a complete firmware snapshot; the version number
only ever goes up, so the highest one is the newest. Directories with an
`e` suffix (v45e onward) target the e-ink display; earlier versions drove
a 240x240 TFT.

The defining design idea of the current line: **all-driven partial
refreshes**. The SSD1681 only drives pixels that differ from its stored
"previous image" — and undriven pixels are what fade and ghost on this
panel. So the firmware writes each frame to the controller's current RAM
plane and the frame's *inverse* to the previous-image plane: every pixel
compares as changed, every pixel gets driven, and the panel delivers
crisp text on every quiet update — no fading, no ghosts, and no blinking
at all during use (full refreshes survive only at boot and as a rare
idle panel-care cycle). The full story, including the failed
approaches that mapped the panel's behavior, is in the v61e README.

Waypoints in the history: `v41` (the TFT firmware, restructured into
modules), `v44` (address book, missed calls, network timezone), `v45e`
(the e-ink port), `v53e` (voice calls), `v60e` (the all-driven refresh breakthrough), `v61e` (current).
