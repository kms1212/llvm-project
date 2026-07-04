add_llvm_component_group(Bedrock)

set(LLVM_TARGET_DEFINITIONS Bedrock.td)

tablegen(LLVM BedrockGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM BedrockGenCallingConv.inc -gen-callingconv)
tablegen(LLVM BedrockGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM BedrockGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM BedrockGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM BedrockGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(BedrockCommonTableGen)

add_llvm_target(BedrockCodeGen
  BedrockAsmPrinter.cpp
  BedrockBoundBranch.cpp
  BedrockFrameLowering.cpp
  BedrockInstrInfo.cpp
  BedrockISelDAGToDAG.cpp
  BedrockISelLowering.cpp
  BedrockMachineFunctionInfo.cpp
  BedrockMCInstLower.cpp
  BedrockPushPopMerge.cpp
  BedrockPushPopMergeArithmetic.cpp
  BedrockPushPopMergeCore.cpp
  BedrockPushPopMergeFrame.cpp
  BedrockPushPopMergeLoops.cpp
  BedrockPushPopMergeMemory.cpp
  BedrockPushPopMergeStack.cpp
  BedrockRegisterInfo.cpp
  BedrockSelectionDAGInfo.cpp
  BedrockSubtarget.cpp
  BedrockTargetMachine.cpp

  LINK_COMPONENTS
  AsmPrinter
  BedrockAsmParser
  CodeGen
  CodeGenTypes
  Core
  MC
  BedrockDesc
  BedrockInfo
  SelectionDAG
  Support
  Target
  TargetParser

  ADD_TO_COMPONENT
  Bedrock
  )

target_include_directories(LLVMBedrockCodeGen PRIVATE
  "${CMAKE_BINARY_DIR}/isa-design/build/generated")

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
