// WP4 revision round 5: the ThinLTO importing entry point.
//
// `clang -cc1 -fthinlto-index=<index> ... -emit-obj` dispatches into
// runThinLTOBackend and returns BEFORE EmitAssemblyHelper::emitAssembly -- the
// function that carries the DeferredChecks guard -- runs. The deferral flag is
// not set on this path either, so before round 5 no layer performed the MCS251
// contract check here: a module the ordinary entry rejects compiled to an
// object through the ThinLTO entry with rc=0.
//
// The round-4 review flagged this dispatch by static analysis only and
// explicitly left it unreproduced. Round 5 built a real index and measured it;
// the guard in clang/lib/CodeGen/BackendUtil.cpp now applies the same
// predicate, and this file keeps both entries in agreement.
//
// The check builds its own index (llvm-as / opt / llvm-lto2) and runs each
// module through both entries, so it exercises the real dispatch rather than a
// mock. It also requires a supported module to still compile through the
// ThinLTO entry.
//
// RUN: %python %S/Inputs/mcs251-r5-thinlto-check.py --clang-cc1 %clang_cc1
//
// The compilation unit for this test is the helper itself; this file carries
// the documentation and the RUN line.
int mcs251_r5_thinlto_check_marker;
