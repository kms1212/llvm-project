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

set(BEDROCK_ISA_GENERATED_DIR "${CMAKE_BINARY_DIR}/isa-design/build/generated")
if(NOT EXISTS "${BEDROCK_ISA_GENERATED_DIR}/bedrock_asm_disasm.h")
  message(FATAL_ERROR
    "Bedrock generated assembler tables not found at "
    "${BEDROCK_ISA_GENERATED_DIR}/bedrock_asm_disasm.h. "
    "Build isa-design first so build/isa-design/build/generated is available.")
endif()

target_include_directories(LLVMBedrockAsmParser
  PRIVATE "${BEDROCK_ISA_GENERATED_DIR}")
