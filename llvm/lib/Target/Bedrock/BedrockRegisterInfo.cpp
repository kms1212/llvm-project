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
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "BedrockGenRegisterInfo.inc"

BedrockRegisterInfo::BedrockRegisterInfo()
    : BedrockGenRegisterInfo(Bedrock::PC) {}

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
  for (MCPhysReg Reg :
       {Bedrock::PC, Bedrock::FLAGS, Bedrock::STATUS, Bedrock::CS, Bedrock::DS,
        Bedrock::SS, Bedrock::GS0, Bedrock::GS1, Bedrock::GS2, Bedrock::GS3,
        Bedrock::GS4, Bedrock::FSTATUS, Bedrock::FFLAGS})
    Reserved.set(Reg);

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

  const BedrockFrameLowering *TFI = static_cast<const BedrockFrameLowering *>(
      MF.getSubtarget().getFrameLowering());
  bool IsCalleeSave =
      llvm::any_of(MFI.getCalleeSavedInfo(), [&](const CalleeSavedInfo &Info) {
        return Info.getFrameIdx() == FrameIndex;
      });
  if (TFI->hasFP(MF) && !IsCalleeSave) {
    unsigned RegisterBaseOpcode = 0;
    switch (MI.getOpcode()) {
    case Bedrock::LOADB_Zfi:
      RegisterBaseOpcode = Bedrock::LOADB_Zro;
      break;
    case Bedrock::LOADW_Zfi:
      RegisterBaseOpcode = Bedrock::LOADW_Zro;
      break;
    case Bedrock::LOADL_Zfi:
      RegisterBaseOpcode = Bedrock::LOADL_Zro;
      break;
    case Bedrock::LOADB_Sfi:
      RegisterBaseOpcode = Bedrock::LOADB_Sro;
      break;
    case Bedrock::LOADW_Sfi:
      RegisterBaseOpcode = Bedrock::LOADW_Sro;
      break;
    case Bedrock::LOADL_Sfi:
      RegisterBaseOpcode = Bedrock::LOADL_Sro;
      break;
    case Bedrock::LOADLfi:
      RegisterBaseOpcode = Bedrock::LOADLro;
      break;
    case Bedrock::LOADQfi:
      RegisterBaseOpcode = Bedrock::LOADQro;
      break;
    case Bedrock::FLOADSfi:
      RegisterBaseOpcode = Bedrock::FLOADSro;
      break;
    case Bedrock::FLOADDfi:
      RegisterBaseOpcode = Bedrock::FLOADDro;
      break;
    case Bedrock::STOREBfi:
      RegisterBaseOpcode = Bedrock::STOREBro;
      break;
    case Bedrock::STOREWfi:
      RegisterBaseOpcode = Bedrock::STOREWro;
      break;
    case Bedrock::STORELfi:
      RegisterBaseOpcode = Bedrock::STORELro;
      break;
    case Bedrock::STOREQfi:
      RegisterBaseOpcode = Bedrock::STOREQro;
      break;
    case Bedrock::FSTORESfi:
      RegisterBaseOpcode = Bedrock::FSTORESro;
      break;
    case Bedrock::FSTOREDfi:
      RegisterBaseOpcode = Bedrock::FSTOREDro;
      break;
    case Bedrock::STOREB_Immfi:
      RegisterBaseOpcode = Bedrock::STOREB_Immro;
      break;
    case Bedrock::STOREW_Immfi:
      RegisterBaseOpcode = Bedrock::STOREW_Immro;
      break;
    case Bedrock::STOREL_Immfi:
      RegisterBaseOpcode = Bedrock::STOREL_Immro;
      break;
    case Bedrock::STOREQ_Immfi:
      RegisterBaseOpcode = Bedrock::STOREQ_Immro;
      break;
    case Bedrock::LEAfi:
      RegisterBaseOpcode = Bedrock::LEAro;
      break;
    default:
      report_fatal_error("unsupported Bedrock frame-pointer reference");
    }
    MI.setDesc(MF.getSubtarget().getInstrInfo()->get(RegisterBaseOpcode));
    Register FrameBase =
        MFI.getMaxAlign() > Align(16) ? Bedrock::R14 : Bedrock::R15;
    MI.getOperand(FIOperandNum).ChangeToRegister(FrameBase, false);
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
    return false;
  }

  MI.getOperand(FIOperandNum).ChangeToImmediate(Offset);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(0);
  return false;
}

Register
BedrockRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return Bedrock::R15;
}
