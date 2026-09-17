// RUN: %python %S/Inputs/mcs251-g11-stable-len.py %clang %t.dir
//
// G11-N6 (design rev.8 §8.1): an identity that does not fit the placement
// NOTE's u8 length field is an explicit frontend error, with a source
// location. Truncating it, hashing it shorter, appending an emission-time
// disambiguator and falling back to a bare name are all forbidden -- the
// helper asserts that the failing input emits no identity string at all, and
// that the accepted inputs emit their identity verbatim (no shortening).
//
// The helper generates the inputs so the exact identity length is known and
// exercises the boundary from both sides at one byte of resolution:
//   * external entity (identity == identifier): 254 ok, 255 ok, 256 error;
//   * file-scope static (<long file name>.<FNV>.<long symbol name>): exactly
//     255 ok, exactly 256 error. This is the "long file name combined with a
//     long symbol name" case.
// The G11-B emitter keeps its own fail-closed 255 guard as defence in depth
// for hand-written IR, which never passes through the frontend check.
static int anchor __attribute__((mcu_place_at(0x200), mcu_retain));