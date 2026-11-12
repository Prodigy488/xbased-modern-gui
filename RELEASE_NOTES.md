# xbased Modern GUI 1.0.76.19: game compatibility fixes, Autofix for stalled games, Linux AppImage

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

- A flip stall (the GPU waiting for the display to flip) that Direct3D's vblank handler never ends is
  ended after 24 vblanks (0.4 s), as the handler's flip would have ended it. DreamWorks Over the Hedge
  froze at its cutscenes this way; it now plays through them (a short pause as each one starts).

New
- SMAA (Settings > Video, off): a sharper anti-aliasing than FXAA (edges found and blended along their shape;
  Jimenez et al.'s reference, MIT). Replaces FXAA when both are on. Not run with a GPU here.
- Frame interpolation (Settings > Video, off): for games at 30 fps, a frame between each two drawn from the
  motion between them, shown half a frame early; costs half a frame (about 17 ms) of delay and some GPU time.
  Not FSR 3's frame generation (no motion vectors from the game). 1.0.76.19 sharpens it (half-pixel motion,
  a motion per pixel instead of per block, a Catmull-Rom filter); checked on a CPU, the shaders compile.
- Steel Battalion's controller: for Steel Battalion (title 43430002) port 1 gets the game's big controller (levers,
  pedals, 40 buttons, gear lever, tuner dial) and your keyboard and gamepad drive it: a gamepad layout (Back held
  for the cockpit panel, with the five start-up toggle switches on the D-pad and left stick click) and every control on the keyboard, listed in the README ("Steel Battalion"). `sbc=1` /
  `sbc=0` in the game's file forces it on or off (Line of Contact, say). Built from the Xbox dev wiki and tested
  in a host test, not with the game: the log has the controller's first poll and the lamps report the game sends.
- Autofix: a game that stalls (no frame for 25 s, or none at all in 90 s) is run again with the next of nine
  switches that fits what the threads and the GPU are doing (locks in the kernel, the drives' timing, reads
  in place, short thread slices, the audio, movie and Direct3D timer patches off, GPU commands one by one,
  128 MiB of memory). A notice counts down 10 s first (the menu, a pause or minimising cancels it). A run
  that gets 900 frames past the stall keeps its fix in the game's settings file ("fix=" lines). What worked
  is remembered for every game and orders the tries for the next (the same publisher's first). In-game menu:
  "Reset this game's fixes". The log of the run that stalled is kept as xbased-before-autofix.log.
  Settings: "Try fixes when a game stalls" (on). It can only try switches the emulator already has: for
  Over the Hedge they moved the timing and got past one stall, not the cause. A long loading screen with
  no frame for 25 s counts as a stall (cancel it from the menu, or turn it off).
- The log names, at the start, the kernel functions a title imports that this build lacks ("known gaps").
- Settings > System > "Scan games for missing kernel functions": reads every game's default.xbe
  and writes compat_report.txt (the games with the most gaps first, and each missing function with
  how many games need it). "Open the scan report" opens it.
- Linux: an AppImage (xbased-1.0.76.19-x86_64.AppImage). Needs a Vulkan driver and glibc 2.38 or newer (Ubuntu 24.04, Fedora 39, Debian 13); not run with a GPU or sound yet.
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

Downloads: `xbased-modern-gui-1.0.76.19-windows-x64.zip` (Windows 10/11 x64, Vulkan 1.1) and `xbased-1.0.76.19-x86_64.AppImage` (Linux x86-64). There is no macOS build: the emulator has no macOS port yet.
