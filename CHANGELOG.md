# xbased changelog

## 1.0 (2026-11)

The first release of xbased, the PC edition of
[hakuX](https://github.com/rfandango/hakuX) for Windows and Linux.

### Emulation
- hakuX's core on x86-64: the guest's code translated to native x86-64, its
  return addresses predicted, interrupts delivered at block boundaries.
- The Xbox kernel's thread priority boosts, so games that lean on them
  (NightFire and others) keep their frame rate.
- Frame-rate unlock, per game, that reaches hundreds of frames per second
  where the game allows it, and that sleeps instead of spinning when capped
  (low power on laptops and handhelds).
- Per-game patches as mods (higher frame rates for a set of games so far),
  toggled per game. IPS patches shared online work too: drop them into
  `mods/<TITLE ID>/` (MODS.md).
- Anisotropic filtering up to 16x, alongside FXAA, internal resolution and
  upscaling.
- Games start in the console's language, or in English when the disc doesn't
  have that language's files.
- Save states with thumbnails, 10 slots per game.

### Front end
- A console-style dashboard: animated orb, hexagons and fog, 3D transitions,
  navigation sounds (can be turned off), a clock.
- Game library with box art (libretro-thumbnails) and the discs' own icons.
- Per-game settings, mods and cheats, controls and states.
- Memory manager for the games' own saves, with their icons: export, import,
  delete.
- In-game menu (F1, or Back + Start on a gamepad): resume, states, settings,
  controls, restart, quit.
- Borderless full screen by default, like a console; Exit from the dashboard,
  with a confirmation, so it closes from a gamepad.
- Controls: any key, button or axis per input, 4 players, per-game mappings,
  configurable shortcuts; prompts follow the device in use.
- CRT filters (scanlines, TV, arcade), frame counter, volume and
  mute-in-background.
- Online: Insignia, XLink Kai and System Link.
- 11 languages, the system's by default; the console's language follows.
- Any window size, aspect ratio and DPI.

### Platforms
- Windows 10/11 x64: a portable folder, system DLLs only.
- Linux x86-64: an AppImage (glibc 2.35 or newer: Ubuntu 22.04, Debian 12,
  Fedora 36 and later); settings in `~/.local/share/xbased`.
