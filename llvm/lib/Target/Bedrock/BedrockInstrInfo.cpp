//===-- BedrockInstrInfo.cpp - Bedrock Instruction Information ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockInstrInfo.h"

#include "BedrockSubtarget.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "BedrockGenInstrInfo.inc"

static bool isFReg(Register Reg) {
  return Reg >= Bedrock::F0 && Reg <= Bedrock::F15;
}

BedrockInstrInfo::BedrockInstrInfo(const BedrockSubtarget &STI)
    : BedrockGenInstrInfo(STI, RI, Bedrock::ADJCALLSTACKDOWN,
                          Bedrock::ADJCALLSTACKUP, 0, Bedrock::RET),
      RI(STI) {}

void BedrockInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MI,
                                   const DebugLoc &DL, Register DstReg,
                                   Register SrcReg, bool KillSrc,
                                   bool RenamableDest,
                                   bool RenamableSrc) const {
  unsigned Opc =
      isFReg(DstReg) && isFReg(SrcReg) ? Bedrock::FMOV64rr : Bedrock::MOV64rr;
  BuildMI(MBB, MI, DL, get(Opc), DstReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

void BedrockInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
    MachineInstr::MIFlag Flags) const {
  unsigned Opc = Bedrock::MOV64mr;
  if (RC == &Bedrock::GPR32RegClass || RC == &Bedrock::D32RegClass)
    Opc = Bedrock::MOV32mr;
  else if (RC == &Bedrock::GPR16RegClass || RC == &Bedrock::D16RegClass)
    Opc = Bedrock::MOV16mr;
  else if (RC == &Bedrock::GPR8RegClass || RC == &Bedrock::D8RegClass)
    Opc = Bedrock::MOV8mr;
  else if (RC == &Bedrock::F32RegClass)
    Opc = Bedrock::FMOV32mr;
  else if (RC == &Bedrock::F64RegClass)
    Opc = Bedrock::FMOV64mr;

  BuildMI(MBB, MI, DebugLoc(), get(Opc))
      .addReg(SrcReg, getKillRegState(IsKill))
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .setMIFlag(Flags);
}

void BedrockInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                            MachineBasicBlock::iterator MI,
                                            Register DstReg, int FrameIndex,
                                            const TargetRegisterClass *RC,
                                            Register VReg, unsigned SubReg,
                                            MachineInstr::MIFlag Flags) const {
  unsigned Opc = Bedrock::MOV64rm;
  if (RC == &Bedrock::GPR32RegClass || RC == &Bedrock::D32RegClass)
    Opc = Bedrock::MOV32rm;
  else if (RC == &Bedrock::GPR16RegClass || RC == &Bedrock::D16RegClass)
    Opc = Bedrock::MOV16rm;
  else if (RC == &Bedrock::GPR8RegClass || RC == &Bedrock::D8RegClass)
    Opc = Bedrock::MOV8rm;
  else if (RC == &Bedrock::F32RegClass)
    Opc = Bedrock::FMOV32rm;
  else if (RC == &Bedrock::F64RegClass)
    Opc = Bedrock::FMOV64rm;

  BuildMI(MBB, MI, DebugLoc(), get(Opc), DstReg)
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .setMIFlag(Flags);
}
