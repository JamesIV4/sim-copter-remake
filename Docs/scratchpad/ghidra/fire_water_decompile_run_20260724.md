# Fire/water/dust-backwash target decompile audit

Run date: 2026-07-24  
Source plan: `Docs/FireWaterDustBackwashPortPlan.md`  
Decompiler: `Tools/re-agent/.venv/Scripts/ghidra-bridge.exe decompile`

Each command returned exit code 0. The audit checked that the output contained the
matching Ghidra function header, address, declaration, and C body. `SHA256-16` is
the first 16 hexadecimal characters of the SHA-256 hash of the UTF-8 command
output.

| Address | Function | Body chars | SHA256-16 |
|---|---|---:|---|
| `0x0046edb0` | `FUN_0046edb0` | 2,308 | `57FA267926E5A63E` |
| `0x004aeba0` | `FUN_004aeba0` | 4,837 | `40DB632979D1714E` |
| `0x0048da50` | `FUN_0048da50` | 1,743 | `FC05B5DFB1EA7D72` |
| `0x0048db20` | `FUN_0048db20` | 7,266 | `428C506DEBC20E4B` |
| `0x0048e0b0` | `FUN_0048e0b0` | 16,438 | `EA1DFD8104FEEA80` |
| `0x004af220` | `FUN_004af220` | 2,463 | `2D2595A9911D24C4` |
| `0x004af100` | `FUN_004af100` | 1,751 | `D24724C9A72962E2` |
| `0x004af3b0` | `FUN_004af3b0` | 4,102 | `6DEF68743AC4587E` |
| `0x00490690` | `FUN_00490690` | 15,001 | `5CF0E2201FEC4C5B` |
| `0x0048ed00` | `FUN_0048ed00` | 39,767 | `84E40464D64B346C` |
| `0x00488060` | `FUN_00488060` | 2,179 | `1E1DC19D828A5E85` |
| `0x004881b0` | `FUN_004881b0` | 1,884 | `9CA75DF62556B62A` |
| `0x00489250` | `FUN_00489250` | 4,893 | `156992038E4CDFB2` |
| `0x0048a8b0` | `FUN_0048a8b0` | 6,941 | `D21740AFCDD06EC6` |
| `0x00484d20` | `FUN_00484d20` | 15,124 | `5D795E13935988C5` |
| `0x0047a760` | `FUN_0047a760` | 1,570 | `9E5B05048F8CD951` |
| `0x00483c20` | `FUN_00483c20` | 10,386 | `87B6AB78AE65030B` |

The 14 implementation targets also completed `xrefs-to`, `xrefs-from`, and
`re-agent reverse --dry-run` preflight successfully.

The paid bounded review was started with the first function in the plan rather
than the master updater:

```powershell
& Tools/re-agent/.venv/Scripts/re-agent.exe reverse `
    --address 0x0046edb0 --max-rounds 3
```

It completed all three rounds with semantic `PASS` and objective `PASS`. The
candidate is saved as
`Docs/scratchpad/re-agent/code/0x0046edb0__0x0046edb0.cpp`.
