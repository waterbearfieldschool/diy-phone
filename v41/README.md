# DIY Phone v41

A restructuring of v40. Same hardware, same keys, same files on the SD card —
the firmware is reorganized so that each idea has one implementation.

```
pio run                      # build
./build_and_copy.sh          # build + produce bin/uf2conv/new_firmware.uf2
./tools/hosttest/run.sh      # run the logic tests on your laptop, no board needed
```

## Layout

| File | Responsibility |
| --- | --- |
| `Config.h` | Every pin, screen dimension, capacity and key code |
| `Log.{h,cpp}` | Leveled tracing; `'9'` toggles Debug at runtime |
| `PhoneNumber.{h,cpp}` | Number normalization and comparison |
| `Timestamp.{h,cpp}` | Modem timestamps as an instant + UTC offset |
| `Storage.{h,cpp}` | The SD card and its SPI bus |
| `Contacts.{h,cpp}` | The address book |
| `MessageStore.{h,cpp}` | `sms_*.txt` files; one reader for all four historic layouts |
| `Threads.{h,cpp}` | Contact list and conversation loading |
| `Modem.{h,cpp}` | SIM7600; sole owner of the modem UART |
| `Screen.{h,cpp}` | Every pixel |
| `main.cpp` | Application state and key handling |

`main.cpp` went from 3248 lines to 606, and the firmware as a whole from 4173
lines to 2230 (−47%) across 11 modules. Flash use dropped from 149,052 to
113,748 bytes (−24%). v40 compiled with 6 warnings; v41 compiles clean.

RAM went up slightly, from 24,980 to 27,124 bytes of static allocation. That is a
deliberate trade: the contact list now holds 100 conversations instead of 20 (see
below), which costs more than the smaller message records save. Runtime heap
churn is much lower — a stored message carries 2 `String`s instead of 8.

## What was consolidated

**Message file reading, 3 copies → 1.** `loadAllMessages`, `buildThreadPreviews`
and `loadThreadForContact` each walked the card and parsed messages with their
own format sniffer, reading fields by fixed line position
(`lines[2].substring(6)`). `MessageStore::forEach` is now the only code that
iterates the card, and the parser is key-driven: it reads `Key: value` lines and
ignores keys it does not recognize, so all four generations of file layout load
through one path. Adding a field no longer means touching three parsers.

**Phone number comparison, ~10 copies → 1.** The five-line "strip spaces, dashes,
parens, plus, then compare with country-code fallback" block was pasted inline
wherever two numbers met. It is now `PhoneNumber::same`.

**Timestamps, ~350 lines → ~90.** `parseTimestamp`, `convertToUTC`, `addOneDay`,
`subtractOneDay` and `formatTimeForDisplay` are replaced by parsing to seconds
since 2000 with a real calendar conversion. The day-rollover helpers guessed
month lengths (`if (day > 28 && month == 2)`) and ignored leap years; correct
date arithmetic makes them unnecessary.

**Modem response reading, 4 readers → 1.** `waitForResponse`, `getATResponse`,
`getMultiLineResponse` and `debugRawResponse` each re-implemented "read bytes
until something looks finished" with their own timeout rules and inline hex
dumps. There is now one `command()` that returns informational lines to the
caller.

Also removed: the duplicated `'C'`/`'c'` call handlers, the never-called
`buildThreadPreviews`, the declared-but-never-defined thread-cache functions,
`drawComposeAreaOnly` (a near-copy of the compose block in
`drawConversationPane`), the unused `SoftwareSerial` constructor, and
`src/examples/basic_test.cpp`, which was entirely inside a comment block. The
host tests cover what it was for.

## Bugs fixed

**A long final message is no longer cut off** (the issue noted in
`v39/latest.md` and `v40/latest.md`). `calculateTotalContentHeight()` estimated
each message as `ceil(length / charsPerLine)` lines, while `drawWrappedText()`
actually wrapped at word boundaries. Word wrapping strands the tail of each line,
so it needs *more* lines than that estimate — the auto-scroll target was
therefore short and the bottom of the last message fell below the viewport.
`Screen::wrapText` now both measures and draws, so the two can never disagree.
The host tests demonstrate the old shortfall directly. `drawWrappedText`'s hard
10-line cap, which truncated long messages outright, is also gone.

**Arrow keys navigate again.** v40 reassigned UP/DOWN to speaker volume
unconditionally, which orphaned `scrollThreadSelection()` and
`scrollConversation()` — nothing called them, so there was no way to move the
contact selection or scroll a conversation; only search-by-letter could change
the selection. v41 restores v39's context-sensitive behaviour: in a call UP/DOWN
are volume, otherwise they navigate the list or scroll the thread.

**All contacts are reachable.** `threadPreviews` held 20 entries while
`addressBook` held 100, so contacts 21–100 could never appear. Both are 100 now.

**Timestamp sort keys no longer overflow.** `year * 10000000000UL` does not fit
in a 32-bit `unsigned long`; the packed sort key wrapped, and ordering was only
accidentally consistent. Seconds-since-2000 fits comfortably.

**Notifications are no longer lost during an AT command.** `readUARTLines()` in
the main loop and `SIM7600`'s response readers both read `Serial1`, so a `RING`
or `+CMTI` arriving mid-command was consumed as command output and dropped.
`Modem` now owns the UART: every line read anywhere is classified, and
notifications are queued and dispatched from `poll()`. Queueing rather than
dispatching inline also means a handler can issue its own AT commands without
reentering an unfinished one.

**Sent messages cannot overwrite each other after a reboot.** Outgoing files were
named `sms_out_<millis>.txt`, and `millis()` restarts at zero on boot, so a new
message could truncate an older one. They are now named from the timestamp with a
collision counter.

## Behaviour changes worth knowing

- **Compose line scrolls.** A message longer than the display used to spill out
  of the 20px compose band. It now shows the tail, keeping the cursor visible.
- **Selection follows the conversation, not the row number.** `selected` indexes
  the *filtered* list, so searching and arrow navigation compose. Clearing a
  search keeps the same conversation highlighted rather than jumping to the top.
- **Diagnostic 7** queries the SIM's slot count via `AT+CPMS?` instead of
  assuming 30 slots.
- **The status bar shows free RAM.** v40 labelled the same number "used".

## Keys

| Key | Action |
| --- | --- |
| `TAB` | Switch pane |
| `UP`/`DOWN` | In a call: speaker volume. Otherwise: move contact selection, or scroll the conversation |
| `LEFT`/`RIGHT` | Microphone gain |
| `ENTER` | Answer a ringing call; else open the selected conversation, or send |
| `ESC` | Hang up; else clear the contact search |
| `C` | Call the selected contact (contacts pane) |
| `H` | Hang up (during a call) |
| `A`–`Z` | Search contacts (contacts pane) / type the message (conversation pane) |
| `1`–`8` | Diagnostics (contacts pane) |
| `9` | Toggle debug logging |

## Known limitations, carried over from v40

- **`C` and `c` cannot be searched for.** The call shortcut is checked before
  letter search, so no contact whose name starts with C can be reached by
  typing. Moving `call` onto a non-letter key would fix this, but it changes a
  key binding, so it is left as-is.
- **Digits cannot be searched for either**, for the same reason: `1`–`9` run
  diagnostics in the contacts pane.
- **A 10-digit contact gets a bare `+` prefix when dialled.** A number stored
  without a country code becomes `+6175551234`, which is not the intended
  destination. Contacts stored in full `+1...` form are unaffected. Fixing this
  means assuming a default country code, which is a decision rather than a
  cleanup.
- **Sent messages need network time.** If `AT+CCLK?` fails there is no clock, so
  the message is transmitted but not written to the card. v40 substituted a
  hard-coded fallback date, which silently mis-sorted the thread; v41 logs the
  failure instead.

## File format

New messages are written as:

```
From: +16175551234        (received)
To: +16175551234          (sent)
Time: 26/01/04,19:04:26-32
Dir: IN | OUT
Content: the message text
```

Older files keep loading unchanged: `From/Time/Status/Content` (v26 incoming),
`From/To/Time/Status/Content` (v26 outgoing), and
`From/To/Time/LocalTime/Status/Content` (v27 dual-timestamp). Nothing on an
existing SD card needs migrating. `Content` is the last field in every layout, so
a message body containing newlines or a colon survives a round trip.
