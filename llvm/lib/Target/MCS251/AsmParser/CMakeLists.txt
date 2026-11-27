add_llvm_component_library(LLVMMCS251AsmParser
  MCS251AsmParser.cpp

  LINK_COMPONENTS
  MCS251Desc
  MCS251Info
  CodeGenTypes
  MC
  MCParser
  Support

  ADD_TO_COMPONENT
  MCS251
  )
