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

static bool isUncondBranchOpcode(unsigned Opc) { return Opc == Bedrock::BR; }

static bool isCondBranchOpcode(unsigned Opc) { return Opc == Bedrock::BRCC; }

static bool isBranchOpcode(unsigned Opc) {
  return isUncondBranchOpcode(Opc) || isCondBranchOpcode(Opc);
}

static unsigned getOppositeCondition(unsigned CC) {
  switch (CC) {
  case 0x2:
    return 0x3;
  case 0x3:
    return 0x2;
  case 0x4:
    return 0x5;
  case 0x5:
    return 0x4;
  case 0x6:
    return 0x7;
  case 0x7:
    return 0x6;
  case 0x8:
    return 0x9;
  case 0x9:
    return 0x8;
  case 0xa:
    return 0xb;
  case 0xb:
    return 0xa;
  case 0xc:
    return 0xd;
  case 0xd:
    return 0xc;
  case 0xe:
    return 0xf;
  case 0xf:
    return 0xe;
  default:
    llvm_unreachable("invalid Bedrock branch condition");
  }
}

static bool isCheapConstMaterialization(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case Bedrock::CONST32:
  case Bedrock::CONST64:
    break;
  default:
    return false;
  }

  if (MI.getNumOperands() < 2 || !MI.getOperand(1).isImm())
    return false;

  int64_t Imm = MI.getOperand(1).getImm();
  return Imm == 0 || Imm == 1;
}

void BedrockInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I,
                                   const DebugLoc &DL, Register DestReg,
                                   Register SrcReg, bool KillSrc,
                                   bool RenamableDest,
                                   bool RenamableSrc) const {
  if (DestReg == Bedrock::SP && Bedrock::GPR64RegClass.contains(SrcReg)) {
    BuildMI(MBB, I, DL, get(Bedrock::MOVQrs))
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (SrcReg == Bedrock::SP && Bedrock::GPR64RegClass.contains(DestReg)) {
    BuildMI(MBB, I, DL, get(Bedrock::MOVQsr), DestReg);
    return;
  }

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

bool BedrockInstrInfo::isAsCheapAsAMove(const MachineInstr &MI) const {
  if (isCheapConstMaterialization(MI))
    return true;
  return MI.isAsCheapAsAMove();
}

bool BedrockInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *&TBB,
                                     MachineBasicBlock *&FBB,
                                     SmallVectorImpl<MachineOperand> &Cond,
                                     bool AllowModify) const {
  MachineBasicBlock::iterator I = MBB.end();
  MachineBasicBlock::iterator UncondBr = MBB.end();

  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;

    if (!isUnpredicatedTerminator(*I))
      break;

    if (!isBranchOpcode(I->getOpcode()))
      return true;

    if (isUncondBranchOpcode(I->getOpcode())) {
      UncondBr = I;

      if (!AllowModify) {
        TBB = I->getOperand(0).getMBB();
        continue;
      }

      MBB.erase(std::next(I), MBB.end());
      Cond.clear();
      FBB = nullptr;

      if (MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
        TBB = nullptr;
        I->eraseFromParent();
        I = MBB.end();
        UncondBr = MBB.end();
        continue;
      }

      TBB = I->getOperand(0).getMBB();
      continue;
    }

    if (Cond.empty()) {
      MachineBasicBlock *TargetBB = I->getOperand(0).getMBB();
      unsigned CC = I->getOperand(1).getImm();

      if (AllowModify && UncondBr != MBB.end() &&
          MBB.isLayoutSuccessor(TargetBB)) {
        MachineBasicBlock *NewTarget = UncondBr->getOperand(0).getMBB();
        DebugLoc DL = MBB.findDebugLoc(I);
        BuildMI(MBB, UncondBr, DL, get(Bedrock::BRCC))
            .addMBB(NewTarget)
            .addImm(getOppositeCondition(CC));
        I->eraseFromParent();
        UncondBr->eraseFromParent();

        I = MBB.end();
        UncondBr = MBB.end();
        TBB = nullptr;
        FBB = nullptr;
        Cond.clear();
        continue;
      }

      FBB = TBB;
      TBB = TargetBB;
      Cond.push_back(MachineOperand::CreateImm(CC));
      continue;
    }

    return true;
  }

  return false;
}

unsigned BedrockInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                        int *BytesRemoved) const {
  unsigned Count = 0;
  MachineBasicBlock::iterator I = MBB.end();

  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (!isBranchOpcode(I->getOpcode()))
      break;

    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }

  if (BytesRemoved)
    *BytesRemoved = Count * 2;
  return Count;
}

unsigned BedrockInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                        MachineBasicBlock *TBB,
                                        MachineBasicBlock *FBB,
                                        ArrayRef<MachineOperand> Cond,
                                        const DebugLoc &DL,
                                        int *BytesAdded) const {
  assert(TBB && "insertBranch must not be told to insert a fallthrough");

  unsigned Count = 0;
  if (Cond.empty()) {
    assert(!FBB && "unconditional branch with false target");
    BuildMI(&MBB, DL, get(Bedrock::BR)).addMBB(TBB);
    Count = 1;
  } else {
    assert(Cond.size() == 1 && "invalid Bedrock branch condition");
    BuildMI(&MBB, DL, get(Bedrock::BRCC)).addMBB(TBB).add(Cond[0]);
    Count = 1;
    if (FBB) {
      BuildMI(&MBB, DL, get(Bedrock::BR)).addMBB(FBB);
      ++Count;
    }
  }

  if (BytesAdded)
    *BytesAdded = Count * 2;
  return Count;
}

bool BedrockInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  if (Cond.size() != 1 || !Cond[0].isImm())
    return true;

  Cond[0].setImm(getOppositeCondition(Cond[0].getImm()));
  return false;
}
