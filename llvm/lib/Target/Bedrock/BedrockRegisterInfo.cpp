//===-- BedrockRegisterInfo.cpp - Bedrock register information ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockRegisterInfo.h"
#include "BedrockFrameLowering.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "BedrockGenRegisterInfo.inc"

BedrockRegisterInfo::BedrockRegisterInfo()
    : BedrockGenRegisterInfo(Bedrock::R0) {}

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
  Reserved.set(Bedrock::R15);

  // FPR callee-save lowering needs separate validation before allocation.
  Reserved.set(Bedrock::F8);
  Reserved.set(Bedrock::F9);
  Reserved.set(Bedrock::F10);
  Reserved.set(Bedrock::F11);
  Reserved.set(Bedrock::F12);
  Reserved.set(Bedrock::F13);
  Reserved.set(Bedrock::F14);
  Reserved.set(Bedrock::F15);

  return Reserved;
}

const TargetRegisterClass *
BedrockRegisterInfo::getPointerRegClass(unsigned Kind) const {
  return &Bedrock::GPR64RegClass;
}

bool BedrockRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                              int SPAdj, unsigned FIOperandNum,
                                              RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  int64_t ExtraOffset = MI.getOperand(FIOperandNum + 1).getImm();
  int64_t Offset =
      MFI.getObjectOffset(FrameIndex) + MFI.getStackSize() + ExtraOffset +
      SPAdj;

  MI.getOperand(FIOperandNum).ChangeToImmediate(Offset);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(0);
  return false;
}

Register
BedrockRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return Bedrock::R15;
}
