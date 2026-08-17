# diy-phone

A from-scratch mobile phone: nRF52840 + SIM7600 cellular + e-ink display +
I2C keyboard. Texts, calls, contacts, a dial pad, and a serial console.

**Current version: [`v60e/`](v60e/) — see its [README](v60e/README.md).**

Each `vNN/` directory is a complete firmware snapshot; the version number
only ever goes up, so the highest one is the newest. Directories with an
`e` suffix (v45e onward) target the e-ink display; earlier versions drove
a 240x240 TFT.

The defining design idea of the current line: **e-ink equilibrium mode**.
Rather than chasing maximum contrast, the screen stays in its uniform
partial-refresh state for the whole session — no blinking, no visible
fading transitions — and the only routine full refresh is the page-turn
moment of opening a message to read it. The reasoning and the panel
measurements behind it are in the v60e README.

Waypoints in the history: `v41` (the TFT firmware, restructured into
modules), `v44` (address book, missed calls, network timezone), `v45e`
(the e-ink port), `v53e` (voice calls), `v60e` (current).
