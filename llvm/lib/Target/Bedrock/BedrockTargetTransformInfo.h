//===-- BedrockTargetTransformInfo.h - Bedrock specific TTI -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_BEDROCKTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_BEDROCK_BEDROCKTARGETTRANSFORMINFO_H

#include "BedrockSubtarget.h"
#include "BedrockTargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/IR/Function.h"

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

  TargetTransformInfo::PopcntSupportKind
  getPopcntSupport(unsigned TyWidth) const override {
    return TyWidth <= 64 ? TTI::PSK_FastHardware : TTI::PSK_Software;
  }

  unsigned getNumberOfRegisters(unsigned ClassID) const override {
    return ClassID == 0 ? 16 : 0;
  }

  TypeSize
  getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const override {
    switch (K) {
    case TTI::RGK_Scalar:
      return TypeSize::getFixed(64);
    case TTI::RGK_FixedWidthVector:
      return TypeSize::getFixed(0);
    case TTI::RGK_ScalableVector:
      return TypeSize::getScalable(0);
    }
    llvm_unreachable("unsupported register kind");
  }
};

} // end namespace llvm

#endif
