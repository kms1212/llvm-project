add_llvm_component_library(LLVMBedrockDesc
  BedrockAsmBackend.cpp
  BedrockELFObjectWriter.cpp
  BedrockInstPrinter.cpp
  BedrockMCAsmInfo.cpp
  BedrockMCCodeEmitter.cpp
  BedrockMCEncoding.cpp
  BedrockMCTargetDesc.cpp

  LINK_COMPONENTS
  BedrockInfo
  MC
  Support

  ADD_TO_COMPONENT
  Bedrock
  )
