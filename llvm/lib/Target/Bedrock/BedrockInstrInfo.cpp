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
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOutliner.h"
#include "llvm/IR/Module.h"
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

static std::optional<unsigned> getGSSelector(Register Reg) {
  switch (Reg.id()) {
  case Bedrock::GS1:
    return 3;
  case Bedrock::GS2:
    return 4;
  case Bedrock::GS3:
    return 5;
  case Bedrock::GS4:
    return 6;
  case Bedrock::GS5:
    return 7;
  default:
    return std::nullopt;
  }
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
  if (std::optional<unsigned> Selector = getGSSelector(DestReg);
      Selector && Bedrock::GPR64RegClass.contains(SrcReg)) {
    BuildMI(MBB, I, DL, get(Bedrock::BEDROCK_WRSEG))
        .addReg(SrcReg, getKillRegState(KillSrc))
        .addImm(*Selector)
        .addReg(DestReg, RegState::Define | RegState::Implicit);
    return;
  }

  if (std::optional<unsigned> Selector = getGSSelector(SrcReg);
      Selector && Bedrock::GPR64RegClass.contains(DestReg)) {
    BuildMI(MBB, I, DL, get(Bedrock::BEDROCK_RDSEG), DestReg)
        .addImm(*Selector)
        .addReg(SrcReg, RegState::Implicit | getKillRegState(KillSrc));
    return;
  }

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

  if (Bedrock::VRRegClass.contains(DestReg, SrcReg)) {
    BuildMI(MBB, I, DL, get(Bedrock::VECTOR_COPY), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (Bedrock::PRRegClass.contains(DestReg, SrcReg)) {
    BuildMI(MBB, I, DL, get(Bedrock::PREDICATE_COPY), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (!Bedrock::GPR64RegClass.contains(DestReg, SrcReg))
    report_fatal_error("Bedrock only supports same-class register copies");

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

  if (RC == &Bedrock::VRRegClass) {
    MachineFrameInfo &MFI = MBB.getParent()->getFrameInfo();
    MFI.setObjectSize(FrameIndex, 576);
    MFI.setObjectAlignment(FrameIndex, Align(16));
    BuildMI(MBB, MI, DL, get(Bedrock::VECTOR_SPILL))
        .addReg(SrcReg, getKillRegState(IsKill))
        .addFrameIndex(FrameIndex)
        .addImm(0);
    return;
  }

  if (RC == &Bedrock::PRRegClass) {
    MachineFrameInfo &MFI = MBB.getParent()->getFrameInfo();
    MFI.setObjectSize(FrameIndex, 64);
    MFI.setObjectAlignment(FrameIndex, Align(16));
    BuildMI(MBB, MI, DL, get(Bedrock::PREDICATE_SPILL))
        .addReg(SrcReg, getKillRegState(IsKill))
        .addFrameIndex(FrameIndex)
        .addImm(0);
    return;
  }

  if (!Bedrock::GPR64RegClass.hasSubClassEq(RC))
    report_fatal_error("unsupported Bedrock stack-slot store class");

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

  if (RC == &Bedrock::VRRegClass) {
    MachineFrameInfo &MFI = MBB.getParent()->getFrameInfo();
    MFI.setObjectSize(FrameIndex, 576);
    MFI.setObjectAlignment(FrameIndex, Align(16));
    BuildMI(MBB, MI, DL, get(Bedrock::VECTOR_RELOAD), DestReg)
        .addFrameIndex(FrameIndex)
        .addImm(0);
    return;
  }

  if (RC == &Bedrock::PRRegClass) {
    MachineFrameInfo &MFI = MBB.getParent()->getFrameInfo();
    MFI.setObjectSize(FrameIndex, 64);
    MFI.setObjectAlignment(FrameIndex, Align(16));
    BuildMI(MBB, MI, DL, get(Bedrock::PREDICATE_RELOAD), DestReg)
        .addFrameIndex(FrameIndex)
        .addImm(0);
    return;
  }

  if (!Bedrock::GPR64RegClass.hasSubClassEq(RC))
    report_fatal_error("unsupported Bedrock stack-slot load class");

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

unsigned BedrockInstrInfo::insertBranch(
    MachineBasicBlock &MBB, MachineBasicBlock *TBB, MachineBasicBlock *FBB,
    ArrayRef<MachineOperand> Cond, const DebugLoc &DL, int *BytesAdded) const {
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

bool BedrockInstrInfo::isFunctionSafeToOutlineFrom(
    MachineFunction &MF, bool OutlineFromLinkOnceODRs) const {
  const Function &F = MF.getFunction();
  if ((!OutlineFromLinkOnceODRs && F.hasLinkOnceODRLinkage()) ||
      F.hasSection() || F.hasFnAttribute(Attribute::Naked))
    return false;
  return true;
}

bool BedrockInstrInfo::shouldOutlineFromFunctionByDefault(
    MachineFunction &MF) const {
  return MF.getFunction().hasMinSize();
}

namespace {
enum BedrockOutlinerConstructionID {
  BedrockOutlinerDefault,
  BedrockOutlinerTailCall
};

static unsigned getOutlinerSizeLowerBound(const MachineInstr &MI) {
  if (MI.isMetaInstruction())
    return 0;
  unsigned Size = MI.getDesc().getSize();
  return Size ? Size : 2;
}

static bool isOutlinerStackInstruction(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case Bedrock::ADJSP_DOWN:
  case Bedrock::ADJSP_UP:
  case Bedrock::PUSHr:
  case Bedrock::PUSHPi:
  case Bedrock::POPr:
  case Bedrock::POPPi:
    return true;
  default: {
    const TargetRegisterInfo *TRI =
        MI.getMF()->getSubtarget().getRegisterInfo();
    return MI.readsRegister(Bedrock::SP, TRI) ||
           MI.modifiesRegister(Bedrock::SP, TRI);
  }
  }
}
} // namespace

std::optional<std::unique_ptr<outliner::OutlinedFunction>>
BedrockInstrInfo::getOutliningCandidateInfo(
    const MachineModuleInfo &MMI,
    std::vector<outliner::Candidate> &RepeatedSequenceLocs,
    unsigned MinRepeats) const {
  if (RepeatedSequenceLocs.size() < MinRepeats)
    return std::nullopt;

  outliner::Candidate &Candidate = RepeatedSequenceLocs.front();
  unsigned ConstructionID = BedrockOutlinerDefault;
  // Local CALL/JMP transfers use a three-byte medium opcode followed by a
  // two-byte PC-relative displacement.
  unsigned CallOverhead = 5;
  // Account for both the one-byte return and worst-case function alignment.
  unsigned FrameOverhead = 2;
  if (Candidate.back().isReturn()) {
    ConstructionID = BedrockOutlinerTailCall;
    FrameOverhead = 1;
  }

  for (outliner::Candidate &C : RepeatedSequenceLocs)
    C.setCallInfo(ConstructionID, CallOverhead);

  unsigned SequenceSize = 0;
  for (const MachineInstr &MI : Candidate)
    SequenceSize += getOutlinerSizeLowerBound(MI);

  return std::make_unique<outliner::OutlinedFunction>(
      RepeatedSequenceLocs, SequenceSize, FrameOverhead, ConstructionID);
}

outliner::InstrType
BedrockInstrInfo::getOutliningTypeImpl(const MachineModuleInfo &MMI,
                                       MachineBasicBlock::iterator &MBBI,
                                       unsigned Flags) const {
  const MachineInstr &MI = *MBBI;
  if (MI.isCFIInstruction() || isOutlinerStackInstruction(MI) ||
      MI.getOpcode() == Bedrock::CONST32 ||
      MI.getOpcode() == Bedrock::CONST64 || MI.getOpcode() == Bedrock::CLRQr)
    return outliner::InstrType::Illegal;

  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isMCSymbol())
      return outliner::InstrType::Illegal;
  }
  return outliner::InstrType::Legal;
}

void BedrockInstrInfo::buildOutlinedFrame(
    MachineBasicBlock &MBB, MachineFunction &MF,
    const outliner::OutlinedFunction &OF) const {
  if (OF.FrameConstructionID == BedrockOutlinerTailCall)
    return;
  BuildMI(MBB, MBB.end(), DebugLoc(), get(Bedrock::RET));
}

MachineBasicBlock::iterator BedrockInstrInfo::insertOutlinedCall(
    Module &M, MachineBasicBlock &MBB, MachineBasicBlock::iterator &It,
    MachineFunction &MF, outliner::Candidate &C) const {
  unsigned Opcode = C.CallConstructionID == BedrockOutlinerTailCall
                        ? Bedrock::TAILCALL
                        : Bedrock::CALL;
  It = MBB.insert(It, BuildMI(MF, DebugLoc(), get(Opcode))
                          .addGlobalAddress(M.getNamedValue(MF.getName())));
  return It;
}
