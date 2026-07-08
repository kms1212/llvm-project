//===-- BedrockPeepholeCore.cpp - Bedrock peepholes -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockPeephole.h"

bool BedrockPeephole::foldIdentityMoves(MachineBasicBlock &MBB,
                                            MachineFunction &MF) const {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &MI = *I++;
    if (MI.isDebugInstr() || !isIdentityMove(MI, TRI))
      continue;
    MI.eraseFromParent();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldArgTruncBitOps(MachineBasicBlock &MBB,
                                             MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &First = *I;
    if (First.isDebugInstr()) {
      ++I;
      continue;
    }

    TruncAnd32Arm Arm0;
    if (!matchTruncAnd32(First, MBB, Arm0, TRI)) {
      ++I;
      continue;
    }

    auto SecondI = nextNonDebug(Arm0.And->getIterator(), MBB);
    if (SecondI == MBB.end()) {
      ++I;
      continue;
    }
    TruncAnd32Arm Arm1;
    if (!matchTruncAnd32(*SecondI, MBB, Arm1, TRI)) {
      ++I;
      continue;
    }

    auto OrI = nextNonDebug(Arm1.And->getIterator(), MBB);
    if (OrI == MBB.end() || OrI->getOpcode() != Bedrock::OR32rr ||
        OrI->getNumOperands() < 3 || !OrI->getOperand(0).isReg() ||
        !OrI->getOperand(1).isReg() || !OrI->getOperand(2).isReg()) {
      ++I;
      continue;
    }

    Register Dst = OrI->getOperand(0).getReg();
    if (!isDReg(Dst) || regsOverlap(TRI, Arm0.Src, Arm1.Src)) {
      ++I;
      continue;
    }

    bool UsesArm0 = regsOverlap(TRI, OrI->getOperand(1).getReg(), Arm0.Tmp) ||
                    regsOverlap(TRI, OrI->getOperand(2).getReg(), Arm0.Tmp);
    bool UsesArm1 = regsOverlap(TRI, OrI->getOperand(1).getReg(), Arm1.Tmp) ||
                    regsOverlap(TRI, OrI->getOperand(2).getReg(), Arm1.Tmp);
    if (!UsesArm0 || !UsesArm1)
      continue;

    TruncAnd32Arm *Acc = nullptr;
    TruncAnd32Arm *Rhs = nullptr;
    if (regsOverlap(TRI, Arm0.Src, Dst)) {
      Acc = &Arm0;
      Rhs = &Arm1;
    } else if (regsOverlap(TRI, Arm1.Src, Dst)) {
      Acc = &Arm1;
      Rhs = &Arm0;
    } else {
      ++I;
      continue;
    }

    if (!operandIsKill(*Arm0.Trunc, Arm0.Src, TRI) ||
        !operandIsKill(*Arm1.Trunc, Arm1.Src, TRI) ||
        !operandIsKill(*OrI, Arm0.Tmp, TRI) ||
        !operandIsKill(*OrI, Arm1.Tmp, TRI)) {
      ++I;
      continue;
    }

    BuildMI(MBB, First.getIterator(), Acc->And->getDebugLoc(),
            TII.get(Bedrock::AND32ri), Acc->Src)
        .addReg(Acc->Src)
        .addImm(Acc->Imm);
    BuildMI(MBB, First.getIterator(), Rhs->And->getDebugLoc(),
            TII.get(Bedrock::AND32ri), Rhs->Src)
        .addReg(Rhs->Src)
        .addImm(Rhs->Imm);
    BuildMI(MBB, First.getIterator(), OrI->getDebugLoc(),
            TII.get(Bedrock::OR32rr), Dst)
        .addReg(Acc->Src)
        .addReg(Rhs->Src);

    OrI->eraseFromParent();
    Arm1.And->eraseFromParent();
    Arm1.Trunc->eraseFromParent();
    Arm0.And->eraseFromParent();
    Arm0.Trunc->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

static bool isRegRegExtOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
  case Bedrock::EXTZQ8rr:
  case Bedrock::EXTZQ16rr:
  case Bedrock::EXTZQ32rr:
  case Bedrock::EXTSQ8rr:
  case Bedrock::EXTSQ16rr:
  case Bedrock::EXTSQ32rr:
  case Bedrock::EXTZL8rr:
  case Bedrock::EXTZL16rr:
  case Bedrock::EXTSL8rr:
  case Bedrock::EXTSL16rr:
  case Bedrock::EXTZW8rr:
  case Bedrock::EXTSW8rr:
    return true;
  }
}

static bool canUseRegAsExtSource(unsigned Opcode, Register Reg) {
  if (isDReg(Reg))
    return true;

  switch (Opcode) {
  default:
    return false;
  case Bedrock::EXTZQ32rr:
  case Bedrock::EXTSQ32rr:
    return isAReg(Reg);
  }
}

static unsigned countHighBits(uint64_t Imm) {
  unsigned Count = 0;
  for (unsigned Bit = 32; Bit != 64; ++Bit)
    if ((Imm & (uint64_t(1) << Bit)) != 0)
      ++Count;
  return Count;
}

bool BedrockPeephole::foldCompactUnary(MachineBasicBlock &MBB,
                                           MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &MI = *I++;
    if (MI.isDebugInstr())
      continue;

    switch (MI.getOpcode()) {
    default:
      break;
    case Bedrock::MOV64ri:
      if (MI.getNumOperands() >= 2 && MI.getOperand(0).isReg() &&
          MI.getOperand(1).isImm() && isDReg(MI.getOperand(0).getReg())) {
        Register Dst = MI.getOperand(0).getReg();
        uint64_t Imm = static_cast<uint64_t>(MI.getOperand(1).getImm());
        uint64_t Low = Imm & 0xffffffffULL;
        unsigned HighBits = countHighBits(Imm);
        unsigned SeedBytes = Low == 0 ? 2 : 4;
        if ((Imm >> 32) != 0 && Low <= 0xffff && HighBits != 0 &&
            SeedBytes + HighBits * 4 < 10 &&
            regDeadAfterInCFG(std::next(MI.getIterator()), MBB,
                              Bedrock::FLAGS, TRI)) {
          if (Low == 0) {
            BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(),
                    TII.get(Bedrock::CLR64r), Dst);
          } else {
            BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(),
                    TII.get(Bedrock::MOV64ri), Dst)
                .addImm(Low);
          }
          for (unsigned Bit = 32; Bit != 64; ++Bit) {
            if ((Imm & (uint64_t(1) << Bit)) == 0)
              continue;
            BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(),
                    TII.get(Bedrock::BSET64ri), Dst)
                .addReg(Dst)
                .addImm(Bit);
          }
          MI.eraseFromParent();
          I = MBB.begin();
          Changed = true;
          continue;
        }
        uint64_t High = Imm >> 32;
        if (Low == 0 && High != 0 && High <= 0xffff &&
            regDeadAfterInCFG(std::next(MI.getIterator()), MBB,
                              Bedrock::FLAGS, TRI)) {
          BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(),
                  TII.get(Bedrock::MOV64ri), Dst)
              .addImm(High);
          BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(),
                  TII.get(Bedrock::SHL64ri), Dst)
              .addReg(Dst)
              .addImm(32);
          MI.eraseFromParent();
          I = MBB.begin();
          Changed = true;
          continue;
        }
      }
      [[fallthrough]];
    case Bedrock::MOV8ri:
    case Bedrock::MOV16ri:
    case Bedrock::MOV32ri:
      if (MI.getNumOperands() >= 2 && MI.getOperand(0).isReg() &&
          MI.getOperand(1).isImm() && MI.getOperand(1).getImm() == 0) {
        Register Dst = MI.getOperand(0).getReg();
        if (!isIntReg(Dst))
          break;
        BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(),
                TII.get(Bedrock::CLR64r), Dst);
        MI.eraseFromParent();
        Changed = true;
        continue;
      }
      break;
    case Bedrock::AND64ri:
      if (MI.getNumOperands() >= 3 && MI.getOperand(0).isReg() &&
          MI.getOperand(1).isReg() && MI.getOperand(2).isImm() &&
          regsOverlap(TRI, MI.getOperand(0).getReg(),
                      MI.getOperand(1).getReg())) {
        Register Dst = MI.getOperand(0).getReg();
        Register Src = MI.getOperand(1).getReg();
        uint64_t Mask = MI.getOperand(2).getImm();
        unsigned ExtOpcode = 0;
        if (Mask == 0xffULL &&
            regDefDeadOrDeadAfterInCFG(MI.getIterator(), MBB, Bedrock::FLAGS,
                                       TRI))
          ExtOpcode = Bedrock::EXTZQ8rr;
        else if (Mask == 0xffffULL &&
                 regDefDeadOrDeadAfterInCFG(MI.getIterator(), MBB,
                                            Bedrock::FLAGS, TRI))
          ExtOpcode = Bedrock::EXTZQ16rr;
        else if (Mask == 0xffffffffULL)
          ExtOpcode = Bedrock::EXTZQ32rr;
        if (ExtOpcode) {
          BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(), TII.get(ExtOpcode),
                  Dst)
              .addReg(Src, getKillRegState(operandIsKill(MI, Src, TRI)));
          MI.eraseFromParent();
          I = MBB.begin();
          Changed = true;
          continue;
        }
      }
      break;
    }

    if (MI.getOpcode() == Bedrock::MOV64rr && MI.getNumOperands() >= 2 &&
        MI.getOperand(0).isReg() && MI.getOperand(1).isReg()) {
      Register CopyDst = MI.getOperand(0).getReg();
      Register CopySrc = MI.getOperand(1).getReg();
      auto ExtI = nextNonDebug(MI.getIterator(), MBB);
      if (ExtI != MBB.end() && isRegRegExtOpcode(ExtI->getOpcode()) &&
          ExtI->getNumOperands() >= 2 && ExtI->getOperand(0).isReg() &&
          ExtI->getOperand(1).isReg() &&
          regsOverlap(TRI, ExtI->getOperand(1).getReg(), CopyDst) &&
          canUseRegAsExtSource(ExtI->getOpcode(), CopySrc) &&
          (regsOverlap(TRI, ExtI->getOperand(0).getReg(), CopyDst) ||
           operandIsKill(*ExtI, CopyDst, TRI) ||
           regUnusedAfterInCFG(std::next(ExtI), MBB, CopyDst, TRI))) {
        MachineInstrBuilder MIB =
            BuildMI(MBB, MI.getIterator(), ExtI->getDebugLoc(),
                    TII.get(ExtI->getOpcode()), ExtI->getOperand(0).getReg())
                .addReg(CopySrc,
                        getKillRegState(operandIsKill(MI, CopySrc, TRI)));
        MIB.setMIFlags(MI.getFlags() | ExtI->getFlags());
        MI.eraseFromParent();
        ExtI->eraseFromParent();
        I = MBB.begin();
        Changed = true;
        continue;
      }
    }

    if (MI.getOpcode() == Bedrock::SHR64ri && MI.getNumOperands() >= 3 &&
        MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
        MI.getOperand(2).isImm() && MI.getOperand(2).getImm() >= 32 &&
        regsOverlap(TRI, MI.getOperand(0).getReg(),
                    MI.getOperand(1).getReg()) &&
        isDReg(MI.getOperand(0).getReg())) {
      Register Reg = MI.getOperand(0).getReg();
      auto AndI = nextNonDebug(MI.getIterator(), MBB);
      if (AndI != MBB.end() && AndI->getOpcode() == Bedrock::AND64ri &&
          AndI->getNumOperands() >= 3 && AndI->getOperand(0).isReg() &&
          AndI->getOperand(1).isReg() && AndI->getOperand(2).isImm() &&
          regsOverlap(TRI, AndI->getOperand(0).getReg(), Reg) &&
          regsOverlap(TRI, AndI->getOperand(1).getReg(), Reg) &&
          uint64_t(AndI->getOperand(2).getImm()) <= 0xffffffffULL &&
          regDefDeadOrDeadAfterInCFG(AndI, MBB, Bedrock::FLAGS, TRI)) {
        AndI->setDesc(TII.get(Bedrock::AND32ri));
        AndI->getOperand(2).setImm(
            uint64_t(AndI->getOperand(2).getImm()) & 0xffffffffULL);
        I = MBB.begin();
        Changed = true;
        continue;
      }
    }

    if (MI.getNumOperands() >= 3 && MI.getOperand(0).isReg() &&
        MI.getOperand(1).isReg() && MI.getOperand(2).isImm()) {
      Register Dst = MI.getOperand(0).getReg();
      Register Src = MI.getOperand(1).getReg();
      int64_t Imm = MI.getOperand(2).getImm();
      auto ExtI = nextNonDebug(MI.getIterator(), MBB);
      if (Imm <= -2 && Imm >= -63 && regsOverlap(TRI, Dst, Src) &&
          regDefDeadOrClobberedAfter(MI.getIterator(), MBB, Bedrock::FLAGS,
                                     TRI)) {
        unsigned NewOpcode = 0;
        switch (MI.getOpcode()) {
        default:
          break;
        case Bedrock::ADD32ri:
          NewOpcode = Bedrock::SUB32ri;
          break;
        case Bedrock::ADD64ri:
          NewOpcode = Bedrock::SUB64ri;
          break;
        case Bedrock::SUB32ri:
          NewOpcode = Bedrock::ADD32ri;
          break;
        case Bedrock::SUB64ri:
          NewOpcode = Bedrock::ADD64ri;
          break;
        }
        if (NewOpcode != 0) {
          MI.setDesc(TII.get(NewOpcode));
          MI.getOperand(2).setImm(-Imm);
          Changed = true;
          continue;
        }
      }
      if (MI.getOpcode() == Bedrock::ADD64ri &&
          (uint64_t(Imm) & 0xffffffffULL) == 0 &&
          regsOverlap(TRI, Dst, Src) &&
          ExtI != MBB.end() && ExtI->getOpcode() == Bedrock::EXTZQ32rr &&
          ExtI->getNumOperands() >= 2 && ExtI->getOperand(0).isReg() &&
          ExtI->getOperand(1).isReg() &&
          regsOverlap(TRI, ExtI->getOperand(0).getReg(), Dst) &&
          regsOverlap(TRI, ExtI->getOperand(1).getReg(), Dst) &&
          canUseRegAsExtSource(ExtI->getOpcode(), Src)) {
        BuildMI(MBB, MI.getIterator(), ExtI->getDebugLoc(),
                TII.get(Bedrock::EXTZQ32rr), Dst)
            .addReg(Src, getKillRegState(operandIsKill(MI, Src, TRI)));
        MI.eraseFromParent();
        ExtI->eraseFromParent();
        I = MBB.begin();
        Changed = true;
        continue;
      }
      if (MI.getOpcode() == Bedrock::ADD64ri &&
          uint64_t(Imm) == 0xffffffff00000001ULL &&
          regsOverlap(TRI, Dst, Src)) {
        if (ExtI != MBB.end() && ExtI->getOpcode() == Bedrock::EXTZQ32rr &&
            ExtI->getNumOperands() >= 2 && ExtI->getOperand(0).isReg() &&
            ExtI->getOperand(1).isReg() &&
            regsOverlap(TRI, ExtI->getOperand(0).getReg(), Dst) &&
            regsOverlap(TRI, ExtI->getOperand(1).getReg(), Dst)) {
          BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(),
                  TII.get(Bedrock::INC32r), Dst)
              .addReg(Dst);
          MI.eraseFromParent();
          Changed = true;
          continue;
        }
      }
      unsigned Opcode = getIncDecRegOpcode(MI.getOpcode(), Imm);
      if (Opcode != 0 && regsOverlap(TRI, Dst, Src)) {
        BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(), TII.get(Opcode), Dst)
            .addReg(Dst);
        MI.eraseFromParent();
        Changed = true;
        continue;
      }
    }

    if (MI.getNumOperands() >= 3 && MI.getOperand(0).isImm() &&
        MI.getOperand(1).isReg() && MI.getOperand(2).isImm()) {
      int64_t Imm = MI.getOperand(0).getImm();
      unsigned Opcode = getIncDecMemOpcode(MI.getOpcode(), Imm);
      if (Opcode != 0) {
        BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(), TII.get(Opcode))
            .addReg(MI.getOperand(1).getReg())
            .addImm(MI.getOperand(2).getImm());
        MI.eraseFromParent();
        Changed = true;
      }
    }
  }

  return Changed;
}

bool BedrockPeephole::foldDecCmpBranch(MachineBasicBlock &MBB,
                                           MachineFunction &MF,
                                           uint32_t KnownZeroIn) const {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  uint32_t KnownZero = KnownZeroIn;

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &IncDec = *I;
    if (IncDec.isDebugInstr()) {
      ++I;
      continue;
    }

    Register CountReg;
    unsigned CmpOpcode = 0;
    if (!isIncDecRegInstr(IncDec, CountReg, CmpOpcode, TRI)) {
      transferKnownZero(IncDec, KnownZero, TRI);
      ++I;
      continue;
    }

    MachineInstr *Cmp = nullptr;
    MachineInstr *Branch = nullptr;
    bool Failed = false;
    uint32_t ScanKnownZero = KnownZero;
    transferKnownZero(IncDec, ScanKnownZero, TRI);

    for (auto Scan = nextNonDebug(I, MBB); Scan != MBB.end();
         Scan = nextNonDebug(Scan, MBB)) {
      if (Scan->getOpcode() == Bedrock::JCC) {
        Branch = &*Scan;
        break;
      }

      if (instrUsesReg(*Scan, Bedrock::FLAGS, TRI)) {
        Failed = true;
        break;
      }

      if (instrDefinesReg(*Scan, CountReg, TRI)) {
        Failed = true;
        break;
      }

      if (!Cmp && isKnownZeroCmpForReg(*Scan, CountReg, ScanKnownZero,
                                       CmpOpcode, TRI)) {
        Cmp = &*Scan;
        continue;
      }

      if (instrDefinesReg(*Scan, Bedrock::FLAGS, TRI)) {
        Failed = true;
        break;
      }
      transferKnownZero(*Scan, ScanKnownZero, TRI);
    }

    if (Failed || !Cmp || !Branch || !isEqNeBranch(*Branch)) {
      transferKnownZero(IncDec, KnownZero, TRI);
      ++I;
      continue;
    }

    Cmp->eraseFromParent();
    I = MBB.begin();
    KnownZero = KnownZeroIn;
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldSequentialEqImmCompareChain(
    MachineFunction &MF) const {
  if (!MF.getRegInfo().tracksLiveness())
    return false;

  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  SmallVector<MachineBasicBlock *, 16> Blocks;
  for (MachineBasicBlock &MBB : MF)
    Blocks.push_back(&MBB);

  SmallPtrSet<MachineBasicBlock *, 16> Rewritten;
  for (MachineBasicBlock *Start : Blocks) {
    if (!Start || Start->getParent() != &MF || Rewritten.contains(Start))
      continue;

    EqImmCompareBlock First;
    if (!matchEqImmCompareBlock(*Start, First) || First.Imm != 1 ||
        !isDReg(First.Reg) ||
        blockHasLiveInReg(*First.CaseTarget, First.Reg, TRI))
      continue;

    SmallVector<EqImmCompareBlock, 8> Chain;
    Chain.push_back(First);

    MachineBasicBlock *Cur = First.Fallthrough;
    int64_t NextImm = 2;
    while (Cur && Cur->getParent() == &MF && !Rewritten.contains(Cur)) {
      EqImmCompareBlock Item;
      if (!matchEqImmCompareBlock(*Cur, Item))
        break;
      if (!regsOverlap(TRI, Item.Reg, First.Reg) || Item.Imm != NextImm ||
          blockHasLiveInReg(*Item.CaseTarget, First.Reg, TRI))
        break;

      MachineBasicBlock::iterator FirstI = Cur->begin();
      while (FirstI != Cur->end() && FirstI->isDebugInstr())
        ++FirstI;
      if (FirstI == Cur->end() || &*FirstI != Item.Cmp)
        break;

      Chain.push_back(Item);
      Cur = Item.Fallthrough;
      ++NextImm;
    }

    if (Chain.size() < 2 || !Cur ||
        blockHasLiveInReg(*Chain.back().Fallthrough, First.Reg, TRI))
      continue;

    for (EqImmCompareBlock &Item : Chain) {
      MachineBasicBlock &MBB = *Item.MBB;
      MachineInstr &Cmp = *Item.Cmp;
      DebugLoc DL = Cmp.getDebugLoc();
      unsigned SrcFlags = Cmp.getOperand(0).isKill() ? RegState::Kill : 0;
      BuildMI(MBB, Cmp.getIterator(), DL, TII.get(Bedrock::DEC32r), First.Reg)
          .addReg(First.Reg, SrcFlags);
      Cmp.eraseFromParent();
      Rewritten.insert(&MBB);
    }
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldStackSpillCompareChain(MachineFunction &MF) const {
  if (!MF.getRegInfo().tracksLiveness())
    return false;

  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  struct StackCmpBlock {
    MachineBasicBlock *MBB = nullptr;
    MachineInstr *Cmp = nullptr;
    MachineBasicBlock *Fallthrough = nullptr;
    int64_t Imm = 0;
  };

  auto MatchStackCmpBlock = [&](MachineBasicBlock &MBB, int64_t Offset,
                                StackCmpBlock &Out) {
    MachineBasicBlock::iterator BranchI = MBB.getLastNonDebugInstr();
    if (BranchI == MBB.end() || BranchI->getOpcode() != Bedrock::JCC ||
        BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB())
      return false;

    std::optional<int64_t> CC = getCondCodeImm(BranchI->getOperand(1));
    if (!CC || *CC != BedrockCC::EQ)
      return false;

    MachineBasicBlock::iterator CmpI = prevNonDebug(BranchI, MBB);
    if (CmpI == MBB.end() || CmpI->getOpcode() != Bedrock::CMP32mi ||
        CmpI->getNumOperands() < 3 || !CmpI->getOperand(0).isImm() ||
        !CmpI->getOperand(1).isReg() || !CmpI->getOperand(2).isImm() ||
        CmpI->getOperand(1).getReg() != Bedrock::SP ||
        CmpI->getOperand(2).getImm() != Offset)
      return false;
    if (!fitsImm6(CmpI->getOperand(0).getImm()))
      return false;

    MachineFunction::iterator Next = std::next(MBB.getIterator());
    if (Next == MBB.getParent()->end())
      return false;
    MachineBasicBlock *Fallthrough = &*Next;
    if (!MBB.isSuccessor(Fallthrough))
      return false;

    MachineBasicBlock::iterator FirstI = MBB.begin();
    while (FirstI != MBB.end() && FirstI->isDebugInstr())
      ++FirstI;
    if (FirstI == MBB.end() || &*FirstI != &*CmpI)
      return false;

    Out.MBB = &MBB;
    Out.Cmp = &*CmpI;
    Out.Fallthrough = Fallthrough;
    Out.Imm = CmpI->getOperand(0).getImm();
    return true;
  };

  SmallVector<MachineBasicBlock *, 16> Blocks;
  for (MachineBasicBlock &MBB : MF)
    Blocks.push_back(&MBB);

  for (MachineBasicBlock *Start : Blocks) {
    if (!Start || Start->getParent() != &MF)
      continue;

    MachineBasicBlock::iterator BranchI = Start->getLastNonDebugInstr();
    if (BranchI == Start->end() || BranchI->getOpcode() != Bedrock::JCC)
      continue;
    MachineFunction::iterator Next = std::next(Start->getIterator());
    if (Next == MF.end() || !Start->isSuccessor(&*Next))
      continue;

    MachineInstr *Store = nullptr;
    Register Src;
    int64_t Offset = 0;
    bool Failed = false;
    for (auto I = Start->begin(); I != BranchI; ++I) {
      if (I->isDebugInstr())
        continue;
      Register StoreSrc;
      Register StoreBase;
      int64_t StoreOffset = 0;
      if (isMemStore(*I, StoreSrc, StoreBase, StoreOffset) &&
          I->getOpcode() == Bedrock::MOV32mr && StoreBase == Bedrock::SP) {
        Store = &*I;
        Src = StoreSrc;
        Offset = StoreOffset;
        continue;
      }
      if (Store) {
        if (instrDefinesReg(*I, Src, TRI) ||
            instrHasRegMaskForReg(*I, Src, TRI) ||
            instrHasSPMemOffset(*I, Offset, TRI)) {
          Failed = true;
          break;
        }
      }
    }
    if (Failed || !Store || !isDReg(Src))
      continue;

    SmallVector<StackCmpBlock, 8> Chain;
    MachineBasicBlock *Cur = &*Next;
    while (Cur && Cur->getParent() == &MF) {
      StackCmpBlock Item;
      if (!MatchStackCmpBlock(*Cur, Offset, Item))
        break;
      Chain.push_back(Item);
      Cur = Item.Fallthrough;
    }
    if (Chain.size() < 2)
      continue;

    Store->getOperand(0).setIsKill(false);
    for (StackCmpBlock &Item : Chain) {
      MachineBasicBlock &MBB = *Item.MBB;
      if (!MBB.isLiveIn(Src))
        MBB.addLiveIn(Src);
      MachineInstr &Cmp = *Item.Cmp;
      BuildMI(MBB, Cmp.getIterator(), Cmp.getDebugLoc(),
              TII.get(Bedrock::CMP32ri))
          .addReg(Src)
          .addImm(Item.Imm);
      Cmp.eraseFromParent();
    }
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldKnownZeroCmp(MachineBasicBlock &MBB,
                                           MachineFunction &MF,
                                           uint32_t KnownZeroIn) const {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  uint32_t KnownZero = KnownZeroIn;

  for (MachineInstr &MI : MBB) {
    if (MI.isDebugInstr())
      continue;

    if (isIntCmpOpcode(MI.getOpcode()) && MI.getNumOperands() >= 2 &&
        MI.getOperand(0).isReg() && MI.getOperand(1).isReg()) {
      bool AllowARegs = MI.getOpcode() == Bedrock::CMP64rr;
      for (unsigned OpNo : {0u, 1u}) {
        Register Reg = MI.getOperand(OpNo).getReg();
        if (!maskHasKnownZeroReg(KnownZero, Reg, TRI))
          continue;
        Register Preferred =
            preferredKnownZeroReg(KnownZero, MBB, Reg, TRI, AllowARegs);
        if (!regsOverlap(TRI, Preferred, Reg)) {
          MI.getOperand(OpNo).setReg(Preferred);
          Changed = true;
        }
        break;
      }
    }

    transferKnownZero(MI, KnownZero, TRI);
  }

  return Changed;
}

bool BedrockPeephole::foldEqNeZeroCmpToTest(MachineBasicBlock &MBB,
                                                MachineFunction &MF,
                                                uint32_t KnownZeroIn) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  uint32_t KnownZero = KnownZeroIn;

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &MI = *I;
    if (MI.isDebugInstr()) {
      ++I;
      continue;
    }

    unsigned TestOpcode = getTestOpcodeForCmp(MI.getOpcode());
    if (TestOpcode == 0 || MI.getNumOperands() < 2 ||
        !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg()) {
      transferKnownZero(MI, KnownZero, TRI);
      ++I;
      continue;
    }

    auto BranchI = nextNonDebug(I, MBB);
    if (BranchI == MBB.end() || !isEqNeBranch(*BranchI)) {
      transferKnownZero(MI, KnownZero, TRI);
      ++I;
      continue;
    }

    Register LHS = MI.getOperand(0).getReg();
    Register RHS = MI.getOperand(1).getReg();
    bool LHSZero = maskHasKnownZeroReg(KnownZero, LHS, TRI);
    bool RHSZero = maskHasKnownZeroReg(KnownZero, RHS, TRI);
    if (LHSZero == RHSZero) {
      transferKnownZero(MI, KnownZero, TRI);
      ++I;
      continue;
    }
    Register Tested = LHSZero ? RHS : LHS;
    unsigned CmpZeroOpcode = getCmpZeroImmOpcodeForSelfTest(TestOpcode, Tested);
    bool UseSelfTest = isLegalSelfTestReg(TestOpcode, Tested);
    if (!UseSelfTest && CmpZeroOpcode == 0) {
      transferKnownZero(MI, KnownZero, TRI);
      ++I;
      continue;
    }
    if (UseSelfTest)
      BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(), TII.get(TestOpcode))
          .addReg(Tested)
          .addReg(Tested);
    else
      BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(), TII.get(CmpZeroOpcode))
          .addReg(Tested)
          .addImm(0);
    MI.eraseFromParent();
    I = MBB.begin();
    KnownZero = KnownZeroIn;
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldClrZeroCmpToTest(MachineBasicBlock &MBB,
                                               MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Clr = *I;
    if (Clr.isDebugInstr()) {
      ++I;
      continue;
    }

    Register ZeroReg;
    if (!isClrReg(Clr, ZeroReg)) {
      ++I;
      continue;
    }

    auto CmpI = nextNonDebug(I, MBB);
    while (CmpI != MBB.end() && getTestOpcodeForCmp(CmpI->getOpcode()) == 0) {
      if (CmpI->isCall() || CmpI->isTerminator() ||
          instrTouchesReg(*CmpI, ZeroReg, TRI) ||
          instrTouchesReg(*CmpI, Bedrock::FLAGS, TRI)) {
        CmpI = MBB.end();
        break;
      }
      CmpI = nextNonDebug(CmpI, MBB);
    }
    unsigned TestOpcode =
        CmpI == MBB.end() ? 0 : getTestOpcodeForCmp(CmpI->getOpcode());
    if (TestOpcode == 0 || CmpI->getNumOperands() < 2 ||
        !CmpI->getOperand(0).isReg() || !CmpI->getOperand(1).isReg()) {
      ++I;
      continue;
    }

    Register LHS = CmpI->getOperand(0).getReg();
    Register RHS = CmpI->getOperand(1).getReg();
    bool ZeroIsLHS = regsOverlap(TRI, LHS, ZeroReg);
    bool ZeroIsRHS = regsOverlap(TRI, RHS, ZeroReg);
    if (ZeroIsLHS == ZeroIsRHS) {
      ++I;
      continue;
    }

    Register Tested = ZeroIsLHS ? RHS : LHS;
    unsigned CmpZeroOpcode = getCmpZeroImmOpcodeForSelfTest(TestOpcode, Tested);
    bool UseSelfTest = isLegalSelfTestReg(TestOpcode, Tested);
    if (!UseSelfTest && CmpZeroOpcode == 0) {
      ++I;
      continue;
    }

    auto ConsumerI = nextNonDebug(CmpI, MBB);
    if (ConsumerI == MBB.end()) {
      ++I;
      continue;
    }

    MachineOperand *CondOp = getCondOperand(*ConsumerI);
    if (!CondOp) {
      ++I;
      continue;
    }

    std::optional<int64_t> CC = getCondCodeImm(*CondOp);
    std::optional<int64_t> NewCC =
        CC ? mapZeroCmpCondToTest(*CC, ZeroIsLHS) : std::nullopt;
    if (!NewCC || !regUnusedAfterInCFG(std::next(CmpI), MBB, ZeroReg, TRI) ||
        !regDeadAfterInCFG(std::next(ConsumerI), MBB, Bedrock::FLAGS, TRI)) {
      ++I;
      continue;
    }

    if (UseSelfTest)
      BuildMI(MBB, CmpI->getIterator(), CmpI->getDebugLoc(), TII.get(TestOpcode))
          .addReg(Tested)
          .addReg(Tested);
    else
      BuildMI(MBB, CmpI->getIterator(), CmpI->getDebugLoc(),
              TII.get(CmpZeroOpcode))
          .addReg(Tested)
          .addImm(0);
    setCondCodeImm(*CondOp, *NewCC);
    CmpI->eraseFromParent();
    Clr.eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldAndTestToImmTest(MachineBasicBlock &MBB,
                                               MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &And = *I;
    if (And.isDebugInstr()) {
      ++I;
      continue;
    }

    unsigned TestImmOpcode = getRegTestOpcodeForAndImm(And.getOpcode());
    if (TestImmOpcode == 0 || And.getNumOperands() < 3 ||
        !And.getOperand(0).isReg() || !And.getOperand(1).isReg() ||
        !And.getOperand(2).isImm()) {
      ++I;
      continue;
    }

    Register AndDst = And.getOperand(0).getReg();
    Register AndSrc = And.getOperand(1).getReg();
    int64_t Mask = And.getOperand(2).getImm();
    if (Mask <= 0 || !fitsImm6(Mask) || !regsOverlap(TRI, AndDst, AndSrc)) {
      ++I;
      continue;
    }

    auto TestI = nextNonDebug(I, MBB);
    if (TestI == MBB.end() ||
        !testSelfOpcodeMatchesAndImm(TestI->getOpcode(), And.getOpcode()) ||
        TestI->getNumOperands() < 2 || !TestI->getOperand(0).isReg() ||
        !TestI->getOperand(1).isReg() ||
        !regsOverlap(TRI, TestI->getOperand(0).getReg(), AndDst) ||
        !regsOverlap(TRI, TestI->getOperand(1).getReg(), AndDst)) {
      ++I;
      continue;
    }

    auto BranchI = nextNonDebug(TestI, MBB);
    if (BranchI == MBB.end() || !isEqNeBranch(*BranchI) ||
        !regUnusedAfterInCFG(std::next(TestI), MBB, AndDst, TRI)) {
      ++I;
      continue;
    }

    Register TestReg = AndSrc;
    MachineInstr *Copy = nullptr;
    if (I != MBB.begin()) {
      auto CopyI = I;
      do {
        --CopyI;
      } while (CopyI != MBB.begin() && CopyI->isDebugInstr());

      if (!CopyI->isDebugInstr() && CopyI->getOpcode() == Bedrock::MOV64rr &&
          CopyI->getNumOperands() >= 2 && CopyI->getOperand(0).isReg() &&
          CopyI->getOperand(1).isReg() &&
          regsOverlap(TRI, CopyI->getOperand(0).getReg(), AndDst) &&
          isDReg(CopyI->getOperand(1).getReg())) {
        TestReg = CopyI->getOperand(1).getReg();
        Copy = &*CopyI;
      }
    }

    if (!isDReg(TestReg)) {
      ++I;
      continue;
    }

    BuildMI(MBB, Copy ? Copy->getIterator() : And.getIterator(),
            TestI->getDebugLoc(), TII.get(TestImmOpcode))
        .addReg(TestReg)
        .addImm(Mask);

    if (Copy)
      Copy->eraseFromParent();
    And.eraseFromParent();
    TestI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldDeadClrs(MachineFunction &MF) const {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &MBB : MF) {
    uint32_t KnownZero = 0;
    for (auto I = MBB.begin(); I != MBB.end();) {
      MachineInstr &MI = *I++;
      if (MI.isDebugInstr())
        continue;

      Register Reg;
      if (isClrReg(MI, Reg) && maskHasKnownZeroReg(KnownZero, Reg, TRI) &&
          regDefDeadOrDeadAfter(MI.getIterator(), MBB, Bedrock::FLAGS, TRI)) {
        MI.eraseFromParent();
        Changed = true;
        continue;
      }

      transferKnownZero(MI, KnownZero, TRI);
    }
  }

  for (MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(); I != MBB.end();) {
      MachineInstr &MI = *I++;
      Register Reg;
      if (MI.isDebugInstr() || !isClrReg(MI, Reg))
        continue;
      if (regsOverlap(TRI, Reg, Bedrock::D0) &&
          regReachesRetBeforeTouch(I, MBB, Reg, TRI))
        continue;
      if (functionUsesReg(MF, Reg, &MI, TRI) &&
          !regUnusedAfterInCFG(I, MBB, Reg, TRI))
        continue;
      MI.eraseFromParent();
      Changed = true;
    }
  }

  return Changed;
}

bool BedrockPeephole::foldDeadPlainDefs(MachineFunction &MF) const {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  bool HasReturnValue = !MF.getFunction().getReturnType()->isVoidTy();

  for (MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(); I != MBB.end();) {
      MachineInstr &MI = *I++;
      if (MI.getFlag(MachineInstr::FrameSetup) ||
          MI.getFlag(MachineInstr::FrameDestroy))
        continue;
      Register Reg;
      bool IsCandidate = !MI.isDebugInstr() && isPlainDeadDefCandidate(MI, Reg);
      if (!IsCandidate) {
        Register Base;
        int64_t Offset = 0;
        IsCandidate = !MI.isDebugInstr() && isMemLoad(MI, Reg, Base, Offset) &&
                      Reg != Bedrock::SP && !hasOrderedMemOperand(MI);
      }
      if (!IsCandidate)
        continue;
      if (instrDefinesReg(MI, Bedrock::FLAGS, TRI) &&
          !regDefDeadOrDeadAfterInCFG(MI.getIterator(), MBB, Bedrock::FLAGS,
                                      TRI))
        continue;
      if (isCalleeSaveRestore(MF, MI))
        continue;
      if (HasReturnValue && isReturnValueReg(Reg, TRI) &&
          regReachesRetBeforeTouch(I, MBB, Reg, TRI))
        continue;
      if (functionUsesReg(MF, Reg, &MI, TRI) &&
          !regUnusedAfterInCFG(I, MBB, Reg, TRI))
        continue;
      MI.eraseFromParent();
      Changed = true;
    }
  }

  return Changed;
}

bool BedrockPeephole::foldZeroRegCopies(MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  SmallVector<std::pair<MachineInstr *, Register>, 8> ZeroDefs;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      Register Reg;
      if (!MI.isDebugInstr() && isClrReg(MI, Reg))
        ZeroDefs.push_back({&MI, Reg});
    }
  }

  for (auto [ZeroDef, Reg] : ZeroDefs)
    Changed |= replaceZeroRegCopiesAfterDef(*ZeroDef, Reg, MF, TII, TRI);

  return Changed;
}

bool BedrockPeephole::foldFallthroughJumps(MachineBasicBlock &MBB,
                                               MachineFunction &MF) const {
  MachineBasicBlock::iterator JmpI = MBB.getLastNonDebugInstr();
  if (JmpI == MBB.end() || JmpI->getOpcode() != Bedrock::JMP ||
      JmpI->getNumOperands() == 0 || !JmpI->getOperand(0).isMBB())
    return false;

  auto EnsureBranchSuccessors = [&]() {
    for (MachineInstr &MI : MBB) {
      if (!MI.isBranch())
        continue;
      for (MachineOperand &MO : MI.operands()) {
        if (!MO.isMBB())
          continue;
        MachineBasicBlock *Target = MO.getMBB();
        if (Target && Target->getParent() == &MF && !MBB.isSuccessor(Target))
          MBB.addSuccessor(Target);
      }
    }
  };

  MachineFunction::iterator Next = std::next(MBB.getIterator());
  if (Next == MF.end())
    return false;

  MachineBasicBlock *NextMBB = &*Next;
  MachineBasicBlock *JmpTarget = JmpI->getOperand(0).getMBB();
  if (NextMBB == JmpTarget) {
    JmpI->eraseFromParent();
    return true;
  }

  if (isPlainRetBlock(*JmpTarget)) {
    BuildMI(MBB, JmpI, JmpI->getDebugLoc(),
            MF.getSubtarget().getInstrInfo()->get(Bedrock::RET));
    JmpI->eraseFromParent();
    while (MBB.isSuccessor(JmpTarget))
      MBB.removeSuccessor(JmpTarget);
    EnsureBranchSuccessors();
    return true;
  }

  MachineBasicBlock::iterator BranchI = prevNonDebug(JmpI, MBB);
  if (BranchI == MBB.end() || BranchI->getOpcode() != Bedrock::JCC ||
      BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
      BranchI->getOperand(0).getMBB() != NextMBB)
    return false;

  std::optional<int64_t> CC = getCondCodeImm(BranchI->getOperand(1));
  std::optional<int64_t> InverseCC = CC ? invertCondCode(*CC) : std::nullopt;
  if (!InverseCC)
    return false;

  BranchI->getOperand(0).setMBB(JmpTarget);
  setCondCodeImm(BranchI->getOperand(1), *InverseCC);
  JmpI->eraseFromParent();
  if (!MBB.isSuccessor(NextMBB))
    MBB.addSuccessor(NextMBB);
  if (!MBB.isSuccessor(JmpTarget))
    MBB.addSuccessor(JmpTarget);
  return true;
}

bool BedrockPeephole::foldTailCallReturn(MachineBasicBlock &MBB,
                                             MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Call = *I;
    if (Call.isDebugInstr()) {
      ++I;
      continue;
    }
    if ((Call.getOpcode() != Bedrock::CALLpcrel &&
         Call.getOpcode() != Bedrock::CALLpcrel16) ||
        Call.getNumOperands() < 1) {
      ++I;
      continue;
    }

    auto Scan = nextNonDebug(I, MBB);
    while (Scan != MBB.end()) {
      if (isPopOpcode(Scan->getOpcode()) &&
          Scan->getFlag(MachineInstr::FrameDestroy)) {
        Scan = nextNonDebug(Scan, MBB);
        continue;
      }
      break;
    }

    if (Scan == MBB.end() || Scan->getOpcode() != Bedrock::RET) {
      ++I;
      continue;
    }

    unsigned TailOpcode = Call.getOpcode() == Bedrock::CALLpcrel16
                              ? Bedrock::JMPWpcrel
                              : Bedrock::JMPLpcrel;
    MachineInstrBuilder Tail =
        BuildMI(MBB, Scan, Call.getDebugLoc(), TII.get(TailOpcode));
    Tail.add(Call.getOperand(0));
    for (unsigned OpNo = 1, E = Call.getNumOperands(); OpNo != E; ++OpNo) {
      const MachineOperand &MO = Call.getOperand(OpNo);
      if (MO.isReg() && MO.isImplicit() && MO.readsReg())
        Tail.add(MO);
    }
    Call.eraseFromParent();
    Scan->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}
