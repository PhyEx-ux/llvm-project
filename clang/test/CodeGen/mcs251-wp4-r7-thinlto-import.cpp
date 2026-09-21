// WP4 revision round 7 (R2): the ThinLTO importing entry judges the module the
// IMPORTER produced, not only the caller module the invocation loaded.
//
// With a real distributed index (llvm-lto2 run --thinlto-distributed-indexes)
// built from two modules -- a caller that only DECLARES and calls `callee`, and
// a callee whose body contains a sequence the optimizer removes (alloca /
// store / atomic load / ret) -- the pre-import structural check had nothing to
// judge in the caller, and optimization erased the imported construct before
// the arithmetic hook could see it. The ThinLTO entry produced IR at rc=0 and
// an EM_MCS251 ELF object, while the same post-import IR handed to the ORDINARY
// entry exited 1 in all four O0/O2 x IR/object cells with the atomic
// diagnostic.
//
// Round 7 adds a structural gate on the POST-IMPORT module, before the
// importing/optimization pipeline (Conf.PostImportModuleHook, composed with any
// pre-existing hook rather than replacing it), so the importing entry reaches
// the ordinary entry's verdict.
//
// Scope, deliberately not widened: what is proven is that the importing backend
// has no structural gap for the content it actually imports. Nothing here
// claims that a whole link accepts or rejects unsupported programs; this test
// performs no link.
//
// The helper builds the index itself and runs the real dispatch. Per callee
// variant, per O0/O2 x IR/object it asserts: the caller ALONE is accepted by
// the ordinary entry (so the verdict is not the caller's own); the ThinLTO
// entry on caller + index exits exactly 1 with an ordinary atomic diagnostic,
// no artifact and no crash text (the O2 cell is the structural gate's); an
// atomic-free callee imports and is accepted with a parsed artifact; and the
// same content linked into one module is rejected by the ordinary entry.
//
// RUN: %python %S/Inputs/mcs251-r7-thinlto-import-check.py %clang_cc1

int mcs251_r7_thinlto_import_marker;
