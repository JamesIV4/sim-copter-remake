# Original Game Binary File Formats: `X/people.df` and `X/privanim.df`

This document covers the two binary data files in `Reference/SimCopterOriginalGame/X/` that drive SimCopter's people: `people.df` (the "global behavior file") and `privanim.df` (articulated 3D figure animation). These were the largest remaining reverse-engineering gaps called out in `Docs/DocumentationCoverage.md`. The companion `Docs/OriginalGameFileCodeWalkthrough.md` covers the formats that already have C++ parsers (`.sc2`, `GEO/*.MAX`, `.BMP`, `.twk`); this file covers formats that do **not** have remake parsers yet.

Status legend (see `Docs/DecompilationWorkflow.md`): `Confirmed` = validated against decompiled code and/or the raw bytes of the shipped files; `Hypothesis` = plausible but not fully validated; `Follow-up` = known missing work.

Evidence sources:

- Raw byte inspection of the two files (probe via `python`, see commands in `Docs/DecompilationWorkflow.md`).
- Ghidra decompiles in `Docs/scratchpad/ghidra/`: `out_people_behavior_runtime.txt`, `out_people_loader.txt`, `out_people_parser.txt`, `out_df_reader.txt`, `out_population_ai_pass1.txt`, `out_privanim_strings.txt`.

## The Maxis "DF" Resource Container

`Confirmed.` Both files share a common header layout. They are not raw data; they are small Maxis resource containers whose bulk payload is opaque to the container itself.

Header bytes (offsets from start):

```text
people.df
000000  00 00 01 00  00 00 dd 0c  00 00 dc 0c  00 00 24 16   version + u32 fields
000010  00 00 d9 42  00 00 00 00  ...
000030  06 'People'                                          length-prefixed name
        RSRC 'OBJm' 01 00 ff ff ff ff 00 00 00 00 28 e6 02 00   resource dir entry 1
        RSRC 'OBJm' 01 00 ff ff ff ff 00 00 00 00 ...           resource dir entry 2
        <opaque payload to EOF>

privanim.df
000000  00 00 01 00  00 0c 3f be  00 0c 3e be  00 00 39 c0   version + u32 fields
000030  08 'privanim'                                        length-prefixed name
        RSRC 'Doug' 01 00 00 c4 00 58 00 00 00 00            resource dir entry 1
        RSRC 'Doug' 01 00 00 c4 00 58 00 00 ...              resource dir entry 2
        <opaque payload to EOF>
```

Confirmed structure:

- Bytes `0..3`: `00 00 01 00` = a version/magic dword (`0x00010000`, i.e. "1.0").
- Bytes `4..0x2f`: a small fixed header of `u32` fields (counts/sizes). People.df reads `1, 0x0cdd, 0x0cdc, 0x1624, 0x42d9` starting at offset 2; privanim.df has different values. Exact meaning is `Follow-up`; they are almost certainly resource counts and section sizes.
- At `0x30`: a **length-prefixed (Pascal) name string**: one count byte then the ASCII name (`06 "People"`, `08 "privanim"`). The engine builds these length-prefixed strings with `FUN_004d16e0` / `FUN_004c...` (count byte = `strlen`, then bytes), so the on-disk name uses the same convention.
- A **2-entry `RSRC` resource directory**. Each entry is `"RSRC"` (4 bytes) + a 4-byte creator/type tag + a `01 00` version word + id/offset fields. The creator tag differs per file: `OBJm` for people.df, `Doug` for privanim.df (`Doug` reads like a Maxis developer's signature; `OBJm` like "object" + a subtype byte). The second entry repeats the first with the trailing offset field zeroed (terminator).
- Everything after the directory is the **opaque payload** (65 KB for people, 800 KB for privanim) interpreted by the consuming subsystem, not by the container.

`Confirmed.` Both files are located and opened the same way: the resource manager `FUN_00433b20` is called with resource **type `0xc`** (`FUN_00432ab0(0xc, 0, "People.df"/"PrivAnim.df", pathBuf)`), which resolves the name against the configured resource directories (the `X/` folder for type `0xc`) into a 260-byte path buffer. The file is then read through a virtual method on the owning manager object (`FUN_004ce2d0` -> `(*vtable[1])(path)`), not by a standalone parser. This is why there is no single "df parse" function: the container is generic and the payload is consumed lazily by the people/figure systems.

## `people.df` - the Global Behavior File

`Confirmed.` `people.df` is what the engine internally calls the "global behavior file" (the load failure path prints `Couldn't open global behavior file`). It is loaded once by `FUN_004c2f30`:

```c
FUN_004c2f30():
  GetSystemTime(&t);
  FUN_004ce9c0((t.ms & 0xffff) + (t.ms >> 16));   // seed the people PRNG from wall-clock ms
  FUN_00432ab0(0xc, 0, "People.df", pathBuf);      // resolve X/people.df
  FUN_004c3010(pathBuf);                            // init people subsystem + load behavior data
  FUN_004c2fc0();                                   // pre-construct 500 person slots
  DAT_00506444 = FUN_004c7980();                    // create the template/default person
```

### Subsystem initialization (`FUN_004c3010`)

`Confirmed.` `FUN_004c3010` runs once (guarded by `DAT_00506440`) and builds all the hardcoded people tables, then loads the behavior payload. The tables are compiled into the exe; only the behavior bytecode comes from `people.df`.

Tables built:

- **8-direction rotation matrix** (`DAT_0058dfd0`..`DAT_0058e02c`): sin/cos pairs in 16.16 fixed point (`0x10000` = 1.0, `0xb504` = 0.7071 = cos 45 deg, `0xffff4afc` = -0.7071). Used to face figures in 8 compass directions. Pre-rotated unit vectors are cached via `FUN_0046c4bf` into `DAT_0058da98`..`DAT_0058daa0`.
- **Per-state primary animation/resource id** `DAT_0058de80[0..0x14]` (21 entries): a state -> figure/sprite id table. Values like `600`, `700`, `0x352`, `0x578`, `0x29a`. State `0x13` reuses `700`.
- **Per-state id table** `DAT_0058d728[0..0x13]` (20 entries, default `0xffff`): a second per-state resource table (`0x385`, `0x38e`, ...).
- **Per-state animation sub-sequences** `DAT_0058d750[state*10 + k]` and `DAT_0058ec00[state*10 + k]`: each state has an array of up to 10 sub-animation ids terminated by `0`. There are two parallel sequence sets (likely "primary action" and "secondary/while-moving"). For example state 2 gets `{0xd, 0xb, 0xa, 0xc}`; the default gets `{0xd, 0xb, 0xa, 0xc, 7}`.
- **Per-state flag/param pair** `DAT_0058d6d0` (4-byte flag) + `DAT_0058d6d4` (2-byte value), 14 states. States 2,3,4,5,7 get flag `1`/value `2` (state 4 gets value `4`); the rest get flag `0`/value `4`. This pair is read at spawn time (`FUN_004c4190` reads `DAT_0058d6d0 + class*6`).
- **256-entry building -> behavior-class map** `DAT_0058e800[0..0xff]`: maps every SC2 `XBLD` tile id to a "people behavior class" `1..0xd`. This decides what kind of crowd/behavior a tile produces. The ranges line up with the known XBLD layout (`Docs/ReverseEngineering.md`): tile `0` -> class 2; `1..4` -> 4; `5..0xc` -> 3; `0xd`/`0xd5`/`0xda` -> 5; the road/rail/bridge ranges and the building ranges (`0x70+`) map to classes `1`, `0xb`, `0xc`, `0xd`. This is the original's answer to "who walks/drives where."
- **500 person slots** `DAT_0058e030[0..499]` (zeroed, then `FUN_004c2fc0` pre-constructs all 500). 500 is the hard cap on simultaneous people.

After the tables, `FUN_004c3010` calls `FUN_004cd550(pathBuf)` to load the behavior payload (storing the behavior buffer at manager `+0x114`, and an "open" flag at `+0x108` checked by `FUN_004ce4f0`), then installs the behavior VM dispatch table (below).

### The behavior VM

`Confirmed (structure) / Follow-up (opcode semantics).` The people behaviors are a small bytecode interpreted by a **computed-goto VM**:

- `DAT_0058ef78[0..0x57]` is an **88-entry jump table** of code labels `LAB_004c84e0`, `LAB_004c8500`, ... each `0x20` bytes apart. Ghidra reports "No function at 004c84e0" because these are not separate functions; they are inline handler blocks reached by an indirect jump through the table. That is the classic shape of a threaded bytecode interpreter: each opcode indexes the table and runs a ~32-byte handler.
- The behavior bytecode itself lives in the `people.df` payload (the buffer at manager `+0x114`).
- Branch decisions use the dedicated people PRNG.

`Follow-up.` Disassembling the 88 handler blocks and the bytecode stream is the remaining work to fully reproduce NPC decision-making. The state machine they drive is the ~20 states in the tables above.

### The people PRNG

`Confirmed.` People behavior uses its own 16-bit LFSR, **separate** from the MSVCRT `rand()`/`_holdrand` used for terrain detail (`Docs/ReverseEngineering.md`):

```c
FUN_004ce9d0():   // advance
  if ((seed & 0x8000) == 0) x = seed << 1;
  else                      x = (seed << 1) ^ 0x1bf5;
  seed ^= x;                // seed = DAT_0058f0d8

FUN_004cea00(n):  return FUN_004ce9d0_value() % n;   // bounded roll
```

It is seeded from `GetSystemTime` milliseconds at load. Tap mask `0x1bf5`. A faithful remake of crowd behavior must use this generator (not `FRandomStream`) if it wants frame-identical crowds, though for a remake deterministic hash noise is the pragmatic substitute (as already done for terrain).

### The person record

`Confirmed (offsets) / Hypothesis (a few field meanings).` Each of the 500 person slots is a struct of roughly `0x200` bytes. Offsets recovered from the update (`FUN_004c3f00`) and spawn (`FUN_004c4190`) code:

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x108` | int | (manager) behavior-file "open" flag |
| `+0x10a` | int | owning mission / controller id |
| `+0x12a` | short | facing X (derived from heading) |
| `+0x12c` | short | facing Y |
| `+0x12e` | short | figure/sprite id returned to callers |
| `+0x142` | short | alive/active flag (0 = free slot) |
| `+0x148` | short | **state** (also the spawn mode; see below) |
| `+0x1a0` | int | carrier object pointer (0 = on foot; non-zero = riding a car) |
| `+0x1c4` | int | speed/step scalar (16.16) |
| `+0x1cc` | int | world X (16.16 fixed) |
| `+0x1d0` | int | world Y / height (16.16); set to ground `+ 0x30000` (3.0) on spawn |
| `+0x1d4` | int | world Z (16.16) |

When a person rides a carrier, position is stored relative to the carrier (`carrier+0x18` = X, `carrier+0x20` = Z) and resolved through `FUN_0046d770`. State changes emit 4CC event codes, e.g. `FUN_004c68f0(0x44656164)` = `"Dead"`.

### Spawn modes / initial states (`FUN_004c4190`)

`Confirmed.` `FUN_004c4190(host, mode, tileX, tileY, p5, hostObj, posPtr)` configures a person; `mode` becomes the initial state and selects placement strategy. It places against the scene-graph render grid `DAT_005d9200[tileX*0x100 + tileY]` (the same `0x100`-stride city grid used by the mesh builder, see memory `simcopter-mesh-orientation-rules`) and validates against the `XBLD` grid `DAT_005910b0`.

| mode | placement strategy |
| --- | --- |
| 0 | place at given tile, or at an explicit world position (`posPtr`) |
| 1, 0xf, 0x13 | attach to an existing host object (`hostObj`), inherit its orientation (rider/passenger) |
| 2 | place at tile after reading `XBLD`; reject tile ids `0xfe`, `0xb7`, `0xff` |
| 3 | try up to 4 nearby tiles for a valid spot (ambient walker) |
| 4, 6 | spiral/diagonal search outward for a valid tile; mode 4 reads a path target via `FUN_004a8890(p5)` |
| 5 | place at an explicit world position with a collision test (`FUN_004c9000`) |
| default | generic single-tile placement |

`FUN_004c02a0` is the "find a valid local position on this tile" helper; `FUN_004c82c0` converts to a world transform; height is forced to ground `+ 0x30000`.

### How this maps to the remake

`Follow-up (none implemented).` The remake currently uses `FSimCopterPopulationBody` (procedural boxes) for pedestrians and a graph-walk for movement (`Docs/GameplayCodeWalkthrough.md`, memory `simcopter-population-rendering`). To reproduce the original crowd it would need: the 500-slot pool, the building->class map, the ~20-state machine with its animation sequences, the behavior bytecode VM, and the 8-direction facing. The data and table layouts are documented above; the remaining decode is the 88 VM handlers and the bytecode stream.

## `privanim.df` - Articulated Figure Animation

`Confirmed (load path) / Follow-up (record layout).` `privanim.df` holds SimCopter's articulated 3D people ("figures"). The `GEO/*.MAX` packs contain no person meshes (verified across all three packs; memory `simcopter-population-rendering`), so all walking/rescue/riot figures come from here, plus the flat `BMP/PEOPLE1.BMP` sprite for distant LOD.

Loaded by `FUN_004ceab0`:

```c
FUN_004ceab0():
  // build a 25x25 (=625) precomputed lookup table:
  for i in 1..0x270:  tbl_0x61b1ac[i] = (long)<float expr>;     // __ftol of a float series
  for row in 0..24, col in 0..row:
      DAT_0061a7e0[row*25 + col] = DAT_0061b1b0[ -(row-col)^2 + row^2 ];  // radial/distance table
  FUN_00432ab0(0xc, 0, "PrivAnim.df", pathBuf);   // resolve X/privanim.df (resource type 0xc)
  FUN_004d16e0(pathBuf, nameBuf);                  // make length-prefixed name
  manager = alloc(0x12);  obj = alloc(0x11a);      // figure manager + a 0x11a-byte figure object
  (*obj.vtable[1])(nameBuf);                        // open/read the .df via virtual method
  if (FUN_004ce4f0() == 0) error(nameBuf);          // shares the "behavior file open" check
  (*manager.vtable[3])();                            // finalize
  *(manager+0xe) = 0x424f4443;                       // type tag (4CC)
  DAT_00506bf4 = manager;
```

`Confirmed` details:

- Same resource type (`0xc`) and same virtual-read path as `people.df`; the two systems are siblings.
- The loader precomputes a **25x25 radial lookup table** (`DAT_0061a7e0`, 625 entries, built from `DAT_0061b1b0` via the quadratic index `-(row-col)^2 + row^2`). This is consistent with a fixed-size figure-distance/scale table used for the LOD boundaries tuned in `figure.twk` (`Far/Med/Near boundary`, `Don't sim past dist`, `Adjust feet by this vert dist`; see `Docs/MissionsAndTweakSystem.md`).
- A figure object is `0x11a` (282) bytes; the manager is `0x12` bytes and is tagged with the 4CC `0x424f4443`. `DAT_00506bf4` is the global privanim manager.
- The privanim string `PrivAnim.df` is referenced only from `FUN_004ceab0` (`out_privanim_strings.txt`).

### On-disk format: big-endian IFF "Doug"

`Confirmed.` `privanim.df` is a **big-endian** IFF-style hierarchical container (the "Doug" format, named for its Maxis author tag). Big-endian is expected: SimCity 2000 was Mac-first and its `.sc2` files are also big-endian IFF. Evidence: the loader registers chunk handlers that *byte-swap* fixed fields (`FUN_004d1ed0`), and pose data reads as sensible big-endian IEEE floats (`41 20 00 00` = 10.0, `40 e0 00 00` = 7.0, `3f 00 00 00` = 0.5). The 4CC tags are stored as plain ASCII and compared as big-endian dwords (e.g. `"ARPP"` == `0x41525050`).

The bulk is read **lazily by 4CC** from the open `FILE*` (`+0x10c`), not parsed whole. A **section directory** sits near the end of the file (at `0xc3fdc` in the shipped file), a list of 8-byte `[4CC][be u32]` entries:

| 4CC | meaning (Confirmed tag / Hypothesis role) |
| --- | --- |
| `BODC` (`0x424f4443`) | body/figure geometry. Same tag the figure manager stamps at `manager+0xe`, so `BODC` == the figure object type. |
| `ANIP` | animation section (contains the `ARPP` clips). Opened as a sub-bank via `FUN_004cf480(path, 'ANIP')`. |
| `ARCP` | articulation component/part records (the skeleton). |
| `ARLU` | articulation lookup table. |
| `ARPP` | per-clip animation/pose records. |
| `SPR#` | sprite section (the flat-sprite LOD source). |

`Confirmed (record sizes + endian field maps, from the registered handlers `FUN_004d1ed0` calls):`

- **`ARCP` = 40-byte (`0x28`) records**, handler `FUN_004d0090` byte-swaps `u32 @+8`, `u32 @+0xc`, and `u16 @+0x1c`, `u16 @+0x20`, `u16 @+0x24`. The three `u16` at `+0x1c/+0x20/+0x24` are almost certainly a part's pivot/joint coordinate (x,y,z); the two `u32` are offsets/links into geometry. These are the per-segment skeleton definitions (the figure is 12 segments; see below).
- **`ARLU` = 8-byte records**, handler `FUN_004d00e0` swaps `u32 @+0`, `u32 @+4` (a two-field lookup, e.g. state/clip id -> ARCP range).
- **`ARPP` = 8-byte records** (registered size), handler at `0x4cea20`. On disk each `ARPP` *entry* is a 40-byte (`0x28`) block: `+0x00` `"ARPP"`, `+0x04` be-u32 id, `+0x08`/`+0x0c`/`+0x10` three big-endian floats (the pose transform, e.g. `(10, 7, 0.5)`), `+0x14` flag bytes, `+0x1c` 4-char **clip name**, `+0x20` 4-char **parent clip name**, `+0x24` trailer.

`Confirmed.` The `ANIP` section is a set of **76 named animation clips** in an inheritance tree. Each clip has a 4-char name and a 4-char parent reference; the root is `"New "`, then `Ne0`/`Ne1`/`Ne2` derive from `New`, `Ne3`/`Ne4` from `Ne0`, `Ne8` from `Ne7`, and so on (verified by reading the name pairs at `0xb406` onward). So pedestrian animations are a hierarchy of derived poses, not 76 independent clips. The per-state animation id (`DAT_0058de80[state]`, see below) selects which clip plays.

### Leaf data: geometry and animation keyframes

`Confirmed (structure) / Follow-up (exact fields).` The IFF nodes are looked up by `(4CC tag, index)` through a 12-byte node-directory entry (`FUN_004cdf40`: `addr = base + index*0xc + entryOffset - 0xe`); the directory table is the 12-byte-record block near EOF (`0xc401c`: `be u32 dataOffset`, `be u32`, `be u16`, `be u16 index`). Following those `dataOffset`s into the file reveals two leaf shapes:

- **Body-part geometry leaves** (e.g. at file offset `0x822`): TLV records of the form `[type][len] ...payload`, e.g. `01 0d 03 26 ...`, `04 2a 01 0e 03 24 ...`. The payload bytes cluster around `0x7c` (124) with limb values like `0x23`/`0xb4` - consistent with **vertices stored as unsigned bytes in a 0..255 box centered near 128**, interleaved with palette color indices. These are the 12 body-segment meshes (`BODC`).
- **Animation keyframe leaves** (e.g. `0x15a0`, `0x1a77`, `0x1dc1`): dense streams of **4-byte records** of small signed values (`fc ef fe 24` = `(-4,-17,-2,36)`) with one field stepping by 8 - consistent with **per-joint keyframe deltas over time** (the `ANIP`/`ARPP` clip frames; the three `ARPP` floats per clip are the clip's base transform, and these leaves are the per-frame motion).

The early part of the file also contains Maxis "Doug" compiler-metadata nodes (`!Compile.rs`, `!ANSI_...`), interleaved with the data nodes.

`Follow-up (the render-correctness gap).` Two things remain to render figures byte-accurately:

1. **Exact geometry decode**: which TLV `type` bytes delimit vertex lists vs. face lists, the vertex byte->model-space scale, face winding, and the palette used (likely the shared `GEO` `CMAP`).
2. **Skinning math**: how the `ARCP` part pivots (`u16@+0x1c/+0x20/+0x24`) compose with the per-frame keyframe deltas and the 8-direction facing matrix to place each of the 12 segments. The draw routine `FUN_004d4800` is an inline software-rasterizer block Ghidra does not lift as a function.

## Live-Memory Validation (confirmed against the running game)

`Confirmed.` A read-only memory rip of the live game (SimCopter 1.0.1.3 patched with SimCopterX, PID-agnostic) validated the static decode end to end. Method and findings:

- **The running binary is byte-identical to the decompiled one** (same SHA-256), loaded at base `0x400000`; SimCopterX applies its fixes in-memory (note the `.detour` section) without moving the people/figure code.
- **Address caveat for live work**: Ghidra's `.data` addresses do **not** equal the loaded VAs - they differ by a *non-uniform*, region-dependent delta (about `+0x5C58` near `0x506xxx`, about `+0x6010` near `0x58xxxx`). `.text` matches 1:1. So the `DAT_*` addresses in these docs are Ghidra-space; for live reads, calibrate per region by scanning for a known anchor (a string like `People.df`, or the `DAT_0058de80` value table).
- **People runtime confirmed live**: scanning for the `DAT_0058de80` animation-id table (`600,700,700,0x352,0x2ee,...`) located the person system; the 500-slot pointer array (`DAT_0058e030`), max-index (`DAT_0058dc3e`), and per-person fields all read correctly. With 54 live pedestrians, `state@+0x148 -> DAT_0058de80[state] = anim@+0x17a` held exactly (state 0 -> 600, state 6 -> 800), positions were sane fixed-point city coordinates, and the **12 x 0x14 segment-instance layout** appeared in the person struct (segment records at `person+0x04` stepping `0x14`, each holding a pointer at `+0x10` to the shared figure-definition).
- **privanim format confirmed big-endian IFF**: the live runtime objects contain the chunk 4CCs **byte-swapped** exactly as expected for a big-endian file loaded on x86 - `PPRA` (= `ARPP`), `ULRA` (= `ARLU`) - plus node names (`104`/`!401`, ...) and the `ARCP` value `0x3482` that the file probe also produced. The IFF reader object even holds the literal `...\X\PrivAnim.df` path and open file handle.
- **The figure is a lazy-streaming IFF object graph**, not a resident vertex array: a shared figure-definition node (one per pedestrian model) links part nodes (`ARCP`/`ARPP`) and sub-nodes, each referencing a single file-reader object that streams chunk data from `privanim.df` **on demand**. There is therefore no flat decoded vertex buffer to rip - the geometry lives in the file.

`Conclusion for the remake.` Because geometry is streamed from the file, the right path to replicate rendering is to **parse `privanim.df` statically** (the `BODC` TLV documented above), not to rip runtime memory. The rip's value was confirming that the static format is correct (big-endian IFF, the chunk tags, the people pipeline). The last open items - exact `BODC` TLV field semantics and the skinning composition - are best finished by either decoding the `BODC` leaf against the now-confirmed format, or manually disassembling `FUN_004d4800`. The live people/animation structures (states, per-state clips, 12-segment layout, 8-direction facing) are now fully validated and can be reproduced directly.

The rip scripts used (`rip3.ps1` person walk, `rip6`-`rip9` figure-graph chase) are self-calibrating read-only `ReadProcessMemory` probes kept in the session scratchpad; promote them to `Tools/` if live inspection is needed again.

### Pedestrian render pipeline (state -> figure -> draw)

`Confirmed (chain) / Follow-up (final pose+geometry decode).` Tracing the readers of the per-state animation table `DAT_0058de80` recovers the full path from an NPC's state to its drawn figure (evidence: `out_ped_render_xrefs.txt`, `out_ped_anim.txt`, `out_figure_instantiate.txt`, `out_privanim_bind.txt`, `out_figure_*vtable.txt`):

1. **State -> animation id.** `FUN_004c7090(state)` stores the state at person `+0x148` and the figure animation id `DAT_0058de80[state]` at person `+0x17a`, plus loop flags at `+0x14a` (states 3/10/0xb/0xc/0xd loop one way, 7/8 another). This is the same per-state table built by `FUN_004c3010`.
2. **Attach a render node.** `FUN_004c7c00` creates the person's render node (vtable `PTR_LAB_004f5018`, an 8-method table), positions it at the person world coords (`+0x1cc/+0x1d0/+0x1d4`), and calls `FUN_004ce630` to build the figure. `FUN_004ce630(0xc, DAT_0058de80[state], owner, ...)` is reached here with figure-segment-count `0xc`.
3. **Figure = 12 segments x 20 bytes.** `FUN_004ce630` zero-inits `12 * 0x14` = 240 bytes of per-segment part records (vtable `PTR_LAB_004f5068`), i.e. the articulated person is **12 body segments**, each a 20-byte transform/part record. The segment count is stored at figure `+0xf6`.
4. **Bind the animation cursor.** `FUN_004ce6c0` sets the playback state on the figure: `animId@+4`, `frame@+6 = 0`, `timer@+8 = 0`, owner transform `@+0x14`, active `@+0xf4`.
5. **Per frame: advance + draw.** The figure render vtable `PTR_LAB_004f5068` (`{0x4d4800 x3, 0x4ce690, 0x4ce7b0, ...}`) advances the frame timer and draws the 12 segments using the bound privanim pose data, transformed by the 8-direction facing matrix. Far from the camera, the flat `PEOPLE1.BMP` sprite replaces the articulated figure per the `figure.twk` LOD bands.

So pedestrian rendering is: **state -> animation id (`DAT_0058de80`) -> 12-segment articulated figure instantiated from `privanim.df` -> per-frame pose advance + draw, with a `PEOPLE1.BMP` sprite at distance.**

`Follow-up.` With the on-disk format decoded (above), the last open piece is the exact **field semantics** of the `BODC` segment geometry and the `ARCP`/`ARPP` records, plus the skinning math inside the figure vtable methods (`FUN_004ce690`, `FUN_004ce7b0`, `FUN_004d4800`). The `Tools/privanim_probe.py` added in this pass walks the container and dumps the section directory, the 76-clip animation tree, and the `ARCP`/`ARLU`/`ARPP` record regions, so an extractor can iterate from there.

## Are Pedestrians 3D Or Sprites? (resolved)

`Confirmed.` They are **3D low-poly vector figures up close, with a flat `PEOPLE1.BMP` sprite far-LOD** - not pre-rendered sprite sheets. Evidence:

- **The data is vector, not bitmaps.** The 798 KB bulk of `privanim.df` is structured 4-byte coordinate/keyframe records (byte[1] steps by 8 = frame/index; the other three are small signed coordinates incl. a Z-like component). A sprite sheet would be arbitrary palette-index pixel rows; this is not that. (Twelve tiny 3D body meshes + animations is also far smaller than 798 KB - the bulk is per-frame vector animation.)
- **People carry a full 3D transform.** The people render loop `FUN_004c5fb0` builds, per person, a transform of `{position @+0x1cc/+0x1d0/+0x1d4}` plus a **16-dword (4x4) matrix @+0x1d8**, and applies it with `FUN_004704d1(sceneNode, &xform, 3)` - a 3D affine transform of geometry, not a 2D billboard placement.
- **People live in the 3D scene.** They are fixed-point 3D positioned, linked into the scene-graph grid `DAT_005d9200`, collision-tested in 3D (`FUN_004c9470`/`FUN_004c9000`), and oriented by the 8-direction sin/cos rotation matrix (`DAT_0058dfd0`).
- **A sprite far-LOD exists.** `figure.twk` defines Near/Med/Far LOD bands; the far source is `BMP/PEOPLE1.BMP`. So distant pedestrians are swapped to flat sprite billboards.

`Why they look like layered sprites.` The near figures are flat-shaded (palette-colored, untextured) low-poly vector models drawn by the 1996 software rasterizer with painter's-algorithm sorting (no Z-buffer). At low resolution that reads as blocky, "projected on top of each other" shapes; and at any distance they are literally the `PEOPLE1.BMP` sprite. Both impressions are correct - but the underlying near-figure asset in `privanim.df` is 3D vector geometry, so to replicate it faithfully the remake should build 3D flat-shaded segment meshes from the `BODC`/keyframe data, with a sprite LOD fallback (which the remake already approximates via `FSimCopterPopulationBody` boxes + the `PEOPLE1` sprite path).

`Correction (2026-06-26 deep pass).` The earlier guess that `0x4cf0b0`/`0x4cf3b0` were the geometry/keyframe "interpreter leaves" was wrong - force-decompiling them shows both are 2-byte type stubs (`return 0;`), and `0x4cf5c0`/`0x4cf6f0` are destructors. They are node lifecycle methods, not byte consumers. The exact primitive question is re-scoped and the real parse/draw path is decoded below (see "Faithful Extraction Method").

## Summary of New Findings vs. the Old Gap List

`Docs/DocumentationCoverage.md` previously listed these as undocumentable. Now documented above:

- `people.df` is the "global behavior file": a type-`0xc` Maxis DF container; runtime is 500 slots, ~20 states, hardcoded animation/sequence tables, a 256-entry building->class map, an 88-handler bytecode VM, an 8-direction facing table, and a dedicated 16-bit LFSR PRNG (`0x1bf5`).
- The person record layout (state, position, carrier, mission owner) and the 7 spawn modes are recovered.
- `privanim.df` load path, manager/figure object sizes, the 25x25 LOD table, and the `figure.twk` tie-in are recovered.
- The pedestrian render pipeline is traced end to end: state -> `DAT_0058de80` animation id -> 12-segment articulated figure (`FUN_004ce630`/`FUN_004ce6c0`) -> per-frame pose advance + draw, with a `PEOPLE1.BMP` sprite at distance.
- `privanim.df` is decoded as a big-endian IFF "Doug" container with a section directory (`BODC`/`ANIP`/`ARCP`/`ARLU`/`ARPP`/`SPR#`), known record sizes + endian field maps, and a 76-clip animation inheritance tree. Walkable via `Tools/privanim_probe.py`.

Still open (`Follow-up`): the 88 VM opcode handlers and bytecode grammar; the exact field *semantics* of `privanim.df` `BODC`/`ARCP`/`ARPP` records and the skinning math; and the small fixed header `u32` fields in both DF containers.

## Exact Container Spec (2026-07-01 pass - fully code-derived, supersedes the removed 2026-06-26 section)

`Confirmed.` This pass finished the decode properly: every directory/record rule below was read out
of the decompiled reader functions, then validated against the shipped file (437/437 record-array
chunks parse with byte-exact sizes). It **corrects several earlier claims** (see the corrections
list at the end). Extractor: `Tools/privanim_extract.py` (rewritten 2026-07-01); evidence:
`out_chunkfetch.txt`, `out_chunkfetch2.txt`, `out_dirload.txt`..`out_dirload4.txt`,
`out_nodemethods.txt`, `out_nodevtables.txt`, `out_figwalk.txt`, `out_vm_handlers.txt`,
`out_vm_ops0-6.txt`.

### File layout (all fields big-endian)

- **Header** (`FUN_004cd3e0`): `@0 u32 dataBase` (0x100 in the shipped file; it doubles as the
  `00 00 01 00` "version" marker), `@4 u32 dirOffset`, `@8 u32` (unused by the chunk path),
  `@0xc u32 dirSize`.
- **Directory** at `dirOffset`: a 0x1c-byte header (copy of the file header + totals), then
  `u16 sectionCount-1`, then a **blob** of `dirSize-0x1c-2` bytes that the game loads whole
  (`FUN_004cdb50` -> `FUN_004cda40`):
  - **Section entries** (`FUN_004cdfe0` swap): `sectionCount x 8` bytes `[4CC][u16 count][u16 entryOff]`.
    Shipped file: `SPR#(0) ARPP(395) ANIP(395) ARCP(21) ARLU(21) BODC(21)`.
  - **Node entries** (`FUN_004ce010` swap): per section, `(count+1) x 12` bytes at
    `blob + entryOff - 2` (`FUN_004cde50`/`FUN_004cdf40`; the extra slot is a separator; index is
    1-based): `[u16 id][u16 nameOff][u8 flags][u24 chunkOff][u32 scratch->runtime handle]`.
    `id` is a lookup key used by `FUN_004cdee0(tag,id)`; `nameOff` indexes the string table.
  - **String table** at `blob + sectionCount*8 + totalEntries*12`: Pascal strings
    (`FUN_004cdfa0`). Every node has a NAME - the 21 BODC figures are
    `pilot, swimmer, fatman, 2blonde, Child, 5.5man, Coww, SUIT, Elvis, Nessie, 5man, SHADES,
    Kopp, Medik, Badguy, Blonde, 2DOGG, 2woman, Fireman, TubaExpert, Woman`;
    the 395 ANIP clips are named `"101!".."495!"` (18 consecutive ids per figure:
    pilot=101-118, swimmer=119-136, ...).
- **Chunks** (`FUN_004cdcb0`): at `dataBase + chunkOff`: `[u32 len][payload]`, lazily loaded into a
  `LocalAlloc` buffer and cached in the entry's last 4 bytes.
- **Record-array payloads** (ARCP/ARLU/ARPP; `FUN_004d1a00` + `FUN_004d1df0` + `FUN_004d1d70`):
  `[u16 recSize][u16 rows][u16 cols][2 pad][rows*4 row-pointer slots][rows*cols*recSize records]`;
  `len == 8 + rows*4 + rows*cols*recSize` holds for all 437 chunks.
  Registered record sizes + swap handlers (`FUN_004ceab0` -> `FUN_004d1ed0`):
  `ARCP=0x28/FUN_004d0090`, `ARLU=8/FUN_004d00e0`, `ARPP=8/FUN_004cea20` (**empty** - ARPP is raw bytes).
- The record arrays are found by **name key**, not by tag+index: `FUN_004cfed0` takes the figure
  name and replaces `name[3]` with `'c'` (ARCP) / `'L'` (ARLU); `FUN_004d18e0` uses `'i'` (ARPP)
  on the clip name. E.g. figure `pilot` -> `pilct`/`pilLt`; clip `101!` -> `101i`.
  BODC and ANIP node payloads themselves are 4 zero bytes + 0xA3 filler (no data).

### Record semantics

- **ARCP = the skeleton/part tree** (one per figure; `1 row x nParts x 0x28`): per record
  `+0 u8 type` (0x08/0x0b/0x0e... draw/type byte), `+1 u8 ref`, `+2 u8 sequential part index`,
  `+3..+7 flags`, `+8 char[4] node name` (root `"New "`, then `Ne0..`), `+0xc char[4] parent name`
  (resolved to a pointer at runtime by `FUN_004cf850`/`FUN_004cf8b0` name matching),
  `+0x10 [u16 8][u16 8]["ARPP"][u32 1]` (pose-record link: stride 8, one per frame),
  `+0x1c/+0x20/+0x24 f32 x3` part dimensions (e.g. `(10,7,0.5)`).
  Part counts vary per figure: pilot=75, Child=32, Coww=88, Nessie=29.
- **ARLU = behavior-anim -> clip binding** (one per figure; `1 x 18 x 8`): `[char[4] mnemonic]`
  `[char[4] clip name]`: `1Wal->101!  Inju  HipH  Whoa  DgRn  NoMo  Tote  Yumm  2Gab  Dead  Slum
  Wave  1Run  DgSt  WvNo  Thro  Play  FaCl`. These mnemonics are exactly the animation names the
  people.df behaviors reference. Every figure has the same 18.
- **ARPP = per-frame pose/geometry** (one per clip; `frames x nParts x 8`): each record is one body
  part's **line segment for that frame**: `[s8 x0][s8 y0][s8 z0][u8 scratch][s8 x1][s8 y1][s8 z1]`
  `[u8 scratch]`, z up, model-space units ~ -30..30. Segments chain end-to-end into the body
  wireframe (spine, shoulders, hips, limbs); rendering frame 0 vs frame 4 of a `1Wal` clip shows
  the leg swap of a walk cycle. Walk clips have 8 frames; most others 1-4.

### The walker VM (figures and behaviors share it)

`FUN_004ce7b0` is a token-stream interpreter with an explicit **12-deep stack of 20-byte frames**
(`FUN_004ce630(0xc,...)` - the earlier "12-segment articulated figure" reading of that ctor was
wrong; 12 is the walk-stack depth). It reads 16-bit tokens from node records: tokens `< 0x100` are
**opcodes** dispatched through `this->vtable[0]`; tokens `>= 0x100` are 4-char node names -> push
the named child node (`FUN_004ce700`). For the person object (which embeds the same walker layout:
stack at `+4`, cursor `+0xf4`, count `+0xf6`) `vtable[0]` is `FUN_004ccf20`, the people-behavior
dispatch `(&DAT_0058ef78)[op]`. The 88 opcode handlers are 0x20-byte thunks at
`0x4c84e0 + 0x20*n` -> real functions (op0=`FUN_004ca750` wait-counter, op3=`FUN_004ca7d0` move
step, op6=`FUN_004ca940` walk-toward-object, ...) - decoding them all is the Phase-4 work item.
The full opcode->function map is in `out_vm_handlers.txt`.

### Corrections to earlier sections (do not trust these older claims)

1. The 2026-06-26 "ARCP coord stream -> drawable geometry" rules (`t = 8k+4` segment index,
   pair-by-t, z-bit7 part split) were **all wrong** - artifacts of reading data at garbage offsets:
   the old extractor treated directory-entry bytes 0-3 (`id<<16|nameOff`) as a payload offset.
2. There is **no 40-byte "node-def" record** and **no 75-76-clip inheritance tree**: those blocks
   were misaligned reads of ARCP part records (the `Ne##<-parent` pairs are the *skeleton* tree;
   the `(x,y,0.5)` floats are part dimensions, not clip base transforms).
3. `ARCP` records really are 0x28 bytes (the "stream of 4-byte records" model is dead), and the
   swapped fields at `+0x1c/+0x20/+0x24` are **f32 dimensions**, not u16 pivots.
4. The figure is not "12 segments x 20 bytes"; clip frame counts come from ARPP `rows`, and part
   counts from ARCP `cols` (29..88 per figure).
5. `people.df` shares this exact container format (same reader class), so the BHAV/behavior
   resources can be enumerated with the same `DougFile` parser - useful for the behavior-VM decode.

### What remains for pixel-exact rendering (open)

- How the engine fleshes each ARPP segment into the filled flat-shaded polygon look: the role of
  the ARCP `type` byte (0x08/0x0b/0x0e), the `(w,h,0.5)` dimension floats, and the palette color
  source. This lives in the scene-engine draw path reached from the render-node vtables
  (`PTR_FUN_004f5018` slot 3 `FUN_004c7cf0`/`FUN_004c8090`), not in the file.
- The `DAT_0058de80` per-state anim ids (600/700/750/800/850...) vs the ARPP clip names
  (101..495): the binding goes through `FUN_004ce6c0(bank@person+0x126, animId@person+0x17a)` and
  needs one more hop of decompilation (likely people.df's own directory ids).
- The ARPP per-record 4th byte ("scratch") - the game registers no swap and appears not to read
  it; confirm from the draw path.
