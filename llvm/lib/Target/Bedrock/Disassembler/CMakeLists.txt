set(BEDROCK_ISA_GENERATED_DIR "${CMAKE_BINARY_DIR}/isa-design/build/generated")
if(NOT EXISTS "${BEDROCK_ISA_GENERATED_DIR}/bedrock_asm_disasm.h")
  message(FATAL_ERROR
    "Bedrock generated disassembler tables not found at "
    "${BEDROCK_ISA_GENERATED_DIR}/bedrock_asm_disasm.h. "
    "Build isa-design first so build/isa-design/build/generated is available.")
endif()

add_llvm_component_library(LLVMBedrockDisassembler
  BedrockDisassembler.cpp

  LINK_COMPONENTS
  BedrockDesc
  BedrockInfo
  MCDisassembler
  Support

  ADD_TO_COMPONENT
  Bedrock
  )

target_include_directories(LLVMBedrockDisassembler
  PRIVATE "${BEDROCK_ISA_GENERATED_DIR}")
