//===-- BedrockPeepholeArithmetic.cpp - Bedrock peepholes -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockPeephole.h"

bool BedrockPeephole::foldCondZextAddToInc(MachineFunction &MF) const {
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  auto FirstNonDebug = [](MachineBasicBlock &MBB) {
    auto I = MBB.begin();
    while (I != MBB.end() && I->isDebugInstr())
      ++I;
    return I;
  };

  bool Changed = false;
  bool LocalChanged = true;
  while (LocalChanged) {
    LocalChanged = false;
    for (MachineBasicBlock &CmpBB : MF) {
      MachineBasicBlock::iterator BranchI = CmpBB.getLastNonDebugInstr();
      if (BranchI == CmpBB.end() || BranchI->getOpcode() != Bedrock::JCC ||
          BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB())
        continue;

      MachineBasicBlock::iterator TrueMoveI = prevNonDebug(BranchI, CmpBB);
      if (TrueMoveI == CmpBB.end() ||
          (TrueMoveI->getOpcode() != Bedrock::MOV32rr &&
           TrueMoveI->getOpcode() != Bedrock::MOV64rr) ||
          TrueMoveI->getNumOperands() < 2 ||
          !TrueMoveI->getOperand(0).isReg() ||
          !TrueMoveI->getOperand(1).isReg())
        continue;

      MachineBasicBlock::iterator CmpI = prevNonDebug(TrueMoveI, CmpBB);
      if (CmpI == CmpBB.end() || (CmpI->getOpcode() != Bedrock::CMP32rr &&
                                  CmpI->getOpcode() != Bedrock::CMP64rr))
        continue;

      std::optional<int64_t> CC = getCondCodeImm(BranchI->getOperand(1));
      std::optional<int64_t> InverseCC =
          CC ? invertCondCode(*CC) : std::nullopt;
      if (!InverseCC)
        continue;

      MachineBasicBlock *MergeBB = BranchI->getOperand(0).getMBB();
      MachineFunction::iterator FallthroughI = std::next(CmpBB.getIterator());
      if (FallthroughI == MF.end())
        continue;
      MachineBasicBlock *IncBB = &*FallthroughI;
      MachineFunction::iterator MergeI = std::next(IncBB->getIterator());
      if (MergeI == MF.end() || &*MergeI != MergeBB)
        continue;
      if (!CmpBB.isSuccessor(IncBB) || !CmpBB.isSuccessor(MergeBB) ||
          !IncBB->isSuccessor(MergeBB))
        continue;

      MachineBasicBlock::iterator FalseMoveI = FirstNonDebug(*IncBB);
      if (FalseMoveI == IncBB->end() ||
          nextNonDebug(FalseMoveI, *IncBB) != IncBB->end() ||
          (FalseMoveI->getOpcode() != Bedrock::MOV32rr &&
           FalseMoveI->getOpcode() != Bedrock::MOV64rr) ||
          FalseMoveI->getNumOperands() < 2 ||
          !FalseMoveI->getOperand(0).isReg() ||
          !FalseMoveI->getOperand(1).isReg())
        continue;

      MachineBasicBlock::iterator AddI = FirstNonDebug(*MergeBB);
      if (AddI == MergeBB->end() ||
          (AddI->getOpcode() != Bedrock::ADD32rr &&
           AddI->getOpcode() != Bedrock::ADD64rr) ||
          AddI->getNumOperands() < 3 || !AddI->getOperand(0).isReg() ||
          !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg())
        continue;

      Register SelectReg = TrueMoveI->getOperand(0).getReg();
      Register TrueReg = TrueMoveI->getOperand(1).getReg();
      Register FalseReg = FalseMoveI->getOperand(1).getReg();
      Register Acc = AddI->getOperand(0).getReg();
      if (!regsOverlap(TRI, FalseMoveI->getOperand(0).getReg(), SelectReg) ||
          !regsOverlap(TRI, AddI->getOperand(1).getReg(), Acc) ||
          !regsOverlap(TRI, AddI->getOperand(2).getReg(), SelectReg) ||
          regsOverlap(TRI, Acc, SelectReg) || regsOverlap(TRI, Acc, TrueReg) ||
          regsOverlap(TRI, Acc, FalseReg))
        continue;

      MachineInstr *TrueDef =
          findLastConstDefBefore(*TrueMoveI, TrueReg, 1, TRI);
      MachineInstr *FalseDef =
          findLastConstDefBefore(*FalseMoveI, FalseReg, 0, TRI);
      if (!TrueDef || !FalseDef)
        continue;
      if (!regDeadAfterInCFG(std::next(FalseMoveI), *IncBB, Bedrock::FLAGS,
                             TRI) ||
          !regDeadAfterInCFG(std::next(AddI), *MergeBB, SelectReg, TRI))
        continue;

      unsigned IncOpcode = AddI->getOpcode() == Bedrock::ADD32rr
                               ? Bedrock::INC32r
                               : Bedrock::INC64r;
      BuildMI(*IncBB, FalseMoveI, FalseMoveI->getDebugLoc(), TII.get(IncOpcode),
              Acc)
          .addReg(Acc);

      setCondCodeImm(BranchI->getOperand(1), *InverseCC);
      AddI->eraseFromParent();
      TrueMoveI->eraseFromParent();
      FalseMoveI->eraseFromParent();

      replaceZeroRegCopiesAfterDef(*FalseDef, FalseReg, MF, TII, TRI);

      if (TrueDef && regUnusedAfterInstrInLayout(*TrueDef, TrueReg, TRI))
        TrueDef->eraseFromParent();
      if (FalseDef && FalseDef != TrueDef &&
          regUnusedAfterInstrInLayout(*FalseDef, FalseReg, TRI))
        FalseDef->eraseFromParent();

      removeRegLiveInsWithoutUses(MF, SelectReg, TRI);
      removeRegLiveInsWithoutUses(MF, TrueReg, TRI);
      removeRegLiveInsWithoutUses(MF, FalseReg, TRI);

      Changed = true;
      LocalChanged = true;
      break;
    }
  }
  return Changed;
}

bool BedrockPeephole::foldImmCmp(MachineBasicBlock &MBB,
                                     MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &MovImm = *I;
    if (MovImm.isDebugInstr()) {
      ++I;
      continue;
    }

    Register ImmReg;
    int64_t Imm = 0;
    if (!isMovImmForCmp(MovImm, ImmReg, Imm)) {
      ++I;
      continue;
    }

    auto CmpI = nextNonDebug(I, MBB);
    if (CmpI == MBB.end() || CmpI->getNumOperands() < 2 ||
        !CmpI->getOperand(0).isReg() || !CmpI->getOperand(1).isReg()) {
      ++I;
      continue;
    }

    if (MovImm.getOpcode() == Bedrock::MOV32ri &&
        CmpI->getOpcode() == Bedrock::CMP32rr && Imm == -1 &&
        regsOverlap(TRI, CmpI->getOperand(1).getReg(), ImmReg) &&
        !regsOverlap(TRI, CmpI->getOperand(0).getReg(), ImmReg) &&
        (operandIsKill(*CmpI, ImmReg, TRI) ||
         regUnusedAfterInCFG(std::next(CmpI), MBB, ImmReg, TRI))) {
      auto BranchI = nextNonDebug(CmpI, MBB);
      if (BranchI != MBB.end() && BranchI->getOpcode() == Bedrock::JCC &&
          BranchI->getNumOperands() >= 2) {
        std::optional<int64_t> CC = getCondCodeImm(BranchI->getOperand(1));
        if (CC && (*CC == BedrockCC::LE || *CC == BedrockCC::GT)) {
          Register Reg = CmpI->getOperand(0).getReg();
          BuildMI(MBB, MovImm.getIterator(), CmpI->getDebugLoc(),
                  TII.get(Bedrock::TEST32rr))
              .addReg(Reg)
              .addReg(Reg);
          setCondCodeImm(BranchI->getOperand(1),
                         *CC == BedrockCC::LE ? BedrockCC::LT : BedrockCC::GE);
          MovImm.eraseFromParent();
          CmpI->eraseFromParent();
          I = MBB.begin();
          Changed = true;
          continue;
        }
      }
    }

    unsigned CmpImmOpcode =
        getProfitableCmpImmOpcode(MovImm.getOpcode(), CmpI->getOpcode(), Imm);
    if (CmpImmOpcode == 0 ||
        !regsOverlap(TRI, CmpI->getOperand(1).getReg(), ImmReg) ||
        regsOverlap(TRI, CmpI->getOperand(0).getReg(), ImmReg) ||
        (!operandIsKill(*CmpI, ImmReg, TRI) &&
         !regDeadAfter(std::next(CmpI), MBB, ImmReg, TRI))) {
      ++I;
      continue;
    }

    BuildMI(MBB, MovImm.getIterator(), CmpI->getDebugLoc(),
            TII.get(CmpImmOpcode))
        .addReg(CmpI->getOperand(0).getReg())
        .addImm(Imm);
    MovImm.eraseFromParent();
    CmpI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldCmpOneBranch(MachineBasicBlock &MBB,
                                           MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Cmp = *I;
    if (Cmp.isDebugInstr()) {
      ++I;
      continue;
    }

    if (Cmp.getOpcode() != Bedrock::CMP32ri || Cmp.getNumOperands() < 2 ||
        !Cmp.getOperand(0).isReg() || !Cmp.getOperand(1).isImm() ||
        Cmp.getOperand(1).getImm() != 1) {
      ++I;
      continue;
    }

    Register Reg = Cmp.getOperand(0).getReg();
    if (!isDReg(Reg)) {
      ++I;
      continue;
    }

    auto BranchI = nextNonDebug(I, MBB);
    if (BranchI == MBB.end() || BranchI->getOpcode() != Bedrock::JCC ||
        BranchI->getNumOperands() < 2) {
      ++I;
      continue;
    }

    std::optional<int64_t> CC = getCondCodeImm(BranchI->getOperand(1));
    if (!CC || (*CC != BedrockCC::LT && *CC != BedrockCC::GE)) {
      ++I;
      continue;
    }

    BuildMI(MBB, Cmp.getIterator(), Cmp.getDebugLoc(),
            TII.get(Bedrock::TEST32rr))
        .addReg(Reg)
        .addReg(Reg);
    setCondCodeImm(BranchI->getOperand(1),
                   *CC == BedrockCC::LT ? BedrockCC::LE : BedrockCC::GT);
    Cmp.eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldImmMul(MachineBasicBlock &MBB,
                                     MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &MovImm = *I;
    if (MovImm.isDebugInstr()) {
      ++I;
      continue;
    }

    Register ImmReg;
    int64_t Imm = 0;
    if (!isMov32Imm(MovImm, ImmReg, Imm)) {
      ++I;
      continue;
    }

    auto MulI = nextNonDebug(I, MBB);
    if (MulI == MBB.end() || MulI->getOpcode() != Bedrock::MULU32rr ||
        MulI->getNumOperands() < 3 || !MulI->getOperand(0).isReg() ||
        !MulI->getOperand(1).isReg() || !MulI->getOperand(2).isReg()) {
      ++I;
      continue;
    }

    Register Dst = MulI->getOperand(0).getReg();
    Register LHS = MulI->getOperand(1).getReg();
    Register RHS = MulI->getOperand(2).getReg();
    Register Acc = Register();
    if (regsOverlap(TRI, RHS, ImmReg) && regsOverlap(TRI, Dst, LHS) &&
        !regsOverlap(TRI, Dst, ImmReg)) {
      Acc = LHS;
    } else if (regsOverlap(TRI, LHS, ImmReg) && regsOverlap(TRI, Dst, RHS) &&
               !regsOverlap(TRI, Dst, ImmReg)) {
      Acc = RHS;
    } else {
      ++I;
      continue;
    }

    if (!operandIsKill(*MulI, ImmReg, TRI) &&
        !regDeadAfter(std::next(MulI), MBB, ImmReg, TRI)) {
      ++I;
      continue;
    }

    BuildMI(MBB, MovImm.getIterator(), MulI->getDebugLoc(),
            TII.get(Bedrock::MULU32ri), Dst)
        .addReg(Acc, getKillRegState(operandIsKill(*MulI, Acc, TRI)))
        .addImm(Imm);
    MovImm.eraseFromParent();
    MulI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &MovImm = *I;
    if (MovImm.isDebugInstr()) {
      ++I;
      continue;
    }

    Register ImmReg;
    int64_t Imm = 0;
    if (!isMov32Imm(MovImm, ImmReg, Imm) || Imm != 3) {
      ++I;
      continue;
    }

    auto MulI = nextNonDebug(I, MBB);
    if (MulI == MBB.end() || MulI->getOpcode() != Bedrock::MULU32rr ||
        MulI->getNumOperands() < 3 || !MulI->getOperand(0).isReg() ||
        !MulI->getOperand(1).isReg() || !MulI->getOperand(2).isReg()) {
      ++I;
      continue;
    }

    Register Dst = MulI->getOperand(0).getReg();
    Register LHS = MulI->getOperand(1).getReg();
    Register RHS = MulI->getOperand(2).getReg();
    if (!regsOverlap(TRI, Dst, ImmReg) || !regsOverlap(TRI, LHS, ImmReg) ||
        regsOverlap(TRI, RHS, ImmReg) || !isDReg(Dst) || !isDReg(RHS) ||
        (instrDefinesReg(*MulI, Bedrock::FLAGS, TRI) &&
         !regDefDeadOrDeadAfter(MulI, MBB, Bedrock::FLAGS, TRI))) {
      ++I;
      continue;
    }

    bool KillRHS = operandIsKill(*MulI, RHS, TRI);
    BuildMI(MBB, MovImm.getIterator(), MovImm.getDebugLoc(),
            TII.get(Bedrock::MOV32rr), Dst)
        .addReg(RHS);
    BuildMI(MBB, MovImm.getIterator(), MulI->getDebugLoc(),
            TII.get(Bedrock::ADD32rr), Dst)
        .addReg(Dst)
        .addReg(RHS);
    BuildMI(MBB, MovImm.getIterator(), MulI->getDebugLoc(),
            TII.get(Bedrock::ADD32rr), Dst)
        .addReg(Dst)
        .addReg(RHS, getKillRegState(KillRHS));
    MovImm.eraseFromParent();
    MulI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldSmallConstMultiply(MachineFunction &MF) const {
  SmallVector<StackConstStore, 8> Slots;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  bool UseImmediateMul = MF.getFunction().hasMinSize();

  collectStackConstStores(MF, TRI, Slots);

  bool Changed = false;
  for (StackConstStore &Slot : Slots) {
    if (!Slot.Store || Slot.Size != 4 ||
        (!UseImmediateMul && !getShiftForSmallConstMul(Slot.Value)))
      continue;

    SmallVector<StackConstMulReplacement, 4> Replacements;
    SmallPtrSet<MachineInstr *, 8> Ignored;
    Ignored.insert(Slot.ImmDef);
    Ignored.insert(Slot.Store);

    bool Invalid = false;
    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : MBB) {
        if (MI.isDebugInstr() || Ignored.contains(&MI))
          continue;
        if (!instrHasOverlappingSPMemRange(MI, Slot.Offset, 4, TRI))
          continue;

        StackConstMulReplacement Replacement;
        if (!isReplaceableStackConstMul(Slot, MI, Replacement, UseImmediateMul,
                                        TRI)) {
          Invalid = true;
          break;
        }
        if (Replacement.Load)
          Ignored.insert(Replacement.Load);
        Ignored.insert(Replacement.Mul);
        Replacements.push_back(Replacement);
      }
      if (Invalid)
        break;
    }

    if (Invalid || Replacements.empty() ||
        stackRangeUsedOutsideSet(MF, Slot.Offset, 4, Ignored, TRI))
      continue;

    for (const StackConstMulReplacement &Replacement : Replacements) {
      MachineInstr *InsertBefore =
          Replacement.Load ? Replacement.Load : Replacement.Mul;
      MachineBasicBlock &MBB = *InsertBefore->getParent();
      MachineBasicBlock::iterator Insert = InsertBefore->getIterator();
      DebugLoc DL = Replacement.Mul->getDebugLoc();

      if (UseImmediateMul) {
        if (!regsOverlap(TRI, Replacement.Product, Replacement.Value))
          BuildMI(MBB, Insert, DL, TII.get(Bedrock::MOV32rr),
                  Replacement.Product)
              .addReg(Replacement.Value);
        BuildMI(MBB, Insert, DL, TII.get(Bedrock::MULU32ri),
                Replacement.Product)
            .addReg(Replacement.Product)
            .addImm(Slot.Value);
      } else if (regsOverlap(TRI, Replacement.Product, Replacement.Value)) {
        BuildMI(MBB, Insert, DL, TII.get(Bedrock::MOV32rr), Replacement.Scratch)
            .addReg(Replacement.Value);
        BuildMI(MBB, Insert, DL, TII.get(Bedrock::SHL32ri), Replacement.Product)
            .addReg(Replacement.Product)
            .addImm(Replacement.Shift);
        BuildMI(MBB, Insert, DL, TII.get(Bedrock::ADD32rr), Replacement.Product)
            .addReg(Replacement.Product)
            .addReg(Replacement.Scratch);
      } else {
        BuildMI(MBB, Insert, DL, TII.get(Bedrock::MOV32rr), Replacement.Product)
            .addReg(Replacement.Value);
        BuildMI(MBB, Insert, DL, TII.get(Bedrock::SHL32ri), Replacement.Product)
            .addReg(Replacement.Product)
            .addImm(Replacement.Shift);
        BuildMI(MBB, Insert, DL, TII.get(Bedrock::ADD32rr), Replacement.Product)
            .addReg(Replacement.Product)
            .addReg(Replacement.Value);
      }

      if (Replacement.Load)
        Replacement.Load->eraseFromParent();
      Replacement.Mul->eraseFromParent();
    }

    Slot.Store->eraseFromParent();
    Slot.ImmDef->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldMinSizeDivmodConstAccumulate(
    MachineFunction &MF) const {
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  auto Next = [](MachineBasicBlock::iterator I, MachineBasicBlock &MBB) {
    return nextNonDebug(I, MBB);
  };
  auto IsSameReg = [&](const MachineOperand &MO, Register Reg) {
    return MO.isReg() && regsOverlap(TRI, MO.getReg(), Reg);
  };

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(); I != MBB.end();) {
      MachineInstr &QuotCopy = *I;
      auto TryFoldImmediateRemMul = [&]() {
        if (QuotCopy.isDebugInstr() ||
            (QuotCopy.getOpcode() != Bedrock::MOV32rr &&
             QuotCopy.getOpcode() != Bedrock::MOV64rr) ||
            QuotCopy.getNumOperands() < 2 || !QuotCopy.getOperand(0).isReg() ||
            !QuotCopy.getOperand(1).isReg())
          return false;

        Register QuotReg = QuotCopy.getOperand(0).getReg();
        Register RemReg = QuotCopy.getOperand(1).getReg();
        auto DivI = Next(I, MBB);
        auto RemMulI = DivI == MBB.end() ? MBB.end() : Next(DivI, MBB);
        auto MAddI = RemMulI == MBB.end() ? MBB.end() : Next(RemMulI, MBB);
        auto AddAccI = MAddI == MBB.end() ? MBB.end() : Next(MAddI, MBB);
        auto DecI = AddAccI == MBB.end() ? MBB.end() : Next(AddAccI, MBB);
        auto IncI = DecI == MBB.end() ? MBB.end() : Next(DecI, MBB);
        if (DivI == MBB.end() || RemMulI == MBB.end() || MAddI == MBB.end() ||
            AddAccI == MBB.end() || DecI == MBB.end() || IncI == MBB.end())
          return false;

        MachineBasicBlock::iterator AddIndexI = prevNonDebug(I, MBB);
        MachineInstr &Div = *DivI;
        MachineInstr &RemMul = *RemMulI;
        MachineInstr &MAdd = *MAddI;
        MachineInstr &AddAcc = *AddAccI;
        MachineInstr &Inc = *IncI;

        if (Div.getOpcode() != Bedrock::DIVMODS32rr ||
            Div.getNumOperands() < 4 ||
            !IsSameReg(Div.getOperand(0), QuotReg) ||
            !IsSameReg(Div.getOperand(1), RemReg) ||
            !IsSameReg(Div.getOperand(2), QuotReg) ||
            !Div.getOperand(3).isReg())
          return false;
        if (RemMul.getOpcode() != Bedrock::MULU32ri ||
            RemMul.getNumOperands() < 3 ||
            !IsSameReg(RemMul.getOperand(0), RemReg) ||
            !IsSameReg(RemMul.getOperand(1), RemReg) ||
            !RemMul.getOperand(2).isImm() || RemMul.getOperand(2).getImm() != 5)
          return false;
        if (MAdd.getOpcode() != Bedrock::MADD32rrr ||
            MAdd.getNumOperands() < 4 || !MAdd.getOperand(0).isReg() ||
            !MAdd.getOperand(1).isReg() || !MAdd.getOperand(2).isReg() ||
            !MAdd.getOperand(3).isReg() ||
            !regsOverlap(TRI, MAdd.getOperand(0).getReg(),
                         MAdd.getOperand(1).getReg()))
          return false;

        Register Acc = MAdd.getOperand(0).getReg();
        Register ConstReg;
        if (regsOverlap(TRI, MAdd.getOperand(2).getReg(), QuotReg))
          ConstReg = MAdd.getOperand(3).getReg();
        else if (regsOverlap(TRI, MAdd.getOperand(3).getReg(), QuotReg))
          ConstReg = MAdd.getOperand(2).getReg();
        else
          return false;

        if (AddAcc.getOpcode() != Bedrock::ADD32rr ||
            AddAcc.getNumOperands() < 3 ||
            !IsSameReg(AddAcc.getOperand(0), Acc) ||
            !IsSameReg(AddAcc.getOperand(1), Acc) ||
            !IsSameReg(AddAcc.getOperand(2), RemReg))
          return false;

        Register IndexReg;
        MachineInstr *IndexZeroDef = nullptr;
        bool CanRemapIndex = false;
        if (AddIndexI != MBB.end() &&
            AddIndexI->getOpcode() == Bedrock::ADD32rr &&
            AddIndexI->getNumOperands() >= 3 &&
            IsSameReg(AddIndexI->getOperand(0), RemReg) &&
            IsSameReg(AddIndexI->getOperand(1), RemReg) &&
            AddIndexI->getOperand(2).isReg() &&
            Inc.getOpcode() == Bedrock::INC32r && Inc.getNumOperands() >= 2 &&
            Inc.getOperand(0).isReg() && Inc.getOperand(1).isReg() &&
            regsOverlap(TRI, Inc.getOperand(0).getReg(),
                        Inc.getOperand(1).getReg()) &&
            regsOverlap(TRI, AddIndexI->getOperand(2).getReg(),
                        Inc.getOperand(0).getReg())) {
          IndexReg = AddIndexI->getOperand(2).getReg();
          IndexZeroDef = findLastConstDefBefore(*AddIndexI, IndexReg, 0, TRI);
          SmallPtrSet<const MachineInstr *, 4> IgnoredIndex;
          if (IndexZeroDef) {
            IgnoredIndex.insert(IndexZeroDef);
            IgnoredIndex.insert(&*AddIndexI);
            IgnoredIndex.insert(&Inc);
            CanRemapIndex =
                !regTouchedOutsideInstrs(MF, IndexReg, IgnoredIndex, TRI);
          }
        }

        MachineInstr *ConstDef = findLastConstDefBefore(MAdd, ConstReg, 3, TRI);
        if (!ConstDef)
          return false;
        SmallPtrSet<const MachineInstr *, 4> IgnoredConst;
        IgnoredConst.insert(ConstDef);
        IgnoredConst.insert(&MAdd);
        if (regTouchedOutsideInstrs(MF, ConstReg, IgnoredConst, TRI))
          return false;

        if (regsOverlap(TRI, ConstReg, Acc) ||
            regsOverlap(TRI, ConstReg, RemReg) ||
            regsOverlap(TRI, ConstReg, Div.getOperand(3).getReg()))
          return false;
        Register NewQuot = ConstReg;

        QuotCopy.setDesc(TII.get(Bedrock::MOV32rr));
        QuotCopy.getOperand(0).setReg(NewQuot);
        Div.getOperand(0).setReg(NewQuot);
        Div.getOperand(2).setReg(NewQuot);

        for (unsigned N = 0; N != 3; ++N)
          BuildMI(MBB, MAdd.getIterator(), MAdd.getDebugLoc(),
                  TII.get(Bedrock::ADD32rr), Acc)
              .addReg(Acc)
              .addReg(NewQuot);

        SmallPtrSet<const MachineInstr *, 1> EmptyIgnored;
        if (CanRemapIndex &&
            !regTouchedOutsideInstrs(MF, Bedrock::D3, EmptyIgnored, TRI)) {
          IndexZeroDef->getOperand(0).setReg(Bedrock::D3);
          AddIndexI->getOperand(2).setReg(Bedrock::D3);
          Inc.getOperand(0).setReg(Bedrock::D3);
          Inc.getOperand(1).setReg(Bedrock::D3);
          removeRegLiveInsWithoutUses(MF, IndexReg, TRI);
        }

        MAdd.eraseFromParent();
        ConstDef->eraseFromParent();
        removeRegLiveInsWithoutUses(MF, QuotReg, TRI);
        removeRegLiveInsWithoutUses(MF, ConstReg, TRI);
        return true;
      };

      if (TryFoldImmediateRemMul()) {
        Changed = true;
        I = MBB.begin();
        continue;
      }

      if (QuotCopy.isDebugInstr() ||
          (QuotCopy.getOpcode() != Bedrock::MOV32rr &&
           QuotCopy.getOpcode() != Bedrock::MOV64rr) ||
          QuotCopy.getNumOperands() < 2 || !QuotCopy.getOperand(0).isReg() ||
          !QuotCopy.getOperand(1).isReg()) {
        ++I;
        continue;
      }

      Register QuotReg = QuotCopy.getOperand(0).getReg();
      Register RemReg = QuotCopy.getOperand(1).getReg();
      auto DivI = Next(I, MBB);
      auto RemCopyI = DivI == MBB.end() ? MBB.end() : Next(DivI, MBB);
      auto ShlI = RemCopyI == MBB.end() ? MBB.end() : Next(RemCopyI, MBB);
      auto AddRemI = ShlI == MBB.end() ? MBB.end() : Next(ShlI, MBB);
      auto MAddI = AddRemI == MBB.end() ? MBB.end() : Next(AddRemI, MBB);
      auto AddAccI = MAddI == MBB.end() ? MBB.end() : Next(MAddI, MBB);
      auto DecI = AddAccI == MBB.end() ? MBB.end() : Next(AddAccI, MBB);
      auto IncI = DecI == MBB.end() ? MBB.end() : Next(DecI, MBB);
      if (DivI == MBB.end() || RemCopyI == MBB.end() || ShlI == MBB.end() ||
          AddRemI == MBB.end() || MAddI == MBB.end() || AddAccI == MBB.end() ||
          DecI == MBB.end() || IncI == MBB.end()) {
        ++I;
        continue;
      }

      MachineBasicBlock::iterator AddIndexI = prevNonDebug(I, MBB);
      MachineInstr &Div = *DivI;
      MachineInstr &RemCopy = *RemCopyI;
      MachineInstr &Shl = *ShlI;
      MachineInstr &AddRem = *AddRemI;
      MachineInstr &MAdd = *MAddI;
      MachineInstr &AddAcc = *AddAccI;
      MachineInstr &Inc = *IncI;
      if (Div.getOpcode() != Bedrock::DIVMODS32rr || Div.getNumOperands() < 4 ||
          !IsSameReg(Div.getOperand(0), QuotReg) ||
          !IsSameReg(Div.getOperand(1), RemReg) ||
          !IsSameReg(Div.getOperand(2), QuotReg) ||
          !Div.getOperand(3).isReg()) {
        ++I;
        continue;
      }
      if (RemCopy.getOpcode() != Bedrock::MOV32rr ||
          RemCopy.getNumOperands() < 2 || !RemCopy.getOperand(0).isReg() ||
          !IsSameReg(RemCopy.getOperand(1), RemReg)) {
        ++I;
        continue;
      }
      Register RemScratch = RemCopy.getOperand(0).getReg();
      if (Shl.getOpcode() != Bedrock::SHL32ri || Shl.getNumOperands() < 3 ||
          !IsSameReg(Shl.getOperand(0), RemReg) ||
          !IsSameReg(Shl.getOperand(1), RemReg) || !Shl.getOperand(2).isImm() ||
          Shl.getOperand(2).getImm() != 2) {
        ++I;
        continue;
      }
      if (AddRem.getOpcode() != Bedrock::ADD32rr ||
          AddRem.getNumOperands() < 3 ||
          !IsSameReg(AddRem.getOperand(0), RemReg) ||
          !IsSameReg(AddRem.getOperand(1), RemReg) ||
          !IsSameReg(AddRem.getOperand(2), RemScratch)) {
        ++I;
        continue;
      }
      if (MAdd.getOpcode() != Bedrock::MADD32rrr || MAdd.getNumOperands() < 4 ||
          !MAdd.getOperand(0).isReg() || !MAdd.getOperand(1).isReg() ||
          !MAdd.getOperand(2).isReg() || !MAdd.getOperand(3).isReg() ||
          !regsOverlap(TRI, MAdd.getOperand(0).getReg(),
                       MAdd.getOperand(1).getReg())) {
        ++I;
        continue;
      }
      Register Acc = MAdd.getOperand(0).getReg();
      Register ConstReg;
      if (regsOverlap(TRI, MAdd.getOperand(2).getReg(), QuotReg))
        ConstReg = MAdd.getOperand(3).getReg();
      else if (regsOverlap(TRI, MAdd.getOperand(3).getReg(), QuotReg))
        ConstReg = MAdd.getOperand(2).getReg();
      else {
        ++I;
        continue;
      }
      if (AddAcc.getOpcode() != Bedrock::ADD32rr ||
          AddAcc.getNumOperands() < 3 ||
          !IsSameReg(AddAcc.getOperand(0), Acc) ||
          !IsSameReg(AddAcc.getOperand(1), Acc) ||
          !IsSameReg(AddAcc.getOperand(2), RemReg)) {
        ++I;
        continue;
      }

      Register IndexReg;
      MachineInstr *IndexZeroDef = nullptr;
      bool CanRemapIndex = false;
      if (AddIndexI != MBB.end() &&
          AddIndexI->getOpcode() == Bedrock::ADD32rr &&
          AddIndexI->getNumOperands() >= 3 &&
          IsSameReg(AddIndexI->getOperand(0), RemReg) &&
          IsSameReg(AddIndexI->getOperand(1), RemReg) &&
          AddIndexI->getOperand(2).isReg() &&
          Inc.getOpcode() == Bedrock::INC32r && Inc.getNumOperands() >= 2 &&
          Inc.getOperand(0).isReg() && Inc.getOperand(1).isReg() &&
          regsOverlap(TRI, Inc.getOperand(0).getReg(),
                      Inc.getOperand(1).getReg()) &&
          regsOverlap(TRI, AddIndexI->getOperand(2).getReg(),
                      Inc.getOperand(0).getReg())) {
        IndexReg = AddIndexI->getOperand(2).getReg();
        IndexZeroDef = findLastConstDefBefore(*AddIndexI, IndexReg, 0, TRI);
        SmallPtrSet<const MachineInstr *, 4> IgnoredIndex;
        if (IndexZeroDef) {
          IgnoredIndex.insert(IndexZeroDef);
          IgnoredIndex.insert(&*AddIndexI);
          IgnoredIndex.insert(&Inc);
          CanRemapIndex =
              !regTouchedOutsideInstrs(MF, IndexReg, IgnoredIndex, TRI);
        }
      }

      MachineInstr *ConstDef = findLastConstDefBefore(MAdd, ConstReg, 3, TRI);
      if (!ConstDef) {
        ++I;
        continue;
      }
      SmallPtrSet<const MachineInstr *, 4> IgnoredConst;
      IgnoredConst.insert(ConstDef);
      IgnoredConst.insert(&MAdd);
      if (regTouchedOutsideInstrs(MF, ConstReg, IgnoredConst, TRI)) {
        ++I;
        continue;
      }

      if (regsOverlap(TRI, ConstReg, Acc) ||
          regsOverlap(TRI, ConstReg, RemReg) ||
          regsOverlap(TRI, ConstReg, Div.getOperand(3).getReg())) {
        ++I;
        continue;
      }
      Register NewQuot = ConstReg;

      QuotCopy.setDesc(TII.get(Bedrock::MOV32rr));
      QuotCopy.getOperand(0).setReg(NewQuot);
      Div.getOperand(0).setReg(NewQuot);
      Div.getOperand(2).setReg(NewQuot);
      RemCopy.getOperand(0).setReg(ConstReg);
      AddRem.getOperand(2).setReg(ConstReg);

      for (unsigned N = 0; N != 3; ++N)
        BuildMI(MBB, RemCopy.getIterator(), MAdd.getDebugLoc(),
                TII.get(Bedrock::ADD32rr), Acc)
            .addReg(Acc)
            .addReg(NewQuot);

      SmallPtrSet<const MachineInstr *, 1> EmptyIgnored;
      if (CanRemapIndex &&
          !regTouchedOutsideInstrs(MF, Bedrock::D3, EmptyIgnored, TRI)) {
        IndexZeroDef->getOperand(0).setReg(Bedrock::D3);
        AddIndexI->getOperand(2).setReg(Bedrock::D3);
        Inc.getOperand(0).setReg(Bedrock::D3);
        Inc.getOperand(1).setReg(Bedrock::D3);
        removeRegLiveInsWithoutUses(MF, IndexReg, TRI);
      }

      MAdd.eraseFromParent();
      ConstDef->eraseFromParent();
      removeRegLiveInsWithoutUses(MF, QuotReg, TRI);
      removeRegLiveInsWithoutUses(MF, RemScratch, TRI);
      removeRegLiveInsWithoutUses(MF, ConstReg, TRI);
      Changed = true;
      I = MBB.begin();
    }
  }

  return Changed;
}

bool BedrockPeephole::foldRegMAddAccumulator(MachineBasicBlock &MBB,
                                                 MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Mul = *I;
    if (Mul.isDebugInstr()) {
      ++I;
      continue;
    }
    if (Mul.getOpcode() != Bedrock::MULU32rr || Mul.getNumOperands() < 3 ||
        !Mul.getOperand(0).isReg() || !Mul.getOperand(1).isReg() ||
        !Mul.getOperand(2).isReg()) {
      ++I;
      continue;
    }

    Register Product = Mul.getOperand(0).getReg();
    Register LHS = Mul.getOperand(1).getReg();
    Register Coeff = Mul.getOperand(2).getReg();
    if (!isDReg(Product) || !isDReg(Coeff) || !regsOverlap(TRI, Product, LHS) ||
        regsOverlap(TRI, Product, Coeff)) {
      ++I;
      continue;
    }

    auto AddI = nextNonDebug(I, MBB);
    if (AddI == MBB.end() || AddI->getOpcode() != Bedrock::ADD32rr ||
        AddI->getNumOperands() < 3 || !AddI->getOperand(0).isReg() ||
        !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg()) {
      ++I;
      continue;
    }

    Register AddDst = AddI->getOperand(0).getReg();
    Register AddLHS = AddI->getOperand(1).getReg();
    Register AddRHS = AddI->getOperand(2).getReg();
    if (!regsOverlap(TRI, AddDst, AddLHS)) {
      ++I;
      continue;
    }

    auto EmitMAdd = [&](Register Acc) {
      BuildMI(MBB, I, Mul.getDebugLoc(), TII.get(Bedrock::MADD32rrr), Acc)
          .addReg(Acc)
          .addReg(Product)
          .addReg(Coeff);
    };

    if (regsOverlap(TRI, AddRHS, Product) && isDReg(AddDst) &&
        !regsOverlap(TRI, AddDst, Product) &&
        !regsOverlap(TRI, AddDst, Coeff)) {
      Register Acc = AddDst;
      if (!operandIsKill(*AddI, Product, TRI) &&
          !regDeadAfter(std::next(AddI), MBB, Product, TRI)) {
        ++I;
        continue;
      }
      EmitMAdd(Acc);
      Mul.eraseFromParent();
      AddI->eraseFromParent();
      I = MBB.begin();
      Changed = true;
      continue;
    }

    if (!regsOverlap(TRI, AddDst, Product) ||
        !regsOverlap(TRI, AddLHS, Product) || !isDReg(AddRHS) ||
        regsOverlap(TRI, AddRHS, Product) || regsOverlap(TRI, AddRHS, Coeff)) {
      ++I;
      continue;
    }

    Register Acc = AddRHS;
    SmallVector<MachineInstr *, 4> Rewrite;
    MachineInstr *CopyBack = nullptr;
    for (auto Scan = nextNonDebug(AddI, MBB); Scan != MBB.end();
         Scan = nextNonDebug(Scan, MBB)) {
      if (isCopyRegToReg(*Scan, Acc, Product, TRI)) {
        CopyBack = &*Scan;
        break;
      }
      if (instrTouchesReg(*Scan, Acc, TRI)) {
        CopyBack = nullptr;
        break;
      }
      if (!instrTouchesReg(*Scan, Product, TRI))
        continue;
      if (!canRewriteProductAccumInstr(*Scan, Product, Acc, Coeff, TRI)) {
        CopyBack = nullptr;
        break;
      }
      Rewrite.push_back(&*Scan);
    }

    if (!CopyBack) {
      ++I;
      continue;
    }

    bool ProductUsedAfterCopyBack = false;
    for (auto Scan = nextNonDebug(CopyBack->getIterator(), MBB);
         Scan != MBB.end(); Scan = nextNonDebug(Scan, MBB)) {
      if (instrUsesReg(*Scan, Product, TRI)) {
        ProductUsedAfterCopyBack = true;
        break;
      }
      if (instrDefinesReg(*Scan, Product, TRI))
        break;
    }
    if (ProductUsedAfterCopyBack) {
      ++I;
      continue;
    }

    EmitMAdd(Acc);
    for (MachineInstr *MI : Rewrite)
      replaceRegOperandUsesAndDefs(*MI, Product, Acc, TRI);
    Mul.eraseFromParent();
    AddI->eraseFromParent();
    CopyBack->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldProductChainMAdd(MachineBasicBlock &MBB,
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

    MAddProductTerm FirstTerm;
    if (!matchMAddProductTerm(I, MBB, FirstTerm, TRI)) {
      ++I;
      continue;
    }

    SmallVector<MAddProductTerm, 4> Terms;
    Terms.push_back(FirstTerm);
    Register Running = FirstTerm.Product;
    auto Scan = nextNonDebug(FirstTerm.Mul->getIterator(), MBB);
    while (Scan != MBB.end()) {
      MAddProductTerm Term;
      if (!matchMAddProductTerm(Scan, MBB, Term, TRI))
        break;
      auto AddI = nextNonDebug(Term.Mul->getIterator(), MBB);
      if (AddI == MBB.end() || AddI->getOpcode() != Bedrock::ADD32rr ||
          AddI->getNumOperands() < 3 || !AddI->getOperand(0).isReg() ||
          !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg())
        break;

      Register Dst = AddI->getOperand(0).getReg();
      Register LHS = AddI->getOperand(1).getReg();
      Register RHS = AddI->getOperand(2).getReg();
      bool AddIntoTerm = regsOverlap(TRI, Dst, Term.Product) &&
                         regsOverlap(TRI, LHS, Term.Product) &&
                         regsOverlap(TRI, RHS, Running);
      bool AddIntoRunning = regsOverlap(TRI, Dst, Running) &&
                            regsOverlap(TRI, LHS, Running) &&
                            regsOverlap(TRI, RHS, Term.Product);
      if (!AddIntoTerm && !AddIntoRunning)
        break;
      if (AddIntoTerm) {
        if (!operandIsKill(*AddI, Running, TRI) &&
            !regDeadAfterInCFG(std::next(AddI), MBB, Running, TRI))
          break;
      } else {
        if (!operandIsKill(*AddI, Term.Product, TRI) &&
            !regDeadAfterInCFG(std::next(AddI), MBB, Term.Product, TRI))
          break;
      }

      Term.Add = &*AddI;
      Terms.push_back(Term);
      Running = Dst;
      Scan = nextNonDebug(AddI, MBB);
    }

    if (Terms.size() < 2 || Scan == MBB.end()) {
      ++I;
      continue;
    }

    Register StoreSrc;
    Register StoreBase;
    int64_t StoreOffset = 0;
    bool IsStore = isMemStore(*Scan, StoreSrc, StoreBase, StoreOffset);
    if (!IsStore && Scan->getOpcode() == Bedrock::MOV32postmr &&
        Scan->getNumOperands() >= 2 && Scan->getOperand(0).isReg() &&
        Scan->getOperand(1).isReg()) {
      StoreSrc = Scan->getOperand(0).getReg();
      StoreBase = Scan->getOperand(1).getReg();
      StoreOffset = 0;
      IsStore = true;
    }
    if (!IsStore ||
        (Scan->getOpcode() != Bedrock::MOV32mr &&
         Scan->getOpcode() != Bedrock::MOV32postmr) ||
        !regsOverlap(TRI, StoreSrc, Running) || hasOrderedMemOperand(*Scan) ||
        (!operandIsKill(*Scan, Running, TRI) &&
         !regDeadAfterInCFG(std::next(Scan), MBB, Running, TRI))) {
      ++I;
      continue;
    }

    Register Acc = findUnusedCallerDReg(MF, TRI);
    if (!Acc) {
      ++I;
      continue;
    }

    DebugLoc DL = First.getDebugLoc();
    BuildMI(MBB, I, DL, TII.get(Bedrock::CLR64r), Acc);
    for (MAddProductTerm &Term : Terms) {
      unsigned Opcode = getMAddMemOpcode(Term.Mul->getOpcode(), Term.PostInc);
      assert(Opcode != 0 && "validated MADD product opcode");
      MachineInstrBuilder MIB =
          BuildMI(MBB, I, Term.Mul->getDebugLoc(), TII.get(Opcode), Acc)
              .addReg(Acc);
      addMaybePostMemOperand(MIB, Term.Base, Term.Offset, Term.PostInc);
      MIB.addReg(Term.Coeff);
      MIB.cloneMemRefs(*Term.Load);
    }

    Scan->getOperand(0).setReg(Acc);
    for (MAddProductTerm &Term : Terms) {
      Term.Load->eraseFromParent();
      Term.Mul->eraseFromParent();
      if (Term.Add)
        Term.Add->eraseFromParent();
    }
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldShiftedBSet(MachineBasicBlock &MBB,
                                          MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Shift = *I;
    if (Shift.isDebugInstr()) {
      ++I;
      continue;
    }

    if (Shift.getOpcode() != Bedrock::SHL32ri || Shift.getNumOperands() < 3 ||
        !Shift.getOperand(0).isReg() || !Shift.getOperand(1).isReg() ||
        !Shift.getOperand(2).isImm() || Shift.getOperand(2).getImm() <= 0) {
      ++I;
      continue;
    }

    Register Reg = Shift.getOperand(0).getReg();
    if (!regsOverlap(TRI, Reg, Shift.getOperand(1).getReg())) {
      ++I;
      continue;
    }

    auto BSetI = nextNonDebug(I, MBB);
    if (BSetI == MBB.end() || BSetI->getOpcode() != Bedrock::BSET32ri ||
        BSetI->getNumOperands() < 3 || !BSetI->getOperand(0).isReg() ||
        !BSetI->getOperand(1).isReg() || !BSetI->getOperand(2).isImm() ||
        !regsOverlap(TRI, BSetI->getOperand(0).getReg(), Reg) ||
        !regsOverlap(TRI, BSetI->getOperand(1).getReg(), Reg) ||
        BSetI->getOperand(2).getImm() != 0 ||
        !regDefDeadOrDeadAfterInCFG(BSetI, MBB, Bedrock::FLAGS, TRI)) {
      ++I;
      continue;
    }

    BSetI->setDesc(TII.get(Bedrock::INC32r));
    while (BSetI->getNumOperands() > 2)
      BSetI->removeOperand(BSetI->getNumOperands() - 1);
    I = nextNonDebug(BSetI, MBB);
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldDivMod(MachineBasicBlock &MBB,
                                     MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Div = *I;
    if (Div.isDebugInstr()) {
      ++I;
      continue;
    }
    unsigned DivModOpcode = getDivModOpcode(Div.getOpcode());
    unsigned MulOpcode = getDivModMulOpcode(Div.getOpcode());
    unsigned SubOpcode = getDivModSubOpcode(Div.getOpcode());
    if (DivModOpcode == 0 || Div.getNumOperands() < 3 ||
        !Div.getOperand(0).isReg() || !Div.getOperand(1).isReg() ||
        !Div.getOperand(2).isReg() ||
        !regsOverlap(TRI, Div.getOperand(0).getReg(),
                     Div.getOperand(1).getReg())) {
      ++I;
      continue;
    }
    Register Quot = Div.getOperand(0).getReg();
    Register Divisor = Div.getOperand(2).getReg();

    auto MulI = nextNonDebug(I, MBB);
    auto CopyI = MBB.end();
    Register QuotCopy;
    Register CopyDst, CopySrc;
    unsigned CopyWidth = 0;
    if (MulI != MBB.end() &&
        isTrackableRegMove(*MulI, CopyDst, CopySrc, CopyWidth) &&
        regsOverlap(TRI, CopySrc, Quot)) {
      CopyI = MulI;
      QuotCopy = CopyDst;
      MulI = nextNonDebug(CopyI, MBB);
    }

    if (MulI == MBB.end() || MulI->getOpcode() != MulOpcode ||
        MulI->getNumOperands() < 3 || !MulI->getOperand(0).isReg() ||
        !MulI->getOperand(1).isReg() || !MulI->getOperand(2).isReg() ||
        !regsOverlap(TRI, MulI->getOperand(0).getReg(),
                     MulI->getOperand(1).getReg())) {
      ++I;
      continue;
    }
    Register Product = MulI->getOperand(0).getReg();
    Register MulLHS = MulI->getOperand(1).getReg();
    Register MulRHS = MulI->getOperand(2).getReg();
    auto IsQuotientValue = [&](Register Reg) {
      return regsOverlap(TRI, Reg, Quot) ||
             (QuotCopy.isValid() && regsOverlap(TRI, Reg, QuotCopy));
    };
    auto IsDivisorValue = [&](Register Reg) {
      return regsOverlap(TRI, Reg, Divisor);
    };
    if (!((IsQuotientValue(MulLHS) && IsDivisorValue(MulRHS)) ||
          (IsDivisorValue(MulLHS) && IsQuotientValue(MulRHS)))) {
      ++I;
      continue;
    }

    auto SubI = nextNonDebug(MulI, MBB);
    if (SubI == MBB.end() || SubI->getOpcode() != SubOpcode ||
        SubI->getNumOperands() < 3 || !SubI->getOperand(0).isReg() ||
        !SubI->getOperand(1).isReg() || !SubI->getOperand(2).isReg() ||
        !regsOverlap(TRI, SubI->getOperand(0).getReg(),
                     SubI->getOperand(1).getReg()) ||
        !regsOverlap(TRI, SubI->getOperand(2).getReg(), Product)) {
      ++I;
      continue;
    }
    Register Rem = SubI->getOperand(0).getReg();
    if (regsOverlap(TRI, Rem, Quot) || regsOverlap(TRI, Rem, Divisor)) {
      ++I;
      continue;
    }
    if (!isDReg(Quot) || !isDReg(Rem) || !isDReg(Divisor)) {
      ++I;
      continue;
    }
    if (!operandIsKill(*SubI, Product, TRI) &&
        !regDeadAfter(std::next(SubI), MBB, Product, TRI)) {
      ++I;
      continue;
    }
    if (QuotCopy.isValid() && !regsOverlap(TRI, QuotCopy, Product) &&
        !operandIsKill(*MulI, QuotCopy, TRI) &&
        !regDeadAfter(std::next(MulI), MBB, QuotCopy, TRI)) {
      ++I;
      continue;
    }

    auto DividendCopyI = prevNonDebug(I, MBB);
    Register DividendCopyDst, DividendCopySrc;
    unsigned DividendCopyWidth = 0;
    if (DividendCopyI == MBB.end() ||
        !isTrackableRegMove(*DividendCopyI, DividendCopyDst, DividendCopySrc,
                            DividendCopyWidth) ||
        !regsOverlap(TRI, DividendCopyDst, Quot) ||
        !regsOverlap(TRI, DividendCopySrc, Rem)) {
      ++I;
      continue;
    }
    if (!regDeadAfter(std::next(SubI), MBB, Bedrock::FLAGS, TRI)) {
      ++I;
      continue;
    }

    BuildMI(MBB, Div.getIterator(), Div.getDebugLoc(), TII.get(DivModOpcode))
        .addReg(Quot, RegState::Define)
        .addReg(Rem, RegState::Define)
        .addReg(Quot)
        .addReg(Divisor);

    Div.eraseFromParent();
    if (CopyI != MBB.end())
      CopyI->eraseFromParent();
    MulI->eraseFromParent();
    SubI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldSum(MachineBasicBlock &MBB,
                                  MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &First = *I;
    if (First.isDebugInstr()) {
      ++I;
      continue;
    }

    unsigned AddOpcode = First.getOpcode();
    if (AddOpcode != Bedrock::ADD8rr && AddOpcode != Bedrock::ADD16rr &&
        AddOpcode != Bedrock::ADD32rr && AddOpcode != Bedrock::ADD64rr) {
      ++I;
      continue;
    }
    if (First.getNumOperands() < 3 || !First.getOperand(0).isReg() ||
        !First.getOperand(1).isReg() || !First.getOperand(2).isReg()) {
      ++I;
      continue;
    }

    Register Dst = First.getOperand(0).getReg();
    if (First.getOperand(1).getReg() != Dst) {
      ++I;
      continue;
    }
    std::optional<unsigned> DstBit = getMaskBit(Dst);
    std::optional<unsigned> RhsBit = getMaskBit(First.getOperand(2).getReg());
    if (!DstBit || !RhsBit || *DstBit == *RhsBit) {
      ++I;
      continue;
    }

    uint16_t Mask = (uint16_t(1) << *DstBit) | (uint16_t(1) << *RhsBit);
    SmallVector<MachineInstr *, 8> Adds;
    Adds.push_back(&First);

    auto J = std::next(I);
    for (; J != MBB.end(); ++J) {
      if (J->isDebugInstr())
        continue;
      if (J->getOpcode() != AddOpcode || J->getNumOperands() < 3 ||
          !J->getOperand(0).isReg() || !J->getOperand(1).isReg() ||
          !J->getOperand(2).isReg())
        break;
      if (J->getOperand(0).getReg() != Dst || J->getOperand(1).getReg() != Dst)
        break;
      std::optional<unsigned> Bit = getMaskBit(J->getOperand(2).getReg());
      if (!Bit || (Mask & (uint16_t(1) << *Bit)) != 0)
        break;
      Mask |= uint16_t(1) << *Bit;
      Adds.push_back(&*J);
    }

    unsigned MinAdds = MF.getFunction().hasMinSize() ? 3 : 2;
    if (Adds.size() < MinAdds) {
      ++I;
      continue;
    }

    unsigned SumOpcode = getSumOpcode(AddOpcode, Dst);
    if (SumOpcode == 0) {
      ++I;
      continue;
    }

    DebugLoc DL = First.getDebugLoc();
    MachineInstrBuilder Sum =
        BuildMI(MBB, First.getIterator(), DL, TII.get(SumOpcode), Dst)
            .addImm(Mask);
    addImplicitSumRegs(Sum, Mask);

    I = J;
    for (MachineInstr *Add : Adds)
      Add->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldSumReturnCopy(MachineBasicBlock &MBB,
                                            MachineFunction &MF) const {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Sum = *I++;
    if (Sum.isDebugInstr() || Sum.getOpcode() != Bedrock::SUM32d ||
        Sum.getNumOperands() < 2 || !Sum.getOperand(0).isReg() ||
        !Sum.getOperand(1).isImm())
      continue;

    auto CopyI = nextNonDebug(Sum.getIterator(), MBB);
    if (CopyI == MBB.end() ||
        (CopyI->getOpcode() != Bedrock::MOV32rr &&
         CopyI->getOpcode() != Bedrock::MOV64rr) ||
        CopyI->getNumOperands() < 2 || !CopyI->getOperand(0).isReg() ||
        !CopyI->getOperand(1).isReg() ||
        !regsOverlap(TRI, CopyI->getOperand(0).getReg(), Bedrock::D0) ||
        !regsOverlap(TRI, CopyI->getOperand(1).getReg(),
                     Sum.getOperand(0).getReg()))
      continue;

    uint16_t Mask = static_cast<uint16_t>(Sum.getOperand(1).getImm());
    std::optional<unsigned> D0Bit = getMaskBit(Bedrock::D0);
    if (!D0Bit || (Mask & (uint16_t(1) << *D0Bit)) != 0)
      continue;

    Sum.getOperand(0).setReg(Bedrock::D0);
    CopyI->eraseFromParent();
    Changed = true;
    I = MBB.begin();
  }

  return Changed;
}

bool BedrockPeephole::foldSignedClampReturn(MachineBasicBlock &MBB,
                                                MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Min = *I;
    if (Min.isDebugInstr() || Min.getOpcode() != Bedrock::MINS32rr ||
        Min.getNumOperands() < 3 || !Min.getOperand(0).isReg() ||
        !Min.getOperand(1).isReg() || !Min.getOperand(2).isReg()) {
      ++I;
      continue;
    }

    Register Hi = Min.getOperand(0).getReg();
    Register MinLHS = Min.getOperand(1).getReg();
    Register Value = Min.getOperand(2).getReg();
    if (!isDReg(Hi) || !isDReg(Value) || !regsOverlap(TRI, Hi, MinLHS) ||
        !regsOverlap(TRI, Value, Bedrock::D0)) {
      ++I;
      continue;
    }

    auto CmpI = nextNonDebug(I, MBB);
    auto MovCCI = CmpI == MBB.end() ? MBB.end() : nextNonDebug(CmpI, MBB);
    auto CopyI = MovCCI == MBB.end() ? MBB.end() : nextNonDebug(MovCCI, MBB);
    auto RetI = CopyI == MBB.end() ? MBB.end() : nextNonDebug(CopyI, MBB);
    if (CmpI == MBB.end() || MovCCI == MBB.end() || CopyI == MBB.end() ||
        RetI == MBB.end() || nextNonDebug(RetI, MBB) != MBB.end()) {
      ++I;
      continue;
    }

    if (CmpI->getOpcode() != Bedrock::CMP32rr || CmpI->getNumOperands() < 2 ||
        !CmpI->getOperand(0).isReg() || !CmpI->getOperand(1).isReg() ||
        !regsOverlap(TRI, CmpI->getOperand(0).getReg(), Value)) {
      ++I;
      continue;
    }
    Register Lo = CmpI->getOperand(1).getReg();
    if (!isDReg(Lo) || regsOverlap(TRI, Lo, Value) ||
        regsOverlap(TRI, Lo, Hi)) {
      ++I;
      continue;
    }

    if (MovCCI->getOpcode() != Bedrock::MOVCC32rr ||
        MovCCI->getNumOperands() < 4 || !MovCCI->getOperand(0).isReg() ||
        !MovCCI->getOperand(1).isReg() || !MovCCI->getOperand(2).isReg() ||
        !MovCCI->getOperand(3).isImm() ||
        MovCCI->getOperand(3).getImm() != BedrockCC::LT ||
        !regsOverlap(TRI, MovCCI->getOperand(0).getReg(), Hi) ||
        !regsOverlap(TRI, MovCCI->getOperand(1).getReg(), Hi) ||
        !regsOverlap(TRI, MovCCI->getOperand(2).getReg(), Lo)) {
      ++I;
      continue;
    }

    if (CopyI->getOpcode() != Bedrock::MOV64rr || CopyI->getNumOperands() < 2 ||
        !CopyI->getOperand(0).isReg() || !CopyI->getOperand(1).isReg() ||
        !regsOverlap(TRI, CopyI->getOperand(0).getReg(), Bedrock::D0) ||
        !regsOverlap(TRI, CopyI->getOperand(1).getReg(), Hi) ||
        RetI->getOpcode() != Bedrock::RET) {
      ++I;
      continue;
    }

    DebugLoc DL = Min.getDebugLoc();
    BuildMI(MBB, Min.getIterator(), DL, TII.get(Bedrock::MAXS32rr), Bedrock::D0)
        .addReg(Value)
        .addReg(Lo);
    BuildMI(MBB, Min.getIterator(), DL, TII.get(Bedrock::MINS32rr), Bedrock::D0)
        .addReg(Bedrock::D0)
        .addReg(Hi);

    Min.eraseFromParent();
    CmpI->eraseFromParent();
    MovCCI->eraseFromParent();
    CopyI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldMinMaxBranchDiamond(MachineFunction &MF) const {
  bool Changed = false;
  bool LocalChanged = true;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  while (LocalChanged) {
    LocalChanged = false;
    for (auto MBBI = MF.begin(); MBBI != MF.end(); ++MBBI) {
      MachineBasicBlock &MBB = *MBBI;
      MachineBasicBlock::iterator LastI = MBB.getLastNonDebugInstr();
      if (LastI == MBB.end())
        continue;

      MachineBasicBlock::iterator BranchI = MBB.end();
      MachineBasicBlock::iterator JmpI = MBB.end();
      MachineBasicBlock *JoinBB = nullptr;
      MachineBasicBlock *MoveBB = nullptr;
      if (LastI->getOpcode() == Bedrock::JMP && LastI->getNumOperands() >= 1 &&
          LastI->getOperand(0).isMBB()) {
        JmpI = LastI;
        BranchI = prevNonDebug(JmpI, MBB);
        if (BranchI == MBB.end() || BranchI->getOpcode() != Bedrock::JCC ||
            BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB())
          continue;
        JoinBB = BranchI->getOperand(0).getMBB();
        MoveBB = JmpI->getOperand(0).getMBB();
      } else if (LastI->getOpcode() == Bedrock::JCC &&
                 LastI->getNumOperands() >= 2 && LastI->getOperand(0).isMBB()) {
        BranchI = LastI;
        JoinBB = BranchI->getOperand(0).getMBB();
        auto Next = std::next(MBB.getIterator());
        if (Next == MF.end())
          continue;
        MoveBB = &*Next;
      } else {
        continue;
      }

      if (!JoinBB || !MoveBB || JoinBB == MoveBB || MoveBB->pred_size() != 1 ||
          *MoveBB->pred_begin() != &MBB || MoveBB->succ_size() != 1 ||
          *MoveBB->succ_begin() != JoinBB)
        continue;

      auto MoveLayoutI = std::next(MBB.getIterator());
      if (MoveLayoutI == MF.end() || &*MoveLayoutI != MoveBB)
        continue;
      auto JoinLayoutI = std::next(MoveBB->getIterator());
      if (JoinLayoutI == MF.end() || &*JoinLayoutI != JoinBB)
        continue;

      MachineBasicBlock::iterator CmpI = prevNonDebug(BranchI, MBB);
      if (CmpI == MBB.end() || nextNonDebug(CmpI, MBB) != BranchI ||
          CmpI->getNumOperands() < 2 || !CmpI->getOperand(0).isReg() ||
          !CmpI->getOperand(1).isReg())
        continue;

      MachineBasicBlock::iterator MoveI = firstNonDebug(*MoveBB);
      if (MoveI == MoveBB->end() ||
          nextNonDebug(MoveI, *MoveBB) != MoveBB->end())
        continue;
      unsigned MoveOpcode = MoveI->getOpcode();
      if ((MoveOpcode != Bedrock::MOV8rr && MoveOpcode != Bedrock::MOV16rr &&
           MoveOpcode != Bedrock::MOV32rr && MoveOpcode != Bedrock::MOV64rr) ||
          MoveI->getNumOperands() < 2 || !MoveI->getOperand(0).isReg() ||
          !MoveI->getOperand(1).isReg())
        continue;

      Register CmpLHS = CmpI->getOperand(0).getReg();
      Register CmpRHS = CmpI->getOperand(1).getReg();
      Register MoveDst = MoveI->getOperand(0).getReg();
      Register MoveSrc = MoveI->getOperand(1).getReg();
      bool TrueSelectsLHS = false;
      if (regsOverlap(TRI, MoveDst, CmpLHS) &&
          regsOverlap(TRI, MoveSrc, CmpRHS)) {
        TrueSelectsLHS = true;
      } else if (regsOverlap(TRI, MoveDst, CmpRHS) &&
                 regsOverlap(TRI, MoveSrc, CmpLHS)) {
        TrueSelectsLHS = false;
      } else {
        continue;
      }

      std::optional<int64_t> CC = getCondCodeImm(BranchI->getOperand(1));
      if (!CC)
        continue;
      unsigned MinMaxOpcode =
          getMinMaxOpcodeForCmp(CmpI->getOpcode(), *CC, TrueSelectsLHS);
      if (MinMaxOpcode == 0)
        continue;

      Register ResultDst = MoveDst;
      Register ResultSrc = MoveSrc;
      MachineBasicBlock::iterator JoinCopyI = firstNonDebug(*JoinBB);
      bool RemoveJoinCopy = false;
      if (JoinCopyI != JoinBB->end() && JoinCopyI->getOpcode() == MoveOpcode &&
          JoinCopyI->getNumOperands() >= 2 &&
          JoinCopyI->getOperand(0).isReg() &&
          JoinCopyI->getOperand(1).isReg() &&
          regsOverlap(TRI, JoinCopyI->getOperand(1).getReg(), MoveDst) &&
          regsOverlap(TRI, JoinCopyI->getOperand(0).getReg(), MoveSrc)) {
        ResultDst = JoinCopyI->getOperand(0).getReg();
        ResultSrc = MoveDst;
        RemoveJoinCopy = true;
      }

      if (!minMaxOpcodeCanUseRegs(MinMaxOpcode, ResultDst, ResultSrc))
        continue;
      if (!regDeadAfterInCFG(std::next(BranchI), MBB, Bedrock::FLAGS, TRI))
        continue;

      DebugLoc DL = CmpI->getDebugLoc();
      BuildMI(MBB, CmpI, DL, TII.get(MinMaxOpcode), ResultDst)
          .addReg(ResultDst)
          .addReg(ResultSrc);

      CmpI->eraseFromParent();
      BranchI->eraseFromParent();
      if (JmpI != MBB.end())
        JmpI->eraseFromParent();
      if (RemoveJoinCopy) {
        JoinCopyI->eraseFromParent();
        JoinBB->removeLiveIn(MoveDst);
        if (!JoinBB->isLiveIn(ResultDst))
          JoinBB->addLiveIn(ResultDst);
      }

      SmallVector<MachineBasicBlock *, 4> Succs(MBB.successors());
      for (MachineBasicBlock *Succ : Succs)
        MBB.removeSuccessor(Succ);
      if (!MBB.isSuccessor(JoinBB))
        MBB.addSuccessor(JoinBB);

      while (!MoveBB->succ_empty())
        MoveBB->removeSuccessor(MoveBB->succ_begin());
      MoveBB->eraseFromParent();
      Changed = true;
      LocalChanged = true;
      break;
    }
  }

  return Changed;
}

bool BedrockPeephole::foldMinSizeA32ToCalleeSavedDRegs(
    MachineFunction &MF) const {
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  SmallVector<Register, 5> Candidates;
  for (Register Reg = Bedrock::A1; Reg <= Bedrock::A5; Reg = Register(Reg + 1))
    if (canRemapA32RegToD(MF, Reg, TRI))
      Candidates.push_back(Reg);
  if (Candidates.empty())
    return false;

  SmallVector<Register, 2> FreeDRegs;
  for (Register Reg : {Register(Bedrock::D6), Register(Bedrock::D7)})
    if (!physRegUsedInFunction(MF, Reg, TRI))
      FreeDRegs.push_back(Reg);
  if (FreeDRegs.empty())
    return false;

  MachineInstr *PushM = nullptr;
  SmallVector<MachineInstr *, 4> PopMs;
  uint16_t PushPopMask = 0;
  bool HasPushPop = collectConsistentPushPopMask(MF, PushM, PopMs, PushPopMask);

  bool HasRet = false;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB)
      if (!MI.isDebugInstr() && MI.getOpcode() == Bedrock::RET)
        HasRet = true;
  if (!HasPushPop && !HasRet)
    return false;

  auto AddedMaskFor = [&](ArrayRef<A32DRemap> Remaps) {
    uint16_t AddedMask = 0;
    for (const A32DRemap &Remap : Remaps) {
      std::optional<unsigned> Bit = getMaskBit(Remap.Dst);
      if (Bit && (PushPopMask & (uint16_t(1) << *Bit)) == 0)
        AddedMask |= uint16_t(1) << *Bit;
    }
    return AddedMask;
  };

  unsigned Baseline = estimateMinSizeA32FunctionSize(MF, {}, 0);
  unsigned BestSize = Baseline;
  SmallVector<A32DRemap, 2> BestRemaps;

  auto Evaluate = [&](ArrayRef<A32DRemap> Remaps) {
    uint16_t AddedMask = AddedMaskFor(Remaps);
    unsigned ExtraSaveRestoreBytes = 0;
    if (!HasPushPop && AddedMask != 0)
      ExtraSaveRestoreBytes = singleRegFromMask(AddedMask) ? 4 : 8;
    unsigned Size =
        estimateMinSizeA32FunctionSize(MF, Remaps, ExtraSaveRestoreBytes);
    if (Size < BestSize) {
      BestSize = Size;
      BestRemaps.assign(Remaps.begin(), Remaps.end());
    }
  };

  for (Register Candidate : Candidates) {
    for (Register DReg : FreeDRegs) {
      SmallVector<A32DRemap, 2> Remaps;
      Remaps.push_back({Candidate, DReg});
      Evaluate(Remaps);
    }
  }

  if (FreeDRegs.size() >= 2) {
    for (unsigned I = 0, E = Candidates.size(); I != E; ++I) {
      for (unsigned J = I + 1; J != E; ++J) {
        SmallVector<A32DRemap, 2> Remaps;
        Remaps.push_back({Candidates[I], FreeDRegs[0]});
        Remaps.push_back({Candidates[J], FreeDRegs[1]});
        Evaluate(Remaps);

        Remaps.clear();
        Remaps.push_back({Candidates[I], FreeDRegs[1]});
        Remaps.push_back({Candidates[J], FreeDRegs[0]});
        Evaluate(Remaps);
      }
    }
  }

  if (BestRemaps.empty())
    return false;

  uint16_t AddedMask = AddedMaskFor(BestRemaps);
  remapA32Operands(MF, BestRemaps);

  if (AddedMask != 0) {
    if (HasPushPop) {
      assert(PushM && "consistent PUSHM/POPM set without PUSHM");
      extendPushPopMask(MF, *PushM, PopMs, PushPopMask, AddedMask);
    } else {
      MachineBasicBlock &Entry = MF.front();
      MachineBasicBlock::iterator Insert = Entry.begin();
      while (Insert != Entry.end() && Insert->isDebugInstr())
        ++Insert;
      DebugLoc DL = Insert != Entry.end() ? Insert->getDebugLoc() : DebugLoc();
      MachineInstrBuilder Push =
          buildPushForMask(Entry, Insert, DL, TII, AddedMask);
      Push.setMIFlag(MachineInstr::FrameSetup);

      for (MachineBasicBlock &MBB : MF) {
        for (auto I = MBB.begin(); I != MBB.end(); ++I) {
          if (I->isDebugInstr() || I->getOpcode() != Bedrock::RET)
            continue;
          MachineInstrBuilder Pop =
              buildPopForMask(MBB, I, I->getDebugLoc(), TII, AddedMask);
          Pop.setMIFlag(MachineInstr::FrameDestroy);
        }
      }
    }

    for (unsigned Bit = 0; Bit != 16; ++Bit) {
      if ((AddedMask & (uint16_t(1) << Bit)) == 0)
        continue;
      Register Reg = getRegForMaskBit(Bit);
      if (!MF.front().isLiveIn(Reg))
        MF.front().addLiveIn(Reg);
    }
  }

  return true;
}

bool BedrockPeephole::foldMinSizeMAddWindowBaseBias(
    MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  auto MatchMAddMem = [&](MachineInstr &MI, Register Acc, Register Base,
                          int64_t Offset, Register &Coeff) {
    if (MI.getOpcode() != Bedrock::MADD32mrr || MI.getNumOperands() < 5 ||
        !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
        !MI.getOperand(2).isReg() || !MI.getOperand(3).isImm() ||
        !MI.getOperand(4).isReg())
      return false;
    if (!regsOverlap(TRI, MI.getOperand(0).getReg(), Acc) ||
        !regsOverlap(TRI, MI.getOperand(1).getReg(), Acc) ||
        !regsOverlap(TRI, MI.getOperand(2).getReg(), Base) ||
        MI.getOperand(3).getImm() != Offset)
      return false;
    Coeff = MI.getOperand(4).getReg();
    return true;
  };

  auto MatchMAddPost = [&](MachineInstr &MI, Register Acc, Register Base,
                           Register &Coeff) {
    if (MI.getOpcode() != Bedrock::MADD32postmrr || MI.getNumOperands() < 4 ||
        !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
        !MI.getOperand(2).isReg() || !MI.getOperand(3).isReg())
      return false;
    if (!regsOverlap(TRI, MI.getOperand(0).getReg(), Acc) ||
        !regsOverlap(TRI, MI.getOperand(1).getReg(), Acc) ||
        !regsOverlap(TRI, MI.getOperand(2).getReg(), Base))
      return false;
    Coeff = MI.getOperand(3).getReg();
    return true;
  };

  auto RegDeadOnNonLoopSuccessors = [&](MachineBasicBlock &MBB,
                                        MachineBasicBlock &Loop, Register Reg) {
    for (MachineBasicBlock *Succ : MBB.successors()) {
      if (Succ == &Loop)
        continue;
      SmallPtrSet<MachineBasicBlock *, 8> Visiting;
      if (!regDeadFromBlockStartInCFG(*Succ, Reg, TRI, Visiting))
        return false;
    }
    return true;
  };

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
    Register CountReg;
    unsigned CmpOpcode = 0;
    if (DecI == Body.end() ||
        !isIncDecRegInstr(*DecI, CountReg, CmpOpcode, TRI) ||
        DecI->getOpcode() != Bedrock::DEC32r || !isDReg(CountReg))
      continue;

    MachineBasicBlock::iterator ClrI = Body.begin();
    while (ClrI != Body.end() && ClrI->isDebugInstr())
      ++ClrI;
    if (ClrI == Body.end() || ClrI->getOpcode() != Bedrock::CLR64r ||
        ClrI->getNumOperands() < 1 || !ClrI->getOperand(0).isReg())
      continue;
    Register Acc = ClrI->getOperand(0).getReg();
    if (!isDReg(Acc))
      continue;

    MachineBasicBlock::iterator M1I = nextNonDebug(ClrI, Body);
    MachineBasicBlock::iterator M2I =
        M1I == Body.end() ? Body.end() : nextNonDebug(M1I, Body);
    MachineBasicBlock::iterator M3I =
        M2I == Body.end() ? Body.end() : nextNonDebug(M2I, Body);
    MachineBasicBlock::iterator StoreI =
        M3I == Body.end() ? Body.end() : nextNonDebug(M3I, Body);
    if (M1I == Body.end() || M2I == Body.end() || M3I == Body.end() ||
        StoreI == Body.end() || nextNonDebug(StoreI, Body) != DecI)
      continue;

    Register Base;
    Register C1, C2, C3;
    if (M1I->getOpcode() != Bedrock::MADD32mrr || M1I->getNumOperands() < 3 ||
        !M1I->getOperand(2).isReg())
      continue;
    Base = M1I->getOperand(2).getReg();
    if (!isAReg(Base) || !MatchMAddMem(*M1I, Acc, Base, -8, C1) ||
        !MatchMAddMem(*M2I, Acc, Base, -4, C2) ||
        !MatchMAddPost(*M3I, Acc, Base, C3))
      continue;
    if (hasOrderedMemOperand(*M1I) || hasOrderedMemOperand(*M2I) ||
        hasOrderedMemOperand(*M3I) || hasOrderedMemOperand(*StoreI))
      continue;

    if (StoreI->getOpcode() != Bedrock::MOV32postmr ||
        StoreI->getNumOperands() < 2 || !StoreI->getOperand(0).isReg() ||
        !StoreI->getOperand(1).isReg() ||
        !regsOverlap(TRI, StoreI->getOperand(0).getReg(), Acc) ||
        regsOverlap(TRI, StoreI->getOperand(1).getReg(), Base))
      continue;

    MachineBasicBlock *Header = findSingleNonSelfPredecessor(Body);
    if (!Header || !isPositiveCountPretest(*Header, Body, CountReg, TRI))
      continue;
    MachineBasicBlock *Preheader = findSingleNonSelfPredecessor(*Header);
    if (!Preheader || Preheader == &Body || !Preheader->isSuccessor(Header) ||
        Preheader->succ_size() != 1)
      continue;

    MachineBasicBlock::iterator BiasI = Preheader->getLastNonDebugInstr();
    if (BiasI == Preheader->end() || !isAddImmToReg(*BiasI, Base, 8, TRI))
      continue;

    bool Unsafe = false;
    for (MachineInstr &MI : *Header) {
      if (MI.isDebugInstr())
        continue;
      if (instrTouchesReg(MI, Base, TRI) ||
          instrHasRegMaskForReg(MI, Base, TRI)) {
        Unsafe = true;
        break;
      }
    }
    if (Unsafe || !RegDeadOnNonLoopSuccessors(*Header, Body, Base) ||
        !RegDeadOnNonLoopSuccessors(Body, Body, Base))
      continue;

    MachineInstrBuilder NewM1 = BuildMI(Body, M1I, M1I->getDebugLoc(),
                                        TII.get(Bedrock::MADD32postmrr), Acc)
                                    .addReg(Acc)
                                    .addReg(Base)
                                    .addReg(C1);
    NewM1.cloneMemRefs(*M1I);
    M1I->eraseFromParent();

    M2I->getOperand(3).setImm(0);

    MachineInstrBuilder NewM3 =
        BuildMI(Body, M3I, M3I->getDebugLoc(), TII.get(Bedrock::MADD32mrr), Acc)
            .addReg(Acc)
            .addReg(Base)
            .addImm(4)
            .addReg(C3);
    NewM3.cloneMemRefs(*M3I);
    M3I->eraseFromParent();

    BiasI->eraseFromParent();
    if (!Preheader->isLiveIn(Base))
      Preheader->addLiveIn(Base);
    if (!Header->isLiveIn(Base))
      Header->addLiveIn(Base);
    if (!Body.isLiveIn(Base))
      Body.addLiveIn(Base);
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::normalizeA32Arithmetic(MachineBasicBlock &MBB,
                                                 MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());

  for (MachineInstr &MI : MBB) {
    if (MI.isDebugInstr() || MI.getNumOperands() == 0 ||
        !MI.getOperand(0).isReg())
      continue;

    Register Dst = MI.getOperand(0).getReg();
    if (!isAReg(Dst))
      continue;

    unsigned NewOpcode = 0;
    switch (MI.getOpcode()) {
    default:
      break;
    case Bedrock::ADD32rr:
      NewOpcode = Bedrock::ADD32ar;
      break;
    case Bedrock::ADD32ri:
      NewOpcode = Bedrock::ADD32ai;
      break;
    case Bedrock::SUB32rr:
      NewOpcode = Bedrock::SUB32ar;
      break;
    case Bedrock::SUB32ri:
      NewOpcode = Bedrock::SUB32ai;
      break;
    }

    if (NewOpcode == 0)
      continue;

    MI.setDesc(TII.get(NewOpcode));
    Changed = true;
  }

  return Changed;
}

