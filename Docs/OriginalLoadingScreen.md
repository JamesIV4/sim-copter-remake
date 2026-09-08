# Original city loading screen

`FUN_00410e90` loads `HRGLASS.SMK` through `PTR_s_hrglass.smk_004f83fc`,
centres the movie on black, and runs the city loader `FUN_00479bb0` on a worker
through `FUN_00411290`. The movie header is SMK2, 300x200, 75 frames, duration
field -10000: 100 ms/frame and a 7.5-second loop.

The original status rectangle is `(width/2-180,height-40,width/2+180,height-20)`.
At the user's request, the remake centres the text beneath the animation with
12 reference pixels of padding and scales it to half its initial size.
The assembly adds 0x275 to the one-based status counter and loads Windows string
resources 630..644. The strings include "Calibrating lag-lead hinges",
"Balancing swashplate", "Reticulating splines" and "Lubricating freewheel unit".
The original advances them from the loader's `DAT_005039d0` bits, not elapsed time.
The remake maps its map, city geometry, population and airport stages onto that
sequence; the original renderer's individual resource passes do not exist in UE.
The font is Slate's regular font; animation pixels, timing and wording are original.

Run `python Tools/Unreal/BakeLoadingScreen.py` with ffmpeg on PATH. It validates
the original header and losslessly packs all frames into
`SimCopterRemake/Content/Generated/Loading/HRGLASS.png` (gitignored original art).
The generated directory is staged NonUFS. Game builds require this bake, just as
they require the original runtime data. No original executable or SMK payload is
added to the package. All 75 atlas cells were compared against decoded original
RGB frames and matched exactly.

`USimCopterLoadingSubsystem` uses Unreal's MoviePlayer module to animate Slate
while map loading blocks the game thread. A viewport overlay serves PIE, where
MoviePlayer is unavailable. It allows engine ticking after the map loads, but
keeps the screen until `FinishStartupCamera` has placed/restored the player and
reset the camera. Input cannot skip it. Travel away and subsystem shutdown clear
the screen. The presentation scales uniformly for modern displays.

Build and headless tests do not verify the visible threaded loading transition;
that last check remains an in-game check.
