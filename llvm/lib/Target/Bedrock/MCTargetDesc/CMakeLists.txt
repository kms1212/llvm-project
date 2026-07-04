set(BEDROCK_ISA_GENERATED_DIR "${CMAKE_BINARY_DIR}/isa-design/build/generated")
set(BEDROCK_ISA_ENCODER "${BEDROCK_ISA_GENERATED_DIR}/bedrock_asm_disasm.c")
if(NOT EXISTS "${BEDROCK_ISA_ENCODER}")
  message(FATAL_ERROR
    "Bedrock generated assembler encoder not found at ${BEDROCK_ISA_ENCODER}. "
    "Build isa-design first so build/isa-design/build/generated is available.")
endif()

add_llvm_component_library(LLVMBedrockDesc
  BedrockAsmBackend.cpp
  BedrockELFObjectWriter.cpp
  BedrockInstPrinter.cpp
  BedrockMCCodeEmitter.cpp
  BedrockMCAsmInfo.cpp
  BedrockMCTargetDesc.cpp
  ${BEDROCK_ISA_ENCODER}

  LINK_COMPONENTS
  BedrockInfo
  BinaryFormat
  MC
  Support
  TargetParser

  ADD_TO_COMPONENT
  Bedrock
  )

target_include_directories(LLVMBedrockDesc PRIVATE "${BEDROCK_ISA_GENERATED_DIR}")
