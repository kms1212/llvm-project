//===-- BedrockFrameLowering.h - Bedrock frame lowering ---------*- C++ -*-===//
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

class BedrockFrameLowering : public TargetFrameLowering {
public:
  explicit BedrockFrameLowering(const BedrockSubtarget &STI);

  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS = nullptr) const override;

  bool assignCalleeSavedSpillSlots(
      MachineFunction &MF, const TargetRegisterInfo *TRI,
      std::vector<CalleeSavedInfo> &CSI) const override;
  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 ArrayRef<CalleeSavedInfo> CSI,
                                 const TargetRegisterInfo *TRI) const override;
  bool restoreCalleeSavedRegisters(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
      MutableArrayRef<CalleeSavedInfo> CSI,
      const TargetRegisterInfo *TRI) const override;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI) const override;

  bool hasReservedCallFrame(const MachineFunction &MF) const override {
    return false;
  }

  bool needsFrameIndexResolution(const MachineFunction &MF) const override {
    return true;
  }

  bool targetHandlesStackFrameRounding() const override { return true; }

protected:
  bool hasFPImpl(const MachineFunction &MF) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_BEDROCK_BEDROCKFRAMELOWERING_H
