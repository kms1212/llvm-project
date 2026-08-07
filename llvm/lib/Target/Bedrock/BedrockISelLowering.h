//===-- BedrockISelLowering.h - Bedrock DAG lowering interface --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_BEDROCKISELLOWERING_H
#define LLVM_LIB_TARGET_BEDROCK_BEDROCKISELLOWERING_H

#include "BedrockSelectionDAGInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

class BedrockSubtarget;

class BedrockTargetLowering : public TargetLowering {
  const BedrockSubtarget &Subtarget;

public:
  BedrockTargetLowering(const TargetMachine &TM, const BedrockSubtarget &STI);

  const char *getTargetNodeName(unsigned Opcode) const override;
  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override;
  MVT getScalarShiftAmountTy(const DataLayout &DL, EVT VT) const override;
  SDValue BuildSDIVPow2(SDNode *N, const APInt &Divisor, SelectionDAG &DAG,
                        SmallVectorImpl<SDNode *> &Created) const override;
  unsigned getJumpTableEncoding() const override;
  bool isJumpTableRelative() const override { return true; }
  bool ShouldShrinkFPConstant(EVT VT) const override { return false; }
  bool enableAggressiveFMAFusion(EVT VT) const override;
  bool isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                  EVT VT) const override;
  bool isFMAFasterThanFMulAndFAdd(const Function &F,
                                  Type *Ty) const override;
  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *MBB) const override;

  bool shouldInsertFencesForAtomic(const Instruction *I) const override;
  Instruction *emitLeadingFence(IRBuilderBase &Builder, Instruction *Inst,
                                AtomicOrdering Ord) const override;
  Instruction *emitTrailingFence(IRBuilderBase &Builder, Instruction *Inst,
                                 AtomicOrdering Ord) const override;
  AtomicExpansionKind
  shouldExpandAtomicRMWInIR(AtomicRMWInst *RMW) const override;

private:
  SDValue PerformDAGCombine(SDNode *N,
                            DAGCombinerInfo &DCI) const override;
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;

  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context, const Type *RetTy) const override;

  SDValue LowerCallResult(SDValue Chain, SDValue InGlue,
                          CallingConv::ID CallConv, bool IsVarArg,
                          const SmallVectorImpl<ISD::InputArg> &Ins,
                          const SDLoc &DL, SelectionDAG &DAG,
                          SmallVectorImpl<SDValue> &InVals) const;
  SDValue LowerBRCOND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSETCC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerMinMax(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSIGN_EXTEND_INREG(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_BEDROCK_BEDROCKISELLOWERING_H
