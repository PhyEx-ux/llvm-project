add_lld_library(lldMCS251
  Driver.cpp
  LinkerCore.cpp
  PlacementReport.cpp

  PARTIAL_SOURCES_INTENDED

  LINK_COMPONENTS
  BinaryFormat
  Object
  Support

  LINK_LIBS
  lldCommon
  )

# G11-D (design §3.4, contract §6.2): the independent placement verifier.  It
# is a standalone executable linking the LLVM Object/Support libraries; it
# deliberately does NOT link lldMCS251, so it can never share the producer's
# merge/parse oracle (contract §2.4/§3).  Build and install travel with lld;
# no Python or source-tree location is a runtime dependency.
#
# `add_lld_tool` forwards to `add_llvm_executable`, which does not parse a
# LINK_COMPONENTS keyword -- the components are taken from the
# LLVM_LINK_COMPONENTS variable, matching lld/tools/lld (and this file lists
# every .cpp of the directory in one of the two targets, so the "all sources
# are in a target" invariant holds without PARTIAL_SOURCES_INTENDED).
set(LLVM_LINK_COMPONENTS
  Object
  Support
  )

add_lld_tool(mcs251-placement-verify
  PlacementVerify.cpp
  PlacementVerifyMain.cpp
  )