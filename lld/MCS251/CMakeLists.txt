add_lld_library(lldMCS251
  Driver.cpp
  LinkerCore.cpp

  LINK_COMPONENTS
  BinaryFormat
  Object
  Support

  LINK_LIBS
  lldCommon
  )
