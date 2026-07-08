//===-- BedrockInstrInfo.cpp - Bedrock instruction information ------------===//
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
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "BedrockGenInstrInfo.inc"

BedrockInstrInfo::BedrockInstrInfo(const BedrockSubtarget &STI)
    : BedrockGenInstrInfo(STI, RI, Bedrock::ADJCALLSTACKDOWN,
                          Bedrock::ADJCALLSTACKUP),
      RI() {}

void BedrockInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I,
                                   const DebugLoc &DL, Register DestReg,
                                   Register SrcReg, bool KillSrc,
                                   bool RenamableDest,
                                   bool RenamableSrc) const {
  if (Bedrock::FPR64RegClass.contains(DestReg, SrcReg)) {
    BuildMI(MBB, I, DL, get(Bedrock::FMOVDrr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (!Bedrock::GPR64RegClass.contains(DestReg, SrcReg))
    report_fatal_error("Bedrock only supports same-class GPR/FPR copies");

  BuildMI(MBB, I, DL, get(Bedrock::MOVQrr), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

void BedrockInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
    MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  if (RC == &Bedrock::FPR64RegClass) {
    BuildMI(MBB, MI, DL, get(Bedrock::FSTOREDfi))
        .addReg(SrcReg, getKillRegState(IsKill))
        .addFrameIndex(FrameIndex)
        .addImm(0);
    return;
  }

  if (RC != &Bedrock::GPR64RegClass)
    report_fatal_error("Bedrock only supports GPR/FPR stack-slot stores");

  BuildMI(MBB, MI, DL, get(Bedrock::STOREQfi))
      .addReg(SrcReg, getKillRegState(IsKill))
      .addFrameIndex(FrameIndex)
      .addImm(0);
}

void BedrockInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                            MachineBasicBlock::iterator MI,
                                            Register DestReg, int FrameIndex,
                                            const TargetRegisterClass *RC,
                                            Register VReg, unsigned SubReg,
                                            MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  if (RC == &Bedrock::FPR64RegClass) {
    BuildMI(MBB, MI, DL, get(Bedrock::FLOADDfi), DestReg)
        .addFrameIndex(FrameIndex)
        .addImm(0);
    return;
  }

  if (RC != &Bedrock::GPR64RegClass)
    report_fatal_error("Bedrock only supports GPR/FPR stack-slot loads");

  BuildMI(MBB, MI, DL, get(Bedrock::LOADQfi), DestReg)
      .addFrameIndex(FrameIndex)
      .addImm(0);
}
