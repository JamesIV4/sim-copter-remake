# Original startup movies

The front end plays `INTRO1` followed by `INTRO2` once per game instance, before
starting the main menu and its music. Returning from a city does not replay them.
Any keyboard key, controller button, mouse button or touch skips the current movie.
Key repeats and releases are consumed; transitions are deferred to the next game
tick so multiple events in one frame cannot skip both movies. Missing or failed
media advances, and an open still pending after 15 seconds advances as well.

Original evidence: `.ghidra-exports/0044cbb0.json` (`FUN_0044cbb0`) opens
`intro1.smk` and then `intro2.smk`, with the second movie displayed at `(0,100)`
in a `640x280` rectangle. The remake centres that rectangle in a black 640x480
canvas and scales the canvas to fit the viewport. The source headers specify:

| Source | Resolution | Frames | Cadence | Audio |
| --- | --- | --- | --- | --- |
| INTRO1.SMK | 640x280 | 202 | 20 fps | None |
| INTRO2.SMK | 460x200 | 838 | 20 fps | Stereo, 11025 Hz |

To regenerate the user-provided, gitignored movie assets:

```powershell
python Tools/Unreal/BakeIntroMovies.py
```

The script validates the original headers and transcodes to H.264/AAC in
`Content/Generated/Movies/Intro`, preserving all frames and video timing. It
checks the output frame count, size, rate, duration, and audio presence/channels.
Audio is resampled to 44100 Hz for the media backend. The existing NonUFS staging
rule includes this folder; game-target builds also require both movie files and
declare them as NonUFS runtime dependencies. Fresh checkouts must run the bake
before packaging. No original SMK files need to be included in the package.

Playback uses the existing MediaAssets facilities plus the engine AudioMixer
module for a UI media sound component. The main-menu game mode closes playback
and destroys the audio component when leaving the intro, including console travel.
`SimCopter.FrontEnd.IntroInput` exercises keyboard, controller and all five mouse
buttons, including repeat/release suppression. Actual display, sound and device
input still require an on-screen check.
