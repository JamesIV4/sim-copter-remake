# Main-menu sky movie decode

Decoded 2026-08-01 from the original 1.0.1.0A executable and CD data. The installed/RIP copy is
not ground truth for this asset: it leaves `SMK/MENUSKY.SMK` at zero bytes and resolves the real
movie from the CD at runtime.

## Executable call path

`FUN_0044d070` is the complete main-menu movie setup:

1. `FUN_00432ab0(1, 0, "menusky.smk", local_104)` resolves the CD-data path.
2. `FUN_004617a0(0)` checks that resolved file.
3. `FUN_004d28b0(0x27c)` allocates the movie object and `FUN_00448170` constructs it.
4. Vtable `+0x54` prepares the movie object.
5. Vtable `+0x10(local_104, 0xfe000, -1)` opens `MENUSKY.SMK`.
6. Vtable `+0x24(DAT_00519cc0)` binds the current 256-colour display palette.
7. `movie+0x08 = 1` enables looping.

The remake cites this as `// SCHOOK: MainMenuSkyMovie 0x0044d070` in
`SSimCopterMainMenu.cpp` and `SimCopterHangarArt.cpp`.

## Recovered movie

The file is `SIMCOP~1/SMK/MENUSKY.SMK` on the original CD:

- SHA-256: `FB476FDA6AE8E8F3B0128C611DB96D143E4E5537CA5D66643D51E0796C90CB64`
- size: 2,917,772 bytes
- signature: `SMK2`
- dimensions: 640x480
- frames: 201
- Smacker duration field: `-7100`
- cadence: exactly 71 ms per frame (`1000/71` fps)
- loop duration: 14.271 seconds

The cyan areas are intentional compression mattes, not sky. They sit under the opaque
`MAIN1.BMP`, `MAIN2.BMP`, and `MAIN3.BMP` machinery. The largest rectangle that is visible sky in
every frame is `(427,29)-(640,315)`, between the right edge of `MAIN1` and the top of `MAIN3`.

## Unreal port and full-screen extension

Unreal does not decode Smacker at runtime. `Tools/Unreal/BakeMenuSky.py` verifies the header and
transcodes only the container/codec to H.264/yuv420p; it then verifies 640x480, 201 frames,
`1000/71` fps, and 14.271 seconds before replacing the generated file at
`SimCopterRemake/Content/Generated/Movies/MENUSKY.mp4`. Both the original data and generated media
remain gitignored.

`USimCopterHangarArt` owns the transient `UMediaPlayer`/`UMediaTexture`, opens the generated movie,
and applies the decoded loop flag. `SSimCopterMenuCloudBackdrop` draws the exact 4:3 movie centred
under the exact 4:3 menu art. To cover wider displays without stretching or cropping that original
composition, it repeats the live `(427,29)-(640,315)` sky crop behind the legacy frame. Only the
extra aspect-ratio margins expose the repeated layer.

## Verification

- `Tools/re-agent/.venv/Scripts/python.exe Tools/Unreal/BakeMenuSky.py`: passed; output verified as
  H.264, 640x480, 201 frames, `1000/71` fps, 14.271 seconds.
- `RebuildUnrealCpp.bat`: `Result: Succeeded`.
- `Automation RunTests SimCopter.FrontEnd`: all six tests passed, including
  `SimCopter.FrontEnd.MenuSkyLayout`.
- Not verified on screen; per `AGENTS.md`, the final visual check is left to the person at the
  keyboard.
