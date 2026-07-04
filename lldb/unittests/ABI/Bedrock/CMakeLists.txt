add_lldb_unittest(ABIBedrockTests
  ABIBedrockTest.cpp

  LINK_COMPONENTS
    Bedrock
    Support

  LINK_LIBS
    lldbTarget
    lldbPluginABIBedrock
  )
