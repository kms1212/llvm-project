//===-- BedrockFrameLowering.cpp - Bedrock frame lowering -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockFrameLowering.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

BedrockFrameLowering::BedrockFrameLowering(const BedrockSubtarget &STI)
    // Generic call-frame tracking rounds adjustments to this value.  Near
    // calls need an exact eight-byte phase adjustment; emitPrologue still
    // rounds every persistent function body frame to the ABI's 16 bytes.
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(8), 0,
                          Align(8)) {}

void BedrockFrameLowering::emitPrologue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t StackSize = MFI.getStackSize();
  // A function observes a 16-byte-aligned SP on entry and keeps its body
  // frame aligned.  Near-call padding and outgoing arguments are allocated
  // dynamically around each call rather than being folded into this frame.
  StackSize = alignTo(StackSize, Align(16));
  MFI.setStackSize(StackSize);
  if (StackSize == 0)
    return;

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  BuildMI(MBB, MBBI, DL, TII.get(Bedrock::ADJSP_DOWN)).addImm(StackSize);
}

void BedrockFrameLowering::emitEpilogue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t StackSize = MFI.getStackSize();
  if (StackSize == 0)
    return;

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  BuildMI(MBB, MBBI, DL, TII.get(Bedrock::ADJSP_UP)).addImm(StackSize);
}

MachineBasicBlock::iterator BedrockFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  int64_t Amount = MI->getOperand(0).getImm();
  if (Amount != 0) {
    const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
    unsigned Opcode = MI->getOpcode() == Bedrock::ADJCALLSTACKDOWN
                          ? Bedrock::ADJSP_DOWN
                          : Bedrock::ADJSP_UP;
    BuildMI(MBB, MI, MI->getDebugLoc(), TII.get(Opcode)).addImm(Amount);
  }
  return MBB.erase(MI);
}

bool BedrockFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  return false;
}
