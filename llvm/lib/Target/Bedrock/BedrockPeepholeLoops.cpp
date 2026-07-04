//===-- BedrockPeepholeLoops.cpp - Bedrock peepholes -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockPeephole.h"

bool BedrockPeephole::foldCountedBranchShortcuts(
    MachineBasicBlock &MBB, MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &MI = *I;
    if (MI.isDebugInstr()) {
      ++I;
      continue;
    }

    unsigned DJccOpcode = getDJccOpcodeForDec(MI.getOpcode());
    if (DJccOpcode != 0 && MI.getNumOperands() >= 2 &&
        MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
        regsOverlap(TRI, MI.getOperand(0).getReg(),
                    MI.getOperand(1).getReg()) &&
        isDReg(MI.getOperand(0).getReg())) {
      auto BranchI = nextNonDebug(I, MBB);
      std::optional<int64_t> CC =
          BranchI == MBB.end() || BranchI->getNumOperands() < 2
              ? std::nullopt
              : getCondCodeImm(BranchI->getOperand(1));
      if (BranchI != MBB.end() && BranchI->getOpcode() == Bedrock::JCC &&
          BranchI->getNumOperands() >= 2 && BranchI->getOperand(0).isMBB() &&
          CC && *CC == BedrockCC::NE) {
        Register Counter = MI.getOperand(0).getReg();
        BuildMI(MBB, I, MI.getDebugLoc(), TII.get(DJccOpcode), Counter)
            .addReg(Counter)
            .addMBB(BranchI->getOperand(0).getMBB())
            .addImm(BedrockCC::T);
        MI.eraseFromParent();
        BranchI->eraseFromParent();
        I = MBB.begin();
        Changed = true;
        continue;
      }
    }

    if ((MI.getOpcode() == Bedrock::INC32r ||
         MI.getOpcode() == Bedrock::INC64r) &&
        MI.getNumOperands() >= 2 && MI.getOperand(0).isReg() &&
        MI.getOperand(1).isReg() &&
        regsOverlap(TRI, MI.getOperand(0).getReg(),
                    MI.getOperand(1).getReg()) &&
        isDReg(MI.getOperand(0).getReg())) {
      auto CmpI = nextNonDebug(I, MBB);
      auto BranchI = CmpI == MBB.end() ? MBB.end() : nextNonDebug(CmpI, MBB);
      std::optional<int64_t> CC =
          BranchI == MBB.end() || BranchI->getNumOperands() < 2
              ? std::nullopt
              : getCondCodeImm(BranchI->getOperand(1));
      if (CmpI != MBB.end() && BranchI != MBB.end() &&
          BranchI->getOpcode() == Bedrock::JCC &&
          BranchI->getNumOperands() >= 2 && BranchI->getOperand(0).isMBB() &&
          CC && *CC == BedrockCC::NE && CmpI->getNumOperands() >= 2 &&
          CmpI->getOperand(0).isReg() && CmpI->getOperand(1).isReg()) {
        unsigned IJccOpcode =
            getIJccOpcodeForIncCmp(MI.getOpcode(), CmpI->getOpcode());
        Register Index = MI.getOperand(0).getReg();
        Register CmpLHS = CmpI->getOperand(0).getReg();
        Register CmpRHS = CmpI->getOperand(1).getReg();
        Register Bound = Bedrock::NoRegister;
        if (regsOverlap(TRI, CmpLHS, Index))
          Bound = CmpRHS;
        else if (regsOverlap(TRI, CmpRHS, Index))
          Bound = CmpLHS;

        if (IJccOpcode != 0 && Bound != Bedrock::NoRegister && isDReg(Bound) &&
            !regsOverlap(TRI, Bound, Index)) {
          BuildMI(MBB, I, MI.getDebugLoc(), TII.get(IJccOpcode), Index)
              .addReg(Index)
              .addReg(Bound)
              .addMBB(BranchI->getOperand(0).getMBB())
              .addImm(BedrockCC::T);
          MI.eraseFromParent();
          CmpI->eraseFromParent();
          BranchI->eraseFromParent();
          I = MBB.begin();
          Changed = true;
          continue;
        }
      }
    }

    ++I;
  }

  return Changed;
}

bool BedrockPeephole::foldTopTestIncLoopToIJcc(MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  SmallVector<MachineBasicBlock *, 8> Headers;
  for (MachineBasicBlock &MBB : MF)
    Headers.push_back(&MBB);

  for (MachineBasicBlock *Header : Headers) {
    if (!Header || Header->getParent() != &MF)
      continue;

    MachineBasicBlock::iterator CmpI = Header->begin();
    while (CmpI != Header->end() && CmpI->isDebugInstr())
      ++CmpI;
    MachineBasicBlock::iterator BranchI =
        CmpI == Header->end() ? Header->end() : nextNonDebug(CmpI, *Header);
    if (CmpI == Header->end() || BranchI == Header->end() ||
        (CmpI->getOpcode() != Bedrock::CMP32rr &&
         CmpI->getOpcode() != Bedrock::CMP64rr) ||
        CmpI->getNumOperands() < 2 || !CmpI->getOperand(0).isReg() ||
        !CmpI->getOperand(1).isReg() || BranchI->getOpcode() != Bedrock::JCC ||
        BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
        nextNonDebug(BranchI, *Header) != Header->end())
      continue;

    std::optional<int64_t> BranchCC = getCondCodeImm(BranchI->getOperand(1));
    if (!BranchCC || *BranchCC != BedrockCC::EQ)
      continue;

    MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();
    if (!Exit || Exit == Header)
      continue;

    MachineBasicBlock *Body = nullptr;
    bool MultipleBodies = false;
    for (MachineBasicBlock *Succ : Header->successors()) {
      if (Succ == Exit)
        continue;
      if (Body) {
        MultipleBodies = true;
        break;
      }
      Body = Succ;
    }
    if (MultipleBodies || !Body || Body == Header || Body->getParent() != &MF ||
        !Header->isSuccessor(Body) || !Header->isSuccessor(Exit) ||
        !Body->isSuccessor(Header))
      continue;

    MachineFunction::iterator BodyNext = std::next(Body->getIterator());
    if (BodyNext == MF.end() || &*BodyNext != Exit)
      continue;

    MachineBasicBlock::iterator JmpI = Body->getLastNonDebugInstr();
    if (JmpI == Body->end() || JmpI->getOpcode() != Bedrock::JMP ||
        JmpI->getNumOperands() < 1 || !JmpI->getOperand(0).isMBB() ||
        JmpI->getOperand(0).getMBB() != Header)
      continue;

    MachineBasicBlock::iterator IncI = prevNonDebug(JmpI, *Body);
    if (IncI == Body->end() ||
        (IncI->getOpcode() != Bedrock::INC32r &&
         IncI->getOpcode() != Bedrock::INC64r) ||
        IncI->getNumOperands() < 2 || !IncI->getOperand(0).isReg() ||
        !IncI->getOperand(1).isReg() ||
        !regsOverlap(TRI, IncI->getOperand(0).getReg(),
                     IncI->getOperand(1).getReg()) ||
        !isDReg(IncI->getOperand(0).getReg()))
      continue;

    Register Index = IncI->getOperand(0).getReg();
    Register CmpLHS = CmpI->getOperand(0).getReg();
    Register CmpRHS = CmpI->getOperand(1).getReg();
    Register Bound = Bedrock::NoRegister;
    if (regsOverlap(TRI, CmpLHS, Index))
      Bound = CmpRHS;
    else if (regsOverlap(TRI, CmpRHS, Index))
      Bound = CmpLHS;
    if (Bound == Bedrock::NoRegister || !isDReg(Bound) ||
        regsOverlap(TRI, Bound, Index))
      continue;

    unsigned IJccOpcode =
        getIJccOpcodeForIncCmp(IncI->getOpcode(), CmpI->getOpcode());
    if (IJccOpcode == 0)
      continue;

    if (!regUnusedBeforeEndOrDef(Body->begin(), *Body, Bedrock::FLAGS, TRI))
      continue;

    BuildMI(*Body, IncI, IncI->getDebugLoc(), TII.get(IJccOpcode), Index)
        .addReg(Index)
        .addReg(Bound)
        .addMBB(Body)
        .addImm(BedrockCC::T);
    IncI->eraseFromParent();
    JmpI->eraseFromParent();

    Body->removeSuccessor(Header);
    if (!Body->isSuccessor(Body))
      Body->addSuccessor(Body);
    if (!Body->isSuccessor(Exit))
      Body->addSuccessor(Exit);
    if (!Body->isLiveIn(Index))
      Body->addLiveIn(Index);
    if (!Body->isLiveIn(Bound))
      Body->addLiveIn(Bound);
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldTopTestCountedLoop(
    MachineBasicBlock &Body, MachineFunction &MF,
    const DenseMap<MachineBasicBlock *, uint32_t> &KnownZeroIns) const {
  MachineBasicBlock::iterator JmpI = Body.getLastNonDebugInstr();
  if (JmpI == Body.end() || JmpI->getOpcode() != Bedrock::JMP ||
      JmpI->getNumOperands() == 0 || !JmpI->getOperand(0).isMBB())
    return false;

  MachineBasicBlock *Header = JmpI->getOperand(0).getMBB();
  if (!Header || Header == &Body || Body.succ_size() != 1 ||
      !Body.isSuccessor(Header))
    return false;

  MachineFunction::iterator HeaderNext = std::next(Header->getIterator());
  if (HeaderNext == MF.end() || &*HeaderNext != &Body)
    return false;

  MachineFunction::iterator ExitI = std::next(Body.getIterator());
  if (ExitI == MF.end())
    return false;
  MachineBasicBlock *Exit = &*ExitI;

  MachineBasicBlock::iterator DecI = prevNonDebug(JmpI, Body);
  if (DecI == Body.end())
    return false;

  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  Register CountReg;
  unsigned CmpOpcode = 0;
  if (!isIncDecRegInstr(*DecI, CountReg, CmpOpcode, TRI))
    return false;

  MachineBasicBlock::iterator CmpI = Header->begin();
  while (CmpI != Header->end() && CmpI->isDebugInstr())
    ++CmpI;
  if (CmpI == Header->end())
    return false;

  MachineBasicBlock::iterator BranchI = nextNonDebug(CmpI, *Header);
  std::optional<int64_t> BranchCC =
      BranchI == Header->end() || BranchI->getNumOperands() < 2
          ? std::nullopt
          : getCondCodeImm(BranchI->getOperand(1));
  if (BranchI == Header->end() || BranchI->getOpcode() != Bedrock::JCC ||
      BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
      BranchI->getOperand(0).getMBB() != Exit || !BranchCC ||
      *BranchCC != BedrockCC::EQ)
    return false;

  if (nextNonDebug(BranchI, *Header) != Header->end())
    return false;

  uint32_t HeaderKnownZero = KnownZeroIns.lookup(Header);
  if (!isKnownZeroCmpForReg(*CmpI, CountReg, HeaderKnownZero, CmpOpcode, TRI))
    return false;

  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  BuildMI(Body, JmpI, JmpI->getDebugLoc(), TII.get(Bedrock::JCC))
      .addMBB(&Body)
      .addImm(BedrockCC::NE);
  JmpI->eraseFromParent();

  Body.removeSuccessor(Header);
  if (!Body.isSuccessor(&Body))
    Body.addSuccessor(&Body);
  if (!Body.isSuccessor(Exit))
    Body.addSuccessor(Exit);
  return true;
}

bool BedrockPeephole::foldPositiveCountedLoopPretest(
    MachineFunction &MF,
    const DenseMap<MachineBasicBlock *, uint32_t> &KnownZeroIns) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  SmallVector<MachineBasicBlock *, 8> Headers;
  for (MachineBasicBlock &MBB : MF)
    Headers.push_back(&MBB);

  for (MachineBasicBlock *HeaderPtr : Headers) {
    if (!HeaderPtr || HeaderPtr->getParent() != &MF)
      continue;
    MachineBasicBlock &Header = *HeaderPtr;
    MachineBasicBlock::iterator CmpI = Header.begin();
    while (CmpI != Header.end() && CmpI->isDebugInstr())
      ++CmpI;
    if (CmpI == Header.end())
      continue;

    Register CountReg;
    if (!findCountedLoopHeaderInfo(Header, KnownZeroIns.lookup(&Header),
                                   CountReg, TRI))
      continue;

    MachineBasicBlock::iterator BranchI = nextNonDebug(CmpI, Header);
    std::optional<int64_t> BranchCC =
        BranchI == Header.end() || BranchI->getNumOperands() < 2
            ? std::nullopt
            : getCondCodeImm(BranchI->getOperand(1));
    if (BranchI == Header.end() || BranchI->getOpcode() != Bedrock::JCC ||
        BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
        !BranchCC || *BranchCC != BedrockCC::EQ ||
        nextNonDebug(BranchI, Header) != Header.end())
      continue;
    MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();

    MachineFunction::iterator BodyI = std::next(Header.getIterator());
    if (BodyI == MF.end())
      continue;
    MachineBasicBlock &Body = *BodyI;
    if (!Header.isSuccessor(&Body) || !Header.isSuccessor(Exit))
      continue;

    MachineBasicBlock::iterator LatchI = Body.getLastNonDebugInstr();
    std::optional<int64_t> LatchCC =
        LatchI == Body.end() || LatchI->getNumOperands() < 2
            ? std::nullopt
            : getCondCodeImm(LatchI->getOperand(1));
    if (LatchI == Body.end() || LatchI->getOpcode() != Bedrock::JCC ||
        LatchI->getNumOperands() < 2 || !LatchI->getOperand(0).isMBB() ||
        LatchI->getOperand(0).getMBB() != &Body || !LatchCC ||
        *LatchCC != BedrockCC::NE)
      continue;

    MachineBasicBlock::iterator DecI = prevNonDebug(LatchI, Body);
    if (DecI == Body.end() ||
        !isUnaryRegOp(*DecI, Bedrock::DEC64r, CountReg, TRI))
      continue;

    if (Header.pred_size() != 1)
      continue;
    MachineBasicBlock *Preheader = *Header.pred_begin();
    if (Preheader == &Body || Preheader == &Header)
      continue;

    MachineInstr *Ext = nullptr;
    for (MachineInstr &MI : *Preheader) {
      if (MI.isDebugInstr())
        continue;
      if (MI.getOpcode() == Bedrock::EXTZQ32rr && MI.getNumOperands() >= 2 &&
          MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
          regsOverlap(TRI, MI.getOperand(0).getReg(), CountReg)) {
        Ext = &MI;
        break;
      }
    }
    if (!Ext)
      continue;
    Register MaxReg = Ext->getOperand(1).getReg();

    MachineInstr *Max = nullptr;
    uint32_t KnownZero = KnownZeroIns.lookup(Preheader);
    for (MachineInstr &MI : *Preheader) {
      if (MI.isDebugInstr())
        continue;
      if (&MI == Ext)
        break;
      if (MI.getOpcode() == Bedrock::MAXS32rr && MI.getNumOperands() >= 3 &&
          MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
          MI.getOperand(2).isReg() &&
          regsOverlap(TRI, MI.getOperand(0).getReg(), MaxReg)) {
        Max = &MI;
        break;
      }
      transferKnownZero(MI, KnownZero, TRI);
    }
    if (!Max)
      continue;

    Register LHS = Max->getOperand(1).getReg();
    Register RHS = Max->getOperand(2).getReg();
    bool LHSZero = maskHasKnownZeroReg(KnownZero, LHS, TRI);
    bool RHSZero = maskHasKnownZeroReg(KnownZero, RHS, TRI);
    if (LHSZero == RHSZero)
      continue;
    Register OriginalReg = LHSZero ? RHS : LHS;
    if (!regsOverlap(TRI, OriginalReg, CountReg))
      continue;
    Register ZeroReg = LHSZero ? LHS : RHS;

    MachineInstr *ZeroClr =
        findRemovableClrDefBefore(*Preheader, Max->getIterator(), ZeroReg, TRI);
    bool UseZeroReturn = ZeroClr && regsOverlap(TRI, MaxReg, Bedrock::D0) &&
                         regsOverlap(TRI, ZeroReg, Bedrock::D0) &&
                         !regsOverlap(TRI, MaxReg, CountReg) &&
                         isPlainRetBlock(*Exit);

    BuildMI(Header, CmpI, CmpI->getDebugLoc(), TII.get(Bedrock::TEST32rr))
        .addReg(CountReg)
        .addReg(CountReg);
    setCondCodeImm(BranchI->getOperand(1), BedrockCC::LE);
    CmpI->eraseFromParent();

    DecI->setDesc(TII.get(Bedrock::DEC32r));
    if (UseZeroReturn) {
      MachineBasicBlock *ZeroExit =
          createZeroReturnBlock(MF, *Exit, BranchI->getDebugLoc(), TII);
      BranchI->getOperand(0).setMBB(ZeroExit);
      Header.replaceSuccessor(Exit, ZeroExit);

      for (MachineInstr &MI : *Preheader) {
        if (&MI == ZeroClr)
          break;
        if (MI.isDebugInstr() || MI.getOpcode() != Bedrock::MOV64rr ||
            MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
            !MI.getOperand(1).isReg())
          continue;
        if (regsOverlap(TRI, MI.getOperand(0).getReg(), CountReg) &&
            regsOverlap(TRI, MI.getOperand(1).getReg(), MaxReg)) {
          MI.setDesc(TII.get(Bedrock::MOV32rr));
          break;
        }
      }

      Max->eraseFromParent();
      Ext->eraseFromParent();
      ZeroClr->eraseFromParent();
    } else if (!regsOverlap(TRI, MaxReg, CountReg)) {
      Ext->setDesc(TII.get(Bedrock::MOV32rr));
      Ext->getOperand(0).setReg(CountReg);
      Ext->getOperand(1).setReg(MaxReg);
    } else {
      Max->eraseFromParent();
      Ext->eraseFromParent();
    }
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldPositiveCountedLoopHeaderPretest(
    MachineFunction &MF,
    const DenseMap<MachineBasicBlock *, uint32_t> &KnownZeroIns) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &Header : MF) {
    MachineBasicBlock::iterator CmpI = Header.begin();
    while (CmpI != Header.end() && CmpI->isDebugInstr())
      ++CmpI;
    if (CmpI == Header.end())
      continue;

    Register CountReg;
    if (!findCountedLoopHeaderInfo(Header, KnownZeroIns.lookup(&Header),
                                   CountReg, TRI))
      continue;

    MachineBasicBlock::iterator BranchI = nextNonDebug(CmpI, Header);
    std::optional<int64_t> BranchCC =
        BranchI == Header.end() || BranchI->getNumOperands() < 2
            ? std::nullopt
            : getCondCodeImm(BranchI->getOperand(1));
    if (BranchI == Header.end() || BranchI->getOpcode() != Bedrock::JCC ||
        BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
        !BranchCC || *BranchCC != BedrockCC::EQ ||
        nextNonDebug(BranchI, Header) != Header.end())
      continue;
    MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();

    SmallPtrSet<MachineBasicBlock *, 16> LoopBlockSet;
    if (!collectLoopBlocksUntilExit(Header, *Exit, LoopBlockSet))
      continue;

    MachineBasicBlock *Preheader = nullptr;
    SmallVector<MachineInstr *, 2> Decs;
    SmallPtrSet<MachineBasicBlock *, 4> SeenPreds;
    bool InvalidPred = false;
    for (MachineBasicBlock *Pred : Header.predecessors()) {
      if (!SeenPreds.insert(Pred).second)
        continue;
      if (LoopBlockSet.contains(Pred)) {
        MachineInstr *PredDec = nullptr;
        for (MachineInstr &MI : *Pred) {
          if (MI.isDebugInstr())
            continue;
          if (isUnaryRegOp(MI, Bedrock::DEC64r, CountReg, TRI)) {
            if (PredDec) {
              InvalidPred = true;
              break;
            }
            PredDec = &MI;
          }
        }
        if (!PredDec)
          InvalidPred = true;
        else
          Decs.push_back(PredDec);
      } else if (!Preheader) {
        Preheader = Pred;
      } else {
        InvalidPred = true;
      }
      if (InvalidPred)
        break;
    }
    if (InvalidPred || !Preheader || Decs.size() != 1)
      continue;
    MachineInstr *Dec = Decs[0];

    MachineInstr *Ext = nullptr;
    for (MachineInstr &MI : *Preheader) {
      if (MI.isDebugInstr())
        continue;
      if (MI.getOpcode() == Bedrock::EXTZQ32rr && MI.getNumOperands() >= 2 &&
          MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
          regsOverlap(TRI, MI.getOperand(0).getReg(), CountReg)) {
        Ext = &MI;
        break;
      }
    }
    if (!Ext)
      continue;
    Register MaxReg = Ext->getOperand(1).getReg();

    MachineInstr *Max = nullptr;
    uint32_t KnownZero = KnownZeroIns.lookup(Preheader);
    for (MachineInstr &MI : *Preheader) {
      if (MI.isDebugInstr())
        continue;
      if (&MI == Ext)
        break;
      if (MI.getOpcode() == Bedrock::MAXS32rr && MI.getNumOperands() >= 3 &&
          MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
          MI.getOperand(2).isReg() &&
          regsOverlap(TRI, MI.getOperand(0).getReg(), MaxReg)) {
        Max = &MI;
        break;
      }
      transferKnownZero(MI, KnownZero, TRI);
    }
    if (!Max)
      continue;

    Register LHS = Max->getOperand(1).getReg();
    Register RHS = Max->getOperand(2).getReg();
    bool LHSZero = maskHasKnownZeroReg(KnownZero, LHS, TRI);
    bool RHSZero = maskHasKnownZeroReg(KnownZero, RHS, TRI);
    if (LHSZero == RHSZero)
      continue;
    Register OriginalReg = LHSZero ? RHS : LHS;
    Register ZeroReg = LHSZero ? LHS : RHS;
    Register NewCountReg =
        regsOverlap(TRI, OriginalReg, CountReg) ? CountReg : OriginalReg;
    if (!isDReg(NewCountReg))
      continue;
    MachineInstr *ZeroClr =
        findRemovableClrDefBefore(*Preheader, Max->getIterator(), ZeroReg, TRI);
    bool UseZeroReturn = ZeroClr && regsOverlap(TRI, MaxReg, Bedrock::D0) &&
                         regsOverlap(TRI, ZeroReg, Bedrock::D0) &&
                         !regsOverlap(TRI, MaxReg, NewCountReg) &&
                         isPlainRetBlock(*Exit);

    MachineInstr *DeadClobber = nullptr;
    bool ClobberUnsafe = false;
    for (auto Scan = nextNonDebug(Ext->getIterator(), *Preheader);
         Scan != Preheader->end(); Scan = nextNonDebug(Scan, *Preheader)) {
      if (!instrTouchesReg(*Scan, NewCountReg, TRI))
        continue;
      Register ClrDst;
      if (!DeadClobber && isClrReg(*Scan, ClrDst) &&
          regsOverlap(TRI, ClrDst, NewCountReg)) {
        DeadClobber = &*Scan;
        continue;
      }
      ClobberUnsafe = true;
      break;
    }
    if (ClobberUnsafe)
      continue;

    SmallVector<MachineBasicBlock *, 16> LoopBlocks;
    for (MachineBasicBlock *MBB : LoopBlockSet)
      LoopBlocks.push_back(MBB);
    if (!loopOnlyTouchesCountAt(LoopBlocks, *CmpI, *Dec, CountReg, NewCountReg,
                                TRI))
      continue;

    BuildMI(Header, CmpI, CmpI->getDebugLoc(), TII.get(Bedrock::TEST32rr))
        .addReg(NewCountReg)
        .addReg(NewCountReg);
    setCondCodeImm(BranchI->getOperand(1), BedrockCC::LE);
    CmpI->eraseFromParent();

    Dec->setDesc(TII.get(Bedrock::DEC32r));
    Dec->getOperand(0).setReg(NewCountReg);
    Dec->getOperand(1).setReg(NewCountReg);
    if (!Header.isLiveIn(NewCountReg))
      Header.addLiveIn(NewCountReg);
    if (!Dec->getParent()->isLiveIn(NewCountReg))
      Dec->getParent()->addLiveIn(NewCountReg);

    if (UseZeroReturn) {
      MachineBasicBlock *ZeroExit =
          createZeroReturnBlock(MF, *Exit, BranchI->getDebugLoc(), TII);
      BranchI->getOperand(0).setMBB(ZeroExit);
      Header.replaceSuccessor(Exit, ZeroExit);

      for (MachineInstr &MI : *Preheader) {
        if (&MI == ZeroClr)
          break;
        if (MI.isDebugInstr() || MI.getOpcode() != Bedrock::MOV64rr ||
            MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
            !MI.getOperand(1).isReg())
          continue;
        if (regsOverlap(TRI, MI.getOperand(0).getReg(), NewCountReg) &&
            regsOverlap(TRI, MI.getOperand(1).getReg(), MaxReg)) {
          MI.setDesc(TII.get(Bedrock::MOV32rr));
          break;
        }
      }

      Max->eraseFromParent();
      Ext->eraseFromParent();
      ZeroClr->eraseFromParent();
    } else if (!regsOverlap(TRI, MaxReg, NewCountReg)) {
      Ext->setDesc(TII.get(Bedrock::MOV32rr));
      Ext->getOperand(0).setReg(NewCountReg);
      Ext->getOperand(1).setReg(MaxReg);
    } else {
      Max->eraseFromParent();
      Ext->eraseFromParent();
    }
    if (DeadClobber)
      DeadClobber->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldSMaxCountdownPretest(
    MachineFunction &MF,
    const DenseMap<MachineBasicBlock *, uint32_t> &KnownZeroIns) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &Preheader : MF) {
    if (Preheader.succ_size() != 1)
      continue;
    MachineBasicBlock *Header = *Preheader.succ_begin();
    if (!Header || Header == &Preheader)
      continue;

    MachineBasicBlock::iterator TestI = Header->begin();
    while (TestI != Header->end() && TestI->isDebugInstr())
      ++TestI;
    MachineBasicBlock::iterator BranchI =
        TestI == Header->end() ? Header->end() : nextNonDebug(TestI, *Header);
    std::optional<int64_t> BranchCC =
        BranchI == Header->end() || BranchI->getNumOperands() < 2
            ? std::nullopt
            : getCondCodeImm(BranchI->getOperand(1));
    if (TestI == Header->end() || BranchI == Header->end() ||
        BranchI->getOpcode() != Bedrock::JCC || BranchI->getNumOperands() < 2 ||
        !BranchI->getOperand(0).isMBB() || !BranchCC ||
        (*BranchCC != BedrockCC::EQ && *BranchCC != BedrockCC::LE) ||
        nextNonDebug(BranchI, *Header) != Header->end())
      continue;

    Register CountReg;
    if (TestI->getOpcode() == Bedrock::TEST32rr &&
        TestI->getNumOperands() >= 2 && TestI->getOperand(0).isReg() &&
        TestI->getOperand(1).isReg() &&
        regsOverlap(TRI, TestI->getOperand(0).getReg(),
                    TestI->getOperand(1).getReg())) {
      CountReg = TestI->getOperand(0).getReg();
    } else if (TestI->getOpcode() == Bedrock::CMP32rr &&
               TestI->getNumOperands() >= 2 && TestI->getOperand(0).isReg() &&
               TestI->getOperand(1).isReg()) {
      Register LHS = TestI->getOperand(0).getReg();
      Register RHS = TestI->getOperand(1).getReg();
      bool LHSZero = maskHasKnownZeroReg(KnownZeroIns.lookup(Header), LHS, TRI);
      bool RHSZero = maskHasKnownZeroReg(KnownZeroIns.lookup(Header), RHS, TRI);
      if (LHSZero == RHSZero)
        continue;
      CountReg = LHSZero ? RHS : LHS;
    } else {
      continue;
    }
    MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();
    if (!Exit)
      continue;

    SmallPtrSet<MachineBasicBlock *, 16> LoopBlockSet;
    if (!collectLoopBlocksUntilExit(*Header, *Exit, LoopBlockSet))
      continue;

    SmallVector<MachineBasicBlock *, 16> LoopBlocks;
    MachineInstr *Dec = nullptr;
    bool InvalidDec = false;
    for (MachineBasicBlock *MBB : LoopBlockSet) {
      LoopBlocks.push_back(MBB);
      for (MachineInstr &MI : *MBB) {
        if (MI.isDebugInstr())
          continue;
        if (!isUnaryRegOp(MI, Bedrock::DEC32r, CountReg, TRI) &&
            !isUnaryRegOp(MI, Bedrock::DEC64r, CountReg, TRI))
          continue;
        if (Dec) {
          InvalidDec = true;
          break;
        }
        Dec = &MI;
      }
      if (InvalidDec)
        break;
    }
    if (InvalidDec || !Dec ||
        !loopOnlyTouchesCountAt(LoopBlocks, *TestI, *Dec, CountReg, CountReg,
                                TRI))
      continue;

    MachineInstr *CountDef = nullptr;
    for (auto I = Preheader.end(); I != Preheader.begin();) {
      --I;
      if (I->isDebugInstr())
        continue;
      if (!instrDefinesReg(*I, CountReg, TRI))
        continue;
      CountDef = &*I;
      break;
    }
    if (!CountDef)
      continue;

    MachineInstr *Max = nullptr;
    MachineInstr *CopyFromMax = nullptr;
    Register MaxReg = CountReg;
    if (CountDef->getOpcode() == Bedrock::MAXS32rr &&
        CountDef->getNumOperands() >= 3 && CountDef->getOperand(0).isReg() &&
        regsOverlap(TRI, CountDef->getOperand(0).getReg(), CountReg)) {
      Max = CountDef;
    } else if ((CountDef->getOpcode() == Bedrock::MOV32rr ||
                CountDef->getOpcode() == Bedrock::MOV64rr ||
                CountDef->getOpcode() == Bedrock::TRUNC64to32) &&
               CountDef->getNumOperands() >= 2 &&
               CountDef->getOperand(0).isReg() &&
               CountDef->getOperand(1).isReg() &&
               regsOverlap(TRI, CountDef->getOperand(0).getReg(), CountReg)) {
      CopyFromMax = CountDef;
      MaxReg = CountDef->getOperand(1).getReg();
      for (auto I = CountDef->getIterator(); I != Preheader.begin();) {
        --I;
        if (I->isDebugInstr())
          continue;
        if (!instrDefinesReg(*I, MaxReg, TRI))
          continue;
        if (I->getOpcode() == Bedrock::MAXS32rr && I->getNumOperands() >= 3 &&
            I->getOperand(0).isReg() &&
            regsOverlap(TRI, I->getOperand(0).getReg(), MaxReg))
          Max = &*I;
        break;
      }
    }
    if (!Max || Max->getNumOperands() < 3 || !Max->getOperand(1).isReg() ||
        !Max->getOperand(2).isReg())
      continue;

    Register LHS = Max->getOperand(1).getReg();
    Register RHS = Max->getOperand(2).getReg();
    MachineInstr *LHSZero =
        findRemovableClrDefBefore(Preheader, Max->getIterator(), LHS, TRI);
    MachineInstr *RHSZero =
        findRemovableClrDefBefore(Preheader, Max->getIterator(), RHS, TRI);
    if (!!LHSZero == !!RHSZero)
      continue;
    MachineInstr *ZeroClr = LHSZero ? LHSZero : RHSZero;
    Register ZeroReg = LHSZero ? LHS : RHS;
    Register OriginalReg = LHSZero ? RHS : LHS;
    if (!regsOverlap(TRI, OriginalReg, CountReg))
      continue;

    bool UnsafeUse = false;
    for (auto I = nextNonDebug(Max->getIterator(), Preheader);
         I != Preheader.end(); I = nextNonDebug(I, Preheader)) {
      if (&*I == CopyFromMax)
        continue;
      if (instrTouchesReg(*I, CountReg, TRI) ||
          (!regsOverlap(TRI, MaxReg, CountReg) &&
           instrTouchesReg(*I, MaxReg, TRI))) {
        UnsafeUse = true;
        break;
      }
    }
    if (UnsafeUse)
      continue;

    bool UseZeroReturn = regsOverlap(TRI, MaxReg, Bedrock::D0) &&
                         regsOverlap(TRI, ZeroReg, Bedrock::D0) &&
                         !regsOverlap(TRI, CountReg, Bedrock::D0) &&
                         isPlainRetBlock(*Exit);

    setCondCodeImm(BranchI->getOperand(1), BedrockCC::LE);
    if (TestI->getOpcode() != Bedrock::TEST32rr ||
        !regsOverlap(TRI, TestI->getOperand(0).getReg(), CountReg) ||
        !regsOverlap(TRI, TestI->getOperand(1).getReg(), CountReg)) {
      BuildMI(*Header, TestI, TestI->getDebugLoc(), TII.get(Bedrock::TEST32rr))
          .addReg(CountReg)
          .addReg(CountReg);
      TestI->eraseFromParent();
    }
    if (Dec->getOpcode() == Bedrock::DEC64r)
      Dec->setDesc(TII.get(Bedrock::DEC32r));

    MachineInstr *OriginalCountCopy = findLastDefBefore(*Max, CountReg, TRI);
    if (OriginalCountCopy &&
        OriginalCountCopy->getOpcode() == Bedrock::MOV64rr &&
        OriginalCountCopy->getNumOperands() >= 2 &&
        OriginalCountCopy->getOperand(0).isReg() &&
        regsOverlap(TRI, OriginalCountCopy->getOperand(0).getReg(), CountReg) &&
        onlyUsesRegAsI32Before(*OriginalCountCopy, *Max, CountReg, TRI))
      OriginalCountCopy->setDesc(TII.get(Bedrock::MOV32rr));

    if (UseZeroReturn) {
      MachineBasicBlock *ZeroExit =
          createZeroReturnBlock(MF, *Exit, BranchI->getDebugLoc(), TII);
      BranchI->getOperand(0).setMBB(ZeroExit);
      Header->replaceSuccessor(Exit, ZeroExit);
      if (CopyFromMax)
        CopyFromMax->eraseFromParent();
      Max->eraseFromParent();
      ZeroClr->eraseFromParent();
    } else if (CopyFromMax) {
      CopyFromMax->eraseFromParent();
    } else if (regsOverlap(TRI, MaxReg, CountReg)) {
      Max->eraseFromParent();
      ZeroClr->eraseFromParent();
    }
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldSMaxPretestZeroReturn(MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &Preheader : MF) {
    if (Preheader.succ_size() != 1)
      continue;
    MachineBasicBlock *Header = *Preheader.succ_begin();
    if (!Header || Header == &Preheader)
      continue;

    MachineBasicBlock::iterator TestI = Header->begin();
    while (TestI != Header->end() && TestI->isDebugInstr())
      ++TestI;
    MachineBasicBlock::iterator BranchI =
        TestI == Header->end() ? Header->end() : nextNonDebug(TestI, *Header);
    std::optional<int64_t> BranchCC =
        BranchI == Header->end() || BranchI->getNumOperands() < 2
            ? std::nullopt
            : getCondCodeImm(BranchI->getOperand(1));
    if (TestI == Header->end() || BranchI == Header->end() ||
        TestI->getOpcode() != Bedrock::TEST32rr ||
        TestI->getNumOperands() < 2 || !TestI->getOperand(0).isReg() ||
        !TestI->getOperand(1).isReg() ||
        !regsOverlap(TRI, TestI->getOperand(0).getReg(),
                     TestI->getOperand(1).getReg()) ||
        BranchI->getOpcode() != Bedrock::JCC || BranchI->getNumOperands() < 2 ||
        !BranchI->getOperand(0).isMBB() || !BranchCC ||
        *BranchCC != BedrockCC::LE ||
        nextNonDebug(BranchI, *Header) != Header->end())
      continue;

    Register CountReg = TestI->getOperand(0).getReg();
    if (!isDReg(CountReg) || regsOverlap(TRI, CountReg, Bedrock::D0))
      continue;

    MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();
    if (!Exit || !isPlainRetBlock(*Exit))
      continue;

    MachineBasicBlock::iterator MaxI = Preheader.getLastNonDebugInstr();
    if (MaxI == Preheader.end() || MaxI->getOpcode() != Bedrock::MAXS32rr ||
        MaxI->getNumOperands() < 3 || !MaxI->getOperand(0).isReg() ||
        !MaxI->getOperand(1).isReg() || !MaxI->getOperand(2).isReg() ||
        !regsOverlap(TRI, MaxI->getOperand(0).getReg(), Bedrock::D0))
      continue;
    bool MaxZeroCount =
        regsOverlap(TRI, MaxI->getOperand(1).getReg(), Bedrock::D0) &&
        regsOverlap(TRI, MaxI->getOperand(2).getReg(), CountReg);
    bool MaxCountZero =
        regsOverlap(TRI, MaxI->getOperand(1).getReg(), CountReg) &&
        regsOverlap(TRI, MaxI->getOperand(2).getReg(), Bedrock::D0);
    if (!MaxZeroCount && !MaxCountZero)
      continue;

    MachineBasicBlock::iterator ZeroI = prevNonDebug(MaxI, Preheader);
    Register ZeroReg;
    if (ZeroI == Preheader.end() || !isClrReg(*ZeroI, ZeroReg) ||
        !regsOverlap(TRI, ZeroReg, Bedrock::D0))
      continue;

    MachineBasicBlock::iterator CopyI = prevNonDebug(ZeroI, Preheader);
    if (CopyI == Preheader.end() ||
        (CopyI->getOpcode() != Bedrock::MOV32rr &&
         CopyI->getOpcode() != Bedrock::MOV64rr) ||
        CopyI->getNumOperands() < 2 || !CopyI->getOperand(0).isReg() ||
        !CopyI->getOperand(1).isReg() ||
        !regsOverlap(TRI, CopyI->getOperand(0).getReg(), CountReg) ||
        !regsOverlap(TRI, CopyI->getOperand(1).getReg(), Bedrock::D0))
      continue;

    SmallPtrSet<MachineBasicBlock *, 16> LoopBlocks;
    if (!collectLoopBlocksUntilExit(*Header, *Exit, LoopBlocks))
      continue;
    bool TouchesRetReg = false;
    for (MachineBasicBlock *MBB : LoopBlocks) {
      for (MachineInstr &MI : *MBB) {
        if (MI.isDebugInstr())
          continue;
        if (instrTouchesReg(MI, Bedrock::D0, TRI)) {
          TouchesRetReg = true;
          break;
        }
      }
      if (TouchesRetReg)
        break;
    }
    if (TouchesRetReg)
      continue;

    CopyI->setDesc(TII.get(Bedrock::MOV32rr));
    MachineBasicBlock *ZeroExit =
        createZeroReturnBlock(MF, *Exit, BranchI->getDebugLoc(), TII);
    BranchI->getOperand(0).setMBB(ZeroExit);
    Header->replaceSuccessor(Exit, ZeroExit);
    MaxI->eraseFromParent();
    ZeroI->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldSMaxIndexLoopBound(MachineFunction &MF) const {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &Preheader : MF) {
    for (auto MaxI = Preheader.begin(); MaxI != Preheader.end();) {
      MachineInstr &Max = *MaxI++;
      if (Max.isDebugInstr() || Max.getOpcode() != Bedrock::MAXS32rr ||
          Max.getNumOperands() < 3 || !Max.getOperand(0).isReg() ||
          !Max.getOperand(1).isReg() || !Max.getOperand(2).isReg())
        continue;

      Register Bound = Max.getOperand(0).getReg();
      if (!isDReg(Bound) ||
          !regsOverlap(TRI, Bound, Max.getOperand(1).getReg()))
        continue;
      Register Zero = Max.getOperand(2).getReg();
      if (!findLastConstDefBefore(Max, Zero, 0, TRI))
        continue;

      MachineBasicBlock *Header = nullptr;
      for (MachineBasicBlock *Succ : Preheader.successors()) {
        Header = Succ;
        break;
      }
      if (!Header)
        continue;

      MachineBasicBlock::iterator CmpI = Header->begin();
      while (CmpI != Header->end() && CmpI->isDebugInstr())
        ++CmpI;
      MachineBasicBlock::iterator BranchI =
          CmpI == Header->end() ? Header->end() : nextNonDebug(CmpI, *Header);
      std::optional<int64_t> BranchCC =
          BranchI == Header->end() || BranchI->getNumOperands() < 2
              ? std::nullopt
              : getCondCodeImm(BranchI->getOperand(1));
      if (CmpI == Header->end() || CmpI->getOpcode() != Bedrock::CMP32rr ||
          CmpI->getNumOperands() < 2 || !CmpI->getOperand(0).isReg() ||
          !CmpI->getOperand(1).isReg() || BranchI == Header->end() ||
          BranchI->getOpcode() != Bedrock::JCC ||
          BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
          !BranchCC || *BranchCC != BedrockCC::EQ ||
          nextNonDebug(BranchI, *Header) != Header->end())
        continue;

      Register LHS = CmpI->getOperand(0).getReg();
      Register RHS = CmpI->getOperand(1).getReg();
      bool BoundIsLHS = regsOverlap(TRI, LHS, Bound);
      bool BoundIsRHS = regsOverlap(TRI, RHS, Bound);
      if (BoundIsLHS == BoundIsRHS)
        continue;
      Register Index = BoundIsLHS ? RHS : LHS;
      if (!isDReg(Index) || !findLastConstDefBefore(*CmpI, Index, 0, TRI))
        continue;

      MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();
      if (!Exit)
        continue;
      SmallPtrSet<MachineBasicBlock *, 16> LoopBlocks;
      if (!collectLoopBlocksUntilExit(*Header, *Exit, LoopBlocks))
        continue;

      bool BadBoundUse = false;
      bool SawIndexInc = false;
      for (MachineBasicBlock *MBB : LoopBlocks) {
        for (MachineInstr &MI : *MBB) {
          if (MI.isDebugInstr() || &MI == &*CmpI)
            continue;
          if (instrTouchesReg(MI, Bound, TRI)) {
            BadBoundUse = true;
            break;
          }
          if (isUnaryRegOp(MI, Bedrock::INC32r, Index, TRI)) {
            if (SawIndexInc) {
              BadBoundUse = true;
              break;
            }
            SawIndexInc = true;
          }
        }
        if (BadBoundUse)
          break;
      }
      if (BadBoundUse || !SawIndexInc)
        continue;

      setCondCodeImm(BranchI->getOperand(1),
                     BoundIsLHS ? BedrockCC::LE : BedrockCC::GE);
      Max.eraseFromParent();
      Changed = true;
      break;
    }
  }

  return Changed;
}

bool BedrockPeephole::foldCountedLoopIndexResult(
    MachineFunction &MF,
    const DenseMap<MachineBasicBlock *, uint32_t> &KnownZeroIns) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &Exit : MF) {
    MachineBasicBlock::iterator RetValueI = Exit.begin();
    while (RetValueI != Exit.end() && RetValueI->isDebugInstr())
      ++RetValueI;
    if (RetValueI == Exit.end())
      continue;

    bool HasRetExt = RetValueI->getOpcode() == Bedrock::EXTSQ32rr &&
                     RetValueI->getNumOperands() >= 2 &&
                     RetValueI->getOperand(0).isReg() &&
                     RetValueI->getOperand(1).isReg();
    bool HasRetCopy =
        (RetValueI->getOpcode() == Bedrock::MOV32rr ||
         RetValueI->getOpcode() == Bedrock::MOV64rr ||
         RetValueI->getOpcode() == Bedrock::TRUNC64to32) &&
        RetValueI->getNumOperands() >= 2 && RetValueI->getOperand(0).isReg() &&
        RetValueI->getOperand(1).isReg() &&
        regsOverlap(TRI, RetValueI->getOperand(0).getReg(), Bedrock::D0) &&
        nextNonDebug(RetValueI, Exit) != Exit.end() &&
        nextNonDebug(RetValueI, Exit)->getOpcode() == Bedrock::RET;
    bool HasDirectRet = RetValueI->getOpcode() == Bedrock::RET;
    if (!HasRetExt && !HasRetCopy && !HasDirectRet)
      continue;

    Register ResultTmp = (HasRetExt || HasRetCopy)
                             ? RetValueI->getOperand(1).getReg()
                             : Register(Bedrock::D0);
    SmallVector<ExitCopyInfo, 2> Copies;
    for (MachineBasicBlock *Pred : Exit.predecessors()) {
      MachineInstr *Branch = findJccTo(*Pred, Exit);
      if (!Branch)
        continue;
      Register CopySrc;
      MachineInstr *Copy = findCopyToBefore(*Branch, ResultTmp, CopySrc, TRI);
      if (!Copy)
        continue;
      Copies.push_back({Pred, Branch, Copy, CopySrc});
    }

    if (Copies.size() != 2)
      continue;

    ExitCopyInfo *HeaderInfo = nullptr;
    Register CountReg;
    for (ExitCopyInfo &Info : Copies) {
      Register CandidateCount;
      if (!findCountedLoopHeaderInfo(*Info.MBB, KnownZeroIns.lookup(Info.MBB),
                                     CandidateCount, TRI))
        continue;
      if (HeaderInfo)
        HeaderInfo = nullptr;
      else {
        HeaderInfo = &Info;
        CountReg = CandidateCount;
      }
    }
    if (!HeaderInfo)
      continue;

    ExitCopyInfo *BodyInfo = HeaderInfo == &Copies[0] ? &Copies[1] : &Copies[0];
    Register LimitReg = HeaderInfo->Source;
    Register IndexReg = BodyInfo->Source;
    if (!isDReg(CountReg) || !isDReg(IndexReg) || !isDReg(LimitReg))
      continue;

    MachineInstr *Dec = nullptr;
    MachineInstr *Inc = nullptr;
    if (!findCountedLoopLatch(*HeaderInfo->MBB, CountReg, IndexReg, Dec, Inc,
                              TRI))
      continue;

    MachineInstr *CountInit = nullptr;
    MachineInstr *IndexInit = nullptr;
    if (!validateCountAndIndexDefs(MF, CountReg, IndexReg, LimitReg, Dec, Inc,
                                   CountInit, IndexInit, TRI))
      continue;

    if (HasRetExt) {
      RetValueI->getOperand(1).setReg(IndexReg);
    } else if (HasRetCopy) {
      if (regsOverlap(TRI, IndexReg, Bedrock::D0)) {
        RetValueI->eraseFromParent();
      } else {
        RetValueI->setDesc(TII.get(Bedrock::MOV32rr));
        RetValueI->getOperand(0).setReg(Bedrock::D0);
        RetValueI->getOperand(1).setReg(IndexReg);
      }
    } else if (!regsOverlap(TRI, IndexReg, Bedrock::D0)) {
      BuildMI(Exit, RetValueI, RetValueI->getDebugLoc(),
              TII.get(Bedrock::MOV32rr), Bedrock::D0)
          .addReg(IndexReg);
    }
    if (!Exit.isLiveIn(IndexReg))
      Exit.addLiveIn(IndexReg);
    HeaderInfo->Copy->eraseFromParent();
    BodyInfo->Copy->eraseFromParent();
    Changed = true;
  }

  for (MachineBasicBlock &Exit : MF) {
    MachineBasicBlock::iterator RetValueI = Exit.begin();
    while (RetValueI != Exit.end() && RetValueI->isDebugInstr())
      ++RetValueI;
    if (RetValueI == Exit.end())
      continue;

    bool HasRetExt = RetValueI->getOpcode() == Bedrock::EXTSQ32rr &&
                     RetValueI->getNumOperands() >= 2 &&
                     RetValueI->getOperand(0).isReg() &&
                     RetValueI->getOperand(1).isReg();
    bool HasRetCopy =
        (RetValueI->getOpcode() == Bedrock::MOV32rr ||
         RetValueI->getOpcode() == Bedrock::MOV64rr ||
         RetValueI->getOpcode() == Bedrock::TRUNC64to32) &&
        RetValueI->getNumOperands() >= 2 && RetValueI->getOperand(0).isReg() &&
        RetValueI->getOperand(1).isReg() &&
        regsOverlap(TRI, RetValueI->getOperand(0).getReg(), Bedrock::D0) &&
        nextNonDebug(RetValueI, Exit) != Exit.end() &&
        nextNonDebug(RetValueI, Exit)->getOpcode() == Bedrock::RET;
    bool HasDirectRet = RetValueI->getOpcode() == Bedrock::RET;
    if (!HasRetExt && !HasRetCopy && !HasDirectRet)
      continue;

    Register ResultTmp = (HasRetExt || HasRetCopy)
                             ? RetValueI->getOperand(1).getReg()
                             : Register(Bedrock::D0);
    ExitCopyInfo HeaderInfo;
    Register CountReg;
    bool FoundHeader = false;
    for (MachineBasicBlock *Pred : Exit.predecessors()) {
      MachineInstr *Branch = findJccTo(*Pred, Exit);
      if (!Branch)
        continue;
      Register CopySrc;
      MachineInstr *Copy = findCopyToBefore(*Branch, ResultTmp, CopySrc, TRI);
      if (!Copy)
        continue;
      Register CandidateCount;
      if (!findCountedLoopHeaderInfo(*Pred, KnownZeroIns.lookup(Pred),
                                     CandidateCount, TRI))
        continue;
      if (FoundHeader) {
        FoundHeader = false;
        break;
      }
      HeaderInfo = {Pred, Branch, Copy, CopySrc};
      CountReg = CandidateCount;
      FoundHeader = true;
    }
    if (!FoundHeader)
      continue;

    ExitCopyInfo BodyInfo;
    bool FoundBody = false;
    for (MachineBasicBlock *Pred : Exit.predecessors()) {
      if (Pred == HeaderInfo.MBB)
        continue;
      MachineInstr *Branch = findJccTo(*Pred, *HeaderInfo.MBB);
      if (!Branch)
        continue;
      Register CopySrc;
      MachineInstr *Copy = findCopyToBefore(*Branch, ResultTmp, CopySrc, TRI);
      if (!Copy)
        continue;
      if (FoundBody) {
        FoundBody = false;
        break;
      }
      BodyInfo = {Pred, Branch, Copy, CopySrc};
      FoundBody = true;
    }
    if (!FoundBody)
      continue;

    Register LimitReg = HeaderInfo.Source;
    Register IndexReg = BodyInfo.Source;
    if (!isDReg(CountReg) || !isDReg(IndexReg) || !isDReg(LimitReg))
      continue;

    MachineInstr *Dec = nullptr;
    MachineInstr *Inc = nullptr;
    if (!findCountedLoopJccLatch(*HeaderInfo.MBB, CountReg, IndexReg, Dec, Inc,
                                 TRI))
      continue;

    MachineInstr *CountInit = nullptr;
    MachineInstr *IndexInit = nullptr;
    if (!validateCountAndIndexDefs(MF, CountReg, IndexReg, LimitReg, Dec, Inc,
                                   CountInit, IndexInit, TRI))
      continue;

    if (HasRetExt) {
      RetValueI->getOperand(1).setReg(IndexReg);
    } else if (HasRetCopy) {
      if (regsOverlap(TRI, IndexReg, Bedrock::D0)) {
        RetValueI->eraseFromParent();
      } else {
        RetValueI->setDesc(TII.get(Bedrock::MOV32rr));
        RetValueI->getOperand(0).setReg(Bedrock::D0);
        RetValueI->getOperand(1).setReg(IndexReg);
      }
    } else if (!regsOverlap(TRI, IndexReg, Bedrock::D0)) {
      BuildMI(Exit, RetValueI, RetValueI->getDebugLoc(),
              TII.get(Bedrock::MOV32rr), Bedrock::D0)
          .addReg(IndexReg);
    }
    if (!Exit.isLiveIn(IndexReg))
      Exit.addLiveIn(IndexReg);
    HeaderInfo.Copy->eraseFromParent();
    BodyInfo.Copy->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldNarrowLoopCountCopies(MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || MI.getOpcode() != Bedrock::MOV64rr ||
          MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
          !MI.getOperand(1).isReg())
        continue;
      Register Dst = MI.getOperand(0).getReg();
      Register Src = MI.getOperand(1).getReg();
      if (!isDReg(Dst) || !isDReg(Src) || regsOverlap(TRI, Dst, Src) ||
          !onlyUsedAsI32LoopCountAfter(MI, Dst, TRI))
        continue;
      MI.setDesc(TII.get(Bedrock::MOV32rr));
      Changed = true;
    }
  }

  return Changed;
}

bool BedrockPeephole::foldSelfZextI32LoopCounts(MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &MBB : MF) {
    for (auto ExtI = MBB.begin(); ExtI != MBB.end();) {
      MachineInstr &Ext = *ExtI++;
      if (Ext.isDebugInstr() || Ext.getOpcode() != Bedrock::EXTZQ32rr ||
          Ext.getNumOperands() < 2 || !Ext.getOperand(0).isReg() ||
          !Ext.getOperand(1).isReg())
        continue;

      Register Count = Ext.getOperand(0).getReg();
      if (!isDReg(Count) ||
          !regsOverlap(TRI, Count, Ext.getOperand(1).getReg()))
        continue;

      SmallVector<MachineInstr *, 4> Tests;
      SmallVector<MachineInstr *, 4> Decs;
      SmallVector<MachineInstr *, 4> DJccs;
      if (!collectSelfZextI32CountUses(Ext, Count, TRI, Tests, Decs, DJccs))
        continue;

      for (MachineInstr *Test : Tests)
        Test->setDesc(TII.get(Bedrock::TEST32rr));
      for (MachineInstr *Dec : Decs)
        Dec->setDesc(TII.get(Bedrock::DEC32r));
      for (MachineInstr *DJcc : DJccs)
        DJcc->setDesc(TII.get(Bedrock::DJCC32r));
      Ext.eraseFromParent();
      Changed = true;
    }
  }

  return Changed;
}

bool BedrockPeephole::foldMinSizeLoopCounter32(MachineFunction &MF) const {
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  bool Changed = false;
  for (MachineBasicBlock &ExtMBB : MF) {
    for (auto ExtI = ExtMBB.begin(); ExtI != ExtMBB.end();) {
      MachineInstr &Ext = *ExtI++;
      if (Ext.isDebugInstr() || Ext.getOpcode() != Bedrock::EXTZQ32rr ||
          Ext.getNumOperands() < 2 || !Ext.getOperand(0).isReg() ||
          !Ext.getOperand(1).isReg() ||
          !regsOverlap(TRI, Ext.getOperand(0).getReg(),
                       Ext.getOperand(1).getReg()))
        continue;

      Register Count = Ext.getOperand(0).getReg();
      if (!isDReg(Count))
        continue;

      MachineBasicBlock::iterator MaxI =
          prevNonDebug(Ext.getIterator(), ExtMBB);
      if (MaxI == ExtMBB.end() || MaxI->getOpcode() != Bedrock::MAXS32rr ||
          MaxI->getNumOperands() < 3 || !MaxI->getOperand(0).isReg() ||
          !MaxI->getOperand(2).isReg() ||
          !regsOverlap(TRI, MaxI->getOperand(0).getReg(), Count))
        continue;
      Register ZeroReg = MaxI->getOperand(2).getReg();
      if (!findLastConstDefBefore(*MaxI, ZeroReg, 0, TRI))
        continue;

      MachineInstr *Cmp = nullptr;
      MachineInstr *Branch = nullptr;
      Register Index;
      bool MultipleCompares = false;
      for (MachineBasicBlock &MBB : MF) {
        for (auto I = MBB.begin(); I != MBB.end(); ++I) {
          MachineInstr &MI = *I;
          if (MI.isDebugInstr() || MI.getOpcode() != Bedrock::CMP64rr ||
              MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
              !MI.getOperand(1).isReg() ||
              !regsOverlap(TRI, MI.getOperand(0).getReg(), Count))
            continue;
          auto J = nextNonDebug(I, MBB);
          if (J == MBB.end() || J->getOpcode() != Bedrock::JCC ||
              J->getNumOperands() < 2)
            continue;
          std::optional<int64_t> CC = getCondCodeImm(J->getOperand(1));
          if (!CC || *CC != BedrockCC::EQ)
            continue;
          if (Cmp) {
            MultipleCompares = true;
            break;
          }
          Cmp = &MI;
          Branch = &*J;
          Index = MI.getOperand(1).getReg();
        }
        if (MultipleCompares)
          break;
      }
      if (!Cmp || !Branch || MultipleCompares || !isDReg(Index))
        continue;

      MachineInstr *IndexZeroDef = findLastConstDefBefore(*Cmp, Index, 0, TRI);
      if (!IndexZeroDef)
        continue;

      MachineInstr *IndexInc = nullptr;
      bool BadIndexDef = false;
      for (MachineBasicBlock &MBB : MF) {
        for (MachineInstr &MI : MBB) {
          if (MI.isDebugInstr() || !instrDefinesReg(MI, Index, TRI))
            continue;
          if (&MI == IndexZeroDef)
            continue;
          if (MI.getOpcode() == Bedrock::INC64r && MI.getNumOperands() >= 2 &&
              MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
              regsOverlap(TRI, MI.getOperand(0).getReg(), Index) &&
              regsOverlap(TRI, MI.getOperand(1).getReg(), Index) && !IndexInc) {
            IndexInc = &MI;
            continue;
          }
          BadIndexDef = true;
          break;
        }
        if (BadIndexDef)
          break;
      }
      if (BadIndexDef || !IndexInc)
        continue;

      bool BadCountUse = false;
      bool PastExt = false;
      for (MachineBasicBlock &MBB : MF) {
        for (MachineInstr &MI : MBB) {
          if (!PastExt) {
            if (&MI == &Ext)
              PastExt = true;
            continue;
          }
          if (MI.isDebugInstr())
            continue;
          if (instrUsesReg(MI, Count, TRI) && &MI != Cmp) {
            BadCountUse = true;
            break;
          }
          if (instrDefinesReg(MI, Count, TRI) && &MI != &Ext) {
            BadCountUse = true;
            break;
          }
        }
        if (BadCountUse)
          break;
      }
      if (BadCountUse || !regUnusedAfterInstrInLayout(Ext, Bedrock::FLAGS, TRI))
        continue;

      MachineInstr *CountCopy = findLastDefBefore(*MaxI, Count, TRI);
      if (CountCopy && CountCopy->getOpcode() == Bedrock::MOV64rr &&
          CountCopy->getNumOperands() >= 2 &&
          CountCopy->getOperand(0).isReg() &&
          regsOverlap(TRI, CountCopy->getOperand(0).getReg(), Count) &&
          onlyUsesRegAsI32Before(*CountCopy, *MaxI, Count, TRI))
        CountCopy->setDesc(TII.get(Bedrock::MOV32rr));

      Cmp->setDesc(TII.get(Bedrock::CMP32rr));
      IndexInc->setDesc(TII.get(Bedrock::INC32r));
      Ext.eraseFromParent();
      Changed = true;
      break;
    }
  }

  return Changed;
}

bool BedrockPeephole::foldLoopCarriedLoadUpdateCopies(
    MachineBasicBlock &MBB, MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  auto IsUpdateOf = [&](const MachineInstr &MI, Register Tmp) {
    if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
        !MI.getOperand(1).isReg())
      return false;
    if (!regsOverlap(TRI, MI.getOperand(0).getReg(), Tmp) ||
        !regsOverlap(TRI, MI.getOperand(1).getReg(), Tmp))
      return false;
    return MI.getOpcode() == Bedrock::ADD32rr ||
           MI.getOpcode() == Bedrock::ADD32ar;
  };

  for (auto CopyI = MBB.begin(); CopyI != MBB.end();) {
    MachineInstr &Copy = *CopyI;
    if (Copy.isDebugInstr() ||
        (Copy.getOpcode() != Bedrock::MOV32rr &&
         Copy.getOpcode() != Bedrock::MOV64rr) ||
        Copy.getNumOperands() < 2 || !Copy.getOperand(0).isReg() ||
        !Copy.getOperand(1).isReg()) {
      ++CopyI;
      continue;
    }

    Register Dest = Copy.getOperand(0).getReg();
    Register Tmp = Copy.getOperand(1).getReg();
    if (!isDReg(Dest) || regsOverlap(TRI, Dest, Tmp) ||
        (!operandIsKill(Copy, Tmp, TRI) &&
         !regDeadAfter(std::next(CopyI), MBB, Tmp, TRI))) {
      ++CopyI;
      continue;
    }

    MachineInstr *Load = nullptr;
    MachineInstr *Add = nullptr;
    for (auto Scan = MBB.begin(); Scan != CopyI;
         Scan = nextNonDebug(Scan, MBB)) {
      if (Scan->isDebugInstr())
        continue;
      if (Scan->getOpcode() != Bedrock::MOV32postrm ||
          Scan->getNumOperands() < 2 || !Scan->getOperand(0).isReg() ||
          !regsOverlap(TRI, Scan->getOperand(0).getReg(), Tmp))
        continue;

      auto AddI = nextNonDebug(Scan, MBB);
      if (AddI == MBB.end() || AddI == CopyI)
        continue;
      if ((AddI->getOpcode() != Bedrock::ADD32rr &&
           AddI->getOpcode() != Bedrock::ADD32ar) ||
          AddI->getNumOperands() < 3 || !AddI->getOperand(0).isReg() ||
          !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg() ||
          !regsOverlap(TRI, AddI->getOperand(0).getReg(), Tmp) ||
          !regsOverlap(TRI, AddI->getOperand(1).getReg(), Tmp) ||
          !regsOverlap(TRI, AddI->getOperand(2).getReg(), Dest))
        continue;

      Load = &*Scan;
      Add = &*AddI;
    }
    if (!Load || !Add) {
      ++CopyI;
      continue;
    }

    bool Safe = true;
    for (auto Scan = nextNonDebug(Add->getIterator(), MBB); Scan != CopyI;
         Scan = nextNonDebug(Scan, MBB)) {
      if (Scan->isDebugInstr())
        continue;
      if (instrTouchesReg(*Scan, Dest, TRI)) {
        Safe = false;
        break;
      }
      if (!instrTouchesReg(*Scan, Tmp, TRI))
        continue;
      if (instrDefinesReg(*Scan, Tmp, TRI) && !IsUpdateOf(*Scan, Tmp)) {
        Safe = false;
        break;
      }
    }
    if (!Safe) {
      ++CopyI;
      continue;
    }

    Register Base = Load->getOperand(1).getReg();
    BuildMI(MBB, Load->getIterator(), Add->getDebugLoc(),
            TII.get(Bedrock::ADD32postrm), Dest)
        .addReg(Dest)
        .addReg(Base);
    for (auto Scan = nextNonDebug(Add->getIterator(), MBB); Scan != CopyI;
         Scan = nextNonDebug(Scan, MBB)) {
      if (Scan->isDebugInstr())
        continue;
      if (Scan->getOpcode() == Bedrock::ADD32ar &&
          Scan->getNumOperands() >= 3 && Scan->getOperand(0).isReg() &&
          regsOverlap(TRI, Scan->getOperand(0).getReg(), Tmp))
        Scan->setDesc(TII.get(Bedrock::ADD32rr));
      for (MachineOperand &MO : Scan->operands())
        if (MO.isReg() && regsOverlap(TRI, MO.getReg(), Tmp))
          MO.setReg(Dest);
    }

    Load->eraseFromParent();
    Add->eraseFromParent();
    Copy.eraseFromParent();
    removeRegLiveInsWithoutUses(MF, Tmp, TRI);
    Changed = true;
    CopyI = MBB.begin();
  }

  return Changed;
}

bool BedrockPeephole::foldLoopCarriedLoadAddCopies(
    MachineBasicBlock &MBB, MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto CopyI = MBB.begin(); CopyI != MBB.end();) {
    MachineInstr &Copy = *CopyI;
    if (Copy.isDebugInstr() ||
        (Copy.getOpcode() != Bedrock::MOV32rr &&
         Copy.getOpcode() != Bedrock::MOV64rr) ||
        Copy.getNumOperands() < 2 || !Copy.getOperand(0).isReg() ||
        !Copy.getOperand(1).isReg()) {
      ++CopyI;
      continue;
    }

    Register Tmp = Copy.getOperand(0).getReg();
    Register Acc = Copy.getOperand(1).getReg();
    if (!isDReg(Tmp) || !isDReg(Acc) || regsOverlap(TRI, Tmp, Acc)) {
      ++CopyI;
      continue;
    }

    auto BiasAddI = nextNonDebug(CopyI, MBB);
    auto LoadI =
        BiasAddI == MBB.end() ? MBB.end() : nextNonDebug(BiasAddI, MBB);
    auto FinalAddI = LoadI == MBB.end() ? MBB.end() : nextNonDebug(LoadI, MBB);
    if (BiasAddI == MBB.end() || LoadI == MBB.end() || FinalAddI == MBB.end()) {
      ++CopyI;
      continue;
    }

    if (BiasAddI->getOpcode() != Bedrock::ADD32rr ||
        BiasAddI->getNumOperands() < 3 || !BiasAddI->getOperand(0).isReg() ||
        !BiasAddI->getOperand(1).isReg() || !BiasAddI->getOperand(2).isReg() ||
        !regsOverlap(TRI, BiasAddI->getOperand(0).getReg(), Tmp) ||
        !regsOverlap(TRI, BiasAddI->getOperand(1).getReg(), Tmp)) {
      ++CopyI;
      continue;
    }
    Register Bias = BiasAddI->getOperand(2).getReg();
    if (!isIntReg(Bias) || regsOverlap(TRI, Bias, Tmp) ||
        regsOverlap(TRI, Bias, Acc)) {
      ++CopyI;
      continue;
    }

    if (LoadI->getOpcode() != Bedrock::MOV32postrm ||
        LoadI->getNumOperands() < 2 || !LoadI->getOperand(0).isReg() ||
        !LoadI->getOperand(1).isReg() ||
        !regsOverlap(TRI, LoadI->getOperand(0).getReg(), Acc) ||
        hasOrderedMemOperand(*LoadI)) {
      ++CopyI;
      continue;
    }
    Register Base = LoadI->getOperand(1).getReg();
    if (!isAReg(Base)) {
      ++CopyI;
      continue;
    }

    if (FinalAddI->getOpcode() != Bedrock::ADD32rr ||
        FinalAddI->getNumOperands() < 3 || !FinalAddI->getOperand(0).isReg() ||
        !FinalAddI->getOperand(1).isReg() ||
        !FinalAddI->getOperand(2).isReg() ||
        !regsOverlap(TRI, FinalAddI->getOperand(0).getReg(), Acc) ||
        !regsOverlap(TRI, FinalAddI->getOperand(1).getReg(), Acc) ||
        !regsOverlap(TRI, FinalAddI->getOperand(2).getReg(), Tmp)) {
      ++CopyI;
      continue;
    }

    if (!operandIsKill(*FinalAddI, Tmp, TRI) &&
        !regDeadAfterInCFG(std::next(FinalAddI), MBB, Tmp, TRI)) {
      ++CopyI;
      continue;
    }

    MachineInstrBuilder LoadAdd = BuildMI(MBB, CopyI, LoadI->getDebugLoc(),
                                          TII.get(Bedrock::ADD32postrm), Acc)
                                      .addReg(Acc)
                                      .addReg(Base);
    LoadAdd.cloneMemRefs(*LoadI);
    BuildMI(MBB, CopyI, BiasAddI->getDebugLoc(), TII.get(Bedrock::ADD32rr), Acc)
        .addReg(Acc)
        .addReg(Bias);

    Copy.eraseFromParent();
    BiasAddI->eraseFromParent();
    LoadI->eraseFromParent();
    FinalAddI->eraseFromParent();
    Changed = true;
    CopyI = MBB.begin();
  }

  return Changed;
}

bool BedrockPeephole::foldAccumulatorLoopUseInputCount(
    MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &Header : MF) {
    MachineBasicBlock::iterator TestI = Header.begin();
    while (TestI != Header.end() && TestI->isDebugInstr())
      ++TestI;
    if (TestI == Header.end() || TestI->getOpcode() != Bedrock::TEST32rr ||
        TestI->getNumOperands() < 2 || !TestI->getOperand(0).isReg() ||
        !TestI->getOperand(1).isReg())
      continue;

    Register CountReg = TestI->getOperand(0).getReg();
    if (!isDReg(CountReg) ||
        !regsOverlap(TRI, CountReg, TestI->getOperand(1).getReg()) ||
        regsOverlap(TRI, CountReg, Bedrock::D0))
      continue;

    MachineBasicBlock::iterator BranchI = nextNonDebug(TestI, Header);
    std::optional<int64_t> BranchCC =
        BranchI == Header.end() || BranchI->getNumOperands() < 2
            ? std::nullopt
            : getCondCodeImm(BranchI->getOperand(1));
    if (BranchI == Header.end() || BranchI->getOpcode() != Bedrock::JCC ||
        BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
        !BranchCC || *BranchCC != BedrockCC::LE ||
        nextNonDebug(BranchI, Header) != Header.end())
      continue;
    MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();
    if (!Exit || !isPlainRetBlock(*Exit))
      continue;

    MachineFunction::iterator BodyI = std::next(Header.getIterator());
    if (BodyI == MF.end())
      continue;
    MachineBasicBlock &Body = *BodyI;
    if (!Header.isSuccessor(&Body) || !Header.isSuccessor(Exit))
      continue;

    MachineBasicBlock::iterator LatchI = Body.getLastNonDebugInstr();
    std::optional<int64_t> LatchCC =
        LatchI == Body.end() || LatchI->getNumOperands() < 2
            ? std::nullopt
            : getCondCodeImm(LatchI->getOperand(1));
    if (LatchI == Body.end() || LatchI->getOpcode() != Bedrock::JCC ||
        LatchI->getNumOperands() < 2 || !LatchI->getOperand(0).isMBB() ||
        LatchI->getOperand(0).getMBB() != &Body || !LatchCC ||
        *LatchCC != BedrockCC::NE)
      continue;

    MachineBasicBlock::iterator DecI = prevNonDebug(LatchI, Body);
    if (DecI == Body.end() ||
        !isUnaryRegOp(*DecI, Bedrock::DEC32r, CountReg, TRI))
      continue;

    if (Header.pred_size() != 1)
      continue;
    MachineBasicBlock *Preheader = *Header.pred_begin();
    if (Preheader == &Header || Preheader == &Body)
      continue;

    MachineInstr *CountCopy = nullptr;
    MachineInstr *AccClr = nullptr;
    for (MachineInstr &MI : *Preheader) {
      if (MI.isDebugInstr())
        continue;
      if (!CountCopy) {
        if ((MI.getOpcode() == Bedrock::MOV32rr ||
             MI.getOpcode() == Bedrock::MOV64rr) &&
            MI.getNumOperands() >= 2 && MI.getOperand(0).isReg() &&
            MI.getOperand(1).isReg() &&
            regsOverlap(TRI, MI.getOperand(0).getReg(), CountReg) &&
            regsOverlap(TRI, MI.getOperand(1).getReg(), Bedrock::D0)) {
          CountCopy = &MI;
          continue;
        }
        continue;
      }

      Register ClrReg;
      if (isClrReg(MI, ClrReg) && regsOverlap(TRI, ClrReg, Bedrock::D0)) {
        AccClr = &MI;
        break;
      }

      if (instrTouchesReg(MI, CountReg, TRI) ||
          instrTouchesReg(MI, Bedrock::D0, TRI)) {
        AccClr = nullptr;
        break;
      }
    }
    if (!CountCopy || !AccClr)
      continue;

    bool Unsafe = false;
    for (MachineInstr &MI : Header) {
      if (MI.isDebugInstr() || &MI == &*TestI)
        continue;
      if (instrTouchesReg(MI, CountReg, TRI) ||
          instrTouchesReg(MI, Bedrock::D0, TRI)) {
        Unsafe = true;
        break;
      }
    }
    if (Unsafe)
      continue;

    for (MachineInstr &MI : Body) {
      if (MI.isDebugInstr() || &MI == &*DecI)
        continue;
      if (instrTouchesReg(MI, CountReg, TRI)) {
        Unsafe = true;
        break;
      }
      if (instrTouchesReg(MI, Bedrock::D0, TRI) &&
          !canRewriteExplicitRegTouches(MI, Bedrock::D0, TRI)) {
        Unsafe = true;
        break;
      }
    }
    if (Unsafe)
      continue;

    for (MachineInstr &MI : *Exit) {
      if (MI.isDebugInstr() || MI.getOpcode() == Bedrock::RET)
        continue;
      if (instrTouchesReg(MI, CountReg, TRI) ||
          instrTouchesReg(MI, Bedrock::D0, TRI)) {
        Unsafe = true;
        break;
      }
    }
    if (Unsafe)
      continue;

    AccClr->getOperand(0).setReg(CountReg);
    TestI->getOperand(0).setReg(Bedrock::D0);
    TestI->getOperand(1).setReg(Bedrock::D0);
    DecI->getOperand(0).setReg(Bedrock::D0);
    DecI->getOperand(1).setReg(Bedrock::D0);

    for (MachineInstr &MI : Body) {
      if (MI.isDebugInstr() || &MI == &*DecI)
        continue;
      replaceRegOperandUsesAndDefs(MI, Bedrock::D0, CountReg, TRI);
    }

    MachineBasicBlock::iterator RetI = Exit->begin();
    while (RetI != Exit->end() && RetI->isDebugInstr())
      ++RetI;
    BuildMI(*Exit, RetI, RetI->getDebugLoc(), TII.get(Bedrock::MOV32rr),
            Bedrock::D0)
        .addReg(CountReg);

    CountCopy->eraseFromParent();
    if (!Header.isLiveIn(Bedrock::D0))
      Header.addLiveIn(Bedrock::D0);
    if (!Body.isLiveIn(Bedrock::D0))
      Body.addLiveIn(Bedrock::D0);
    if (!Body.isLiveIn(CountReg))
      Body.addLiveIn(CountReg);
    if (!Exit->isLiveIn(CountReg))
      Exit->addLiveIn(CountReg);

    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldPositiveConstRepPretests(
    MachineFunction &MF) const {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  SmallVector<MachineBasicBlock *, 8> Blocks;
  for (MachineBasicBlock &MBB : MF)
    Blocks.push_back(&MBB);

  for (MachineBasicBlock *Header : Blocks) {
    if (!Header || Header->getParent() != &MF)
      continue;

    MachineBasicBlock::iterator BranchI = Header->getLastNonDebugInstr();
    if (BranchI == Header->end() || BranchI->getOpcode() != Bedrock::JCC ||
        BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB())
      continue;
    std::optional<int64_t> BranchCC = getCondCodeImm(BranchI->getOperand(1));
    if (!BranchCC || *BranchCC != BedrockCC::LE)
      continue;

    MachineBasicBlock::iterator TestI = prevNonDebug(BranchI, *Header);
    if (TestI == Header->end() ||
        (TestI->getOpcode() != Bedrock::TEST32rr &&
         TestI->getOpcode() != Bedrock::TEST64rr) ||
        TestI->getNumOperands() < 2 || !TestI->getOperand(0).isReg() ||
        !TestI->getOperand(1).isReg() ||
        !regsOverlap(TRI, TestI->getOperand(0).getReg(),
                     TestI->getOperand(1).getReg()))
      continue;
    Register CountReg = TestI->getOperand(0).getReg();

    MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();
    if (!Exit || Exit == Header)
      continue;

    MachineBasicBlock *Body = nullptr;
    bool MultipleBodies = false;
    for (MachineBasicBlock *Succ : Header->successors()) {
      if (Succ == Exit)
        continue;
      if (Body) {
        MultipleBodies = true;
        break;
      }
      Body = Succ;
    }
    if (MultipleBodies || !Body || !Header->isSuccessor(Body))
      continue;

    bool HasPositiveConst =
        findPositiveConstDefInBlockBefore(*Header, TestI, CountReg, TRI);
    if (!HasPositiveConst) {
      MachineBasicBlock *Preheader = findSingleNonSelfPredecessor(*Header);
      if (!Preheader || Preheader == Header || Preheader == Body ||
          Preheader == Exit || !Preheader->isSuccessor(Header) ||
          Preheader->succ_size() != 1)
        continue;
      HasPositiveConst = findPositiveConstDefInBlockBefore(
          *Preheader, Preheader->end(), CountReg, TRI);
    }
    if (!HasPositiveConst)
      continue;

    Header->removeSuccessor(Exit);
    TestI->eraseFromParent();
    BranchI->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldAscendingStoreProgressionLoops(
    MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  SmallVector<MachineBasicBlock *, 8> Headers;
  for (MachineBasicBlock &MBB : MF)
    Headers.push_back(&MBB);

  for (MachineBasicBlock *Header : Headers) {
    if (!Header || Header->getParent() != &MF)
      continue;

    MachineBasicBlock::iterator CmpI = Header->begin();
    while (CmpI != Header->end() && CmpI->isDebugInstr())
      ++CmpI;
    if (CmpI == Header->end() ||
        (CmpI->getOpcode() != Bedrock::CMP64rr &&
         CmpI->getOpcode() != Bedrock::CMP32rr &&
         CmpI->getOpcode() != Bedrock::CMP64ri &&
         CmpI->getOpcode() != Bedrock::CMP32ri) ||
        CmpI->getNumOperands() < 2 || !CmpI->getOperand(0).isReg())
      continue;

    MachineBasicBlock::iterator BranchI = nextNonDebug(CmpI, *Header);
    std::optional<int64_t> BranchCC =
        BranchI == Header->end() || BranchI->getNumOperands() < 2
            ? std::nullopt
            : getCondCodeImm(BranchI->getOperand(1));
    if (BranchI == Header->end() || BranchI->getOpcode() != Bedrock::JCC ||
        BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
        !BranchCC || *BranchCC != BedrockCC::EQ ||
        nextNonDebug(BranchI, *Header) != Header->end())
      continue;

    MachineFunction::iterator BodyIt = std::next(Header->getIterator());
    if (BodyIt == MF.end())
      continue;
    MachineBasicBlock &Body = *BodyIt;
    MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();
    if (!Exit || !Header->isSuccessor(&Body) || !Header->isSuccessor(Exit) ||
        Body.pred_size() != 1 || !Body.isPredecessor(Header))
      continue;

    MachineBasicBlock::iterator StoreI = Body.begin();
    while (StoreI != Body.end() && StoreI->isDebugInstr())
      ++StoreI;
    MachineBasicBlock::iterator FirstAddI =
        StoreI == Body.end() ? Body.end() : nextNonDebug(StoreI, Body);
    MachineBasicBlock::iterator SecondAddI =
        FirstAddI == Body.end() ? Body.end() : nextNonDebug(FirstAddI, Body);
    MachineBasicBlock::iterator JmpI =
        SecondAddI == Body.end() ? Body.end() : nextNonDebug(SecondAddI, Body);
    if (StoreI == Body.end() || FirstAddI == Body.end() ||
        SecondAddI == Body.end() || JmpI == Body.end() ||
        nextNonDebug(JmpI, Body) != Body.end() ||
        StoreI->getOpcode() != Bedrock::MOV32idx1mr ||
        StoreI->getNumOperands() < 4 || !StoreI->getOperand(0).isReg() ||
        !StoreI->getOperand(1).isReg() || !StoreI->getOperand(2).isReg() ||
        !StoreI->getOperand(3).isImm() || JmpI->getOpcode() != Bedrock::JMP ||
        JmpI->getNumOperands() < 1 || !JmpI->getOperand(0).isMBB() ||
        JmpI->getOperand(0).getMBB() != Header)
      continue;

    Register ValueReg = StoreI->getOperand(0).getReg();
    Register BaseReg = StoreI->getOperand(1).getReg();
    Register IndexReg = StoreI->getOperand(2).getReg();
    int64_t StoreOffset = StoreI->getOperand(3).getImm();
    if (!isDReg(ValueReg) || !isDReg(IndexReg))
      continue;

    auto IsValueAdd = [&](MachineInstr &MI) {
      return MI.getOpcode() == Bedrock::ADD32ri && MI.getNumOperands() >= 3 &&
             MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
             MI.getOperand(2).isImm() &&
             regsOverlap(TRI, MI.getOperand(0).getReg(), ValueReg) &&
             regsOverlap(TRI, MI.getOperand(1).getReg(), ValueReg);
    };
    auto IsIndexAdd = [&](MachineInstr &MI) {
      return MI.getOpcode() == Bedrock::ADD64ri && MI.getNumOperands() >= 3 &&
             MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
             MI.getOperand(2).isImm() && MI.getOperand(2).getImm() == 4 &&
             regsOverlap(TRI, MI.getOperand(0).getReg(), IndexReg) &&
             regsOverlap(TRI, MI.getOperand(1).getReg(), IndexReg);
    };

    MachineBasicBlock::iterator AddValueI = Body.end();
    MachineBasicBlock::iterator AddIndexI = Body.end();
    if (IsValueAdd(*FirstAddI) && IsIndexAdd(*SecondAddI)) {
      AddValueI = FirstAddI;
      AddIndexI = SecondAddI;
    } else if (IsIndexAdd(*FirstAddI) && IsValueAdd(*SecondAddI)) {
      AddIndexI = FirstAddI;
      AddValueI = SecondAddI;
    } else {
      continue;
    }

    int64_t Start = 0;
    int64_t Limit = 0;
    bool CmpImm = CmpI->getOpcode() == Bedrock::CMP64ri ||
                  CmpI->getOpcode() == Bedrock::CMP32ri;
    if (CmpImm) {
      if (!CmpI->getOperand(1).isImm() ||
          !regsOverlap(TRI, CmpI->getOperand(0).getReg(), IndexReg))
        continue;
      Limit = CmpI->getOperand(1).getImm();
    } else {
      if (!CmpI->getOperand(1).isReg())
        continue;
      bool IndexIsLHS =
          regsOverlap(TRI, CmpI->getOperand(0).getReg(), IndexReg);
      bool IndexIsRHS =
          regsOverlap(TRI, CmpI->getOperand(1).getReg(), IndexReg);
      if (IndexIsLHS == IndexIsRHS)
        continue;
      Register LimitReg = IndexIsLHS ? CmpI->getOperand(1).getReg()
                                     : CmpI->getOperand(0).getReg();
      if (!findLastConstDefBeforeAny(*CmpI, LimitReg, Limit, TRI))
        continue;
    }
    if (!findLastConstDefBeforeAny(*CmpI, IndexReg, Start, TRI) ||
        Limit <= Start || ((Limit - Start) % 4) != 0 ||
        (Limit - Start) / 4 > std::numeric_limits<int32_t>::max())
      continue;

    SmallPtrSet<MachineBasicBlock *, 8> Visiting;
    if (!regDeadFromBlockStartInCFG(*Exit, IndexReg, TRI, Visiting))
      continue;

    Register PtrReg =
        findScratchARegForLoop(*Header, Body, *Exit, TRI, BaseReg, ValueReg);
    if (!PtrReg)
      continue;

    int64_t Count = (Limit - Start) / 4;
    int64_t PointerOffset = StoreOffset + Start;
    DebugLoc DL = StoreI->getDebugLoc();

    BuildMI(*Header, CmpI, DL, TII.get(Bedrock::LEAri), PtrReg)
        .addReg(BaseReg)
        .addImm(PointerOffset);
    BuildMI(*Header, CmpI, DL, TII.get(Bedrock::MOV32ri), IndexReg)
        .addImm(Count);
    CmpI->eraseFromParent();
    BranchI->eraseFromParent();
    Header->removeSuccessor(Exit);

    MachineInstrBuilder Store =
        BuildMI(Body, StoreI, DL, TII.get(Bedrock::MOV32postmr))
            .addReg(ValueReg)
            .addReg(PtrReg);
    Store.cloneMemRefs(*StoreI);
    StoreI->eraseFromParent();

    BuildMI(Body, JmpI, AddIndexI->getDebugLoc(), TII.get(Bedrock::DEC32r),
            IndexReg)
        .addReg(IndexReg);
    AddIndexI->eraseFromParent();

    BuildMI(Body, JmpI, JmpI->getDebugLoc(), TII.get(Bedrock::JCC))
        .addMBB(&Body)
        .addImm(BedrockCC::NE);
    JmpI->eraseFromParent();

    Body.removeSuccessor(Header);
    if (!Body.isSuccessor(&Body))
      Body.addSuccessor(&Body);
    if (!Body.isSuccessor(Exit))
      Body.addSuccessor(Exit);

    if (!Header->isLiveIn(IndexReg))
      Header->addLiveIn(IndexReg);
    if (!Header->isLiveIn(PtrReg))
      Header->addLiveIn(PtrReg);
    if (!Body.isLiveIn(IndexReg))
      Body.addLiveIn(IndexReg);
    if (!Body.isLiveIn(PtrReg))
      Body.addLiveIn(PtrReg);
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldAscendingAddressStoreLoops(
    MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  SmallVector<MachineBasicBlock *, 8> Headers;
  for (MachineBasicBlock &MBB : MF)
    Headers.push_back(&MBB);

  for (MachineBasicBlock *Header : Headers) {
    if (!Header || Header->getParent() != &MF)
      continue;

    MachineBasicBlock::iterator CmpI = Header->begin();
    while (CmpI != Header->end() && CmpI->isDebugInstr())
      ++CmpI;
    if (CmpI == Header->end() ||
        (CmpI->getOpcode() != Bedrock::CMP64rr &&
         CmpI->getOpcode() != Bedrock::CMP32rr &&
         CmpI->getOpcode() != Bedrock::CMP64ri &&
         CmpI->getOpcode() != Bedrock::CMP32ri) ||
        CmpI->getNumOperands() < 2 || !CmpI->getOperand(0).isReg())
      continue;

    MachineBasicBlock::iterator BranchI = nextNonDebug(CmpI, *Header);
    std::optional<int64_t> BranchCC =
        BranchI == Header->end() || BranchI->getNumOperands() < 2
            ? std::nullopt
            : getCondCodeImm(BranchI->getOperand(1));
    if (BranchI == Header->end() || BranchI->getOpcode() != Bedrock::JCC ||
        BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
        !BranchCC || *BranchCC != BedrockCC::EQ ||
        nextNonDebug(BranchI, *Header) != Header->end())
      continue;

    MachineFunction::iterator BodyIt = std::next(Header->getIterator());
    if (BodyIt == MF.end())
      continue;
    MachineBasicBlock &Body = *BodyIt;
    MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();
    if (!Exit || !Header->isSuccessor(&Body) || !Header->isSuccessor(Exit) ||
        Body.pred_size() != 1 || !Body.isPredecessor(Header))
      continue;

    MachineBasicBlock::iterator LeaI = Body.begin();
    while (LeaI != Body.end() && LeaI->isDebugInstr())
      ++LeaI;
    MachineBasicBlock::iterator AddAddrI =
        LeaI == Body.end() ? Body.end() : nextNonDebug(LeaI, Body);
    if (LeaI == Body.end() || AddAddrI == Body.end() ||
        ((LeaI->getOpcode() != Bedrock::LEAri || LeaI->getNumOperands() < 3 ||
          !LeaI->getOperand(0).isReg() || !LeaI->getOperand(1).isReg() ||
          !LeaI->getOperand(2).isImm()) &&
         (LeaI->getOpcode() != Bedrock::MOV64rr || LeaI->getNumOperands() < 2 ||
          !LeaI->getOperand(0).isReg() || !LeaI->getOperand(1).isReg())) ||
        AddAddrI->getOpcode() != Bedrock::ADD64rr ||
        AddAddrI->getNumOperands() < 3 || !AddAddrI->getOperand(0).isReg() ||
        !AddAddrI->getOperand(1).isReg() || !AddAddrI->getOperand(2).isReg())
      continue;

    Register AddrReg = LeaI->getOperand(0).getReg();
    Register BaseReg = LeaI->getOperand(1).getReg();
    int64_t BaseOffset =
        LeaI->getOpcode() == Bedrock::LEAri ? LeaI->getOperand(2).getImm() : 0;
    if (!isAReg(AddrReg) ||
        !regsOverlap(TRI, AddAddrI->getOperand(0).getReg(), AddrReg) ||
        !regsOverlap(TRI, AddAddrI->getOperand(1).getReg(), AddrReg))
      continue;

    Register IndexReg = AddAddrI->getOperand(2).getReg();

    MachineBasicBlock::iterator JmpI = Body.getLastNonDebugInstr();
    if (JmpI == Body.end() || JmpI->getOpcode() != Bedrock::JMP ||
        JmpI->getNumOperands() < 1 || !JmpI->getOperand(0).isMBB() ||
        JmpI->getOperand(0).getMBB() != Header)
      continue;

    MachineBasicBlock::iterator AddIndexI = prevNonDebug(JmpI, Body);
    while (AddIndexI != Body.end() &&
           !(AddIndexI->getOpcode() == Bedrock::ADD64ri &&
             AddIndexI->getNumOperands() >= 3 &&
             AddIndexI->getOperand(0).isReg() &&
             AddIndexI->getOperand(1).isReg() &&
             AddIndexI->getOperand(2).isImm() &&
             AddIndexI->getOperand(2).getImm() == 4 &&
             regsOverlap(TRI, AddIndexI->getOperand(0).getReg(), IndexReg) &&
             regsOverlap(TRI, AddIndexI->getOperand(1).getReg(), IndexReg))) {
      if (AddIndexI == Body.begin())
        AddIndexI = Body.end();
      else
        AddIndexI = prevNonDebug(AddIndexI, Body);
    }
    if (AddIndexI == Body.end() || AddIndexI == LeaI || AddIndexI == AddAddrI)
      continue;

    MachineBasicBlock::iterator StoreI = nextNonDebug(AddAddrI, Body);
    while (StoreI != Body.end() && StoreI != AddIndexI &&
           !(StoreI->getOpcode() == Bedrock::MOV32mr &&
             StoreI->getNumOperands() >= 3 && StoreI->getOperand(0).isReg() &&
             StoreI->getOperand(1).isReg() && StoreI->getOperand(2).isImm() &&
             StoreI->getOperand(2).getImm() == 0 &&
             regsOverlap(TRI, StoreI->getOperand(1).getReg(), AddrReg)))
      StoreI = nextNonDebug(StoreI, Body);
    if (StoreI == Body.end() || StoreI == AddIndexI)
      continue;

    int64_t Start = 0;
    int64_t Limit = 0;
    MachineInstr *IndexMaterializedDef = nullptr;
    MachineInstr *LimitMaterializedDef = nullptr;
    bool CmpImm = CmpI->getOpcode() == Bedrock::CMP64ri ||
                  CmpI->getOpcode() == Bedrock::CMP32ri;
    if (CmpImm) {
      if (!CmpI->getOperand(1).isImm() ||
          !regsOverlap(TRI, CmpI->getOperand(0).getReg(), IndexReg))
        continue;
      Limit = CmpI->getOperand(1).getImm();
    } else {
      if (!CmpI->getOperand(1).isReg())
        continue;
      bool IndexIsLHS =
          regsOverlap(TRI, CmpI->getOperand(0).getReg(), IndexReg);
      bool IndexIsRHS =
          regsOverlap(TRI, CmpI->getOperand(1).getReg(), IndexReg);
      if (IndexIsLHS == IndexIsRHS)
        continue;
      Register LimitReg = IndexIsLHS ? CmpI->getOperand(1).getReg()
                                     : CmpI->getOperand(0).getReg();
      if (!findConstOrStackLoadBefore(*CmpI, LimitReg, Limit,
                                      LimitMaterializedDef, TRI))
        continue;
    }
    if (!findConstOrStackLoadBefore(*CmpI, IndexReg, Start,
                                    IndexMaterializedDef, TRI) ||
        Limit <= Start || ((Limit - Start) % 4) != 0 ||
        (Limit - Start) / 4 > std::numeric_limits<int32_t>::max())
      continue;

    if (Start == 0 && isAReg(BaseReg)) {
      MachineBasicBlock::iterator ExitAddI =
          findBaseAddBeforeBaseTouch(*Exit, BaseReg, Limit, TRI);
      if (ExitAddI != Exit->end() &&
          ExitAddI->getOpcode() == Bedrock::ADD64ri &&
          ExitAddI->getNumOperands() >= 3 && ExitAddI->getOperand(0).isReg() &&
          ExitAddI->getOperand(1).isReg() && ExitAddI->getOperand(2).isImm() &&
          ExitAddI->getOperand(2).getImm() == Limit &&
          regsOverlap(TRI, ExitAddI->getOperand(0).getReg(), BaseReg) &&
          regsOverlap(TRI, ExitAddI->getOperand(1).getReg(), BaseReg) &&
          (!instrDefinesReg(*ExitAddI, Bedrock::FLAGS, TRI) ||
           regDefDeadOrDeadAfterInCFG(ExitAddI, *Exit, Bedrock::FLAGS, TRI))) {
        Register CountReg =
            isDReg(IndexReg) ? IndexReg
                             : findScratchDRegForLoop(*Header, Body, *Exit, TRI,
                                                      IndexReg, AddrReg);
        if (!CountReg)
          continue;

        SmallPtrSet<MachineBasicBlock *, 8> Visiting;
        if (!regUnusedFromBlockStartInCFG(*Exit, IndexReg, TRI, Visiting))
          continue;
        Visiting.clear();
        if (!regDeadFromBlockStartInCFG(*Exit, CountReg, TRI, Visiting))
          continue;
        Visiting.clear();
        if (!regDeadFromBlockStartInCFG(*Exit, AddrReg, TRI, Visiting))
          continue;

        bool InvalidBody = false;
        for (MachineBasicBlock::iterator I = Body.begin(); I != JmpI;
             I = nextNonDebug(I, Body)) {
          if (I->isDebugInstr() || I == LeaI || I == AddAddrI || I == StoreI ||
              I == AddIndexI)
            continue;
          if (I->isBranch() || I->isCall() || I->isReturn() ||
              I->isTerminator() || hasOrderedMemOperand(*I) ||
              instrTouchesReg(*I, BaseReg, TRI) ||
              instrTouchesReg(*I, IndexReg, TRI) ||
              instrTouchesReg(*I, CountReg, TRI)) {
            InvalidBody = true;
            break;
          }
        }
        if (InvalidBody)
          continue;

        Register StoreSrc = StoreI->getOperand(0).getReg();
        if (regsOverlap(TRI, StoreSrc, BaseReg) ||
            regsOverlap(TRI, StoreSrc, CountReg))
          continue;

        int64_t Count = Limit / 4;
        DebugLoc DL = StoreI->getDebugLoc();
        BuildMI(*Header, CmpI, DL, TII.get(Bedrock::MOV32ri), CountReg)
            .addImm(Count);
        MachineInstrBuilder Rep =
            BuildMI(*Header, CmpI, DL, TII.get(Bedrock::REPMOV32postmr),
                    CountReg)
                .addReg(CountReg)
                .addReg(StoreSrc)
                .addReg(BaseReg)
                .addImm(Bedrock::UpdatePostInc);
        Rep.cloneMemRefs(*StoreI);
        CmpI->eraseFromParent();
        BranchI->eraseFromParent();
        Header->removeSuccessor(&Body);
        if (!Header->isSuccessor(Exit))
          Header->addSuccessor(Exit);
        ExitAddI->eraseFromParent();

        for (auto I = Body.begin(); I != Body.end();) {
          if (I->isDebugInstr()) {
            ++I;
            continue;
          }
          MachineInstr *MI = &*I++;
          MI->eraseFromParent();
        }
        SmallVector<MachineBasicBlock *, 4> BodySuccs(Body.successors());
        for (MachineBasicBlock *Succ : BodySuccs)
          Body.removeSuccessor(Succ);
        Body.eraseFromParent();

        if (!Header->isLiveIn(CountReg))
          Header->addLiveIn(CountReg);
        if (!Header->isLiveIn(BaseReg))
          Header->addLiveIn(BaseReg);
        Changed = true;
        continue;
      }
    }

    Register CountReg = isDReg(IndexReg)
                            ? IndexReg
                            : findScratchDRegForLoop(*Header, Body, *Exit, TRI,
                                                     IndexReg, AddrReg);
    if (!CountReg)
      continue;

    SmallPtrSet<MachineBasicBlock *, 8> Visiting;
    if (!regDeadFromBlockStartInCFG(*Exit, IndexReg, TRI, Visiting))
      continue;
    Visiting.clear();
    if (!regDeadFromBlockStartInCFG(*Exit, CountReg, TRI, Visiting))
      continue;
    Visiting.clear();
    if (!regDeadFromBlockStartInCFG(*Exit, AddrReg, TRI, Visiting))
      continue;
    Visiting.clear();
    if (!regDeadFromBlockStartInCFG(*Exit, Bedrock::FLAGS, TRI, Visiting))
      continue;

    bool InvalidBody = false;
    for (MachineBasicBlock::iterator I = Body.begin(); I != JmpI;
         I = nextNonDebug(I, Body)) {
      if (I->isDebugInstr() || I == LeaI || I == AddAddrI || I == AddIndexI)
        continue;
      if (I->isBranch() || I->isCall() || I->isReturn() || I->isTerminator() ||
          hasOrderedMemOperand(*I) || instrTouchesReg(*I, CountReg, TRI)) {
        InvalidBody = true;
        break;
      }
    }
    if (InvalidBody)
      continue;

    DebugLoc DL = StoreI->getDebugLoc();
    int64_t Count = (Limit - Start) / 4;
    BuildMI(*Header, CmpI, DL, TII.get(Bedrock::LEAri), AddrReg)
        .addReg(BaseReg)
        .addImm(BaseOffset + Start);
    BuildMI(*Header, CmpI, DL, TII.get(Bedrock::MOV32ri), CountReg)
        .addImm(Count);
    CmpI->eraseFromParent();
    BranchI->eraseFromParent();
    Header->removeSuccessor(Exit);
    if (IndexMaterializedDef)
      IndexMaterializedDef->eraseFromParent();
    if (LimitMaterializedDef && LimitMaterializedDef != IndexMaterializedDef)
      LimitMaterializedDef->eraseFromParent();

    LeaI->eraseFromParent();
    AddAddrI->eraseFromParent();

    MachineInstrBuilder Store =
        BuildMI(Body, StoreI, DL, TII.get(Bedrock::MOV32postmr))
            .addReg(StoreI->getOperand(0).getReg())
            .addReg(AddrReg);
    Store.cloneMemRefs(*StoreI);
    StoreI->eraseFromParent();

    AddIndexI->eraseFromParent();

    BuildMI(Body, JmpI, JmpI->getDebugLoc(), TII.get(Bedrock::DEC32r), CountReg)
        .addReg(CountReg);
    BuildMI(Body, JmpI, JmpI->getDebugLoc(), TII.get(Bedrock::JCC))
        .addMBB(&Body)
        .addImm(BedrockCC::NE);
    JmpI->eraseFromParent();

    Body.removeSuccessor(Header);
    if (!Body.isSuccessor(&Body))
      Body.addSuccessor(&Body);
    if (!Body.isSuccessor(Exit))
      Body.addSuccessor(Exit);
    if (!Header->isLiveIn(CountReg))
      Header->addLiveIn(CountReg);
    if (!Header->isLiveIn(AddrReg))
      Header->addLiveIn(AddrReg);
    if (!Body.isLiveIn(CountReg))
      Body.addLiveIn(CountReg);
    if (!Body.isLiveIn(AddrReg))
      Body.addLiveIn(AddrReg);
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldAscendingConstStoreLoops(
    MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  SmallVector<MachineBasicBlock *, 8> Headers;
  for (MachineBasicBlock &MBB : MF)
    Headers.push_back(&MBB);

  for (MachineBasicBlock *Header : Headers) {
    if (!Header || Header->getParent() != &MF)
      continue;

    MachineBasicBlock::iterator CmpI = Header->begin();
    while (CmpI != Header->end() && CmpI->isDebugInstr())
      ++CmpI;
    if (CmpI == Header->end() ||
        (CmpI->getOpcode() != Bedrock::CMP64rr &&
         CmpI->getOpcode() != Bedrock::CMP32rr &&
         CmpI->getOpcode() != Bedrock::CMP64ri &&
         CmpI->getOpcode() != Bedrock::CMP32ri) ||
        CmpI->getNumOperands() < 2 || !CmpI->getOperand(0).isReg())
      continue;

    MachineBasicBlock::iterator BranchI = nextNonDebug(CmpI, *Header);
    std::optional<int64_t> BranchCC =
        BranchI == Header->end() || BranchI->getNumOperands() < 2
            ? std::nullopt
            : getCondCodeImm(BranchI->getOperand(1));
    if (BranchI == Header->end() || BranchI->getOpcode() != Bedrock::JCC ||
        BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
        !BranchCC || *BranchCC != BedrockCC::EQ ||
        nextNonDebug(BranchI, *Header) != Header->end())
      continue;

    MachineFunction::iterator BodyIt = std::next(Header->getIterator());
    if (BodyIt == MF.end())
      continue;
    MachineBasicBlock &Body = *BodyIt;
    MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();
    if (!Exit || !Header->isSuccessor(&Body) || !Header->isSuccessor(Exit) ||
        Body.pred_size() != 1 || !Body.isPredecessor(Header))
      continue;

    MachineBasicBlock::iterator StoreI = Body.begin();
    while (StoreI != Body.end() && StoreI->isDebugInstr())
      ++StoreI;
    MachineBasicBlock::iterator AddIndexI =
        StoreI == Body.end() ? Body.end() : nextNonDebug(StoreI, Body);
    MachineBasicBlock::iterator JmpI =
        AddIndexI == Body.end() ? Body.end() : nextNonDebug(AddIndexI, Body);
    if (StoreI == Body.end() || AddIndexI == Body.end() || JmpI == Body.end() ||
        nextNonDebug(JmpI, Body) != Body.end() ||
        StoreI->getOpcode() != Bedrock::MOV32idx1mr ||
        StoreI->getNumOperands() < 4 || !StoreI->getOperand(0).isReg() ||
        !StoreI->getOperand(1).isReg() || !StoreI->getOperand(2).isReg() ||
        !StoreI->getOperand(3).isImm() ||
        AddIndexI->getOpcode() != Bedrock::ADD64ri ||
        AddIndexI->getNumOperands() < 3 || !AddIndexI->getOperand(0).isReg() ||
        !AddIndexI->getOperand(1).isReg() ||
        !AddIndexI->getOperand(2).isImm() ||
        AddIndexI->getOperand(2).getImm() != 4 ||
        JmpI->getOpcode() != Bedrock::JMP || JmpI->getNumOperands() < 1 ||
        !JmpI->getOperand(0).isMBB() || JmpI->getOperand(0).getMBB() != Header)
      continue;

    Register SrcReg = StoreI->getOperand(0).getReg();
    Register BaseReg = StoreI->getOperand(1).getReg();
    Register IndexReg = StoreI->getOperand(2).getReg();
    int64_t StoreOffset = StoreI->getOperand(3).getImm();
    if (!isDReg(SrcReg) || !isDReg(IndexReg) ||
        !regsOverlap(TRI, AddIndexI->getOperand(0).getReg(), IndexReg) ||
        !regsOverlap(TRI, AddIndexI->getOperand(1).getReg(), IndexReg))
      continue;

    int64_t Start = 0;
    int64_t Limit = 0;
    MachineInstr *IndexMaterializedDef = nullptr;
    MachineInstr *LimitMaterializedDef = nullptr;
    bool CmpImm = CmpI->getOpcode() == Bedrock::CMP64ri ||
                  CmpI->getOpcode() == Bedrock::CMP32ri;
    if (CmpImm) {
      if (!CmpI->getOperand(1).isImm() ||
          !regsOverlap(TRI, CmpI->getOperand(0).getReg(), IndexReg))
        continue;
      Limit = CmpI->getOperand(1).getImm();
    } else {
      if (!CmpI->getOperand(1).isReg())
        continue;
      bool IndexIsLHS =
          regsOverlap(TRI, CmpI->getOperand(0).getReg(), IndexReg);
      bool IndexIsRHS =
          regsOverlap(TRI, CmpI->getOperand(1).getReg(), IndexReg);
      if (IndexIsLHS == IndexIsRHS)
        continue;
      Register LimitReg = IndexIsLHS ? CmpI->getOperand(1).getReg()
                                     : CmpI->getOperand(0).getReg();
      if (!findConstOrStackLoadBefore(*CmpI, LimitReg, Limit,
                                      LimitMaterializedDef, TRI))
        continue;
    }
    if (!findConstOrStackLoadBefore(*CmpI, IndexReg, Start,
                                    IndexMaterializedDef, TRI) ||
        Limit <= Start || ((Limit - Start) % 4) != 0 ||
        (Limit - Start) / 4 > std::numeric_limits<int32_t>::max())
      continue;

    SmallPtrSet<MachineBasicBlock *, 8> Visiting;
    if (!regDeadFromBlockStartInCFG(*Exit, IndexReg, TRI, Visiting))
      continue;
    Visiting.clear();
    if (isAReg(BaseReg) &&
        regDeadFromBlockStartInCFG(*Exit, BaseReg, TRI, Visiting))
      continue;

    int64_t Count = (Limit - Start) / 4;
    int64_t PointerOffset = StoreOffset + Start;
    DebugLoc DL = StoreI->getDebugLoc();

    if (PointerOffset == 0 && Start == 0 && isAReg(BaseReg)) {
      MachineBasicBlock::iterator ExitAddI =
          findBaseAddBeforeBaseTouch(*Exit, BaseReg, Limit, TRI);
      if (ExitAddI != Exit->end() &&
          (!instrDefinesReg(*ExitAddI, Bedrock::FLAGS, TRI) ||
           regDefDeadOrDeadAfterInCFG(ExitAddI, *Exit, Bedrock::FLAGS, TRI))) {
        BuildMI(*Header, CmpI, DL, TII.get(Bedrock::MOV32ri), IndexReg)
            .addImm(Count);
        MachineInstrBuilder Rep =
            BuildMI(*Header, CmpI, DL, TII.get(Bedrock::REPMOV32postmr),
                    IndexReg)
                .addReg(IndexReg)
                .addReg(SrcReg)
                .addReg(BaseReg)
                .addImm(Bedrock::UpdatePostInc);
        Rep.cloneMemRefs(*StoreI);

        CmpI->eraseFromParent();
        BranchI->eraseFromParent();
        Header->removeSuccessor(&Body);
        if (!Header->isSuccessor(Exit))
          Header->addSuccessor(Exit);
        if (IndexMaterializedDef)
          IndexMaterializedDef->eraseFromParent();
        if (LimitMaterializedDef &&
            LimitMaterializedDef != IndexMaterializedDef)
          LimitMaterializedDef->eraseFromParent();
        ExitAddI->eraseFromParent();

        for (auto I = Body.begin(); I != Body.end();) {
          if (I->isDebugInstr()) {
            ++I;
            continue;
          }
          MachineInstr *MI = &*I++;
          MI->eraseFromParent();
        }
        SmallVector<MachineBasicBlock *, 4> BodySuccs(Body.successors());
        for (MachineBasicBlock *Succ : BodySuccs)
          Body.removeSuccessor(Succ);
        Body.eraseFromParent();

        if (!Header->isLiveIn(IndexReg))
          Header->addLiveIn(IndexReg);
        if (!Header->isLiveIn(BaseReg))
          Header->addLiveIn(BaseReg);
        Changed = true;
        continue;
      }
    }

    Register PtrReg =
        findScratchARegForLoop(*Header, Body, *Exit, TRI, BaseReg, SrcReg);
    if (!PtrReg)
      continue;

    if (PointerOffset == 0 && isAReg(BaseReg)) {
      BuildMI(*Header, CmpI, DL, TII.get(Bedrock::MOV64rr), PtrReg)
          .addReg(BaseReg);
    } else {
      BuildMI(*Header, CmpI, DL, TII.get(Bedrock::LEAri), PtrReg)
          .addReg(BaseReg)
          .addImm(PointerOffset);
    }
    BuildMI(*Header, CmpI, DL, TII.get(Bedrock::MOV32ri), IndexReg)
        .addImm(Count);
    MachineInstrBuilder Rep =
        BuildMI(*Header, CmpI, DL, TII.get(Bedrock::REPMOV32postmr), IndexReg)
            .addReg(IndexReg)
            .addReg(SrcReg)
            .addReg(PtrReg)
            .addImm(Bedrock::UpdatePostInc);
    Rep.cloneMemRefs(*StoreI);

    CmpI->eraseFromParent();
    BranchI->eraseFromParent();
    Header->removeSuccessor(&Body);
    if (!Header->isSuccessor(Exit))
      Header->addSuccessor(Exit);
    if (IndexMaterializedDef)
      IndexMaterializedDef->eraseFromParent();
    if (LimitMaterializedDef && LimitMaterializedDef != IndexMaterializedDef)
      LimitMaterializedDef->eraseFromParent();

    for (auto I = Body.begin(); I != Body.end();) {
      if (I->isDebugInstr()) {
        ++I;
        continue;
      }
      MachineInstr *MI = &*I++;
      MI->eraseFromParent();
    }
    SmallVector<MachineBasicBlock *, 4> BodySuccs(Body.successors());
    for (MachineBasicBlock *Succ : BodySuccs)
      Body.removeSuccessor(Succ);
    Body.eraseFromParent();

    if (!Header->isLiveIn(IndexReg))
      Header->addLiveIn(IndexReg);
    if (!Header->isLiveIn(PtrReg))
      Header->addLiveIn(PtrReg);
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldAscendingMultiStoreLoops(
    MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  SmallVector<MachineBasicBlock *, 8> Headers;
  for (MachineBasicBlock &MBB : MF)
    Headers.push_back(&MBB);

  for (MachineBasicBlock *Header : Headers) {
    if (!Header || Header->getParent() != &MF)
      continue;

    MachineBasicBlock::iterator CmpI = Header->begin();
    while (CmpI != Header->end() && CmpI->isDebugInstr())
      ++CmpI;
    if (CmpI == Header->end() ||
        (CmpI->getOpcode() != Bedrock::CMP64rr &&
         CmpI->getOpcode() != Bedrock::CMP32rr) ||
        CmpI->getNumOperands() < 2 || !CmpI->getOperand(0).isReg() ||
        !CmpI->getOperand(1).isReg())
      continue;

    MachineBasicBlock::iterator BranchI = nextNonDebug(CmpI, *Header);
    std::optional<int64_t> BranchCC =
        BranchI == Header->end() || BranchI->getNumOperands() < 2
            ? std::nullopt
            : getCondCodeImm(BranchI->getOperand(1));
    if (BranchI == Header->end() || BranchI->getOpcode() != Bedrock::JCC ||
        BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
        !BranchCC || *BranchCC != BedrockCC::EQ ||
        nextNonDebug(BranchI, *Header) != Header->end())
      continue;

    MachineFunction::iterator BodyIt = std::next(Header->getIterator());
    if (BodyIt == MF.end())
      continue;
    MachineBasicBlock &Body = *BodyIt;
    MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();
    if (!Exit || !Header->isSuccessor(&Body) || !Header->isSuccessor(Exit) ||
        Body.pred_size() != 1 || !Body.isPredecessor(Header))
      continue;

    MachineBasicBlock::iterator BaseCopyI = Body.begin();
    while (BaseCopyI != Body.end() && BaseCopyI->isDebugInstr())
      ++BaseCopyI;
    MachineBasicBlock::iterator AddAddrI =
        BaseCopyI == Body.end() ? Body.end() : nextNonDebug(BaseCopyI, Body);
    if (BaseCopyI == Body.end() || AddAddrI == Body.end() ||
        BaseCopyI->getOpcode() != Bedrock::MOV64rr ||
        BaseCopyI->getNumOperands() < 2 || !BaseCopyI->getOperand(0).isReg() ||
        !BaseCopyI->getOperand(1).isReg() ||
        AddAddrI->getOpcode() != Bedrock::ADD64rr ||
        AddAddrI->getNumOperands() < 3 || !AddAddrI->getOperand(0).isReg() ||
        !AddAddrI->getOperand(1).isReg() || !AddAddrI->getOperand(2).isReg())
      continue;

    Register AddrReg = BaseCopyI->getOperand(0).getReg();
    Register BaseReg = BaseCopyI->getOperand(1).getReg();
    Register IndexReg = AddAddrI->getOperand(2).getReg();
    if (!isAReg(AddrReg) || !isAReg(BaseReg) ||
        !regsOverlap(TRI, AddAddrI->getOperand(0).getReg(), AddrReg) ||
        !regsOverlap(TRI, AddAddrI->getOperand(1).getReg(), AddrReg))
      continue;

    bool IndexIsLHS = regsOverlap(TRI, CmpI->getOperand(0).getReg(), IndexReg);
    bool IndexIsRHS = regsOverlap(TRI, CmpI->getOperand(1).getReg(), IndexReg);
    if (IndexIsLHS == IndexIsRHS)
      continue;
    Register LimitReg = IndexIsLHS ? CmpI->getOperand(1).getReg()
                                   : CmpI->getOperand(0).getReg();

    MachineBasicBlock::iterator JmpI = Body.getLastNonDebugInstr();
    if (JmpI == Body.end() || JmpI->getOpcode() != Bedrock::JMP ||
        JmpI->getNumOperands() < 1 || !JmpI->getOperand(0).isMBB() ||
        JmpI->getOperand(0).getMBB() != Header)
      continue;
    MachineBasicBlock::iterator AddIndexI = prevNonDebug(JmpI, Body);
    if (AddIndexI == Body.end() || AddIndexI->getOpcode() != Bedrock::ADD64ri ||
        AddIndexI->getNumOperands() < 3 || !AddIndexI->getOperand(0).isReg() ||
        !AddIndexI->getOperand(1).isReg() ||
        !AddIndexI->getOperand(2).isImm() ||
        AddIndexI->getOperand(2).getImm() != 4 ||
        !regsOverlap(TRI, AddIndexI->getOperand(0).getReg(), IndexReg) ||
        !regsOverlap(TRI, AddIndexI->getOperand(1).getReg(), IndexReg))
      continue;

    struct StoreDesc {
      Register Src;
      int64_t Offset;
      MachineInstr *MI;
    };
    SmallVector<StoreDesc, 4> Stores;
    bool InvalidBody = false;
    for (MachineBasicBlock::iterator I = nextNonDebug(AddAddrI, Body);
         I != AddIndexI; I = nextNonDebug(I, Body)) {
      if (I == Body.end()) {
        InvalidBody = true;
        break;
      }
      if (I->getOpcode() != Bedrock::MOV32mr || I->getNumOperands() < 3 ||
          !I->getOperand(0).isReg() || !I->getOperand(1).isReg() ||
          !I->getOperand(2).isImm() ||
          !regsOverlap(TRI, I->getOperand(1).getReg(), AddrReg)) {
        InvalidBody = true;
        break;
      }
      Stores.push_back(
          {I->getOperand(0).getReg(), I->getOperand(2).getImm(), &*I});
      if (Stores.size() > 4) {
        InvalidBody = true;
        break;
      }
    }
    if (InvalidBody || Stores.size() < 2)
      continue;

    int64_t Start = 0;
    int64_t Limit = 0;
    MachineInstr *IndexMaterializedDef = nullptr;
    MachineInstr *LimitMaterializedDef = nullptr;
    if (!findConstOrStackLoadBefore(*CmpI, IndexReg, Start,
                                    IndexMaterializedDef, TRI) ||
        !findConstOrStackLoadBefore(*CmpI, LimitReg, Limit,
                                    LimitMaterializedDef, TRI) ||
        Limit <= Start || ((Limit - Start) % 4) != 0 ||
        (Limit - Start) / 4 > std::numeric_limits<int32_t>::max())
      continue;

    SmallPtrSet<MachineBasicBlock *, 8> Visiting;
    if (!regDeadFromBlockStartInCFG(*Exit, IndexReg, TRI, Visiting))
      continue;
    Visiting.clear();
    if (!regDeadFromBlockStartInCFG(*Exit, AddrReg, TRI, Visiting))
      continue;

    SmallVector<Register, 4> PtrRegs;
    PtrRegs.push_back(AddrReg);
    for (Register Reg = Bedrock::A0;
         PtrRegs.size() < Stores.size() && Reg <= Bedrock::A7;
         Reg = Register(Reg + 1)) {
      if (Reg == Bedrock::SP || regsOverlap(TRI, Reg, BaseReg))
        continue;
      bool AlreadyUsed = false;
      for (Register Used : PtrRegs)
        if (regsOverlap(TRI, Reg, Used))
          AlreadyUsed = true;
      if (AlreadyUsed)
        continue;
      bool TouchedInLoop = false;
      for (MachineInstr &MI : *Header)
        if (!MI.isDebugInstr() && instrTouchesReg(MI, Reg, TRI))
          TouchedInLoop = true;
      for (MachineInstr &MI : Body)
        if (!MI.isDebugInstr() && instrTouchesReg(MI, Reg, TRI))
          TouchedInLoop = true;
      if (TouchedInLoop)
        continue;
      Visiting.clear();
      if (!regDeadFromBlockStartInCFG(*Exit, Reg, TRI, Visiting))
        continue;
      PtrRegs.push_back(Reg);
    }
    if (PtrRegs.size() != Stores.size())
      continue;

    Register CountReg = isDReg(IndexReg)
                            ? IndexReg
                            : findScratchDRegForLoop(*Header, Body, *Exit, TRI,
                                                     IndexReg, BaseReg);
    if (!CountReg)
      continue;

    int64_t Count = (Limit - Start) / 4;
    DebugLoc DL = Stores.front().MI->getDebugLoc();
    for (unsigned I = 0, E = Stores.size(); I != E; ++I)
      BuildMI(*Header, CmpI, DL, TII.get(Bedrock::LEAri), PtrRegs[I])
          .addReg(BaseReg)
          .addImm(Stores[I].Offset + Start);
    BuildMI(*Header, CmpI, DL, TII.get(Bedrock::MOV32ri), CountReg)
        .addImm(Count);

    CmpI->eraseFromParent();
    BranchI->eraseFromParent();
    Header->removeSuccessor(Exit);
    if (IndexMaterializedDef)
      IndexMaterializedDef->eraseFromParent();
    if (LimitMaterializedDef && LimitMaterializedDef != IndexMaterializedDef)
      LimitMaterializedDef->eraseFromParent();

    for (auto I = Body.begin(); I != Body.end();) {
      if (I->isDebugInstr()) {
        ++I;
        continue;
      }
      MachineInstr *MI = &*I++;
      MI->eraseFromParent();
    }
    for (unsigned I = 0, E = Stores.size(); I != E; ++I) {
      BuildMI(Body, Body.end(), DL, TII.get(Bedrock::MOV32postmr))
          .addReg(Stores[I].Src)
          .addReg(PtrRegs[I]);
    }
    BuildMI(Body, Body.end(), DL, TII.get(Bedrock::DEC32r), CountReg)
        .addReg(CountReg);
    BuildMI(Body, Body.end(), DL, TII.get(Bedrock::JCC))
        .addMBB(&Body)
        .addImm(BedrockCC::NE);

    Body.removeSuccessor(Header);
    if (!Body.isSuccessor(&Body))
      Body.addSuccessor(&Body);
    if (!Body.isSuccessor(Exit))
      Body.addSuccessor(Exit);
    if (!Header->isLiveIn(CountReg))
      Header->addLiveIn(CountReg);
    for (Register Reg : PtrRegs) {
      if (!Header->isLiveIn(Reg))
        Header->addLiveIn(Reg);
      if (!Body.isLiveIn(Reg))
        Body.addLiveIn(Reg);
    }
    if (!Body.isLiveIn(CountReg))
      Body.addLiveIn(CountReg);
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldMixedZeroStoreLoops(MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  SmallVector<MachineBasicBlock *, 8> Headers;
  for (MachineBasicBlock &MBB : MF)
    Headers.push_back(&MBB);

  auto IsSupportedStore = [](unsigned Opcode) {
    switch (Opcode) {
    default:
      return false;
    case Bedrock::MOV8mr:
    case Bedrock::MOV16mr:
    case Bedrock::MOV32mr:
    case Bedrock::MOV64mr:
      return true;
    }
  };

  auto RegIndex = [](Register Reg) -> std::optional<unsigned> {
    if (Reg >= Bedrock::D0 && Reg <= Bedrock::D7)
      return unsigned(Reg - Bedrock::D0);
    if (Reg >= Bedrock::A0 && Reg <= Bedrock::A7)
      return unsigned(8 + Reg - Bedrock::A0);
    return std::nullopt;
  };

  auto FindConstOrCopyBefore = [&](MachineInstr &Use, Register Reg,
                                   int64_t &Value) -> MachineInstr * {
    std::optional<int64_t> Known[16];
    MachineInstr *Defs[16] = {};

    auto ClearDefinedRegs = [&](MachineInstr &MI) {
      for (Register R = Bedrock::D0; R <= Bedrock::D7; R = Register(R + 1)) {
        if (!instrDefinesReg(MI, R, TRI))
          continue;
        unsigned Idx = *RegIndex(R);
        Known[Idx] = std::nullopt;
        Defs[Idx] = nullptr;
      }
      for (Register R = Bedrock::A0; R <= Bedrock::A7; R = Register(R + 1)) {
        if (!instrDefinesReg(MI, R, TRI))
          continue;
        unsigned Idx = *RegIndex(R);
        Known[Idx] = std::nullopt;
        Defs[Idx] = nullptr;
      }
    };

    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : MBB) {
        if (&MI == &Use) {
          std::optional<unsigned> Idx = RegIndex(Reg);
          if (!Idx || !Known[*Idx])
            return nullptr;
          Value = *Known[*Idx];
          return Defs[*Idx];
        }
        if (MI.isDebugInstr())
          continue;

        std::optional<unsigned> CopyDst;
        std::optional<int64_t> CopyValue;
        MachineInstr *CopyDef = nullptr;
        if ((MI.getOpcode() == Bedrock::MOV32rr ||
             MI.getOpcode() == Bedrock::MOV64rr) &&
            MI.getNumOperands() >= 2 && MI.getOperand(0).isReg() &&
            MI.getOperand(1).isReg()) {
          CopyDst = RegIndex(MI.getOperand(0).getReg());
          std::optional<unsigned> SrcIdx = RegIndex(MI.getOperand(1).getReg());
          if (CopyDst && SrcIdx && Known[*SrcIdx]) {
            CopyValue = Known[*SrcIdx];
            CopyDef = &MI;
          }
        }

        ClearDefinedRegs(MI);

        if (MI.getNumOperands() >= 1 && MI.getOperand(0).isReg()) {
          Register Dst = MI.getOperand(0).getReg();
          if (std::optional<unsigned> Idx = RegIndex(Dst)) {
            if (std::optional<int64_t> Const =
                    getConstDefForReg(MI, Dst, TRI)) {
              Known[*Idx] = *Const;
              Defs[*Idx] = &MI;
              continue;
            }
          }
        }

        if (CopyDst && CopyValue) {
          Known[*CopyDst] = *CopyValue;
          Defs[*CopyDst] = CopyDef;
        }
      }
    }
    return nullptr;
  };

  for (MachineBasicBlock *Header : Headers) {
    if (!Header || Header->getParent() != &MF)
      continue;

    MachineBasicBlock::iterator CmpI = Header->begin();
    while (CmpI != Header->end() && CmpI->isDebugInstr())
      ++CmpI;
    if (CmpI == Header->end() ||
        (CmpI->getOpcode() != Bedrock::CMP64rr &&
         CmpI->getOpcode() != Bedrock::CMP32rr) ||
        CmpI->getNumOperands() < 2 || !CmpI->getOperand(0).isReg() ||
        !CmpI->getOperand(1).isReg())
      continue;

    MachineBasicBlock::iterator BranchI = nextNonDebug(CmpI, *Header);
    std::optional<int64_t> BranchCC =
        BranchI == Header->end() || BranchI->getNumOperands() < 2
            ? std::nullopt
            : getCondCodeImm(BranchI->getOperand(1));
    if (BranchI == Header->end() || BranchI->getOpcode() != Bedrock::JCC ||
        BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
        !BranchCC || *BranchCC != BedrockCC::EQ ||
        nextNonDebug(BranchI, *Header) != Header->end())
      continue;

    MachineFunction::iterator BodyIt = std::next(Header->getIterator());
    if (BodyIt == MF.end())
      continue;
    MachineBasicBlock &Body = *BodyIt;
    MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();
    if (!Exit || !Header->isSuccessor(&Body) || !Header->isSuccessor(Exit) ||
        Body.pred_size() != 1 || !Body.isPredecessor(Header))
      continue;

    MachineBasicBlock::iterator BaseCopyI = Body.begin();
    while (BaseCopyI != Body.end() && BaseCopyI->isDebugInstr())
      ++BaseCopyI;
    MachineBasicBlock::iterator AddAddrI =
        BaseCopyI == Body.end() ? Body.end() : nextNonDebug(BaseCopyI, Body);
    if (BaseCopyI == Body.end() || AddAddrI == Body.end() ||
        BaseCopyI->getOpcode() != Bedrock::MOV64rr ||
        BaseCopyI->getNumOperands() < 2 || !BaseCopyI->getOperand(0).isReg() ||
        !BaseCopyI->getOperand(1).isReg() ||
        AddAddrI->getOpcode() != Bedrock::ADD64rr ||
        AddAddrI->getNumOperands() < 3 || !AddAddrI->getOperand(0).isReg() ||
        !AddAddrI->getOperand(1).isReg() || !AddAddrI->getOperand(2).isReg())
      continue;

    Register AddrReg = BaseCopyI->getOperand(0).getReg();
    Register BaseReg = BaseCopyI->getOperand(1).getReg();
    Register IndexReg = AddAddrI->getOperand(2).getReg();
    if (!isAReg(AddrReg) || !isAReg(BaseReg) || !isDReg(IndexReg) ||
        !regsOverlap(TRI, AddAddrI->getOperand(0).getReg(), AddrReg) ||
        !regsOverlap(TRI, AddAddrI->getOperand(1).getReg(), AddrReg))
      continue;

    bool IndexIsLHS = regsOverlap(TRI, CmpI->getOperand(0).getReg(), IndexReg);
    bool IndexIsRHS = regsOverlap(TRI, CmpI->getOperand(1).getReg(), IndexReg);
    if (IndexIsLHS == IndexIsRHS)
      continue;
    Register LimitReg = IndexIsLHS ? CmpI->getOperand(1).getReg()
                                   : CmpI->getOperand(0).getReg();

    MachineBasicBlock::iterator JmpI = Body.getLastNonDebugInstr();
    if (JmpI == Body.end() || JmpI->getOpcode() != Bedrock::JMP ||
        JmpI->getNumOperands() < 1 || !JmpI->getOperand(0).isMBB() ||
        JmpI->getOperand(0).getMBB() != Header)
      continue;
    MachineBasicBlock::iterator AddIndexI = prevNonDebug(JmpI, Body);
    if (AddIndexI == Body.end() || AddIndexI->getOpcode() != Bedrock::ADD64ri ||
        AddIndexI->getNumOperands() < 3 || !AddIndexI->getOperand(0).isReg() ||
        !AddIndexI->getOperand(1).isReg() ||
        !AddIndexI->getOperand(2).isImm() ||
        !regsOverlap(TRI, AddIndexI->getOperand(0).getReg(), IndexReg) ||
        !regsOverlap(TRI, AddIndexI->getOperand(1).getReg(), IndexReg))
      continue;

    int64_t Stride = AddIndexI->getOperand(2).getImm();
    if (Stride <= 0 || Stride > std::numeric_limits<int32_t>::max())
      continue;

    struct StoreDesc {
      unsigned Opcode;
      Register Src;
      int64_t Offset;
      MachineInstr *MI;
    };
    SmallVector<StoreDesc, 4> Stores;
    bool InvalidBody = false;
    for (MachineBasicBlock::iterator I = nextNonDebug(AddAddrI, Body);
         I != AddIndexI; I = nextNonDebug(I, Body)) {
      if (I == Body.end()) {
        InvalidBody = true;
        break;
      }
      if (!IsSupportedStore(I->getOpcode()) || I->getNumOperands() < 3 ||
          !I->getOperand(0).isReg() || !I->getOperand(1).isReg() ||
          !I->getOperand(2).isImm() ||
          !regsOverlap(TRI, I->getOperand(1).getReg(), AddrReg) ||
          hasOrderedMemOperand(*I) ||
          regsOverlap(TRI, I->getOperand(0).getReg(), AddrReg) ||
          regsOverlap(TRI, I->getOperand(0).getReg(), IndexReg)) {
        InvalidBody = true;
        break;
      }

      int64_t Zero = 0;
      if (!FindConstOrCopyBefore(*I, I->getOperand(0).getReg(), Zero) ||
          Zero != 0) {
        InvalidBody = true;
        break;
      }
      Stores.push_back({I->getOpcode(), I->getOperand(0).getReg(),
                        I->getOperand(2).getImm(), &*I});
      if (Stores.size() > 4) {
        InvalidBody = true;
        break;
      }
    }
    if (InvalidBody || Stores.size() < 2)
      continue;

    int64_t Start = 0;
    int64_t Limit = 0;
    MachineInstr *IndexMaterializedDef =
        FindConstOrCopyBefore(*CmpI, IndexReg, Start);
    MachineInstr *LimitMaterializedDef =
        FindConstOrCopyBefore(*CmpI, LimitReg, Limit);
    if (!IndexMaterializedDef || !LimitMaterializedDef || Limit <= Start ||
        ((Limit - Start) % Stride) != 0 ||
        (Limit - Start) / Stride > std::numeric_limits<int32_t>::max())
      continue;

    Register CountReg = IndexReg;
    for (const StoreDesc &Store : Stores)
      if (regsOverlap(TRI, Store.Src, CountReg) ||
          regsOverlap(TRI, Store.Src, LimitReg))
        InvalidBody = true;
    if (InvalidBody)
      continue;

    SmallPtrSet<MachineBasicBlock *, 8> Visiting;
    if (!regDeadFromBlockStartInCFG(*Exit, IndexReg, TRI, Visiting))
      continue;
    Visiting.clear();
    if (!regDeadFromBlockStartInCFG(*Exit, AddrReg, TRI, Visiting))
      continue;
    Visiting.clear();
    if (!regDeadFromBlockStartInCFG(*Exit, Bedrock::FLAGS, TRI, Visiting))
      continue;

    bool ReuseBaseReg = false;
    if (Start == 0 && !regsOverlap(TRI, BaseReg, CountReg) &&
        !regsOverlap(TRI, BaseReg, LimitReg)) {
      Visiting.clear();
      ReuseBaseReg = regDeadFromBlockStartInCFG(*Exit, BaseReg, TRI, Visiting);
      for (const StoreDesc &Store : Stores)
        if (regsOverlap(TRI, Store.Src, BaseReg))
          ReuseBaseReg = false;
    }
    Register LoopBaseReg = ReuseBaseReg ? BaseReg : AddrReg;

    int64_t Count = (Limit - Start) / Stride;
    DebugLoc DL = Stores.front().MI->getDebugLoc();
    if (ReuseBaseReg) {
      // The original base is dead after the counted loop, so it can serve as
      // the post-increment loop pointer instead of materializing an alias.
    } else if (Start == 0) {
      BuildMI(*Header, CmpI, DL, TII.get(Bedrock::MOV64rr), AddrReg)
          .addReg(BaseReg);
    } else {
      BuildMI(*Header, CmpI, DL, TII.get(Bedrock::LEAri), AddrReg)
          .addReg(BaseReg)
          .addImm(Start);
    }
    BuildMI(*Header, CmpI, DL, TII.get(Bedrock::MOV32ri), CountReg)
        .addImm(Count);

    CmpI->eraseFromParent();
    BranchI->eraseFromParent();
    Header->removeSuccessor(Exit);
    if (IndexMaterializedDef)
      IndexMaterializedDef->eraseFromParent();
    if (LimitMaterializedDef && LimitMaterializedDef != IndexMaterializedDef)
      LimitMaterializedDef->eraseFromParent();

    MachineBasicBlock::iterator Insert = BaseCopyI;
    for (const StoreDesc &Store : Stores) {
      MachineInstrBuilder MIB = BuildMI(Body, Insert, DL, TII.get(Store.Opcode))
                                    .addReg(Store.Src)
                                    .addReg(LoopBaseReg)
                                    .addImm(Store.Offset);
      MIB.cloneMemRefs(*Store.MI);
    }
    BuildMI(Body, Insert, AddIndexI->getDebugLoc(), TII.get(Bedrock::ADD64ri),
            LoopBaseReg)
        .addReg(LoopBaseReg)
        .addImm(Stride);
    BuildMI(Body, Insert, AddIndexI->getDebugLoc(), TII.get(Bedrock::DEC32r),
            CountReg)
        .addReg(CountReg);
    BuildMI(Body, Insert, JmpI->getDebugLoc(), TII.get(Bedrock::JCC))
        .addMBB(&Body)
        .addImm(BedrockCC::NE);

    SmallVector<MachineInstr *, 8> Erase;
    Erase.push_back(&*BaseCopyI);
    Erase.push_back(&*AddAddrI);
    for (const StoreDesc &Store : Stores)
      Erase.push_back(Store.MI);
    Erase.push_back(&*AddIndexI);
    Erase.push_back(&*JmpI);
    for (MachineInstr *MI : Erase)
      MI->eraseFromParent();

    Body.removeSuccessor(Header);
    if (!Body.isSuccessor(&Body))
      Body.addSuccessor(&Body);
    if (!Body.isSuccessor(Exit))
      Body.addSuccessor(Exit);

    if (!Header->isLiveIn(BaseReg))
      Header->addLiveIn(BaseReg);
    if (!Body.isLiveIn(LoopBaseReg))
      Body.addLiveIn(LoopBaseReg);
    if (!Body.isLiveIn(CountReg))
      Body.addLiveIn(CountReg);
    for (const StoreDesc &Store : Stores) {
      if (!Header->isLiveIn(Store.Src))
        Header->addLiveIn(Store.Src);
      if (!Body.isLiveIn(Store.Src))
        Body.addLiveIn(Store.Src);
    }
    removeRegLiveInsWithoutUses(MF, LimitReg, TRI);
    if (ReuseBaseReg)
      removeRegLiveInsWithoutUses(MF, AddrReg, TRI);
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldByteZeroOffsetStoreLoops(
    MachineFunction &MF) const {
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  SmallVector<MachineBasicBlock *, 8> Headers;
  for (MachineBasicBlock &MBB : MF)
    Headers.push_back(&MBB);

  for (MachineBasicBlock *Header : Headers) {
    if (!Header || Header->getParent() != &MF)
      continue;

    MachineBasicBlock::iterator CmpI = Header->begin();
    while (CmpI != Header->end() && CmpI->isDebugInstr())
      ++CmpI;
    if (CmpI == Header->end() || CmpI->getOpcode() != Bedrock::CMP64rr ||
        CmpI->getNumOperands() < 2 || !CmpI->getOperand(0).isReg() ||
        !CmpI->getOperand(1).isReg())
      continue;

    MachineBasicBlock::iterator BranchI = nextNonDebug(CmpI, *Header);
    std::optional<int64_t> BranchCC =
        BranchI == Header->end() || BranchI->getNumOperands() < 2
            ? std::nullopt
            : getCondCodeImm(BranchI->getOperand(1));
    if (BranchI == Header->end() || BranchI->getOpcode() != Bedrock::JCC ||
        BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
        !BranchCC || *BranchCC != BedrockCC::EQ ||
        nextNonDebug(BranchI, *Header) != Header->end())
      continue;

    MachineFunction::iterator BodyIt = std::next(Header->getIterator());
    if (BodyIt == MF.end())
      continue;
    MachineBasicBlock &Body = *BodyIt;
    MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();
    if (!Exit || !Header->isSuccessor(&Body) || !Header->isSuccessor(Exit) ||
        Body.pred_size() != 1 || !Body.isPredecessor(Header))
      continue;

    MachineBasicBlock::iterator CopyI = Body.begin();
    while (CopyI != Body.end() && CopyI->isDebugInstr())
      ++CopyI;
    MachineBasicBlock::iterator AddI =
        CopyI == Body.end() ? Body.end() : nextNonDebug(CopyI, Body);
    MachineBasicBlock::iterator StoreI =
        AddI == Body.end() ? Body.end() : nextNonDebug(AddI, Body);
    MachineBasicBlock::iterator IncI =
        StoreI == Body.end() ? Body.end() : nextNonDebug(StoreI, Body);
    MachineBasicBlock::iterator JmpI =
        IncI == Body.end() ? Body.end() : nextNonDebug(IncI, Body);
    if (CopyI == Body.end() || AddI == Body.end() || StoreI == Body.end() ||
        IncI == Body.end() || JmpI == Body.end() ||
        nextNonDebug(JmpI, Body) != Body.end() ||
        CopyI->getOpcode() != Bedrock::MOV64rr || CopyI->getNumOperands() < 2 ||
        !CopyI->getOperand(0).isReg() || !CopyI->getOperand(1).isReg() ||
        AddI->getOpcode() != Bedrock::ADD64rr || AddI->getNumOperands() < 3 ||
        !AddI->getOperand(0).isReg() || !AddI->getOperand(1).isReg() ||
        !AddI->getOperand(2).isReg() ||
        StoreI->getOpcode() != Bedrock::MOV8mr ||
        StoreI->getNumOperands() < 3 || !StoreI->getOperand(0).isReg() ||
        !StoreI->getOperand(1).isReg() || !StoreI->getOperand(2).isImm() ||
        StoreI->getOperand(2).getImm() != 0 ||
        IncI->getOpcode() != Bedrock::INC64r || IncI->getNumOperands() < 2 ||
        !IncI->getOperand(0).isReg() || !IncI->getOperand(1).isReg() ||
        JmpI->getOpcode() != Bedrock::JMP || JmpI->getNumOperands() < 1 ||
        !JmpI->getOperand(0).isMBB() || JmpI->getOperand(0).getMBB() != Header)
      continue;

    Register TempBase = CopyI->getOperand(0).getReg();
    Register Base = CopyI->getOperand(1).getReg();
    Register Index = CmpI->getOperand(0).getReg();
    Register Limit = CmpI->getOperand(1).getReg();
    Register StoreBase = StoreI->getOperand(1).getReg();
    Register StoreSrc = StoreI->getOperand(0).getReg();
    if (!isAReg(TempBase) || !isAReg(Base) || !isDReg(Index) ||
        !isDReg(Limit) || !isDReg(StoreSrc) ||
        !regsOverlap(TRI, AddI->getOperand(0).getReg(), TempBase) ||
        !regsOverlap(TRI, AddI->getOperand(1).getReg(), TempBase) ||
        !regsOverlap(TRI, AddI->getOperand(2).getReg(), Index) ||
        !regsOverlap(TRI, StoreBase, TempBase) ||
        !regsOverlap(TRI, IncI->getOperand(0).getReg(), Index) ||
        !regsOverlap(TRI, IncI->getOperand(1).getReg(), Index))
      continue;

    int64_t Start = 0;
    int64_t End = 0;
    MachineInstr *IndexDef =
        findLastConstDefBeforeAny(*CmpI, Index, Start, TRI);
    MachineInstr *LimitDef = findLastConstDefBeforeAny(*CmpI, Limit, End, TRI);
    if (!IndexDef || !LimitDef || End <= Start || (Start % 4) != 0 ||
        (End % 4) != 0 ||
        (End - Start) / 4 > std::numeric_limits<int32_t>::max())
      continue;
    if (!findLastConstDefBefore(*StoreI, StoreSrc, 0, TRI))
      continue;

    MachineInstr *PtrDef = nullptr;
    Register Ptr;
    for (const MachineBasicBlock::RegisterMaskPair &LiveIn :
         Header->liveins()) {
      Register Candidate = Register(LiveIn.PhysReg);
      if (!isAReg(Candidate) || regsOverlap(TRI, Candidate, TempBase))
        continue;
      if (MachineInstr *Def =
              findLeaDefBefore(*CmpI, Candidate, Base, Start, TRI)) {
        PtrDef = Def;
        Ptr = Candidate;
        break;
      }
    }
    if (!PtrDef)
      continue;

    SmallPtrSet<MachineBasicBlock *, 8> Visiting;
    if (!regDeadFromBlockStartInCFG(*Exit, TempBase, TRI, Visiting))
      continue;
    Visiting.clear();
    if (!regDeadFromBlockStartInCFG(*Exit, Index, TRI, Visiting))
      continue;

    IndexDef->setDesc(TII.get(Bedrock::MOV32ri));
    IndexDef->getOperand(1).setImm((End - Start) / 4);

    DebugLoc DL = StoreI->getDebugLoc();
    BuildMI(*Header, CmpI, DL, TII.get(Bedrock::MOV64rr), TempBase).addReg(Ptr);
    BuildMI(*Header, CmpI, DL, TII.get(Bedrock::REPMOV32postmr), Index)
        .addReg(Index)
        .addReg(StoreSrc)
        .addReg(TempBase)
        .addImm(Bedrock::UpdatePostInc);

    CmpI->eraseFromParent();
    BranchI->eraseFromParent();
    LimitDef->eraseFromParent();

    SmallVector<MachineBasicBlock *, 4> HeaderSuccs(Header->successors());
    for (MachineBasicBlock *Succ : HeaderSuccs)
      Header->removeSuccessor(Succ);
    Header->addSuccessor(Exit);

    for (auto I = Body.begin(); I != Body.end();) {
      if (I->isDebugInstr()) {
        ++I;
        continue;
      }
      MachineInstr *MI = &*I++;
      MI->eraseFromParent();
    }
    SmallVector<MachineBasicBlock *, 4> BodySuccs(Body.successors());
    for (MachineBasicBlock *Succ : BodySuccs)
      Body.removeSuccessor(Succ);
    Body.eraseFromParent();

    removeRegLiveInsWithoutUses(MF, Limit, TRI);
    removeRegLiveInsWithoutUses(MF, Base, TRI);
    removeRegLiveInsWithoutUses(MF, TempBase, TRI);
    return true;
  }

  return false;
}

bool BedrockPeephole::foldByteOffsetStoreLoops(MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &Header : MF) {
    MachineBasicBlock::iterator CmpI = Header.begin();
    while (CmpI != Header.end() && CmpI->isDebugInstr())
      ++CmpI;
    if (CmpI == Header.end())
      continue;
    bool CmpReg = CmpI->getOpcode() == Bedrock::CMP64rr;
    bool CmpImm = CmpI->getOpcode() == Bedrock::CMP64ri;
    if ((!CmpReg && !CmpImm) || CmpI->getNumOperands() < 2 ||
        !CmpI->getOperand(0).isReg() ||
        (CmpReg && !CmpI->getOperand(1).isReg()) ||
        (CmpImm && !CmpI->getOperand(1).isImm()))
      continue;

    MachineBasicBlock::iterator BranchI = nextNonDebug(CmpI, Header);
    std::optional<int64_t> BranchCC =
        BranchI == Header.end() || BranchI->getNumOperands() < 2
            ? std::nullopt
            : getCondCodeImm(BranchI->getOperand(1));
    if (BranchI == Header.end() || BranchI->getOpcode() != Bedrock::JCC ||
        BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
        !BranchCC || *BranchCC != BedrockCC::EQ ||
        nextNonDebug(BranchI, Header) != Header.end())
      continue;

    MachineFunction::iterator BodyI = std::next(Header.getIterator());
    if (BodyI == MF.end())
      continue;
    MachineBasicBlock &Body = *BodyI;
    MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();
    if (!Exit || !Header.isSuccessor(&Body) || !Header.isSuccessor(Exit))
      continue;

    MachineBasicBlock::iterator StoreI = Body.begin();
    while (StoreI != Body.end() && StoreI->isDebugInstr())
      ++StoreI;
    MachineBasicBlock::iterator AddI =
        StoreI == Body.end() ? Body.end() : nextNonDebug(StoreI, Body);
    MachineBasicBlock::iterator JmpI =
        AddI == Body.end() ? Body.end() : nextNonDebug(AddI, Body);
    if (StoreI == Body.end() || AddI == Body.end() || JmpI == Body.end() ||
        nextNonDebug(JmpI, Body) != Body.end() ||
        StoreI->getOpcode() != Bedrock::MOV32idx1mr ||
        StoreI->getNumOperands() < 4 || !StoreI->getOperand(0).isReg() ||
        !StoreI->getOperand(1).isReg() || !StoreI->getOperand(2).isReg() ||
        !StoreI->getOperand(3).isImm() || StoreI->getOperand(3).getImm() != 0 ||
        AddI->getOpcode() != Bedrock::ADD64ri || AddI->getNumOperands() < 3 ||
        !AddI->getOperand(0).isReg() || !AddI->getOperand(1).isReg() ||
        !AddI->getOperand(2).isImm() || AddI->getOperand(2).getImm() != 4 ||
        JmpI->getOpcode() != Bedrock::JMP || JmpI->getNumOperands() < 1 ||
        !JmpI->getOperand(0).isMBB() || JmpI->getOperand(0).getMBB() != &Header)
      continue;

    Register SrcReg = StoreI->getOperand(0).getReg();
    Register BaseReg = StoreI->getOperand(1).getReg();
    Register IndexReg = StoreI->getOperand(2).getReg();
    if (!isAReg(BaseReg) || regsOverlap(TRI, SrcReg, BaseReg) ||
        regsOverlap(TRI, SrcReg, IndexReg) ||
        !regsOverlap(TRI, AddI->getOperand(0).getReg(), IndexReg) ||
        !regsOverlap(TRI, AddI->getOperand(1).getReg(), IndexReg))
      continue;

    Register LimitReg;
    int64_t LimitBytes = 0;
    MachineInstr *LimitDef = nullptr;
    if (CmpReg) {
      bool IndexIsLHS =
          regsOverlap(TRI, CmpI->getOperand(0).getReg(), IndexReg);
      bool IndexIsRHS =
          regsOverlap(TRI, CmpI->getOperand(1).getReg(), IndexReg);
      if (IndexIsLHS == IndexIsRHS)
        continue;
      LimitReg = IndexIsLHS ? CmpI->getOperand(1).getReg()
                            : CmpI->getOperand(0).getReg();
      if (!isDReg(LimitReg) || regsOverlap(TRI, LimitReg, SrcReg) ||
          regsOverlap(TRI, LimitReg, BaseReg))
        continue;
      LimitDef = findLastConstDefBeforeAny(*CmpI, LimitReg, LimitBytes, TRI);
    } else {
      if (!regsOverlap(TRI, CmpI->getOperand(0).getReg(), IndexReg))
        continue;
      LimitReg = IndexReg;
      LimitBytes = CmpI->getOperand(1).getImm();
    }

    MachineInstr *IndexZeroDef =
        findLastConstDefBefore(*CmpI, IndexReg, 0, TRI);
    if (!IndexZeroDef)
      continue;

    if ((CmpReg && !LimitDef) || LimitBytes <= 0 || (LimitBytes % 4) != 0 ||
        LimitBytes / 4 > std::numeric_limits<int32_t>::max())
      continue;

    SmallPtrSet<MachineBasicBlock *, 8> Visiting;
    if (!regDeadFromBlockStartInCFG(*Exit, BaseReg, TRI, Visiting))
      continue;

    DebugLoc DL = StoreI->getDebugLoc();
    int64_t Count = LimitBytes / 4;
    if (CmpImm) {
      BuildMI(Header, CmpI, CmpI->getDebugLoc(), TII.get(Bedrock::MOV32ri),
              LimitReg)
          .addImm(Count);
      MachineInstrBuilder Rep =
          BuildMI(Header, CmpI, DL, TII.get(Bedrock::REPMOV32postmr), LimitReg)
              .addReg(LimitReg)
              .addReg(SrcReg)
              .addReg(BaseReg)
              .addImm(Bedrock::UpdatePostInc);
      Rep.cloneMemRefs(*StoreI);

      CmpI->eraseFromParent();
      BranchI->eraseFromParent();
      IndexZeroDef->eraseFromParent();

      SmallVector<MachineBasicBlock *, 4> HeaderSuccs(Header.successors());
      for (MachineBasicBlock *Succ : HeaderSuccs)
        Header.removeSuccessor(Succ);
      if (!Header.isSuccessor(Exit))
        Header.addSuccessor(Exit);

      for (auto I = Body.begin(); I != Body.end();) {
        if (I->isDebugInstr()) {
          ++I;
          continue;
        }
        MachineInstr *MI = &*I++;
        MI->eraseFromParent();
      }
      SmallVector<MachineBasicBlock *, 4> BodySuccs(Body.successors());
      for (MachineBasicBlock *Succ : BodySuccs)
        Body.removeSuccessor(Succ);
      Body.eraseFromParent();

      removeRegLiveInsWithoutUses(MF, IndexReg, TRI);
      removeRegLiveInsWithoutUses(MF, BaseReg, TRI);
      Changed = true;
      continue;
    }

    if (CmpReg) {
      LimitDef->setDesc(TII.get(Bedrock::MOV32ri));
      LimitDef->getOperand(1).setImm(Count);
      BuildMI(Header, CmpI, CmpI->getDebugLoc(), TII.get(Bedrock::TEST32rr))
          .addReg(LimitReg)
          .addReg(LimitReg);
      CmpI->eraseFromParent();
      setCondCodeImm(BranchI->getOperand(1), BedrockCC::LE);
    }

    MachineInstrBuilder Store =
        BuildMI(Body, StoreI, DL, TII.get(Bedrock::MOV32postmr))
            .addReg(SrcReg)
            .addReg(BaseReg);
    Store.cloneMemRefs(*StoreI);
    StoreI->eraseFromParent();

    BuildMI(Body, AddI, AddI->getDebugLoc(), TII.get(Bedrock::DEC32r), LimitReg)
        .addReg(LimitReg);
    AddI->eraseFromParent();

    BuildMI(Body, JmpI, JmpI->getDebugLoc(), TII.get(Bedrock::JCC))
        .addMBB(&Body)
        .addImm(BedrockCC::NE);
    JmpI->eraseFromParent();

    Body.removeSuccessor(&Header);
    if (!Body.isSuccessor(&Body))
      Body.addSuccessor(&Body);
    if (!Body.isSuccessor(Exit))
      Body.addSuccessor(Exit);
    if (CmpReg && !Header.isLiveIn(LimitReg))
      Header.addLiveIn(LimitReg);
    if (!Body.isLiveIn(LimitReg))
      Body.addLiveIn(LimitReg);
    if (!Body.isLiveIn(BaseReg))
      Body.addLiveIn(BaseReg);
    IndexZeroDef->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldSingleInstructionRepLoops(
    MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &Body : MF) {
    MachineBasicBlock::iterator LatchI = Body.getLastNonDebugInstr();
    if (LatchI == Body.end() || LatchI->getOpcode() != Bedrock::JCC ||
        LatchI->getNumOperands() < 2 || !LatchI->getOperand(0).isMBB() ||
        LatchI->getOperand(0).getMBB() != &Body)
      continue;

    std::optional<int64_t> LatchCC = getCondCodeImm(LatchI->getOperand(1));
    if (!LatchCC || *LatchCC != BedrockCC::NE)
      continue;

    MachineBasicBlock::iterator DecI = prevNonDebug(LatchI, Body);
    if (DecI == Body.end())
      continue;

    Register CountReg;
    unsigned CmpOpcode = 0;
    if (!isIncDecRegInstr(*DecI, CountReg, CmpOpcode, TRI) ||
        (DecI->getOpcode() != Bedrock::DEC32r &&
         DecI->getOpcode() != Bedrock::DEC64r) ||
        !isDReg(CountReg))
      continue;

    MachineBasicBlock::iterator BodyI = prevNonDebug(DecI, Body);
    if (BodyI == Body.end() || prevNonDebug(BodyI, Body) != Body.end())
      continue;
    if (hasOrderedMemOperand(*BodyI) || instrTouchesReg(*BodyI, CountReg, TRI))
      continue;

    MachineBasicBlock *Header = findSingleNonSelfPredecessor(Body);
    if (!Header || (!isPositiveCountPretest(*Header, Body, CountReg, TRI) &&
                    !isZeroExitCountPretest(*Header, Body, CountReg, TRI)))
      continue;

    bool Built = buildRepMovMM(Body, *BodyI, CountReg, TII) ||
                 buildRepPostMemStore(Body, *BodyI, CountReg, TII) ||
                 buildRepPostMemSource(Body, *BodyI, CountReg, TII);
    if (!Built)
      continue;

    MachineInstr *OldBody = &*BodyI;
    MachineInstr *Dec = &*DecI;
    MachineInstr *Latch = &*LatchI;
    OldBody->eraseFromParent();
    Dec->eraseFromParent();
    Latch->eraseFromParent();

    if (Body.isSuccessor(&Body))
      Body.removeSuccessor(&Body);
    MachineFunction::iterator Next = std::next(Body.getIterator());
    if (Next != MF.end() && !Body.isSuccessor(&*Next))
      Body.addSuccessor(&*Next);
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldWideZeroFillDjtLoops(MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &Body : MF) {
    MachineBasicBlock::iterator LatchI = Body.getLastNonDebugInstr();
    if (LatchI == Body.end() || LatchI->getOpcode() != Bedrock::DJCC32r ||
        LatchI->getNumOperands() < 4 || !LatchI->getOperand(0).isReg() ||
        !LatchI->getOperand(1).isReg() || !LatchI->getOperand(2).isMBB() ||
        LatchI->getOperand(2).getMBB() != &Body)
      continue;

    std::optional<int64_t> LatchCC = getCondCodeImm(LatchI->getOperand(3));
    if (!LatchCC || *LatchCC != BedrockCC::T)
      continue;

    Register CountReg = LatchI->getOperand(0).getReg();
    if (!regsOverlap(TRI, CountReg, LatchI->getOperand(1).getReg()) ||
        !isDReg(CountReg))
      continue;

    MachineBasicBlock::iterator AddI = prevNonDebug(LatchI, Body);
    MachineBasicBlock::iterator Store1I =
        AddI == Body.end() ? Body.end() : prevNonDebug(AddI, Body);
    MachineBasicBlock::iterator Store0I =
        Store1I == Body.end() ? Body.end() : prevNonDebug(Store1I, Body);
    if (AddI == Body.end() || Store1I == Body.end() || Store0I == Body.end() ||
        prevNonDebug(Store0I, Body) != Body.end())
      continue;

    if (AddI->getOpcode() != Bedrock::ADD64ri || AddI->getNumOperands() < 3 ||
        !AddI->getOperand(0).isReg() || !AddI->getOperand(1).isReg() ||
        !AddI->getOperand(2).isImm() || AddI->getOperand(2).getImm() != 16 ||
        (instrDefinesReg(*AddI, Bedrock::FLAGS, TRI) &&
         !regDefDeadOrDeadAfterInCFG(AddI, Body, Bedrock::FLAGS, TRI)))
      continue;

    Register Src0;
    Register Base0;
    int64_t Offset0 = 0;
    Register Src1;
    Register Base1;
    int64_t Offset1 = 0;
    if (!isMemStore(*Store0I, Src0, Base0, Offset0) ||
        !isMemStore(*Store1I, Src1, Base1, Offset1) ||
        Store0I->getOpcode() != Bedrock::MOV64mr ||
        Store1I->getOpcode() != Bedrock::MOV64mr ||
        hasOrderedMemOperand(*Store0I) || hasOrderedMemOperand(*Store1I) ||
        !regsOverlap(TRI, Src0, Src1) || !regsOverlap(TRI, Base0, Base1) ||
        !regsOverlap(TRI, AddI->getOperand(0).getReg(), Base0) ||
        !regsOverlap(TRI, AddI->getOperand(1).getReg(), Base0) ||
        regsOverlap(TRI, Src0, Base0) || regsOverlap(TRI, Src0, CountReg) ||
        regsOverlap(TRI, Base0, CountReg))
      continue;

    if (!((Offset0 == 0 && Offset1 == 8) || (Offset0 == 8 && Offset1 == 0)))
      continue;

    if (!findLastConstDefBefore(*Store0I, Src0, 0, TRI))
      continue;

    int64_t Count = 0;
    if (!findLastConstDefBeforeAny(*LatchI, CountReg, Count, TRI) ||
        Count <= 0 || Count > (std::numeric_limits<int32_t>::max() / 4))
      continue;

    DebugLoc DL = Store0I->getDebugLoc();
    BuildMI(Body, Store0I, DL, TII.get(Bedrock::MOV32ri), CountReg)
        .addImm(Count * 4);
    MachineInstrBuilder Rep =
        BuildMI(Body, Store0I, DL, TII.get(Bedrock::REPMOV32postmr), CountReg)
            .addReg(CountReg)
            .addReg(Src0)
            .addReg(Base0)
            .addImm(Bedrock::UpdatePostInc);
    Rep.cloneMergedMemRefs({&*Store0I, &*Store1I});

    Store0I->eraseFromParent();
    Store1I->eraseFromParent();
    AddI->eraseFromParent();
    LatchI->eraseFromParent();

    if (Body.isSuccessor(&Body))
      Body.removeSuccessor(&Body);
    MachineFunction::iterator Next = std::next(Body.getIterator());
    if (Next != MF.end() && !Body.isSuccessor(&*Next))
      Body.addSuccessor(&*Next);
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldZeroExitRepgTailLoops(MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &Body : MF) {
    MachineBasicBlock::iterator JmpI = Body.getLastNonDebugInstr();
    if (JmpI == Body.end() || JmpI->getOpcode() != Bedrock::JMP ||
        JmpI->getNumOperands() < 1 || !JmpI->getOperand(0).isMBB())
      continue;

    MachineBasicBlock *Header = JmpI->getOperand(0).getMBB();
    if (!Header || Header == &Body || !Body.isSuccessor(Header) ||
        Body.pred_size() != 1 || !Body.isPredecessor(Header))
      continue;

    MachineBasicBlock::iterator TailI = prevNonDebug(JmpI, Body);
    if (TailI == Body.end() ||
        (TailI->getOpcode() != Bedrock::INC32r &&
         TailI->getOpcode() != Bedrock::INC64r) ||
        TailI->getNumOperands() < 2 || !TailI->getOperand(0).isReg() ||
        !TailI->getOperand(1).isReg())
      continue;

    MachineBasicBlock::iterator DecI = prevNonDebug(TailI, Body);
    if (DecI == Body.end())
      continue;

    Register CountReg;
    unsigned CmpOpcode = 0;
    if (!isIncDecRegInstr(*DecI, CountReg, CmpOpcode, TRI) ||
        (DecI->getOpcode() != Bedrock::DEC32r &&
         DecI->getOpcode() != Bedrock::DEC64r) ||
        !isDReg(CountReg) || instrTouchesReg(*TailI, CountReg, TRI) ||
        instrUsesReg(*TailI, Bedrock::FLAGS, TRI))
      continue;

    if (!isZeroExitCountPretest(*Header, Body, CountReg, TRI))
      continue;

    MachineInstr *HeaderBranch = &*Header->getLastNonDebugInstr();
    MachineBasicBlock *Exit = HeaderBranch->getOperand(0).getMBB();
    if (!Exit || !Header->isSuccessor(&Body) || !Header->isSuccessor(Exit))
      continue;

    bool InvalidBody = false;
    for (auto I = Body.begin(); I != DecI; I = nextNonDebug(I, Body)) {
      if (I == Body.end()) {
        InvalidBody = true;
        break;
      }
      if (I->isDebugInstr())
        continue;
      if (I->isBranch() || I->isCall() || I->isReturn() || I->isTerminator() ||
          hasOrderedMemOperand(*I) || instrTouchesReg(*I, CountReg, TRI)) {
        InvalidBody = true;
        break;
      }
    }
    if (InvalidBody)
      continue;

    unsigned DecOpcode = DecI->getOpcode();
    DebugLoc DecDL = DecI->getDebugLoc();
    DecI->eraseFromParent();

    BuildMI(Body, JmpI, DecDL, TII.get(DecOpcode), CountReg).addReg(CountReg);
    BuildMI(Body, JmpI, JmpI->getDebugLoc(), TII.get(Bedrock::JCC))
        .addMBB(&Body)
        .addImm(BedrockCC::NE);
    JmpI->eraseFromParent();

    Body.removeSuccessor(Header);
    if (!Body.isSuccessor(&Body))
      Body.addSuccessor(&Body);
    if (!Body.isSuccessor(Exit))
      Body.addSuccessor(Exit);
    if (!Body.isLiveIn(CountReg))
      Body.addLiveIn(CountReg);
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldAscendingLoadProgressionLoops(
    MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  SmallVector<MachineBasicBlock *, 8> Headers;
  for (MachineBasicBlock &MBB : MF)
    Headers.push_back(&MBB);

  for (MachineBasicBlock *Header : Headers) {
    if (!Header || Header->getParent() != &MF)
      continue;

    MachineBasicBlock::iterator CmpI = Header->begin();
    while (CmpI != Header->end() && CmpI->isDebugInstr())
      ++CmpI;
    if (CmpI == Header->end() ||
        (CmpI->getOpcode() != Bedrock::CMP64rr &&
         CmpI->getOpcode() != Bedrock::CMP32rr &&
         CmpI->getOpcode() != Bedrock::CMP64ri &&
         CmpI->getOpcode() != Bedrock::CMP32ri) ||
        CmpI->getNumOperands() < 2 || !CmpI->getOperand(0).isReg())
      continue;

    MachineBasicBlock::iterator BranchI = nextNonDebug(CmpI, *Header);
    std::optional<int64_t> BranchCC =
        BranchI == Header->end() || BranchI->getNumOperands() < 2
            ? std::nullopt
            : getCondCodeImm(BranchI->getOperand(1));
    if (BranchI == Header->end() || BranchI->getOpcode() != Bedrock::JCC ||
        BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
        !BranchCC || *BranchCC != BedrockCC::EQ ||
        nextNonDebug(BranchI, *Header) != Header->end())
      continue;

    MachineFunction::iterator BodyIt = std::next(Header->getIterator());
    if (BodyIt == MF.end())
      continue;
    MachineBasicBlock &Body = *BodyIt;
    MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();
    if (!Exit || !Header->isSuccessor(&Body) || !Header->isSuccessor(Exit) ||
        Body.pred_size() != 1 || !Body.isPredecessor(Header))
      continue;

    MachineBasicBlock::iterator LoadI = Body.begin();
    while (LoadI != Body.end() && LoadI->isDebugInstr())
      ++LoadI;
    if (LoadI == Body.end() || LoadI->getOpcode() != Bedrock::MOV32idx1rm ||
        LoadI->getNumOperands() < 4 || !LoadI->getOperand(0).isReg() ||
        !LoadI->getOperand(1).isReg() || !LoadI->getOperand(2).isReg() ||
        !LoadI->getOperand(3).isImm() || LoadI->getOperand(3).getImm() != 0 ||
        hasOrderedMemOperand(*LoadI))
      continue;

    Register LoadDst = LoadI->getOperand(0).getReg();
    Register BaseReg = LoadI->getOperand(1).getReg();
    Register IndexReg = LoadI->getOperand(2).getReg();
    if (!isDReg(LoadDst) || !isAReg(BaseReg) || !isDReg(IndexReg))
      continue;

    MachineBasicBlock::iterator JmpI = Body.getLastNonDebugInstr();
    if (JmpI == Body.end() || JmpI->getOpcode() != Bedrock::JMP ||
        JmpI->getNumOperands() < 1 || !JmpI->getOperand(0).isMBB() ||
        JmpI->getOperand(0).getMBB() != Header)
      continue;

    MachineBasicBlock::iterator AddIndexI = prevNonDebug(JmpI, Body);
    while (AddIndexI != Body.end() &&
           !(AddIndexI->getOpcode() == Bedrock::ADD64ri &&
             AddIndexI->getNumOperands() >= 3 &&
             AddIndexI->getOperand(0).isReg() &&
             AddIndexI->getOperand(1).isReg() &&
             AddIndexI->getOperand(2).isImm() &&
             AddIndexI->getOperand(2).getImm() == 4 &&
             regsOverlap(TRI, AddIndexI->getOperand(0).getReg(), IndexReg) &&
             regsOverlap(TRI, AddIndexI->getOperand(1).getReg(), IndexReg))) {
      if (AddIndexI == Body.begin())
        AddIndexI = Body.end();
      else
        AddIndexI = prevNonDebug(AddIndexI, Body);
    }
    if (AddIndexI == Body.end() || AddIndexI == LoadI)
      continue;

    int64_t Start = 0;
    int64_t Limit = 0;
    MachineInstr *IndexDef =
        findLastConstDefBeforeAny(*CmpI, IndexReg, Start, TRI);
    MachineInstr *LimitDef = nullptr;
    bool CmpImm = CmpI->getOpcode() == Bedrock::CMP64ri ||
                  CmpI->getOpcode() == Bedrock::CMP32ri;
    if (CmpImm) {
      if (!CmpI->getOperand(1).isImm() ||
          !regsOverlap(TRI, CmpI->getOperand(0).getReg(), IndexReg))
        continue;
      Limit = CmpI->getOperand(1).getImm();
    } else {
      if (!CmpI->getOperand(1).isReg())
        continue;
      bool IndexIsLHS =
          regsOverlap(TRI, CmpI->getOperand(0).getReg(), IndexReg);
      bool IndexIsRHS =
          regsOverlap(TRI, CmpI->getOperand(1).getReg(), IndexReg);
      if (IndexIsLHS == IndexIsRHS)
        continue;
      Register LimitReg = IndexIsLHS ? CmpI->getOperand(1).getReg()
                                     : CmpI->getOperand(0).getReg();
      LimitDef = findLastConstDefBeforeAny(*CmpI, LimitReg, Limit, TRI);
      if (!LimitDef)
        continue;
    }
    if (!IndexDef || Start != 0 || Limit <= Start ||
        ((Limit - Start) % 4) != 0 ||
        (Limit - Start) / 4 > std::numeric_limits<int32_t>::max())
      continue;

    SmallPtrSet<MachineBasicBlock *, 8> Visiting;
    if (!regDeadFromBlockStartInCFG(*Exit, IndexReg, TRI, Visiting))
      continue;
    Visiting.clear();
    if (!regUnusedFromBlockStartInCFG(*Exit, BaseReg, TRI, Visiting))
      continue;

    bool InvalidBody = false;
    for (auto I = nextNonDebug(LoadI, Body); I != AddIndexI;
         I = nextNonDebug(I, Body)) {
      if (I == Body.end()) {
        InvalidBody = true;
        break;
      }
      if (I->isDebugInstr())
        continue;
      if (I->isBranch() || I->isCall() || I->isReturn() || I->isTerminator() ||
          hasOrderedMemOperand(*I) || instrTouchesReg(*I, IndexReg, TRI) ||
          instrTouchesReg(*I, BaseReg, TRI)) {
        InvalidBody = true;
        break;
      }
    }
    if (InvalidBody)
      continue;

    int64_t Count = (Limit - Start) / 4;
    DebugLoc DL = LoadI->getDebugLoc();
    BuildMI(*Header, CmpI, DL, TII.get(Bedrock::MOV32ri), IndexReg)
        .addImm(Count);
    CmpI->eraseFromParent();
    BranchI->eraseFromParent();
    Header->removeSuccessor(Exit);

    MachineInstrBuilder Load =
        BuildMI(Body, LoadI, DL, TII.get(Bedrock::MOV32postrm), LoadDst)
            .addReg(BaseReg);
    Load.cloneMemRefs(*LoadI);
    LoadI->eraseFromParent();
    AddIndexI->eraseFromParent();

    BuildMI(Body, JmpI, JmpI->getDebugLoc(), TII.get(Bedrock::DEC32r), IndexReg)
        .addReg(IndexReg);
    BuildMI(Body, JmpI, JmpI->getDebugLoc(), TII.get(Bedrock::JCC))
        .addMBB(&Body)
        .addImm(BedrockCC::NE);
    JmpI->eraseFromParent();

    Body.removeSuccessor(Header);
    if (!Body.isSuccessor(&Body))
      Body.addSuccessor(&Body);
    if (!Body.isSuccessor(Exit))
      Body.addSuccessor(Exit);
    if (!Header->isLiveIn(IndexReg))
      Header->addLiveIn(IndexReg);
    if (!Header->isLiveIn(BaseReg))
      Header->addLiveIn(BaseReg);
    if (!Body.isLiveIn(IndexReg))
      Body.addLiveIn(IndexReg);
    if (!Body.isLiveIn(BaseReg))
      Body.addLiveIn(BaseReg);
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldByteIndexedMemUtilityLoops(
    MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  auto CollectInstrs = [](MachineBasicBlock &MBB,
                          SmallVectorImpl<MachineInstr *> &Instrs) {
    Instrs.clear();
    for (MachineInstr &MI : MBB)
      if (!MI.isDebugInstr())
        Instrs.push_back(&MI);
  };

  auto MatchJmp = [](MachineInstr &MI, MachineBasicBlock *Target) {
    return MI.getOpcode() == Bedrock::JMP && MI.getNumOperands() >= 1 &&
           MI.getOperand(0).isMBB() && MI.getOperand(0).getMBB() == Target;
  };

  auto MatchBaseCopy = [&](MachineInstr &MI, Register &Temp, Register Base) {
    if (MI.getOpcode() != Bedrock::MOV64rr || MI.getNumOperands() < 2 ||
        !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
        !regsOverlap(TRI, MI.getOperand(1).getReg(), Base))
      return false;
    Temp = MI.getOperand(0).getReg();
    return isAReg(Temp);
  };

  auto MatchAddIndex = [&](MachineInstr &MI, Register Temp, Register Index) {
    return MI.getOpcode() == Bedrock::ADD64rr && MI.getNumOperands() >= 3 &&
           MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
           MI.getOperand(2).isReg() &&
           regsOverlap(TRI, MI.getOperand(0).getReg(), Temp) &&
           regsOverlap(TRI, MI.getOperand(1).getReg(), Temp) &&
           regsOverlap(TRI, MI.getOperand(2).getReg(), Index);
  };

  auto MatchIncOne = [&](MachineInstr &MI, Register Index) {
    if (MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
        !MI.getOperand(1).isReg() ||
        !regsOverlap(TRI, MI.getOperand(0).getReg(), Index) ||
        !regsOverlap(TRI, MI.getOperand(1).getReg(), Index))
      return false;
    if (MI.getOpcode() == Bedrock::INC64r)
      return true;
    return MI.getOpcode() == Bedrock::ADD64ri && MI.getNumOperands() >= 3 &&
           MI.getOperand(2).isImm() && MI.getOperand(2).getImm() == 1;
  };

  auto MatchZeroOffsetMem = [](MachineInstr &MI) {
    return MI.getNumOperands() >= 3 && MI.getOperand(1).isReg() &&
           MI.getOperand(2).isImm() && MI.getOperand(2).getImm() == 0;
  };

  auto MatchZeroOffsetMM = [](MachineInstr &MI) {
    return MI.getNumOperands() >= 4 && MI.getOperand(0).isReg() &&
           MI.getOperand(1).isImm() && MI.getOperand(1).getImm() == 0 &&
           MI.getOperand(2).isReg() && MI.getOperand(3).isImm() &&
           MI.getOperand(3).getImm() == 0;
  };

  auto IsReturnOnlyUnreferenced = [](MachineBasicBlock &MBB) {
    if (!MBB.pred_empty())
      return false;
    MachineInstr *Only = nullptr;
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      if (Only)
        return false;
      Only = &MI;
    }
    return Only && Only->getOpcode() == Bedrock::RET;
  };

  SmallVector<MachineBasicBlock *, 8> Headers;
  for (MachineBasicBlock &MBB : MF)
    Headers.push_back(&MBB);

  SmallVector<MachineInstr *, 8> HeaderInstrs;
  SmallVector<MachineInstr *, 8> BodyInstrs;
  for (MachineBasicBlock *Header : Headers) {
    if (!Header || Header->getParent() != &MF)
      continue;

    CollectInstrs(*Header, HeaderInstrs);
    if (HeaderInstrs.size() != 2 && HeaderInstrs.size() != 3)
      continue;

    MachineInstr *CmpI = HeaderInstrs[0];
    MachineInstr *BranchI = HeaderInstrs[1];
    MachineInstr *TailI = HeaderInstrs.size() == 3 ? HeaderInstrs[2] : nullptr;
    MachineInstr *JmpI = nullptr;
    MachineInstr *RetI = nullptr;
    if (CmpI->getOpcode() != Bedrock::CMP64rr || CmpI->getNumOperands() < 2 ||
        !CmpI->getOperand(0).isReg() || !CmpI->getOperand(1).isReg() ||
        BranchI->getOpcode() != Bedrock::JCC || BranchI->getNumOperands() < 2 ||
        !BranchI->getOperand(0).isMBB())
      continue;

    std::optional<int64_t> BranchCC = getCondCodeImm(BranchI->getOperand(1));
    if (!BranchCC || *BranchCC != BedrockCC::NE)
      continue;

    MachineBasicBlock *Body = BranchI->getOperand(0).getMBB();
    MachineBasicBlock *Exit = nullptr;
    if (TailI) {
      if (TailI->getOpcode() == Bedrock::JMP) {
        if (TailI->getNumOperands() < 1 || !TailI->getOperand(0).isMBB())
          continue;
        JmpI = TailI;
        Exit = JmpI->getOperand(0).getMBB();
      } else if (TailI->getOpcode() == Bedrock::RET) {
        RetI = TailI;
      } else {
        continue;
      }
    } else {
      for (MachineBasicBlock *Succ : Header->successors()) {
        if (Succ == Body)
          continue;
        if (Exit) {
          Exit = nullptr;
          break;
        }
        Exit = Succ;
      }
    }
    if (!Body || Body == Header || Body == Exit || (!Exit && !RetI) ||
        !Body->isSuccessor(Header))
      continue;
    if (Exit && (!Header->isSuccessor(Body) || !Header->isSuccessor(Exit) ||
                 Body->pred_size() != 1))
      continue;

    Register CmpLHS = CmpI->getOperand(0).getReg();
    Register CmpRHS = CmpI->getOperand(1).getReg();
    MachineInstr *LHSZero = findLastConstDefBefore(*CmpI, CmpLHS, 0, TRI);
    MachineInstr *RHSZero = findLastConstDefBefore(*CmpI, CmpRHS, 0, TRI);
    if ((LHSZero == nullptr) == (RHSZero == nullptr))
      continue;

    Register IndexReg = LHSZero ? CmpLHS : CmpRHS;
    Register CountReg = LHSZero ? CmpRHS : CmpLHS;
    MachineInstr *IndexZeroDef = LHSZero ? LHSZero : RHSZero;
    if (!isDReg(IndexReg) || !isDReg(CountReg) ||
        regsOverlap(TRI, IndexReg, CountReg))
      continue;

    SmallPtrSet<MachineBasicBlock *, 8> Visiting;
    if (Exit) {
      if (!regDeadFromBlockStartInCFG(*Exit, IndexReg, TRI, Visiting))
        continue;
      Visiting.clear();
      if (!regDeadFromBlockStartInCFG(*Exit, CountReg, TRI, Visiting))
        continue;
      Visiting.clear();
      if (!regDeadFromBlockStartInCFG(*Exit, Bedrock::FLAGS, TRI, Visiting))
        continue;
    }

    CollectInstrs(*Body, BodyInstrs);
    if (BodyInstrs.size() != 5 && BodyInstrs.size() != 7 &&
        BodyInstrs.size() != 8)
      continue;
    if (!MatchJmp(*BodyInstrs.back(), Header))
      continue;

    DebugLoc DL = CmpI->getDebugLoc();
    bool DidBuild = false;
    Register TmpA = Register();
    Register TmpB = Register();

    if (BodyInstrs.size() == 7) {
      Register DstTmp, SrcTmp;
      if (!MatchBaseCopy(*BodyInstrs[0], DstTmp, Bedrock::A0) ||
          !MatchAddIndex(*BodyInstrs[1], DstTmp, IndexReg) ||
          !MatchBaseCopy(*BodyInstrs[2], SrcTmp, Bedrock::A1) ||
          !MatchAddIndex(*BodyInstrs[3], SrcTmp, IndexReg) ||
          BodyInstrs[4]->getOpcode() != Bedrock::MOV8mm ||
          !MatchZeroOffsetMM(*BodyInstrs[4]) ||
          !MatchIncOne(*BodyInstrs[5], IndexReg) ||
          hasOrderedMemOperand(*BodyInstrs[4]))
        continue;

      Register LoadBase = BodyInstrs[4]->getOperand(0).getReg();
      Register StoreBase = BodyInstrs[4]->getOperand(2).getReg();
      if (!regsOverlap(TRI, LoadBase, SrcTmp) ||
          !regsOverlap(TRI, StoreBase, DstTmp) ||
          regsOverlap(TRI, DstTmp, CountReg) ||
          regsOverlap(TRI, SrcTmp, CountReg))
        continue;

      if (Exit) {
        Visiting.clear();
        if (!regDeadFromBlockStartInCFG(*Exit, DstTmp, TRI, Visiting))
          continue;
        Visiting.clear();
        if (!regDeadFromBlockStartInCFG(*Exit, SrcTmp, TRI, Visiting))
          continue;
      }

      BuildMI(*Header, CmpI, DL, TII.get(Bedrock::MOV64rr), DstTmp)
          .addReg(Bedrock::A0);
      BuildMI(*Header, CmpI, DL, TII.get(Bedrock::MOV64rr), SrcTmp)
          .addReg(Bedrock::A1);
      MachineInstrBuilder Rep =
          BuildMI(*Header, CmpI, DL, TII.get(Bedrock::REPMOV8mmpostboth64),
                  CountReg)
              .addReg(CountReg)
              .addReg(SrcTmp)
              .addImm(Bedrock::UpdatePostInc)
              .addReg(DstTmp)
              .addImm(Bedrock::UpdatePostInc);
      Rep.cloneMemRefs(*BodyInstrs[4]);
      TmpA = DstTmp;
      TmpB = SrcTmp;
      DidBuild = true;
    } else if (BodyInstrs.size() == 8) {
      Register DstTmp, SrcTmp;
      if (!MatchBaseCopy(*BodyInstrs[0], DstTmp, Bedrock::A0) ||
          !MatchAddIndex(*BodyInstrs[1], DstTmp, IndexReg) ||
          !MatchBaseCopy(*BodyInstrs[2], SrcTmp, Bedrock::A1) ||
          !MatchAddIndex(*BodyInstrs[3], SrcTmp, IndexReg) ||
          BodyInstrs[4]->getOpcode() != Bedrock::MOV8rm ||
          !MatchZeroOffsetMem(*BodyInstrs[4]) ||
          BodyInstrs[5]->getOpcode() != Bedrock::MOV8mr ||
          !MatchZeroOffsetMem(*BodyInstrs[5]) ||
          !MatchIncOne(*BodyInstrs[6], IndexReg) ||
          hasOrderedMemOperand(*BodyInstrs[4]) ||
          hasOrderedMemOperand(*BodyInstrs[5]))
        continue;

      Register LoadDst = BodyInstrs[4]->getOperand(0).getReg();
      Register LoadBase = BodyInstrs[4]->getOperand(1).getReg();
      Register StoreSrc = BodyInstrs[5]->getOperand(0).getReg();
      Register StoreBase = BodyInstrs[5]->getOperand(1).getReg();
      if (!isDReg(LoadDst) || !regsOverlap(TRI, LoadBase, SrcTmp) ||
          !regsOverlap(TRI, StoreSrc, LoadDst) ||
          !regsOverlap(TRI, StoreBase, DstTmp) ||
          regsOverlap(TRI, LoadDst, CountReg) ||
          regsOverlap(TRI, LoadDst, IndexReg) ||
          regsOverlap(TRI, DstTmp, CountReg) ||
          regsOverlap(TRI, SrcTmp, CountReg))
        continue;

      if (Exit) {
        Visiting.clear();
        if (!regDeadFromBlockStartInCFG(*Exit, DstTmp, TRI, Visiting))
          continue;
        Visiting.clear();
        if (!regDeadFromBlockStartInCFG(*Exit, SrcTmp, TRI, Visiting))
          continue;
      }

      BuildMI(*Header, CmpI, DL, TII.get(Bedrock::MOV64rr), DstTmp)
          .addReg(Bedrock::A0);
      BuildMI(*Header, CmpI, DL, TII.get(Bedrock::MOV64rr), SrcTmp)
          .addReg(Bedrock::A1);
      MachineInstrBuilder Rep =
          BuildMI(*Header, CmpI, DL, TII.get(Bedrock::REPMOV8mmpostboth64),
                  CountReg)
              .addReg(CountReg)
              .addReg(SrcTmp)
              .addImm(Bedrock::UpdatePostInc)
              .addReg(DstTmp)
              .addImm(Bedrock::UpdatePostInc);
      Rep.cloneMergedMemRefs({BodyInstrs[4], BodyInstrs[5]});
      TmpA = DstTmp;
      TmpB = SrcTmp;
      DidBuild = true;
    } else {
      Register DstTmp;
      if (!MatchBaseCopy(*BodyInstrs[0], DstTmp, Bedrock::A0) ||
          !MatchAddIndex(*BodyInstrs[1], DstTmp, IndexReg) ||
          BodyInstrs[2]->getOpcode() != Bedrock::MOV8mr ||
          !MatchZeroOffsetMem(*BodyInstrs[2]) ||
          !MatchIncOne(*BodyInstrs[3], IndexReg) ||
          hasOrderedMemOperand(*BodyInstrs[2]))
        continue;

      Register StoreSrc = BodyInstrs[2]->getOperand(0).getReg();
      Register StoreBase = BodyInstrs[2]->getOperand(1).getReg();
      if (!isDReg(StoreSrc) || !regsOverlap(TRI, StoreBase, DstTmp) ||
          regsOverlap(TRI, StoreSrc, CountReg) ||
          regsOverlap(TRI, DstTmp, CountReg))
        continue;

      if (Exit) {
        Visiting.clear();
        if (!regDeadFromBlockStartInCFG(*Exit, DstTmp, TRI, Visiting))
          continue;
      }

      BuildMI(*Header, CmpI, DL, TII.get(Bedrock::MOV64rr), DstTmp)
          .addReg(Bedrock::A0);
      MachineInstrBuilder Rep =
          BuildMI(*Header, CmpI, DL, TII.get(Bedrock::REPMOV8postmr64),
                  CountReg)
              .addReg(CountReg)
              .addReg(StoreSrc)
              .addReg(DstTmp)
              .addImm(Bedrock::UpdatePostInc);
      Rep.cloneMemRefs(*BodyInstrs[2]);
      TmpA = DstTmp;
      DidBuild = true;
    }

    if (!DidBuild)
      continue;

    CmpI->eraseFromParent();
    BranchI->eraseFromParent();
    if (JmpI)
      JmpI->eraseFromParent();
    IndexZeroDef->eraseFromParent();

    removeAllSuccessors(*Header);
    if (Exit)
      Header->addSuccessor(Exit);

    eraseAllNonDebugInstrs(*Body);
    removeAllSuccessors(*Body);
    MachineBasicBlock *MaybeReturnBlock = nullptr;
    MachineFunction::iterator NextBody = std::next(Body->getIterator());
    if (RetI && NextBody != MF.end() && IsReturnOnlyUnreferenced(*NextBody))
      MaybeReturnBlock = &*NextBody;
    Body->eraseFromParent();
    if (MaybeReturnBlock)
      MaybeReturnBlock->eraseFromParent();

    MachineFunction::iterator Next = std::next(Header->getIterator());
    if (Exit && (Next == MF.end() || &*Next != Exit))
      BuildMI(*Header, Header->end(), DL, TII.get(Bedrock::JMP)).addMBB(Exit);

    removeRegLiveInsWithoutUses(MF, IndexReg, TRI);
    removeRegLiveInsWithoutUses(MF, TmpA, TRI);
    if (TmpB != Register())
      removeRegLiveInsWithoutUses(MF, TmpB, TRI);
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldScanUntilZeroRepne(MachineFunction &MF) const {
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  SmallVector<MachineBasicBlock *, 8> Blocks;
  for (MachineBasicBlock &MBB : MF)
    Blocks.push_back(&MBB);

  for (MachineBasicBlock *Body : Blocks) {
    if (!Body || Body->empty())
      continue;

    MachineBasicBlock::iterator LoadI = Body->begin();
    while (LoadI != Body->end() && LoadI->isDebugInstr())
      ++LoadI;
    if (LoadI == Body->end() || LoadI->getOpcode() != Bedrock::MOV32postrm ||
        LoadI->getNumOperands() < 2 || !LoadI->getOperand(0).isReg() ||
        !LoadI->getOperand(1).isReg() || hasOrderedMemOperand(*LoadI))
      continue;
    Register TmpReg = LoadI->getOperand(0).getReg();
    Register SrcBaseReg = LoadI->getOperand(1).getReg();
    if (!isDReg(TmpReg) || !isAReg(SrcBaseReg))
      continue;

    MachineInstr *StoreI = nullptr;
    Register DstBaseReg;
    MachineBasicBlock::iterator TestI = nextNonDebug(LoadI, *Body);
    if (TestI != Body->end() && TestI->getOpcode() == Bedrock::MOV32postmr) {
      if (TestI->getNumOperands() < 2 || !TestI->getOperand(0).isReg() ||
          !TestI->getOperand(1).isReg() || hasOrderedMemOperand(*TestI) ||
          !regsOverlap(TRI, TestI->getOperand(0).getReg(), TmpReg))
        continue;
      StoreI = &*TestI;
      DstBaseReg = TestI->getOperand(1).getReg();
      if (!isAReg(DstBaseReg))
        continue;
      TestI = nextNonDebug(TestI, *Body);
    }

    MachineBasicBlock::iterator BranchI =
        TestI == Body->end() ? Body->end() : nextNonDebug(TestI, *Body);
    if (TestI == Body->end() || BranchI == Body->end() ||
        nextNonDebug(BranchI, *Body) != Body->end() ||
        BranchI->getOpcode() != Bedrock::JCC || BranchI->getNumOperands() < 2 ||
        !BranchI->getOperand(0).isMBB())
      continue;
    std::optional<int64_t> BodyCC = getCondCodeImm(BranchI->getOperand(1));
    if (!BodyCC)
      continue;

    bool UseRepneMov =
        TestI->getOpcode() == Bedrock::TEST32rr && *BodyCC == BedrockCC::EQ;
    bool UseRepgtCmp = !StoreI && TestI->getOpcode() == Bedrock::CMP32rr &&
                       *BodyCC == BedrockCC::GE;
    Register CmpRHSReg;
    if (UseRepneMov) {
      if (TestI->getNumOperands() < 2 || !TestI->getOperand(0).isReg() ||
          !TestI->getOperand(1).isReg() ||
          !regsOverlap(TRI, TestI->getOperand(0).getReg(), TmpReg) ||
          !regsOverlap(TRI, TestI->getOperand(1).getReg(), TmpReg))
        continue;
    } else if (UseRepgtCmp) {
      if (TestI->getNumOperands() < 2 || !TestI->getOperand(0).isReg() ||
          !TestI->getOperand(1).isReg() ||
          !regsOverlap(TRI, TestI->getOperand(0).getReg(), TmpReg))
        continue;
      CmpRHSReg = TestI->getOperand(1).getReg();
      if (!isIntReg(CmpRHSReg))
        continue;
    } else {
      continue;
    }

    MachineBasicBlock *Cleanup = BranchI->getOperand(0).getMBB();
    if (!Cleanup)
      continue;

    MachineBasicBlock *Latch = nullptr;
    for (MachineBasicBlock *Succ : Body->successors()) {
      if (Succ == Cleanup)
        continue;
      if (Latch)
        Latch = nullptr;
      else
        Latch = Succ;
    }
    if (!Latch)
      continue;

    MachineBasicBlock::iterator DecI = Latch->begin();
    while (DecI != Latch->end() && DecI->isDebugInstr())
      ++DecI;
    MachineBasicBlock::iterator IncI =
        DecI == Latch->end() ? Latch->end() : nextNonDebug(DecI, *Latch);
    MachineBasicBlock::iterator JmpI =
        IncI == Latch->end() ? Latch->end() : nextNonDebug(IncI, *Latch);
    if (DecI == Latch->end() || IncI == Latch->end() || JmpI == Latch->end() ||
        nextNonDebug(JmpI, *Latch) != Latch->end() ||
        JmpI->getOpcode() != Bedrock::JMP || JmpI->getNumOperands() < 1 ||
        !JmpI->getOperand(0).isMBB())
      continue;
    MachineBasicBlock *Header = JmpI->getOperand(0).getMBB();
    if (!Header)
      continue;

    Register CountReg;
    unsigned CmpOpcode = 0;
    if (!isIncDecRegInstr(*DecI, CountReg, CmpOpcode, TRI) ||
        (DecI->getOpcode() != Bedrock::DEC32r &&
         DecI->getOpcode() != Bedrock::DEC64r) ||
        !isDReg(CountReg))
      continue;

    Register IndexReg;
    unsigned IncCmpOpcode = 0;
    if (!isIncDecRegInstr(*IncI, IndexReg, IncCmpOpcode, TRI) ||
        IncI->getOpcode() != Bedrock::INC32r || !isDReg(IndexReg) ||
        regsOverlap(TRI, IndexReg, Bedrock::D0) ||
        regsOverlap(TRI, IndexReg, TmpReg) ||
        (UseRepgtCmp && regsOverlap(TRI, IndexReg, CmpRHSReg)))
      continue;

    if (!isPositiveCountPretest(*Header, *Body, CountReg, TRI) &&
        !isZeroExitCountPretest(*Header, *Body, CountReg, TRI))
      continue;

    MachineBasicBlock *Preheader =
        findHeaderPredecessorExcluding(*Header, *Latch);
    MachineInstr *ZeroDef =
        Preheader ? findLeadingZeroRegDef(*Preheader, IndexReg, TRI) : nullptr;
    if (!ZeroDef)
      continue;

    MachineBasicBlock::iterator RetCopyI = Cleanup->begin();
    while (RetCopyI != Cleanup->end() && RetCopyI->isDebugInstr())
      ++RetCopyI;
    MachineBasicBlock::iterator RetI = RetCopyI == Cleanup->end()
                                           ? Cleanup->end()
                                           : nextNonDebug(RetCopyI, *Cleanup);
    if (RetCopyI == Cleanup->end() || RetI == Cleanup->end() ||
        nextNonDebug(RetI, *Cleanup) != Cleanup->end() ||
        RetCopyI->getOpcode() != Bedrock::MOV32rr ||
        RetCopyI->getNumOperands() < 2 || !RetCopyI->getOperand(0).isReg() ||
        !RetCopyI->getOperand(1).isReg() ||
        !regsOverlap(TRI, RetCopyI->getOperand(0).getReg(), Bedrock::D0) ||
        !regsOverlap(TRI, RetCopyI->getOperand(1).getReg(), IndexReg) ||
        RetI->getOpcode() != Bedrock::RET)
      continue;

    DebugLoc DL = LoadI->getDebugLoc();
    BuildMI(*Body, LoadI, DL, TII.get(Bedrock::MOV32rr), IndexReg)
        .addReg(CountReg);
    if (StoreI) {
      MachineInstrBuilder Rep =
          BuildMI(*Body, LoadI, DL, TII.get(Bedrock::REPNEMOV32mmpostboth),
                  IndexReg)
              .addReg(IndexReg)
              .addReg(SrcBaseReg)
              .addImm(Bedrock::UpdatePostInc)
              .addReg(DstBaseReg)
              .addImm(Bedrock::UpdatePostInc);
      Rep.cloneMergedMemRefs({&*LoadI, StoreI});
    } else if (UseRepgtCmp) {
      MachineInstrBuilder Rep =
          BuildMI(*Body, LoadI, DL, TII.get(Bedrock::REPGTCMP32postrm),
                  IndexReg)
              .addReg(IndexReg)
              .addReg(SrcBaseReg)
              .addImm(Bedrock::UpdatePostInc)
              .addReg(CmpRHSReg);
      Rep.cloneMemRefs(*LoadI);
    } else {
      MachineInstrBuilder Rep =
          BuildMI(*Body, LoadI, DL, TII.get(Bedrock::REPNEMOV32postrm))
              .addReg(IndexReg, RegState::Define)
              .addReg(TmpReg, RegState::Define)
              .addReg(IndexReg)
              .addReg(SrcBaseReg)
              .addImm(Bedrock::UpdatePostInc);
      Rep.cloneMemRefs(*LoadI);
    }
    if (!regsOverlap(TRI, CountReg, Bedrock::D0))
      BuildMI(*Body, LoadI, DL, TII.get(Bedrock::MOV32rr), Bedrock::D0)
          .addReg(CountReg);
    BuildMI(*Body, LoadI, DL, TII.get(Bedrock::SUB32rr), Bedrock::D0)
        .addReg(Bedrock::D0)
        .addReg(IndexReg);
    BuildMI(*Body, LoadI, DL, TII.get(Bedrock::RET));

    if (StoreI)
      StoreI->eraseFromParent();
    LoadI->eraseFromParent();
    TestI->eraseFromParent();
    BranchI->eraseFromParent();
    removeAllSuccessors(*Body);

    BuildMI(*Cleanup, RetCopyI, RetCopyI->getDebugLoc(),
            TII.get(Bedrock::CLR64r), Bedrock::D0);
    RetCopyI->eraseFromParent();
    ZeroDef->eraseFromParent();

    eraseAllNonDebugInstrs(*Latch);
    removeAllSuccessors(*Latch);
    Latch->eraseFromParent();
    return true;
  }

  return false;
}

