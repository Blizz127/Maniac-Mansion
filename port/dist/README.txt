Maniac Mansion (NES) native port for Linux x86_64
=================================================

This runs YOUR OWN Maniac Mansion (USA) NES ROM. No game data is included.
The ROM is checked by SHA-256 (No-Intro "Maniac Mansion (USA)", 262,160
bytes); a clean dump with a different iNES header is also accepted.

  ./launch.sh "/path/to/Maniac Mansion (USA).nes"

The ROM is remembered, so later just run ./launch.sh. You can also set
MM_ROM, or drop the .nes file onto the window when asked.

Keyboard: arrows = D-pad, X = A, Z = B, Enter = Start, Right Shift = Select,
  F6 = fast-forward on/off, Backspace (hold) = fast-forward,
  F10 = screenshot, Alt+Enter = fullscreen, Esc = quit.
Controller: A = A, B or X = B, Back/View = Select, Start/Menu = Start,
  L3 = fast-forward on/off, R3 (hold) = fast-forward.
  Steam Deck / Legion Go pads work in Game Mode (Steam's ignore list is
  corrected at start-up; see port.log).

Battery save: ~/.local/share/maniac-mansion-port/saves/maniac-mansion-usa.sav
Config:       ~/.config/maniac-mansion-port/config.ini (see config.example.ini)
Log:          ~/.local/state/maniac-mansion-port/port.log

This is an alpha of a hardware-level runtime (6502, MMC1, PPU, APU)
verified frame-by-frame against the Mesen2 emulator. It is part of the
Maniac Mansion NES decompilation project: github.com/Blizz127/Maniac-Mansion
