//===-- BedrockMachineFunctionInfo.h - Bedrock function info ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_BEDROCKMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_BEDROCK_BEDROCKMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class BedrockMachineFunctionInfo final : public MachineFunctionInfo {
  Register SRetReturnReg;
  int VarArgsFrameIndex = 0;
  bool HasVarArgsFrameIndex = false;

public:
  BedrockMachineFunctionInfo(const Function &F,
                             const TargetSubtargetInfo *STI) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override {
    return DestMF.cloneInfo<BedrockMachineFunctionInfo>(*this);
  }

  Register getSRetReturnReg() const { return SRetReturnReg; }
  void setSRetReturnReg(Register Reg) { SRetReturnReg = Reg; }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  bool hasVarArgsFrameIndex() const { return HasVarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Index) {
    VarArgsFrameIndex = Index;
    HasVarArgsFrameIndex = true;
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_BEDROCK_BEDROCKMACHINEFUNCTIONINFO_H
