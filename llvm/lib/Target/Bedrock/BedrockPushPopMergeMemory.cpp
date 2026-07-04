//===-- BedrockPushPopMergeMemory.cpp - Bedrock peepholes -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockPushPopMerge.h"

bool BedrockPushPopMerge::foldClrZeroMemCmp(MachineBasicBlock &MBB,
                                            MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Cmp = *I;
    if (Cmp.isDebugInstr()) {
      ++I;
      continue;
    }

    unsigned MemImmOpcode = getMemImmCmpOpcodeForMemRegCmp(Cmp.getOpcode());
    if (MemImmOpcode == 0 || Cmp.getNumOperands() < 3 ||
        !Cmp.getOperand(0).isReg() || !Cmp.getOperand(1).isReg() ||
        !Cmp.getOperand(2).isImm() || hasOrderedMemOperand(Cmp)) {
      ++I;
      continue;
    }

    Register ZeroReg = Cmp.getOperand(0).getReg();
    Register Base = Cmp.getOperand(1).getReg();
    int64_t Offset = Cmp.getOperand(2).getImm();
    if (regsOverlap(TRI, ZeroReg, Base)) {
      ++I;
      continue;
    }

    MachineInstr *ClrI = findRemovableClrDefBefore(MBB, I, ZeroReg, TRI);
    if (!ClrI) {
      ++I;
      continue;
    }

    auto ConsumerI = nextNonDebug(I, MBB);
    while (ConsumerI != MBB.end() && !getCondOperand(*ConsumerI) &&
           !instrTouchesReg(*ConsumerI, Bedrock::FLAGS, TRI))
      ConsumerI = nextNonDebug(ConsumerI, MBB);

    MachineOperand *CondOp =
        ConsumerI == MBB.end() ? nullptr : getCondOperand(*ConsumerI);
    std::optional<int64_t> CC = CondOp ? getCondCodeImm(*CondOp) : std::nullopt;
    if (!CC || (*CC != BedrockCC::EQ && *CC != BedrockCC::NE) ||
        !regUnusedAfterInCFG(std::next(I), MBB, ZeroReg, TRI) ||
        !regDeadAfterInCFG(std::next(ConsumerI), MBB, Bedrock::FLAGS, TRI)) {
      ++I;
      continue;
    }

    MachineInstrBuilder MIB = BuildMI(MBB, Cmp.getIterator(), Cmp.getDebugLoc(),
                                      TII.get(MemImmOpcode))
                                  .addImm(0)
                                  .addReg(Base)
                                  .addImm(Offset);
    MIB.cloneMemRefs(Cmp);

    Cmp.eraseFromParent();
    ClrI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldMemoryBitOps(MachineBasicBlock &MBB,
                                           MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Load = *I;
    if (Load.isDebugInstr()) {
      ++I;
      continue;
    }

    Register ValueReg;
    Register BaseReg;
    int64_t Offset = 0;
    if (!isLoadForBitOp(Load, ValueReg, BaseReg, Offset)) {
      ++I;
      continue;
    }

    auto BitI = std::next(I);
    while (BitI != MBB.end() && BitI->isDebugInstr())
      ++BitI;
    if (BitI == MBB.end()) {
      ++I;
      continue;
    }

    MachineInstr &BitOp = *BitI;
    unsigned MemOpcode = getMemBitOpcode(BitOp.getOpcode());
    if (MemOpcode == 0 || BitOp.getNumOperands() < 3 ||
        !BitOp.getOperand(0).isReg() || !BitOp.getOperand(1).isReg() ||
        !BitOp.getOperand(2).isImm() ||
        BitOp.getOperand(0).getReg() != ValueReg ||
        BitOp.getOperand(1).getReg() != ValueReg) {
      ++I;
      continue;
    }

    auto StoreI = std::next(BitI);
    while (StoreI != MBB.end() && StoreI->isDebugInstr())
      ++StoreI;
    if (StoreI == MBB.end() ||
        !isStoreForBitOp(*StoreI, ValueReg, BaseReg, Offset)) {
      ++I;
      continue;
    }

    DebugLoc DL = Load.getDebugLoc();
    BuildMI(MBB, Load.getIterator(), DL, TII.get(MemOpcode))
        .addImm(BitOp.getOperand(2).getImm())
        .addReg(BaseReg)
        .addImm(Offset);

    I = std::next(StoreI);
    Load.eraseFromParent();
    BitOp.eraseFromParent();
    StoreI->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldMemoryBinStore(MachineBasicBlock &MBB,
                                             MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Load = *I;
    if (Load.isDebugInstr()) {
      ++I;
      continue;
    }

    Register Loaded;
    Register Base;
    int64_t Offset = 0;
    if (!isMemLoad(Load, Loaded, Base, Offset)) {
      ++I;
      continue;
    }

    auto AddI = nextNonDebug(I, MBB);
    if (AddI == MBB.end()) {
      ++I;
      continue;
    }

    if (!binOpcodeMatchesLoad(AddI->getOpcode(), Load.getOpcode())) {
      ++I;
      continue;
    }

    Register Result;
    Register Src;
    if (!isBinUsingLoadedValue(*AddI, Loaded, Result, Src, TRI) ||
        regsOverlap(TRI, Loaded, Base) || regsOverlap(TRI, Result, Base)) {
      ++I;
      continue;
    }

    auto StoreI = nextNonDebug(AddI, MBB);
    if (StoreI == MBB.end()) {
      ++I;
      continue;
    }

    Register StoreSrc;
    Register StoreBase;
    int64_t StoreOffset = 0;
    unsigned Opcode = getMemDestBinOpcode(AddI->getOpcode());
    if (!isMemStore(*StoreI, StoreSrc, StoreBase, StoreOffset) ||
        getStoreOpcodeForLoad(Load.getOpcode()) != StoreI->getOpcode() ||
        !regsOverlap(TRI, StoreSrc, Result) ||
        !regsOverlap(TRI, StoreBase, Base) || StoreOffset != Offset ||
        (!regsOverlap(TRI, Loaded, Result) &&
         !regDeadAfter(std::next(AddI), MBB, Loaded, TRI)) ||
        (!operandIsKill(*StoreI, Result, TRI) &&
         !regDeadAfter(std::next(StoreI), MBB, Result, TRI))) {
      ++I;
      continue;
    }

    BuildMI(MBB, Load.getIterator(), Load.getDebugLoc(), TII.get(Opcode))
        .addReg(Src)
        .addReg(Base)
        .addImm(Offset);

    Load.eraseFromParent();
    AddI->eraseFromParent();
    StoreI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldMemoryBinStoreAcrossDef(
    MachineBasicBlock &MBB, MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Load = *I;
    if (Load.isDebugInstr() || hasOrderedMemOperand(Load)) {
      ++I;
      continue;
    }

    Register Loaded;
    Register Base;
    int64_t Offset = 0;
    if (!isMemLoad(Load, Loaded, Base, Offset)) {
      ++I;
      continue;
    }

    auto AddI = nextNonDebug(I, MBB);
    if (AddI == MBB.end() ||
        !binOpcodeMatchesLoad(AddI->getOpcode(), Load.getOpcode())) {
      ++I;
      continue;
    }

    Register Result;
    Register Src;
    if (!isBinUsingLoadedValue(*AddI, Loaded, Result, Src, TRI) ||
        !isDReg(Src) || !regsOverlap(TRI, Loaded, Result) ||
        regsOverlap(TRI, Loaded, Base) || regsOverlap(TRI, Src, Base) ||
        hasOrderedMemOperand(*AddI) || !operandIsKill(*AddI, Src, TRI)) {
      ++I;
      continue;
    }

    auto DefI = nextNonDebug(AddI, MBB);
    auto StoreI = DefI == MBB.end() ? MBB.end() : nextNonDebug(DefI, MBB);
    if (DefI == MBB.end() || StoreI == MBB.end() ||
        hasOrderedMemOperand(*DefI) || hasOrderedMemOperand(*StoreI)) {
      ++I;
      continue;
    }

    if (!instrDefinesReg(*DefI, Src, TRI) || instrUsesReg(*DefI, Src, TRI) ||
        instrTouchesReg(*DefI, Loaded, TRI) ||
        instrTouchesReg(*DefI, Base, TRI)) {
      ++I;
      continue;
    }

    Register StoreSrc;
    Register StoreBase;
    int64_t StoreOffset = 0;
    unsigned Opcode = getMemDestBinOpcode(AddI->getOpcode());
    if (Opcode == 0 || !isMemStore(*StoreI, StoreSrc, StoreBase, StoreOffset) ||
        StoreI->getOpcode() != getStoreOpcodeForLoad(Load.getOpcode()) ||
        !regsOverlap(TRI, StoreSrc, Result) ||
        !regsOverlap(TRI, StoreBase, Base) || StoreOffset != Offset ||
        (!operandIsKill(*StoreI, Result, TRI) &&
         !regDeadAfter(std::next(StoreI), MBB, Result, TRI))) {
      ++I;
      continue;
    }

    bool RewroteDef = false;
    for (MachineOperand &MO : DefI->operands()) {
      if (!MO.isReg() || !MO.isDef() || !regsOverlap(TRI, MO.getReg(), Src))
        continue;
      MO.setReg(Loaded);
      RewroteDef = true;
    }
    if (!RewroteDef) {
      ++I;
      continue;
    }

    MachineInstrBuilder MemAdd =
        BuildMI(MBB, StoreI, AddI->getDebugLoc(), TII.get(Opcode))
            .addReg(Src, RegState::Kill)
            .addReg(Base)
            .addImm(Offset);
    MemAdd.cloneMergedMemRefs({&Load, &*StoreI});

    for (auto Scan = nextNonDebug(StoreI, MBB); Scan != MBB.end();
         Scan = nextNonDebug(Scan, MBB)) {
      if (Scan->isDebugInstr())
        continue;
      if (instrDefinesReg(*Scan, Src, TRI))
        break;
      for (MachineOperand &MO : Scan->operands())
        if (MO.isReg() && MO.readsReg() && regsOverlap(TRI, MO.getReg(), Src))
          MO.setReg(Loaded);
    }

    Load.eraseFromParent();
    AddI->eraseFromParent();
    StoreI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldMemoryBinStoreWithLoadedSource(
    MachineBasicBlock &MBB, MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &AccLoad = *I;
    if (AccLoad.isDebugInstr()) {
      ++I;
      continue;
    }

    Register Acc;
    Register AccBase;
    int64_t AccOffset = 0;
    if (!isMemLoad(AccLoad, Acc, AccBase, AccOffset) ||
        AccBase != Bedrock::SP || hasOrderedMemOperand(AccLoad)) {
      ++I;
      continue;
    }

    auto SrcLoadI = nextNonDebug(I, MBB);
    if (SrcLoadI == MBB.end()) {
      ++I;
      continue;
    }

    Register Src;
    Register SrcBase;
    int64_t SrcOffset = 0;
    if (!isMemLoad(*SrcLoadI, Src, SrcBase, SrcOffset) ||
        SrcBase != Bedrock::SP || hasOrderedMemOperand(*SrcLoadI) ||
        regsOverlap(TRI, Acc, Src)) {
      ++I;
      continue;
    }

    auto AddI = nextNonDebug(SrcLoadI, MBB);
    if (AddI == MBB.end() ||
        !binOpcodeMatchesLoad(AddI->getOpcode(), AccLoad.getOpcode()) ||
        !binOpcodeMatchesLoad(AddI->getOpcode(), SrcLoadI->getOpcode()) ||
        AddI->getNumOperands() < 3 || !AddI->getOperand(0).isReg() ||
        !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg() ||
        !regsOverlap(TRI, AddI->getOperand(0).getReg(), Acc) ||
        !regsOverlap(TRI, AddI->getOperand(1).getReg(), Acc) ||
        !regsOverlap(TRI, AddI->getOperand(2).getReg(), Src)) {
      ++I;
      continue;
    }

    auto StoreI = nextNonDebug(AddI, MBB);
    if (StoreI == MBB.end()) {
      ++I;
      continue;
    }

    Register StoreSrc;
    Register StoreBase;
    int64_t StoreOffset = 0;
    unsigned Opcode = getMemDestBinOpcode(AddI->getOpcode());
    if (Opcode == 0 || !isMemStore(*StoreI, StoreSrc, StoreBase, StoreOffset) ||
        StoreI->getOpcode() != getStoreOpcodeForLoad(AccLoad.getOpcode()) ||
        !regsOverlap(TRI, StoreSrc, Acc) || StoreBase != Bedrock::SP ||
        StoreOffset != AccOffset ||
        (!operandIsKill(*StoreI, Acc, TRI) &&
         !regDeadAfter(std::next(StoreI), MBB, Acc, TRI))) {
      ++I;
      continue;
    }

    BuildMI(MBB, AddI, AddI->getDebugLoc(), TII.get(Opcode))
        .addReg(Src, getKillRegState(operandIsKill(*AddI, Src, TRI)))
        .addReg(AccBase)
        .addImm(AccOffset);

    AccLoad.eraseFromParent();
    AddI->eraseFromParent();
    StoreI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldMemoryImmBinStore(MachineBasicBlock &MBB,
                                                MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Load = *I;
    if (Load.isDebugInstr()) {
      ++I;
      continue;
    }

    Register Loaded;
    Register Base;
    int64_t Offset = 0;
    if (!isMemLoad(Load, Loaded, Base, Offset)) {
      ++I;
      continue;
    }

    auto AddI = nextNonDebug(I, MBB);
    if (AddI == MBB.end()) {
      ++I;
      continue;
    }

    Register Result;
    int64_t Imm = 0;
    if (!binOpcodeMatchesLoad(AddI->getOpcode(), Load.getOpcode()) ||
        !isImmBinUsingLoadedValue(*AddI, Loaded, Result, Imm, TRI) ||
        regsOverlap(TRI, Loaded, Base) || regsOverlap(TRI, Result, Base)) {
      ++I;
      continue;
    }

    auto StoreI = nextNonDebug(AddI, MBB);
    if (StoreI == MBB.end()) {
      ++I;
      continue;
    }

    Register StoreSrc;
    Register StoreBase;
    int64_t StoreOffset = 0;
    unsigned Opcode = getMemDestImmBinOpcode(AddI->getOpcode());
    if (!isMemStore(*StoreI, StoreSrc, StoreBase, StoreOffset) ||
        getStoreOpcodeForLoad(Load.getOpcode()) != StoreI->getOpcode() ||
        !regsOverlap(TRI, StoreSrc, Result) ||
        !regsOverlap(TRI, StoreBase, Base) || StoreOffset != Offset ||
        (!operandIsKill(*StoreI, Result, TRI) &&
         !regDeadAfter(std::next(StoreI), MBB, Result, TRI))) {
      ++I;
      continue;
    }

    unsigned IncDecOpcode = getIncDecMemOpcode(Opcode, Imm);
    if (IncDecOpcode != 0) {
      BuildMI(MBB, Load.getIterator(), AddI->getDebugLoc(),
              TII.get(IncDecOpcode))
          .addReg(Base)
          .addImm(Offset);
    } else {
      unsigned EmitOpcode = Opcode;
      int64_t EncImm = Imm;
      if (EncImm < 0) {
        unsigned OppositeOpcode = getOppositeAddSubMemImmOpcode(Opcode);
        if (OppositeOpcode != 0) {
          EmitOpcode = OppositeOpcode;
          EncImm = -EncImm;
        }
      }
      if (!fitsImm6(EncImm)) {
        ++I;
        continue;
      }
      BuildMI(MBB, Load.getIterator(), AddI->getDebugLoc(), TII.get(EmitOpcode))
          .addImm(EncImm)
          .addReg(Base)
          .addImm(Offset);
    }

    Load.eraseFromParent();
    AddI->eraseFromParent();
    StoreI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldMemoryRegFlagOp(MachineBasicBlock &MBB,
                                              MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Load = *I;
    if (Load.isDebugInstr()) {
      ++I;
      continue;
    }

    Register Loaded;
    Register Base;
    Register Index;
    int64_t Offset = 0;
    unsigned Scale = 0;
    bool LongIndex = false;
    bool IndexedLoad =
        isIndexedMemLoad(Load, Loaded, Base, Index, Offset, Scale, LongIndex);
    if (!IndexedLoad && (!isMemLoad(Load, Loaded, Base, Offset) ||
                         regsOverlap(TRI, Loaded, Base))) {
      ++I;
      continue;
    }
    if (hasOrderedMemOperand(Load)) {
      ++I;
      continue;
    }

    auto IsCandidate = [&](MachineBasicBlock::iterator FlagI, bool &LoadedIsLHS,
                           Register &Other, unsigned &Opcode) {
      if (FlagI == MBB.end() || FlagI->getNumOperands() < 2 ||
          !FlagI->getOperand(0).isReg() || !FlagI->getOperand(1).isReg())
        return false;
      LoadedIsLHS = regsOverlap(TRI, FlagI->getOperand(0).getReg(), Loaded);
      bool LoadedIsRHS =
          regsOverlap(TRI, FlagI->getOperand(1).getReg(), Loaded);
      if (LoadedIsLHS == LoadedIsRHS)
        return false;
      Opcode = IndexedLoad
                   ? getIndexedMemRegFlagOpcode(FlagI->getOpcode(), Scale,
                                                LongIndex, LoadedIsLHS)
                   : getMemRegFlagOpcode(FlagI->getOpcode(), Load.getOpcode(),
                                         LoadedIsLHS);
      if (Opcode == 0)
        return false;
      Other = LoadedIsLHS ? FlagI->getOperand(1).getReg()
                          : FlagI->getOperand(0).getReg();
      return true;
    };

    auto CanSkip = [&](const MachineInstr &MI) {
      return !MI.isTerminator() && !MI.isCall() &&
             !MI.hasUnmodeledSideEffects() && !MI.mayLoadOrStore() &&
             !instrTouchesReg(MI, Bedrock::FLAGS, TRI) &&
             !instrTouchesReg(MI, Loaded, TRI) &&
             !instrTouchesReg(MI, Base, TRI) &&
             (!IndexedLoad || !instrTouchesReg(MI, Index, TRI));
    };

    auto FlagI = nextNonDebug(I, MBB);
    bool LoadedIsLHS = false;
    Register Other;
    unsigned Opcode = 0;
    while (FlagI != MBB.end() &&
           !IsCandidate(FlagI, LoadedIsLHS, Other, Opcode) && CanSkip(*FlagI))
      FlagI = nextNonDebug(FlagI, MBB);

    if (FlagI == MBB.end() || !IsCandidate(FlagI, LoadedIsLHS, Other, Opcode) ||
        (!operandIsKill(*FlagI, Loaded, TRI) &&
         !regUnusedAfterInCFG(std::next(FlagI), MBB, Loaded, TRI))) {
      ++I;
      continue;
    }

    bool OtherClobbered = false;
    for (auto Scan = nextNonDebug(I, MBB); Scan != FlagI;
         Scan = nextNonDebug(Scan, MBB)) {
      if (instrTouchesReg(*Scan, Other, TRI)) {
        OtherClobbered = true;
        break;
      }
    }
    if (OtherClobbered) {
      ++I;
      continue;
    }

    MachineInstrBuilder MIB =
        BuildMI(MBB, Load.getIterator(), FlagI->getDebugLoc(), TII.get(Opcode))
            .addReg(Other, getKillRegState(operandIsKill(*FlagI, Other, TRI)));
    if (IndexedLoad)
      MIB.addReg(Base).addReg(Index).addImm(Offset);
    else
      MIB.addReg(Base).addImm(Offset);
    MIB.cloneMemRefs(Load);

    Load.eraseFromParent();
    FlagI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldMemoryImmFlagOp(MachineBasicBlock &MBB,
                                              MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Load = *I;
    if (Load.isDebugInstr()) {
      ++I;
      continue;
    }

    Register Loaded;
    Register Base;
    int64_t Offset = 0;
    if (!isMemLoad(Load, Loaded, Base, Offset)) {
      ++I;
      continue;
    }

    auto OpI = nextNonDebug(I, MBB);
    if (OpI == MBB.end()) {
      ++I;
      continue;
    }

    auto CanSkip = [&](const MachineInstr &MI,
                       ArrayRef<Register> ProtectedRegs) {
      if (MI.isTerminator() || MI.isCall() || MI.hasUnmodeledSideEffects() ||
          instrTouchesReg(MI, Bedrock::FLAGS, TRI))
        return false;
      for (Register Reg : ProtectedRegs)
        if (Reg.isValid() && instrTouchesReg(MI, Reg, TRI))
          return false;
      return true;
    };

    unsigned TestOpcode = getMemTestOpcodeForAndImm(OpI->getOpcode());
    if (TestOpcode != 0 &&
        binOpcodeMatchesLoad(OpI->getOpcode(), Load.getOpcode()) &&
        OpI->getNumOperands() >= 3 && OpI->getOperand(0).isReg() &&
        OpI->getOperand(1).isReg() && OpI->getOperand(2).isImm()) {
      Register AndDst = OpI->getOperand(0).getReg();
      Register AndLHS = OpI->getOperand(1).getReg();
      int64_t Mask = OpI->getOperand(2).getImm();
      auto CmpI = nextNonDebug(OpI, MBB);
      SmallVector<Register, 2> KnownZeroRegs;
      auto IsKnownZero = [&](Register Reg) {
        for (Register ZeroReg : KnownZeroRegs)
          if (regsOverlap(TRI, Reg, ZeroReg))
            return true;
        return false;
      };
      auto IsZeroCmpCandidate = [&]() {
        return CmpI != MBB.end() &&
               (cmpZeroOpcodeMatchesAndImm(CmpI->getOpcode(),
                                           OpI->getOpcode()) ||
                cmpRROpcodeMatchesAndImm(CmpI->getOpcode(), OpI->getOpcode()) ||
                testSelfOpcodeMatchesAndImm(CmpI->getOpcode(),
                                            OpI->getOpcode()));
      };
      auto DefinesZeroReg = [](const MachineInstr &MI, Register &Reg) {
        if (MI.getOpcode() == Bedrock::CLR64r && MI.getNumOperands() >= 1 &&
            MI.getOperand(0).isReg()) {
          Reg = MI.getOperand(0).getReg();
          return true;
        }
        switch (MI.getOpcode()) {
        default:
          return false;
        case Bedrock::MOV8ri:
        case Bedrock::MOV16ri:
        case Bedrock::MOV32ri:
        case Bedrock::MOV64ri:
          if (MI.getNumOperands() >= 2 && MI.getOperand(0).isReg() &&
              MI.getOperand(1).isImm() && MI.getOperand(1).getImm() == 0) {
            Reg = MI.getOperand(0).getReg();
            return true;
          }
          return false;
        }
      };
      while (CmpI != MBB.end() && !IsZeroCmpCandidate() &&
             CanSkip(*CmpI, ArrayRef<Register>({Loaded, AndDst}))) {
        Register ZeroReg;
        if (DefinesZeroReg(*CmpI, ZeroReg))
          KnownZeroRegs.push_back(ZeroReg);
        CmpI = nextNonDebug(CmpI, MBB);
      }
      bool IsCmpZero =
          CmpI != MBB.end() &&
          cmpZeroOpcodeMatchesAndImm(CmpI->getOpcode(), OpI->getOpcode()) &&
          CmpI->getNumOperands() >= 2 && CmpI->getOperand(0).isReg() &&
          CmpI->getOperand(1).isImm() && CmpI->getOperand(1).getImm() == 0 &&
          regsOverlap(TRI, CmpI->getOperand(0).getReg(), AndDst);
      bool IsCmpKnownZero =
          CmpI != MBB.end() &&
          cmpRROpcodeMatchesAndImm(CmpI->getOpcode(), OpI->getOpcode()) &&
          CmpI->getNumOperands() >= 2 && CmpI->getOperand(0).isReg() &&
          CmpI->getOperand(1).isReg() &&
          regsOverlap(TRI, CmpI->getOperand(0).getReg(), AndDst) &&
          IsKnownZero(CmpI->getOperand(1).getReg());
      bool IsTestSelf =
          CmpI != MBB.end() &&
          testSelfOpcodeMatchesAndImm(CmpI->getOpcode(), OpI->getOpcode()) &&
          CmpI->getNumOperands() >= 2 && CmpI->getOperand(0).isReg() &&
          CmpI->getOperand(1).isReg() &&
          regsOverlap(TRI, CmpI->getOperand(0).getReg(), AndDst) &&
          regsOverlap(TRI, CmpI->getOperand(1).getReg(), AndDst);
      if ((IsCmpZero || IsCmpKnownZero || IsTestSelf) && fitsImm6(Mask) &&
          regsOverlap(TRI, AndDst, AndLHS) &&
          regsOverlap(TRI, AndLHS, Loaded) &&
          (regsOverlap(TRI, AndDst, Loaded) ||
           operandIsKill(*OpI, Loaded, TRI) ||
           regUnusedAfterInCFG(std::next(OpI), MBB, Loaded, TRI)) &&
          (operandIsKill(*CmpI, AndDst, TRI) ||
           regUnusedAfterInCFG(std::next(CmpI), MBB, AndDst, TRI))) {
        BuildMI(MBB, Load.getIterator(), CmpI->getDebugLoc(),
                TII.get(TestOpcode))
            .addImm(Mask)
            .addReg(Base)
            .addImm(Offset);

        Load.eraseFromParent();
        OpI->eraseFromParent();
        CmpI->eraseFromParent();
        I = MBB.begin();
        Changed = true;
        continue;
      }
    }

    auto FlagI = OpI;
    while (FlagI != MBB.end() && getMemImmFlagOpcode(FlagI->getOpcode()) == 0 &&
           CanSkip(*FlagI, ArrayRef<Register>({Loaded})))
      FlagI = nextNonDebug(FlagI, MBB);
    if (FlagI == MBB.end()) {
      ++I;
      continue;
    }
    unsigned Opcode = getMemImmFlagOpcode(FlagI->getOpcode());
    if (Opcode == 0 ||
        !flagImmOpcodeMatchesLoad(FlagI->getOpcode(), Load.getOpcode()) ||
        FlagI->getNumOperands() < 2 || !FlagI->getOperand(0).isReg() ||
        !FlagI->getOperand(1).isImm() ||
        !regsOverlap(TRI, FlagI->getOperand(0).getReg(), Loaded) ||
        (!operandIsKill(*FlagI, Loaded, TRI) &&
         !regUnusedAfterInCFG(std::next(FlagI), MBB, Loaded, TRI))) {
      ++I;
      continue;
    }

    int64_t Imm = FlagI->getOperand(1).getImm();
    if (!canEncodeMemImmFlagOpcode(FlagI->getOpcode(), Imm)) {
      ++I;
      continue;
    }

    BuildMI(MBB, Load.getIterator(), FlagI->getDebugLoc(), TII.get(Opcode))
        .addImm(Imm)
        .addReg(Base)
        .addImm(Offset);

    Load.eraseFromParent();
    FlagI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldKnownZeroByteStoreToBSet(
    MachineBasicBlock &MBB, MachineFunction &MF) const {
  MachineBasicBlock *Pred = nullptr;
  for (MachineBasicBlock *Candidate : MBB.predecessors()) {
    if (Pred)
      return false;
    Pred = Candidate;
  }
  if (!Pred)
    return false;

  MachineBasicBlock::iterator StoreI = MBB.begin();
  for (; StoreI != MBB.end(); StoreI = nextNonDebug(StoreI, MBB)) {
    if (StoreI->isDebugInstr())
      continue;
    if (StoreI->getOpcode() == Bedrock::MOV8mi &&
        StoreI->getNumOperands() >= 3 && StoreI->getOperand(0).isImm() &&
        StoreI->getOperand(0).getImm() == 1 && StoreI->getOperand(1).isReg() &&
        StoreI->getOperand(2).isImm())
      break;
    if (StoreI->isTerminator() || StoreI->isCall())
      return false;
  }
  if (StoreI == MBB.end() || hasOrderedMemOperand(*StoreI))
    return false;

  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  if (!regDeadAfter(nextNonDebug(StoreI, MBB), MBB, Bedrock::FLAGS, TRI))
    return false;

  MachineBasicBlock::iterator BranchI = Pred->getLastNonDebugInstr();
  if (BranchI == Pred->end() || BranchI->getOpcode() != Bedrock::JCC ||
      BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
      BranchI->getOperand(0).getMBB() != &MBB)
    return false;

  std::optional<int64_t> CC = getCondCodeImm(BranchI->getOperand(1));
  if (!CC || *CC != BedrockCC::EQ)
    return false;

  MachineBasicBlock::iterator CmpI = prevNonDebug(BranchI, *Pred);
  if (CmpI == Pred->end() || CmpI->getOpcode() != Bedrock::CMP8mi ||
      CmpI->getNumOperands() < 3 || !CmpI->getOperand(0).isImm() ||
      CmpI->getOperand(0).getImm() != 0 || !CmpI->getOperand(1).isReg() ||
      !CmpI->getOperand(2).isImm() || hasOrderedMemOperand(*CmpI))
    return false;

  if (CmpI->getOperand(1).getReg() != StoreI->getOperand(1).getReg() ||
      CmpI->getOperand(2).getImm() != StoreI->getOperand(2).getImm())
    return false;

  Register Base = StoreI->getOperand(1).getReg();
  int64_t Offset = StoreI->getOperand(2).getImm();
  auto ByteRangeOverlaps = [&](Register OtherBase, int64_t OtherOffset,
                               unsigned OtherSize) {
    if (OtherSize == 0 || !regsOverlap(TRI, OtherBase, Base))
      return OtherSize == 0;
    return OtherOffset < Offset + 1 &&
           Offset < OtherOffset + int64_t(OtherSize);
  };
  auto KnownAccessAliasesByte =
      [&](const MachineInstr &MI) -> std::optional<bool> {
    Register MemReg;
    Register MemBase;
    int64_t MemOffset = 0;
    if (isMemLoad(MI, MemReg, MemBase, MemOffset) ||
        isMemStore(MI, MemReg, MemBase, MemOffset))
      return ByteRangeOverlaps(MemBase, MemOffset,
                               memSizeForOpcode(MI.getOpcode()));

    switch (MI.getOpcode()) {
    default:
      return std::nullopt;
    case Bedrock::MOV8mm:
    case Bedrock::MOV16mm:
    case Bedrock::MOV32mm:
    case Bedrock::MOV64mm:
      if (MI.getNumOperands() < 4 || !MI.getOperand(0).isReg() ||
          !MI.getOperand(1).isImm() || !MI.getOperand(2).isReg() ||
          !MI.getOperand(3).isImm())
        return true;
      return ByteRangeOverlaps(MI.getOperand(0).getReg(),
                               MI.getOperand(1).getImm(),
                               memSizeForOpcode(MI.getOpcode())) ||
             ByteRangeOverlaps(MI.getOperand(2).getReg(),
                               MI.getOperand(3).getImm(),
                               memSizeForOpcode(MI.getOpcode()));
    }
  };
  auto IsPlainMemMove = [](unsigned Opcode) {
    switch (Opcode) {
    default:
      return false;
    case Bedrock::MOV8mm:
    case Bedrock::MOV16mm:
    case Bedrock::MOV32mm:
    case Bedrock::MOV64mm:
      return true;
    }
  };
  for (MachineBasicBlock::iterator Scan = MBB.begin(); Scan != StoreI;
       Scan = nextNonDebug(Scan, MBB)) {
    if (Scan->isDebugInstr())
      continue;
    std::optional<bool> AliasesKnownByte = KnownAccessAliasesByte(*Scan);
    if (AliasesKnownByte && !*AliasesKnownByte &&
        IsPlainMemMove(Scan->getOpcode()))
      continue;
    if (Scan->isTerminator() || Scan->isCall() ||
        (Scan->hasUnmodeledSideEffects() && !AliasesKnownByte) ||
        instrDefinesReg(*Scan, Base, TRI) ||
        instrHasRegMaskForReg(*Scan, Base, TRI) ||
        (AliasesKnownByte ? *AliasesKnownByte : Scan->mayLoadOrStore()))
      return false;
  }

  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  MachineInstrBuilder BSet =
      BuildMI(MBB, StoreI, StoreI->getDebugLoc(), TII.get(Bedrock::BSET8mi))
          .addImm(0)
          .addReg(Base)
          .addImm(Offset);
  BSet.setMIFlags(StoreI->getFlags());
  StoreI->eraseFromParent();
  return true;
}

bool BedrockPushPopMerge::foldIndexedZeroStore(MachineBasicBlock &MBB,
                                               MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Ext = *I;
    if (Ext.isDebugInstr()) {
      ++I;
      continue;
    }
    if (Ext.getOpcode() != Bedrock::EXTZQ32rr || Ext.getNumOperands() < 2 ||
        !Ext.getOperand(0).isReg() || !Ext.getOperand(1).isReg()) {
      ++I;
      continue;
    }

    Register AddrReg = Ext.getOperand(0).getReg();
    Register IndexReg = Ext.getOperand(1).getReg();
    if (!isDReg(IndexReg)) {
      ++I;
      continue;
    }

    auto ShlI = nextNonDebug(I, MBB);
    auto MovBaseI = ShlI == MBB.end() ? MBB.end() : nextNonDebug(ShlI, MBB);
    auto AddI = MovBaseI == MBB.end() ? MBB.end() : nextNonDebug(MovBaseI, MBB);
    auto ClrI = AddI == MBB.end() ? MBB.end() : nextNonDebug(AddI, MBB);
    auto StoreI = ClrI == MBB.end() ? MBB.end() : nextNonDebug(ClrI, MBB);
    if (ShlI == MBB.end() || MovBaseI == MBB.end() || AddI == MBB.end() ||
        ClrI == MBB.end() || StoreI == MBB.end()) {
      ++I;
      continue;
    }

    if (ShlI->getOpcode() != Bedrock::SHL64ri || ShlI->getNumOperands() < 3 ||
        !ShlI->getOperand(0).isReg() || !ShlI->getOperand(1).isReg() ||
        !ShlI->getOperand(2).isImm() || ShlI->getOperand(2).getImm() != 2 ||
        !regsOverlap(TRI, ShlI->getOperand(0).getReg(), AddrReg) ||
        !regsOverlap(TRI, ShlI->getOperand(1).getReg(), AddrReg) ||
        (!instrDefinesDeadReg(*ShlI, Bedrock::FLAGS, TRI) &&
         !regDeadAfter(std::next(ShlI), MBB, Bedrock::FLAGS, TRI))) {
      ++I;
      continue;
    }

    if (MovBaseI->getOpcode() != Bedrock::MOV64rr ||
        MovBaseI->getNumOperands() < 2 || !MovBaseI->getOperand(0).isReg() ||
        !MovBaseI->getOperand(1).isReg()) {
      ++I;
      continue;
    }
    Register BaseTmp = MovBaseI->getOperand(0).getReg();
    Register BaseReg = MovBaseI->getOperand(1).getReg();
    if (regsOverlap(TRI, BaseTmp, BaseReg) ||
        regsOverlap(TRI, BaseTmp, IndexReg)) {
      ++I;
      continue;
    }

    if (AddI->getOpcode() != Bedrock::ADD64rr || AddI->getNumOperands() < 3 ||
        !AddI->getOperand(0).isReg() || !AddI->getOperand(1).isReg() ||
        !AddI->getOperand(2).isReg() ||
        !regsOverlap(TRI, AddI->getOperand(0).getReg(), BaseTmp) ||
        !regsOverlap(TRI, AddI->getOperand(1).getReg(), BaseTmp) ||
        !regsOverlap(TRI, AddI->getOperand(2).getReg(), AddrReg)) {
      ++I;
      continue;
    }

    if (ClrI->getOpcode() != Bedrock::CLR64r || ClrI->getNumOperands() < 1 ||
        !ClrI->getOperand(0).isReg()) {
      ++I;
      continue;
    }
    Register ZeroReg = ClrI->getOperand(0).getReg();

    if (StoreI->getOpcode() != Bedrock::MOV32mr ||
        StoreI->getNumOperands() < 3 || !StoreI->getOperand(0).isReg() ||
        !StoreI->getOperand(1).isReg() || !StoreI->getOperand(2).isImm() ||
        StoreI->getOperand(2).getImm() != 0 ||
        !regsOverlap(TRI, StoreI->getOperand(0).getReg(), ZeroReg) ||
        !regsOverlap(TRI, StoreI->getOperand(1).getReg(), BaseTmp) ||
        hasOrderedMemOperand(*StoreI) ||
        !regDeadAfter(std::next(StoreI), MBB, BaseTmp, TRI)) {
      ++I;
      continue;
    }

    bool KeepZeroReg = !regDeadAfter(std::next(StoreI), MBB, ZeroReg, TRI);
    DebugLoc DL = StoreI->getDebugLoc();
    BuildMI(MBB, Ext.getIterator(), DL, TII.get(Bedrock::CLR64r), BaseTmp);
    MachineInstrBuilder Store =
        BuildMI(MBB, Ext.getIterator(), DL, TII.get(Bedrock::MOV32idx4lmr))
            .addReg(BaseTmp)
            .addReg(BaseReg)
            .addReg(IndexReg)
            .addImm(0);
    Store.cloneMemRefs(*StoreI);
    if (KeepZeroReg)
      BuildMI(MBB, Ext.getIterator(), ClrI->getDebugLoc(),
              TII.get(Bedrock::CLR64r), ZeroReg);

    Ext.eraseFromParent();
    ShlI->eraseFromParent();
    MovBaseI->eraseFromParent();
    AddI->eraseFromParent();
    ClrI->eraseFromParent();
    StoreI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldByteLoadTestZeroBranch(
    MachineBasicBlock &MBB, MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Load = *I;
    if (Load.isDebugInstr()) {
      ++I;
      continue;
    }

    Register Loaded, Base;
    int64_t Offset = 0;
    if (Load.getOpcode() != Bedrock::MOV8rm ||
        !isMemLoad(Load, Loaded, Base, Offset) || hasOrderedMemOperand(Load)) {
      ++I;
      continue;
    }

    auto TestI = nextNonDebug(I, MBB);
    if (TestI == MBB.end() || TestI->getOpcode() != Bedrock::TEST8rr ||
        TestI->getNumOperands() < 2 || !TestI->getOperand(0).isReg() ||
        !TestI->getOperand(1).isReg() ||
        !regsOverlap(TRI, TestI->getOperand(0).getReg(), Loaded) ||
        !regsOverlap(TRI, TestI->getOperand(1).getReg(), Loaded)) {
      ++I;
      continue;
    }

    auto BranchI = nextNonDebug(TestI, MBB);
    if (BranchI == MBB.end() || BranchI->getOpcode() != Bedrock::JCC ||
        BranchI->getNumOperands() < 2) {
      ++I;
      continue;
    }
    std::optional<int64_t> CC = getCondCodeImm(BranchI->getOperand(1));
    if (!CC || (*CC != BedrockCC::EQ && *CC != BedrockCC::NE) ||
        !regUnusedAfterInCFG(std::next(TestI), MBB, Loaded, TRI)) {
      ++I;
      continue;
    }

    MachineInstrBuilder MIB =
        BuildMI(MBB, Load.getIterator(), TestI->getDebugLoc(),
                TII.get(Bedrock::CMP8mi))
            .addImm(0)
            .addReg(Base)
            .addImm(Offset);
    MIB.cloneMemRefs(Load);

    Load.eraseFromParent();
    TestI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldByteLoadKnownZeroCmpBranch(
    MachineBasicBlock &MBB, MachineFunction &MF, uint32_t KnownZeroIn) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  uint32_t KnownZero = KnownZeroIn;

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Load = *I;
    if (Load.isDebugInstr()) {
      ++I;
      continue;
    }

    Register Loaded, Base;
    int64_t Offset = 0;
    if (Load.getOpcode() != Bedrock::MOV8rm ||
        !isMemLoad(Load, Loaded, Base, Offset) || hasOrderedMemOperand(Load)) {
      transferKnownZero(Load, KnownZero, TRI);
      ++I;
      continue;
    }

    auto CmpI = nextNonDebug(I, MBB);
    if (CmpI == MBB.end() || CmpI->getOpcode() != Bedrock::CMP8rr ||
        CmpI->getNumOperands() < 2 || !CmpI->getOperand(0).isReg() ||
        !CmpI->getOperand(1).isReg()) {
      transferKnownZero(Load, KnownZero, TRI);
      ++I;
      continue;
    }

    auto BranchI = nextNonDebug(CmpI, MBB);
    if (BranchI == MBB.end() || !isEqNeBranch(*BranchI)) {
      transferKnownZero(Load, KnownZero, TRI);
      ++I;
      continue;
    }

    Register LHS = CmpI->getOperand(0).getReg();
    Register RHS = CmpI->getOperand(1).getReg();
    bool LHSLoaded = regsOverlap(TRI, LHS, Loaded);
    bool RHSLoaded = regsOverlap(TRI, RHS, Loaded);
    if (LHSLoaded == RHSLoaded) {
      transferKnownZero(Load, KnownZero, TRI);
      ++I;
      continue;
    }

    Register ZeroReg = LHSLoaded ? RHS : LHS;
    if (!maskHasKnownZeroReg(KnownZero, ZeroReg, TRI) ||
        !regUnusedAfterInCFG(std::next(CmpI), MBB, Loaded, TRI)) {
      transferKnownZero(Load, KnownZero, TRI);
      ++I;
      continue;
    }

    MachineInstrBuilder MIB =
        BuildMI(MBB, Load.getIterator(), CmpI->getDebugLoc(),
                TII.get(Bedrock::CMP8mi))
            .addImm(0)
            .addReg(Base)
            .addImm(Offset);
    MIB.cloneMemRefs(Load);

    Load.eraseFromParent();
    CmpI->eraseFromParent();
    I = MBB.begin();
    KnownZero = KnownZeroIn;
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldImmStore(MachineBasicBlock &MBB,
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

    unsigned NewOpcode = getMovMIOpcode(MovImm.getOpcode());
    unsigned StoreOpcode = getMovMROpcodeForImmOpcode(MovImm.getOpcode());
    if ((MovImm.getOpcode() != Bedrock::MOV8ri &&
         MovImm.getOpcode() != Bedrock::MOV16ri) ||
        NewOpcode == 0 || StoreOpcode == 0 || MovImm.getNumOperands() < 2 ||
        !MovImm.getOperand(0).isReg() || !MovImm.getOperand(1).isImm()) {
      ++I;
      continue;
    }

    Register ValueReg = MovImm.getOperand(0).getReg();
    int64_t Imm = MovImm.getOperand(1).getImm();
    auto StoreI = nextNonDebug(I, MBB);
    if (StoreI == MBB.end() || StoreI->getOpcode() != StoreOpcode ||
        StoreI->getNumOperands() < 3 || !StoreI->getOperand(0).isReg() ||
        !StoreI->getOperand(1).isReg() || !StoreI->getOperand(2).isImm() ||
        !regsOverlap(TRI, StoreI->getOperand(0).getReg(), ValueReg) ||
        regsOverlap(TRI, StoreI->getOperand(1).getReg(), ValueReg) ||
        hasOrderedMemOperand(*StoreI) ||
        (!operandIsKill(*StoreI, ValueReg, TRI) &&
         !regDeadAfter(std::next(StoreI), MBB, ValueReg, TRI))) {
      ++I;
      continue;
    }

    MachineInstrBuilder MIB = BuildMI(MBB, MovImm.getIterator(),
                                      StoreI->getDebugLoc(), TII.get(NewOpcode))
                                  .addImm(Imm)
                                  .addReg(StoreI->getOperand(1).getReg())
                                  .addImm(StoreI->getOperand(2).getImm());
    MIB.cloneMemRefs(*StoreI);

    MovImm.eraseFromParent();
    StoreI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldSmallMov64Imm(MachineBasicBlock &MBB,
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

    Register Dst = Register();
    int64_t Imm = 0;
    if (isMov64Imm(MovImm, Dst, Imm) && isDReg(Dst)) {
      uint64_t UImm = static_cast<uint64_t>(Imm);
      if (UImm >= 0x80000000ULL && UImm <= 0xffffffffULL) {
        int32_t NarrowImm = static_cast<int32_t>(UImm);
        DebugLoc DL = MovImm.getDebugLoc();
        BuildMI(MBB, MovImm.getIterator(), DL, TII.get(Bedrock::MOV32ri), Dst)
            .addImm(NarrowImm);
        BuildMI(MBB, MovImm.getIterator(), DL, TII.get(Bedrock::EXTZQ32rr), Dst)
            .addReg(Dst);
        MovImm.eraseFromParent();
        I = MBB.begin();
        Changed = true;
        continue;
      }
    }

    bool Matched = isMov64Imm(MovImm, Dst, Imm) && isAReg(Dst);
    if (!Matched && isMov32Imm(MovImm, Dst, Imm) && isAReg(Dst))
      Matched = true;
    if (!Matched || Imm == 0 || Imm < std::numeric_limits<int16_t>::min() ||
        Imm > std::numeric_limits<int16_t>::max()) {
      ++I;
      continue;
    }

    auto Next = std::next(I);
    if (!regDeadAfterInCFG(Next, MBB, Bedrock::FLAGS, TRI)) {
      ++I;
      continue;
    }

    DebugLoc DL = MovImm.getDebugLoc();
    BuildMI(MBB, MovImm.getIterator(), DL, TII.get(Bedrock::CLR64r), Dst);
    BuildMI(MBB, MovImm.getIterator(), DL, TII.get(Bedrock::ADD64ri), Dst)
        .addReg(Dst)
        .addImm(Imm);
    MovImm.eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldClrStore(MachineBasicBlock &MBB,
                                       MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Store = *I++;
    if (Store.isDebugInstr())
      continue;

    Register StoreSrc;
    Register Base;
    int64_t Offset = 0;
    if (Store.getOpcode() != Bedrock::MOV64mr ||
        !isMemStore(Store, StoreSrc, Base, Offset) || Base == Bedrock::SP ||
        hasOrderedMemOperand(Store) || regsOverlap(TRI, StoreSrc, Base))
      continue;

    MachineInstr *Zero =
        findRemovableZeroDefBefore(MBB, Store.getIterator(), StoreSrc, TRI);
    if (!Zero)
      continue;
    if ((Zero->getFlags() & (Bedrock::RepgStart | Bedrock::RepgEnd)) != 0)
      continue;

    bool SrcDead =
        operandIsKill(Store, StoreSrc, TRI) ||
        regUnusedAfterInCFG(std::next(Store.getIterator()), MBB, StoreSrc, TRI);
    if (!SrcDead)
      continue;

    MachineInstrBuilder MIB =
        BuildMI(MBB, Store.getIterator(), Store.getDebugLoc(),
                TII.get(Bedrock::CLRm))
            .addReg(Base)
            .addImm(Offset)
            .setMIFlags(Store.getFlags());
    MIB.cloneMemRefs(Store);

    Zero->eraseFromParent();
    Store.eraseFromParent();
    removeRegLiveInsWithoutUses(MF, StoreSrc, TRI);
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldAImmCopyToDImm(MachineBasicBlock &MBB,
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

    Register AReg;
    int64_t Imm = 0;
    if (!isMov32Imm(MovImm, AReg, Imm) || !isAReg(AReg)) {
      ++I;
      continue;
    }

    SmallVector<MachineInstr *, 2> Copies;
    bool Invalid = false;
    MachineBasicBlock::iterator Scan = nextNonDebug(I, MBB);
    for (; Scan != MBB.end(); Scan = nextNonDebug(Scan, MBB)) {
      MachineInstr &Use = *Scan;
      if (instrHasRegMaskForReg(Use, AReg, TRI)) {
        Invalid = true;
        break;
      }
      if (instrDefinesReg(Use, AReg, TRI))
        break;
      if (!instrUsesReg(Use, AReg, TRI))
        continue;

      if ((Use.getOpcode() == Bedrock::MOV32rr ||
           Use.getOpcode() == Bedrock::MOV64rr) &&
          Use.getNumOperands() >= 2 && Use.getOperand(0).isReg() &&
          Use.getOperand(1).isReg() &&
          regsOverlap(TRI, Use.getOperand(1).getReg(), AReg) &&
          isDReg(Use.getOperand(0).getReg())) {
        Copies.push_back(&Use);
        if (Copies.size() >= 3) {
          Invalid = true;
          break;
        }
        continue;
      }

      Invalid = true;
      break;
    }

    if (Invalid || Copies.empty()) {
      ++I;
      continue;
    }

    MachineInstr *LastCopy = Copies.back();
    if (!operandIsKill(*LastCopy, AReg, TRI) &&
        !regDeadAfter(std::next(LastCopy->getIterator()), MBB, AReg, TRI)) {
      ++I;
      continue;
    }

    for (MachineInstr *Copy : Copies) {
      BuildMI(MBB, Copy->getIterator(), Copy->getDebugLoc(),
              TII.get(Bedrock::MOV32ri), Copy->getOperand(0).getReg())
          .addImm(Imm);
      Copy->eraseFromParent();
    }
    MovImm.eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldMemCopy(MachineBasicBlock &MBB,
                                      MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Load = *I;
    if (Load.isDebugInstr()) {
      ++I;
      continue;
    }

    Register Tmp, SrcBase;
    int64_t SrcOffset = 0;
    unsigned PlainLoadOpcode = 0;
    bool LoadPost = false;
    if (!isMemCopyLoad(Load, Tmp, SrcBase, SrcOffset, PlainLoadOpcode,
                       LoadPost)) {
      ++I;
      continue;
    }

    auto StoreI = nextNonDebug(I, MBB);
    if (StoreI == MBB.end()) {
      ++I;
      continue;
    }

    Register StoreSrc, DstBase;
    int64_t DstOffset = 0;
    unsigned PlainStoreOpcode = 0;
    bool StorePost = false;
    if (!isMemCopyStore(*StoreI, StoreSrc, DstBase, DstOffset, PlainStoreOpcode,
                        StorePost) ||
        !regsOverlap(TRI, Tmp, StoreSrc) ||
        getStoreOpcodeForLoad(PlainLoadOpcode) != PlainStoreOpcode ||
        !operandIsKill(*StoreI, Tmp, TRI) || hasOrderedMemOperand(Load) ||
        hasOrderedMemOperand(*StoreI)) {
      ++I;
      continue;
    }

    if (!LoadPost && !StorePost && regsOverlap(TRI, SrcBase, DstBase) &&
        SrcOffset == DstOffset) {
      Load.eraseFromParent();
      StoreI->eraseFromParent();
      I = MBB.begin();
      Changed = true;
      continue;
    }

    unsigned Size = memSizeForOpcode(PlainLoadOpcode);
    MachineInstr *SrcInc = !LoadPost && SrcOffset == 0
                               ? findPostInc(StoreI, MBB, SrcBase, Size, TRI)
                               : nullptr;
    MachineInstr *DstInc = !StorePost && DstOffset == 0
                               ? findPostInc(StoreI, MBB, DstBase, Size, TRI)
                               : nullptr;
    bool SrcPost = LoadPost || SrcInc != nullptr;
    bool DstPost = StorePost || DstInc != nullptr;
    if (SrcPost && DstPost &&
        ((SrcInc && SrcInc == DstInc) || regsOverlap(TRI, SrcBase, DstBase))) {
      ++I;
      continue;
    }
    bool EncodedDstPost = DstPost;

    unsigned Opcode = getMovMMOpcode(PlainLoadOpcode, SrcPost, EncodedDstPost);
    if (Opcode == 0) {
      ++I;
      continue;
    }

    MachineInstrBuilder MIB =
        BuildMI(MBB, Load.getIterator(), Load.getDebugLoc(), TII.get(Opcode));
    addMaybePostMemOperand(MIB, SrcBase, SrcOffset, SrcPost);
    addMaybePostMemOperand(MIB, DstBase, DstOffset, EncodedDstPost);

    Load.eraseFromParent();
    StoreI->eraseFromParent();
    if (SrcInc)
      SrcInc->eraseFromParent();
    if (DstInc && EncodedDstPost)
      DstInc->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldLoadOp(MachineBasicBlock &MBB,
                                     MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const bool MinSize = MF.getFunction().hasMinSize();
  SmallVector<StackConstStore, 8> StackConstSlots;
  if (MinSize)
    collectStackConstStores(MF, TRI, StackConstSlots);

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Load = *I;
    if (Load.isDebugInstr()) {
      ++I;
      continue;
    }

    Register IndexedTmp, IndexedBase, IndexedIndex;
    int64_t IndexedOffset = 0;
    unsigned IndexedScale = 0;
    bool LongIndex = false;
    if (isIndexedMemLoad(Load, IndexedTmp, IndexedBase, IndexedIndex,
                         IndexedOffset, IndexedScale, LongIndex)) {
      auto OpI = nextNonDebug(I, MBB);
      if (OpI == MBB.end() || hasOrderedMemOperand(Load) ||
          OpI->getNumOperands() < 3 || !OpI->getOperand(0).isReg() ||
          !OpI->getOperand(1).isReg() || !OpI->getOperand(2).isReg()) {
        ++I;
        continue;
      }

      MachineInstr &Op = *OpI;
      Register Acc = Op.getOperand(0).getReg();
      if (!regsOverlap(TRI, Acc, Op.getOperand(1).getReg()) ||
          !regsOverlap(TRI, IndexedTmp, Op.getOperand(2).getReg()) ||
          regsOverlap(TRI, Acc, IndexedTmp) ||
          regsOverlap(TRI, Acc, IndexedIndex) || isAReg(Acc) ||
          (!operandIsKill(Op, IndexedTmp, TRI) &&
           !regDeadAfter(std::next(OpI), MBB, IndexedTmp, TRI))) {
        ++I;
        continue;
      }

      unsigned Opcode =
          getIndexedMemSourceOpcode(Op.getOpcode(), IndexedScale, LongIndex);
      if (Opcode == 0) {
        ++I;
        continue;
      }

      MachineInstrBuilder MIB = BuildMI(MBB, Load.getIterator(),
                                        Op.getDebugLoc(), TII.get(Opcode), Acc)
                                    .addReg(Acc)
                                    .addReg(IndexedBase)
                                    .addReg(IndexedIndex)
                                    .addImm(IndexedOffset);
      MIB.cloneMemRefs(Load);

      Load.eraseFromParent();
      Op.eraseFromParent();
      I = MBB.begin();
      Changed = true;
      continue;
    }

    Register Tmp, Base;
    int64_t Offset = 0;
    if (!isMemLoad(Load, Tmp, Base, Offset)) {
      ++I;
      continue;
    }

    auto OpI = nextNonDebug(I, MBB);
    if (OpI == MBB.end()) {
      ++I;
      continue;
    }

    if (OpI->getOpcode() == Bedrock::MOV32ri && OpI->getNumOperands() >= 2 &&
        OpI->getOperand(0).isReg() && OpI->getOperand(1).isImm() &&
        !hasOrderedMemOperand(Load)) {
      Register Acc = OpI->getOperand(0).getReg();
      auto FoldI = nextNonDebug(OpI, MBB);
      if (FoldI != MBB.end() && FoldI->getNumOperands() >= 3 &&
          FoldI->getOperand(0).isReg() && FoldI->getOperand(1).isReg() &&
          FoldI->getOperand(2).isReg() &&
          regsOverlap(TRI, FoldI->getOperand(0).getReg(), Acc) &&
          regsOverlap(TRI, FoldI->getOperand(1).getReg(), Acc) &&
          regsOverlap(TRI, FoldI->getOperand(2).getReg(), Tmp) &&
          !regsOverlap(TRI, Acc, Tmp) && !regsOverlap(TRI, Acc, Base) &&
          (operandIsKill(*FoldI, Tmp, TRI) ||
           regDeadAfter(std::next(FoldI), MBB, Tmp, TRI))) {
        unsigned Size = memSizeForOpcode(Load.getOpcode());
        if (!MinSize ||
            (memSourceFoldSavesSizeWithoutPostInc(FoldI->getOpcode()) &&
             !(Base == Bedrock::SP &&
               hasAvailableStackConstStore(StackConstSlots, Load, Offset,
                                           Size)))) {
          unsigned Opcode = getMemSourceOpcode(FoldI->getOpcode(), false);
          if (Opcode != 0 && !isAReg(Acc)) {
            BuildMI(MBB, Load.getIterator(), OpI->getDebugLoc(),
                    TII.get(Bedrock::MOV32ri), Acc)
                .addImm(OpI->getOperand(1).getImm());
            MachineInstrBuilder MIB =
                BuildMI(MBB, Load.getIterator(), FoldI->getDebugLoc(),
                        TII.get(Opcode), Acc)
                    .addReg(Acc);
            addMaybePostMemOperand(MIB, Base, Offset, false);
            MIB.cloneMemRefs(Load);

            Load.eraseFromParent();
            OpI->eraseFromParent();
            FoldI->eraseFromParent();
            I = MBB.begin();
            Changed = true;
            continue;
          }
        }
      }
    }

    MachineInstr &Op = *OpI;
    if (Op.getNumOperands() < 3 || !Op.getOperand(0).isReg() ||
        !Op.getOperand(1).isReg() || !Op.getOperand(2).isReg()) {
      ++I;
      continue;
    }

    Register Acc = Op.getOperand(0).getReg();
    if (!regsOverlap(TRI, Acc, Op.getOperand(1).getReg()) ||
        !regsOverlap(TRI, Tmp, Op.getOperand(2).getReg()) ||
        regsOverlap(TRI, Acc, Tmp)) {
      ++I;
      continue;
    }

    if (!operandIsKill(Op, Tmp, TRI) &&
        !regDeadAfter(std::next(OpI), MBB, Tmp, TRI)) {
      ++I;
      continue;
    }

    unsigned Size = memSizeForOpcode(Load.getOpcode());
    MachineInstr *Inc =
        Offset == 0 ? findPostInc(OpI, MBB, Base, Size, TRI) : nullptr;
    bool PostInc = Inc != nullptr;
    if (MinSize && !PostInc) {
      if (!memSourceFoldSavesSizeWithoutPostInc(Op.getOpcode())) {
        ++I;
        continue;
      }
      if (Base == Bedrock::SP &&
          hasAvailableStackConstStore(StackConstSlots, Load, Offset, Size)) {
        ++I;
        continue;
      }
    }
    if (isAReg(Acc)) {
      ++I;
      continue;
    }
    unsigned Opcode = getMemSourceOpcode(Op.getOpcode(), PostInc);
    if (Opcode == 0) {
      ++I;
      continue;
    }

    MachineInstrBuilder MIB =
        BuildMI(MBB, Load.getIterator(), Op.getDebugLoc(), TII.get(Opcode), Acc)
            .addReg(Acc);
    addMaybePostMemOperand(MIB, Base, Offset, PostInc);

    Load.eraseFromParent();
    Op.eraseFromParent();
    if (Inc)
      Inc->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldLoadMAdd(MachineBasicBlock &MBB,
                                       MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Load = *I;
    if (Load.isDebugInstr()) {
      ++I;
      continue;
    }

    Register Tmp, Base;
    int64_t Offset = 0;
    bool LoadPostInc = false;
    if (!isMAddLoad(Load, Tmp, Base, Offset, LoadPostInc)) {
      ++I;
      continue;
    }

    auto FirstAfterLoad = nextNonDebug(I, MBB);
    if (FirstAfterLoad == MBB.end()) {
      ++I;
      continue;
    }

    Register CoeffLoadReg, CoeffBase;
    int64_t CoeffOffset = 0;
    bool CoeffPostInc = false;
    auto MulI = FirstAfterLoad;
    bool HasCoeffLoad = isMAddLoad(*FirstAfterLoad, CoeffLoadReg, CoeffBase,
                                   CoeffOffset, CoeffPostInc);
    if (HasCoeffLoad)
      MulI = nextNonDebug(FirstAfterLoad, MBB);

    if (MulI == MBB.end()) {
      ++I;
      continue;
    }
    MachineInstr &Mul = *MulI;
    if (Mul.getNumOperands() < 3 || !Mul.getOperand(0).isReg() ||
        !Mul.getOperand(1).isReg() || !Mul.getOperand(2).isReg() ||
        !regsOverlap(TRI, Mul.getOperand(0).getReg(),
                     Mul.getOperand(1).getReg())) {
      ++I;
      continue;
    }
    Register Product = Mul.getOperand(0).getReg();
    Register MulLHS = Mul.getOperand(1).getReg();
    Register MulRHS = Mul.getOperand(2).getReg();
    Register Coeff;
    if (regsOverlap(TRI, Product, Tmp) && regsOverlap(TRI, MulLHS, Tmp) &&
        !regsOverlap(TRI, MulRHS, Tmp)) {
      Coeff = MulRHS;
      HasCoeffLoad = false;
    } else if (HasCoeffLoad && regsOverlap(TRI, Product, CoeffLoadReg) &&
               regsOverlap(TRI, MulLHS, CoeffLoadReg) &&
               regsOverlap(TRI, MulRHS, Tmp)) {
      Coeff = CoeffLoadReg;
    } else {
      ++I;
      continue;
    }

    auto AddI = nextNonDebug(MulI, MBB);
    if (!HasCoeffLoad && !LoadPostInc && AddI != MBB.end() &&
        AddI->getOpcode() == Bedrock::MOV32rm && AddI->getNumOperands() >= 3 &&
        AddI->getOperand(0).isReg() && AddI->getOperand(1).isReg() &&
        AddI->getOperand(2).isImm() && !hasOrderedMemOperand(Load) &&
        !hasOrderedMemOperand(*AddI)) {
      MachineInstr &AccLoad = *AddI;
      auto AccAddI = nextNonDebug(AddI, MBB);
      if (AccAddI != MBB.end() && AccAddI->getOpcode() == Bedrock::ADD32rr &&
          AccAddI->getNumOperands() >= 3 && AccAddI->getOperand(0).isReg() &&
          AccAddI->getOperand(1).isReg() && AccAddI->getOperand(2).isReg()) {
        Register Acc = AccLoad.getOperand(0).getReg();
        Register AccDst = AccAddI->getOperand(0).getReg();
        Register AccLHS = AccAddI->getOperand(1).getReg();
        Register AccRHS = AccAddI->getOperand(2).getReg();
        if (isDReg(Acc) && regsOverlap(TRI, AccDst, Acc) &&
            regsOverlap(TRI, AccLHS, Acc) &&
            regsOverlap(TRI, AccRHS, Product) &&
            !regsOverlap(TRI, Acc, Product) && !regsOverlap(TRI, Acc, Coeff) &&
            !regsOverlap(TRI, Acc, Base) &&
            (operandIsKill(Mul, Tmp, TRI) ||
             regDeadAfter(std::next(MulI), MBB, Tmp, TRI)) &&
            (operandIsKill(*AccAddI, Product, TRI) ||
             regDeadAfter(std::next(AccAddI), MBB, Product, TRI))) {
          unsigned Opcode = getMAddMemOpcode(Mul.getOpcode(), false);
          if (Opcode != 0) {
            BuildMI(MBB, Load.getIterator(), AccLoad.getDebugLoc(),
                    TII.get(Bedrock::MOV32rm), Acc)
                .addReg(AccLoad.getOperand(1).getReg())
                .addImm(AccLoad.getOperand(2).getImm())
                .cloneMemRefs(AccLoad);
            MachineInstrBuilder MIB =
                BuildMI(MBB, Load.getIterator(), AccAddI->getDebugLoc(),
                        TII.get(Opcode), Acc)
                    .addReg(Acc);
            addMaybePostMemOperand(MIB, Base, Offset, false);
            MIB.addReg(Coeff);
            MIB.cloneMemRefs(Load);

            Load.eraseFromParent();
            Mul.eraseFromParent();
            AccLoad.eraseFromParent();
            AccAddI->eraseFromParent();
            I = MBB.begin();
            Changed = true;
            continue;
          }
        }
      }
    }

    if (AddI == MBB.end()) {
      ++I;
      continue;
    }
    MachineInstr &Add = *AddI;
    if (Add.getOpcode() != Bedrock::ADD8rr &&
        Add.getOpcode() != Bedrock::ADD16rr &&
        Add.getOpcode() != Bedrock::ADD32rr &&
        Add.getOpcode() != Bedrock::ADD64rr) {
      ++I;
      continue;
    }
    if (Add.getNumOperands() < 3 || !Add.getOperand(0).isReg() ||
        !Add.getOperand(1).isReg() || !Add.getOperand(2).isReg()) {
      ++I;
      continue;
    }

    if (!operandIsKill(Mul, Tmp, TRI) &&
        !regDeadAfter(std::next(MulI), MBB, Tmp, TRI)) {
      ++I;
      continue;
    }

    MachineInstr *Inc = nullptr;
    bool PostInc = LoadPostInc;
    if (!PostInc && Offset == 0) {
      unsigned Size = memSizeForOpcode(Load.getOpcode());
      Inc = findPostInc(AddI, MBB, Base, Size, TRI);
      PostInc = Inc != nullptr;
    }

    unsigned Opcode = getMAddMemOpcode(Mul.getOpcode(), PostInc);
    if (Opcode == 0) {
      ++I;
      continue;
    }

    Register AddDst = Add.getOperand(0).getReg();
    Register AddLHS = Add.getOperand(1).getReg();
    Register AddRHS = Add.getOperand(2).getReg();

    auto EraseOld = [&]() {
      Load.eraseFromParent();
      Mul.eraseFromParent();
      Add.eraseFromParent();
      if (Inc)
        Inc->eraseFromParent();
      I = MBB.begin();
      Changed = true;
    };

    if (regsOverlap(TRI, AddDst, AddLHS) && regsOverlap(TRI, Product, AddRHS) &&
        !regsOverlap(TRI, AddDst, Tmp) && !regsOverlap(TRI, AddDst, Coeff)) {
      Register Acc = AddDst;
      if (!operandIsKill(Add, Product, TRI) &&
          !regDeadAfter(std::next(AddI), MBB, Product, TRI)) {
        ++I;
        continue;
      }

      MachineInstrBuilder MIB =
          BuildMI(MBB, HasCoeffLoad ? Mul.getIterator() : Load.getIterator(),
                  Add.getDebugLoc(), TII.get(Opcode), Acc)
              .addReg(Acc);
      addMaybePostMemOperand(MIB, Base, Offset, PostInc);
      MIB.addReg(Coeff);
      EraseOld();
      continue;
    }

    if (!HasCoeffLoad && regsOverlap(TRI, AddDst, AddLHS) &&
        regsOverlap(TRI, AddDst, Product) &&
        !regsOverlap(TRI, AddRHS, Product) &&
        !regsOverlap(TRI, AddRHS, Coeff) && !regsOverlap(TRI, Product, Base)) {
      Register Acc = AddRHS;
      unsigned CopyOpcode =
          getRegMoveOpcodeForMaybePostLoad(Load.getOpcode(), Product, Acc);
      if (CopyOpcode == 0) {
        ++I;
        continue;
      }

      BuildMI(MBB, Load.getIterator(), Add.getDebugLoc(), TII.get(CopyOpcode),
              Product)
          .addReg(Acc);
      MachineInstrBuilder MIB =
          BuildMI(MBB, Load.getIterator(), Add.getDebugLoc(), TII.get(Opcode),
                  Product)
              .addReg(Product);
      addMaybePostMemOperand(MIB, Base, Offset, PostInc);
      MIB.addReg(Coeff);
      EraseOld();
      continue;
    }

    ++I;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldPostInc(MachineBasicBlock &MBB,
                                      MachineFunction &MF) const {
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

    Register Reg, Base;
    int64_t Offset = 0;
    bool IsLoad = isMemLoad(MI, Reg, Base, Offset);
    bool IsStore = false;
    if (!IsLoad)
      IsStore = isMemStore(MI, Reg, Base, Offset);
    if ((!IsLoad && !IsStore) || Offset != 0 || regsOverlap(TRI, Reg, Base)) {
      ++I;
      continue;
    }

    unsigned Size = memSizeForOpcode(MI.getOpcode());
    MachineInstr *Inc = findPostInc(I, MBB, Base, Size, TRI);
    if (!Inc) {
      ++I;
      continue;
    }

    unsigned Opcode = IsLoad ? getPostLoadOpcode(MI.getOpcode())
                             : getPostStoreOpcode(MI.getOpcode());
    if (Opcode == 0) {
      ++I;
      continue;
    }
    if (IsLoad && isAReg(Reg) && !postLoadSupportsAReg(MI.getOpcode())) {
      ++I;
      continue;
    }

    if (IsLoad)
      BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(), TII.get(Opcode), Reg)
          .addReg(Base);
    else
      BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(), TII.get(Opcode))
          .addReg(Reg)
          .addReg(Base);

    MI.eraseFromParent();
    Inc->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldCrossBlockPostInc(MachineBasicBlock &MBB,
                                                MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  MachineFunction::iterator NextMBBI = std::next(MBB.getIterator());
  if (NextMBBI == MF.end())
    return false;
  MachineBasicBlock &Fallthrough = *NextMBBI;
  if (!MBB.isSuccessor(&Fallthrough) || Fallthrough.pred_size() != 1)
    return false;

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Load = *I;
    if (Load.isDebugInstr()) {
      ++I;
      continue;
    }

    Register ValueReg;
    Register Base;
    int64_t Offset = 0;
    bool IsLoad = isMemLoad(Load, ValueReg, Base, Offset);
    bool IsStore = false;
    if (!IsLoad)
      IsStore = isMemStore(Load, ValueReg, Base, Offset);
    if ((!IsLoad && !IsStore) || Offset != 0 ||
        regsOverlap(TRI, ValueReg, Base)) {
      ++I;
      continue;
    }

    unsigned Size = memSizeForOpcode(Load.getOpcode());
    unsigned PostOpcode = IsLoad ? getPostLoadOpcode(Load.getOpcode())
                                 : getPostStoreOpcode(Load.getOpcode());
    if (Size == 0 || PostOpcode == 0) {
      ++I;
      continue;
    }
    if (IsLoad && isAReg(ValueReg) && !postLoadSupportsAReg(Load.getOpcode())) {
      ++I;
      continue;
    }

    MachineInstr *Branch = nullptr;
    MachineBasicBlock *Exit = nullptr;
    bool Failed = false;
    for (auto Scan = nextNonDebug(I, MBB); Scan != MBB.end();
         Scan = nextNonDebug(Scan, MBB)) {
      if (instrTouchesReg(*Scan, Base, TRI) ||
          (IsLoad && instrDefinesReg(*Scan, ValueReg, TRI))) {
        Failed = true;
        break;
      }
      if (Scan->getOpcode() == Bedrock::JCC && Scan->getNumOperands() >= 2 &&
          Scan->getOperand(0).isMBB()) {
        Branch = &*Scan;
        Exit = Scan->getOperand(0).getMBB();
        break;
      }
    }
    if (Failed || !Branch || !Exit || Exit == &Fallthrough ||
        !MBB.isSuccessor(Exit)) {
      ++I;
      continue;
    }

    SmallPtrSet<MachineBasicBlock *, 8> Visiting;
    if (!regDeadFromBlockStartInCFG(*Exit, Base, TRI, Visiting)) {
      ++I;
      continue;
    }

    MachineInstr *Add = nullptr;
    for (auto Scan = Fallthrough.begin(); Scan != Fallthrough.end(); ++Scan) {
      if (Scan->isDebugInstr())
        continue;
      if (isAddImmToReg(*Scan, Base, Size, TRI)) {
        Add = &*Scan;
        break;
      }
      if (instrTouchesReg(*Scan, Base, TRI))
        break;
    }
    if (!Add) {
      ++I;
      continue;
    }

    if (IsLoad) {
      BuildMI(MBB, Load.getIterator(), Load.getDebugLoc(), TII.get(PostOpcode),
              ValueReg)
          .addReg(Base);
    } else {
      BuildMI(MBB, Load.getIterator(), Load.getDebugLoc(), TII.get(PostOpcode))
          .addReg(ValueReg)
          .addReg(Base);
    }
    Load.eraseFromParent();
    Add->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldLea(MachineBasicBlock &MBB,
                                  MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Shl = *I;
    if (Shl.isDebugInstr()) {
      ++I;
      continue;
    }

    if (Shl.getOpcode() == Bedrock::LEAri && Shl.getNumOperands() >= 3 &&
        Shl.getOperand(0).isReg() && Shl.getOperand(1).isReg() &&
        Shl.getOperand(2).isImm()) {
      Register Addr = Shl.getOperand(0).getReg();
      Register Base = Shl.getOperand(1).getReg();
      int64_t Offset = Shl.getOperand(2).getImm();
      auto AddI = nextNonDebug(I, MBB);
      if (AddI != MBB.end() && AddI->getOpcode() == Bedrock::ADD64ri &&
          AddI->getNumOperands() >= 3 && AddI->getOperand(0).isReg() &&
          AddI->getOperand(1).isReg() && AddI->getOperand(2).isImm() &&
          regsOverlap(TRI, AddI->getOperand(0).getReg(), Addr) &&
          regsOverlap(TRI, AddI->getOperand(1).getReg(), Addr) &&
          (!instrDefinesReg(*AddI, Bedrock::FLAGS, TRI) ||
           regDefDeadOrDeadAfter(AddI, MBB, Bedrock::FLAGS, TRI))) {
        BuildMI(MBB, Shl.getIterator(), AddI->getDebugLoc(),
                TII.get(Bedrock::LEAri), Addr)
            .addReg(Base)
            .addImm(Offset + AddI->getOperand(2).getImm());
        Shl.eraseFromParent();
        AddI->eraseFromParent();
        I = MBB.begin();
        Changed = true;
        continue;
      }

      bool FoldedCopy = false;
      for (auto CopyI = nextNonDebug(I, MBB); CopyI != MBB.end();
           CopyI = nextNonDebug(CopyI, MBB)) {
        if (CopyI->isCall() || CopyI->isTerminator() || CopyI->isBranch())
          break;
        if (CopyI->getOpcode() != Bedrock::MOV64rr ||
            CopyI->getNumOperands() < 2 || !CopyI->getOperand(0).isReg() ||
            !CopyI->getOperand(1).isReg()) {
          if (instrTouchesReg(*CopyI, Addr, TRI))
            break;
          continue;
        }

        Register CopyDst = CopyI->getOperand(0).getReg();
        if (!isAReg(CopyDst) ||
            !regsOverlap(TRI, CopyI->getOperand(1).getReg(), Addr)) {
          if (instrTouchesReg(*CopyI, Addr, TRI))
            break;
          continue;
        }

        bool Safe = true;
        for (auto Scan = nextNonDebug(I, MBB); Scan != CopyI;
             Scan = nextNonDebug(Scan, MBB)) {
          if (Scan->isCall() || Scan->isTerminator() || Scan->isBranch() ||
              instrTouchesReg(*Scan, Addr, TRI) ||
              instrTouchesReg(*Scan, CopyDst, TRI)) {
            Safe = false;
            break;
          }
        }
        if (!Safe)
          break;

        if (!operandIsKill(*CopyI, Addr, TRI) &&
            !regDeadAfter(std::next(CopyI), MBB, Addr, TRI))
          break;

        BuildMI(MBB, Shl.getIterator(), Shl.getDebugLoc(),
                TII.get(Bedrock::LEAri), CopyDst)
            .addReg(Base)
            .addImm(Offset);
        Shl.eraseFromParent();
        CopyI->eraseFromParent();
        I = MBB.begin();
        Changed = true;
        FoldedCopy = true;
        break;
      }
      if (FoldedCopy)
        continue;
    }

    if (Shl.getOpcode() == Bedrock::MOV64rr && Shl.getNumOperands() >= 2 &&
        Shl.getOperand(0).isReg() && Shl.getOperand(1).isReg() &&
        isAReg(Shl.getOperand(0).getReg())) {
      Register Addr = Shl.getOperand(0).getReg();
      Register Base = Shl.getOperand(1).getReg();
      auto AddI = nextNonDebug(I, MBB);
      if (AddI != MBB.end() && AddI->getOpcode() == Bedrock::ADD64ri &&
          AddI->getNumOperands() >= 3 && AddI->getOperand(0).isReg() &&
          AddI->getOperand(1).isReg() && AddI->getOperand(2).isImm() &&
          (isAReg(Base) || Base == Bedrock::SP) &&
          regsOverlap(TRI, AddI->getOperand(0).getReg(), Addr) &&
          regsOverlap(TRI, AddI->getOperand(1).getReg(), Addr) &&
          (!instrDefinesReg(*AddI, Bedrock::FLAGS, TRI) ||
           regDefDeadOrDeadAfter(AddI, MBB, Bedrock::FLAGS, TRI))) {
        BuildMI(MBB, Shl.getIterator(), AddI->getDebugLoc(),
                TII.get(Bedrock::LEAri), Addr)
            .addReg(Base)
            .addImm(AddI->getOperand(2).getImm());
        Shl.eraseFromParent();
        AddI->eraseFromParent();
        I = MBB.begin();
        Changed = true;
        continue;
      }
    }

    if ((Shl.getOpcode() == Bedrock::EXTSQ32rr ||
         Shl.getOpcode() == Bedrock::EXTZQ32rr) &&
        Shl.getNumOperands() >= 2 && Shl.getOperand(0).isReg() &&
        Shl.getOperand(1).isReg() && isAReg(Shl.getOperand(0).getReg()) &&
        isDReg(Shl.getOperand(1).getReg())) {
      Register Scaled = Shl.getOperand(0).getReg();
      Register Index32 = Shl.getOperand(1).getReg();
      auto ShlI = nextNonDebug(I, MBB);
      if (ShlI != MBB.end() && ShlI->getOpcode() == Bedrock::SHL64ri &&
          ShlI->getNumOperands() >= 3 && ShlI->getOperand(0).isReg() &&
          ShlI->getOperand(1).isReg() && ShlI->getOperand(2).isImm() &&
          ShlI->getOperand(2).getImm() == 2 &&
          regsOverlap(TRI, ShlI->getOperand(0).getReg(), Scaled) &&
          regsOverlap(TRI, ShlI->getOperand(1).getReg(), Scaled) &&
          regDefDeadOrDeadAfter(ShlI, MBB, Bedrock::FLAGS, TRI)) {
        auto AddI = nextNonDebug(ShlI, MBB);
        if (AddI != MBB.end() && AddI->getOpcode() == Bedrock::ADD64rr &&
            AddI->getNumOperands() >= 3 && AddI->getOperand(0).isReg() &&
            AddI->getOperand(1).isReg() && AddI->getOperand(2).isReg()) {
          Register Dst = AddI->getOperand(0).getReg();
          Register Base = Register();
          if (regsOverlap(TRI, AddI->getOperand(1).getReg(), Scaled))
            Base = AddI->getOperand(2).getReg();
          else if (regsOverlap(TRI, AddI->getOperand(2).getReg(), Scaled))
            Base = AddI->getOperand(1).getReg();

          if (isAReg(Base) && isAReg(Dst) &&
              (regsOverlap(TRI, Dst, Scaled) ||
               operandIsKill(*AddI, Scaled, TRI) ||
               regDeadAfter(std::next(AddI), MBB, Scaled, TRI))) {
            BuildMI(MBB, Shl.getIterator(), AddI->getDebugLoc(),
                    TII.get(Bedrock::LEA4L), Dst)
                .addReg(Base)
                .addReg(Index32,
                        operandIsKill(Shl, Index32, TRI) ? RegState::Kill : 0);
            Shl.eraseFromParent();
            ShlI->eraseFromParent();
            AddI->eraseFromParent();
            I = MBB.begin();
            Changed = true;
            continue;
          }
        }
      }
    }

    if (Shl.getOpcode() == Bedrock::MOV64rr && Shl.getNumOperands() >= 2 &&
        Shl.getOperand(0).isReg() && Shl.getOperand(1).isReg() &&
        isAReg(Shl.getOperand(0).getReg()) &&
        isDReg(Shl.getOperand(1).getReg())) {
      Register Scaled = Shl.getOperand(0).getReg();
      Register Index32 = Shl.getOperand(1).getReg();
      auto ShlI = nextNonDebug(I, MBB);
      if (ShlI != MBB.end() && ShlI->getOpcode() == Bedrock::SHL64ri &&
          ShlI->getNumOperands() >= 3 && ShlI->getOperand(0).isReg() &&
          ShlI->getOperand(1).isReg() && ShlI->getOperand(2).isImm() &&
          ShlI->getOperand(2).getImm() == 2 &&
          regsOverlap(TRI, ShlI->getOperand(0).getReg(), Scaled) &&
          regsOverlap(TRI, ShlI->getOperand(1).getReg(), Scaled) &&
          regDefDeadOrDeadAfter(ShlI, MBB, Bedrock::FLAGS, TRI)) {
        auto AddI = nextNonDebug(ShlI, MBB);
        if (AddI != MBB.end() && AddI->getOpcode() == Bedrock::ADD64rr &&
            AddI->getNumOperands() >= 3 && AddI->getOperand(0).isReg() &&
            AddI->getOperand(1).isReg() && AddI->getOperand(2).isReg()) {
          Register Dst = AddI->getOperand(0).getReg();
          Register Base = Register();
          if (regsOverlap(TRI, AddI->getOperand(1).getReg(), Scaled))
            Base = AddI->getOperand(2).getReg();
          else if (regsOverlap(TRI, AddI->getOperand(2).getReg(), Scaled))
            Base = AddI->getOperand(1).getReg();

          if (isAReg(Base) && isAReg(Dst) &&
              (operandIsKill(*AddI, Scaled, TRI) ||
               regDeadAfter(std::next(AddI), MBB, Scaled, TRI))) {
            BuildMI(MBB, Shl.getIterator(), AddI->getDebugLoc(),
                    TII.get(Bedrock::LEA4L), Dst)
                .addReg(Base)
                .addReg(Index32);
            Shl.eraseFromParent();
            ShlI->eraseFromParent();
            AddI->eraseFromParent();
            I = MBB.begin();
            Changed = true;
            continue;
          }
        }
      }
    }

    if (Shl.getOpcode() == Bedrock::MOV64rr && Shl.getNumOperands() >= 2 &&
        Shl.getOperand(0).isReg() && Shl.getOperand(1).isReg() &&
        isAReg(Shl.getOperand(0).getReg()) &&
        isDReg(Shl.getOperand(1).getReg())) {
      Register Bias = Shl.getOperand(0).getReg();
      Register Numer = Shl.getOperand(1).getReg();
      auto SarSignI = nextNonDebug(I, MBB);
      auto ShrBiasI =
          SarSignI == MBB.end() ? MBB.end() : nextNonDebug(SarSignI, MBB);
      auto AddBiasI =
          ShrBiasI == MBB.end() ? MBB.end() : nextNonDebug(ShrBiasI, MBB);
      auto AndI = MBB.end();
      if (AddBiasI != MBB.end()) {
        for (auto Scan = nextNonDebug(AddBiasI, MBB); Scan != MBB.end();
             Scan = nextNonDebug(Scan, MBB)) {
          if (Scan->getOpcode() == Bedrock::AND64ri) {
            AndI = Scan;
            break;
          }
          if (instrTouchesReg(*Scan, Bias, TRI) ||
              instrTouchesReg(*Scan, Numer, TRI))
            break;
        }
      }
      auto AddBaseI = MBB.end();
      if (AndI != MBB.end()) {
        for (auto Scan = nextNonDebug(AndI, MBB); Scan != MBB.end();
             Scan = nextNonDebug(Scan, MBB)) {
          if (Scan->getOpcode() == Bedrock::ADD64rr &&
              instrUsesReg(*Scan, Bias, TRI)) {
            AddBaseI = Scan;
            break;
          }
          if (instrTouchesReg(*Scan, Bias, TRI))
            break;
        }
      }

      if (SarSignI != MBB.end() && ShrBiasI != MBB.end() &&
          AddBiasI != MBB.end() && AndI != MBB.end() && AddBaseI != MBB.end() &&
          SarSignI->getOpcode() == Bedrock::SAR64ri &&
          ShrBiasI->getOpcode() == Bedrock::SHR64ri &&
          AddBiasI->getOpcode() == Bedrock::ADD64rr &&
          AndI->getOpcode() == Bedrock::AND64ri &&
          AddBaseI->getOpcode() == Bedrock::ADD64rr &&
          SarSignI->getNumOperands() >= 3 && ShrBiasI->getNumOperands() >= 3 &&
          AddBiasI->getNumOperands() >= 3 && AndI->getNumOperands() >= 3 &&
          AddBaseI->getNumOperands() >= 3 && SarSignI->getOperand(0).isReg() &&
          SarSignI->getOperand(1).isReg() && SarSignI->getOperand(2).isImm() &&
          ShrBiasI->getOperand(0).isReg() && ShrBiasI->getOperand(1).isReg() &&
          ShrBiasI->getOperand(2).isImm() && AddBiasI->getOperand(0).isReg() &&
          AddBiasI->getOperand(1).isReg() && AddBiasI->getOperand(2).isReg() &&
          AndI->getOperand(0).isReg() && AndI->getOperand(1).isReg() &&
          AndI->getOperand(2).isImm() && AddBaseI->getOperand(0).isReg() &&
          AddBaseI->getOperand(1).isReg() && AddBaseI->getOperand(2).isReg() &&
          SarSignI->getOperand(2).getImm() == 63 &&
          ShrBiasI->getOperand(2).getImm() == 62 &&
          AndI->getOperand(2).getImm() == -4 &&
          regsOverlap(TRI, SarSignI->getOperand(0).getReg(), Bias) &&
          regsOverlap(TRI, SarSignI->getOperand(1).getReg(), Bias) &&
          regsOverlap(TRI, ShrBiasI->getOperand(0).getReg(), Bias) &&
          regsOverlap(TRI, ShrBiasI->getOperand(1).getReg(), Bias) &&
          regsOverlap(TRI, AddBiasI->getOperand(0).getReg(), Bias) &&
          regsOverlap(TRI, AddBiasI->getOperand(1).getReg(), Bias) &&
          regsOverlap(TRI, AddBiasI->getOperand(2).getReg(), Numer) &&
          regsOverlap(TRI, AndI->getOperand(0).getReg(), Bias) &&
          regsOverlap(TRI, AndI->getOperand(1).getReg(), Bias) &&
          regsOverlap(TRI, AddBaseI->getOperand(1).getReg(), Bias)) {
        Register Addr = AddBaseI->getOperand(0).getReg();
        Register Base = AddBaseI->getOperand(2).getReg();
        bool InterveningClobbersBase = false;
        for (auto Scan = nextNonDebug(AddBiasI, MBB); Scan != AddBaseI;
             Scan = nextNonDebug(Scan, MBB)) {
          if (Scan == AndI)
            continue;
          if (instrDefinesReg(*Scan, Base, TRI)) {
            InterveningClobbersBase = true;
            break;
          }
        }
        if (isAReg(Addr) && isAReg(Base) && !regsOverlap(TRI, Base, Bias) &&
            !regsOverlap(TRI, Base, Numer) && !InterveningClobbersBase &&
            (operandIsKill(*AddBiasI, Numer, TRI) ||
             regDeadAfter(std::next(AddBiasI), MBB, Numer, TRI)) &&
            (regsOverlap(TRI, Addr, Bias) ||
             operandIsKill(*AddBaseI, Bias, TRI) ||
             regDeadAfter(std::next(AddBaseI), MBB, Bias, TRI))) {
          if (MF.getFunction().hasMinSize()) {
            BuildMI(MBB, Shl.getIterator(), AddBiasI->getDebugLoc(),
                    TII.get(Bedrock::DIVS64ri), Numer)
                .addReg(Numer)
                .addImm(4);
          } else {
            auto FindLocalDRegScratch = [&]() {
              static constexpr MCPhysReg ScratchRegs[] = {
                  Bedrock::D3, Bedrock::D4, Bedrock::D5};
              for (MCPhysReg PhysReg : ScratchRegs) {
                Register Reg(PhysReg);
                if (MBB.isLiveIn(Reg))
                  continue;
                bool Touched = false;
                for (auto Scan = I; Scan != AddBiasI;
                     Scan = nextNonDebug(Scan, MBB)) {
                  if (!Scan->isDebugInstr() &&
                      instrTouchesReg(*Scan, Reg, TRI)) {
                    Touched = true;
                    break;
                  }
                }
                if (!Touched &&
                    regDeadAfter(std::next(AddBiasI), MBB, Reg, TRI))
                  return Reg;
              }
              return Register();
            };
            Register BiasScratch = FindLocalDRegScratch();
            Register BiasWork = BiasScratch.isValid() ? BiasScratch : Bias;
            BuildMI(MBB, Shl.getIterator(), Shl.getDebugLoc(),
                    TII.get(Bedrock::MOV64rr), BiasWork)
                .addReg(Numer);
            BuildMI(MBB, Shl.getIterator(), SarSignI->getDebugLoc(),
                    TII.get(Bedrock::SAR64ri), BiasWork)
                .addReg(BiasWork)
                .addImm(63);
            BuildMI(MBB, Shl.getIterator(), ShrBiasI->getDebugLoc(),
                    TII.get(Bedrock::SHR64ri), BiasWork)
                .addReg(BiasWork)
                .addImm(62);
            BuildMI(MBB, Shl.getIterator(), AddBiasI->getDebugLoc(),
                    TII.get(Bedrock::ADD64rr), Numer)
                .addReg(Numer)
                .addReg(BiasWork);
            BuildMI(MBB, Shl.getIterator(), AndI->getDebugLoc(),
                    TII.get(Bedrock::SAR64ri), Numer)
                .addReg(Numer)
                .addImm(2);
          }
          BuildMI(MBB, Shl.getIterator(), AddBaseI->getDebugLoc(),
                  TII.get(Bedrock::LEA4), Addr)
              .addReg(Base)
              .addReg(Numer);

          Shl.eraseFromParent();
          SarSignI->eraseFromParent();
          ShrBiasI->eraseFromParent();
          AddBiasI->eraseFromParent();
          AndI->eraseFromParent();
          AddBaseI->eraseFromParent();
          I = MBB.begin();
          Changed = true;
          continue;
        }
      }
    }

    if (Shl.getOpcode() != Bedrock::SHL64ri || Shl.getNumOperands() < 3 ||
        !Shl.getOperand(0).isReg() || !Shl.getOperand(1).isReg() ||
        !Shl.getOperand(2).isImm() || Shl.getOperand(2).getImm() != 2 ||
        !regsOverlap(TRI, Shl.getOperand(0).getReg(),
                     Shl.getOperand(1).getReg()) ||
        !isDReg(Shl.getOperand(0).getReg())) {
      ++I;
      continue;
    }
    Register Index = Shl.getOperand(0).getReg();

    auto AddI = nextNonDebug(I, MBB);
    if (AddI == MBB.end() || AddI->getOpcode() != Bedrock::ADD64rr ||
        AddI->getNumOperands() < 3 || !AddI->getOperand(0).isReg() ||
        !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg()) {
      ++I;
      continue;
    }
    Register Dst = AddI->getOperand(0).getReg();
    Register Base = Register();
    if (regsOverlap(TRI, AddI->getOperand(1).getReg(), Index))
      Base = AddI->getOperand(2).getReg();
    else if (regsOverlap(TRI, AddI->getOperand(2).getReg(), Index))
      Base = AddI->getOperand(1).getReg();
    else {
      ++I;
      continue;
    }
    if (!isAReg(Base) || !isAReg(Dst) ||
        !regDefDeadOrDeadAfter(I, MBB, Bedrock::FLAGS, TRI) ||
        (!operandIsKill(*AddI, Index, TRI) &&
         !regDeadAfter(std::next(AddI), MBB, Index, TRI))) {
      ++I;
      continue;
    }

    BuildMI(MBB, Shl.getIterator(), AddI->getDebugLoc(), TII.get(Bedrock::LEA4),
            Dst)
        .addReg(Base)
        .addReg(Index);
    Shl.eraseFromParent();
    AddI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldShortLeaAliasCopies(MachineBasicBlock &MBB,
                                                  MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  constexpr unsigned LeaRiSize = 6;
  auto Mov64Size = [](Register Dst, Register Src) {
    return isAReg(Dst) && isAReg(Src) ? 4u : 2u;
  };

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Lea = *I;
    if (Lea.isDebugInstr() || Lea.getOpcode() != Bedrock::LEAri ||
        Lea.getNumOperands() < 3 || !Lea.getOperand(0).isReg() ||
        !Lea.getOperand(1).isReg() || !Lea.getOperand(2).isImm()) {
      ++I;
      continue;
    }

    Register Alias = Lea.getOperand(0).getReg();
    Register Base = Lea.getOperand(1).getReg();
    int64_t Offset = Lea.getOperand(2).getImm();
    if (!isAReg(Alias) || Alias == Bedrock::SP ||
        regsOverlap(TRI, Alias, Base)) {
      ++I;
      continue;
    }

    SmallVector<MachineInstr *, 4> Copies;
    unsigned OldSize = LeaRiSize;
    unsigned NewSize = 0;
    bool Failed = false;

    auto Scan = nextNonDebug(I, MBB);
    for (; Scan != MBB.end(); Scan = nextNonDebug(Scan, MBB)) {
      if (Scan->getOpcode() == Bedrock::MOV64rr &&
          Scan->getNumOperands() >= 2 && Scan->getOperand(0).isReg() &&
          Scan->getOperand(1).isReg() &&
          regsOverlap(TRI, Scan->getOperand(1).getReg(), Alias)) {
        Register Dst = Scan->getOperand(0).getReg();
        if (!isAReg(Dst) || Dst == Bedrock::SP ||
            regsOverlap(TRI, Dst, Alias) || regsOverlap(TRI, Dst, Base)) {
          Failed = true;
          break;
        }
        Copies.push_back(&*Scan);
        OldSize += Mov64Size(Dst, Alias);
        NewSize += LeaRiSize;
        continue;
      }

      if (instrUsesReg(*Scan, Alias, TRI)) {
        Failed = true;
        break;
      }

      if (instrDefinesReg(*Scan, Alias, TRI) ||
          instrHasRegMaskForReg(*Scan, Alias, TRI) ||
          instrDefinesReg(*Scan, Base, TRI) ||
          instrHasRegMaskForReg(*Scan, Base, TRI))
        break;
    }

    if (Failed || Copies.empty() || NewSize >= OldSize ||
        !regUnusedAfterInCFG(Scan, MBB, Alias, TRI)) {
      ++I;
      continue;
    }

    for (MachineInstr *Copy : Copies) {
      Register Dst = Copy->getOperand(0).getReg();
      BuildMI(MBB, Copy->getIterator(), Copy->getDebugLoc(),
              TII.get(Bedrock::LEAri), Dst)
          .addReg(Base)
          .addImm(Offset);
      Copy->eraseFromParent();
    }

    Lea.eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldARegAliasCopies(MachineBasicBlock &MBB,
                                              MachineFunction &MF) const {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Copy = *I;
    if (Copy.isDebugInstr() || Copy.getOpcode() != Bedrock::MOV64rr ||
        Copy.getNumOperands() < 2 || !Copy.getOperand(0).isReg() ||
        !Copy.getOperand(1).isReg()) {
      ++I;
      continue;
    }

    Register Alias = Copy.getOperand(0).getReg();
    Register Src = Copy.getOperand(1).getReg();
    bool SameAliasClass =
        (isAReg(Alias) && isAReg(Src)) || (isDReg(Alias) && isDReg(Src));
    if (!SameAliasClass || regsOverlap(TRI, Alias, Src) ||
        Alias == Bedrock::SP || Src == Bedrock::SP) {
      ++I;
      continue;
    }

    SmallVector<MachineOperand *, 8> Uses;
    bool Failed = false;
    bool StoppedAtAliasDef = false;
    auto Scan = nextNonDebug(I, MBB);
    for (; Scan != MBB.end(); Scan = nextNonDebug(Scan, MBB)) {
      if (instrHasRegMaskForReg(*Scan, Src, TRI) ||
          instrDefinesReg(*Scan, Src, TRI)) {
        Failed = true;
        break;
      }

      if (instrDefinesReg(*Scan, Alias, TRI)) {
        StoppedAtAliasDef = true;
        break;
      }

      for (MachineOperand &MO : Scan->operands()) {
        if (!operandTouchesReg(MO, Alias, TRI))
          continue;
        if (!MO.readsReg())
          continue;
        if (MO.isImplicit() || MO.getSubReg() != 0) {
          Failed = true;
          break;
        }
        Uses.push_back(&MO);
      }
      if (Failed)
        break;
    }

    if (Failed || Uses.empty()) {
      ++I;
      continue;
    }
    if (!StoppedAtAliasDef && !regUnusedAfterInCFG(Scan, MBB, Alias, TRI)) {
      ++I;
      continue;
    }

    for (MachineOperand *MO : Uses) {
      MO->setReg(Src);
      MO->setIsKill(false);
    }
    Copy.eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldAliasBackCopies(MachineBasicBlock &MBB,
                                              MachineFunction &MF) const {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Copy = *I;
    if (Copy.isDebugInstr() || Copy.getOpcode() != Bedrock::MOV64rr ||
        Copy.getNumOperands() < 2 || !Copy.getOperand(0).isReg() ||
        !Copy.getOperand(1).isReg()) {
      ++I;
      continue;
    }

    Register Alias = Copy.getOperand(0).getReg();
    Register Src = Copy.getOperand(1).getReg();
    bool SameAliasClass =
        (isAReg(Alias) && isAReg(Src)) || (isDReg(Alias) && isDReg(Src));
    if (!SameAliasClass || regsOverlap(TRI, Alias, Src) ||
        Alias == Bedrock::SP || Src == Bedrock::SP) {
      ++I;
      continue;
    }

    SmallVector<MachineInstr *, 4> BackCopies;
    SmallVector<MachineBasicBlock *, 4> LiveInBlocks;
    SmallPtrSet<MachineBasicBlock *, 4> Visited;
    MachineBasicBlock *ScanMBB = &MBB;
    auto Scan = nextNonDebug(I, MBB);
    Visited.insert(&MBB);
    while (true) {
      for (; Scan != ScanMBB->end(); Scan = nextNonDebug(Scan, *ScanMBB)) {
        if (Scan->getOpcode() == Bedrock::MOV64rr &&
            Scan->getNumOperands() >= 2 && Scan->getOperand(0).isReg() &&
            Scan->getOperand(1).isReg() &&
            regsOverlap(TRI, Scan->getOperand(0).getReg(), Src) &&
            regsOverlap(TRI, Scan->getOperand(1).getReg(), Alias)) {
          BackCopies.push_back(&*Scan);
          continue;
        }

        if (instrDefinesReg(*Scan, Alias, TRI) ||
            instrHasRegMaskForReg(*Scan, Alias, TRI) ||
            instrDefinesReg(*Scan, Src, TRI) ||
            instrHasRegMaskForReg(*Scan, Src, TRI))
          break;
      }

      if (Scan != ScanMBB->end())
        break;
      if (ScanMBB->succ_size() != 1)
        break;

      MachineBasicBlock *Succ = *ScanMBB->succ_begin();
      if (Succ->pred_size() != 1 || !Visited.insert(Succ).second)
        break;

      LiveInBlocks.push_back(Succ);
      ScanMBB = Succ;
      Scan = ScanMBB->begin();
      while (Scan != ScanMBB->end() && Scan->isDebugInstr())
        ++Scan;
    }

    if (BackCopies.empty()) {
      ++I;
      continue;
    }

    for (MachineInstr *BackCopy : BackCopies)
      BackCopy->eraseFromParent();
    for (MachineBasicBlock *LiveInBlock : LiveInBlocks)
      LiveInBlock->addLiveIn(Src);
    MF.getRegInfo().clearKillFlags(Src);
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldDRegCopyCoalescing(MachineBasicBlock &MBB,
                                                 MachineFunction &MF) const {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  auto CanRewriteOperand = [&](const MachineOperand &MO, Register Reg) {
    return operandTouchesReg(MO, Reg, TRI) && !MO.isImplicit() &&
           MO.getSubReg() == 0;
  };

  auto ReachesRetLiveOut = [&](MachineBasicBlock::iterator From, Register Reg) {
    return (Reg == Bedrock::D0 || Reg == Bedrock::D1) &&
           regReachesRetBeforeTouch(From, MBB, Reg, TRI);
  };

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Copy = *I;
    if (Copy.isDebugInstr() || Copy.getOpcode() != Bedrock::MOV64rr ||
        Copy.getNumOperands() < 2 || !Copy.getOperand(0).isReg() ||
        !Copy.getOperand(1).isReg()) {
      ++I;
      continue;
    }

    Register Alias = Copy.getOperand(0).getReg();
    Register Src = Copy.getOperand(1).getReg();
    if (!isDReg(Alias) || !isDReg(Src) || regsOverlap(TRI, Alias, Src)) {
      ++I;
      continue;
    }

    SmallVector<MachineOperand *, 8> Rewrites;
    SmallVector<MachineOperand *, 8> SrcFlagsToClear;
    bool Failed = false;
    bool ClobbersSrcValue = false;
    bool RangeEnded = false;

    auto Scan = nextNonDebug(I, MBB);
    for (; Scan != MBB.end(); Scan = nextNonDebug(Scan, MBB)) {
      if (Scan->isCall() || Scan->isBranch() || Scan->isReturn() ||
          Scan->isTerminator() || instrHasRegMaskForReg(*Scan, Alias, TRI) ||
          instrHasRegMaskForReg(*Scan, Src, TRI)) {
        Failed = true;
        break;
      }

      bool TouchesAlias = false;
      bool TouchesSrc = false;
      bool DefinesAlias = false;
      bool DefinesSrc = false;
      for (MachineOperand &MO : Scan->operands()) {
        if (operandTouchesReg(MO, Alias, TRI)) {
          TouchesAlias = true;
          if (!CanRewriteOperand(MO, Alias)) {
            Failed = true;
            break;
          }
          if (MO.isDef())
            DefinesAlias = true;
          Rewrites.push_back(&MO);
        }
        if (operandTouchesReg(MO, Src, TRI)) {
          TouchesSrc = true;
          if (!CanRewriteOperand(MO, Src)) {
            Failed = true;
            break;
          }
          if (MO.isDef())
            DefinesSrc = true;
          SrcFlagsToClear.push_back(&MO);
        }
      }
      if (Failed)
        break;

      if (!TouchesAlias) {
        if (TouchesSrc) {
          Failed = true;
          break;
        }
        continue;
      }

      if (DefinesAlias || DefinesSrc)
        ClobbersSrcValue = true;

      MachineBasicBlock::iterator Next = nextNonDebug(Scan, MBB);
      if (ClobbersSrcValue && regUnusedAfterInCFG(Next, MBB, Alias, TRI) &&
          regUnusedAfterInCFG(Next, MBB, Src, TRI) &&
          !ReachesRetLiveOut(Next, Src)) {
        RangeEnded = true;
        break;
      }
    }

    if (Failed || !RangeEnded || Rewrites.empty()) {
      ++I;
      continue;
    }

    for (MachineOperand *MO : Rewrites) {
      MO->setReg(Src);
      if (MO->readsReg())
        MO->setIsKill(false);
      if (MO->isDef())
        MO->setIsDead(false);
    }
    for (MachineOperand *MO : SrcFlagsToClear) {
      if (MO->readsReg())
        MO->setIsKill(false);
      if (MO->isDef())
        MO->setIsDead(false);
    }
    Copy.eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldIndexedMem(MachineBasicBlock &MBB,
                                         MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  auto FindDeadDReg = [&](MachineBasicBlock::iterator From,
                          ArrayRef<Register> Avoid) {
    auto DeadInBlockAndSuccessors = [&](Register Reg) {
      for (auto Scan = From; Scan != MBB.end(); ++Scan) {
        if (Scan->isDebugInstr())
          continue;
        if (instrUsesReg(*Scan, Reg, TRI))
          return false;
        if (instrDefinesReg(*Scan, Reg, TRI))
          return true;
      }
      for (MachineBasicBlock *Succ : MBB.successors())
        if (blockHasLiveInReg(*Succ, Reg, TRI))
          return false;
      return true;
    };

    for (Register Reg = Bedrock::D0; Reg <= Bedrock::D7;
         Reg = Register(Reg + 1)) {
      bool IsAvoided = false;
      for (Register AvoidReg : Avoid)
        if (regsOverlap(TRI, Reg, AvoidReg))
          IsAvoided = true;
      if (!IsAvoided && DeadInBlockAndSuccessors(Reg))
        return Reg;
    }
    return Register();
  };

  auto TryFoldARegIndex = [&](MachineBasicBlock::iterator StartI) {
    MachineBasicBlock::iterator CopyI = MBB.end();
    MachineBasicBlock::iterator ExtI = StartI;
    Register Scaled;
    Register IndexA;

    if (StartI->getOpcode() == Bedrock::MOV64rr &&
        StartI->getNumOperands() >= 2 && StartI->getOperand(0).isReg() &&
        StartI->getOperand(1).isReg() &&
        isAReg(StartI->getOperand(0).getReg()) &&
        isAReg(StartI->getOperand(1).getReg())) {
      CopyI = StartI;
      Scaled = StartI->getOperand(0).getReg();
      IndexA = StartI->getOperand(1).getReg();
      ExtI = nextNonDebug(StartI, MBB);
    }

    if (ExtI == MBB.end() ||
        (ExtI->getOpcode() != Bedrock::EXTSQ32rr &&
         ExtI->getOpcode() != Bedrock::EXTZQ32rr) ||
        ExtI->getNumOperands() < 2 || !ExtI->getOperand(0).isReg() ||
        !ExtI->getOperand(1).isReg() || !isAReg(ExtI->getOperand(0).getReg()) ||
        !isAReg(ExtI->getOperand(1).getReg()))
      return false;

    if (CopyI == MBB.end()) {
      Scaled = ExtI->getOperand(0).getReg();
      IndexA = ExtI->getOperand(1).getReg();
    } else if (!regsOverlap(TRI, ExtI->getOperand(0).getReg(), Scaled) ||
               !regsOverlap(TRI, ExtI->getOperand(1).getReg(), Scaled)) {
      return false;
    }

    MachineBasicBlock::iterator ShlI = nextNonDebug(ExtI, MBB);
    if (ShlI == MBB.end() || ShlI->getOpcode() != Bedrock::SHL64ri ||
        ShlI->getNumOperands() < 3 || !ShlI->getOperand(0).isReg() ||
        !ShlI->getOperand(1).isReg() || !ShlI->getOperand(2).isImm() ||
        ShlI->getOperand(2).getImm() != 2 ||
        !regsOverlap(TRI, ShlI->getOperand(0).getReg(), Scaled) ||
        !regsOverlap(TRI, ShlI->getOperand(1).getReg(), Scaled) ||
        !regDefDeadOrDeadAfter(ShlI, MBB, Bedrock::FLAGS, TRI))
      return false;

    MachineBasicBlock::iterator BaseI = MBB.end();
    MachineBasicBlock::iterator AddI = nextNonDebug(ShlI, MBB);
    Register Addr;
    Register Base;
    int64_t BaseOffset = 0;
    if (AddI != MBB.end() && AddI->getOpcode() == Bedrock::MOV64rr &&
        AddI->getNumOperands() >= 2 && AddI->getOperand(0).isReg() &&
        AddI->getOperand(1).isReg() && isAReg(AddI->getOperand(0).getReg()) &&
        isAReg(AddI->getOperand(1).getReg()) &&
        !instrTouchesReg(*AddI, Scaled, TRI) &&
        !instrTouchesReg(*AddI, IndexA, TRI)) {
      BaseI = AddI;
      Addr = AddI->getOperand(0).getReg();
      Base = AddI->getOperand(1).getReg();
      AddI = nextNonDebug(BaseI, MBB);
    } else if (AddI != MBB.end() && AddI->getOpcode() == Bedrock::LEAri &&
               AddI->getNumOperands() >= 3 && AddI->getOperand(0).isReg() &&
               AddI->getOperand(1).isReg() && AddI->getOperand(2).isImm() &&
               isAReg(AddI->getOperand(0).getReg()) &&
               (isAReg(AddI->getOperand(1).getReg()) ||
                AddI->getOperand(1).getReg() == Bedrock::SP) &&
               !instrTouchesReg(*AddI, Scaled, TRI) &&
               !instrTouchesReg(*AddI, IndexA, TRI)) {
      BaseI = AddI;
      Addr = AddI->getOperand(0).getReg();
      Base = AddI->getOperand(1).getReg();
      BaseOffset = AddI->getOperand(2).getImm();
      AddI = nextNonDebug(BaseI, MBB);
    }

    if (AddI == MBB.end() || AddI->getOpcode() != Bedrock::ADD64rr ||
        AddI->getNumOperands() < 3 || !AddI->getOperand(0).isReg() ||
        !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg() ||
        (instrDefinesReg(*AddI, Bedrock::FLAGS, TRI) &&
         !regDefDeadOrDeadAfter(AddI, MBB, Bedrock::FLAGS, TRI)))
      return false;

    if (BaseI == MBB.end()) {
      Addr = AddI->getOperand(0).getReg();
      if (regsOverlap(TRI, AddI->getOperand(1).getReg(), Scaled))
        Base = AddI->getOperand(2).getReg();
      else if (regsOverlap(TRI, AddI->getOperand(2).getReg(), Scaled))
        Base = AddI->getOperand(1).getReg();
      else
        return false;
    } else if (!regsOverlap(TRI, AddI->getOperand(0).getReg(), Addr) ||
               !regsOverlap(TRI, AddI->getOperand(1).getReg(), Addr)) {
      return false;
    } else if (!regsOverlap(TRI, AddI->getOperand(2).getReg(), Scaled)) {
      return false;
    }

    if (!isAReg(Addr) || !(isAReg(Base) || Base == Bedrock::SP) ||
        Addr == Bedrock::SP || regsOverlap(TRI, IndexA, Base))
      return false;

    MachineBasicBlock::iterator MemI = nextNonDebug(AddI, MBB);
    if (MemI == MBB.end())
      return false;

    MachineBasicBlock::iterator AfterMem = nextNonDebug(MemI, MBB);
    if (!regUnusedAfterInCFG(AfterMem, MBB, Addr, TRI))
      return false;
    if (!regsOverlap(TRI, Addr, Scaled) &&
        !regUnusedBeforeEndOrDef(AfterMem, MBB, Scaled, TRI))
      return false;

    Register Scratch;
    unsigned NewMemOpcode = 0;
    Register MemReg;
    int64_t MemOffset = 0;
    bool IsLoad = false;
    if (MemI->getOpcode() == Bedrock::MOV32rm && MemI->getNumOperands() >= 3 &&
        MemI->getOperand(0).isReg() && MemI->getOperand(1).isReg() &&
        MemI->getOperand(2).isImm() &&
        regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
      MemReg = MemI->getOperand(0).getReg();
      if (!isDReg(MemReg))
        return false;
      Scratch = MemReg;
      NewMemOpcode = Bedrock::MOV32idx4lrm;
      MemOffset = MemI->getOperand(2).getImm();
      IsLoad = true;
    } else if (MemI->getOpcode() == Bedrock::MOV32mr &&
               MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
               MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
               regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
      MemReg = MemI->getOperand(0).getReg();
      Scratch = FindDeadDReg(StartI, {MemReg});
      if (!Scratch)
        return false;
      NewMemOpcode = Bedrock::MOV32idx4lmr;
      MemOffset = MemI->getOperand(2).getImm();
    } else {
      return false;
    }

    BuildMI(MBB, StartI, StartI->getDebugLoc(), TII.get(Bedrock::MOV64rr),
            Scratch)
        .addReg(IndexA,
                getKillRegState(CopyI == MBB.end()
                                    ? operandIsKill(*ExtI, IndexA, TRI)
                                    : operandIsKill(*CopyI, IndexA, TRI)));
    MachineInstrBuilder NewMem =
        BuildMI(MBB, StartI, MemI->getDebugLoc(), TII.get(NewMemOpcode));
    if (IsLoad)
      NewMem.addReg(MemReg, RegState::Define);
    else
      NewMem.addReg(MemReg, getKillRegState(operandIsKill(*MemI, MemReg, TRI)));
    NewMem.addReg(Base)
        .addReg(Scratch, RegState::Kill)
        .addImm(BaseOffset + MemOffset);
    NewMem.cloneMemRefs(*MemI);

    if (CopyI != MBB.end())
      CopyI->eraseFromParent();
    ExtI->eraseFromParent();
    ShlI->eraseFromParent();
    if (BaseI != MBB.end())
      BaseI->eraseFromParent();
    AddI->eraseFromParent();
    MemI->eraseFromParent();
    return true;
  };

  auto TryFoldScaledDIndexMemCopy = [&](MachineBasicBlock::iterator ExtI) {
    if ((ExtI->getOpcode() != Bedrock::EXTSQ32rr &&
         ExtI->getOpcode() != Bedrock::EXTZQ32rr) ||
        ExtI->getNumOperands() < 2 || !ExtI->getOperand(0).isReg() ||
        !ExtI->getOperand(1).isReg() || !isDReg(ExtI->getOperand(0).getReg()) ||
        !isDReg(ExtI->getOperand(1).getReg()))
      return false;

    Register Index64 = ExtI->getOperand(0).getReg();
    Register Index32 = ExtI->getOperand(1).getReg();
    MachineBasicBlock::iterator ShlI = nextNonDebug(ExtI, MBB);
    if (ShlI == MBB.end() || ShlI->getOpcode() != Bedrock::SHL64ri ||
        ShlI->getNumOperands() < 3 || !ShlI->getOperand(0).isReg() ||
        !ShlI->getOperand(1).isReg() || !ShlI->getOperand(2).isImm() ||
        ShlI->getOperand(2).getImm() != 2 ||
        !regsOverlap(TRI, ShlI->getOperand(0).getReg(), Index64) ||
        !regsOverlap(TRI, ShlI->getOperand(1).getReg(), Index64) ||
        !regDefDeadOrDeadAfter(ShlI, MBB, Bedrock::FLAGS, TRI))
      return false;

    MachineBasicBlock::iterator BaseI = nextNonDebug(ShlI, MBB);
    MachineBasicBlock::iterator AddI =
        BaseI == MBB.end() ? MBB.end() : nextNonDebug(BaseI, MBB);
    if (BaseI == MBB.end() || AddI == MBB.end() ||
        BaseI->getOpcode() != Bedrock::MOV64rr || BaseI->getNumOperands() < 2 ||
        !BaseI->getOperand(0).isReg() || !BaseI->getOperand(1).isReg() ||
        AddI->getOpcode() != Bedrock::ADD64rr || AddI->getNumOperands() < 3 ||
        !AddI->getOperand(0).isReg() || !AddI->getOperand(1).isReg() ||
        !AddI->getOperand(2).isReg() ||
        !isAReg(BaseI->getOperand(0).getReg()) ||
        !isAReg(BaseI->getOperand(1).getReg()) ||
        !regsOverlap(TRI, AddI->getOperand(0).getReg(),
                     BaseI->getOperand(0).getReg()) ||
        !regsOverlap(TRI, AddI->getOperand(1).getReg(),
                     BaseI->getOperand(0).getReg()) ||
        !regsOverlap(TRI, AddI->getOperand(2).getReg(), Index64) ||
        (instrDefinesReg(*AddI, Bedrock::FLAGS, TRI) &&
         !regDefDeadOrDeadAfter(AddI, MBB, Bedrock::FLAGS, TRI)))
      return false;

    Register Addr = BaseI->getOperand(0).getReg();
    Register AddrBase = BaseI->getOperand(1).getReg();
    MachineBasicBlock::iterator MemI = nextNonDebug(AddI, MBB);
    if (MemI == MBB.end())
      return false;

    Register SrcBase;
    Register DstBase;
    int64_t SrcOffset = 0;
    int64_t DstOffset = 0;
    if (MemI->getOpcode() == Bedrock::MOV32idx1mm &&
        MemI->getNumOperands() >= 5 && MemI->getOperand(0).isReg() &&
        MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
        MemI->getOperand(3).isReg() && MemI->getOperand(4).isImm()) {
      if (!regsOverlap(TRI, MemI->getOperand(1).getReg(), Index64) ||
          !regsOverlap(TRI, MemI->getOperand(3).getReg(), Addr))
        return false;
      SrcBase = MemI->getOperand(0).getReg();
      SrcOffset = MemI->getOperand(2).getImm();
      DstBase = AddrBase;
      DstOffset = MemI->getOperand(4).getImm();
    } else if (MemI->getOpcode() == Bedrock::MOV32midx1 &&
               MemI->getNumOperands() >= 5 && MemI->getOperand(0).isReg() &&
               MemI->getOperand(1).isImm() && MemI->getOperand(2).isReg() &&
               MemI->getOperand(3).isReg() && MemI->getOperand(4).isImm()) {
      if (!regsOverlap(TRI, MemI->getOperand(0).getReg(), Addr) ||
          !regsOverlap(TRI, MemI->getOperand(3).getReg(), Index64))
        return false;
      SrcBase = AddrBase;
      SrcOffset = MemI->getOperand(1).getImm();
      DstBase = MemI->getOperand(2).getReg();
      DstOffset = MemI->getOperand(4).getImm();
    } else {
      return false;
    }

    auto IsIndexBase = [](Register Reg) {
      return Reg == Bedrock::SP || isAReg(Reg);
    };
    if (!IsIndexBase(SrcBase) || !IsIndexBase(DstBase))
      return false;

    MachineBasicBlock::iterator AfterMem = nextNonDebug(MemI, MBB);
    if (!regUnusedBeforeEndOrDef(AfterMem, MBB, Addr, TRI) ||
        !regUnusedBeforeEndOrDef(AfterMem, MBB, Index64, TRI))
      return false;

    Register Scratch = FindDeadDReg(ExtI, {Index32});
    if (!Scratch)
      return false;

    MachineInstrBuilder Load = BuildMI(MBB, ExtI, MemI->getDebugLoc(),
                                       TII.get(Bedrock::MOV32idx4lrm), Scratch)
                                   .addReg(SrcBase)
                                   .addReg(Index32)
                                   .addImm(SrcOffset);
    Load.cloneMemRefs(*MemI);
    MachineInstrBuilder Store =
        BuildMI(MBB, ExtI, MemI->getDebugLoc(), TII.get(Bedrock::MOV32idx4lmr))
            .addReg(Scratch, RegState::Kill)
            .addReg(DstBase)
            .addReg(Index32,
                    getKillRegState(operandIsKill(*ExtI, Index32, TRI)))
            .addImm(DstOffset);
    Store.cloneMemRefs(*MemI);

    ExtI->eraseFromParent();
    ShlI->eraseFromParent();
    BaseI->eraseFromParent();
    AddI->eraseFromParent();
    MemI->eraseFromParent();
    return true;
  };

  auto TryFoldLeaScale4MultiMem = [&](MachineBasicBlock::iterator LeaI,
                                      bool LongIndex) {
    if (LeaI->getNumOperands() < 3 || !LeaI->getOperand(0).isReg() ||
        !LeaI->getOperand(1).isReg() || !LeaI->getOperand(2).isReg())
      return false;

    Register Addr = LeaI->getOperand(0).getReg();
    Register Base = LeaI->getOperand(1).getReg();
    Register Index = LeaI->getOperand(2).getReg();
    if (!isAReg(Addr) || !(isAReg(Base) || Base == Bedrock::SP) ||
        !isDReg(Index))
      return false;

    struct IndexedMemUse {
      enum KindTy { Load, Store, MemToMemSrc, MemToMemDst };
      MachineInstr *MI = nullptr;
      KindTy Kind = Load;
      Register Reg;
      int64_t Offset = 0;
      Register OtherBase;
      int64_t OtherOffset = 0;
    };
    SmallVector<IndexedMemUse, 4> Uses;

    for (auto Scan = nextNonDebug(LeaI, MBB); Scan != MBB.end();
         Scan = nextNonDebug(Scan, MBB)) {
      if (instrHasRegMaskForReg(*Scan, Addr, TRI) ||
          instrHasRegMaskForReg(*Scan, Base, TRI) ||
          instrHasRegMaskForReg(*Scan, Index, TRI) ||
          instrDefinesReg(*Scan, Base, TRI) ||
          instrDefinesReg(*Scan, Index, TRI))
        return false;

      bool UsesAddr = instrUsesReg(*Scan, Addr, TRI);
      if (!UsesAddr) {
        if (instrDefinesReg(*Scan, Addr, TRI))
          break;
        continue;
      }

      IndexedMemUse Use;
      Use.MI = &*Scan;
      if (Scan->getOpcode() == Bedrock::MOV32rm &&
          Scan->getNumOperands() >= 3 && Scan->getOperand(0).isReg() &&
          Scan->getOperand(1).isReg() && Scan->getOperand(2).isImm() &&
          regsOverlap(TRI, Scan->getOperand(1).getReg(), Addr) &&
          !instrDefinesReg(*Scan, Base, TRI) &&
          !instrDefinesReg(*Scan, Index, TRI) && !hasOrderedMemOperand(*Scan)) {
        Use.Kind = IndexedMemUse::Load;
        Use.Reg = Scan->getOperand(0).getReg();
        Use.Offset = Scan->getOperand(2).getImm();
      } else if (Scan->getOpcode() == Bedrock::MOV32mr &&
                 Scan->getNumOperands() >= 3 && Scan->getOperand(0).isReg() &&
                 Scan->getOperand(1).isReg() && Scan->getOperand(2).isImm() &&
                 regsOverlap(TRI, Scan->getOperand(1).getReg(), Addr) &&
                 !hasOrderedMemOperand(*Scan)) {
        Use.Kind = IndexedMemUse::Store;
        Use.Reg = Scan->getOperand(0).getReg();
        Use.Offset = Scan->getOperand(2).getImm();
      } else if (Scan->getOpcode() == Bedrock::MOV32mm &&
                 Scan->getNumOperands() >= 4 && Scan->getOperand(0).isReg() &&
                 Scan->getOperand(1).isImm() && Scan->getOperand(2).isReg() &&
                 Scan->getOperand(3).isImm() && !hasOrderedMemOperand(*Scan)) {
        bool SrcUsesAddr = regsOverlap(TRI, Scan->getOperand(0).getReg(), Addr);
        bool DstUsesAddr = regsOverlap(TRI, Scan->getOperand(2).getReg(), Addr);
        if (SrcUsesAddr == DstUsesAddr)
          return false;
        if (SrcUsesAddr) {
          Use.Kind = IndexedMemUse::MemToMemSrc;
          Use.Offset = Scan->getOperand(1).getImm();
          Use.OtherBase = Scan->getOperand(2).getReg();
          Use.OtherOffset = Scan->getOperand(3).getImm();
        } else {
          Use.Kind = IndexedMemUse::MemToMemDst;
          Use.OtherBase = Scan->getOperand(0).getReg();
          Use.OtherOffset = Scan->getOperand(1).getImm();
          Use.Offset = Scan->getOperand(3).getImm();
        }
      } else {
        return false;
      }

      Uses.push_back(Use);
      if (Uses.size() > 4)
        return false;
    }

    // LEA scale-4 forms are 6 bytes. Replacing each [addr+off] memory
    // use with an indexed form adds 2 bytes, so one or two folded uses
    // are size-profitable.
    if (Uses.empty() || Uses.size() > 2)
      return false;

    MachineBasicBlock::iterator AfterLast =
        nextNonDebug(Uses.back().MI->getIterator(), MBB);
    if (!regUnusedBeforeEndOrDef(AfterLast, MBB, Addr, TRI))
      return false;

    unsigned LoadOpcode =
        LongIndex ? Bedrock::MOV32idx4lrm : Bedrock::MOV32idx4rm;
    unsigned StoreOpcode =
        LongIndex ? Bedrock::MOV32idx4lmr : Bedrock::MOV32idx4mr;
    unsigned MemToMemSrcOpcode =
        LongIndex ? Bedrock::MOV32idx4lmm : Bedrock::MOV32idx4mm;
    unsigned MemToMemDstOpcode =
        LongIndex ? Bedrock::MOV32midx4l : Bedrock::MOV32midx4;

    for (IndexedMemUse &Use : Uses) {
      if (Use.Kind == IndexedMemUse::Load) {
        MachineInstrBuilder MIB =
            BuildMI(MBB, Use.MI->getIterator(), Use.MI->getDebugLoc(),
                    TII.get(LoadOpcode), Use.Reg)
                .addReg(Base)
                .addReg(Index)
                .addImm(Use.Offset);
        MIB.cloneMemRefs(*Use.MI);
      } else if (Use.Kind == IndexedMemUse::Store) {
        MachineInstrBuilder MIB =
            BuildMI(MBB, Use.MI->getIterator(), Use.MI->getDebugLoc(),
                    TII.get(StoreOpcode))
                .addReg(Use.Reg)
                .addReg(Base)
                .addReg(Index)
                .addImm(Use.Offset);
        MIB.cloneMemRefs(*Use.MI);
      } else if (Use.Kind == IndexedMemUse::MemToMemSrc) {
        MachineInstrBuilder MIB =
            BuildMI(MBB, Use.MI->getIterator(), Use.MI->getDebugLoc(),
                    TII.get(MemToMemSrcOpcode))
                .addReg(Base)
                .addReg(Index)
                .addImm(Use.Offset)
                .addReg(Use.OtherBase)
                .addImm(Use.OtherOffset);
        MIB.cloneMemRefs(*Use.MI);
      } else {
        MachineInstrBuilder MIB =
            BuildMI(MBB, Use.MI->getIterator(), Use.MI->getDebugLoc(),
                    TII.get(MemToMemDstOpcode))
                .addReg(Use.OtherBase)
                .addImm(Use.OtherOffset)
                .addReg(Base)
                .addReg(Index)
                .addImm(Use.Offset);
        MIB.cloneMemRefs(*Use.MI);
      }
    }

    for (IndexedMemUse &Use : Uses)
      Use.MI->eraseFromParent();
    LeaI->eraseFromParent();
    return true;
  };

  auto TryFoldLeaBiasIntoIndexedMem = [&](MachineBasicBlock::iterator LeaI) {
    if (LeaI->getOpcode() != Bedrock::LEAri || LeaI->getNumOperands() < 3 ||
        !LeaI->getOperand(0).isReg() || !LeaI->getOperand(1).isReg() ||
        !LeaI->getOperand(2).isImm())
      return false;

    Register Alias = LeaI->getOperand(0).getReg();
    Register Base = LeaI->getOperand(1).getReg();
    int64_t Bias = LeaI->getOperand(2).getImm();
    if (!isAReg(Alias) || !(isAReg(Base) || Base == Bedrock::SP) ||
        Alias == Bedrock::SP || regsOverlap(TRI, Alias, Base))
      return false;

    struct IndexedBaseRewrite {
      MachineInstr *MI = nullptr;
      unsigned BaseOp = 0;
      unsigned OffsetOp = 0;
    };
    SmallVector<IndexedBaseRewrite, 4> Uses;

    auto AddRewrite = [&](MachineInstr &MI, unsigned BaseOp,
                          unsigned OffsetOp) {
      if (MI.getNumOperands() <= OffsetOp || !MI.getOperand(BaseOp).isReg() ||
          !MI.getOperand(OffsetOp).isImm() ||
          !regsOverlap(TRI, MI.getOperand(BaseOp).getReg(), Alias))
        return false;
      Uses.push_back({&MI, BaseOp, OffsetOp});
      return true;
    };

    auto RewriteIndexedUse = [&](MachineInstr &MI) {
      switch (MI.getOpcode()) {
      default:
        return false;
      case Bedrock::MOV32idx4rm:
      case Bedrock::MOV32idx4lrm:
      case Bedrock::MOV32idx4mr:
      case Bedrock::MOV32idx4lmr:
        return AddRewrite(MI, 1, 3);
      case Bedrock::MOV32idx4mm:
      case Bedrock::MOV32idx4lmm:
        return AddRewrite(MI, 0, 2);
      case Bedrock::MOV32midx4:
      case Bedrock::MOV32midx4l:
        return AddRewrite(MI, 2, 4);
      case Bedrock::ADD32idx4rm:
      case Bedrock::ADD32idx4lrm:
      case Bedrock::SUB32idx4rm:
      case Bedrock::SUB32idx4lrm:
      case Bedrock::AND32idx4rm:
      case Bedrock::AND32idx4lrm:
      case Bedrock::OR32idx4rm:
      case Bedrock::OR32idx4lrm:
      case Bedrock::XOR32idx4rm:
      case Bedrock::XOR32idx4lrm:
      case Bedrock::MULU32idx4rm:
      case Bedrock::MULU32idx4lrm:
        return AddRewrite(MI, 2, 4);
      case Bedrock::CMP32idx4rm:
      case Bedrock::CMP32idx4lrm:
      case Bedrock::CMP32idx4mr:
      case Bedrock::CMP32idx4lmr:
      case Bedrock::TEST32idx4rm:
      case Bedrock::TEST32idx4lrm:
      case Bedrock::TEST32idx4mr:
      case Bedrock::TEST32idx4lmr:
        return AddRewrite(MI, 1, 3);
      case Bedrock::INC32idx4m:
      case Bedrock::INC32idx4lm:
      case Bedrock::DEC32idx4m:
      case Bedrock::DEC32idx4lm:
        return AddRewrite(MI, 0, 2);
      }
    };

    SmallPtrSet<MachineBasicBlock *, 8> Region;
    SmallVector<MachineBasicBlock *, 8> Worklist;
    Region.insert(&MBB);
    for (MachineBasicBlock *Succ : MBB.successors())
      if (blockHasLiveInReg(*Succ, Alias, TRI) && Region.insert(Succ).second)
        Worklist.push_back(Succ);

    while (!Worklist.empty()) {
      MachineBasicBlock *Block = Worklist.pop_back_val();
      for (MachineBasicBlock *Succ : Block->successors())
        if (blockHasLiveInReg(*Succ, Alias, TRI) && Region.insert(Succ).second)
          Worklist.push_back(Succ);
    }

    for (MachineBasicBlock *Block : Region) {
      if (Block == &MBB)
        continue;
      if (Base != Bedrock::SP && !blockHasLiveInReg(*Block, Base, TRI))
        return false;
      for (MachineBasicBlock *Pred : Block->predecessors())
        if (Pred != &MBB && !Region.contains(Pred))
          return false;
    }

    for (MachineBasicBlock *Block : Region) {
      MachineBasicBlock::iterator Scan =
          Block == &MBB ? nextNonDebug(LeaI, MBB) : Block->begin();
      while (Scan != Block->end()) {
        if (Scan->isDebugInstr()) {
          ++Scan;
          continue;
        }
        if (instrHasRegMaskForReg(*Scan, Alias, TRI) ||
            instrHasRegMaskForReg(*Scan, Base, TRI) ||
            instrDefinesReg(*Scan, Base, TRI))
          return false;
        if (instrUsesReg(*Scan, Alias, TRI)) {
          if (!RewriteIndexedUse(*Scan))
            return false;
          if (Uses.size() > 4)
            return false;
        }
        if (instrDefinesReg(*Scan, Alias, TRI))
          return false;
        ++Scan;
      }
    }

    if (Uses.empty())
      return false;

    for (IndexedBaseRewrite &Use : Uses) {
      Use.MI->getOperand(Use.BaseOp).setReg(Base);
      Use.MI->getOperand(Use.OffsetOp)
          .setImm(Use.MI->getOperand(Use.OffsetOp).getImm() + Bias);
    }
    LeaI->eraseFromParent();
    return true;
  };

  auto TryFoldScale1AddrIntoIndexedUses = [&](MachineBasicBlock::iterator
                                                  ExtI) {
    if (!((ExtI->getOpcode() == Bedrock::LEAri && ExtI->getNumOperands() >= 3 &&
           ExtI->getOperand(0).isReg() && ExtI->getOperand(1).isReg() &&
           ExtI->getOperand(2).isImm()) ||
          (ExtI->getOpcode() == Bedrock::MOV64rr &&
           ExtI->getNumOperands() >= 2 && ExtI->getOperand(0).isReg() &&
           ExtI->getOperand(1).isReg())))
      return false;

    Register Addr = ExtI->getOperand(0).getReg();
    Register Base = ExtI->getOperand(1).getReg();
    int64_t BaseOffset = 0;
    if (ExtI->getOpcode() == Bedrock::LEAri)
      BaseOffset = ExtI->getOperand(2).getImm();

    if (!isAReg(Addr) || !(isAReg(Base) || Base == Bedrock::SP) ||
        Addr == Bedrock::SP || regsOverlap(TRI, Addr, Base))
      return false;

    auto AddI = nextNonDebug(ExtI, MBB);
    if (AddI == MBB.end() || AddI->getOpcode() != Bedrock::ADD64rr ||
        AddI->getNumOperands() < 3 || !AddI->getOperand(0).isReg() ||
        !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg() ||
        !regsOverlap(TRI, AddI->getOperand(0).getReg(), Addr) ||
        (!regsOverlap(TRI, AddI->getOperand(1).getReg(), Addr) &&
         !regsOverlap(TRI, AddI->getOperand(2).getReg(), Addr)) ||
        (instrDefinesReg(*AddI, Bedrock::FLAGS, TRI) &&
         !regDefDeadOrDeadAfter(AddI, MBB, Bedrock::FLAGS, TRI)))
      return false;

    Register Index = regsOverlap(TRI, AddI->getOperand(1).getReg(), Addr)
                         ? AddI->getOperand(2).getReg()
                         : AddI->getOperand(1).getReg();
    if (!isDReg(Index) || regsOverlap(TRI, Index, Base))
      return false;

    if (MachineInstr *IndexDef = findLastDefBefore(*ExtI, Index, TRI);
        IndexDef && IndexDef->getOpcode() == Bedrock::SHL64ri &&
        IndexDef->getNumOperands() >= 3 && IndexDef->getOperand(0).isReg() &&
        IndexDef->getOperand(1).isReg() && IndexDef->getOperand(2).isImm() &&
        IndexDef->getOperand(2).getImm() == 2 &&
        regsOverlap(TRI, IndexDef->getOperand(0).getReg(), Index) &&
        regsOverlap(TRI, IndexDef->getOperand(1).getReg(), Index))
      return false;

    struct Scale1IndexedUse {
      MachineInstr *MI = nullptr;
      enum KindTy {
        Load,
        Store,
        MemToMemSrc,
        MemToMemDst,
        BinRM,
        CmpRM,
        CmpMR,
        TestRM,
        TestMR,
        ImmFlag,
        Inc,
        Dec
      } Kind = Load;
      Register Reg;
      Register OtherBase;
      int64_t Imm = 0;
      int64_t Offset = 0;
      int64_t OtherOffset = 0;
      unsigned NewOpcode = 0;
    };
    SmallVector<Scale1IndexedUse, 4> Uses;

    auto MatchUse = [&](MachineInstr &MI, Scale1IndexedUse &Use) -> bool {
      Use.MI = &MI;
      switch (MI.getOpcode()) {
      default:
        return false;
      case Bedrock::MOV8rm:
      case Bedrock::MOV16rm:
      case Bedrock::MOV32rm:
      case Bedrock::MOV64rm:
        if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
            !MI.getOperand(1).isReg() || !MI.getOperand(2).isImm() ||
            !regsOverlap(TRI, MI.getOperand(1).getReg(), Addr))
          return false;
        Use.Kind = Scale1IndexedUse::Load;
        Use.Reg = MI.getOperand(0).getReg();
        Use.Offset = MI.getOperand(2).getImm();
        Use.NewOpcode = getIndexedMemLoadOpcode(MI.getOpcode(), 1, false);
        return Use.NewOpcode != 0;
      case Bedrock::MOV16mr:
      case Bedrock::MOV32mr:
      case Bedrock::MOV64mr:
        if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
            !MI.getOperand(1).isReg() || !MI.getOperand(2).isImm() ||
            !regsOverlap(TRI, MI.getOperand(1).getReg(), Addr))
          return false;
        Use.Kind = Scale1IndexedUse::Store;
        Use.Reg = MI.getOperand(0).getReg();
        Use.Offset = MI.getOperand(2).getImm();
        Use.NewOpcode = getIndexedMemStoreOpcode(MI.getOpcode(), 1, false);
        return Use.NewOpcode != 0;
      case Bedrock::MOV32mm:
        if (MI.getNumOperands() < 4 || !MI.getOperand(0).isReg() ||
            !MI.getOperand(1).isImm() || !MI.getOperand(2).isReg() ||
            !MI.getOperand(3).isImm())
          return false;
        if (regsOverlap(TRI, MI.getOperand(0).getReg(), Addr) ==
            regsOverlap(TRI, MI.getOperand(2).getReg(), Addr))
          return false;
        if (regsOverlap(TRI, MI.getOperand(0).getReg(), Addr)) {
          Use.Kind = Scale1IndexedUse::MemToMemSrc;
          Use.Offset = MI.getOperand(1).getImm();
          Use.OtherBase = MI.getOperand(2).getReg();
          Use.OtherOffset = MI.getOperand(3).getImm();
          Use.NewOpcode = Bedrock::MOV32idx1mm;
        } else {
          Use.Kind = Scale1IndexedUse::MemToMemDst;
          Use.OtherBase = MI.getOperand(0).getReg();
          Use.OtherOffset = MI.getOperand(1).getImm();
          Use.Offset = MI.getOperand(3).getImm();
          Use.NewOpcode = Bedrock::MOV32midx1;
        }
        return true;
      case Bedrock::ADD32rm:
      case Bedrock::SUB32rm:
      case Bedrock::AND32rm:
      case Bedrock::OR32rm:
      case Bedrock::XOR32rm:
      case Bedrock::MULU32rm:
        if (MI.getNumOperands() < 4 || !MI.getOperand(0).isReg() ||
            !MI.getOperand(1).isReg() || !MI.getOperand(2).isReg() ||
            !MI.getOperand(3).isImm() ||
            !regsOverlap(TRI, MI.getOperand(2).getReg(), Addr))
          return false;
        Use.Kind = Scale1IndexedUse::BinRM;
        Use.Reg = MI.getOperand(0).getReg();
        Use.Offset = MI.getOperand(3).getImm();
        Use.NewOpcode = getIndexedMemSourceOpcode(MI.getOpcode(), 1, false);
        return Use.NewOpcode != 0;
      case Bedrock::CMP32rm:
      case Bedrock::CMP8rm:
      case Bedrock::CMP16rm:
      case Bedrock::CMP64rm:
      case Bedrock::TEST32rm:
      case Bedrock::TEST8rm:
      case Bedrock::TEST16rm:
      case Bedrock::TEST64rm:
        if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
            !MI.getOperand(1).isReg() || !MI.getOperand(2).isImm() ||
            !regsOverlap(TRI, MI.getOperand(1).getReg(), Addr))
          return false;
        Use.Kind = MI.getOpcode() == Bedrock::CMP8rm ||
                           MI.getOpcode() == Bedrock::CMP16rm ||
                           MI.getOpcode() == Bedrock::CMP32rm ||
                           MI.getOpcode() == Bedrock::CMP64rm
                       ? Scale1IndexedUse::CmpRM
                       : Scale1IndexedUse::TestRM;
        Use.Reg = MI.getOperand(0).getReg();
        Use.Offset = MI.getOperand(2).getImm();
        Use.NewOpcode = getIndexedMemRegFlagOpcode(
            Use.Kind == Scale1IndexedUse::CmpRM
                ? (MI.getOpcode() == Bedrock::CMP8rm    ? Bedrock::CMP8rr
                   : MI.getOpcode() == Bedrock::CMP16rm ? Bedrock::CMP16rr
                   : MI.getOpcode() == Bedrock::CMP32rm ? Bedrock::CMP32rr
                                                        : Bedrock::CMP64rr)
                : (MI.getOpcode() == Bedrock::TEST8rm    ? Bedrock::TEST8rr
                   : MI.getOpcode() == Bedrock::TEST16rm ? Bedrock::TEST16rr
                   : MI.getOpcode() == Bedrock::TEST32rm ? Bedrock::TEST32rr
                                                         : Bedrock::TEST64rr),
            1, false, false);
        return Use.NewOpcode != 0;
      case Bedrock::CMP32mr:
      case Bedrock::CMP8mr:
      case Bedrock::CMP16mr:
      case Bedrock::CMP64mr:
      case Bedrock::TEST32mr:
      case Bedrock::TEST8mr:
      case Bedrock::TEST16mr:
      case Bedrock::TEST64mr:
        if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
            !MI.getOperand(1).isReg() || !MI.getOperand(2).isImm() ||
            !regsOverlap(TRI, MI.getOperand(1).getReg(), Addr))
          return false;
        Use.Kind = MI.getOpcode() == Bedrock::CMP8mr ||
                           MI.getOpcode() == Bedrock::CMP16mr ||
                           MI.getOpcode() == Bedrock::CMP32mr ||
                           MI.getOpcode() == Bedrock::CMP64mr
                       ? Scale1IndexedUse::CmpMR
                       : Scale1IndexedUse::TestMR;
        Use.Reg = MI.getOperand(0).getReg();
        Use.Offset = MI.getOperand(2).getImm();
        Use.NewOpcode = getIndexedMemRegFlagOpcode(
            Use.Kind == Scale1IndexedUse::CmpMR
                ? (MI.getOpcode() == Bedrock::CMP8mr    ? Bedrock::CMP8rr
                   : MI.getOpcode() == Bedrock::CMP16mr ? Bedrock::CMP16rr
                   : MI.getOpcode() == Bedrock::CMP32mr ? Bedrock::CMP32rr
                                                        : Bedrock::CMP64rr)
                : (MI.getOpcode() == Bedrock::TEST8mr    ? Bedrock::TEST8rr
                   : MI.getOpcode() == Bedrock::TEST16mr ? Bedrock::TEST16rr
                   : MI.getOpcode() == Bedrock::TEST32mr ? Bedrock::TEST32rr
                                                         : Bedrock::TEST64rr),
            1, false, true);
        return Use.NewOpcode != 0;
      case Bedrock::CMP8mi:
      case Bedrock::CMP16mi:
      case Bedrock::CMP32mi:
      case Bedrock::CMP64mi:
      case Bedrock::TEST8mi:
      case Bedrock::TEST16mi:
      case Bedrock::TEST32mi:
      case Bedrock::TEST64mi:
        if (MI.getNumOperands() < 3 || !MI.getOperand(0).isImm() ||
            !MI.getOperand(1).isReg() || !MI.getOperand(2).isImm() ||
            !regsOverlap(TRI, MI.getOperand(1).getReg(), Addr))
          return false;
        Use.Kind = Scale1IndexedUse::ImmFlag;
        Use.Imm = MI.getOperand(0).getImm();
        Use.Offset = MI.getOperand(2).getImm();
        Use.NewOpcode = getIndexedMemImmFlagOpcode(MI.getOpcode(), 1, false);
        return Use.NewOpcode != 0;
      case Bedrock::INC32m:
      case Bedrock::DEC32m:
        if (MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
            !MI.getOperand(1).isImm() ||
            !regsOverlap(TRI, MI.getOperand(0).getReg(), Addr))
          return false;
        Use.Kind = MI.getOpcode() == Bedrock::INC32m ? Scale1IndexedUse::Inc
                                                     : Scale1IndexedUse::Dec;
        Use.Offset = MI.getOperand(1).getImm();
        Use.NewOpcode = MI.getOpcode() == Bedrock::INC32m ? Bedrock::INC32idx1m
                                                          : Bedrock::DEC32idx1m;
        return true;
      }
    };

    SmallPtrSet<MachineBasicBlock *, 8> Region;
    SmallVector<MachineBasicBlock *, 8> Worklist;
    Region.insert(&MBB);
    for (MachineBasicBlock *Succ : MBB.successors())
      if (blockHasLiveInReg(*Succ, Addr, TRI) && Region.insert(Succ).second)
        Worklist.push_back(Succ);

    while (!Worklist.empty()) {
      MachineBasicBlock *Block = Worklist.pop_back_val();
      for (MachineBasicBlock *Succ : Block->successors())
        if (blockHasLiveInReg(*Succ, Addr, TRI) && Region.insert(Succ).second)
          Worklist.push_back(Succ);
    }

    for (MachineBasicBlock *Block : Region) {
      if (Block == &MBB)
        continue;
      for (MachineBasicBlock *Pred : Block->predecessors())
        if (!Region.contains(Pred))
          return false;
    }

    for (MachineBasicBlock *Block : Region) {
      MachineBasicBlock::iterator Scan =
          Block == &MBB ? nextNonDebug(AddI, MBB) : Block->begin();
      while (Scan != Block->end()) {
        if (Scan->isDebugInstr()) {
          ++Scan;
          continue;
        }
        if (instrHasRegMaskForReg(*Scan, Addr, TRI) ||
            instrHasRegMaskForReg(*Scan, Base, TRI) ||
            instrHasRegMaskForReg(*Scan, Index, TRI))
          return false;

        bool UsesAddr = instrUsesReg(*Scan, Addr, TRI);
        if (!UsesAddr) {
          if (instrDefinesReg(*Scan, Addr, TRI))
            break;
          if (instrDefinesReg(*Scan, Base, TRI) ||
              instrDefinesReg(*Scan, Index, TRI))
            return false;
          ++Scan;
          continue;
        }

        if (instrDefinesReg(*Scan, Base, TRI) ||
            instrDefinesReg(*Scan, Index, TRI))
          return false;

        Scale1IndexedUse Use;
        if (!MatchUse(*Scan, Use))
          return false;
        Uses.push_back(Use);
        if (Uses.size() > 4)
          return false;
        ++Scan;
      }
    }

    if (Uses.empty())
      return false;

    for (MachineBasicBlock *Block : Region) {
      if (Block == &MBB)
        continue;
      if (Base != Bedrock::SP && !Block->isLiveIn(Base))
        Block->addLiveIn(Base);
      if (!Block->isLiveIn(Index))
        Block->addLiveIn(Index);
    }

    for (Scale1IndexedUse &Use : Uses) {
      MachineInstr &MI = *Use.MI;
      MachineInstrBuilder MIB =
          BuildMI(*MI.getParent(), MI.getIterator(), MI.getDebugLoc(),
                  TII.get(Use.NewOpcode));
      switch (Use.Kind) {
      case Scale1IndexedUse::Load:
        MIB.addReg(Use.Reg, RegState::Define)
            .addReg(Base)
            .addReg(Index)
            .addImm(BaseOffset + Use.Offset);
        break;
      case Scale1IndexedUse::Store:
        MIB.addReg(Use.Reg).addReg(Base).addReg(Index).addImm(BaseOffset +
                                                              Use.Offset);
        break;
      case Scale1IndexedUse::MemToMemSrc:
        MIB.addReg(Base)
            .addReg(Index)
            .addImm(BaseOffset + Use.Offset)
            .addReg(Use.OtherBase)
            .addImm(Use.OtherOffset);
        break;
      case Scale1IndexedUse::MemToMemDst:
        MIB.addReg(Use.OtherBase)
            .addImm(Use.OtherOffset)
            .addReg(Base)
            .addReg(Index)
            .addImm(BaseOffset + Use.Offset);
        break;
      case Scale1IndexedUse::BinRM:
        MIB.addReg(Use.Reg, RegState::Define)
            .addReg(MI.getOperand(1).getReg())
            .addReg(Base)
            .addReg(Index)
            .addImm(BaseOffset + Use.Offset);
        break;
      case Scale1IndexedUse::CmpRM:
      case Scale1IndexedUse::TestRM:
        MIB.addReg(Use.Reg).addReg(Base).addReg(Index).addImm(BaseOffset +
                                                              Use.Offset);
        break;
      case Scale1IndexedUse::CmpMR:
      case Scale1IndexedUse::TestMR:
        MIB.addReg(Use.Reg).addReg(Base).addReg(Index).addImm(BaseOffset +
                                                              Use.Offset);
        break;
      case Scale1IndexedUse::ImmFlag:
        MIB.addImm(Use.Imm).addReg(Base).addReg(Index).addImm(BaseOffset +
                                                              Use.Offset);
        break;
      case Scale1IndexedUse::Inc:
      case Scale1IndexedUse::Dec:
        MIB.addReg(Base).addReg(Index).addImm(BaseOffset + Use.Offset);
        break;
      }
      MIB.cloneMemRefs(MI);
    }

    for (Scale1IndexedUse &Use : Uses)
      Use.MI->eraseFromParent();
    ExtI->eraseFromParent();
    AddI->eraseFromParent();
    removeRegLiveInsWithoutUses(MF, Addr, TRI);
    return true;
  };

  auto TryFoldScaledARegAddrMem = [&](MachineBasicBlock::iterator BaseI) {
    if (!((BaseI->getOpcode() == Bedrock::MOV64rr &&
           BaseI->getNumOperands() >= 2 && BaseI->getOperand(0).isReg() &&
           BaseI->getOperand(1).isReg()) ||
          (BaseI->getOpcode() == Bedrock::LEAri &&
           BaseI->getNumOperands() >= 3 && BaseI->getOperand(0).isReg() &&
           BaseI->getOperand(1).isReg() && BaseI->getOperand(2).isImm())))
      return false;

    Register Addr = BaseI->getOperand(0).getReg();
    Register Base = BaseI->getOperand(1).getReg();
    int64_t BaseOffset = BaseI->getOpcode() == Bedrock::LEAri
                             ? BaseI->getOperand(2).getImm()
                             : 0;
    if (!isAReg(Addr) || !(isAReg(Base) || Base == Bedrock::SP) ||
        Addr == Bedrock::SP || regsOverlap(TRI, Addr, Base))
      return false;

    auto AddI = nextNonDebug(BaseI, MBB);
    if (AddI == MBB.end() || AddI->getOpcode() != Bedrock::ADD64rr ||
        AddI->getNumOperands() < 3 || !AddI->getOperand(0).isReg() ||
        !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg() ||
        !regsOverlap(TRI, AddI->getOperand(0).getReg(), Addr) ||
        (instrDefinesReg(*AddI, Bedrock::FLAGS, TRI) &&
         !regDefDeadOrDeadAfter(AddI, MBB, Bedrock::FLAGS, TRI)))
      return false;

    Register Scaled;
    if (regsOverlap(TRI, AddI->getOperand(1).getReg(), Addr) &&
        isAReg(AddI->getOperand(2).getReg()))
      Scaled = AddI->getOperand(2).getReg();
    else if (regsOverlap(TRI, AddI->getOperand(2).getReg(), Addr) &&
             isAReg(AddI->getOperand(1).getReg()))
      Scaled = AddI->getOperand(1).getReg();
    else
      return false;

    MachineInstr *Shl = findLastDefBefore(*AddI, Scaled, TRI);
    if (!Shl || Shl->getParent() != &MBB ||
        Shl->getOpcode() != Bedrock::SHL64ri || Shl->getNumOperands() < 3 ||
        !Shl->getOperand(0).isReg() || !Shl->getOperand(1).isReg() ||
        !Shl->getOperand(2).isImm() || Shl->getOperand(2).getImm() != 2 ||
        !regsOverlap(TRI, Shl->getOperand(0).getReg(), Scaled) ||
        !regsOverlap(TRI, Shl->getOperand(1).getReg(), Scaled))
      return false;

    MachineInstr *Ext = findLastDefBefore(*Shl, Scaled, TRI);
    if (!Ext || Ext->getParent() != &MBB ||
        (Ext->getOpcode() != Bedrock::EXTZQ32rr &&
         Ext->getOpcode() != Bedrock::EXTSQ32rr) ||
        Ext->getNumOperands() < 2 || !Ext->getOperand(0).isReg() ||
        !Ext->getOperand(1).isReg() ||
        !regsOverlap(TRI, Ext->getOperand(0).getReg(), Scaled) ||
        !isDReg(Ext->getOperand(1).getReg()))
      return false;

    Register Index = Ext->getOperand(1).getReg();
    auto MemI = nextNonDebug(AddI, MBB);
    if (MemI == MBB.end() || hasOrderedMemOperand(*MemI))
      return false;

    for (auto Scan = nextNonDebug(Ext->getIterator(), MBB); Scan != MemI;
         Scan = nextNonDebug(Scan, MBB)) {
      if (Scan == MBB.end())
        return false;
      if (&*Scan != Shl && &*Scan != &*BaseI && &*Scan != &*AddI &&
          instrDefinesReg(*Scan, Index, TRI))
        return false;
    }

    auto AfterMem = nextNonDebug(MemI, MBB);
    if (!instrDefinesReg(*MemI, Addr, TRI) &&
        !regUnusedBeforeEndOrDef(AfterMem, MBB, Addr, TRI))
      return false;

    unsigned NewOpcode = 0;
    Register Reg;
    enum { Load, Store, CmpRM, CmpMR, TestRM, TestMR } Kind = Load;
    int64_t Offset = 0;
    switch (MemI->getOpcode()) {
    default:
      return false;
    case Bedrock::MOV32rm:
      if (MemI->getNumOperands() < 3 || !MemI->getOperand(0).isReg() ||
          !MemI->getOperand(1).isReg() || !MemI->getOperand(2).isImm() ||
          !regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr))
        return false;
      Kind = Load;
      Reg = MemI->getOperand(0).getReg();
      Offset = MemI->getOperand(2).getImm();
      NewOpcode = Bedrock::MOV32idx4lrm;
      break;
    case Bedrock::MOV32mr:
      if (MemI->getNumOperands() < 3 || !MemI->getOperand(0).isReg() ||
          !MemI->getOperand(1).isReg() || !MemI->getOperand(2).isImm() ||
          !regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr))
        return false;
      Kind = Store;
      Reg = MemI->getOperand(0).getReg();
      Offset = MemI->getOperand(2).getImm();
      NewOpcode = Bedrock::MOV32idx4lmr;
      break;
    case Bedrock::CMP32rm:
    case Bedrock::TEST32rm:
      if (MemI->getNumOperands() < 3 || !MemI->getOperand(0).isReg() ||
          !MemI->getOperand(1).isReg() || !MemI->getOperand(2).isImm() ||
          !regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr))
        return false;
      Kind = MemI->getOpcode() == Bedrock::CMP32rm ? CmpRM : TestRM;
      Reg = MemI->getOperand(0).getReg();
      Offset = MemI->getOperand(2).getImm();
      NewOpcode = MemI->getOpcode() == Bedrock::CMP32rm
                      ? Bedrock::CMP32idx4lrm
                      : Bedrock::TEST32idx4lrm;
      break;
    case Bedrock::CMP32mr:
    case Bedrock::TEST32mr:
      if (MemI->getNumOperands() < 3 || !MemI->getOperand(0).isReg() ||
          !MemI->getOperand(1).isReg() || !MemI->getOperand(2).isImm() ||
          !regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr))
        return false;
      Kind = MemI->getOpcode() == Bedrock::CMP32mr ? CmpMR : TestMR;
      Reg = MemI->getOperand(0).getReg();
      Offset = MemI->getOperand(2).getImm();
      NewOpcode = MemI->getOpcode() == Bedrock::CMP32mr
                      ? Bedrock::CMP32idx4lmr
                      : Bedrock::TEST32idx4lmr;
      break;
    }

    MachineInstrBuilder MIB =
        BuildMI(MBB, BaseI, MemI->getDebugLoc(), TII.get(NewOpcode));
    switch (Kind) {
    case Load:
      MIB.addReg(Reg, RegState::Define)
          .addReg(Base)
          .addReg(Index)
          .addImm(BaseOffset + Offset);
      break;
    case Store:
      MIB.addReg(Reg, getKillRegState(operandIsKill(*MemI, Reg, TRI)))
          .addReg(Base)
          .addReg(Index)
          .addImm(BaseOffset + Offset);
      break;
    case CmpRM:
    case TestRM:
    case CmpMR:
    case TestMR:
      MIB.addReg(Reg).addReg(Base).addReg(Index).addImm(BaseOffset + Offset);
      break;
    }
    MIB.cloneMemRefs(*MemI);

    if (Ext->getOperand(1).isReg() &&
        regsOverlap(TRI, Ext->getOperand(1).getReg(), Index))
      Ext->getOperand(1).setIsKill(false);
    BaseI->eraseFromParent();
    AddI->eraseFromParent();
    MemI->eraseFromParent();
    return true;
  };

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Ext = *I;
    if (Ext.isDebugInstr()) {
      ++I;
      continue;
    }

    if (TryFoldARegIndex(I)) {
      I = MBB.begin();
      Changed = true;
      continue;
    }

    if (TryFoldScaledDIndexMemCopy(I)) {
      I = MBB.begin();
      Changed = true;
      continue;
    }

    if (Ext.getOpcode() == Bedrock::LEA4 &&
        TryFoldLeaScale4MultiMem(I, false)) {
      I = MBB.begin();
      Changed = true;
      continue;
    }

    if (Ext.getOpcode() == Bedrock::LEA4L &&
        TryFoldLeaScale4MultiMem(I, true)) {
      I = MBB.begin();
      Changed = true;
      continue;
    }

    if (TryFoldLeaBiasIntoIndexedMem(I)) {
      I = MBB.begin();
      Changed = true;
      continue;
    }

    if (TryFoldScale1AddrIntoIndexedUses(I)) {
      I = MBB.begin();
      Changed = true;
      continue;
    }

    if (TryFoldScaledARegAddrMem(I)) {
      I = MBB.begin();
      Changed = true;
      continue;
    }

    if ((Ext.getOpcode() == Bedrock::LEAri && Ext.getNumOperands() >= 3 &&
         Ext.getOperand(0).isReg() && Ext.getOperand(1).isReg() &&
         Ext.getOperand(2).isImm()) ||
        (Ext.getOpcode() == Bedrock::MOV64rr && Ext.getNumOperands() >= 2 &&
         Ext.getOperand(0).isReg() && Ext.getOperand(1).isReg())) {
      Register Addr = Ext.getOperand(0).getReg();
      Register Base = Ext.getOperand(1).getReg();
      int64_t BaseOffset = 0;
      if (Ext.getOpcode() == Bedrock::LEAri) {
        BaseOffset = Ext.getOperand(2).getImm();
      } else if (!isAReg(Addr)) {
        ++I;
        continue;
      }

      auto AddI = nextNonDebug(I, MBB);
      if (AddI == MBB.end() || AddI->getOpcode() != Bedrock::ADD64rr ||
          AddI->getNumOperands() < 3 || !AddI->getOperand(0).isReg() ||
          !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg() ||
          !regsOverlap(TRI, AddI->getOperand(0).getReg(), Addr) ||
          (!regsOverlap(TRI, AddI->getOperand(1).getReg(), Addr) &&
           !regsOverlap(TRI, AddI->getOperand(2).getReg(), Addr)) ||
          (instrDefinesReg(*AddI, Bedrock::FLAGS, TRI) &&
           !regDefDeadOrDeadAfter(AddI, MBB, Bedrock::FLAGS, TRI))) {
        ++I;
        continue;
      }

      Register Index = regsOverlap(TRI, AddI->getOperand(1).getReg(), Addr)
                           ? AddI->getOperand(2).getReg()
                           : AddI->getOperand(1).getReg();
      if (!isDReg(Index)) {
        ++I;
        continue;
      }

      auto MemI = nextNonDebug(AddI, MBB);
      if (MemI == MBB.end()) {
        ++I;
        continue;
      }

      auto AfterMem = nextNonDebug(MemI, MBB);
      if (!instrDefinesReg(*MemI, Addr, TRI) &&
          !regUnusedAfterInCFG(AfterMem, MBB, Addr, TRI)) {
        ++I;
        continue;
      }

      bool Folded = false;
      switch (MemI->getOpcode()) {
      default:
        break;
      case Bedrock::MOV8rm:
      case Bedrock::MOV16rm:
      case Bedrock::MOV32rm:
      case Bedrock::MOV64rm:
        if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
            regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
          unsigned NewOpcode =
              getIndexedMemLoadOpcode(MemI->getOpcode(), 1, false);
          if (!NewOpcode)
            break;
          BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                  TII.get(NewOpcode), MemI->getOperand(0).getReg())
              .addReg(Base)
              .addReg(Index)
              .addImm(BaseOffset + MemI->getOperand(2).getImm());
          Folded = true;
        }
        break;
      case Bedrock::MOV16mr:
      case Bedrock::MOV32mr:
      case Bedrock::MOV64mr:
        if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
            regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
          unsigned NewOpcode =
              getIndexedMemStoreOpcode(MemI->getOpcode(), 1, false);
          if (!NewOpcode)
            break;
          BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                  TII.get(NewOpcode))
              .addReg(MemI->getOperand(0).getReg())
              .addReg(Base)
              .addReg(Index)
              .addImm(BaseOffset + MemI->getOperand(2).getImm());
          Folded = true;
        }
        break;
      case Bedrock::CMP8mi:
      case Bedrock::CMP16mi:
      case Bedrock::CMP32mi:
      case Bedrock::CMP64mi:
      case Bedrock::TEST8mi:
      case Bedrock::TEST16mi:
      case Bedrock::TEST32mi:
      case Bedrock::TEST64mi:
        if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isImm() &&
            MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
            regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
          unsigned NewOpcode =
              getIndexedMemImmFlagOpcode(MemI->getOpcode(), 1, false);
          if (!NewOpcode)
            break;
          BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                  TII.get(NewOpcode))
              .addImm(MemI->getOperand(0).getImm())
              .addReg(Base)
              .addReg(Index)
              .addImm(BaseOffset + MemI->getOperand(2).getImm());
          Folded = true;
        }
        break;
      case Bedrock::CMP8rm:
      case Bedrock::CMP16rm:
      case Bedrock::CMP32rm:
      case Bedrock::CMP64rm:
      case Bedrock::TEST8rm:
      case Bedrock::TEST16rm:
      case Bedrock::TEST32rm:
      case Bedrock::TEST64rm:
        if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
            regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
          bool IsCmp = MemI->getOpcode() == Bedrock::CMP8rm ||
                       MemI->getOpcode() == Bedrock::CMP16rm ||
                       MemI->getOpcode() == Bedrock::CMP32rm ||
                       MemI->getOpcode() == Bedrock::CMP64rm;
          unsigned RegFlagOpcode =
              IsCmp
                  ? (MemI->getOpcode() == Bedrock::CMP8rm    ? Bedrock::CMP8rr
                     : MemI->getOpcode() == Bedrock::CMP16rm ? Bedrock::CMP16rr
                     : MemI->getOpcode() == Bedrock::CMP32rm ? Bedrock::CMP32rr
                                                             : Bedrock::CMP64rr)
                  : (MemI->getOpcode() == Bedrock::TEST8rm ? Bedrock::TEST8rr
                     : MemI->getOpcode() == Bedrock::TEST16rm
                         ? Bedrock::TEST16rr
                     : MemI->getOpcode() == Bedrock::TEST32rm
                         ? Bedrock::TEST32rr
                         : Bedrock::TEST64rr);
          unsigned NewOpcode =
              getIndexedMemRegFlagOpcode(RegFlagOpcode, 1, false, false);
          if (!NewOpcode)
            break;
          BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                  TII.get(NewOpcode))
              .addReg(MemI->getOperand(0).getReg())
              .addReg(Base)
              .addReg(Index)
              .addImm(BaseOffset + MemI->getOperand(2).getImm());
          Folded = true;
        }
        break;
      case Bedrock::CMP8mr:
      case Bedrock::CMP16mr:
      case Bedrock::CMP32mr:
      case Bedrock::CMP64mr:
      case Bedrock::TEST8mr:
      case Bedrock::TEST16mr:
      case Bedrock::TEST32mr:
      case Bedrock::TEST64mr:
        if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
            regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
          bool IsCmp = MemI->getOpcode() == Bedrock::CMP8mr ||
                       MemI->getOpcode() == Bedrock::CMP16mr ||
                       MemI->getOpcode() == Bedrock::CMP32mr ||
                       MemI->getOpcode() == Bedrock::CMP64mr;
          unsigned RegFlagOpcode =
              IsCmp
                  ? (MemI->getOpcode() == Bedrock::CMP8mr    ? Bedrock::CMP8rr
                     : MemI->getOpcode() == Bedrock::CMP16mr ? Bedrock::CMP16rr
                     : MemI->getOpcode() == Bedrock::CMP32mr ? Bedrock::CMP32rr
                                                             : Bedrock::CMP64rr)
                  : (MemI->getOpcode() == Bedrock::TEST8mr ? Bedrock::TEST8rr
                     : MemI->getOpcode() == Bedrock::TEST16mr
                         ? Bedrock::TEST16rr
                     : MemI->getOpcode() == Bedrock::TEST32mr
                         ? Bedrock::TEST32rr
                         : Bedrock::TEST64rr);
          unsigned NewOpcode =
              getIndexedMemRegFlagOpcode(RegFlagOpcode, 1, false, true);
          if (!NewOpcode)
            break;
          BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                  TII.get(NewOpcode))
              .addReg(MemI->getOperand(0).getReg())
              .addReg(Base)
              .addReg(Index)
              .addImm(BaseOffset + MemI->getOperand(2).getImm());
          Folded = true;
        }
        break;
      case Bedrock::MOV32mm:
        if (MemI->getNumOperands() >= 4 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isImm() && MemI->getOperand(2).isReg() &&
            MemI->getOperand(3).isImm()) {
          bool SrcUsesAddr =
              regsOverlap(TRI, MemI->getOperand(0).getReg(), Addr);
          bool DstUsesAddr =
              regsOverlap(TRI, MemI->getOperand(2).getReg(), Addr);
          if (SrcUsesAddr != DstUsesAddr) {
            if (SrcUsesAddr) {
              BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                      TII.get(Bedrock::MOV32idx1mm))
                  .addReg(Base)
                  .addReg(Index)
                  .addImm(BaseOffset + MemI->getOperand(1).getImm())
                  .addReg(MemI->getOperand(2).getReg())
                  .addImm(MemI->getOperand(3).getImm());
            } else {
              BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                      TII.get(Bedrock::MOV32midx1))
                  .addReg(MemI->getOperand(0).getReg())
                  .addImm(MemI->getOperand(1).getImm())
                  .addReg(Base)
                  .addReg(Index)
                  .addImm(BaseOffset + MemI->getOperand(3).getImm());
            }
            Folded = true;
          }
        }
        break;
      case Bedrock::INC32m:
      case Bedrock::DEC32m:
        if (MemI->getNumOperands() >= 2 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isImm() &&
            regsOverlap(TRI, MemI->getOperand(0).getReg(), Addr)) {
          MachineInstrBuilder MIB =
              BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                      TII.get(MemI->getOpcode() == Bedrock::INC32m
                                  ? Bedrock::INC32idx4m
                                  : Bedrock::DEC32idx4m))
                  .addReg(Base)
                  .addReg(Index)
                  .addImm(MemI->getOperand(1).getImm());
          MIB.cloneMemRefs(*MemI);
          Folded = true;
        }
        break;
      }

      if (!Folded) {
        ++I;
        continue;
      }

      Ext.eraseFromParent();
      AddI->eraseFromParent();
      MemI->eraseFromParent();
      I = MBB.begin();
      Changed = true;
      continue;
    }

    if (Ext.getOpcode() == Bedrock::LEA4 && Ext.getNumOperands() >= 3 &&
        Ext.getOperand(0).isReg() && Ext.getOperand(1).isReg() &&
        Ext.getOperand(2).isReg()) {
      Register Addr = Ext.getOperand(0).getReg();
      Register Base = Ext.getOperand(1).getReg();
      Register Index = Ext.getOperand(2).getReg();
      auto MemI = nextNonDebug(I, MBB);
      if (MemI == MBB.end()) {
        ++I;
        continue;
      }

      auto AfterMem = nextNonDebug(MemI, MBB);
      if (!instrDefinesReg(*MemI, Addr, TRI) &&
          !regUnusedAfterInCFG(AfterMem, MBB, Addr, TRI)) {
        ++I;
        continue;
      }

      bool Folded = false;
      switch (MemI->getOpcode()) {
      default:
        break;
      case Bedrock::MOV32rm:
        if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
            regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
          BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                  TII.get(Bedrock::MOV32idx4rm), MemI->getOperand(0).getReg())
              .addReg(Base)
              .addReg(Index)
              .addImm(MemI->getOperand(2).getImm());
          Folded = true;
        }
        break;
      case Bedrock::MOV32mr:
        if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
            regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
          BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                  TII.get(Bedrock::MOV32idx4mr))
              .addReg(MemI->getOperand(0).getReg())
              .addReg(Base)
              .addReg(Index)
              .addImm(MemI->getOperand(2).getImm());
          Folded = true;
        }
        break;
      case Bedrock::MOV32mm:
        if (MemI->getNumOperands() >= 4 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isImm() && MemI->getOperand(2).isReg() &&
            MemI->getOperand(3).isImm()) {
          bool SrcUsesAddr =
              regsOverlap(TRI, MemI->getOperand(0).getReg(), Addr);
          bool DstUsesAddr =
              regsOverlap(TRI, MemI->getOperand(2).getReg(), Addr);
          if (SrcUsesAddr != DstUsesAddr && SrcUsesAddr) {
            BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                    TII.get(Bedrock::MOV32idx4mm))
                .addReg(Base)
                .addReg(Index)
                .addImm(MemI->getOperand(1).getImm())
                .addReg(MemI->getOperand(2).getReg())
                .addImm(MemI->getOperand(3).getImm());
            Folded = true;
          } else if (SrcUsesAddr != DstUsesAddr && DstUsesAddr) {
            BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                    TII.get(Bedrock::MOV32midx4))
                .addReg(MemI->getOperand(0).getReg())
                .addImm(MemI->getOperand(1).getImm())
                .addReg(Base)
                .addReg(Index)
                .addImm(MemI->getOperand(3).getImm());
            Folded = true;
          }
        }
        break;
      }

      if (!Folded) {
        ++I;
        continue;
      }

      Ext.eraseFromParent();
      MemI->eraseFromParent();
      I = MBB.begin();
      Changed = true;
      continue;
    }

    if (Ext.getOpcode() == Bedrock::LEA4L && Ext.getNumOperands() >= 3 &&
        Ext.getOperand(0).isReg() && Ext.getOperand(1).isReg() &&
        Ext.getOperand(2).isReg()) {
      Register Addr = Ext.getOperand(0).getReg();
      Register Base = Ext.getOperand(1).getReg();
      Register Index32 = Ext.getOperand(2).getReg();
      auto MemI = nextNonDebug(I, MBB);
      if (MemI == MBB.end()) {
        ++I;
        continue;
      }

      auto AfterMem = nextNonDebug(MemI, MBB);
      if (!instrDefinesReg(*MemI, Addr, TRI) &&
          !regUnusedAfterInCFG(AfterMem, MBB, Addr, TRI)) {
        ++I;
        continue;
      }

      bool Folded = false;
      switch (MemI->getOpcode()) {
      default:
        break;
      case Bedrock::MOV32rm:
        if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
            regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
          BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                  TII.get(Bedrock::MOV32idx4lrm), MemI->getOperand(0).getReg())
              .addReg(Base)
              .addReg(Index32)
              .addImm(MemI->getOperand(2).getImm());
          Folded = true;
        }
        break;
      case Bedrock::MOV32mr:
        if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
            regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
          BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                  TII.get(Bedrock::MOV32idx4lmr))
              .addReg(MemI->getOperand(0).getReg())
              .addReg(Base)
              .addReg(Index32)
              .addImm(MemI->getOperand(2).getImm());
          Folded = true;
        }
        break;
      case Bedrock::MOV32mm:
        if (MemI->getNumOperands() >= 4 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isImm() && MemI->getOperand(2).isReg() &&
            MemI->getOperand(3).isImm()) {
          bool SrcUsesAddr =
              regsOverlap(TRI, MemI->getOperand(0).getReg(), Addr);
          bool DstUsesAddr =
              regsOverlap(TRI, MemI->getOperand(2).getReg(), Addr);
          if (SrcUsesAddr != DstUsesAddr && SrcUsesAddr) {
            BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                    TII.get(Bedrock::MOV32idx4lmm))
                .addReg(Base)
                .addReg(Index32)
                .addImm(MemI->getOperand(1).getImm())
                .addReg(MemI->getOperand(2).getReg())
                .addImm(MemI->getOperand(3).getImm());
            Folded = true;
          } else if (SrcUsesAddr != DstUsesAddr && DstUsesAddr) {
            BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                    TII.get(Bedrock::MOV32midx4l))
                .addReg(MemI->getOperand(0).getReg())
                .addImm(MemI->getOperand(1).getImm())
                .addReg(Base)
                .addReg(Index32)
                .addImm(MemI->getOperand(3).getImm());
            Folded = true;
          }
        }
        break;
      case Bedrock::INC32m:
      case Bedrock::DEC32m:
        if (MemI->getNumOperands() >= 2 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isImm() &&
            regsOverlap(TRI, MemI->getOperand(0).getReg(), Addr)) {
          MachineInstrBuilder MIB =
              BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                      TII.get(MemI->getOpcode() == Bedrock::INC32m
                                  ? Bedrock::INC32idx4lm
                                  : Bedrock::DEC32idx4lm))
                  .addReg(Base)
                  .addReg(Index32)
                  .addImm(MemI->getOperand(1).getImm());
          MIB.cloneMemRefs(*MemI);
          Folded = true;
        }
        break;
      }

      if (Folded) {
        Ext.eraseFromParent();
        MemI->eraseFromParent();
        I = MBB.begin();
        Changed = true;
        continue;
      }
    }

    if ((Ext.getOpcode() == Bedrock::EXTSQ32rr ||
         Ext.getOpcode() == Bedrock::EXTZQ32rr) &&
        Ext.getNumOperands() >= 2 && Ext.getOperand(0).isReg() &&
        Ext.getOperand(1).isReg() && isDReg(Ext.getOperand(0).getReg()) &&
        isDReg(Ext.getOperand(1).getReg())) {
      Register Index64 = Ext.getOperand(0).getReg();
      Register Index32 = Ext.getOperand(1).getReg();
      auto MemI = nextNonDebug(I, MBB);
      if (MemI == MBB.end()) {
        ++I;
        continue;
      }

      auto AfterMem = nextNonDebug(MemI, MBB);
      if (!instrDefinesReg(*MemI, Index64, TRI) &&
          !regUnusedBeforeEndOrDef(AfterMem, MBB, Index64, TRI)) {
        ++I;
        continue;
      }

      bool Folded = false;
      switch (MemI->getOpcode()) {
      default:
        break;
      case Bedrock::MOV32idx4rm:
        if (MemI->getNumOperands() >= 4 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isReg() && MemI->getOperand(2).isReg() &&
            MemI->getOperand(3).isImm() &&
            regsOverlap(TRI, MemI->getOperand(2).getReg(), Index64)) {
          BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                  TII.get(Bedrock::MOV32idx4lrm), MemI->getOperand(0).getReg())
              .addReg(MemI->getOperand(1).getReg())
              .addReg(Index32,
                      getKillRegState(operandIsKill(Ext, Index32, TRI)))
              .addImm(MemI->getOperand(3).getImm());
          Folded = true;
        }
        break;
      case Bedrock::MOV32idx4mr:
        if (MemI->getNumOperands() >= 4 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isReg() && MemI->getOperand(2).isReg() &&
            MemI->getOperand(3).isImm() &&
            regsOverlap(TRI, MemI->getOperand(2).getReg(), Index64)) {
          BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                  TII.get(Bedrock::MOV32idx4lmr))
              .addReg(MemI->getOperand(0).getReg())
              .addReg(MemI->getOperand(1).getReg())
              .addReg(Index32,
                      getKillRegState(operandIsKill(Ext, Index32, TRI)))
              .addImm(MemI->getOperand(3).getImm());
          Folded = true;
        }
        break;
      case Bedrock::MOV32idx4mm:
        if (MemI->getNumOperands() >= 5 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
            MemI->getOperand(3).isReg() && MemI->getOperand(4).isImm() &&
            regsOverlap(TRI, MemI->getOperand(1).getReg(), Index64)) {
          BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                  TII.get(Bedrock::MOV32idx4lmm))
              .addReg(MemI->getOperand(0).getReg())
              .addReg(Index32,
                      getKillRegState(operandIsKill(Ext, Index32, TRI)))
              .addImm(MemI->getOperand(2).getImm())
              .addReg(MemI->getOperand(3).getReg())
              .addImm(MemI->getOperand(4).getImm());
          Folded = true;
        }
        break;
      case Bedrock::MOV32midx4:
        if (MemI->getNumOperands() >= 5 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isImm() && MemI->getOperand(2).isReg() &&
            MemI->getOperand(3).isReg() && MemI->getOperand(4).isImm() &&
            regsOverlap(TRI, MemI->getOperand(3).getReg(), Index64)) {
          BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                  TII.get(Bedrock::MOV32midx4l))
              .addReg(MemI->getOperand(0).getReg())
              .addImm(MemI->getOperand(1).getImm())
              .addReg(MemI->getOperand(2).getReg())
              .addReg(Index32,
                      getKillRegState(operandIsKill(Ext, Index32, TRI)))
              .addImm(MemI->getOperand(4).getImm());
          Folded = true;
        }
        break;
      }

      if (Folded) {
        Ext.eraseFromParent();
        MemI->eraseFromParent();
        I = MBB.begin();
        Changed = true;
        continue;
      }
    }

    if ((Ext.getOpcode() == Bedrock::EXTSQ32rr ||
         Ext.getOpcode() == Bedrock::EXTZQ32rr) &&
        Ext.getNumOperands() >= 2 && Ext.getOperand(0).isReg() &&
        Ext.getOperand(1).isReg() && isDReg(Ext.getOperand(0).getReg()) &&
        isDReg(Ext.getOperand(1).getReg())) {
      Register Index64 = Ext.getOperand(0).getReg();
      Register Index32 = Ext.getOperand(1).getReg();
      auto ShlI = nextNonDebug(I, MBB);
      if (ShlI != MBB.end() && ShlI->getOpcode() == Bedrock::SHL64ri &&
          ShlI->getNumOperands() >= 3 && ShlI->getOperand(0).isReg() &&
          ShlI->getOperand(1).isReg() && ShlI->getOperand(2).isImm() &&
          ShlI->getOperand(2).getImm() == 2 &&
          regsOverlap(TRI, ShlI->getOperand(0).getReg(), Index64) &&
          regsOverlap(TRI, ShlI->getOperand(1).getReg(), Index64) &&
          regDefDeadOrDeadAfter(ShlI, MBB, Bedrock::FLAGS, TRI)) {
        auto MemI = nextNonDebug(ShlI, MBB);
        if (MemI != MBB.end()) {
          auto AfterMem = nextNonDebug(MemI, MBB);
          if (instrDefinesReg(*MemI, Index64, TRI) ||
              regUnusedAfterInCFG(AfterMem, MBB, Index64, TRI)) {
            bool Folded = false;
            switch (MemI->getOpcode()) {
            default:
              break;
            case Bedrock::MOV32idx1rm:
              if (MemI->getNumOperands() >= 4 && MemI->getOperand(0).isReg() &&
                  MemI->getOperand(1).isReg() && MemI->getOperand(2).isReg() &&
                  MemI->getOperand(3).isImm() &&
                  regsOverlap(TRI, MemI->getOperand(2).getReg(), Index64)) {
                BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                        TII.get(Bedrock::MOV32idx4lrm),
                        MemI->getOperand(0).getReg())
                    .addReg(MemI->getOperand(1).getReg())
                    .addReg(Index32,
                            getKillRegState(operandIsKill(Ext, Index32, TRI)))
                    .addImm(MemI->getOperand(3).getImm());
                Folded = true;
              }
              break;
            case Bedrock::MOV32idx1mr:
              if (MemI->getNumOperands() >= 4 && MemI->getOperand(0).isReg() &&
                  MemI->getOperand(1).isReg() && MemI->getOperand(2).isReg() &&
                  MemI->getOperand(3).isImm() &&
                  regsOverlap(TRI, MemI->getOperand(2).getReg(), Index64)) {
                BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                        TII.get(Bedrock::MOV32idx4lmr))
                    .addReg(MemI->getOperand(0).getReg())
                    .addReg(MemI->getOperand(1).getReg())
                    .addReg(Index32,
                            getKillRegState(operandIsKill(Ext, Index32, TRI)))
                    .addImm(MemI->getOperand(3).getImm());
                Folded = true;
              }
              break;
            case Bedrock::MOV32idx1mm:
              if (MemI->getNumOperands() >= 5 && MemI->getOperand(0).isReg() &&
                  MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
                  MemI->getOperand(3).isReg() && MemI->getOperand(4).isImm() &&
                  regsOverlap(TRI, MemI->getOperand(1).getReg(), Index64)) {
                BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                        TII.get(Bedrock::MOV32idx4lmm))
                    .addReg(MemI->getOperand(0).getReg())
                    .addReg(Index32,
                            getKillRegState(operandIsKill(Ext, Index32, TRI)))
                    .addImm(MemI->getOperand(2).getImm())
                    .addReg(MemI->getOperand(3).getReg())
                    .addImm(MemI->getOperand(4).getImm());
                Folded = true;
              }
              break;
            case Bedrock::MOV32midx1:
              if (MemI->getNumOperands() >= 5 && MemI->getOperand(0).isReg() &&
                  MemI->getOperand(1).isImm() && MemI->getOperand(2).isReg() &&
                  MemI->getOperand(3).isReg() && MemI->getOperand(4).isImm() &&
                  regsOverlap(TRI, MemI->getOperand(3).getReg(), Index64)) {
                BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                        TII.get(Bedrock::MOV32midx4l))
                    .addReg(MemI->getOperand(0).getReg())
                    .addImm(MemI->getOperand(1).getImm())
                    .addReg(MemI->getOperand(2).getReg())
                    .addReg(Index32,
                            getKillRegState(operandIsKill(Ext, Index32, TRI)))
                    .addImm(MemI->getOperand(4).getImm());
                Folded = true;
              }
              break;
            }

            if (Folded) {
              Ext.eraseFromParent();
              ShlI->eraseFromParent();
              MemI->eraseFromParent();
              I = MBB.begin();
              Changed = true;
              continue;
            }
          }
        }
      }
    }

    if ((Ext.getOpcode() == Bedrock::EXTSQ32rr ||
         Ext.getOpcode() == Bedrock::EXTZQ32rr) &&
        Ext.getNumOperands() >= 2 && Ext.getOperand(0).isReg() &&
        Ext.getOperand(1).isReg() && isAReg(Ext.getOperand(0).getReg()) &&
        isDReg(Ext.getOperand(1).getReg())) {
      Register Scaled = Ext.getOperand(0).getReg();
      Register Index32 = Ext.getOperand(1).getReg();
      auto ShlI = nextNonDebug(I, MBB);
      if (ShlI == MBB.end() || ShlI->getOpcode() != Bedrock::SHL64ri ||
          ShlI->getNumOperands() < 3 || !ShlI->getOperand(0).isReg() ||
          !ShlI->getOperand(1).isReg() || !ShlI->getOperand(2).isImm() ||
          ShlI->getOperand(2).getImm() != 2 ||
          !regsOverlap(TRI, ShlI->getOperand(0).getReg(), Scaled) ||
          !regsOverlap(TRI, ShlI->getOperand(1).getReg(), Scaled) ||
          !regDefDeadOrDeadAfter(ShlI, MBB, Bedrock::FLAGS, TRI)) {
        ++I;
        continue;
      }

      auto BaseI = nextNonDebug(ShlI, MBB);
      auto AddI = BaseI == MBB.end() ? MBB.end() : nextNonDebug(BaseI, MBB);
      if (BaseI == MBB.end() || AddI == MBB.end() ||
          AddI->getOpcode() != Bedrock::ADD64rr || AddI->getNumOperands() < 3 ||
          !AddI->getOperand(0).isReg() || !AddI->getOperand(1).isReg() ||
          !AddI->getOperand(2).isReg() ||
          (instrDefinesReg(*AddI, Bedrock::FLAGS, TRI) &&
           !regDefDeadOrDeadAfter(AddI, MBB, Bedrock::FLAGS, TRI)) ||
          instrTouchesReg(*BaseI, Scaled, TRI) ||
          instrTouchesReg(*BaseI, Index32, TRI)) {
        ++I;
        continue;
      }

      Register Addr = AddI->getOperand(0).getReg();
      Register Base = Register();
      if (regsOverlap(TRI, AddI->getOperand(1).getReg(), Scaled))
        Base = AddI->getOperand(2).getReg();
      else if (regsOverlap(TRI, AddI->getOperand(2).getReg(), Scaled))
        Base = AddI->getOperand(1).getReg();

      bool ScaledDeadAfterAdd = regsOverlap(TRI, Addr, Scaled) ||
                                operandIsKill(*AddI, Scaled, TRI) ||
                                regDeadAfter(std::next(AddI), MBB, Scaled, TRI);
      if (!ScaledDeadAfterAdd) {
        Register OriginalBase = Register();
        int64_t BaseOffset = 0;
        if (BaseI->getOpcode() == Bedrock::MOV64rr &&
            BaseI->getNumOperands() >= 2 && BaseI->getOperand(0).isReg() &&
            BaseI->getOperand(1).isReg() &&
            regsOverlap(TRI, BaseI->getOperand(0).getReg(), Addr) &&
            regsOverlap(TRI, BaseI->getOperand(0).getReg(), Base)) {
          OriginalBase = BaseI->getOperand(1).getReg();
        } else if (BaseI->getOpcode() == Bedrock::LEAri &&
                   BaseI->getNumOperands() >= 3 &&
                   BaseI->getOperand(0).isReg() &&
                   BaseI->getOperand(1).isReg() &&
                   BaseI->getOperand(2).isImm() &&
                   regsOverlap(TRI, BaseI->getOperand(0).getReg(), Addr) &&
                   regsOverlap(TRI, BaseI->getOperand(0).getReg(), Base)) {
          OriginalBase = BaseI->getOperand(1).getReg();
          BaseOffset = BaseI->getOperand(2).getImm();
        }

        auto MemI = nextNonDebug(AddI, MBB);
        auto AfterMem = MemI == MBB.end() ? MBB.end() : nextNonDebug(MemI, MBB);
        if (OriginalBase.isValid() &&
            (isAReg(OriginalBase) || OriginalBase == Bedrock::SP) &&
            MemI != MBB.end() &&
            regUnusedBeforeEndOrDef(AfterMem, MBB, Addr, TRI)) {
          bool Folded = false;
          switch (MemI->getOpcode()) {
          default:
            break;
          case Bedrock::MOV32rm:
            if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
                MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
                regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
              MachineInstrBuilder MIB =
                  BuildMI(MBB, BaseI, MemI->getDebugLoc(),
                          TII.get(Bedrock::MOV32idx4lrm),
                          MemI->getOperand(0).getReg())
                      .addReg(OriginalBase)
                      .addReg(Index32)
                      .addImm(BaseOffset + MemI->getOperand(2).getImm());
              MIB.cloneMemRefs(*MemI);
              Folded = true;
            }
            break;
          case Bedrock::MOV32mr:
            if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
                MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
                regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
              MachineInstrBuilder MIB =
                  BuildMI(MBB, BaseI, MemI->getDebugLoc(),
                          TII.get(Bedrock::MOV32idx4lmr))
                      .addReg(MemI->getOperand(0).getReg())
                      .addReg(OriginalBase)
                      .addReg(Index32)
                      .addImm(BaseOffset + MemI->getOperand(2).getImm());
              MIB.cloneMemRefs(*MemI);
              Folded = true;
            }
            break;
          }

          if (Folded) {
            if (Ext.getOperand(1).isReg() &&
                regsOverlap(TRI, Ext.getOperand(1).getReg(), Index32))
              Ext.getOperand(1).setIsKill(false);
            BaseI->eraseFromParent();
            AddI->eraseFromParent();
            MemI->eraseFromParent();
            I = MBB.begin();
            Changed = true;
            continue;
          }
        }
      }

      if (!isAReg(Addr) || !isAReg(Base) || !ScaledDeadAfterAdd) {
        ++I;
        continue;
      }

      auto MemI = nextNonDebug(AddI, MBB);
      if (MemI == MBB.end()) {
        ++I;
        continue;
      }

      auto AfterMem = nextNonDebug(MemI, MBB);
      if (!regUnusedBeforeEndOrDef(AfterMem, MBB, Addr, TRI)) {
        ++I;
        continue;
      }

      bool Folded = false;
      switch (MemI->getOpcode()) {
      default:
        break;
      case Bedrock::MOV32rm:
        if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
            regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
          BuildMI(MBB, MemI->getIterator(), MemI->getDebugLoc(),
                  TII.get(Bedrock::MOV32idx4lrm), MemI->getOperand(0).getReg())
              .addReg(Base)
              .addReg(Index32,
                      getKillRegState(operandIsKill(Ext, Index32, TRI)))
              .addImm(MemI->getOperand(2).getImm());
          Folded = true;
        }
        break;
      case Bedrock::MOV32mr:
        if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
            regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
          BuildMI(MBB, MemI->getIterator(), MemI->getDebugLoc(),
                  TII.get(Bedrock::MOV32idx4lmr))
              .addReg(MemI->getOperand(0).getReg())
              .addReg(Base)
              .addReg(Index32,
                      getKillRegState(operandIsKill(Ext, Index32, TRI)))
              .addImm(MemI->getOperand(2).getImm());
          Folded = true;
        }
        break;
      }

      if (!Folded) {
        ++I;
        continue;
      }

      Ext.eraseFromParent();
      ShlI->eraseFromParent();
      AddI->eraseFromParent();
      MemI->eraseFromParent();
      I = MBB.begin();
      Changed = true;
      continue;
    }

    if ((Ext.getOpcode() != Bedrock::EXTSQ32rr &&
         Ext.getOpcode() != Bedrock::EXTZQ32rr) ||
        Ext.getNumOperands() < 2 || !Ext.getOperand(0).isReg() ||
        !Ext.getOperand(1).isReg() || !isDReg(Ext.getOperand(0).getReg()) ||
        !isDReg(Ext.getOperand(1).getReg())) {
      ++I;
      continue;
    }

    Register Index64 = Ext.getOperand(0).getReg();
    Register Index32 = Ext.getOperand(1).getReg();
    auto LeaI = nextNonDebug(I, MBB);
    if (LeaI == MBB.end() || LeaI->getOpcode() != Bedrock::LEA4 ||
        LeaI->getNumOperands() < 3 || !LeaI->getOperand(0).isReg() ||
        !LeaI->getOperand(1).isReg() || !LeaI->getOperand(2).isReg() ||
        !regsOverlap(TRI, LeaI->getOperand(2).getReg(), Index64)) {
      ++I;
      continue;
    }

    Register Addr = LeaI->getOperand(0).getReg();
    Register Base = LeaI->getOperand(1).getReg();
    auto MemI = nextNonDebug(LeaI, MBB);
    if (MemI == MBB.end()) {
      ++I;
      continue;
    }

    auto AfterMem = nextNonDebug(MemI, MBB);
    if (!instrDefinesReg(*MemI, Index64, TRI) &&
        !regUnusedBeforeEndOrDef(AfterMem, MBB, Index64, TRI)) {
      ++I;
      continue;
    }
    if (!regUnusedBeforeEndOrDef(AfterMem, MBB, Addr, TRI)) {
      ++I;
      continue;
    }

    bool Folded = false;
    switch (MemI->getOpcode()) {
    default:
      break;
    case Bedrock::MOV32rm:
      if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
          MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
          regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
        BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                TII.get(Bedrock::MOV32idx4lrm), MemI->getOperand(0).getReg())
            .addReg(Base)
            .addReg(Index32)
            .addImm(MemI->getOperand(2).getImm());
        Folded = true;
      }
      break;
    case Bedrock::MOV32mr:
      if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
          MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
          regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
        BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                TII.get(Bedrock::MOV32idx4lmr))
            .addReg(MemI->getOperand(0).getReg())
            .addReg(Base)
            .addReg(Index32)
            .addImm(MemI->getOperand(2).getImm());
        Folded = true;
      }
      break;
    case Bedrock::MOV32mm:
      if (MemI->getNumOperands() >= 4 && MemI->getOperand(0).isReg() &&
          MemI->getOperand(1).isImm() && MemI->getOperand(2).isReg() &&
          MemI->getOperand(3).isImm()) {
        bool SrcUsesAddr = regsOverlap(TRI, MemI->getOperand(0).getReg(), Addr);
        bool DstUsesAddr = regsOverlap(TRI, MemI->getOperand(2).getReg(), Addr);
        if (SrcUsesAddr != DstUsesAddr && SrcUsesAddr) {
          BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                  TII.get(Bedrock::MOV32idx4lmm))
              .addReg(Base)
              .addReg(Index32)
              .addImm(MemI->getOperand(1).getImm())
              .addReg(MemI->getOperand(2).getReg())
              .addImm(MemI->getOperand(3).getImm());
          Folded = true;
        } else if (SrcUsesAddr != DstUsesAddr && DstUsesAddr) {
          BuildMI(MBB, Ext.getIterator(), MemI->getDebugLoc(),
                  TII.get(Bedrock::MOV32midx4l))
              .addReg(MemI->getOperand(0).getReg())
              .addImm(MemI->getOperand(1).getImm())
              .addReg(Base)
              .addReg(Index32)
              .addImm(MemI->getOperand(3).getImm());
          Folded = true;
        }
      }
      break;
    }

    if (!Folded) {
      ++I;
      continue;
    }

    Ext.eraseFromParent();
    LeaI->eraseFromParent();
    MemI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldIndexedMemFromDataBase(
    MachineBasicBlock &MBB, MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &BaseLoad = *I;
    if (BaseLoad.isDebugInstr()) {
      ++I;
      continue;
    }
    if (BaseLoad.getOpcode() != Bedrock::MOV64rm ||
        BaseLoad.getNumOperands() < 3 || !BaseLoad.getOperand(0).isReg() ||
        !BaseLoad.getOperand(1).isReg() || !BaseLoad.getOperand(2).isImm() ||
        !isDReg(BaseLoad.getOperand(0).getReg())) {
      ++I;
      continue;
    }
    Register DataBase = BaseLoad.getOperand(0).getReg();

    auto IndexI = nextNonDebug(I, MBB);
    if (IndexI == MBB.end() || IndexI->getOpcode() != Bedrock::MOV32rm ||
        IndexI->getNumOperands() < 3 || !IndexI->getOperand(0).isReg() ||
        !IndexI->getOperand(1).isReg() || !IndexI->getOperand(2).isImm() ||
        !isDReg(IndexI->getOperand(0).getReg())) {
      ++I;
      continue;
    }
    Register Index = IndexI->getOperand(0).getReg();
    if (regsOverlap(TRI, DataBase, Index)) {
      ++I;
      continue;
    }

    auto ExtI = nextNonDebug(IndexI, MBB);
    if (ExtI == MBB.end() || ExtI->getOpcode() != Bedrock::EXTSQ32rr ||
        ExtI->getNumOperands() < 2 || !ExtI->getOperand(0).isReg() ||
        !ExtI->getOperand(1).isReg() || !isAReg(ExtI->getOperand(0).getReg()) ||
        !regsOverlap(TRI, ExtI->getOperand(1).getReg(), Index)) {
      ++I;
      continue;
    }
    Register Addr = ExtI->getOperand(0).getReg();

    auto ShlI = nextNonDebug(ExtI, MBB);
    if (ShlI == MBB.end() || ShlI->getOpcode() != Bedrock::SHL64ri ||
        ShlI->getNumOperands() < 3 || !ShlI->getOperand(0).isReg() ||
        !ShlI->getOperand(1).isReg() || !ShlI->getOperand(2).isImm() ||
        ShlI->getOperand(2).getImm() != 2 ||
        !regsOverlap(TRI, ShlI->getOperand(0).getReg(), Addr) ||
        !regsOverlap(TRI, ShlI->getOperand(1).getReg(), Addr)) {
      ++I;
      continue;
    }

    auto AddI = nextNonDebug(ShlI, MBB);
    if (AddI == MBB.end() || AddI->getOpcode() != Bedrock::ADD64rr ||
        AddI->getNumOperands() < 3 || !AddI->getOperand(0).isReg() ||
        !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg() ||
        !regsOverlap(TRI, AddI->getOperand(0).getReg(), Addr) ||
        !regsOverlap(TRI, AddI->getOperand(1).getReg(), Addr) ||
        !regsOverlap(TRI, AddI->getOperand(2).getReg(), DataBase)) {
      ++I;
      continue;
    }

    auto MemI = nextNonDebug(AddI, MBB);
    if (MemI == MBB.end()) {
      ++I;
      continue;
    }
    auto AfterMem = nextNonDebug(MemI, MBB);
    if (!regUnusedBeforeEndOrDef(AfterMem, MBB, Addr, TRI) ||
        !regUnusedBeforeEndOrDef(AfterMem, MBB, DataBase, TRI)) {
      ++I;
      continue;
    }

    bool Folded = false;

    switch (MemI->getOpcode()) {
    default:
      break;
    case Bedrock::MOV32rm:
      if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
          MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
          regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
        BuildMI(MBB, ExtI->getIterator(), MemI->getDebugLoc(),
                TII.get(Bedrock::MOV32idx4lrm), MemI->getOperand(0).getReg())
            .addReg(Addr)
            .addReg(Index)
            .addImm(MemI->getOperand(2).getImm());
        Folded = true;
      }
      break;
    case Bedrock::MOV32mr:
      if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
          MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
          regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
        BuildMI(MBB, ExtI->getIterator(), MemI->getDebugLoc(),
                TII.get(Bedrock::MOV32idx4lmr))
            .addReg(MemI->getOperand(0).getReg())
            .addReg(Addr)
            .addReg(Index)
            .addImm(MemI->getOperand(2).getImm());
        Folded = true;
      }
      break;
    case Bedrock::MOV32mm:
      if (MemI->getNumOperands() >= 4 && MemI->getOperand(0).isReg() &&
          MemI->getOperand(1).isImm() && MemI->getOperand(2).isReg() &&
          MemI->getOperand(3).isImm() &&
          regsOverlap(TRI, MemI->getOperand(0).getReg(), Addr)) {
        BuildMI(MBB, ExtI->getIterator(), MemI->getDebugLoc(),
                TII.get(Bedrock::MOV32idx4lmm))
            .addReg(Addr)
            .addReg(Index)
            .addImm(MemI->getOperand(1).getImm())
            .addReg(MemI->getOperand(2).getReg())
            .addImm(MemI->getOperand(3).getImm());
        Folded = true;
      }
      break;
    }

    if (!Folded) {
      ++I;
      continue;
    }

    BuildMI(MBB, BaseLoad.getIterator(), BaseLoad.getDebugLoc(),
            TII.get(Bedrock::MOV64rm), Addr)
        .addReg(BaseLoad.getOperand(1).getReg())
        .addImm(BaseLoad.getOperand(2).getImm());

    BaseLoad.eraseFromParent();
    ExtI->eraseFromParent();
    ShlI->eraseFromParent();
    AddI->eraseFromParent();
    MemI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldIndexedAddFromAbsBase(MachineBasicBlock &MBB,
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

    MachineInstr *Ext = nullptr;
    MachineInstr *Shl = &First;
    Register Index;
    Register Scaled;

    if (First.getOpcode() == Bedrock::EXTZQ32rr) {
      if (First.getNumOperands() < 2 || !First.getOperand(0).isReg() ||
          !First.getOperand(1).isReg() ||
          !regDefDeadOrDeadAfter(I, MBB, Bedrock::FLAGS, TRI)) {
        ++I;
        continue;
      }
      Ext = &First;
      Scaled = First.getOperand(0).getReg();
      Index = First.getOperand(1).getReg();
      if (!isDReg(Index)) {
        ++I;
        continue;
      }

      auto ShlI = nextNonDebug(I, MBB);
      if (ShlI == MBB.end()) {
        ++I;
        continue;
      }
      Shl = &*ShlI;
    }

    if (Shl->getOpcode() != Bedrock::SHL64ri || Shl->getNumOperands() < 3 ||
        !Shl->getOperand(0).isReg() || !Shl->getOperand(1).isReg() ||
        !Shl->getOperand(2).isImm() || Shl->getOperand(2).getImm() != 2 ||
        !regsOverlap(TRI, Shl->getOperand(0).getReg(),
                     Shl->getOperand(1).getReg()) ||
        !regDefDeadOrDeadAfter(Shl->getIterator(), MBB, Bedrock::FLAGS, TRI)) {
      ++I;
      continue;
    }

    if (Ext) {
      if (!regsOverlap(TRI, Shl->getOperand(0).getReg(), Scaled)) {
        ++I;
        continue;
      }
    } else {
      Scaled = Shl->getOperand(0).getReg();
      Index = Scaled;
      if (!isDReg(Index)) {
        ++I;
        continue;
      }
    }

    auto BaseI = nextNonDebug(Shl->getIterator(), MBB);
    if (BaseI == MBB.end() || BaseI->getOpcode() != Bedrock::MOV64abs ||
        BaseI->getNumOperands() < 2 || !BaseI->getOperand(0).isReg()) {
      ++I;
      continue;
    }
    Register Base = BaseI->getOperand(0).getReg();
    if (!isAReg(Base)) {
      ++I;
      continue;
    }

    auto AddI = nextNonDebug(BaseI, MBB);
    if (AddI == MBB.end() || AddI->getOpcode() != Bedrock::ADD64rr ||
        AddI->getNumOperands() < 3 || !AddI->getOperand(0).isReg() ||
        !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg()) {
      ++I;
      continue;
    }

    Register Addr = AddI->getOperand(0).getReg();
    if (!isAReg(Addr) ||
        !regsOverlap(TRI, AddI->getOperand(1).getReg(), Base) ||
        !regsOverlap(TRI, AddI->getOperand(2).getReg(), Scaled)) {
      ++I;
      continue;
    }

    auto MemI = nextNonDebug(AddI, MBB);
    if (MemI == MBB.end()) {
      ++I;
      continue;
    }

    if (MemI->getOpcode() == Bedrock::MOV32rm && MemI->getNumOperands() >= 3 &&
        MemI->getOperand(0).isReg() && MemI->getOperand(1).isReg() &&
        MemI->getOperand(2).isImm() && MemI->getOperand(2).getImm() == 0 &&
        regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
      Register Loaded = MemI->getOperand(0).getReg();
      auto AddValueI = nextNonDebug(MemI, MBB);
      if (AddValueI != MBB.end() &&
          AddValueI->getOpcode() == Bedrock::ADD32rr &&
          AddValueI->getNumOperands() >= 3 &&
          AddValueI->getOperand(0).isReg() &&
          AddValueI->getOperand(1).isReg() &&
          AddValueI->getOperand(2).isReg() &&
          regsOverlap(TRI, AddValueI->getOperand(0).getReg(), Loaded) &&
          regsOverlap(TRI, AddValueI->getOperand(1).getReg(), Loaded)) {
        Register Bias = AddValueI->getOperand(2).getReg();
        auto RetI = nextNonDebug(AddValueI, MBB);
        if (RetI == MBB.end() || RetI->getOpcode() != Bedrock::RET ||
            !isDReg(Loaded) || !isDReg(Bias) ||
            !regsOverlap(TRI, Index, Bedrock::D0) ||
            regsOverlap(TRI, Bias, Index) ||
            !regDeadAfterInCFG(std::next(MemI), MBB, Addr, TRI)) {
          ++I;
          continue;
        }

        auto InsertI = Ext ? Ext->getIterator() : Shl->getIterator();
        auto Load = BuildMI(MBB, InsertI, Shl->getDebugLoc(),
                            TII.get(Bedrock::MOV32idx4lrm), Index)
                        .addReg(Bedrock::PC)
                        .addReg(Index);
        Load.add(BaseI->getOperand(1));
        BuildMI(MBB, std::next(Load->getIterator()), AddValueI->getDebugLoc(),
                TII.get(Bedrock::ADD32rr), Index)
            .addReg(Index)
            .addReg(Bias);

        if (Ext)
          Ext->eraseFromParent();
        Shl->eraseFromParent();
        BaseI->eraseFromParent();
        AddI->eraseFromParent();
        MemI->eraseFromParent();
        AddValueI->eraseFromParent();
        I = MBB.begin();
        Changed = true;
        continue;
      }
    }

    if (MemI->getOpcode() == Bedrock::MOV32rm && MemI->getNumOperands() >= 3 &&
        MemI->getOperand(0).isReg() && MemI->getOperand(1).isReg() &&
        MemI->getOperand(2).isImm() && MemI->getOperand(2).getImm() == 0 &&
        regsOverlap(TRI, MemI->getOperand(1).getReg(), Addr)) {
      Register Loaded = MemI->getOperand(0).getReg();
      if (!isDReg(Loaded) || regsOverlap(TRI, Loaded, Index) ||
          !regsOverlap(TRI, Index, Bedrock::D0)) {
        ++I;
        continue;
      }

      auto BiasCopyI = nextNonDebug(MemI, MBB);
      if (BiasCopyI == MBB.end() ||
          (BiasCopyI->getOpcode() != Bedrock::MOV32rr &&
           BiasCopyI->getOpcode() != Bedrock::TRUNC64to32) ||
          BiasCopyI->getNumOperands() < 2 ||
          !BiasCopyI->getOperand(0).isReg() ||
          !BiasCopyI->getOperand(1).isReg()) {
        ++I;
        continue;
      }
      Register Sum = BiasCopyI->getOperand(0).getReg();
      Register Bias = BiasCopyI->getOperand(1).getReg();
      if (!regsOverlap(TRI, Sum, Bedrock::D0) ||
          regsOverlap(TRI, Bias, Index) || regsOverlap(TRI, Bias, Loaded)) {
        ++I;
        continue;
      }

      auto AddValueI = nextNonDebug(BiasCopyI, MBB);
      if (AddValueI == MBB.end() ||
          AddValueI->getOpcode() != Bedrock::ADD32rr ||
          AddValueI->getNumOperands() < 3 ||
          !AddValueI->getOperand(0).isReg() ||
          !AddValueI->getOperand(1).isReg() ||
          !AddValueI->getOperand(2).isReg() ||
          !regsOverlap(TRI, AddValueI->getOperand(0).getReg(), Sum) ||
          !regsOverlap(TRI, AddValueI->getOperand(1).getReg(), Sum) ||
          !regsOverlap(TRI, AddValueI->getOperand(2).getReg(), Loaded)) {
        ++I;
        continue;
      }

      auto RetI = nextNonDebug(AddValueI, MBB);
      if (RetI == MBB.end() || RetI->getOpcode() != Bedrock::RET ||
          !regDeadAfterInCFG(std::next(AddValueI), MBB, Loaded, TRI) ||
          !regDeadAfterInCFG(std::next(MemI), MBB, Addr, TRI)) {
        ++I;
        continue;
      }

      auto InsertI = Ext ? Ext->getIterator() : Shl->getIterator();
      auto Load = BuildMI(MBB, InsertI, Shl->getDebugLoc(),
                          TII.get(Bedrock::MOV32idx4lrm), Index)
                      .addReg(Bedrock::PC)
                      .addReg(Index);
      Load.add(BaseI->getOperand(1));
      BuildMI(MBB, std::next(Load->getIterator()), AddValueI->getDebugLoc(),
              TII.get(Bedrock::ADD32rr), Index)
          .addReg(Index)
          .addReg(Bias);

      if (Ext)
        Ext->eraseFromParent();
      Shl->eraseFromParent();
      BaseI->eraseFromParent();
      AddI->eraseFromParent();
      MemI->eraseFromParent();
      BiasCopyI->eraseFromParent();
      AddValueI->eraseFromParent();
      I = MBB.begin();
      Changed = true;
      continue;
    }

    if (MemI->getOpcode() != Bedrock::ADD32rm || MemI->getNumOperands() < 4 ||
        !MemI->getOperand(0).isReg() || !MemI->getOperand(1).isReg() ||
        !MemI->getOperand(2).isReg() || !MemI->getOperand(3).isImm() ||
        MemI->getOperand(3).getImm() != 0 ||
        !regsOverlap(TRI, MemI->getOperand(2).getReg(), Addr)) {
      ++I;
      continue;
    }
    Register Sum = MemI->getOperand(0).getReg();
    Register Bias = MemI->getOperand(1).getReg();
    if (!isDReg(Sum) || !isDReg(Bias) || regsOverlap(TRI, Bias, Index)) {
      ++I;
      continue;
    }

    auto ExtI = nextNonDebug(MemI, MBB);
    if (ExtI == MBB.end() || ExtI->getOpcode() != Bedrock::EXTSQ32rr ||
        ExtI->getNumOperands() < 2 || !ExtI->getOperand(0).isReg() ||
        !ExtI->getOperand(1).isReg() ||
        !regsOverlap(TRI, ExtI->getOperand(1).getReg(), Sum) ||
        !regDeadAfterInCFG(std::next(ExtI), MBB, Sum, TRI) ||
        !regDeadAfterInCFG(std::next(MemI), MBB, Addr, TRI)) {
      ++I;
      continue;
    }

    auto InsertI = Ext ? Ext->getIterator() : Shl->getIterator();
    auto Load = BuildMI(MBB, InsertI, Shl->getDebugLoc(),
                        TII.get(Bedrock::MOV32idx4lrm), Index)
                    .addReg(Bedrock::PC)
                    .addReg(Index);
    Load.add(BaseI->getOperand(1));
    BuildMI(MBB, std::next(Load->getIterator()), MemI->getDebugLoc(),
            TII.get(Bedrock::ADD32rr), Index)
        .addReg(Index)
        .addReg(Bias);

    ExtI->getOperand(1).setReg(Index);
    if (Ext)
      Ext->eraseFromParent();
    Shl->eraseFromParent();
    BaseI->eraseFromParent();
    AddI->eraseFromParent();
    MemI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

