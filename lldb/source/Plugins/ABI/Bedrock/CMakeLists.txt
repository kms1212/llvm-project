add_lldb_library(lldbPluginABIBedrock PLUGIN
  ABIBedrock.cpp

  LINK_COMPONENTS
    Support
    TargetParser

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbValueObject
  )
