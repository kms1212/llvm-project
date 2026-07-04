//===-- BedrockFrameLowering.h - Bedrock Frame Information -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_BEDROCKFRAMELOWERING_H
#define LLVM_LIB_TARGET_BEDROCK_BEDROCKFRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {
class BedrockSubtarget;
class BedrockInstrInfo;

class BedrockFrameLowering : public TargetFrameLowering {
  const BedrockSubtarget &STI;

public:
  explicit BedrockFrameLowering(const BedrockSubtarget &STI);

  bool hasFPImpl(const MachineFunction &MF) const override;
  bool targetHandlesStackFrameRounding() const override { return true; }
  bool hasReservedCallFrame(const MachineFunction &MF) const override;
  bool canSimplifyCallFramePseudos(const MachineFunction &MF) const override;
  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI) const override;
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
};

} // end namespace llvm

#endif
