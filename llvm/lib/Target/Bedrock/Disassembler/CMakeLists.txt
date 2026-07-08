add_llvm_component_library(LLVMBedrockDisassembler
  BedrockDisassembler.cpp

  LINK_COMPONENTS
  BedrockDesc
  BedrockInfo
  MC
  MCDisassembler
  Support

  ADD_TO_COMPONENT
  Bedrock
  )
