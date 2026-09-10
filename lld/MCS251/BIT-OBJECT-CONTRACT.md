# MCS251 `bit` object ELF input contract (lld-frozen, v1)

Status: **frozen lld-side input contract**, 2026-09-10. Owned by the lld stream.
This document freezes what `lld/MCS251` accepts as the ELF encoding of persistent
`bit` objects and how it allocates their slots. It is the lld counterpart of
`BIT-TASK-BREAKDOWN.md` cards BT12/BT13/BT14 (lld reader/allocator/init) and of
`BIT-FIRST-CLASS-INCREMENT.md` §3. The backend stream must emit exactly this
shape; the relocation numbers are an lld-local extension until BT00 registers
them in the public header (see §6).

## 1. Address model (normative)

- Bit-addressable internal RAM = bit addresses `0x00..0x7F` (128 bits) mapping
  to bytes `0x20..0x2F` (16 bytes).
- `backing_byte(bit) = 0x20 + (bit >> 3)`, `bit_index(bit) = bit & 7`.
- Bit address 0 is legal and must not be confused with a null pointer.
- Bit addresses `0x80..0xFF` are SFR bit references: no RAM backing, no window
  byte, no participation in slot allocation.

## 2. Physical pool vs. sub-allocation (the BT07/BT14 boundary)

The 16-byte window `[0x20,0x30)` is **physically reserved exactly once**. lld
distinguishes two roles that the earlier incremental draft conflated:

- **Pool reservation** (physical): the byte range is taken out of the ordinary
  RAM ledger. Today this is the existing `.mcs251.BSEG_BYTES` slice (the IRQ CRT
  owns 16 bytes; `LinkerCore.cpp` allocates it at `0x20`), plus the legacy
  `BIT_BANK` overlay group. A `BIT_BANK` byte is unavailable to automatic bit
  objects: legacy overlay and the new pool are never implicitly overlaid.
- **Bit sub-allocation** (logical): individual bit slots inside the pool. When
  the byte is already covered by a `.mcs251.BSEG_BYTES` slice, lld adds **no**
  second DATA-ledger reservation. Only bytes outside such a slice (non-CRT
  links) are reserved byte-wise by lld itself.

Consequences enforced here:

- A byte owned by a fixed bit reference is exclusive: no automatic bit object
  may enter any bit of it (design §3.2.6). A fixed reference inside the CRT
  pool does not re-reserve the byte.
- An automatic bit object in a byte that is not pool-covered makes lld reserve
  that byte, excluding it from DSEG/OSEG/ISEG/SSEG and from ordinary RAM.
- The old IRQ CRT unconditionally clears all 16 pool bytes and then applies
  XINIT; that is the S1 "owned 16B, all-zero" subset. Preserving an *external*
  owner's neighbouring bits needs the BT14 `crt-bit.yaml` profile and is out of
  scope here (lld carries the ownership metadata it will need).

## 3. Section: `.mcs251.bit`

A non-ALLOC `SHT_PROGBITS` section, `sh_flags = 0`, `sh_addralign = 4`, holding
one 8-byte **bit-object record** per record. It carries no loadable bytes. At
most one `.mcs251.bit` per object (like `.mcs251.isr`); `sh_entsize` must be 0.

Record layout (big-endian, fixed 8 bytes; `sh_size % 8 == 0` required):

| offset | size | field | rule |
|---|---|---|---|
| 0 | 1 | `version` | must be `1` |
| 1 | 1 | `kind` | `1` = definition, `2` = fixed reference |
| 2 | 1 | `init_value` | `0` or `1`; ignored for kind 2 |
| 3 | 1 | `capabilities` | must be `1` (the frozen capability word) |
| 4 | 4 | `symbol_reference` | four zero bytes; the association is carried by exactly one `R_MCS251_BIT_REF` at `r_offset = record + 4` |

Each record must carry exactly one association relocation, and each relocation
must land on a record base + 4; unmatched records/relocations are errors.

- `kind = 1` (definition): the referenced symbol must be a defined
  `STT_OBJECT` with `st_size = 1` and `st_shndx` = this `.mcs251.bit` section,
  `st_value` = the record's byte offset (a multiple of 8). The symbol names the
  persistent bit object across translation units. LOCAL = file-static bit;
  GLOBAL = `extern bit`. Duplicate strong definitions and undefined strong
  references follow the ordinary symbol rules.
- `kind = 2` (fixed reference): the referenced symbol must be a defined
  `SHN_ABS` `STT_OBJECT` whose `st_value` is a bit address `0..0xFF`. Values
  `0x00..0x7F` reserve the whole backing byte; `0x80..0xFF` are SFR references
  that reserve nothing. Several aliases of the same address are idempotent.

A cross-TU *use* needs no record of its own: a `R_MCS251_BITADDR8` relocation
names the defining symbol, which resolves to the single definition.

## 4. Relocations

Two bit relocations, both rejected on ordinary ALLOC sections unless stated.
The numbers `10`/`11` are lld-local constants (`LinkerCore.cpp`); BT00 owns
registering the public `R_MCS251_*` names in `ELFRelocs/MCS251.def`.

### 4.1 `R_MCS251_BIT_REF` (type 10, width 0)

Identity association only, valid **exclusively** inside `.rela.mcs251.bit`
(the section must be named exactly `.rela.mcs251.bit`). Zero addend. It writes
no bytes. Using it anywhere else (mirroring the `R_MCS251_ISR_REF` rule) is an
error.

### 4.2 `R_MCS251_BITADDR8` (type 11, width 1)

Writes the resolved bit address into one byte of a bit instruction's address
field. `r_offset` is the field start, the field is zero-filled by the producer,
`r_addend` must be 0, and bit address 0 writes `0x00` (never treated as null).

**Field position (frozen).** The write location must be the bit-address operand
of a bit instruction: the byte immediately following that instruction's opcode
byte, in an executable (`SHF_EXECINSTR`) PROGBITS section. The accepted opcode
bytes are the frozen classic encodes `D2` (setb), `C2` (clr), `B2` (cpl), `92`
(mov bit,c), `A2` (mov c,bit), `20` (jb), `30` (jnb), `10` (jbc). The producer
must zero-fill the field, so a nonzero placeholder byte is rejected.

Because the section is a byte stream with no instruction metadata, position is
proved by **decoding the whole stream from offset 0 to the section end** with
the exact instruction lengths of the backend emitter (`mcs251InstrLen`,
mirroring `MCTargetDesc/MCS251MCCodeEmitter.cpp`); the field must be the second
byte of a bit-opcode instruction. A byte that merely *looks* like a bit opcode
(e.g. `75`/`74` where `D2` is an immediate or the following instruction's
opcode) does not satisfy the boundary, an unknown opcode anywhere in the stream
fails, and **a truncated instruction anywhere — including after the last field
— fails closed**. This decode runs **twice**: first on the producer's original
unrelocated bytes (so a relocation cannot rewrite an earlier byte to launder a
field that was never a bit operand into one), and again on the final image after
all relocations (so a relocation cannot rewrite the field's own opcode either).
Both checks are mandatory. A relocation that lands on an opcode/immediate byte,
on the first byte of a section, or in a non-executable section is rejected
regardless of a valid symbol target: symbol identity being valid is not
field-position validity.

Target validation:

- a bit object definition (symbol in `.mcs251.bit`), or
- a defined `SHN_ABS STT_OBJECT` bit reference **that is registered by a kind-2
  record in some input object's `.mcs251.bit`**.

The registration requirement is normative: an `SHN_ABS STT_OBJECT` whose bit
address was never registered by a kind-2 record is not a known bit reference, so
a `BITADDR8` naming it is rejected (the address would otherwise be an
unvalidated arbitrary 8-bit constant, and a fixed backing byte would bypass the
reservation conflict rules of §5 step 1). A registered reference is identified
by the kind-2 record's resolved symbol; several aliases of one address are
idempotent.

Anything else — a function, a `.text`/DSEG object, a section+addend fold, an
undefined symbol, an unregistered ABS object — is rejected. Conversely, an
ordinary address relocation (`R_MCS251_16/24/LO8/...`) that targets a
`.mcs251.bit` symbol is rejected: bit symbols have no byte address.
NOBITS/out-of-bounds/overlapping writes are rejected as for every other
relocation.

## 5. Allocation algorithm

Runs once, inside `layoutData()` after the `BSEG_BYTES` slices and the
`BIT_BANK` overlay are placed, and before the DSEG/OSEG/ISEG/SSEG first-fit
classes. Input order is (input-file order, section-header order, record order),
which makes the result byte-reproducible for a fixed command line.

1. **Reserve fixed references.** For each kind-2 record in order, compute the
   backing byte. If it lies in a `BSEG_BYTES` pool it is already physically
   reserved (no second reservation); otherwise reserve the byte in the DATA
   ledger. A collision with a user reservation (`--reserve-data`,
   `.mcs251.DATA.*`, `BIT_BANK`) is a hard error. Mark the byte exclusive.
2. **Pack definitions.** For each kind-1 record in order, first-fit the lowest
   bit address whose backing byte is neither exclusive (step 1) nor blocked by
   a non-pool DATA reservation nor a legacy `BIT_BANK` byte. Bytes are packed
   from `0x00` upward; different translation units share a byte's free bits.
   The byte is reserved once, on the first bit that lands in it, unless a
   `BSEG_BYTES` pool already covers it.
3. **Exhaustion is a hard failure.** If a definition cannot be placed in the
   128-bit window the link fails, reporting the object, the requested and
   available bit counts and the first unplaced symbol. It never degrades to
   byte RAM.

`l_DSEG` and the stack high-water automatically include every byte lld reserved
(`reserve()` is the single ledger), so ordinary RAM cannot alias a bit byte.

## 6. Relocation numbering note

`ELFRelocs/MCS251.def` allocates 0..9 today (`9 = R_MCS251_ISR_REF`). The bit
relocations are frozen here as `10` (`BIT_REF`) and `11` (`BITADDR8`), matching
the BT13 card's two-relocation model. The lld parser accepts the raw numeric
types, so the backend can emit them immediately; `llvm-readobj` prints
`Unknown (10/11)` until BT00 registers the public names. Registering them is an
`llvm/**` change owned by the backend stream — not made here.

## 7. Initialization (CRT wiring)

- lld aggregates, per automatic backing byte, `mask` (bits owned by kind-1
  records) and `value` (bits whose `init_value` is 1). Fixed-reference bytes
  are never in a mask: they are the user's, never lld-initialized.
- For every byte with `value != 0` lld synthesizes a v1 XINIT record
  `{u16 dest = backing byte, u16 size = 1, u16 payload = 1, u8 value}` in a
  synthesized `.mcs251.xinit.bit` PROGBITS section. Existing CRT order —
  explicit clear of `[0x20,0x30)`, then the `__mcs251_globals_init` walker —
  gives "clear first, explicit value afterwards". Zero-initialized bits come
  from that clear; NOBITS is never trusted as power-on zero.
- The synthesized section participates in the ordinary XINIT checks and the
  XINIT area gate, so a nonzero bit initial value without
  `--area-start=XINIT` fails rather than silently dropping the value. It is
  also placed through the same always-on CODE range/occupancy check as every
  input XINIT section (`rangeFits` 24-bit limit + `CodeUsed` overlap); the
  optional `--flash-base/--flash-size` board gate is never a substitute for
  that architectural bound.
- **Initialization ownership (frozen).** A backing byte with at least one
  automatic bit (a byte in `mask`) is initialized exclusively by the allocator:
  the CRT clear plus the synthesized record. An *input* XINIT record whose
  destination range covers any such byte is a hard error, not an ordering
  accident — it would either override a declared nonzero bit value or fight the
  clear for a declared zero, and the map would then disagree with the image.
  Fixed-reference-only bytes are not in `mask`, so they remain the user's and
  are not restricted. A nonzero-object byte and a same-destination input record
  are therefore rejected rather than producing two records for one byte.
  This check is **unconditional**: it runs whether or not a
  `__mcs251_globals_init` walker is linked, so dropping the CRT cannot bypass
  it. Independently, a nonzero automatic bit initial value with **no**
  `__mcs251_globals_init` in the link fails closed: without the walker the
  synthesized record could never be applied, and the value must not be
  silently dropped.
- No section is synthesized when no nonzero value exists, so links without bit
  objects keep byte-identical images and maps (frozen artifacts).
- The map gains `BIT`/`BITBYTE` rows only when bit objects exist. `BITBYTE`
  rows name the byte's physical owner and its initialization strategy
  (`crt-clear`, `crt-clear+xinit`, `xinit`, `none`, or the `user-xinit`
  variants for a fixed-only byte the user's own input record writes). A
  fixed-reference-only RAM byte gets its own `BITBYTE ... mask 0x00 owner fixed
  <name>` row so it is traceable, and its `BIT` row reports `init user-xinit`
  versus `init none` so "no declared initial value" is distinguishable from
  "initialized to 0". A bit-slot exhaustion report lists **every** blocked byte
  (untruncated) with its occupying source and every already-allocated bit with
  its owner, so a full 128-bit window is fully attributable.
