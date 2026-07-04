//===-- BedrockFrameLowering.cpp - Bedrock Frame Information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockFrameLowering.h"

#include "BedrockInstrInfo.h"
#include "BedrockSubtarget.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

BedrockFrameLowering::BedrockFrameLowering(const BedrockSubtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(16), 0,
                          Align(16)),
      STI(STI) {}

bool BedrockFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
         MFI.hasVarSizedObjects() || MFI.isFrameAddressTaken();
}

bool BedrockFrameLowering::hasReservedCallFrame(
    const MachineFunction &MF) const {
  return !MF.getFrameInfo().hasVarSizedObjects();
}

bool BedrockFrameLowering::canSimplifyCallFramePseudos(
    const MachineFunction &MF) const {
  return true;
}

MachineBasicBlock::iterator BedrockFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  const BedrockInstrInfo &TII = *STI.getInstrInfo();
  DebugLoc DL = MI->getDebugLoc();
  uint64_t Amount = MI->getOperand(0).getImm();

  if (hasReservedCallFrame(MF))
    return MBB.erase(MI);

  if (Amount != 0) {
    unsigned Opc = MI->getOpcode() == Bedrock::ADJCALLSTACKDOWN
                       ? Bedrock::SUB64ri
                       : Bedrock::ADD64ri;
    BuildMI(MBB, MI, DL, TII.get(Opc), Bedrock::SP)
        .addReg(Bedrock::SP)
        .addImm(Amount);
  }

  return MBB.erase(MI);
}

void BedrockFrameLowering::emitPrologue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  bool HasFP = hasFP(MF);
  uint64_t StackSize = MFI.getStackSize();
  if (hasReservedCallFrame(MF) && MFI.adjustsStack())
    StackSize += MFI.getMaxCallFrameSize();
  if (HasFP)
    StackSize += 16;
  if (hasReservedCallFrame(MF) && MFI.hasCalls())
    StackSize = alignTo(StackSize + 8, Align(16)) - 8;
  else
    StackSize = alignTo(StackSize, Align(16));
  MFI.setStackSize(StackSize);
  if (StackSize == 0)
    return;

  const BedrockInstrInfo &TII = *STI.getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  BuildMI(MBB, MBBI, DL, TII.get(Bedrock::SUB64ri), Bedrock::SP)
      .addReg(Bedrock::SP)
      .addImm(StackSize)
      .setMIFlag(MachineInstr::FrameSetup);

  if (HasFP) {
    BuildMI(MBB, MBBI, DL, TII.get(Bedrock::MOV64mr))
        .addReg(Bedrock::A7)
        .addReg(Bedrock::SP)
        .addImm(0)
        .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MBBI, DL, TII.get(Bedrock::MOV64rr), Bedrock::A7)
        .addReg(Bedrock::SP)
        .setMIFlag(MachineInstr::FrameSetup);
  }
}

void BedrockFrameLowering::emitEpilogue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  uint64_t StackSize = MF.getFrameInfo().getStackSize();
  if (StackSize == 0)
    return;

  const BedrockInstrInfo &TII = *STI.getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  if (hasFP(MF)) {
    BuildMI(MBB, MBBI, DL, TII.get(Bedrock::MOV64rr), Bedrock::SP)
        .addReg(Bedrock::A7)
        .setMIFlag(MachineInstr::FrameDestroy);
    BuildMI(MBB, MBBI, DL, TII.get(Bedrock::MOV64rm), Bedrock::A7)
        .addReg(Bedrock::SP)
        .addImm(0)
        .setMIFlag(MachineInstr::FrameDestroy);
  }

  BuildMI(MBB, MBBI, DL, TII.get(Bedrock::ADD64ri), Bedrock::SP)
      .addReg(Bedrock::SP)
      .addImm(StackSize)
      .setMIFlag(MachineInstr::FrameDestroy);
}
