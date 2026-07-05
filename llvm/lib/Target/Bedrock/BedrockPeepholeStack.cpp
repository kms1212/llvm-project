//===-- BedrockPeepholeStack.cpp - Bedrock peepholes -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockPeephole.h"

bool BedrockPeephole::foldEntryLiveInStores(MachineFunction &MF) const {
  if (MF.empty())
    return false;

  MachineBasicBlock &MBB = MF.front();
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  DenseMap<Register, EntryTrackedValue> RegValues;
  for (const MachineBasicBlock::RegisterMaskPair &LiveIn : MBB.liveins()) {
    Register Reg = LiveIn.PhysReg;
    if (!isDReg(Reg) && !isAReg(Reg))
      continue;
    EntryTrackedValue Value;
    Value.Origin = Reg;
    Value.Width = 64;
    RegValues[Reg] = Value;
  }
  if (RegValues.empty())
    return false;

  MachineBasicBlock::iterator InsertPt = MBB.begin();
  while (InsertPt != MBB.end() && InsertPt->isDebugInstr())
    ++InsertPt;
  if (InsertPt == MBB.end())
    return false;

  int64_t StackAdjust = 0;
  if (isStackAdjust(*InsertPt, Bedrock::SUB64ri, StackAdjust))
    InsertPt = nextNonDebug(InsertPt, MBB);

  DenseMap<int64_t, EntryTrackedValue> StackValues;
  SmallVector<EntryStoreRewrite, 8> Rewrites;
  auto Scan = InsertPt;
  for (; Scan != MBB.end(); Scan = nextNonDebug(Scan, MBB)) {
    MachineInstr &MI = *Scan;
    if (MI.isDebugInstr())
      continue;
    if (MI.mayLoadOrStore() && hasOrderedMemOperand(MI))
      break;

    Register Reg;
    Register Base;
    int64_t Offset = 0;
    if (isMemStore(MI, Reg, Base, Offset) && Base == Bedrock::SP) {
      auto RegIt = RegValues.find(Reg);
      if (RegIt == RegValues.end())
        break;

      unsigned StoreWidth = memSizeForOpcode(MI.getOpcode()) * 8;
      if (RegIt->second.Width < StoreWidth)
        break;

      EntryTrackedValue Stored = RegIt->second;
      Stored.Width = StoreWidth;
      if (!MI.memoperands_empty() &&
          (Stored.Origin != Reg || !Stored.Defs.empty()) &&
          isStoreSrcCompatible(MI.getOpcode(), Stored.Origin)) {
        EntryStoreRewrite Rewrite;
        Rewrite.Store = &MI;
        Rewrite.Opcode = MI.getOpcode();
        Rewrite.Origin = Stored.Origin;
        Rewrite.Offset = Offset;
        Rewrite.Value = Stored;
        Rewrites.push_back(std::move(Rewrite));
      }

      Stored.Defs.push_back(&MI);
      StackValues[Offset] = std::move(Stored);
      continue;
    }

    if (isMemLoad(MI, Reg, Base, Offset) && Base == Bedrock::SP) {
      auto StackIt = StackValues.find(Offset);
      if (StackIt == StackValues.end())
        break;

      EntryTrackedValue Loaded = StackIt->second;
      unsigned LoadWidth = memSizeForOpcode(MI.getOpcode()) * 8;
      Loaded.Width = std::min(Loaded.Width, LoadWidth);
      Loaded.Defs.push_back(&MI);
      RegValues[Reg] = std::move(Loaded);
      continue;
    }

    Register Dst;
    Register Src;
    unsigned MoveWidth = 0;
    if (isTrackableRegMove(MI, Dst, Src, MoveWidth)) {
      auto RegIt = RegValues.find(Src);
      if (RegIt == RegValues.end())
        break;

      EntryTrackedValue Moved = RegIt->second;
      Moved.Width = std::min(Moved.Width, MoveWidth);
      Moved.Defs.push_back(&MI);
      RegValues[Dst] = std::move(Moved);
      continue;
    }

    break;
  }

  if (Rewrites.empty())
    return false;

  SmallPtrSet<MachineInstr *, 16> EraseSet;
  SmallPtrSet<MachineInstr *, 8> ReplacedStores;
  for (const EntryStoreRewrite &Rewrite : Rewrites) {
    EraseSet.insert(Rewrite.Store);
    ReplacedStores.insert(Rewrite.Store);
    for (MachineInstr *Def : Rewrite.Value.Defs)
      EraseSet.insert(Def);
  }

  SmallVector<int64_t, 4> RemovedTempOffsets;
  for (MachineInstr *MI : EraseSet) {
    Register Src;
    Register Base;
    int64_t Offset = 0;
    if (ReplacedStores.contains(MI))
      continue;
    if (isMemStore(*MI, Src, Base, Offset) && Base == Bedrock::SP)
      RemovedTempOffsets.push_back(Offset);
  }
  for (int64_t Offset : RemovedTempOffsets)
    if (stackOffsetUsedOutsideSet(MF, Offset, EraseSet, TRI))
      return false;

  for (const EntryStoreRewrite &Rewrite : Rewrites) {
    BuildMI(MBB, InsertPt, Rewrite.Store->getDebugLoc(),
            TII.get(Rewrite.Opcode))
        .addReg(Rewrite.Origin)
        .addReg(Bedrock::SP)
        .addImm(Rewrite.Offset)
        .cloneMemRefs(*Rewrite.Store);
  }

  for (MachineInstr *MI : EraseSet)
    MI->eraseFromParent();
  return true;
}

bool BedrockPeephole::foldStackStoreLoadForward(MachineBasicBlock &MBB,
                                                    MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Store = *I;
    if (Store.isDebugInstr()) {
      ++I;
      continue;
    }

    Register Src;
    Register StoreBase;
    int64_t StoreOffset = 0;
    if (!isMemStore(Store, Src, StoreBase, StoreOffset) ||
        StoreBase != Bedrock::SP || hasOrderedMemOperand(Store)) {
      ++I;
      continue;
    }
    if (isCalleeSaveSpill(MF, Store)) {
      ++I;
      continue;
    }

    auto LoadI = nextNonDebug(I, MBB);
    if (LoadI == MBB.end()) {
      ++I;
      continue;
    }

    Register Dst;
    Register LoadBase;
    int64_t LoadOffset = 0;
    if (!isMemLoad(*LoadI, Dst, LoadBase, LoadOffset) ||
        LoadBase != Bedrock::SP || LoadOffset != StoreOffset ||
        getStoreOpcodeForLoad(LoadI->getOpcode()) != Store.getOpcode() ||
        hasOrderedMemOperand(*LoadI)) {
      ++I;
      continue;
    }
    if (isCalleeSaveRestore(MF, *LoadI)) {
      ++I;
      continue;
    }

    unsigned MoveOpcode = getRegMoveOpcodeForLoad(LoadI->getOpcode(), Dst, Src);
    if (!regsOverlap(TRI, Dst, Src) && MoveOpcode == 0) {
      ++I;
      continue;
    }

    Store.getOperand(0).setIsKill(false);
    if (!regsOverlap(TRI, Dst, Src)) {
      BuildMI(MBB, LoadI, LoadI->getDebugLoc(), TII.get(MoveOpcode), Dst)
          .addReg(Src);
    }
    LoadI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldStackPointerCopyMemBase(
    MachineBasicBlock &MBB, MachineFunction &MF) const {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Copy = *I;
    ++I;
    if (Copy.isDebugInstr() || Copy.getOpcode() != Bedrock::MOV64rr ||
        Copy.getNumOperands() < 2 || !Copy.getOperand(0).isReg() ||
        !Copy.getOperand(1).isReg() ||
        Copy.getOperand(1).getReg() != Bedrock::SP ||
        !isAReg(Copy.getOperand(0).getReg()))
      continue;

    Register Alias = Copy.getOperand(0).getReg();
    SmallVector<MachineInstr *, 4> MemUsers;
    auto Scan = nextNonDebug(Copy.getIterator(), MBB);
    for (; Scan != MBB.end(); Scan = nextNonDebug(Scan, MBB)) {
      if (onlyUsesRegAsMemoryBase(*Scan, Alias, TRI)) {
        MemUsers.push_back(&*Scan);
        continue;
      }
      break;
    }

    bool SafeToDropCopy = regDeadAfter(Scan, MBB, Alias, TRI);
    if (!SafeToDropCopy && Scan != MBB.end() && Scan->isCall() &&
        !instrUsesReg(*Scan, Alias, TRI))
      SafeToDropCopy = true;
    if (MemUsers.empty() || !SafeToDropCopy)
      continue;

    for (MachineInstr *MI : MemUsers)
      replaceMemoryBase(*MI, Alias, Bedrock::SP, TRI);
    Copy.eraseFromParent();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldA6BaseCopyStackSpill(MachineFunction &MF) const {
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  MachineInstr *Save = nullptr;
  MachineInstr *Copy = nullptr;
  MachineInstr *Call = nullptr;
  MachineInstr *Restore = nullptr;
  int64_t SlotOffset = 0;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;

      Register Reg;
      Register Base;
      int64_t Offset = 0;
      if (isMemStore(MI, Reg, Base, Offset) && Base == Bedrock::SP &&
          Reg == Bedrock::A6) {
        if (Save)
          return false;
        Save = &MI;
        SlotOffset = Offset;
        continue;
      }

      if (MI.getOpcode() == Bedrock::MOV64rr && MI.getNumOperands() >= 2 &&
          MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
          MI.getOperand(0).getReg() == Bedrock::A6 &&
          MI.getOperand(1).getReg() == Bedrock::A0) {
        if (Copy)
          return false;
        Copy = &MI;
        continue;
      }

      if (MI.isCall()) {
        if (Call)
          return false;
        Call = &MI;
        continue;
      }

      if (isMemLoad(MI, Reg, Base, Offset) && Base == Bedrock::SP &&
          Reg == Bedrock::A6 && Save && Offset == SlotOffset) {
        if (Restore)
          return false;
        Restore = &MI;
      }
    }
  }

  if (!Save || !Copy || !Call || !Restore)
    return false;

  bool PastCopy = false;
  bool PastCall = false;
  bool InsertedReload = false;
  SmallVector<MachineInstr *, 8> Uses;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      if (&MI == Copy) {
        PastCopy = true;
        continue;
      }
      if (&MI == Save || &MI == Restore)
        continue;
      if (!PastCopy)
        continue;
      if (&MI == Call) {
        PastCall = true;
        continue;
      }

      if (!PastCall) {
        if (instrDefinesReg(MI, Bedrock::A0, TRI) ||
            instrHasRegMaskForReg(MI, Bedrock::A0, TRI))
          return false;
      } else if (!InsertedReload && instrTouchesReg(MI, Bedrock::A0, TRI)) {
        return false;
      }

      if (!instrTouchesReg(MI, Bedrock::A6, TRI))
        continue;
      if (!onlyUsesRegAsAnyMemoryBase(MI, Bedrock::A6, TRI))
        return false;
      if (instrTouchesReg(MI, Bedrock::A0, TRI))
        return false;

      if (PastCall && !InsertedReload) {
        BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(),
                TII.get(Bedrock::MOV64rm), Bedrock::A0)
            .addReg(Bedrock::SP)
            .addImm(SlotOffset);
        InsertedReload = true;
      }
      Uses.push_back(&MI);
    }
  }

  if (Uses.empty())
    return false;

  Save->getOperand(0).setReg(Bedrock::A0);
  Save->getOperand(0).setIsKill(false);
  for (MachineInstr *MI : Uses)
    replaceAnyMemoryBase(*MI, Bedrock::A6, Bedrock::A0, TRI);
  Copy->eraseFromParent();
  Restore->eraseFromParent();
  for (MachineBasicBlock &MBB : MF) {
    if (!MBB.isLiveIn(Bedrock::A6))
      continue;
    MBB.removeLiveIn(Bedrock::A6);
    if (!MBB.isLiveIn(Bedrock::A0))
      MBB.addLiveIn(Bedrock::A0);
  }
  MF.getRegInfo().clearKillFlags(Bedrock::A0);
  removeRegLiveInsWithoutUses(MF, Bedrock::A6, TRI);
  return true;
}

bool BedrockPeephole::foldStackSlotsToARegs(MachineFunction &MF) const {
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB)
      if (MI.isCall())
        return false;

  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  uint32_t UsedARegs = 0;
  for (MachineBasicBlock &MBB : MF) {
    for (const MachineBasicBlock::RegisterMaskPair &LiveIn : MBB.liveins()) {
      Register Reg(LiveIn.PhysReg);
      if (isAReg(Reg))
        UsedARegs |= 1u << (Reg - Bedrock::A0);
    }
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isReg())
          continue;
        Register Reg = MO.getReg();
        if (isAReg(Reg))
          UsedARegs |= 1u << (Reg - Bedrock::A0);
      }
    }
  }

  SmallVector<Register, 5> FreeARegs;
  for (Register Reg = Bedrock::A1; Reg <= Bedrock::A5; Reg = Register(Reg + 1))
    if ((UsedARegs & (1u << (Reg - Bedrock::A0))) == 0)
      FreeARegs.push_back(Reg);

  MachineInstr *PushM = nullptr;
  SmallVector<MachineInstr *, 4> PopMs;
  uint16_t PushPopMask = 0;
  bool CanBorrowCalleeSavedARegs =
      collectConsistentPushPopMask(MF, PushM, PopMs, PushPopMask);
  if (CanBorrowCalleeSavedARegs) {
    for (Register Reg = Bedrock::A6; Reg <= Bedrock::A7;
         Reg = Register(Reg + 1)) {
      unsigned Bit = *getMaskBit(Reg);
      if ((PushPopMask & (uint16_t(1) << Bit)) == 0 &&
          (UsedARegs & (1u << (Reg - Bedrock::A0))) == 0)
        FreeARegs.push_back(Reg);
    }
  }

  if (FreeARegs.empty())
    return false;

  DenseMap<int64_t, SmallVector<MachineInstr *, 8>> AccessesByOffset;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      int64_t Offset = 0;
      if (isPromotableStackSlotAccess(MI, Offset))
        AccessesByOffset[Offset].push_back(&MI);
    }
  }

  SmallVector<int64_t, 8> Offsets;
  for (const auto &It : AccessesByOffset)
    Offsets.push_back(It.first);
  llvm::sort(Offsets, [&](int64_t A, int64_t B) {
    unsigned ASavings = estimateStackSlotPromotionSavings(AccessesByOffset[A]);
    unsigned BSavings = estimateStackSlotPromotionSavings(AccessesByOffset[B]);
    if (ASavings != BSavings)
      return ASavings > BSavings;
    return AccessesByOffset[A].size() > AccessesByOffset[B].size();
  });

  bool Changed = false;
  unsigned NextA = 0;
  uint16_t BorrowedCalleeSavedMask = 0;
  for (int64_t Offset : Offsets) {
    if (NextA >= FreeARegs.size())
      break;

    SmallVector<MachineInstr *, 8> &Accesses = AccessesByOffset[Offset];
    bool CanPromote = !Accesses.empty() &&
                      canPromoteStackSlotToAReg(MF, Offset, Accesses, TRI);
    if (!CanPromote)
      continue;

    Register AReg = FreeARegs[NextA++];
    if (AReg == Bedrock::A6 || AReg == Bedrock::A7)
      BorrowedCalleeSavedMask |= uint16_t(1) << *getMaskBit(AReg);
    for (MachineBasicBlock &MBB : MF)
      if (&MBB != &MF.front() && !MBB.isLiveIn(AReg))
        MBB.addLiveIn(AReg);

    for (MachineInstr *MI : Accesses) {
      MachineBasicBlock &MBB = *MI->getParent();
      DebugLoc DL = MI->getDebugLoc();
      switch (MI->getOpcode()) {
      default:
        llvm_unreachable("unexpected promoted stack-slot access");
      case Bedrock::MOV32rm: {
        Register Dst = MI->getOperand(0).getReg();
        BuildMI(MBB, MI->getIterator(), DL, TII.get(Bedrock::MOV64rr), Dst)
            .addReg(AReg);
        break;
      }
      case Bedrock::MOV32mr: {
        Register Src = MI->getOperand(0).getReg();
        BuildMI(MBB, MI->getIterator(), DL, TII.get(Bedrock::MOV64rr), AReg)
            .addReg(Src);
        break;
      }
      case Bedrock::INC32m:
        BuildMI(MBB, MI->getIterator(), DL, TII.get(Bedrock::INC64r), AReg)
            .addReg(AReg);
        break;
      case Bedrock::DEC32m:
        BuildMI(MBB, MI->getIterator(), DL, TII.get(Bedrock::DEC64r), AReg)
            .addReg(AReg);
        break;
      case Bedrock::ADD32rm: {
        Register Dst = MI->getOperand(0).getReg();
        Register LHS = MI->getOperand(1).getReg();
        Register Scratch =
            findScratchDRegAt(MI->getIterator(), MBB, TRI, Dst, LHS);
        assert(Scratch && "validated promoted stack-slot scratch");
        BuildMI(MBB, MI->getIterator(), DL, TII.get(Bedrock::MOV64rr), Scratch)
            .addReg(AReg);
        BuildMI(MBB, MI->getIterator(), DL, TII.get(Bedrock::ADD32rr), Dst)
            .addReg(LHS)
            .addReg(Scratch);
        break;
      }
      }
      MI->eraseFromParent();
      Changed = true;
    }
  }

  if (BorrowedCalleeSavedMask != 0) {
    assert(PushM && "borrowed callee-saved A reg without PUSHM");
    extendPushPopMask(MF, *PushM, PopMs, PushPopMask, BorrowedCalleeSavedMask);
    for (unsigned Bit = 0; Bit != 16; ++Bit) {
      if ((BorrowedCalleeSavedMask & (uint16_t(1) << Bit)) == 0)
        continue;
      Register Reg = getRegForMaskBit(Bit);
      if (!MF.front().isLiveIn(Reg))
        MF.front().addLiveIn(Reg);
    }
  }

  return Changed;
}

bool BedrockPeephole::foldStackReloadFromZextCount(
    MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &LoadMBB : MF) {
    for (auto LoadI = LoadMBB.begin(); LoadI != LoadMBB.end(); ++LoadI) {
      MachineInstr &Load = *LoadI;
      if (Load.isDebugInstr() || Load.getOpcode() != Bedrock::MOV32rm ||
          hasOrderedMemOperand(Load))
        continue;

      Register Tmp;
      Register Base;
      int64_t Offset = 0;
      if (!isMemLoad(Load, Tmp, Base, Offset) || Base != Bedrock::SP ||
          !isDReg(Tmp))
        continue;

      auto ExtI = nextNonDebug(Load.getIterator(), LoadMBB);
      if (ExtI == LoadMBB.end() || ExtI->getOpcode() != Bedrock::EXTZQ32rr ||
          ExtI->getNumOperands() < 2 || !ExtI->getOperand(0).isReg() ||
          !ExtI->getOperand(1).isReg() ||
          !regsOverlap(TRI, ExtI->getOperand(1).getReg(), Tmp))
        continue;

      Register Count = ExtI->getOperand(0).getReg();
      if (!isDReg(Count))
        continue;

      SmallVector<MachineInstr *, 4> Reloads;
      for (auto ScanI = std::next(ExtI); ScanI != LoadMBB.end(); ++ScanI) {
        MachineInstr &MI = *ScanI;
        if (MI.isDebugInstr())
          continue;
        if (instrHasRegMaskForReg(MI, Count, TRI))
          break;
        if (instrDefinesReg(MI, Count, TRI))
          break;

        Register Dst;
        Register ReloadBase;
        int64_t ReloadOffset = 0;
        if (MI.getOpcode() == Bedrock::MOV32rm && !hasOrderedMemOperand(MI) &&
            isMemLoad(MI, Dst, ReloadBase, ReloadOffset) &&
            ReloadBase == Bedrock::SP && ReloadOffset == Offset &&
            isDReg(Dst)) {
          Reloads.push_back(&MI);
          continue;
        }

        if (instrHasOverlappingSPMemRange(MI, Offset, 4, TRI))
          break;
      }

      if (Reloads.empty() && LoadMBB.succ_size() == 1) {
        bool SafeToSucc = true;
        for (auto ScanI = std::next(ExtI); ScanI != LoadMBB.end(); ++ScanI) {
          MachineInstr &MI = *ScanI;
          if (MI.isDebugInstr())
            continue;
          if (instrHasRegMaskForReg(MI, Count, TRI) ||
              instrDefinesReg(MI, Count, TRI) ||
              instrHasOverlappingSPMemRange(MI, Offset, 4, TRI)) {
            SafeToSucc = false;
            break;
          }
        }

        MachineBasicBlock *Succ = *LoadMBB.succ_begin();
        if (SafeToSucc && Succ->isLiveIn(Count)) {
          for (auto ScanI = Succ->begin(); ScanI != Succ->end(); ++ScanI) {
            MachineInstr &MI = *ScanI;
            if (MI.isDebugInstr())
              continue;
            if (instrHasRegMaskForReg(MI, Count, TRI))
              break;
            if (instrDefinesReg(MI, Count, TRI))
              break;

            Register Dst;
            Register ReloadBase;
            int64_t ReloadOffset = 0;
            if (MI.getOpcode() == Bedrock::MOV32rm &&
                !hasOrderedMemOperand(MI) &&
                isMemLoad(MI, Dst, ReloadBase, ReloadOffset) &&
                ReloadBase == Bedrock::SP && ReloadOffset == Offset &&
                isDReg(Dst)) {
              Reloads.push_back(&MI);
              continue;
            }

            if (instrHasOverlappingSPMemRange(MI, Offset, 4, TRI))
              break;
          }
        }
      }

      if (Reloads.empty())
        continue;

      for (MachineInstr *Reload : Reloads) {
        MachineBasicBlock &MBB = *Reload->getParent();
        Register Dst = Reload->getOperand(0).getReg();
        if (regsOverlap(TRI, Dst, Count)) {
          Reload->eraseFromParent();
        } else {
          BuildMI(MBB, Reload->getIterator(), Reload->getDebugLoc(),
                  TII.get(Bedrock::MOV32rr), Dst)
              .addReg(Count);
          Reload->eraseFromParent();
        }
      }
      Changed = true;
      break;
    }
    if (Changed)
      break;
  }

  return Changed;
}

bool BedrockPeephole::foldStackConstLoads(MachineFunction &MF) const {
  SmallVector<StackConstStore, 8> Slots;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  collectStackConstStores(MF, TRI, Slots);

  bool Changed = false;
  for (StackConstStore &Slot : Slots) {
    if (!Slot.Store || (Slot.Size != 4 && Slot.Size != 8))
      continue;

    SmallVector<MachineInstr *, 4> Loads;
    SmallPtrSet<MachineInstr *, 8> Ignored;
    Ignored.insert(Slot.ImmDef);
    Ignored.insert(Slot.Store);

    bool Invalid = false;
    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : MBB) {
        if (MI.isDebugInstr() || Ignored.contains(&MI))
          continue;
        if (!instrHasOverlappingSPMemRange(MI, Slot.Offset, Slot.Size, TRI))
          continue;
        unsigned ExpectedLoadOpcode =
            Slot.Size == 8 ? Bedrock::MOV64rm : Bedrock::MOV32rm;
        if (MI.getOpcode() != ExpectedLoadOpcode || hasOrderedMemOperand(MI) ||
            !stackConstStoreAvailableAtUse(Slot, MI)) {
          Invalid = true;
          break;
        }
        Register Dst;
        Register Base;
        int64_t Offset = 0;
        if (!isMemLoad(MI, Dst, Base, Offset) || Base != Bedrock::SP ||
            Offset != Slot.Offset) {
          Invalid = true;
          break;
        }
        Ignored.insert(&MI);
        Loads.push_back(&MI);
      }
      if (Invalid)
        break;
    }

    if (Invalid || Loads.empty() ||
        stackRangeUsedOutsideSet(MF, Slot.Offset, Slot.Size, Ignored, TRI))
      continue;

    for (MachineInstr *Load : Loads) {
      MachineBasicBlock &MBB = *Load->getParent();
      Register Dst = Load->getOperand(0).getReg();
      if (Slot.Value == 0 && isIntReg(Dst)) {
        BuildMI(MBB, Load->getIterator(), Load->getDebugLoc(),
                TII.get(Bedrock::CLR64r), Dst);
      } else if (Slot.Size == 4) {
        BuildMI(MBB, Load->getIterator(), Load->getDebugLoc(),
                TII.get(Bedrock::MOV32ri), Dst)
            .addImm(Slot.Value);
      } else {
        BuildMI(MBB, Load->getIterator(), Load->getDebugLoc(),
                TII.get(Bedrock::MOV64ri), Dst)
            .addImm(Slot.Value);
      }
      Load->eraseFromParent();
    }

    bool ConstDefDead = operandIsKill(*Slot.Store, Slot.Reg, TRI) ||
                        regDeadAfter(std::next(Slot.Store->getIterator()),
                                     *Slot.MBB, Slot.Reg, TRI);
    Slot.Store->eraseFromParent();
    if (ConstDefDead)
      Slot.ImmDef->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldStackZeroCmp(MachineFunction &MF) const {
  SmallVector<StackConstStore, 8> Slots;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  collectStackConstStores(MF, TRI, Slots);

  bool Changed = false;
  for (StackConstStore &Slot : Slots) {
    if (!Slot.Store || Slot.Value != 0 || (Slot.Size != 4 && Slot.Size != 8))
      continue;

    struct Replacement {
      MachineInstr *Insert = nullptr;
      MachineInstr *Load = nullptr;
      MachineInstr *Cmp = nullptr;
      Register Reg;
      unsigned TestOpcode = 0;
    };
    SmallVector<Replacement, 4> Replacements;
    SmallPtrSet<MachineInstr *, 8> Ignored;
    Ignored.insert(Slot.ImmDef);
    Ignored.insert(Slot.Store);

    bool Invalid = false;
    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : MBB) {
        if (MI.isDebugInstr() || Ignored.contains(&MI))
          continue;
        if (!instrHasOverlappingSPMemRange(MI, Slot.Offset, Slot.Size, TRI))
          continue;

        Register Reg;
        unsigned TestOpcode = 0;
        MachineInstr *Cmp = nullptr;
        MachineInstr *Load = nullptr;
        MachineInstr *Insert = nullptr;
        if (isZeroStackCmpUse(MI, Slot, Reg, TestOpcode)) {
          Cmp = &MI;
          Insert = &MI;
        } else if (isZeroStackLoadCmpUse(MI, Slot, Reg, TestOpcode, Cmp, TRI)) {
          Load = &MI;
          Insert = &MI;
        }

        if (!Cmp || !Insert || !stackConstStoreAvailableAtUse(Slot, *Insert) ||
            !hasEqNeBranchBeforeFlagsClobber(*Cmp, TRI)) {
          Invalid = true;
          break;
        }

        Ignored.insert(&MI);
        if (Load)
          Ignored.insert(Cmp);
        Replacements.push_back({Insert, Load, Cmp, Reg, TestOpcode});
      }
      if (Invalid)
        break;
    }

    if (Invalid || Replacements.empty() ||
        stackRangeUsedOutsideSet(MF, Slot.Offset, Slot.Size, Ignored, TRI))
      continue;

    for (const Replacement &R : Replacements) {
      MachineBasicBlock &MBB = *R.Insert->getParent();
      BuildMI(MBB, R.Insert->getIterator(), R.Cmp->getDebugLoc(),
              TII.get(R.TestOpcode))
          .addReg(R.Reg)
          .addReg(R.Reg);
      if (R.Load)
        R.Load->eraseFromParent();
      R.Cmp->eraseFromParent();
    }

    bool ConstDefDead = operandIsKill(*Slot.Store, Slot.Reg, TRI) ||
                        regDeadAfter(std::next(Slot.Store->getIterator()),
                                     *Slot.MBB, Slot.Reg, TRI);
    Slot.Store->eraseFromParent();
    if (ConstDefDead)
      Slot.ImmDef->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldStackBaseLeaOffsets(MachineBasicBlock &MBB,
                                                  MachineFunction &MF) const {
  struct MemRewrite {
    MachineInstr *MI;
    unsigned BaseOp;
    int64_t Offset;
  };
  struct InstrReplace {
    MachineInstr *First;
    MachineInstr *Second;
    Register Dst;
    int64_t Offset;
  };

  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &BaseLea = *I;
    if (BaseLea.isDebugInstr()) {
      ++I;
      continue;
    }
    if (BaseLea.getOpcode() != Bedrock::LEAri || BaseLea.getNumOperands() < 3 ||
        !BaseLea.getOperand(0).isReg() || !BaseLea.getOperand(1).isReg() ||
        !BaseLea.getOperand(2).isImm() ||
        BaseLea.getOperand(1).getReg() != Bedrock::SP) {
      ++I;
      continue;
    }

    Register BaseReg = BaseLea.getOperand(0).getReg();
    int64_t BaseOffset = BaseLea.getOperand(2).getImm();
    if (!isAReg(BaseReg) || !fitsDisp16(BaseOffset)) {
      ++I;
      continue;
    }

    SmallVector<MemRewrite, 8> MemRewrites;
    SmallVector<InstrReplace, 4> Replaces;
    bool SawUse = false;
    bool Failed = false;
    bool StoppedAtOverwrite = false;
    int SizeDelta = -6;

    for (auto Scan = nextNonDebug(I, MBB); Scan != MBB.end();
         Scan = nextNonDebug(Scan, MBB)) {
      if (instrHasRegMaskForReg(*Scan, BaseReg, TRI)) {
        Failed = true;
        break;
      }

      if (Scan->getOpcode() == Bedrock::ADD64ri &&
          Scan->getNumOperands() >= 3 && Scan->getOperand(0).isReg() &&
          Scan->getOperand(1).isReg() && Scan->getOperand(2).isImm() &&
          regsOverlap(TRI, Scan->getOperand(0).getReg(), BaseReg) &&
          regsOverlap(TRI, Scan->getOperand(1).getReg(), BaseReg) &&
          (!instrDefinesReg(*Scan, Bedrock::FLAGS, TRI) ||
           regDefDeadOrDeadAfter(Scan, MBB, Bedrock::FLAGS, TRI))) {
        auto CopyI = nextNonDebug(Scan, MBB);
        if (CopyI != MBB.end() && CopyI->getOpcode() == Bedrock::MOV64rr &&
            CopyI->getNumOperands() >= 2 && CopyI->getOperand(0).isReg() &&
            CopyI->getOperand(1).isReg() &&
            isAReg(CopyI->getOperand(0).getReg()) &&
            !regsOverlap(TRI, CopyI->getOperand(0).getReg(), BaseReg) &&
            regsOverlap(TRI, CopyI->getOperand(1).getReg(), BaseReg) &&
            regDeadAfterInCFG(std::next(CopyI), MBB, BaseReg, TRI)) {
          int64_t Combined = BaseOffset + Scan->getOperand(2).getImm();
          if (!fitsDisp16(Combined)) {
            Failed = true;
            break;
          }
          Replaces.push_back(
              {&*Scan, &*CopyI, CopyI->getOperand(0).getReg(), Combined});
          SizeDelta -= 4;
          SawUse = true;
          StoppedAtOverwrite = true;
          break;
        }
      }

      if (instrDefinesReg(*Scan, BaseReg, TRI)) {
        if (instrUsesReg(*Scan, BaseReg, TRI))
          Failed = true;
        else
          StoppedAtOverwrite = true;
        break;
      }

      if (!instrUsesReg(*Scan, BaseReg, TRI))
        continue;

      bool RewroteUse = false;
      bool UnsupportedUse = false;
      if (Scan->getOpcode() == Bedrock::MOV64rr &&
          Scan->getNumOperands() >= 2 && Scan->getOperand(0).isReg() &&
          Scan->getOperand(1).isReg() && isAReg(Scan->getOperand(0).getReg()) &&
          !regsOverlap(TRI, Scan->getOperand(0).getReg(), BaseReg) &&
          regsOverlap(TRI, Scan->getOperand(1).getReg(), BaseReg)) {
        Replaces.push_back(
            {&*Scan, nullptr, Scan->getOperand(0).getReg(), BaseOffset});
        SizeDelta += 2;
        RewroteUse = true;
      } else {
        for (unsigned OpNo = 0, E = Scan->getNumOperands(); OpNo != E; ++OpNo) {
          MachineOperand &MO = Scan->getOperand(OpNo);
          if (!operandTouchesReg(MO, BaseReg, TRI) || !MO.readsReg())
            continue;
          if (!isMemoryBaseOperand(*Scan, OpNo, BaseReg, TRI)) {
            UnsupportedUse = true;
            break;
          }
          int64_t Combined = BaseOffset + Scan->getOperand(OpNo + 1).getImm();
          if (!fitsDisp16(Combined)) {
            UnsupportedUse = true;
            break;
          }
          MemRewrites.push_back({&*Scan, OpNo, Combined});
          RewroteUse = true;
          ++OpNo;
        }
      }

      if (UnsupportedUse || !RewroteUse) {
        Failed = true;
        break;
      }
      SawUse = true;
    }

    if (!Failed && !StoppedAtOverwrite &&
        !regDeadAfterInCFG(MBB.end(), MBB, BaseReg, TRI))
      Failed = true;

    if (Failed || !SawUse || SizeDelta >= 0) {
      ++I;
      continue;
    }

    for (const MemRewrite &Rewrite : MemRewrites) {
      Rewrite.MI->getOperand(Rewrite.BaseOp).setReg(Bedrock::SP);
      Rewrite.MI->getOperand(Rewrite.BaseOp + 1).setImm(Rewrite.Offset);
    }

    SmallPtrSet<MachineInstr *, 8> EraseSet;
    for (const InstrReplace &Replace : Replaces) {
      BuildMI(MBB, Replace.First->getIterator(), Replace.First->getDebugLoc(),
              TII.get(Bedrock::LEAri), Replace.Dst)
          .addReg(Bedrock::SP)
          .addImm(Replace.Offset);
      EraseSet.insert(Replace.First);
      if (Replace.Second)
        EraseSet.insert(Replace.Second);
    }

    EraseSet.insert(&BaseLea);
    for (MachineInstr *MI : EraseSet)
      MI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldStackBaseBiasOriginalUses(
    MachineFunction &MF) const {
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  struct BiasCandidate {
    MachineInstr *Add = nullptr;
    Register Base;
    int64_t OriginalOffset = 0;
    int64_t Bias = 0;
    SmallVector<MachineInstr *, 8> OriginalLeas;
    SmallVector<MachineInstr *, 8> BiasedMoves;
    int Savings = 0;
  };

  auto IsOriginalStackLea = [&](MachineInstr &MI, int64_t Offset,
                                Register &Dst) {
    if (MI.getOpcode() != Bedrock::LEAri || MI.getNumOperands() < 3 ||
        !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
        !MI.getOperand(2).isImm() || MI.getOperand(1).getReg() != Bedrock::SP ||
        MI.getOperand(2).getImm() != Offset)
      return false;
    Dst = MI.getOperand(0).getReg();
    return isAReg(Dst);
  };

  auto Evaluate = [&](MachineInstr &Add) {
    BiasCandidate Candidate;
    if (Add.getOpcode() != Bedrock::ADD64ri || Add.getNumOperands() < 3 ||
        !Add.getOperand(0).isReg() || !Add.getOperand(1).isReg() ||
        !Add.getOperand(2).isImm() ||
        !regsOverlap(TRI, Add.getOperand(0).getReg(),
                     Add.getOperand(1).getReg()))
      return Candidate;

    Register Base = Add.getOperand(0).getReg();
    int64_t Bias = Add.getOperand(2).getImm();
    if (!isAReg(Base) || Base == Bedrock::SP || Bias == 0 ||
        !fitsDisp16(Bias) ||
        !regDefDeadOrDeadAfterInCFG(Add.getIterator(), *Add.getParent(),
                                    Bedrock::FLAGS, TRI))
      return Candidate;

    MachineInstr *Def = findLastDefBefore(Add, Base, TRI);
    if (!Def || Def->getOpcode() != Bedrock::LEAri ||
        Def->getNumOperands() < 3 || !Def->getOperand(0).isReg() ||
        !Def->getOperand(1).isReg() || !Def->getOperand(2).isImm() ||
        !regsOverlap(TRI, Def->getOperand(0).getReg(), Base) ||
        Def->getOperand(1).getReg() != Bedrock::SP)
      return Candidate;

    int64_t OriginalOffset = Def->getOperand(2).getImm();
    SmallVector<MachineInstr *, 8> OriginalLeas;
    SmallVector<MachineInstr *, 8> BiasedMoves;
    bool PastAdd = false;
    bool Failed = false;
    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : MBB) {
        if (!PastAdd) {
          if (&MI == &Add)
            PastAdd = true;
          continue;
        }
        if (MI.isDebugInstr())
          continue;
        if (instrHasRegMaskForReg(MI, Base, TRI)) {
          Failed = true;
          break;
        }

        Register LeaDst;
        if (IsOriginalStackLea(MI, OriginalOffset, LeaDst)) {
          if (regsOverlap(TRI, LeaDst, Base)) {
            Failed = true;
            break;
          }
          OriginalLeas.push_back(&MI);
          continue;
        }

        if (MI.getOpcode() == Bedrock::MOV64rr && MI.getNumOperands() >= 2 &&
            MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
            regsOverlap(TRI, MI.getOperand(1).getReg(), Base)) {
          Register Dst = MI.getOperand(0).getReg();
          if (!isAReg(Dst) || regsOverlap(TRI, Dst, Base)) {
            Failed = true;
            break;
          }
          BiasedMoves.push_back(&MI);
          continue;
        }

        if (instrUsesReg(MI, Base, TRI)) {
          Failed = true;
          break;
        }

        if (instrDefinesReg(MI, Base, TRI)) {
          if (isPopOpcode(MI.getOpcode()))
            break;
          Failed = true;
          break;
        }
      }
      if (Failed)
        break;
    }

    int Savings =
        4 + int(OriginalLeas.size()) * 4 - int(BiasedMoves.size()) * 4;
    if (Failed || OriginalLeas.empty() || Savings <= 0)
      return Candidate;

    Candidate.Add = &Add;
    Candidate.Base = Base;
    Candidate.OriginalOffset = OriginalOffset;
    Candidate.Bias = Bias;
    Candidate.OriginalLeas = std::move(OriginalLeas);
    Candidate.BiasedMoves = std::move(BiasedMoves);
    Candidate.Savings = Savings;
    return Candidate;
  };

  bool Changed = false;
  while (true) {
    BiasCandidate Best;
    for (MachineBasicBlock &MBB : MF)
      for (MachineInstr &MI : MBB)
        if (!MI.isDebugInstr()) {
          BiasCandidate Candidate = Evaluate(MI);
          if (Candidate.Savings > Best.Savings)
            Best = std::move(Candidate);
        }

    if (!Best.Add)
      break;

    for (MachineInstr *MI : Best.OriginalLeas) {
      MachineBasicBlock &MBB = *MI->getParent();
      Register Dst = MI->getOperand(0).getReg();
      if (&MBB != Best.Add->getParent() && !MBB.isLiveIn(Best.Base))
        MBB.addLiveIn(Best.Base);
      BuildMI(MBB, MI->getIterator(), MI->getDebugLoc(),
              TII.get(Bedrock::MOV64rr), Dst)
          .addReg(Best.Base);
      MI->eraseFromParent();
    }

    for (MachineInstr *MI : Best.BiasedMoves) {
      MachineBasicBlock &MBB = *MI->getParent();
      Register Dst = MI->getOperand(0).getReg();
      BuildMI(MBB, MI->getIterator(), MI->getDebugLoc(),
              TII.get(Bedrock::LEAri), Dst)
          .addReg(Best.Base)
          .addImm(Best.Bias);
      MI->eraseFromParent();
    }

    Best.Add->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldRepeatedStackAddressLeas(
    MachineFunction &MF) const {
  struct StackAddressFacts {
    bool Reachable = false;
    bool Known[8] = {};
    int64_t Offset[8] = {};
  };

  auto RegIndex = [](Register Reg) -> unsigned { return Reg - Bedrock::A0; };

  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  auto ClearReg = [&](StackAddressFacts &Facts, Register Reg) {
    if (isAReg(Reg))
      Facts.Known[RegIndex(Reg)] = false;
  };

  auto SetReg = [&](StackAddressFacts &Facts, Register Reg, int64_t Offset) {
    if (!isAReg(Reg))
      return;
    unsigned Idx = RegIndex(Reg);
    Facts.Known[Idx] = true;
    Facts.Offset[Idx] = Offset;
  };

  auto GetRegOffset = [&](const StackAddressFacts &Facts,
                          Register Reg) -> std::optional<int64_t> {
    if (!isAReg(Reg))
      return std::nullopt;
    unsigned Idx = RegIndex(Reg);
    if (!Facts.Known[Idx])
      return std::nullopt;
    return Facts.Offset[Idx];
  };

  auto FindRegWithOffset = [&](const StackAddressFacts &Facts, int64_t Offset,
                               Register Avoid) -> Register {
    for (Register Reg = Bedrock::A6; Reg <= Bedrock::A7;
         Reg = Register(Reg + 1)) {
      if (regsOverlap(TRI, Reg, Avoid))
        continue;
      std::optional<int64_t> Known = GetRegOffset(Facts, Reg);
      if (Known && *Known == Offset)
        return Reg;
    }
    for (Register Reg = Bedrock::A0; Reg <= Bedrock::A5;
         Reg = Register(Reg + 1)) {
      if (regsOverlap(TRI, Reg, Avoid))
        continue;
      std::optional<int64_t> Known = GetRegOffset(Facts, Reg);
      if (Known && *Known == Offset)
        return Reg;
    }
    return Register();
  };

  auto TransferInstr = [&](StackAddressFacts &Facts, const MachineInstr &MI) {
    if (MI.isDebugInstr())
      return;

    if (!MI.isCall() && instrDefinesReg(MI, Bedrock::SP, TRI)) {
      for (unsigned I = 0; I != 8; ++I)
        Facts.Known[I] = false;
    }

    for (Register Reg = Bedrock::A0; Reg <= Bedrock::A7;
         Reg = Register(Reg + 1))
      if (instrHasRegMaskForReg(MI, Reg, TRI))
        ClearReg(Facts, Reg);

    if (MI.getOpcode() == Bedrock::LEAri && MI.getNumOperands() >= 3 &&
        MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
        MI.getOperand(2).isImm()) {
      Register Dst = MI.getOperand(0).getReg();
      Register Base = MI.getOperand(1).getReg();
      int64_t Offset = MI.getOperand(2).getImm();
      if (Base == Bedrock::SP) {
        SetReg(Facts, Dst, Offset);
        return;
      }
      if (std::optional<int64_t> BaseOffset = GetRegOffset(Facts, Base)) {
        SetReg(Facts, Dst, *BaseOffset + Offset);
        return;
      }
    }

    if (MI.getOpcode() == Bedrock::MOV64rr && MI.getNumOperands() >= 2 &&
        MI.getOperand(0).isReg() && MI.getOperand(1).isReg()) {
      Register Dst = MI.getOperand(0).getReg();
      Register Src = MI.getOperand(1).getReg();
      if (std::optional<int64_t> SrcOffset = GetRegOffset(Facts, Src))
        SetReg(Facts, Dst, *SrcOffset);
      else
        ClearReg(Facts, Dst);
      return;
    }

    if ((MI.getOpcode() == Bedrock::ADD64ri ||
         MI.getOpcode() == Bedrock::SUB64ri) &&
        MI.getNumOperands() >= 3 && MI.getOperand(0).isReg() &&
        MI.getOperand(1).isReg() && MI.getOperand(2).isImm()) {
      Register Dst = MI.getOperand(0).getReg();
      Register Src = MI.getOperand(1).getReg();
      if (std::optional<int64_t> SrcOffset = GetRegOffset(Facts, Src)) {
        int64_t Imm = MI.getOperand(2).getImm();
        SetReg(Facts, Dst,
               MI.getOpcode() == Bedrock::ADD64ri ? *SrcOffset + Imm
                                                  : *SrcOffset - Imm);
      } else {
        ClearReg(Facts, Dst);
      }
      return;
    }

    for (Register Reg = Bedrock::A0; Reg <= Bedrock::A7;
         Reg = Register(Reg + 1))
      if (instrDefinesReg(MI, Reg, TRI))
        ClearReg(Facts, Reg);
  };

  SmallVector<MachineBasicBlock *, 16> Blocks;
  for (MachineBasicBlock &MBB : MF)
    Blocks.push_back(&MBB);
  DenseMap<MachineBasicBlock *, StackAddressFacts> InFacts;
  DenseMap<MachineBasicBlock *, StackAddressFacts> OutFacts;

  auto MeetPredecessors = [&](MachineBasicBlock &MBB) {
    StackAddressFacts In;
    if (&MBB == &MF.front()) {
      In.Reachable = true;
      return In;
    }

    bool First = true;
    for (MachineBasicBlock *Pred : MBB.predecessors()) {
      StackAddressFacts PredOut = OutFacts.lookup(Pred);
      if (!PredOut.Reachable)
        continue;
      if (First) {
        In = PredOut;
        First = false;
        continue;
      }
      for (unsigned I = 0; I != 8; ++I)
        if (!In.Known[I] || !PredOut.Known[I] ||
            In.Offset[I] != PredOut.Offset[I])
          In.Known[I] = false;
    }
    In.Reachable = !First;
    return In;
  };

  auto SameFacts = [](const StackAddressFacts &A, const StackAddressFacts &B) {
    if (A.Reachable != B.Reachable)
      return false;
    for (unsigned I = 0; I != 8; ++I)
      if (A.Known[I] != B.Known[I] ||
          (A.Known[I] && A.Offset[I] != B.Offset[I]))
        return false;
    return true;
  };

  bool FactsChanged = true;
  while (FactsChanged) {
    FactsChanged = false;
    for (MachineBasicBlock *MBB : Blocks) {
      StackAddressFacts In = MeetPredecessors(*MBB);
      StackAddressFacts Out = In;
      if (Out.Reachable)
        for (const MachineInstr &MI : *MBB)
          TransferInstr(Out, MI);

      if (!SameFacts(InFacts.lookup(MBB), In)) {
        InFacts[MBB] = In;
        FactsChanged = true;
      }
      if (!SameFacts(OutFacts.lookup(MBB), Out)) {
        OutFacts[MBB] = Out;
        FactsChanged = true;
      }
    }
  }

  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  bool Changed = false;
  for (MachineBasicBlock *MBB : Blocks) {
    StackAddressFacts Facts = InFacts.lookup(MBB);
    if (!Facts.Reachable)
      continue;
    uint32_t DefinedARegs = 0;
    for (auto I = MBB->begin(); I != MBB->end();) {
      MachineInstr &MI = *I;
      if (MI.isDebugInstr()) {
        ++I;
        continue;
      }

      if (MI.getOpcode() == Bedrock::LEAri && MI.getNumOperands() >= 3 &&
          MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
          MI.getOperand(2).isImm()) {
        Register Dst = MI.getOperand(0).getReg();
        Register Base = MI.getOperand(1).getReg();
        std::optional<int64_t> BaseOffset;
        if (Base == Bedrock::SP)
          BaseOffset = 0;
        else
          BaseOffset = GetRegOffset(Facts, Base);
        if (BaseOffset) {
          int64_t Offset = *BaseOffset + MI.getOperand(2).getImm();
          if (std::optional<int64_t> DstOffset = GetRegOffset(Facts, Dst);
              DstOffset && *DstOffset == Offset) {
            auto Next = std::next(I);
            MI.eraseFromParent();
            I = Next;
            Changed = true;
            continue;
          }
          Register Src = FindRegWithOffset(Facts, Offset, Dst);
          if (Src) {
            if ((DefinedARegs & (1u << RegIndex(Src))) == 0 &&
                MBB != &MF.front() && !MBB->isLiveIn(Src))
              MBB->addLiveIn(Src);
            BuildMI(*MBB, I, MI.getDebugLoc(), TII.get(Bedrock::MOV64rr), Dst)
                .addReg(Src);
            auto Next = std::next(I);
            MI.eraseFromParent();
            I = Next;
            Changed = true;
            SetReg(Facts, Dst, Offset);
            DefinedARegs |= 1u << RegIndex(Dst);
            continue;
          }
        }
      }

      TransferInstr(Facts, MI);
      for (Register Reg = Bedrock::A0; Reg <= Bedrock::A7;
           Reg = Register(Reg + 1))
        if (instrDefinesReg(MI, Reg, TRI))
          DefinedARegs |= 1u << RegIndex(Reg);
      ++I;
    }
  }

  return Changed;
}

bool BedrockPeephole::foldRepeatedStackAddressLeasWithBorrowedBase(
    MachineFunction &MF) const {
  MachineInstr *PushM = nullptr;
  SmallVector<MachineInstr *, 4> PopMs;
  uint16_t PushPopMask = 0;
  if (!collectConsistentPushPopMask(MF, PushM, PopMs, PushPopMask))
    return false;

  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  BitVector Reserved = TRI.getReservedRegs(MF);

  SmallVector<Register, 2> FreeRegs;
  for (Register Reg = Bedrock::A6; Reg <= Bedrock::A7;
       Reg = Register(Reg + 1)) {
    if (Reserved.test(Reg))
      continue;

    bool Used = false;
    for (MachineBasicBlock &MBB : MF) {
      for (const MachineBasicBlock::RegisterMaskPair &LiveIn : MBB.liveins()) {
        if (regsOverlap(TRI, Register(LiveIn.PhysReg), Reg)) {
          Used = true;
          break;
        }
      }
      if (Used)
        break;

      for (MachineInstr &MI : MBB) {
        if (MI.isDebugInstr())
          continue;
        if (isPushPopOpcode(MI.getOpcode()))
          continue;
        if (instrHasRegMaskForReg(MI, Reg, TRI) ||
            instrTouchesReg(MI, Reg, TRI)) {
          Used = true;
          break;
        }
      }
      if (Used)
        break;
    }

    if (!Used)
      FreeRegs.push_back(Reg);
  }
  if (FreeRegs.empty())
    return false;

  MachineBasicBlock &Entry = MF.front();
  MachineBasicBlock::iterator Insert = Entry.begin();
  while (Insert != Entry.end() && Insert->isDebugInstr())
    ++Insert;
  if (Insert != Entry.end() && isPushOpcode(Insert->getOpcode()))
    Insert = std::next(Insert);
  for (auto I = Insert; I != Entry.end(); ++I) {
    if (I->isDebugInstr())
      continue;
    bool IsSubSP = (I->getOpcode() == Bedrock::SUB64ri ||
                    I->getOpcode() == Bedrock::SUB64rr) &&
                   I->getNumOperands() >= 2 && I->getOperand(0).isReg() &&
                   I->getOperand(1).isReg() &&
                   I->getOperand(0).getReg() == Bedrock::SP &&
                   I->getOperand(1).getReg() == Bedrock::SP;
    if (IsSubSP) {
      Insert = std::next(I);
      break;
    }
    if (I->isCall() || I->isTerminator())
      break;
  }

  auto BeforeInsert = [&](const MachineInstr &MI) {
    if (MI.getParent() != &Entry)
      return false;
    for (auto I = Entry.begin(); I != Insert; ++I)
      if (&*I == &MI)
        return true;
    return false;
  };

  DenseMap<int64_t, SmallVector<MachineInstr *, 8>> LeasByOffset;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || BeforeInsert(MI))
        continue;
      if (MI.getOpcode() != Bedrock::LEAri || MI.getNumOperands() < 3 ||
          !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
          !MI.getOperand(2).isImm() || MI.getOperand(1).getReg() != Bedrock::SP)
        continue;
      Register Dst = MI.getOperand(0).getReg();
      if (!isAReg(Dst) || Dst == Bedrock::SP)
        continue;
      LeasByOffset[MI.getOperand(2).getImm()].push_back(&MI);
    }
  }

  struct Candidate {
    int64_t Offset = 0;
    int Savings = 0;
  };

  auto BestCandidate = [&](Register BorrowReg) {
    Candidate Best;
    for (const auto &It : LeasByOffset) {
      int Savings = -6;
      for (MachineInstr *MI : It.second) {
        Register Dst = MI->getOperand(0).getReg();
        Savings += regsOverlap(TRI, Dst, BorrowReg) ? 6 : 2;
      }
      if (Savings > Best.Savings)
        Best = {It.first, Savings};
    }
    return Best;
  };

  bool Changed = false;
  uint16_t AddedMask = 0;
  for (Register BorrowReg : FreeRegs) {
    Candidate Best = BestCandidate(BorrowReg);
    if (Best.Savings <= 0)
      continue;

    DebugLoc DL = Insert != Entry.end() ? Insert->getDebugLoc() : DebugLoc();
    BuildMI(Entry, Insert, DL, TII.get(Bedrock::LEAri), BorrowReg)
        .addReg(Bedrock::SP)
        .addImm(Best.Offset);

    SmallVector<MachineInstr *, 8> Leas = LeasByOffset.lookup(Best.Offset);
    LeasByOffset.erase(Best.Offset);
    for (MachineInstr *MI : Leas) {
      MachineBasicBlock &MBB = *MI->getParent();
      Register Dst = MI->getOperand(0).getReg();
      if (!regsOverlap(TRI, Dst, BorrowReg)) {
        BuildMI(MBB, MI->getIterator(), MI->getDebugLoc(),
                TII.get(Bedrock::MOV64rr), Dst)
            .addReg(BorrowReg);
      }
      MI->eraseFromParent();
    }

    if (std::optional<unsigned> Bit = getMaskBit(BorrowReg))
      if ((PushPopMask & (uint16_t(1) << *Bit)) == 0)
        AddedMask |= uint16_t(1) << *Bit;

    for (MachineBasicBlock &MBB : MF)
      if (&MBB != &MF.front() && !MBB.isLiveIn(BorrowReg))
        MBB.addLiveIn(BorrowReg);
    Changed = true;
  }

  if (AddedMask != 0) {
    extendPushPopMask(MF, *PushM, PopMs, PushPopMask, AddedMask);
    for (unsigned Bit = 0; Bit != 16; ++Bit) {
      if ((AddedMask & (uint16_t(1) << Bit)) == 0)
        continue;
      Register Reg = getRegForMaskBit(Bit);
      if (!MF.front().isLiveIn(Reg))
        MF.front().addLiveIn(Reg);
    }
  }

  return Changed;
}

bool BedrockPeephole::foldRepeatedStackAddressLeasWithScopedBase(
    MachineFunction &MF) const {
  MachineInstr *PushM = nullptr;
  SmallVector<MachineInstr *, 4> PopMs;
  uint16_t PushPopMask = 0;
  if (!collectConsistentPushPopMask(MF, PushM, PopMs, PushPopMask))
    return false;

  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  BitVector Reserved = TRI.getReservedRegs(MF);

  SmallVector<MachineBasicBlock *, 16> Blocks;
  DenseMap<MachineBasicBlock *, unsigned> BlockIndex;
  for (MachineBasicBlock &MBB : MF) {
    BlockIndex[&MBB] = Blocks.size();
    Blocks.push_back(&MBB);
  }
  if (Blocks.empty())
    return false;

  unsigned NumBlocks = Blocks.size();
  SmallVector<BitVector, 16> Dom(NumBlocks, BitVector(NumBlocks, true));
  Dom[0].reset();
  Dom[0].set(0);

  bool DomChanged = true;
  while (DomChanged) {
    DomChanged = false;
    for (unsigned BI = 1; BI != NumBlocks; ++BI) {
      MachineBasicBlock *MBB = Blocks[BI];
      BitVector NewDom(NumBlocks, true);
      bool SawPred = false;
      for (MachineBasicBlock *Pred : MBB->predecessors()) {
        auto It = BlockIndex.find(Pred);
        if (It == BlockIndex.end())
          continue;
        if (!SawPred) {
          NewDom = Dom[It->second];
          SawPred = true;
        } else {
          NewDom &= Dom[It->second];
        }
      }
      if (!SawPred)
        NewDom.reset();
      NewDom.set(BI);
      if (NewDom != Dom[BI]) {
        Dom[BI] = NewDom;
        DomChanged = true;
      }
    }
  }

  auto Dominates = [&](MachineBasicBlock *A, MachineBasicBlock *B) {
    auto AI = BlockIndex.find(A);
    auto BI = BlockIndex.find(B);
    return AI != BlockIndex.end() && BI != BlockIndex.end() &&
           Dom[BI->second].test(AI->second);
  };

  auto IsStackLea = [&](const MachineInstr &MI, int64_t Offset, Register &Dst) {
    if (MI.getOpcode() != Bedrock::LEAri || MI.getNumOperands() < 3 ||
        !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
        !MI.getOperand(2).isImm() || MI.getOperand(1).getReg() != Bedrock::SP ||
        MI.getOperand(2).getImm() != Offset)
      return false;
    Dst = MI.getOperand(0).getReg();
    return isAReg(Dst) && Dst != Bedrock::SP;
  };

  auto TransferKnown = [&](MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator Begin, bool Known,
                           Register CacheReg, int64_t Offset) {
    for (auto I = Begin; I != MBB.end() && Known; ++I) {
      if (I->isDebugInstr())
        continue;
      Register Dst;
      if (IsStackLea(*I, Offset, Dst) && regsOverlap(TRI, Dst, CacheReg))
        continue;
      if (!I->isCall() && instrDefinesReg(*I, Bedrock::SP, TRI))
        Known = false;
      if (instrHasRegMaskForReg(*I, CacheReg, TRI) ||
          instrDefinesReg(*I, CacheReg, TRI))
        Known = false;
    }
    return Known;
  };

  struct ScopedCandidate {
    MachineInstr *Start = nullptr;
    Register CacheReg;
    int64_t Offset = 0;
    bool NeedsSetup = false;
    int Savings = 0;
    SmallVector<MachineInstr *, 8> Rewrites;
  };

  auto Evaluate = [&](MachineInstr &Start, Register CacheReg,
                      int64_t Offset) -> ScopedCandidate {
    ScopedCandidate Candidate;
    MachineBasicBlock &StartMBB = *Start.getParent();
    Register StartDst = Start.getOperand(0).getReg();
    bool NeedsSetup = !regsOverlap(TRI, StartDst, CacheReg);
    if (NeedsSetup &&
        !regUnusedAfterInCFG(Start.getIterator(), StartMBB, CacheReg, TRI))
      return Candidate;

    SmallVector<bool, 16> InKnown(NumBlocks, false);
    SmallVector<bool, 16> OutKnown(NumBlocks, false);
    for (unsigned BI = 0; BI != NumBlocks; ++BI)
      OutKnown[BI] = Dominates(&StartMBB, Blocks[BI]);

    bool Changed = true;
    while (Changed) {
      Changed = false;
      for (unsigned BI = 0; BI != NumBlocks; ++BI) {
        MachineBasicBlock *MBB = Blocks[BI];
        bool In = false;
        if (MBB == &StartMBB) {
          In = true;
        } else if (Dominates(&StartMBB, MBB)) {
          In = !MBB->pred_empty();
          for (MachineBasicBlock *Pred : MBB->predecessors()) {
            auto It = BlockIndex.find(Pred);
            if (It == BlockIndex.end() || !OutKnown[It->second]) {
              In = false;
              break;
            }
          }
        }

        MachineBasicBlock::iterator Begin =
            MBB == &StartMBB ? Start.getIterator() : MBB->begin();
        bool Out = TransferKnown(*MBB, Begin, In, CacheReg, Offset);
        if (InKnown[BI] != In || OutKnown[BI] != Out) {
          InKnown[BI] = In;
          OutKnown[BI] = Out;
          Changed = true;
        }
      }
    }

    int Savings = NeedsSetup ? -6 : 0;
    SmallVector<MachineInstr *, 8> Rewrites;
    for (unsigned BI = 0; BI != NumBlocks; ++BI) {
      MachineBasicBlock *MBB = Blocks[BI];
      bool Known = MBB == &StartMBB ? true : InKnown[BI];
      if (!Known)
        continue;
      MachineBasicBlock::iterator Begin =
          MBB == &StartMBB ? Start.getIterator() : MBB->begin();
      for (auto I = Begin; I != MBB->end() && Known; ++I) {
        if (I->isDebugInstr())
          continue;
        Register Dst;
        if (IsStackLea(*I, Offset, Dst)) {
          if (&*I != &Start || NeedsSetup) {
            Savings += regsOverlap(TRI, Dst, CacheReg) ? 6 : 2;
            Rewrites.push_back(&*I);
          }
          if (regsOverlap(TRI, Dst, CacheReg))
            continue;
        }
        if (!I->isCall() && instrDefinesReg(*I, Bedrock::SP, TRI))
          Known = false;
        if (instrHasRegMaskForReg(*I, CacheReg, TRI) ||
            instrDefinesReg(*I, CacheReg, TRI))
          Known = false;
      }
    }

    if (Savings <= 0 || Rewrites.empty())
      return Candidate;
    Candidate.Start = &Start;
    Candidate.CacheReg = CacheReg;
    Candidate.Offset = Offset;
    Candidate.NeedsSetup = NeedsSetup;
    Candidate.Savings = Savings;
    Candidate.Rewrites = std::move(Rewrites);
    return Candidate;
  };

  bool Changed = false;
  while (true) {
    ScopedCandidate Best;
    for (MachineBasicBlock *MBB : Blocks) {
      if (MBB->getParent() != &MF)
        continue;
      for (MachineInstr &MI : *MBB) {
        if (MI.isDebugInstr() || MI.getOpcode() != Bedrock::LEAri ||
            MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
            !MI.getOperand(1).isReg() || !MI.getOperand(2).isImm() ||
            MI.getOperand(1).getReg() != Bedrock::SP ||
            !isAReg(MI.getOperand(0).getReg()) ||
            MI.getOperand(0).getReg() == Bedrock::SP)
          continue;

        int64_t Offset = MI.getOperand(2).getImm();
        for (Register CacheReg :
             {Register(Bedrock::A6), Register(Bedrock::A7)}) {
          if (Reserved.test(CacheReg))
            continue;
          ScopedCandidate Candidate = Evaluate(MI, CacheReg, Offset);
          if (Candidate.Savings > Best.Savings)
            Best = std::move(Candidate);
        }
      }
    }

    if (!Best.Start)
      break;

    if (Best.NeedsSetup) {
      BuildMI(*Best.Start->getParent(), Best.Start->getIterator(),
              Best.Start->getDebugLoc(), TII.get(Bedrock::LEAri), Best.CacheReg)
          .addReg(Bedrock::SP)
          .addImm(Best.Offset);
    }

    for (MachineInstr *MI : Best.Rewrites) {
      MachineBasicBlock &MBB = *MI->getParent();
      Register Dst = MI->getOperand(0).getReg();
      if (!regsOverlap(TRI, Dst, Best.CacheReg)) {
        if (&MBB != Best.Start->getParent() && !MBB.isLiveIn(Best.CacheReg))
          MBB.addLiveIn(Best.CacheReg);
        BuildMI(MBB, MI->getIterator(), MI->getDebugLoc(),
                TII.get(Bedrock::MOV64rr), Dst)
            .addReg(Best.CacheReg);
      }
      MI->eraseFromParent();
    }

    if (std::optional<unsigned> Bit = getMaskBit(Best.CacheReg)) {
      uint16_t AddedMask = uint16_t(1) << *Bit;
      if ((PushPopMask & AddedMask) == 0) {
        extendPushPopMask(MF, *PushM, PopMs, PushPopMask, AddedMask);
        PushPopMask |= AddedMask;
        if (!MF.front().isLiveIn(Best.CacheReg))
          MF.front().addLiveIn(Best.CacheReg);
      }
    }

    Changed = true;
  }

  return Changed;
}
