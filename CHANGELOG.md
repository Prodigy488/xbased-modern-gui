# xbased changelog

## 1.0.76.40 (Modern GUI fork)

- The cache drives (partitions 3 to 5) always report at least 1 GB free: a log showed partition 5 at 0 MB free because its
  folder held 299 GB, and titles that check free blocks before saving ("Not enough free blocks to save game data") refused.

## 1.0.76.39 (Modern GUI fork)

- The update check no longer offers an older build: the dashboard kept its own copy of the version number, which
  was not bumped after 1.0.76.34 (the About line and the check read 1.0.76.34 in later builds). The version is now
  written once, in native/app/version.h, for the dashboard and the window loop.

## 1.0.76.38 (Modern GUI fork)

- Jade Empire's focus mode draws right: the game's filter draw samples a texture laid over the depth buffer's
  memory, and the picture it produced was grey and blocky (on stock xbased and xemu too). For this title that
  draw is now left out (the test mode 3 below), found with the F10 test modes; the world keeps its colours. The
  cause in the depth-as-colour conversion is not found, so the effect's depth part is missing. Other titles are
  unchanged. F10 cycles auto (the default), 0, 1, 2 and 3: 1 sends the programs that read their own target
  through a copy of it, 2 skips those draws, 3 skips the draws that sample a depth buffer's words; the log and
  a notice name the mode.

## 1.0.76.36 (Modern GUI fork)

- F10 cycles a graphics test mode (debug.hakux.fbdbg) for chasing the focus-mode picture in Jade Empire: 1 sends
  the pixel-shader draws that read the target they render through a copy of it, sampled at their own texture
  coordinates; 2 skips those draws; 3 skips the draws that sample a depth buffer's words. A notice shows the
  mode and the log says it; 0 is the normal picture.

## 1.0.76.35 (Modern GUI fork)

- F9 takes a graphics capture while a game runs, for a bug report: the next frame's render targets go to
  hle_shadow/ and 800 draws, with their textures and sampler registers, to the log. It sets the same debug
  properties as props.ini (debug.hakux.rtdump, debug.hakux.pbdraws), which only act on a value that changes
  while the game runs and so were awkward to use from the file; each press is a new value.

## 1.0.76.34 (Modern GUI fork)

- Voice chat (experimental, Settings > Audio > "Voice chat headset", off): an Xbox Live Communicator, the Xbox's
  USB headset, in player 1's controller (its first memory unit slot), for the games' voice chat on Insignia and
  System Link. It is the real headset's USB device (045e:0283: two interfaces, an isochronous endpoint each, mono
  16-bit samples at 8000 to 24000 Hz, as the Xbox dev wiki lists it); the PC's default microphone is what it
  hears and its speaker is the game's sound device. The controller's USB (ohci.c) now does isochronous transfers
  (it skipped them before): one packet a frame from the TD's PSW list, condition codes and sizes written back, a
  TD whose frames have gone by retired with DataOverrun. Host test (tests/native/usb): the headset's descriptors,
  the sample-rate request, a speaker's four packets out and a microphone's in with their PSWs and done queue, and
  the missed frames. NOT tried with a game: the Xbox's voice library, and how it uses the device, are unseen
  here, so whether a title sees the headset is not known. With it off (default) nothing changes. The microphone is
  opened when a title first uses the headset (the system may ask for permission); the log says "headset: ..." for
  each step. Also: autofix makes no retry for a game whose GPU was lost, and a failed upscaler batch is said in
  the log.

## 1.0.76.33 (Modern GUI fork)

- Sound device: Settings > Audio > "Sound device" lists the playback devices the system has and the one chosen
  is used from the next game start (the default one if it is not there any more or will not open; the log says
  which). Chosen by name, global or per game.
- Upscaling while playing is lighter: a run beside a game takes at most about 2 million input pixels (about eight
  textures of 512 a side, the newest dumps first: what the game shows now), so the GPU is held for a second or so
  and let go; the run when a game ends still takes everything. Textures of 1024 or more a side are not made
  (the pictures are used at 1024 at most, so a 4x of them was only made smaller again). Tested here with a
  stand-in for the program: which files it is handed, in which order, for a budget and for none.
- Looked through with clang's static analyzer (the new files, the presenter, the back end, the draw and window
  code): nothing real found; one report (a possible null render target in a channel clear) cannot happen, the
  caller drops the target flag without one.

## 1.0.76.32 (Modern GUI fork)

- Texture pack pictures are capped at 1024 a side for every game and GPU (2048 before; an xbased.ini that says
  2048 is capped too). Two logs of Morrowind (4x, upscaling on, an RTX 5090 Laptop GPU) lost the GPU
  (VK_ERROR_DEVICE_LOST in vkQueueSubmit2) at the same place, an area load with hundreds of pictures of 2048
  a side in use, and with the live upscaler off the second time. Above 1024 the pictures add little at the sizes
  these games are shown at. Cause not proven; the log now says how many pictures and MiB were uploaded (every
  512 MiB), so the next one shows it.
- Real-ESRGAN runs in 128-pixel tiles ("-t 128"), so that none of its GPU dispatches is long beside a game.

## 1.0.76.31 (Modern GUI fork)

- Windows: no console window when the upscaler runs. 1.0.76.30 started Real-ESRGAN as an ordinary child
  process so that its end could be asked for, and on Windows a console program started that way from a
  windowed one gets a console window: a black window came up over the game at every batch (reported with a
  screenshot). It is started as SDL's "background" process again on Windows (CREATE_NO_WINDOW), as before
  1.0.76.30; SDL's Windows code can ask such a process for its end all the same (it keeps the handle), which
  is all the batches need. Linux is unchanged. Read from SDL's source; not run on Windows here.

## 1.0.76.30 (Modern GUI fork)

- Textures upscaled while the game runs (Settings > Video > "Upscale while playing too", "texlive" in the
  settings files; on, and in effect with automatic upscaling on). Until now the upscaler ran when a game was
  left, so its pictures were first drawn in the next session. Now:
  - every ten seconds of a game, what it wrote out since goes to Real-ESRGAN, on a thread of the window's;
    one run at a time (a run still going is left to finish; what came meanwhile goes with the next);
  - a texture that was written out and has no picture yet is remembered as awaited; the draw code asks for
    its picture every two seconds or so (each texture at its own frame, so the asking is spread out), and
    when the file is there and two seconds old (whole) defines the texture again, which draws the picture
    from then on. At most one texture is taken up every six frames: reading a picture takes tens of
    milliseconds, and a scene's worth at once would be a stall;
  - a picture's absence is no longer remembered for the run when this is on (it was asked for once);
  - the upscaler is started as an ordinary child with its output thrown away, not as a "background" process
    (whose end can't be asked for). It still runs on when xbased has gone.
  What it costs: the upscaler takes the same GPU as the game while a batch runs, so the frame rate can drop
  then, and each texture taken up is a short hitch. With the setting off it is as before (after the game).
  Tested on Linux with a stand-in for the upscaler: one run at a time, the next run takes only what is new,
  a run goes on after xbased exits, none of its output in the log. The taking up of pictures in a running
  game is not tested: it uses the path a picture waiting for room has used since 1.0.76.25.

## 1.0.76.29 (Modern GUI fork)

- HD texture packs to share (native/app/ui/texshare.c). A game's page and its in-game menu have "Export HD
  texture pack": the game's upscaled pictures (auto/) and hand-made ones go into
  "texture packs/exported/<game's name> [<title id>] HD textures.zip" in the data folder (a second part past
  1.9 GB or 60000 pictures), with a pack.txt that names the game. A zip put into "texture packs" is installed
  when the dashboard next starts, a message says how many pictures, and the zip moves to
  "texture packs/installed". An installed upscaler's picture is treated as one made on this PC: checked
  against the game's texture and given its alpha when drawn. Only entries named <title id>/<hash>.png or
  <title id>/auto/<hash>.png leave a zip, and only into the textures folder; a pack zipped again by another
  program (deflate) is read too. Tested on Linux: an export installed on a second folder gives the same
  files, a deflated zip installs, entries with ".." in their names are left out, Python's zipfile reads the
  export. The menu row and the message were not seen on a screen.
- The disc scanned for textures (native/app/texscan.c). With automatic upscaling on, the first run of a game
  reads its disc image (or folder) once, on a thread of its own, for XPR0 containers, the XDK's packed
  resources, alone or inside other files, and writes the textures in them to dump/ under the hash the game's
  own loading of them gets, so the upscaler has them after that run without the game having shown them. Not
  palettized ones (their palette comes at run time), and nothing from a game that keeps its textures in a
  format of its own or compressed, as many large ones do: the log's "texture scan:" line says how many were
  found, and dump/scan.done keeps it from being done again. What stands on reading, not on a test: that a
  texture's description in a container is Direct3D's five words with the common word 0x0004xxxx and one
  reference, and that the game hands the data over unchanged, so the hashes meet. The finding of containers
  (a tag across a read's join, a tag that is no container) was tested on Linux with a made-up image; no real
  disc was read. A found texture that never matches costs an upscale and nothing else.

## 1.0.76.28 (Modern GUI fork)

- Every game's log is kept. xbased.log is written over when a game starts, so the log of a crash was gone as
  soon as another game was run (a crash of The Incredibles was reported with the log of the game played
  after it). Before it is written over, a log that got as far as a game is moved to the data folder's logs/
  as "<date>-<time> <the disc's name>.log", by when it was last written; the 40 newest stay. A run started
  again (a state loaded, a title's relaunch) still goes on in the same log, and the dashboard's own log is
  not kept. Tested on Linux with sample logs.
- Automatic texture upscaling on by default where the GPU has the memory. The hardware pick (1.0.76.25) now
  also sets "texauto": on for a discrete GPU of 6 GB or more when the setting isn't there yet (a first start,
  or an upgrade from a version before the pick), and off elsewhere when the pick is asked for again. What
  that brings to someone who didn't ask for it: a game's first loads are slower while its textures are written
  out, the GPU is busy for some minutes after a game is left, the textures folder grows by some hundred MB a
  game, and games use more video memory. Settings > Video turns it off.

## 1.0.76.27 (Modern GUI fork)

- Automatic texture upscaling took on few of some games' textures. A log of 1.0.76.26 (title 41430007) ends
  "this run drew 13 upscaled textures and refused 180", and the refusals' lines say which: 22 of the 24 logged
  are palettized textures (format 0x0B), the other two A8R8G8B8, with the upscaled picture's alpha far from
  the texture's (up to all of it: 255 of 255) and its colour often too (40 to 240, 28 allowed); two had the
  colour right (0.1) and only the alpha wrong. The textures that passed are the ones without an alpha
  channel to speak of. So what Real-ESRGAN's program makes of a PNG with an alpha channel is not to be used,
  on this PC at least. Now:
  - a texture is written out as colour alone (a three-channel PNG), which is the path the passing ones took;
  - the alpha drawn is always the game's own, read bilinearly at the picture's size, whatever the file has;
  - the check is on colour alone (where the texture isn't transparent; everywhere if nearly all of it is);
  - a picture refused is removed with its dump, so the texture is written out again in the new form and
    upscaled after that run; one refused a second time is marked (auto/<hash>.bad) and left alone;
  - textures under 16 texels a side are left as they are (an 8 x 8 came back as noise).
  Pictures already made that have the colour right are used as they are, with the game's alpha.
  Checked on a CPU: the alpha's transfer and the check. Whether the colour of those textures now comes back
  right is for the next log to say ("drew N, refused M", and each refusal's line).

## 1.0.76.26 (Modern GUI fork)

- Real-ESRGAN comes with xbased: realesrgan-ncnn-vulkan (release v0.2.5.0, 20220424) and its
  realesrgan-x4plus model, unmodified, in a "realesrgan" folder beside the program (in the AppImage, beside
  the binary), where automatic texture upscaling looks first. The download grows from 5 to about 42 MB. Their
  licences (Real-ESRGAN and the model BSD 3-Clause, the program MIT, ncnn BSD 3-Clause and its parts') are in
  licenses/realesrgan.license.txt and in the folder. The files are not in the repository:
  tools/release/fetch_realesrgan.sh fetches them for packaging.
- The log tells an upscaled texture's story truly. "in use" was written when a picture was read, before the
  check against the game's texture, so a refused picture was logged as in use and then as not used (a log of
  1.0.76.25 has 16 such pairs). It is now written when the picture goes up. A refusal says why: the two sizes,
  the texture's format, and how far the picture averaged down is from it in colour and in alpha (of 255; 28
  is the most allowed). At exit: "this run drew N upscaled textures and refused M".
- mvstat=1 did nothing on Windows in 1.0.76.24 and .25 (that log has no "motion stats" line): the option set
  an environment variable the draw code then didn't see. It is a setting passed like the others now, and the
  log says "motion statistics on" when it is read.

## 1.0.76.25 (Modern GUI fork)

- Texture packs and automatic upscaling garbled many games (reported on 1.0.76.24 with screenshots: black
  scenes with a few objects left, coloured noise over walls, a texture drawn where another belongs). The
  pictures went up through the upload arena, a 32 MiB ring the draws' vertices and constants share. A picture
  is megabytes (a 2048 one with its levels 22), so the arena was full at almost every one, and the wait for
  room there ends the submission being built: the draw in the middle of which the texture was being defined
  had its own vertices and constants in that submission, and once the GPU finished it the next allocation
  could land on them. The draw then ran with another's data. (The same wait exists for the game's own
  textures, which are small enough that it nearly never falls inside a draw.) Now:
  - a pack's picture never waits for room: it goes up only into what is free at that moment, with a quarter
    of the arena left for the draws; otherwise the game's own texture is drawn and the picture is tried
    again a frame later, so a scene's textures sharpen over its first frames;
  - with packs or the upscaler on, the arena is 256 MiB (the usual size if the GPU won't give that);
  - an upscaler's picture is checked against the game's texture before it is used: a whole number of times
    its size and, averaged back down, like it in colour (where the texture isn't transparent) and in alpha.
    One that fails (a black or garbled output, an alpha channel lost) is left unused and the log says so.
    Your own files in the title's folder are not checked;
  - "texcap" (xbased.ini; set by the hardware pick below) is the largest side a pack's picture is drawn at.
  The cause is read from the code and the fix follows it; the check was tested on a CPU. Neither was run in a
  game: the screenshots' games are the test.
- Picture settings chosen for the PC. The first time the dashboard starts it reads the GPU (through Vulkan:
  discrete or integrated, its largest memory heap) and the display's height, and writes to xbased.ini the
  settings that depend on them, those you haven't set: the render scale (the first to reach the display's
  height, 480 lines a step, up to 4x on a discrete GPU of 6 GB, 3x of 3 GB, 2x on a smaller or integrated one),
  anisotropic filtering (16x or 8x), the anti-aliasing (SMAA, or FXAA on the small ones), FSR where the scale
  doesn't reach the display, and texcap (2048 from 10 GB, 1024 from 6, 512 from 3, else 256). Settings > Video >
  "Choose picture settings for this PC again" sets all of them afresh at the next start. A table of memory
  sizes, not a benchmark: a game that runs slowly still wants a lower scale. The table was checked on a CPU;
  the reading of the GPU was not run.

## 1.0.76.24 (Modern GUI fork)

- Sharpening without an upscaler (Settings > Video, "sharpen" 0 to 3 in the settings files; off). FSR 1 and
  NIS sharpen as they scale; with Bilinear or Nearest (or a window the frame's own size) nothing did. It is
  FSR's RCAS pass (AMD, MIT) over the frame at its own size, after the anti-aliasing and the in-between
  frame, before the picture is placed: low, medium and high are 2, 1 and 0 stops of its sharpness. Off under
  a CRT filter, and when FSR or NIS is the upscaler in use. The shader compiles (Vulkan 1.1, 1.3); not run on
  a GPU.
- A measurement toward per-pixel motion vectors ("mvstat=1" in xbased.ini or a game's file; nothing is drawn
  differently). Motion vectors would let frame interpolation use the game's own motion instead of searching
  for it, and are what temporal upscalers (FSR 2 and its kind) need. Here every draw, fixed function
  included, is a vertex program with a block of 192 constants, so a draw's position a frame ago is the same
  program run with the constants its twin had then. That needs each draw paired with its twin, and this
  counts how often there is one: the same program, primitive, vertex layout and count, textures and
  (vertices read in place) addresses, the n-th of its kind in the frame. Every 600 frames the log has a
  "motion stats" line: draws a frame, the share with a twin (and of those, with other constants, which is
  what moves), the share with none. What it can't tell: whether a twin's vertices are the same (a mesh the
  game animates on the CPU), and the camera's own jitter, which a temporal upscaler also needs.

## 1.0.76.23 (Modern GUI fork)

- Frame interpolation flickered on detailed backgrounds (reported on 1.0.76.22). The in-between frame was the
  average of the two frames around it, each moved half-way. Each frame has its own aliasing and dither
  pattern, and a move by part of a pixel softens, so the average kept about 57 % of a detailed picture's
  finest detail (measured on a CPU model: point-sampled texture, motion 0 to 8 pixels a frame): sharp, soft,
  sharp at half the frame rate. It is now the newer frame alone, moved half-way back, and that move is pulled
  toward whole pixels (by an eighth of a pixel at most), where reading the picture loses nothing: 86 % on the
  same model. The two frames are still compared, and where they disagree the frame is shown unmoved, as
  before. (The first suspect, the search giving still, dithered backgrounds a motion, was modelled and is not
  the cause: under 2 % of blocks, before and after.) Not run on a GPU.
- Automatic texture upscaling (Settings > Video, "texauto" in the settings files; off). With it on, a game
  writes its textures out and uses a pack as with those two settings, and when you leave the game the
  textures that have no picture yet (none of your own in the title's folder, none in its auto/) are copied to
  a folder of their own and handed to Real-ESRGAN (realesrgan-ncnn-vulkan -n realesrgan-x4plus -s 4), which
  writes them four times the size into auto/ and runs on by itself. The next time the game starts they are
  drawn. The program is not included: its portable release goes into a "realesrgan" folder beside xbased, or
  in the data folder (the AppImage: ~/.local/share/xbased/realesrgan), or $XBASED_REALESRGAN names it. The
  log says how many textures went to it, or that it wasn't found.
  A pack's picture may now be up to 8192 a side and goes up at 2048 at most (halved until it fits), so a 4x
  of a large texture is used at 2x; a file cut short (the PC turned off mid-way) is deleted and made again.
  Your own file in the title's folder wins over auto/'s.
  What to expect: upscaled textures take 16 times the memory at 4x, and DXT textures become uncompressed, so a
  game heavy on textures can need several GB of video memory; tiling textures can show seams, since each is
  upscaled on its own; folders with non-ASCII letters in their path are not handled on Windows yet. Tested on
  Linux with a stand-in for the program (the right files queued, the program's arguments, it running on after
  xbased exits, nothing queued twice); not with Real-ESRGAN itself and not on Windows.

## 1.0.76.22 (Modern GUI fork)

Four additions, none of them run on a GPU here (the shaders compile for Vulkan 1.1 and 1.3; the parts that are
plain arithmetic were modelled or tested on a CPU, as said with each).

- Frame interpolation for 60 fps games on fast displays. It was for frames 24 to 100 ms apart, which is 30 fps
  on any display. The window's front end now tells the presenter the display's refresh rate
  (SDL's, again when the window changes display), and a frame counts as slow when it comes 1.6 refreshes or
  more after the last: 60 fps on 120 Hz and up as well. 24 ms still counts on any display, 12 ms is the least.
  The setting reads "games slower than the display".
- Frame interpolation follows faster and finer motion. The search compared 16 point samples of each block on
  the full picture at shifts 2 pixels apart, up to 16 console pixels a frame. Fine texture defeated it (a
  shift of an odd number of pixels matched nowhere, so the block got another motion or none), and anything
  faster than 16 pixels fell back to the real frame. It now goes from coarse to fine: the frame and the one
  before are kept at a half and a quarter of the console's size (averaged, so a texel's step still matches);
  the quarter-size level is searched 10 texels each way (40 console pixels), the half-size level 2 texels
  around that answer, then the two refinements on the full picture as before. The motion images hold pixels
  as 16-bit floats (8 bits could not hold 40 pixels to the half pixel). On a CPU model with pictures of a
  natural spectrum, blocks within a pixel of the true motion: 5 px 80 % -> 95 %, 15 px 41 % -> 96 %,
  24 px 0 -> 88 %, 33 px 0 -> 88 %, 40 px 0 -> 68 %; on busier pictures 15 px 7 % -> 67 %. A still picture
  with dither noise still gets no motion. About the same number of texture reads as before, most of them now
  on the small pictures.
- Integer scaling (Settings > Video, "integer" in the settings files; off). The picture's height becomes the
  largest whole number of times the console's lines (the frame's height over the render scale) that fits the
  window, the shape kept, borders around it. With the Nearest upscaler each console pixel is a square of
  window pixels. A window lower than the console's picture is left as it is.
- Texture packs (Settings > Video: "Texture packs" and "Write the game's textures out"; "texpack" and
  "texdump" in the settings files; both off). A texture is known by a hash (XXH3-64) of its largest level
  as the game holds it, with its palette, format and size. With packs on, hle/textures/<title id>/<hash>.png
  in the data folder is drawn in its place: any size up to 4096 a side, with smaller levels made from it when
  the game's texture has them. With the dump on, each texture the game uploads is written once to that
  folder's dump/ under the same name, so a pack is made by editing or upscaling a dumped file and putting it
  one folder up. Plain textures only: no cube maps, volumes, video or signed (bump) formats, and the dump
  reads the 8888, 565, 1555, 4444, L8, A8, AL8, A8L8, P8 and DXT1/3/5 formats. A game that changes a texture
  every frame gets a file per version of it in the dump. The log names the folder at first use and the first
  pack files used. A host test wrote a texture out, read it back identical as a pack file and checked the
  hashes; decoding the game's formats and the replaced textures in a scene are not tested.

## 1.0.76.21 (Modern GUI fork)

- Frame interpolation: the in-between frame is presented once. While it was up, the window's loop took every
  turn as something new to show and presented it again, until the frame's own time came. Under V-Sync those
  presents queue behind each other (a swapchain of three images holds two), so the picture ran about two
  refreshes late on top of the half frame interpolation costs by design; without V-Sync the loop presented
  the same picture as fast as the GPU went for half of every frame. Now the loop waits until the frame is
  due (or another comes) and presents that. A model of the queue (60 Hz, a 30 fps game, three images) puts
  the frame on the display 18 to 32 ms after it arrives, from 52 to 66 ms; the same two presents a frame.
  Modelled, not measured: not run on a GPU.

## 1.0.76.20 (Modern GUI fork)

- Frame interpolation smeared low-contrast content that moves fast (a dim scene, fog, sky, distant ground in
  a camera turn). The motion search leaned toward "no motion" by a fixed amount a step (0.12 on the sum of 16
  luma differences), which is more than the whole difference such content has: it was given no motion, and
  the two frames, alike enough to pass the check, were blended one over the other. The lean is now a share of
  the difference (3 % a step, plus a trace to settle flat areas), so it weighs the same on a dim picture as
  on a bright one, and the check for showing the real frame instead is stricter (it also looks at the two
  samples themselves). On a CPU model of the search and the mix, content of 8 % contrast moving 12 px a frame
  went from 38 % error against the true half-way frame to none (52 % to 3 % with dither noise), content of
  4 % and 2 % contrast from about 100 % (a plain double image) to none (7 % and 15 % with dither); a still,
  dithered picture still gets no motion; high-contrast content is unchanged. Both shaders compile (Vulkan 1.1
  and 1.3); not run on a GPU.

## 1.0.76.19 (Modern GUI fork)

- Frame interpolation looked slightly smeared (reported on 1.0.76.18). Three causes, three changes: the
  motion search was accurate to 1 to 2 pixels, and the half-way frame averages two samples, so a motion a
  pixel off blurs it: the search now refines to half a pixel (two more passes over 64 samples a block);
  each pixel took its motion from a blend of the four nearest blocks', which dragged one block's motion over
  its neighbour's edge: each pixel now takes whichever of the four (or none) the two frames agree best on
  around it; and the two samples were read bilinearly, which blurs a half-pixel shift: they are now read with
  a Catmull-Rom filter. The fall-back to the real frame where the two disagree starts earlier. Checked on a
  CPU against synthetic motion (half-pixel shifts, an object moving against its background: the picture's
  edge sharpness against the true half-way frame went from 0.82 to 0.92, and the error was halved where the
  motion is a whole number of pixels); not run on a GPU. The search costs about half as much again per new
  frame.

## 1.0.76.18 (Modern GUI fork)

- SMAA: Settings > Video > "Anti-aliasing: SMAA" (off). The presenter's FXAA is a blur of the frame's
  edges; SMAA finds the edges and blends along their shape (the reference implementation by Jimenez et al.,
  MIT, with its two lookup tables, "High" preset), which keeps the picture sharper and smooths the long
  slanted edges FXAA leaves. It replaces FXAA when both are on, runs on each new frame at the render scale's
  size (the cost grows with it: three passes over the frame), and works with FSR, NIS and the downscale. The
  log says "SMAA ready" (or "unavailable ...: FXAA instead"). `smaa=1` in the game's file.
- Frame interpolation: Settings > Video > "Frame interpolation" (off). A game that runs at 30 fps on the 60 Hz
  refresh gets a frame between each two: when a new frame comes, the presenter finds how each 8-pixel block
  (8 x the render scale) moved since the frame before (a search of 17 x 17 shifts), draws the picture half-way
  along that motion, shows it, and half a frame's time later shows the frame itself. Where the two don't
  agree (a wrong motion, a cut, a flash) it shows the frame itself rather than a double image. The price is
  half a frame of delay (about 17 ms at 30 fps). It acts only on frames 24 to 100 ms apart (so not on a game
  at 60 fps), not under the in-game menu, and its cost is a search per block per new frame (heavier at
  higher render scales). It is not FSR 3's frame generation: it has no motion vectors from the game, so it
  can be wrong where the picture changes in ways a block can't follow. `interp=1` in the game's file.
- Neither could be run with a GPU here: the shaders compile (checked with the project's compiler), the
  motion search was checked on a CPU against a synthetic moving picture, and the code builds. How they look
  and what they cost on a real GPU is for the first run to say.

## 1.0.76.17 (Modern GUI fork)

- Steel Battalion: from the 1.0.76.15 log, the game found the controller, polled it and sent its panel
  report, and ran (23 fps at 4x render scale), but the log could not say whether the buttons reached the game
  or what it was waiting for. The log now has a "the title reads buttons ..." line whenever the buttons,
  switches, pedals, tuner dial or gear lever the game reads change, and a "lamps lit: ..." line whenever the
  game changes the panel's lamps (which buttons it is asking for). Nothing else changed.

## 1.0.76.16 (Modern GUI fork)

- Steel Battalion on a gamepad: the five toggle switches the cockpit start-up has you flip (filter control,
  oxygen supply, fuel flow rate, buffer material, VT location) had no button on the pad, so the mech could
  not be started with one. With Back held, the D-pad (up, right, down, left) and the left stick click now
  flip them in that order (comms and washing are keyboard only). Reported: the game ran but the mech
  would not start or walk with the pad. Not run on the game here.

## 1.0.76.15 (Modern GUI fork)

- Steel Battalion's controller. The game only plays with its own controller (levers, pedals, 40 buttons,
  gear lever, tuner dial), which is a USB device of its own, not a gamepad. xbased now has that device: for
  Steel Battalion (title 43430002) player 1's port gets it, and the keyboard and gamepad drive it (a gamepad
  layout for the controls a pad can reach, with Back held for the cockpit panel; every control on the
  keyboard: see README, "Steel Battalion"). `sbc=1` / `sbc=0` in the game's file forces it on or off, for
  Line of Contact or another title. Built from the Xbox dev wiki's description of the controller's reports
  and checked in a host test (tests/sbc), not run with the game here. The wiki's right pedal overlaps the
  tuner dial's byte, so that pedal's value is carried in the high nibble and the tuner in the low one; if a
  pedal or the dial reads wrong in the game, that is the first place to look. The log has the controller's
  first poll and the panel report (lamps) the game sends.

## 1.0.76.14 (Modern GUI fork)

- Over the Hedge's cutscene stalls, from the 1.0.76.13 log (its GPU trace now showed the frames): the
  stall is a flip stall that nothing ends. At the cutscene's first swap the game's frame ended with the
  end-of-frame trap, a flip increment and the flip stall, but without the swap trap that tells Direct3D's
  vblank handler there is a flip to do; the handler ran at every vblank with nothing to flip, the GPU stood
  at the stall, and the title spun on its fence for ever (the log: "flip stall: read 0 write 0 modulo 3,
  353 flips so far", every second). A flip stall that lasts 24 vblanks (0.4 s; a swap waits for one to
  three) is now ended as the handler's flip would have ended it: the read counter moves on ("the read
  counter moved on" in the log). This is why a fix that changed the timing (locks in the kernel, the
  drives' timing) got the game past one cutscene and not the next. Not run on the game here: if it
  stalls again, the log shows whether the limit fired and what came after.
- Autofix: a fix that got the game past its stall ends that chain, and the next stall starts a new one
  with four tries of its own. A game that stalled at several cutscenes used up the four tries of one
  chain on the first of them.

## 1.0.76.13 (Modern GUI fork)

- Over the Hedge's cutscene stalls, from the 1.0.76.12 log: with "locks in the kernel" kept, the game got
  from the intro to frame 12289 and stopped again at a later cutscene, in the same way as at the first
  (the main thread spinning in Direct3D on a GPU fence, the decoder at a flip stall the game's vblank
  handler never ends). The fix stayed on the whole run (the log's IRQL events are the kernel's own
  locks at work); it only moves the timing, and the stall is the same bug each time. That log's
  GPU event trace was all IRQL changes (a thread spinning on the kernel's own locks), so the flips and
  traps of the stall were not in it. The trace now leaves IRQL changes out, and every second of a
  flip stall the log says which indexes it waits on ("flip stall: read, write, modulo"). No fix for
  the stall yet: this is the evidence for the next one.

## 1.0.76.12 (Modern GUI fork)

- Autofix keeps the log of the run that stalled. The first restart of a chain used to overwrite
  xbased.log, and with it the evidence of the stall the fixes were tried for; it is now kept as
  xbased-before-autofix.log (the run's log, then the last trial's in xbased.log).
- "Reset this game's fixes" in the in-game menu also shows when the game's file only has fixes that were
  tried without use ("fixfail="), which would otherwise have been skipped for that game for good.

## 1.0.76.11 (Modern GUI fork)

- Autofix: a game that stalls is run again with another setting, and the one that works is kept. When
  no frame comes for 25 seconds (or none at all in 90), the emulator looks at what the threads and the
  render thread are doing (everything waits, something spins, the GPU is waited on, no frame ever) and
  picks the next switch from a fixed list that fits it: the kernel running the locks itself, the
  drives' own timing, reads in place, short thread slices, each of the audio, movie and Direct3D timer
  patches off, GPU commands one by one, 128 MiB of memory. A notice counts down 10 seconds (opening the
  menu, pausing or minimising cancels it), the game starts again with that one change, and a run that
  gets 900 frames past where the last one stalled, and doesn't stall, writes "fix=<name>" into the game's
  settings file, applied from then on. At most four tries a chain, each switch once; a try that stalls
  again is noted ("fixfail=") and not tried for that game again. In-game menu: "Reset this game's fixes".
  Settings: "Try fixes when a game stalls" (on).
- What worked, and for which kind of stall, is kept for every game (compat_learned.ini) and orders the
  tries for the next one: first what worked for a title of the same publisher, then what worked most.
- The log says, at the start, which kernel functions the title imports that this build lacks ("known
  gaps"): for those no setting helps.
- Not tested on hardware (the machine that built it has no graphics card): the decisions are tested on
  their own, the stall detection and the restart are the 1.0.76 code's that was there. A long loading
  screen that draws no frame for 25 seconds would be taken for a stall: cancel it from the menu, or
  turn Autofix off.

## 1.0.76.10 (Modern GUI fork)

- Marvel vs. Capcom 2's audio, from the 1.0.76.9 log: the music plays (one stream voice) and the game
  never starts a voice for a hit or for speech, in the title or in a fight. The APU is not stuck (not
  trapped, no missing notification); the game doesn't ask. So the stats now also show each guest
  thread's turns per second and CPU time (to see whether one of the game's helper threads never gets to
  run), and the audio line counts the methods the game writes to the APU, with the buffer changes among
  them, and names its columns (cbo, start, end, state).
- First release since 1.0.76.2 (the notes of 1.0.76.3 to here are in RELEASE_NOTES.md), with a Linux AppImage.

## 1.0.76.9 (Modern GUI fork)

- The log now has an "audio:" line every 3 seconds while a game's sound is active: how many voices the
  game started and stopped, how many notifications and interrupts the audio chip raised, the registers
  that decide the interrupt, and the voices playing (format, position, start). For Marvel vs. Capcom 2,
  whose title music keeps playing in a fight while the fight sounds never start, it shows whether the
  game starts the voices and whether they end. No fix for that yet: this is the evidence.

## 1.0.76.8 (Modern GUI fork)

- Marvel vs. Capcom 2: its main thread waited for ever in its heap lock, a critical section in the
  game's data that was all zero (never passed to RtlInitializeCriticalSection; a zero LockCount
  instead of -1 made the first entry look contended, a wait for an owner that doesn't exist). An
  all-zero critical section is now taken as free, and the log says so ("never initialized"). Not
  confirmed on hardware: if the game still stops there, the log shows what comes next.

## 1.0.76.7 (Modern GUI fork)

- The thread list a hang (or a crash) writes to the log now also holds the game's code: the bytes where
  each thread is and at the calls up its stack, the bytes of a critical section a thread waits on, and
  the code that made the kernel's latest calls. Enough to read from the log what a stuck game waits for
  (Marvel vs. Capcom 2: its main thread waits on a critical section while the game's own scheduler
  thread keeps resuming and suspending the other two).

## 1.0.76.6 (Modern GUI fork)

- When a game hangs (the "HANG started" lines in the log), the log now also lists every guest thread:
  what it waits on, where it is, its stack, and the kernel's last calls. It shows what a stuck game is
  waiting for (Marvel vs. Capcom 2 stalls after loading its shaders with every thread blocked).

## 1.0.76.5 (Modern GUI fork)

- Dr. Muto no longer crashes after its intro movies. Its code has MMX instructions with a 0x66 prefix
  ("66 0F DC 02", a saturating add); the Xbox's Pentium III ignores that prefix and does an 8-byte MMX
  add, but xbased read it as the SSE2 form, which needs 16-byte aligned memory, and the CPU faulted. A
  0x66 on an MMX opcode (without F2/F3) is now ignored as on the Xbox. Other games with such code
  were hitting the same fault.

## 1.0.76.4 (Modern GUI fork)

- Fixed the game library scan (1.0.76.3) finding nothing: it ran before the kernel's function table was
  loaded, so it saw no functions at all and reported "0 of 0". It now lists the missing ones.
- When a game crashes on a bad memory access, the log now also holds the bytes of the game's code
  around it (a "code from ..." line), so the instruction that failed can be read from the log alone.

## 1.0.76.3 (Modern GUI fork)

- New: Settings > System > "Scan games for missing kernel functions". It reads every game in your
  library (just the headers of each default.xbe, it doesn't start anything) and lists which Xbox
  kernel functions each one imports that this emulator doesn't implement yet. The report is saved as
  compat_report.txt in the data folder (the "Open the scan report" row opens it): games with the
  most gaps first, then each missing function with how many games need it. A game that stops with
  "kunimpl" in its log is one of these. It only finds kernel gaps, not graphics or timing problems,
  and a function being imported doesn't mean the game calls it.

## 1.0.76.2 (Modern GUI fork)

- The mouse pointer stays hidden while a game runs (it could come back after a message box, a focus
  change or moving the mouse), with the keyboard and mouse or a controller. A transparent pointer is
  set over the game as well, and it is set again several times a second (Windows leaves the pointer
  the dashboard showed on the game until the mouse moves, so a controller never got rid of it); the log says "pointer: hidden over the game" (and "showed again" if it
  ever has to hide it again).
- Fixed "your Xbox doesn't have enough free blocks" in games: with a hard disk size set in Settings >
  System smaller than what the E: folder already holds (10 GB set, 15 GB of homebrew in it), the
  drive read 0 MB free. The data drive now always has room, as it already did on the automatic size.
- Choosing "Keyboard, mouse and controllers" now turns the mouse on (it stays off if you switch
  "Mouse as a stick" off yourself). The mouse is the right stick by default, which moves the camera
  in a game; set it to the left stick in Settings > Controls to steer menus with it. Click the game
  to take the mouse, F3 or Esc to let go. The log notes when the mouse is taken, or when a click is
  ignored because the mouse is off.

## 1.0.76.1 (Modern GUI fork)

Everything from xbased 1.0.76, plus the Modern GUI changes: dashboard styles and themes, Insignia tab
and Setup Assistant, Homebrew tab and store, OG Dashboard tab, play time, Resume and auto-save,
covers (floating and 3D), per-game cover keys for games without a title ID, Nearest and NVIDIA NIS
upscaling (NIS: a GLSL port of NVIDIA Image Scaling's NVScaler, checked against the SDK on the CPU,
not yet on a GPU), GPU and disk-size settings, pause on focus loss, keyboard / mouse / controller
settings (mouse as a stick, layouts, quick save buttons), non-Xbox disc images skipped,
interface sounds off by default. The update check follows this fork's releases.

## 1.0.76 (2026-11)

### New
- A Vertical sync setting (Settings, Video), on by default. Off, frames are
  shown as soon as they're ready, with tearing.

### Fixes
- The Xbox dashboard (Settings, Online) took no controller or keyboard until
  the in-game menu was opened and closed.
- The library's bottom line shows the game's region and version, and two
  games with the same name show theirs on their tiles, so different releases
  can be told apart without opening them. The game's page lists its title
  ID, version, region, publisher, year and size under the cover, no longer
  running under the menu beside it. The separator between publisher and year
  no longer shows as garbled characters.
- The on-screen keyboard's highlight follows the mouse on mice that report
  small steps. It stayed on the key the pointer had left while the click
  typed the one under it.

## 1.0.75 (2026-11)

### Changes
- The shader caches are cleared the first time a new version starts, so
  each update builds its shaders again and none kept from an older version
  is used.

## 1.0.74 (2026-11)

### Changes
- The asynchronous shaders setting is gone: shaders are always built when
  they are first drawn. Built in the background, the draws waiting for them
  were skipped, which showed as flickering or broken graphics, and it didn't
  stutter less.

## 1.0.73 (2026-11)

### Changes
- Asynchronous shaders and building cached shaders before the game are now
  off by default, and an earlier setting of either is reset once. Timed
  in-game with the shader cache cleared (Fable, Burnout 3, Vice City, Prince
  of Persia: The Two Thrones), building shaders when they are first drawn
  stuttered no more than building them in the background, while
  asynchronous shaders skipped the draws that were waiting for theirs, which
  shows as flickering or missing graphics. Turning asynchronous shaders back
  on shows a warning about that.

## 1.0.72 (2026-11)

### Fixes
- Water and other bump-mapped effects (Fable's pond, Burnout 3's lake and
  many more) are drawn as on the console. Textures whose channels the game
  marks as signed are now read and filtered as signed values, and the bump
  matrix is taken in the order the GPU uses. Before, the water showed a
  streaked or grainy pattern.

## 1.0.71 (2026-11)

### Fixes
- GTA Vice City in 480p widescreen (NTSC consoles, 720 x 480) with Trails
  on drew its night sky as a solid colour, white or yellow, and smeared the
  picture. The trails pass now draws over the frame itself.

## 1.0.70 (2026-11)

### Fixes
- A PC whose graphics card has no Vulkan driver (or one too old) ran every
  game with its sound over a black screen, the reason only in the log. It's
  said at once now, in a message that tells what to update.

## 1.0.69 (2026-11)

### Fixes
- Games that share a title ID and version with another one (prototypes,
  homebrew, modded discs: He-Man with an Aliens vs Predator homebrew) were
  one game to the library: hiding one hid both, and only one of them was
  listed. Two discs are the same game now only if their names match too;
  each one is hidden, renamed or made a favorite on its own. What was set
  before still applies until it's changed.

## 1.0.68 (2026-11)

### Fixes
- Star Wars: Republic Commando took minutes to reach its title screen and
  played its LucasArts logo at about 5 fps: while it loads it shows empty
  frames as fast as it can, thousands a second, and xbased sent every one
  of them to the screen on its own, far behind the game. Frames that come
  faster than any monitor can show, with a newer one already waiting, are
  now left out (the game still draws them all), and the title screen is up
  in about 20 seconds with the movies at their normal speed. Games at a
  normal frame rate keep every frame as before.

## 1.0.67 (2026-11)

### Fixes
- Chase: Hollywood Stunt Driver froze on the last frame of its intro: as
  its menu comes up it resets the processor's time stamp counter (WRMSR),
  an instruction xbased didn't take, and the emulation stopped there. The
  model-specific registers are written and read now, the time stamp
  counter counting from 0 again when zeroed as on the console. The game
  reaches its menu and plays.

## 1.0.66 (2026-11)

### Fixes
- Linux: crackling sound, with the emulator never short of it. xbased asked
  the sound server (PulseAudio, or PipeWire's) for a 512-sample buffer, and
  it ran the stream every 128 samples (2.7 ms): the audio thread a little
  late and the server ran dry for a moment. The buffer is 2048 samples on
  Linux now, which took Forza Motorsport's crackles away on the machine
  that had them. Still crackling: NC_APU_SAMPLES=4096 (more delay).

## 1.0.65 (2026-11)

### Fixes
- A disc for North America and Japan both (a World release) always played
  as on a North American console, whatever the language: Capcom Fighting
  Evolution's World disc stayed Capcom Fighting Evolution in Japanese,
  where a Japanese console has it Capcom Fighting Jam (the game reads the
  console's region, not its language). With the language set to Japanese,
  Korean or Chinese, such a disc now plays as on a Japanese console.

## 1.0.64 (2026-11)

### Fixes
- CHD images made with `chdman createcd` (what most guides use, even for
  DVD games) said the game's file wasn't an Xbox disc image: only
  `createdvd`'s were read, and a CD's CHD keeps each 2048-byte sector in a
  bigger CD frame. Both are read now, from an ISO or a raw .cue/.bin, the
  same bytes as the image they were made from.

## 1.0.63 (2026-11)

### Fixes
- Forza Motorsport stalled while loading from a slow drive (a USB hard
  disk): it sets the size of each of its cache files before filling them,
  and on Windows that wrote every one of the new bytes as zeros, the whole
  console waiting meanwhile (up to 23 seconds at a time, 70 in all). A
  file now takes its new size at once on any drive, its new part reading
  back as zeros as before.

### Other
- The log says which audio driver plays the sound and the period the
  device really runs at (PipeWire can run every app at a shorter period
  than xbased asks for).

## 1.0.62 (2026-11)

### Fixes
- Dave Mirra Freestyle BMX 2 crashed right after its intro, as its title
  screen came up. It reads a little past the top of its stack there (an
  index it never set), which on the Xbox lands in memory the console's
  kernel keeps over every thread's stack. Every thread's stack now has
  that memory over it too: the game reaches its menus and plays.
- The widescreen database's longer patches were cut at 96 bytes, written
  only halfway: Phantom Dust's 16:9 patch left a function half moved and
  the game closed as it started. Patterns of any length the database has
  (up to 337 bytes, 15 games' patches) are written whole now.

## 1.0.61 (2026-11)

### Fixes
- Insignia never got past the first connection on Windows: as soon as a
  game or the Setup Assistant opened a connection to the Internet, the
  network stopped (Windows refused a kind of request the network asked
  for). Insignia's Setup Assistant now registers the console, and the
  Xbox dashboard signs in to Xbox Live through Insignia.
- The gamepad works with xbased's window in the background: the menu (and
  games) take it with another window in front, with no click on xbased
  first.

### New
- Settings › Online › Open the Xbox dashboard (with Insignia chosen): the
  Xbox's own dashboard, for creating or recovering an Xbox Live account on
  Insignia. Its files (xboxdash.xbe and its folders, from your own Xbox)
  go in hdd/Partition2, the hard disk's C: drive. It starts with the tray
  empty.

## 1.0.60 (2026-11)

### Fixes
- Conflict: Desert Storm and Conflict: Desert Storm II crashed before
  showing anything. Both ask for a block of memory before the graphics
  start and write a marker just past its end. xbased handed out the very
  last bytes of the console's memory for it, so that marker landed
  outside it. The Xbox keeps the top 84 KiB of its memory for the
  graphics chip from the moment it boots, and now xbased does too: both
  games boot, reach their menus and play.

## 1.0.59 (2026-11)

### Fixes
- AMD FSR 1 filled the picture with noise and a diagonal seam on AMD
  graphics cards at 1x and 2x internal resolution (the sizes FSR scales
  up from), with FXAA on. Two of the passes that draw the picture into
  the window shared one set of inputs, and AMD's driver read the later
  one for both, so FXAA sampled the image it was drawing into. Each pass
  has its own now (the Vulkan validation layer reported the old way as an
  error, and reports nothing now).

## 1.0.58 (2026-11)

### Fixes
- Games never saw the component (HDTV) cable xbased reports: the value
  Xbox games read for the cable plugged in at boot was left empty, which
  means SCART. So the 480p, 720p and 1080i settings were dropped before
  games saw them, and a game that only goes widescreen in those modes
  stayed 4:3 and got stretched. X-Men Legends now runs at 720p in
  widescreen (or 720x480 widescreen with only 480p on), as on an Xbox,
  and other games now use the HD modes turned on in Settings.

## 1.0.57 (2026-11)

### Fixes
- Star Wars: KotOR II stayed on a black screen at the T3-M4 scene on
  Peragus, after T3's part in the fuel depot. The game needs its disc read
  at the speed of the Xbox's DVD drive there (players hit the same black
  screen on the Xbox when they played it from the hard disk). Every
  edition of KotOR II now reads its disc at the drive's speed by default.

### New
- Disc speed in a game's settings: Fast (as fast as the PC reads, the
  default), Close to the console's, or Like the console, for games that
  break when their loads finish too soon.

## 1.0.56 (2026-11)

### Fixes
- No game booted on a Windows with "Randomize memory allocations
  (Bottom-up ASLR)" turned off in Exploit protection: Windows put its own
  memory where the game's program has to go, and every game said its
  default.xbe couldn't be loaded. xbased now runs its games with that
  protection on for their own process, without changing any setting,
  and if the memory is still taken the message says what to turn on.
- The "Add games" card in the icons view was drawn at the size of a
  cover's, spilling out of its tile. It's laid out like a game's tile now.

## 1.0.55 (2026-11)

### Fixes
- A folder added while the library was still being read was never read
  at all until xbased started again. It's read now, whenever it's added.
  The same went for swapping one folder for another in Settings.
- A game whose file was moved or renamed (and the second copy of a game,
  when the copy that was shown went away) vanished from the library until
  xbased started again. Now the library follows the file.
- Reading a big library is much faster: each game's executable is no
  longer read whole for its name and icon, only the few kilobytes those
  are in. On a hard disk that took minutes for a few hundred games.
- Pressing A on one of the game folders in Settings, Interface took it
  out of the library at once. Taking a folder (or a game file) out now
  asks first.
- Games in subfolders were read once for each folder level they were
  under (a library with a folder a game read every game twice).
- Changing xbased.ini by hand while xbased was open was undone when it
  closed, as it wrote its own copy over the file. It reads the file again
  when it changes now, and a folder added there is scanned at once.

### New
- The library loads in the background and is playable meanwhile: a
  counter in the corner says how many games are in so far, and the
  selection stays on its game as others come in. A game started while it
  loads plays as usual (the library reads gently meanwhile).
- The library is kept: the next time xbased starts, every game shows at
  once, before the folders are checked again. A disk that isn't plugged
  in keeps its games for when it is.
- Search: Y (Tab, F3 or Ctrl+F) on the Games screen. Type the name with
  the keyboard or, with a gamepad, on the on-screen keyboard. It matches
  the game's name and its file's. The same screen filters by publisher,
  year and region, and sets the order and the view. B on the Games screen
  clears the search.
- Order by name (numbers first, then A to Z), by publisher or by year.
  The triggers (Z and C on the keyboard) jump to the next letter,
  publisher or year, which shows big for a moment.
- Three views: large covers (as before), small covers (about four times
  as many on the screen) and icons with names, publishers and years. The
  View button (V) switches between them.
- Favorites: Add to favorites on a game's page. They come first, A to Z,
  with a star.
- The last game played comes first.
- Both can be turned off, in the search screen or Settings, Interface.
- A game's page shows its publisher and year.

## 1.0.54 (2026-11)

### New
- A file browser of xbased's own for adding games, made for the gamepad
  and just as usable with the keyboard or the mouse. Before, adding games
  opened the system's file dialog, which a gamepad can't drive.
  - Open it from the new **Add games** card at the end of the Games grid
    (or with A when the library is empty), or from Settings, Interface.
  - On the left are your places: the library's folders, your home,
    desktop, documents and downloads folders, and every drive with its
    name, file system and free space. On Linux that's the disks your
    desktop mounts (/media, /run/media), anything mounted in /mnt and
    any other real disk, from ext4, Btrfs and XFS to FAT32, exFAT, NTFS
    and network shares.
  - On the right is the folder you're in: its folders first, then its
    disc images. Y shows the other files too. Hidden and system folders
    stay out.
  - A opens a folder, or adds a single game image to the library after
    checking it really is one. X (or the Add this folder button) adds
    the folder you're in. Adding a folder that holds folders or files
    already in the library merges them into it. Everything that's
    already in the library is marked as such.
  - B goes up a folder and keeps your place in the list, left and right
    move between the two sides, LB and RB page through long folders.
    With the mouse, a click opens, the wheel scrolls and a right click
    goes back.
  - Folders are read in the background, so a slow USB disk or network
    share doesn't freeze the dashboard, and the browser opens where you
    left it last time.
- The same browser replaces the other file dialogs too: playing a game
  file without adding it, choosing a game's cover image, and finding
  xemu.toml for importing xemu saves.

## 1.0.53 (2026-11)

### Fixes
- Test Drive no longer hangs on its loading screen before a race. The
  race executable keeps a slot for each of its data archives, and after
  reading an archive's header it closes the file but keeps the handle in
  the slot, then closes that same handle again when it frees the slot.
  xbased always gave out the lowest free handle number, so by then the
  number belonged to the game's loader thread, and the game lost its
  loader and waited for it forever while it filled up memory. Handles
  are now given out in turn around the table, so a number that was just
  closed isn't handed out again straight away.
- Test Drive's loading screen shows its Pong inside the dashboard screen
  again instead of on black. The game draws it over the menu's last frame
  without clearing it, relying on the console keeping its memory when it
  reboots into the race executable. xbased started each executable with
  clean memory; what the last one left in memory is now carried over, and
  a block the next one allocates there gets it back, as on the console.
- The frame a game keeps on screen for its next executable is now also
  saved when the game has shut its GPU channel down before the reboot.
  Thanks to BloodRaynare for the report (#88).

## 1.0.52 (2026-11)

### Fixes
- RalliSport Challenge no longer says the console doesn't have enough
  free blocks when you create a profile. Before saving one, the game
  makes a file named with a single space in its save folder. The
  console's file system takes that name, but Windows drops spaces at the
  end of a name, so the file became the save folder itself, creating it
  failed and the game took that as a full disk. Names Windows can't hold
  (ending in a space or a dot, or a device name like CON or NUL) are now
  stored on the host with those characters written as =XX, and the game
  sees them exactly as it named them. Profiles now get created and saved,
  races start, and the profile is still there after restarting. Thanks to
  Archive82-star for the report (#92).

## 1.0.51 (2026-11)

### Fixes
- FIFA World Cup: Germany 2006 no longer freezes at start on every boot
  after the first. On its first boot the game copies its data to a cache
  drive, and from then on a helper thread checks that cache while the
  game starts. The game pauses that helper by waiting for it to reach a
  safe point, giving up the CPU in the meantime, but the helper runs at a
  lower priority and the emulator only ever handed the CPU to threads of
  the same priority or higher, so it never got to run and the game waited
  forever. Giving up the CPU now lets a lower priority thread run, as on
  the console. Checked over five boots in a row with the cache in place,
  each reaching the intro. Thanks to RiasatSalminSami for the report (#89).

## 1.0.50 (2026-11)

### Fixes
- Steadier frame pacing on high refresh rate and variable refresh rate
  displays (G-Sync, FreeSync). xbased used to send each frame out on the
  first refresh of the display after it was ready, so at 60 fps on a 144
  or 280 Hz display frames went out anywhere from 8 to 26 ms apart. A
  variable refresh rate display refreshes when each frame arrives, so its
  refresh rate swung all over the place and the game stuttered even at a
  locked 60. On these displays frames now go out as soon as they're ready,
  at the game's own pace (16.7 ms apart, give or take half a millisecond),
  and the emulated console's clock no longer tries to follow refreshes
  that aren't the display's real ones. On regular 60 and 120 Hz displays
  nothing changes. Thanks to RiasatSalminSami for the report (#90).

## 1.0.49 (2026-11)

### Fixes
- xbased now copes much better with slow drives, like external USB hard
  disks and SD cards. Everything the emulated hard disk did on the host
  ran on the emulated console's own CPU, so a slow drive held the whole
  console while a game wrote to it. That's what froze Project Gotham
  Racing 2's intro for minutes while it filled its cache. Now writes go
  to a background thread like reads already did, so the game thread that
  writes waits and the rest of the game keeps running. The hard disk also
  has its own thread next to the DVD drive's, each write is a single
  request instead of one per memory page, and wiping a cache drive
  happens in the background. Files on the cache drives (X, Y and Z) are
  also no longer forced to disk one by one as they're closed, since the
  console treats those as disposable anyway. Saves still are. Thanks to
  shadowflash for the report (#83).

## 1.0.48 (2026-11)

### Fixes
- Loading a savestate no longer restarts the game after a few loads.
  Every time xbased restarts itself (to load a state, or when a game
  switches to another of its executables) it handed the new run all of
  its settings again on top of the ones the new run reads anyway, so the
  list kept growing until it was full and the last entries were lost,
  among them the state to load. Some games that switch executables, like
  Chicago Enforcer, could even end up restarting over and over. Now only
  the options given on the command line are passed on, and loading the
  same state five times in a row goes back to it every time. This also
  covers loading from the in-game menu (F1). Thanks to antonio3687 for
  the report (#81).

## 1.0.47 (2026-11)

### New
- Stick anti dead zone, in Settings, Controls, Stick range, and for each
  game in its own Controls. Many Xbox games ignore the first part of a
  stick's travel themselves (they were made for the original controller's
  loose sticks), so even with the dead zone at 0 a small push does
  nothing. In Chicago Enforcer, for example, the view doesn't turn at all
  until the stick is about a third of the way over. The anti dead zone
  makes the smallest push past the dead zone start at that amount, so
  the game reacts straight away. Both stick boxes on the Stick range
  screen draw it as a red disc, next to the dead zone's grey ring.
- A game's Controls now have its own sticks (dead zone, anti dead zone,
  range, shape) and triggers, so each game can be set apart from the
  global settings. Thanks to crashmidnick25 for the report (#84).

## 1.0.46 (2026-11)

### Fixes
- Less audio stutter on Linux in games with a lot of sound going on,
  like Forza Motorsport's races. The emulated sound chip's voices were
  split between the audio thread and a helper thread, and on Linux
  waking the helper and waiting for it 1500 times a second cost more
  than the work it took over. In a Forza race the audio thread was busy
  about three times as much on Linux as on Windows on the same machine,
  and on slower laptops it couldn't keep up. On Linux the audio thread
  now does the voices itself, which takes it from around 20% busy to
  around 7% in that race. Windows keeps the helper, which pays off
  there. Thanks to saultsomy-jpg for the report and the detailed log
  (#82).

## 1.0.45 (2026-11)

### Fixes
- Chicago Enforcer no longer freezes when starting a new game. Its
  loading screen works with events it opens by name, and the emulator
  had no way to open an object by name, so it stopped right there. Named
  events, semaphores and mutants are now supported, and a name goes away
  with the last handle to it, as on the console, since the next loading
  screen waits for exactly that. The game now gets through both loads
  of the first chapter and plays. Thanks to P1Login for the report (#56).

## 1.0.44 (2026-11)

### Fixes
- The updater on Windows no longer gives up on an update that unpacked
  fine. It unpacks the new version with Windows' own tar, and on some
  Windows 10 machines tar reported an error even though every file came
  out right, so the updater stopped and opened the releases page
  instead. Now, when tar reports an error, the updater checks every
  file of the package against the download (its name and size) and
  carries on if they're all there. Thanks to shadowflash for the report
  (#76).

## 1.0.43 (2026-11)

### Fixes
- Crimson Skies: High Road to Revenge no longer goes back to the
  dashboard as it starts. Some games check that the disc is an original
  by reading its security sector, which lives in a part of the disc that
  disc images don't include, and the emulator handed back zeros. The
  emulated DVD drive now makes a security sector that passes the game's
  check (the right time stamp and checksum, signed with a key of
  xbased's own, which the emulated console uses as its public key). The
  game now boots and plays. Thanks to BlueDragon992 for the report (#80).

## 1.0.42 (2026-11)

### Fixes
- The console's language and video settings are now kept in step with
  the console's EEPROM (hdd/eeprom.bin), both ways. Before, the
  emulator wrote its own settings over the EEPROM every time a game
  started, so editing eeprom.bin (as you would in xemu) did nothing.
  Now a change made in eeprom.bin, by hand or by a game saving its
  settings, shows up in Settings, and a change made in Settings is
  written to eeprom.bin. The file also no longer picks up what was only
  meant for one game, like that game's own language. Thanks to joesan09
  and ArhumMK for the reports (#43).
- A game's own language (its Game settings) is now always kept. Games
  whose disc has an English folder but none for the chosen language
  still fall back to English when the language comes from the global
  setting, because some of them break otherwise, but choosing the
  language for the game itself now overrides that.
- In Game settings the language is either the global setting or a
  language of its own. The separate Auto there, which followed the
  interface's language rather than the global setting, is gone. In
  Settings, Auto now says which language it gives.

## 1.0.41 (2026-11)

### Fixes
- Sound no longer stutters and pops now and then, mostly while a game is
  loading. The emulator kept only one block of sound ready ahead of what
  the sound system takes at a time, so if it was a few milliseconds late
  (the computer busy with a game's loading) the sound ran dry for a
  moment and came back with a click. It now keeps two blocks ready, sized
  from what the sound system actually asks for rather than what it was
  asked for at the start. That matters most on Linux, where PulseAudio
  and PipeWire can take bigger blocks than asked: with Burnout 3 on
  PulseAudio taking 43 ms blocks, gaps went from over two thousand in
  three minutes to one or two. Sound now runs about 10 ms further
  behind the picture. Thanks to saultsomy-jpg for the report (#77).

## 1.0.40 (2026-11)

### Fixes
- XIII no longer goes to a black screen after the logos. Its engine
  catches an error in every function it passes through and throws it
  again from there, so one error at boot ends up nine exceptions deep,
  each still being handled when the next starts. The emulator kept at
  most eight per thread and stopped the game past that. It now keeps up
  to 64, and XIII reaches its menus and plays. Savestates made with older
  versions still load. Thanks to P1Login for the report (#64).
- Operation Flashpoint: Elite no longer goes to a black screen at boot.
  It writes its cache files with gathered writes (one write from many
  separate memory pages), a kernel call the emulator didn't have yet.
  It's in now, and the game reaches its menus and missions. Thanks to
  P1Login for the report (#63).
- Total Immersion Racing no longer closes the emulator at its first logo
  video. Reads a game starts in the background (overlapped reads) now
  take a little time, as the console's drive does, instead of finishing
  almost at once. The game counts on its first video read still being
  under way when it first checks, and when it wasn't, the video's sound
  was set up with no channels and the game divided by zero. Background
  reads still run four times faster than the console's DVD drive. The
  game now plays its videos and reaches its races. Thanks to P1Login for
  the report (#65).
- Hitman 2: Silent Assassin no longer goes to a black screen at boot.
  It edits the console's page tables to see its memory at a second
  address, by copying the entries the console's kernel keeps for its
  RAM. The emulator gave it page tables without those entries, so the
  copy came out empty and the game's first use of that memory failed.
  The page tables now start the way the console's do, and the game
  reaches its menus and levels. Thanks to P1Login for the report (#69).
- On Windows, a game dividing by zero now leaves a line in the log
  saying where, instead of the emulator closing without a word.

## 1.0.39 (2026-11)

### Fixes
- Sniper Elite no longer stops on its first screen the first time it
  runs. It copies its sound bank to the cache drive in writes that skip
  the file system's cache, and waits for each one to be reported as
  pending before it moves on. Those writes finished at once here, so the
  game sent the same first block again and again forever. They now pend
  as they do on the console's hard disk, the 135 MB copy finishes in a
  few seconds, and the game plays. Thanks to P1Login for the report (#62).

## 1.0.38 (2026-11)

### The interface over the game
The interface is now drawn by the game's own presenter, over the game's
picture, so the window never changes hands while you play.

- The in-game menu comes up over the paused game, blurred behind a
  translucent veil, and fades in and out. Before, the presenter let go of
  the window and another renderer drew the menu over a still picture.
- Changing the screen shape in the menu shows at once on the picture
  under it, and the picture eases into its new shape instead of jumping.
- Saving a state and taking a screenshot show a short notice over the
  game.

### Overscan
Settings > Video (and each game's settings) have an Overscan row, for any
screen shape, 4:3 as much as 16:9. As on a TV, it's the share of the
picture hidden past the screen's edges: 3% to 15%, where a CRT hid some
5 to 10% (7% and 10% are the action-safe areas). The negative values
underscan, leaving that much border. It shows at once, under the in-game
menu too. Thanks to P1Login for asking (#67).

### Transitions in and out of games
- Starting a game: the dashboard breaks into shards that fly at you, and
  the game comes in out of a white flash. Quitting to the dashboard or
  restarting breaks the game's picture the same way, and the dashboard
  comes back in out of black. Each takes half a second.
- Loading a state, or a game switching to another of its executables:
  the picture stays on screen until the new one is ready, then fades into
  it. The next window opens under the current one, which closes only once
  the new one has drawn, so there's no desktop or blank window between
  them. A window keeps its place and size.
- Settings > Interface > Transitions in and out of games turns the
  effects off.

None of this runs during gameplay. Checked against 1.0.37 on Conker's
same scene: the same frame rate unlocked (840) and at Original (59.9, one
dropped frame in each), and the same presenter timings.

## 1.0.37 (2026-11)

### Patches and cheats only for the build they were made for
A game's releases can share a title ID and still have executables built
apart (another region, a later printing), and a patch or cheat writes to
fixed places of the one it was made for. xbased now tells them apart by
the executable itself: its certificate's regions and its build time.

- The shipped IPS patches (60 FPS, widescreen, 480p) were made and tested
  on the North American releases. They're offered only on an executable
  sold there, and only where each record fits its code. Grand Theft Auto:
  Vice City's European release crashed with its 60 FPS patch, which cut
  an instruction in half; it's no longer offered there. Patches you add
  yourself are offered as before.
- Cheat files: nearly all compare the build they were made for before
  they patch anything. xbased reads those builds from each file (plain or
  XOR-obscured) and lists a file only on its own build. Burnout 3's USA
  disc lists its four trainers and none of the three for the later
  printing. 36 files that patch without checking the build are out of the
  collection, and an update deletes them while they're unchanged.

Checked in game with the frame rate at Original: the 60 FPS patches of
Driv3r, Rainbow Six 3, Prince of Persia: The Sands of Time and The Thing
go from 30 to 60, and the widescreen patches of Conker and The Sands of
Time widen the view.

## 1.0.36 (2026-11)

### Fixes
- Grand Theft Auto: San Andreas (European Classics release) crashed while
  loading with its 60 FPS patch on. The shipped patches (60 FPS,
  widescreen, 480p) were made for the USA releases and write bytes at fixed
  places of the game's executable; given to other releases of the same game
  they landed on other code (in San Andreas, half of a call). They now go
  only to the USA release each one was made for: 45 copies on other
  releases are gone, and an update deletes them from the mods folder at
  start (only while they're unchanged, never a patch of your own).

## 1.0.35 (2026-11)

### Sound
The voice processor now follows raddad772's measurements of the audio chip
on a retail console (xemu pull request #3105):

- 3D sound (HRTF): the filter taps are read as the chip's small floating
  point numbers, so 3D voices no longer come out several times too loud and
  lopsided between ears; the left and right ears go to their own routes, a
  3D voice plays mono, and new HRTF settings blend in across one frame.
- Voices are resampled with the chip's 8-point interpolation instead of a
  linear one: cleaner highs, no aliasing.
- Volume and pitch changes ramp across each frame instead of stepping (no
  clicks or buzz on envelopes), with the chip's volume curve and envelope
  timing.
- The ParaEQ and I3DL2 filters and the voice LFOs (vibrato, tremolo) work;
  the DLS2 filter has no extra input cut or distortion.
- DirectSound submixes (multipass voices) are no longer up to 42 dB too
  quiet, and a voice sharing a page number with an output buffer no longer
  goes silent.
- Savestates from earlier versions load; their voices fade back in over
  one frame.

## 1.0.34 (2026-11)

### Fixes
- Spikeout: Battle Street said its program was damaged and didn't start.
  Its executable has 398 sections and the loader took no more than 256; it
  now takes up to 512.
- That error message said hakuX where it should say xbased.

## 1.0.33 (2026-11)

### Fixes
- Rainbow Six 3 crashed back to the dashboard while loading the first
  campaign mission. At boot it rewrites the C runtime's malloc, calloc and
  free to use its own allocator, and malloc had already been translated, so
  the old version kept handing out the system heap's memory while free sent
  it to the game's allocator. Changing a range's protection (as a game does
  before and after patching its code) now drops the code translated from
  it.

## 1.0.32 (2026-11)

### Fixes
- SNK vs. Capcom: SVC Chaos went back to the dashboard after the Neo Geo
  screen. It makes thousands of small contiguous memory blocks at boot, and
  past 4096 of them every allocation failed even with plenty of memory free,
  so a disc read went into a block it never got. The kernel now holds as
  many blocks as memory has pages, so only running out of memory fails, as
  on the console.

## 1.0.31 (2026-11)

### Fixes
- StarCraft: Ghost's 2003 build froze on its title screen with 128 MB of
  console memory. D3D's push buffer went at the very top of memory, past
  the end of the area the GPU reads commands from (D3D leaves the top 20 KB
  of a development kit's 128 MB to the GPU), so its last commands never
  ran. With 128 MB those 20 KB now stay out of every allocation, as on a
  development kit.
- Development builds that import the debug monitor (xbdm.dll) no longer
  stop at the first call into it: its DmRegisterCommandProcessor is a stub
  that reports success (StarCraft: Ghost's GhostR.xbe stopped at its first
  video). Other xbdm imports are listed in the log.

## 1.0.30 (2026-11)

### Fixes
- WWE WrestleMania 21 stayed on a black screen at boot. It copies its
  1024 x 512 legal screen whole into its 640 x 480 front buffer, and the rows
  past the buffer's end ran over the commands it had just sent the GPU (and
  a page of its own code), so nothing after them ever ran. A copy whose rows
  are wider than the render target it goes into now stops at the target's
  edges, which leaves the target exactly as the console's copy does.
- Prince of Persia: The Two Thrones' menus and gameplay were washed out to
  a bright green and yellow haze. Its glow shrinks the frame in place,
  drawing each smaller copy into the corner of the same target while it
  reads the bigger one, and those reads were taken as each pixel reading
  itself, so nothing shrank and the frame was added over itself. A draw now
  reads its own target that way only where the texture is the target's
  full size.

## 1.0.29 (2026-11)

### Fixes
- Hunter: The Reckoning - Redeemer crashed at the copyright screen and
  stayed black (on every boot once its cache was on the hard disk). Its back
  buffer is a supersampled 640 x 960 one, bound before the video mode is
  set, and the two 640 x 480 front buffers cleared then were taken for
  640 x 960. Writing one back to memory ran 480 rows past its end, over the
  game's sound data. Until the video mode is known a target is now as tall
  as it's drawn, not as tall as that first buffer.

## 1.0.28 (2026-11)

### Changes
- 1.0.27's change for Prince of Persia: The Two Thrones on AMD graphics
  cards is taken back. It wasn't tested on AMD hardware and it isn't what
  draws the blocks, so images read before anything wrote them behave as in
  1.0.26 again. The blocks are still being looked into.

## 1.0.27 (2026-11)

### Fixes
- Prince of Persia: The Two Thrones on AMD graphics cards: the screen
  filled with blocks of other images over the menus and the game. Its glow
  shrinks the frame through targets down to 10x7 and samples them before
  all of them are drawn; an image read before anything wrote it held the
  memory's leftovers, which NVIDIA's drivers clear and AMD's don't. Such an
  image now reads as zeros (a depth buffer as its far plane) on every GPU.

## 1.0.26 (2026-11)

### New
- Console memory, 64 MB (retail) or 128 MB (debug kit), for all games or
  one (Settings › System, or a game's settings): betas, demos and debug
  builds made for a debug kit's 128 MB (StarCraft: Ghost's beta, Halo 2's
  E3 2003 demo and others) get it. A game marked for 64 MB keeps 64, as a
  debug kit runs it (given 128, a retail Halo drew nothing). The GPU
  reports the memory as well. On the command line: `ram=128`.
- Play a game file without adding it to the library: Settings › Interface
  › Play a game file, or drop the disc image on xbased's window.

## 1.0.25 (2026-11)

### Fixes
- 3D sound (Halo, Grand Theft Auto San Andreas and every game with
  positional effects and voices): muffled, smeared and too quiet. The
  console's HRTF filters, the head-related responses DirectSound loads
  for 3D voices, come as sign-and-magnitude bytes; they were read as two's
  complement, so a small negative tap (0x81, -1) became -127 and each
  response turned to noise across its 31 taps. Each was then divided by
  the sum of its taps' sizes, which left 3D voices 8 to 10 dB under the
  rest of the mix (dialogue among them). The taps are read as the
  hardware reads them and weighed as it does: the frontal response passes
  speech at about unity, and the sides show the head's shadow and the
  ear's resonance as they should. Savestates from before are converted as
  they load.

## 1.0.24 (2026-11)

### New
- A game with a frame rate patch of its own (60 or 120 fps, in Mods &
  patches) says so when the frame rate is unlocked: the patch is made for
  the game and keeps its speed right, where lifting the console's pace can
  break it. The frame rate option's help says so too.

## 1.0.23 (2026-11)

### New
- Cheats for 500 games: the Xbox scene's cheat files (.etm and .xbtf,
  the ones EvolutionX, Avalaunch and XBMC4Xbox ran as trainers) ship in
  `cheats/`, about a thousand of them, by title ID. A game's Mods & patches
  lists each file's options as `[cheat]` entries; switched on, the file's
  code runs once before the game's entry point and patches it, as the
  console's loader ran it. `.xbtf` files get the helper routines XBMC4Xbox
  loads for them. Your own files go in `cheats/<TITLE ID>/`.
- Over 160 widescreen, 60 fps and 480p patches ship in `mods/<TITLE ID>/`
  (icup321's and Slippy2k's XboxGraphicalPatches, matched to their games
  through xemu's title database; the 720p ones, which their authors call
  unstable, left out). They were made for the USA releases: a patch made for
  another release of a game can break it.
- Widescreen 16:9 (database) for 90 games: OgXHD's widescreen patterns,
  searched for in the disc's executable and written where they're found
  (their old bytes checked), so they hold across releases where the code is
  the same and do nothing where it isn't.

## 1.0.22 (2026-11)

### New
- CHD disc images (`chdman createdvd`, from an XISO or a full dump): in the
  library and on the command line, like an ISO, read through libchdr.
- Desktop shortcuts (a game's page › Create desktop shortcut): xbased with
  the game's image as its argument, which boots the game straight away, with
  the game's own icon. Windows: a shortcut on the desktop; Linux: a desktop
  entry there and in the applications menu. Steam and other front ends take
  the same command: `xbased "<image>"`.
- A game's page: Name (rename it; left empty, the disc's own name again),
  Choose cover image (a PNG or JPEG of your own as its box art) and Hide
  game. Settings › Interface › Show hidden games brings them back, dimmed.
- Settings › Interface › 12-hour clock (the dashboard's clock and the save
  dates).
- Downloaded content and title updates copied in by hand
  (hdd/Partition1/TDATA/<title id>/$c, $u) are signed for this console as
  the game starts. Signed for another console, or by a tool with another
  key, a game called them damaged.

### Fixes
- The frame rate counter wobbled between 58 and 62 at a steady 60: it
  counted frames in fixed half seconds, where one frame more or less is
  2 fps. It now measures the time between the frames themselves, over a
  second.

## 1.0.21 (2026-11)

### Fixes
- The King of Fighters: Maximum Impact (Maniax): a black screen at boot.
  The game closes each file by waiting on it until it is signalled, a
  file it read nothing from too. A file object's event was signalled only
  once an I/O of its own completed, so a file opened and closed unread
  was waited on forever. As on the console (the open's request completes
  on that event), a file is now signalled from its open: the game boots,
  plays its movies and reaches its menus and fights.

## 1.0.20 (2026-11)

### New
- Frame rate, per game (the game's Mods and patches): Original (the
  console's pace), Up to 60 fps, Up to 120 fps or Unlocked. The limits let
  a game draw faster than the console did but no faster than the chosen
  rate. Anything other than Original can break a game (its speed, physics
  or cutscenes), and the option's help says so. Fast forward lifts the
  limit while held. A game set to the old Unlocked stays Unlocked.

## 1.0.19 (2026-11)

### Fixes
- The Lord of the Rings: The Return of the King: walls and floors turned
  into a smear of another level's textures after playing for a while
  (a savestate loaded there showed them right). A texture is defined
  again when its memory is written, and that was seen through dirty
  page bits any other user of the same memory could take in first (another
  texture over it, a render target's copy). Each texture now also keeps
  its pages' write counts, which only grow, and is defined again when
  they change.

## 1.0.18 (2026-11)

### Fixes
- Disney's The Haunted Mansion: the cutscenes were black with their sound
  playing. The game copies each Bink frame into a frame buffer and draws it
  from there; the frame buffer's image was written back to memory as it was
  unbound, after the game had already written its next frame there (the
  command decoder runs behind the CPU), landing the older image over it.
  The CPU's writes now go into the image first.
- Settings: holding left or right on an on/off option changes it once,
  instead of repeating on to the next value.

### About
- xbased is an original Xbox emulator for Windows and Linux, developed by
  crazyriddler. Based on hakuX. The About screen, the executable's details
  and the Linux menu entry say so.

## 1.0.17 (2026-11)

### Fixes
- AMD graphics: Grand Theft Auto San Andreas' and Vice City's characters
  stretched into spikes. Their vertices are 9 bytes, 16-bit values at odd
  offsets, and AMD's vertex fetch reads a value only at an offset its size
  divides (NVIDIA's reads them anywhere). Such vertices are now copied with
  each value aligned, as they already were for Mali-G57.
- Windows: back from a game to the dashboard (the in-game menu's quit, or
  a game closing itself), the dashboard could still be left without the
  gamepad and keyboard until touched or clicked (an ROG Xbox Ally X). The
  game now brings the dashboard's window to the front itself as it ends,
  while it still has the foreground.

## 1.0.16 (2026-11)

### Fixes
- Windows: back in the dashboard after quitting a game from the in-game
  menu, the dashboard showed but had neither the keyboard nor the gamepad
  until clicked or Alt+Tabbed to. The game hid its window before handing
  the foreground on, and hidden it no longer had one to hand; it now hands
  it on first, and the dashboard takes the foreground as it comes back.

## 1.0.15 (2026-11)

### New
- Trigger calibration (Settings › Controls › Triggers, also from the in-game
  menu's settings): a bar per trigger shows its travel as you
  press it, with the dead zone and the full-press zone, beside what the
  game gets and how far it reached. Trigger dead zone (default 10%) and
  Full press at (default 100%: lower it for a trigger that no longer
  reaches its end). Past the dead zone a trigger now rises smoothly to full
  instead of jumping from nothing to a tenth.

### Fixes
- AMD graphics cards: geometry broken or missing in games with vertex
  programs (Def Jam: Fight for NY's menus black, with a stray band across
  them). AMD's drivers lack the 3-component 8- and 16-bit vertex formats;
  their 4-component forms were used, and the extra component, read past
  the array, became the position's w. It is now 1, as on the console.
- Windows: back from a game to the dashboard, or a game restarting itself
  (Crimson Skies after a mission), the window could end up behind others,
  its taskbar button flashing, as if minimized.

### Shader cache
- The compiled shaders are kept in one file per game (hle_spirv/<title
  id>.pack) instead of a file each: thousands of small files took far more
  disk than their size and were slow to open on a hard disk. The existing
  ones are packed into one file (legacy.pack) the first time, nothing to
  compile again.

## 1.0.14 (2026-11)

### Fixes
- Def Jam: Fight for NY: froze at the intro movie (black screen or a still
  frame, the game never reaching its title). Its 60 Hz input timer
  schedules each tick from the last through D3D's SetTimerCallback, which
  refuses a time already passed; one tick a frame late (the emulation held
  up a moment) and the timer stopped for good, and with it the game's
  updates. Such a tick now comes at once and the game catches up: it
  reaches the menus and plays.
- PTIMER's alarm: at a title's first alarm, its thread could start before
  the alarm was set and raise a stray alarm interrupt flag.

## 1.0.13 (2026-11)

### Fixes
- Prince of Persia: The Sands of Time: a band of stray colour (green,
  purple) down the right of the screen in the menus and in game. The game's
  glow adds four sizes of a blurred picture in one pass, each smaller than
  the image it sits in (the extra columns are never drawn); only the first
  was kept to its own size, and the others' filtering read the stale
  columns. Every texture of such a pass is now kept to its own size.

## 1.0.12 (2026-11)

### New
- Import from xemu (Memory, Y): xemu's game saves (its hard disk's E:
  UDATA and TDATA) copied into xbased and re-signed, so games accept them.
  Copied as they were, a game found them damaged: xemu's console signs
  saves with its own keys (from its BIOS and EEPROM), xbased with its own.
  xemu's settings (xemu.toml) are found where xemu keeps them, or chosen;
  its BIOS, boot ROM, EEPROM and qcow2 or raw hard disk image are only read.
  Saves whose game isn't in the library are copied but not re-signed: add
  the game and import again. xemu_import.log beside xbased lists each file.
- Add a game file (Settings, below the game folders, or Y in an empty
  Games screen): a
  disc image the folder scan doesn't find (another extension, another
  place) added on its own; it stays in the library.

### Fixes
- Halo: Combat Evolved: starting a campaign level closed the game back to
  xbased's menu. Halo lays out its game state and tags from 0x61000 to past
  16 MiB, and D3D's second page of visibility-test results has to be below
  16 MiB: the kernel kept 256 KiB of low memory it doesn't use (the Xbox
  kernel lets its start-up memory go), none was left, and the game read
  through the page it didn't get.
- Grand Theft Auto III and Vice City: a black screen at start. Their timer
  reads save the registers with the 16-bit PUSHA/POPA, which stopped the
  game; they run now.
- Quit from the in-game menu, and a game closing itself (a title opening
  the Xbox dashboard), could leave a black full-screen window that had to
  be closed by force: the process now ends at once, without waiting on the
  graphics, gamepad and audio drivers' shutdown.
- A game opening the Xbox dashboard's Xbox Live account page (creating or
  recovering an account) with no dashboard installed now says so instead
  of just closing.

### Dashboard
- Long messages and questions wrap over several lines instead of being cut.

## 1.0.11 (2026-11)

### Fixes
- Mission: Impossible - Operation Surma: a new game stopped on the "disc is
  dirty or damaged" screen. The game copies its levels to the hard disk's
  cache in the background, keeping each copy's file open and unshared until
  it is complete, and loads a level from the cache only when it can open it
  (from the disc meanwhile). Files on the hard disk now keep the share
  access their handles were opened with, as the Xbox's do: an open of a
  copy still in progress is refused, and the level loads from the disc.

### Picture
- Upscaling with AMD FidelityFX Super Resolution 1 (its EASU edge
  reconstruction and RCAS sharpening) in place of Snapdragon GSR: Settings ›
  Video › Upscaling › AMD FSR 1. A setting that chose GSR (global or a
  game's) now chooses FSR; bilinear stays bilinear.

## 1.0.10 (2026-11)

### New
- PAL games at 60 Hz by default, as a PAL console with its PAL-60 setting
  on runs them (Panzer Dragoon Orta, 50 fps before, plays at 60). Settings ›
  System › PAL games sets 50 Hz instead, for all games or one (its
  settings). A game without a 60 Hz mode stays at 50, as on the console.

### Fixes
- Driv3r: Tanner drawn with flat, skin-coloured legs and a bald head,
  where he has jeans and hair. The game's characters add
  an environment cube map through a texture stage whose coordinates their
  vertex program leaves unwritten; a PC GPU's look-up along no direction
  returned a bright texel, and the game's tint turned it into skin tone over
  the whole body. That look-up now gives black, and Tanner looks as he
  does on the console.
- Save states no longer depend on where xbased's folder is: they kept the
  full path of each file the game had open, so a state stopped loading once
  the folder moved (or reopened the old folder's files), and a Windows
  state didn't load on Linux. They now keep each file's place in the hard
  disk folder; states from earlier versions load too.
- Windows: no console windows flash as xbased starts (the update check and
  the box-art downloads ran curl in one each).
- The mouse pointer hides itself: hidden when xbased opens and during play,
  it shows when the mouse moves, and hides again after a few still seconds
  or when a key or a gamepad is used.

## 1.0.9 (2026-11)

### New
- Stick range, in Settings › Controls (and in the in-game menu's settings):
  a screen that shows both sticks live. A green square is the range the
  games expect (the Xbox controller's); a white outline is the range your
  stick reaches, as the game gets it, with its reach in percent. Rotate the
  sticks once around and adjust while watching it:
  - Range, 50% to 150%: above 100% for a worn stick that doesn't reach its
    edge, below for games that turn too quickly.
  - Shape: Square (Xbox), the default, or Round (the stick's own circle).
  - Dead zone, also here.
  While the screen is open, the stick doesn't move through the menu.

## 1.0.8 (2026-11)

### Fixes
- After a game closed, the dashboard's settings showed their defaults until
  xbased restarted, and a setting changed then wasn't saved. Reloading the
  settings file (a game may change it) lost its path on the way. The games
  themselves kept the right settings all along.
- Analog stick diagonals (Tony Hawk's Pro Skater 2x's tricks, a direction
  and a button, and other games of the time): a modern stick pushed fully
  diagonal reached only two thirds of each axis, where the Xbox controller
  reached nearly all of it, so games read a straight direction, or none. The
  stick now responds like the Xbox controller's: full deflection is full on
  both axes at any angle, and the dead zone is a circle around the centre
  instead of a band along each axis.

## 1.0.7 (2026-11)

### New
- Updates: as the dashboard starts, xbased looks for a newer release on
  GitHub and offers it: Update, Not now, or Don't ask again. Update downloads
  it and installs it over this copy (the Windows folder, or the Linux
  AppImage), then starts it; settings, saves, states and the hard disk stay
  as they are. Settings › Interface › Check for updates at startup turns the
  check back on (or off). A copy that can't replace itself opens the
  release's page instead.
- Audio output, in Settings › Audio: Stereo (the default) or 5.1 surround.
  In 5.1 the console reports Dolby Digital, the games mix for six speakers,
  and xbased plays the six channels (front, center, subwoofer, rear); on
  stereo speakers the system mixes them down. Stereo is now set whatever an
  older console image (EEPROM) had kept.

## 1.0.6 (2026-11)

### Picture
- Internal resolution up to 8x (5120x3840 for a 640x480 game), from 4x:
  5x and 6x for 4K screens, 7x and 8x as supersampling. A target too big for
  the GPU's largest image at the chosen scale renders at the largest one that
  fits. Fable plays at its full 30 fps at 8x.

## 1.0.5 (2026-11)

### Credits
- xbased is crazyriddler's Windows and Linux edition of hakuX by rfandango,
  and now says so everywhere: the About screen (in every language, with
  github.com/crazyriddler/xbased), the README, the program's version
  information on Windows and its desktop entry on Linux.

## 1.0.4 (2026-11)

### Fixes
- Fable: it stopped on a black screen as it booted. Its own memory pager
  leaves each page fault with an IRETD, which the core didn't run. Fable now
  plays.
- Games on a console set to a language their disc doesn't have (Spanish on
  Fable, The Thing, Call of Duty: Finest Hour...): they start in English, as a
  console would have them do. Swapping in the English files under the other
  language's name wasn't enough: Fable saw that language's folder missing,
  loaded half of it and crashed.
- The Thing: it stopped at boot. The kernel now has I/O completion ports
  (CreateIoCompletionPort, GetQueuedCompletionStatus,
  PostQueuedCompletionStatus), with packets from reads and writes of the
  files tied to them. The Thing plays.

## 1.0.3 (2026-11)

### Fixes
- Windows: the "disc is dirty or damaged" message in games that copy disc
  files to their cache on the hard disk (Panzer Dragoon Orta and others).
  Such a game writes each file under a temporary name and renames it while
  it is still open, which Windows refused: the cache stayed broken and the
  game reported a bad disc. Files are now opened so they can be renamed or
  deleted while open, as on the Xbox and on Linux. Panzer Dragoon Orta plays
  its stages.

## 1.0.2 (2026-11)

### Fixes
- Games that stopped forever on a loading screen while one of their threads
  waited for another of the same priority (NFL Fever 2004 before its
  matches): giving up the CPU now takes a step off a thread's wait boost, as
  on the Xbox, so the thread it waits for runs. NFL Fever 2004 plays its
  matches at full speed.

## 1.0.1 (2026-11)

Fixes for games that stopped with their "disc is dirty or damaged" message,
or closed at start.

### Fixes
- Windows: xbased in a folder whose path has an accent, an ñ or letters of
  another alphabet (`C:\Users\José\...`) put its virtual hard disk in another
  folder, or nowhere. Games that write their cache then stopped with the
  dirty-disc message, or closed. xbased now uses UTF-8 paths throughout.
- Windows: when xbased's folder can't be written (Program Files, a read-only
  copy, Windows' controlled folder access on Desktop or Documents), its data
  goes to `%LOCALAPPDATA%\xbased` instead of failing.
- Disc images trimmed too far by an image tool: reads past the image's end
  return zeros, as on the disc, instead of the dirty-disc message.
- Disc images whose directory tree an image tool left damaged: every file is
  still found.
- A disc read that fails is tried again, through a path the emulated
  console's memory protection can't refuse.
- Memory: an allocation too big to fit gives back what it took. Games that
  ask for all the memory they can get (BloodRayne 2) closed at start; they
  run now.

## 1.0 (2026-11)

The first release of xbased, crazyriddler's Windows and Linux edition of
[hakuX](https://github.com/rfandango/hakuX) by rfandango.

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
