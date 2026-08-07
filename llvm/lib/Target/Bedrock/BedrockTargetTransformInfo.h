//===-- BedrockTargetTransformInfo.h - Bedrock specific TTI ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_BEDROCKTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_BEDROCK_BEDROCKTARGETTRANSFORMINFO_H

#include "BedrockISelLowering.h"
#include "BedrockSubtarget.h"
#include "BedrockTargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/Support/MathExtras.h"

namespace llvm {

class BedrockTTIImpl final : public BasicTTIImplBase<BedrockTTIImpl> {
  using BaseT = BasicTTIImplBase<BedrockTTIImpl>;
  using TTI = TargetTransformInfo;
  friend BaseT;

  const BedrockSubtarget *ST;
  const BedrockTargetLowering *TLI;

  const BedrockSubtarget *getST() const { return ST; }
  const BedrockTargetLowering *getTLI() const { return TLI; }

public:
  explicit BedrockTTIImpl(const BedrockTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  InstructionCost getArithmeticInstrCost(
      unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo Op1Info = {TTI::OK_AnyValue, TTI::OP_None},
      TTI::OperandValueInfo Op2Info = {TTI::OK_AnyValue, TTI::OP_None},
      ArrayRef<const Value *> Args = {},
      const Instruction *CxtI = nullptr) const override {
    int ISD = TLI->InstructionOpcodeToISD(Opcode);

    switch (ISD) {
    default:
      return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info,
                                           Op2Info, Args, CxtI);
    case ISD::MUL:
      return 3 * BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info,
                                               Op2Info, Args, CxtI);
    case ISD::SDIV:
    case ISD::UDIV:
    case ISD::SREM:
    case ISD::UREM:
      return 8 * BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info,
                                               Op2Info, Args, CxtI);
    }
  }

  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP,
                               OptimizationRemarkEmitter *ORE) const override {
    BaseT::getUnrollingPreferences(L, SE, UP, ORE);
    UP.Threshold = 0;
    UP.OptSizeThreshold = 0;
    UP.PartialThreshold = 0;
    UP.PartialOptSizeThreshold = 0;
    UP.MaxCount = 1;
    UP.FullUnrollMaxCount = 1;
    UP.Partial = false;
    UP.Runtime = false;
    UP.UpperBound = false;
    UP.UnrollRemainder = false;
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_BEDROCK_BEDROCKTARGETTRANSFORMINFO_H
