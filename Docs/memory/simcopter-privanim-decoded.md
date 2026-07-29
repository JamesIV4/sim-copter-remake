# SimCopter privanim decoded

*privanim.df is FULLY decoded (2026-07-01): exact container spec, 21 named figures, skeleton trees, 18-clip maps, per-frame line-segment poses; old display rules were wrong*

`X/privanim.df` was fully, code-derivedly decoded on 2026-07-01 (Fable 5 pass). Authoritative spec:
`Docs/OriginalGameFileFormats.md` "Exact Container Spec"; parser `Tools/privanim_extract.py`
(rewritten - the old version's directory decode and ALL its "display rules" (t=8k+4 pairing,
z-bit7 split) were wrong: it read payloads from garbage offsets `id<<16|nameOff`).

Key facts (validated 437/437 chunks):
- DF "Doug" container: `@0 u32 dataBase(0x100)`, `@4 dirOffset`, `@0xc dirSize`; directory blob =
  8-byte section entries + 12-byte node entries `[u16 id][u16 nameOff][u8 flags][u24 chunkOff][u32]`
  + Pascal string table. Chunk at `dataBase+chunkOff` = `[BE u32 len][payload]`.
  Record-array payload: `[u16 recSize][u16 rows][u16 cols][2][rows*4][records]`.
  **people.df uses the same container/reader class** - reuse the `DougFile` parser for BHAV work.
- 21 figures by NAME: pilot swimmer fatman 2blonde Child 5.5man Coww SUIT Elvis Nessie 5man SHADES
  Kopp Medik Badguy Blonde 2DOGG 2woman Fireman TubaExpert Woman. Record arrays are keyed by
  name[3] substitution: 'c'=ARCP skeleton, 'L'=ARLU clip map, 'i'=ARPP poses (pilot->pilct/pilLt).
- ARCP (0x28/record) = skeleton tree: name/parent 4-char links ("New " root, Ne0..), f32 dims
  (w,h,0.5), type byte 0x08/0x0b/0x0e (draw type - still to decode), 29-88 parts per figure.
- ARLU = 18 records [mnemonic][clip]: 1Wal Inju HipH Whoa DgRn NoMo Tote Yumm 2Gab Dead Slum Wave
  1Run DgSt WvNo Thro Play FaCl -> clips "101!".."495!" (18 consecutive per figure).
- ARPP (8/record, NO byte swap) = frames x parts; each record = one body-part LINE SEGMENT
  [s8 x0 y0 z0][pad][s8 x1 y1 z1][pad], z up. Walk clips = 8 frames; verified: Child walks,
  Nessie is the serpent, legs alternate across frames.
- The "88-handler VM" `(&DAT_0058ef78)[op]` is dispatched from walker `FUN_004ce7b0`: 16-bit
  tokens <0x100 = opcodes (thunks at `0x4c84e0+0x20*n`, map in out_vm_handlers.txt), >=0x100 =
  4-char child-node links. The person object IS a walker instance (stack@+4, cursor@+0xf4).
  `FUN_004ce630(0xc,...)` builds a 12-deep WALK STACK, not "12 body segments".

**Renderer decoded too (2026-07-01, same pass):** clip bind = VM op1 -> `FUN_004c68f0(mnemonic)`
-> ARLU lookup `FUN_004cf7b0` -> clip node @person+0x224 (Coww/2DOG remap human anims to
DgRn/DgSt). Draw = `FUN_004c7f10` (scale 35/depth, LOD step mask 0xffff/4/2/1 from figure.twk
bands, facing = person+0x140 * pi/4) -> `FUN_004cfb30` (per-part transform w/ parent endpoint
chaining, ARCP+4 = per-part LOD bitmask, painter's qsort) -> `FUN_004cf8f0` per-part primitive by
ARCP type byte: 0xb thick line (limbs), 0xa thin line, 9 = ROTATED HEAD SPRITE from SIM3D.BMP
image `DAT_0058f0e0[person+0x18e]` (indices 4,5,0x2c-0x31,0x41-0x43; ARPP byte3 = roll angle -
the only use of the 4th byte), 8/0xc/0xd/0xe dots. Color = palette `0x24 + ARCP[+3]*16`, parts
with ARCP[+5]==0 offset by person+0x160 mod 14 (clothes variation). Endpoints unpack (x=b0,
up=b2, depth=b1). UE port: capsules/ribbons per segment + camera-facing head card + palette ramp
colors. Remaining trivia: exact px width expr, ARCP +1/+6/+7 bytes, dim floats (unused by draw).
**LIVE-VALIDATED (ORACLE PASS, 2026-07-01):** `Tools/privanim_live_oracle.py` (read-only, anchors
on the head-image table - no absolute addresses needed) matched a live pedestrian's ARPP pose
records byte-for-byte (1224 bytes, 2woman clip 412!/NoMo, rows/cols/binding all as predicted).
Live 4-char name fields (mnemonic@+0x220, node names@+0x1c) read byte-REVERSED (LE u32 of BE chars).
See [[simcopter-people-logic-next]] for the behavior-VM decode plan.
