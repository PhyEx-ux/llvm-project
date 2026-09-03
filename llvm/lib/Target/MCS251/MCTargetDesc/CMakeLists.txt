add_llvm_component_library(LLVMMCS251Desc
  MCS251InstPrinter.cpp
  MCS251MCAsmInfo.cpp
  MCS251MCTargetDesc.cpp

  LINK_COMPONENTS
  MC
  MCS251Info
  Support

  ADD_TO_COMPONENT
  MCS251
  )
