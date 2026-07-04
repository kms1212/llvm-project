//===-- BedrockMachineFunctionInfo.h - Bedrock per-function info -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_BEDROCKMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_BEDROCK_BEDROCKMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Register.h"

namespace llvm {

class BedrockMachineFunctionInfo : public MachineFunctionInfo {
  virtual void anchor();

  Register SRetReturnReg = 0;
  int VarArgsFrameIndex = -1;

public:
  BedrockMachineFunctionInfo() = default;

  BedrockMachineFunctionInfo(const Function &F,
                             const TargetSubtargetInfo *STI) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  Register getSRetReturnReg() const { return SRetReturnReg; }
  void setSRetReturnReg(Register Reg) { SRetReturnReg = Reg; }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int FI) { VarArgsFrameIndex = FI; }
};

} // end namespace llvm

#endif
