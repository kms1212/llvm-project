//===-- BedrockRegisterInfo.cpp - Bedrock Register Information ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockRegisterInfo.h"

#include "BedrockFrameLowering.h"
#include "BedrockInstrInfo.h"
#include "BedrockSubtarget.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "BedrockGenRegisterInfo.inc"

BedrockRegisterInfo::BedrockRegisterInfo(const BedrockSubtarget &STI)
    : BedrockGenRegisterInfo(Bedrock::PC), STI(STI) {}

const MCPhysReg *
BedrockRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_Bedrock_SaveList;
}

const uint32_t *
BedrockRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                          CallingConv::ID CC) const {
  return CSR_Bedrock_RegMask;
}

BitVector
BedrockRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  Reserved.set(Bedrock::SP);
  Reserved.set(Bedrock::PC);
  Reserved.set(Bedrock::FLAGS);
  Reserved.set(Bedrock::STATUS);

  if (STI.getFrameLowering()->hasFP(MF))
    Reserved.set(Bedrock::A7);

  return Reserved;
}

const TargetRegisterClass *
BedrockRegisterInfo::getPointerRegClass(unsigned Kind) const {
  return &Bedrock::PTR64RegClass;
}

bool BedrockRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                              int SPAdj, unsigned FIOperandNum,
                                              RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  int64_t Offset = MFI.getObjectOffset(FrameIndex) + MFI.getStackSize() + SPAdj +
                   MI.getOperand(FIOperandNum + 1).getImm();

  Register FrameReg = getFrameRegister(MF);
  if (MI.getOpcode() == Bedrock::ADD64fi) {
    const BedrockInstrInfo &TII = *STI.getInstrInfo();
    Register DstReg = MI.getOperand(0).getReg();
    DebugLoc DL = MI.getDebugLoc();

    BuildMI(*MI.getParent(), II, DL, TII.get(Bedrock::MOV64rr), DstReg)
        .addReg(FrameReg);
    if (Offset != 0)
      BuildMI(*MI.getParent(), II, DL, TII.get(Bedrock::ADD64ri), DstReg)
          .addReg(DstReg)
          .addImm(Offset);
    MI.eraseFromParent();
    return true;
  }

  MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
  return false;
}

Register
BedrockRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return STI.getFrameLowering()->hasFP(MF) ? Bedrock::A7 : Bedrock::SP;
}
