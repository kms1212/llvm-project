add_llvm_component_group(Bedrock)

set(LLVM_TARGET_DEFINITIONS Bedrock.td)

tablegen(LLVM BedrockGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM BedrockGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM BedrockGenCallingConv.inc -gen-callingconv)
tablegen(LLVM BedrockGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM BedrockGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM BedrockGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM BedrockGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM BedrockGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM BedrockGenSDNodeInfo.inc -gen-sd-node-info)
tablegen(LLVM BedrockGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(BedrockCommonTableGen)

add_llvm_target(BedrockCodeGen
  BedrockAsmPrinter.cpp
  BedrockFrameLowering.cpp
  BedrockInstrInfo.cpp
  BedrockISelDAGToDAG.cpp
  BedrockISelLowering.cpp
  BedrockPreEmitPeephole.cpp
  BedrockRegisterInfo.cpp
  BedrockSelectionDAGInfo.cpp
  BedrockSubtarget.cpp
  BedrockTargetMachine.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  BedrockDesc
  BedrockInfo
  MC
  SelectionDAG
  Support
  Target
  TargetParser

  ADD_TO_COMPONENT
  Bedrock
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
