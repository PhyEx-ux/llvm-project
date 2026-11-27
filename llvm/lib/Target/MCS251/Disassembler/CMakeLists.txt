add_llvm_component_library(LLVMMCS251Disassembler
  MCS251Disassembler.cpp

  LINK_COMPONENTS
  MCS251Info
  CodeGenTypes
  MC
  MCDisassembler
  Support

  ADD_TO_COMPONENT
  MCS251
  )
