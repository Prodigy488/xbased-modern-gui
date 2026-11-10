# xbased Modern GUI 1.0.76.10: game compatibility fixes, kernel scan, Linux AppImage

```
Changes since 1.0.76.2
----------------------
Fixes that apply to every game
- Games that use MMX instructions with an operand-size prefix (0x66) no longer crash; the
  Pentium III the Xbox has ignores that prefix on MMX, the emulator treated it as SSE2 and
  stopped on a misaligned operand. (Dr. Muto crashed at its first screen.)
- A critical section a game never initialized (all zero) is taken as free instead of waited
  on for ever, as it is on the console. (Marvel vs. Capcom 2 stayed on a black screen.) The
  log says "never initialized" when it happens.

New
- Settings > System > "Scan games for missing kernel functions": reads every game's default.xbe
  and writes compat_report.txt (the games with the most gaps first, and each missing function with
  how many games need it). "Open the scan report" opens it.
- Linux: an AppImage (xbased-1.0.76.10-x86_64.AppImage). Needs a Vulkan driver and glibc 2.38 or newer (Ubuntu 24.04, Fedora 39, Debian 13); not run with a GPU or sound yet.
  Settings, saves and states are in ~/.local/share/xbased.

Logs (to find out why a game stops or is silent)
- A hang or a crash now lists every guest thread: what it waits on, where it is, its stack, the
  game's code there, and the kernel's latest calls.
- An "audio:" line every 3 seconds while a game's sound is active (voices started and stopped,
  interrupts, the voices playing), and each thread's turns and CPU time in the stats lines.

Known
- Marvel vs. Capcom 2 now starts and plays, with the music, but hit sounds and speech are
  missing: the game never asks the audio chip to play them. Not fixed yet.
```

Downloads: `xbased-modern-gui-1.0.76.10-windows-x64.zip` (Windows 10/11 x64, Vulkan 1.1) and `xbased-1.0.76.10-x86_64.AppImage` (Linux x86-64). There is no macOS build: the emulator has no macOS port yet.
