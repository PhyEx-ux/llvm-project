add_lldb_library(lldbPluginABIMCS251 PLUGIN
  ABISysV_mcs251.cpp

  LINK_COMPONENTS
    Support
    TargetParser
  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbValueObject
  )
