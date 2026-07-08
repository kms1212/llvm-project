add_llvm_component_library(LLVMBedrockAsmParser
  BedrockAsmParser.cpp

  LINK_COMPONENTS
  BedrockDesc
  BedrockInfo
  MC
  MCParser
  Support

  ADD_TO_COMPONENT
  Bedrock
  )
