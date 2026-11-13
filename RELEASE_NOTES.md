# xbased Modern GUI 1.0.76.31: game compatibility fixes, Autofix for stalled games, Linux AppImage

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
  1.0.76.20 fixes its smearing on low-contrast content (dim scenes, fog, sky, distant ground): the motion search
  found no motion there and the two frames were blended on top of each other.
  1.0.76.21 takes about two refreshes (33 ms at 60 Hz) of delay off it under V-Sync, and a needless present loop
  off it without V-Sync: the in-between frame was presented over and over until the frame's own time.
  1.0.76.22: also for 60 fps games on displays of 120 Hz and up, and it follows motion up to about 40 console
  pixels a frame (16 before), fine texture included: the search now goes from a quarter-size picture to the full.
  1.0.76.23: less flicker on detailed backgrounds (the in-between frame is one frame moved, not two averaged).
- Sharpening (Settings > Video, off; 1.0.76.24): low, medium or high, for a picture no upscaler sharpens
  (Bilinear or Nearest). AMD's RCAS, at the frame's own size. Not run on a GPU.
- Picture settings chosen for the PC (1.0.76.25): the first start sets the render scale, anisotropic filtering,
  anti-aliasing and upscaler from the GPU's kind and memory and the display's height, where you haven't chosen
  them; Settings > Video can choose them again. A guess from memory sizes, not a measurement.
- 1.0.76.25: texture packs and automatic upscaling no longer garble games (black scenes, noise, textures in the
  wrong place): their pictures only go up when the upload buffer has room, and an upscaled picture that doesn't
  look like the game's texture is not used.
- 1.0.76.26: Real-ESRGAN (the program and its x4plus model, about 40 MB) comes in the download, so automatic
  texture upscaling works without fetching anything. Its licences are in the realesrgan folder.
- 1.0.76.27: automatic upscaling now takes on textures with transparency and palettized ones, which it refused
  before (most of some games' textures): the upscaler gets the colour, the game's own alpha is kept. Textures
  refused earlier are made again after the next run of the game.
- 1.0.76.28: every game's log is kept (the logs folder, named by time and game, the 40 newest), so a crash can
  be looked into after another game was started. Automatic texture upscaling is turned on at the first start on
  a discrete GPU of 6 GB or more (Settings > Video turns it off).
- 1.0.76.29: HD texture packs to share. A game's menu has "Export HD texture pack" (a zip named after the game in
  "texture packs/exported"); a pack's zip put into the "texture packs" folder is installed when xbased starts.
  With automatic upscaling on, a game's disc is also read once for the textures it keeps in standard XPR
  containers, so those are upscaled without playing to them (not every game keeps its textures that way).
- 1.0.76.30: textures are upscaled while you play. Real-ESRGAN runs beside the game in batches, and each texture
  changes to its upscaled picture a few seconds after that is ready, so a new area sharpens on the first visit.
  It shares the GPU with the game; Settings > Video > "Upscale while playing too" turns that part off.
  (1.0.76.31: without the console window that 1.0.76.30 showed at every batch on Windows.)
- Integer scaling (Settings > Video, off): the picture a whole number of times the console's lines tall, with
  borders. Pair it with the Nearest upscaler for square pixels.
- Texture packs (Settings > Video, off): PNG files in hle/textures/<title id>/ (in the data folder) are drawn in
  place of the game's textures, at any size up to 4096. "Write the game's textures out" puts the game's own in
  that folder's dump/, named by their hashes; an edited one goes one folder up under the same name. Plain
  textures only (no cube maps, volumes or video). The files' reading and writing were tested; not run in a game.
- Automatic texture upscaling (Settings > Video, off; 1.0.76.23): with Real-ESRGAN's portable program
  (realesrgan-ncnn-vulkan, a separate download: github.com/xinntao/Real-ESRGAN/releases) in a "realesrgan" folder
  beside xbased or in the data folder, the textures a game used are upscaled 4x in the background after you
  leave it and drawn the next time you play. Tested with a stand-in for the program on Linux; not with the
  program itself, and not on Windows.
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
- Linux: an AppImage (xbased-1.0.76.31-x86_64.AppImage). Needs a Vulkan driver and glibc 2.38 or newer (Ubuntu 24.04, Fedora 39, Debian 13); not run with a GPU or sound yet.
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

Downloads: `xbased-modern-gui-1.0.76.31-windows-x64.zip` (Windows 10/11 x64, Vulkan 1.1) and `xbased-1.0.76.31-x86_64.AppImage` (Linux x86-64). There is no macOS build: the emulator has no macOS port yet.
