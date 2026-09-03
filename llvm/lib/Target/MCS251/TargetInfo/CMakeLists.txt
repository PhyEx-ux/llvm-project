add_llvm_component_library(LLVMMCS251Info
  MCS251TargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  MCS251
  )
