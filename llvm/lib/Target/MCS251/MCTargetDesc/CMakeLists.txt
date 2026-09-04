add_llvm_component_library(LLVMMCS251Desc
  MCS251AsmBackend.cpp
  MCS251InstPrinter.cpp
  MCS251MCAsmInfo.cpp
  MCS251MCCodeEmitter.cpp
  MCS251MCTargetDesc.cpp
  MCS251RELObjectWriter.cpp

  LINK_COMPONENTS
  MC
  MCS251Info
  Support

  ADD_TO_COMPONENT
  MCS251
  )
