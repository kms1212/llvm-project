add_llvm_component_library(LLVMBedrockInfo
  BedrockTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  Bedrock
  )
