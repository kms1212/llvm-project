//===-- BedrockPeepholeMemory.cpp - Bedrock peepholes -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockPeephole.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MathExtras.h"

namespace {

struct AbsStoreRunEntry {
  MachineInstr *Addr;
  MachineInstr *Store;
  int64_t Offset;
};

struct AbsLoadRunEntry {
  MachineInstr *Addr;
  MachineInstr *Load;
  Register Dst;
  int64_t Offset;
};

struct AbsDestStoreRunEntry {
  MachineInstr *Addr;
  MachineInstr *Store;
  int64_t Offset;
};

enum class DirectAbsMemShape {
  Load,
  Store,
  ImmMem,
  MemToAbs,
  IncDec,
};

struct DirectAbsMemRunEntry {
  MachineInstr *MI = nullptr;
  const MachineOperand *Target = nullptr;
  unsigned NewOpcode = 0;
  DirectAbsMemShape Shape = DirectAbsMemShape::Load;
};

struct ImmQwordStoreRunEntry {
  MachineInstr *AddrImm;
  MachineInstr *AddrCopy;
  MachineInstr *ValueImm;
  MachineInstr *Store;
  Register Base;
  Register ValueReg;
  int64_t Addr;
  int64_t Value;
};

struct OffsetQwordZeroStoreEntry {
  MachineInstr *Store;
  int64_t Offset;
};

static bool sameAbsTargetIgnoringOffset(const MachineOperand &A,
                                        const MachineOperand &B) {
  if (A.getType() != B.getType() || A.getTargetFlags() != B.getTargetFlags())
    return false;

  if (A.isGlobal())
    return A.getGlobal() == B.getGlobal();
  if (A.isSymbol())
    return StringRef(A.getSymbolName()) == StringRef(B.getSymbolName());
  if (A.isMCSymbol())
    return A.getMCSymbol() == B.getMCSymbol();
  if (A.isCPI())
    return A.getIndex() == B.getIndex();
  if (A.isBlockAddress())
    return A.getBlockAddress() == B.getBlockAddress();
  return false;
}

static bool matchAbsQwordStorePair(MachineBasicBlock::iterator AddrI,
                                   MachineBasicBlock &MBB,
                                   const TargetRegisterInfo &TRI,
                                   const MachineOperand *&Target,
                                   Register &Base, Register &Src,
                                   int64_t &Offset, MachineInstr *&Store) {
  MachineInstr &Addr = *AddrI;
  if (Addr.getOpcode() != Bedrock::MOV64abs || Addr.getNumOperands() < 2 ||
      !Addr.getOperand(0).isReg() || !isAbsTargetOperand(Addr.getOperand(1)))
    return false;

  Base = Addr.getOperand(0).getReg();
  if (!isAReg(Base))
    return false;

  auto StoreI = nextNonDebug(AddrI, MBB);
  if (StoreI == MBB.end() || StoreI->getOpcode() != Bedrock::MOV64mr ||
      StoreI->getNumOperands() < 3 || hasOrderedMemOperand(*StoreI))
    return false;

  Register StoreBase;
  int64_t StoreOffset = 0;
  if (!isMemStore(*StoreI, Src, StoreBase, StoreOffset) ||
      StoreOffset != 0 || !regsOverlap(TRI, StoreBase, Base) ||
      regsOverlap(TRI, Src, Base))
    return false;

  Target = &Addr.getOperand(1);
  Offset = Target->getOffset();
  Store = &*StoreI;
  return true;
}

static bool matchAbsLoadPair(MachineBasicBlock::iterator AddrI,
                             MachineBasicBlock &MBB,
                             const TargetRegisterInfo &TRI,
                             const MachineOperand *&Target, Register &Base,
                             Register &Dst, int64_t &Offset,
                             MachineInstr *&Load) {
  MachineInstr &Addr = *AddrI;
  if (Addr.getOpcode() != Bedrock::MOV64abs || Addr.getNumOperands() < 2 ||
      !Addr.getOperand(0).isReg() || !isAbsTargetOperand(Addr.getOperand(1)))
    return false;

  Base = Addr.getOperand(0).getReg();
  if (!isAReg(Base))
    return false;

  auto LoadI = nextNonDebug(AddrI, MBB);
  if (LoadI == MBB.end() || hasOrderedMemOperand(*LoadI))
    return false;

  Register LoadBase;
  int64_t LoadOffset = 0;
  if (!isMemLoad(*LoadI, Dst, LoadBase, LoadOffset) || LoadOffset != 0 ||
      !regsOverlap(TRI, LoadBase, Base) || regsOverlap(TRI, Dst, Base))
    return false;

  Target = &Addr.getOperand(1);
  Offset = Target->getOffset();
  Load = &*LoadI;
  return true;
}

static bool matchImmQwordStoreEntry(MachineBasicBlock::iterator AddrI,
                                    MachineBasicBlock &MBB,
                                    const TargetRegisterInfo &TRI,
                                    ImmQwordStoreRunEntry &Entry) {
  MachineInstr &AddrImm = *AddrI;
  Register Scratch;
  int64_t Addr = 0;
  if ((!isMov32Imm(AddrImm, Scratch, Addr) &&
       !isMov64Imm(AddrImm, Scratch, Addr)) ||
      !isDReg(Scratch))
    return false;

  auto AddrCopyI = nextNonDebug(AddrI, MBB);
  if (AddrCopyI == MBB.end() || AddrCopyI->getOpcode() != Bedrock::MOV64rr ||
      AddrCopyI->getNumOperands() < 2 || !AddrCopyI->getOperand(0).isReg() ||
      !AddrCopyI->getOperand(1).isReg() ||
      !regsOverlap(TRI, AddrCopyI->getOperand(1).getReg(), Scratch) ||
      !isAReg(AddrCopyI->getOperand(0).getReg()))
    return false;
  Register Base = AddrCopyI->getOperand(0).getReg();

  auto ValueImmI = nextNonDebug(AddrCopyI, MBB);
  Register ValueReg;
  int64_t Value = 0;
  if (ValueImmI == MBB.end() ||
      !isMov64Imm(*ValueImmI, ValueReg, Value) ||
      !regsOverlap(TRI, ValueReg, Scratch) || !isDReg(ValueReg))
    return false;

  auto StoreI = nextNonDebug(ValueImmI, MBB);
  if (StoreI == MBB.end() || StoreI->getOpcode() != Bedrock::MOV64mr)
    return false;

  Register StoreSrc;
  Register StoreBase;
  int64_t StoreOffset = 0;
  if (!isMemStore(*StoreI, StoreSrc, StoreBase, StoreOffset) ||
      StoreOffset != 0 || !regsOverlap(TRI, StoreSrc, ValueReg) ||
      !regsOverlap(TRI, StoreBase, Base) || regsOverlap(TRI, Base, ValueReg))
    return false;

  Entry.AddrImm = &AddrImm;
  Entry.AddrCopy = &*AddrCopyI;
  Entry.ValueImm = &*ValueImmI;
  Entry.Store = &*StoreI;
  Entry.Base = Base;
  Entry.ValueReg = ValueReg;
  Entry.Addr = Addr;
  Entry.Value = Value;
  return true;
}

static bool isPlainMemToMemMove(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
  case Bedrock::MOV8mm:
  case Bedrock::MOV16mm:
  case Bedrock::MOV32mm:
  case Bedrock::MOV64mm:
    return true;
  }
}

static bool matchAbsDestStorePair(MachineBasicBlock::iterator AddrI,
                                  MachineBasicBlock &MBB,
                                  const TargetRegisterInfo &TRI,
                                  const MachineOperand *&Target,
                                  Register &Base, int64_t &Offset,
                                  MachineInstr *&Store) {
  MachineInstr &Addr = *AddrI;
  if (Addr.getOpcode() != Bedrock::MOV64abs || Addr.getNumOperands() < 2 ||
      !Addr.getOperand(0).isReg() || !isAbsTargetOperand(Addr.getOperand(1)))
    return false;

  Base = Addr.getOperand(0).getReg();
  if (!isAReg(Base))
    return false;

  auto StoreI = nextNonDebug(AddrI, MBB);
  if (StoreI == MBB.end() || hasOrderedMemOperand(*StoreI))
    return false;

  Register StoreSrc;
  Register StoreBase;
  int64_t StoreOffset = 0;
  if (isMemStore(*StoreI, StoreSrc, StoreBase, StoreOffset)) {
    if (StoreOffset != 0 || !regsOverlap(TRI, StoreBase, Base) ||
        regsOverlap(TRI, StoreSrc, Base))
      return false;
  } else if (isPlainMemToMemMove(StoreI->getOpcode())) {
    if (StoreI->getNumOperands() < 4 || !StoreI->getOperand(0).isReg() ||
        !StoreI->getOperand(1).isImm() || !StoreI->getOperand(2).isReg() ||
        !StoreI->getOperand(3).isImm() ||
        StoreI->getOperand(3).getImm() != 0 ||
        !regsOverlap(TRI, StoreI->getOperand(2).getReg(), Base) ||
        regsOverlap(TRI, StoreI->getOperand(0).getReg(), Base))
      return false;
  } else {
    return false;
  }

  Target = &Addr.getOperand(1);
  Offset = Target->getOffset();
  Store = &*StoreI;
  return true;
}

static bool getLeaAliasForBase(MachineBasicBlock::iterator StoreI,
                               MachineBasicBlock &MBB,
                               const TargetRegisterInfo &TRI, Register Base,
                               Register &AliasBase, int64_t &AliasOffset) {
  AliasBase = Register();
  AliasOffset = 0;

  auto Prev = prevNonDebug(StoreI, MBB);
  if (Prev == MBB.end() || Prev->getOpcode() != Bedrock::LEAri ||
      Prev->getNumOperands() < 3 || !Prev->getOperand(0).isReg() ||
      !Prev->getOperand(1).isReg() || !Prev->getOperand(2).isImm())
    return false;

  if (!regsOverlap(TRI, Prev->getOperand(0).getReg(), Base))
    return false;

  AliasBase = Prev->getOperand(1).getReg();
  AliasOffset = Prev->getOperand(2).getImm();
  return AliasBase.isValid() && isAReg(AliasBase) &&
         !regsOverlap(TRI, AliasBase, Base);
}

static bool matchOffsetQwordZeroStore(MachineBasicBlock::iterator StoreI,
                                      MachineBasicBlock &MBB,
                                      const TargetRegisterInfo &TRI,
                                      Register Base, Register AliasBase,
                                      int64_t AliasOffset, Register Src,
                                      int64_t &Offset) {
  MachineInstr &Store = *StoreI;
  if (Store.getOpcode() != Bedrock::MOV64mr || hasOrderedMemOperand(Store))
    return false;

  Register StoreSrc;
  Register StoreBase;
  int64_t StoreOffset = 0;
  if (!isMemStore(Store, StoreSrc, StoreBase, StoreOffset) ||
      !regsOverlap(TRI, StoreSrc, Src) || regsOverlap(TRI, StoreBase, Src))
    return false;

  if (regsOverlap(TRI, StoreBase, Base)) {
    Offset = StoreOffset;
    return true;
  }

  if (AliasBase.isValid() && regsOverlap(TRI, StoreBase, AliasBase)) {
    Offset = StoreOffset - AliasOffset;
    return true;
  }

  return false;
}

} // end anonymous namespace

bool BedrockPeephole::foldClrZeroMemCmp(MachineBasicBlock &MBB,
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

bool BedrockPeephole::foldMemoryBitOps(MachineBasicBlock &MBB,
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

bool BedrockPeephole::foldMemoryBinStore(MachineBasicBlock &MBB,
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

bool BedrockPeephole::foldMemoryBinStoreAcrossDef(
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

bool BedrockPeephole::foldMemoryBinStoreWithLoadedSource(
    MachineBasicBlock &MBB, MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    {
      MachineInstr &SrcLoad = *I;
      Register Src;
      Register SrcBase;
      int64_t SrcOffset = 0;
      if (!SrcLoad.isDebugInstr() &&
          isMemLoad(SrcLoad, Src, SrcBase, SrcOffset) &&
          !hasOrderedMemOperand(SrcLoad)) {
        auto AccLoadI = nextNonDebug(I, MBB);
        auto AddI =
            AccLoadI == MBB.end() ? MBB.end() : nextNonDebug(AccLoadI, MBB);
        auto StoreI =
            AddI == MBB.end() ? MBB.end() : nextNonDebug(AddI, MBB);

        Register Acc;
        Register AccBase;
        int64_t AccOffset = 0;
        Register Result;
        Register BinSrc;
        Register StoreSrc;
        Register StoreBase;
        int64_t StoreOffset = 0;
        unsigned Opcode =
            AddI == MBB.end() ? 0 : getMemDestBinOpcode(AddI->getOpcode());
        if (AccLoadI != MBB.end() && AddI != MBB.end() &&
            StoreI != MBB.end() &&
            isMemLoad(*AccLoadI, Acc, AccBase, AccOffset) &&
            !hasOrderedMemOperand(*AccLoadI) &&
            !hasOrderedMemOperand(*AddI) && !hasOrderedMemOperand(*StoreI) &&
            Opcode != 0 &&
            binOpcodeMatchesLoad(AddI->getOpcode(), SrcLoad.getOpcode()) &&
            binOpcodeMatchesLoad(AddI->getOpcode(), AccLoadI->getOpcode()) &&
            isBinUsingLoadedValue(*AddI, Acc, Result, BinSrc, TRI) &&
            regsOverlap(TRI, BinSrc, Src) &&
            isMemStore(*StoreI, StoreSrc, StoreBase, StoreOffset) &&
            StoreI->getOpcode() == getStoreOpcodeForLoad(AccLoadI->getOpcode()) &&
            !regsOverlap(TRI, Acc, Src) &&
            regsOverlap(TRI, StoreSrc, Result) &&
            regsOverlap(TRI, StoreBase, AccBase) && StoreOffset == AccOffset &&
            (operandIsKill(*StoreI, Result, TRI) ||
             regDeadAfter(std::next(StoreI), MBB, Result, TRI))) {
          MachineInstrBuilder MemBin =
              BuildMI(MBB, AccLoadI, AddI->getDebugLoc(), TII.get(Opcode))
                  .addReg(Src, getKillRegState(operandIsKill(*AddI, Src, TRI)))
                  .addReg(AccBase)
                  .addImm(AccOffset);
          MemBin.cloneMergedMemRefs({&*AccLoadI, &*StoreI});

          AccLoadI->eraseFromParent();
          AddI->eraseFromParent();
          StoreI->eraseFromParent();
          I = MBB.begin();
          Changed = true;
          continue;
        }
      }
    }

    {
      MachineInstr &Src1Load = *I;
      Register Src1;
      Register Src1Base;
      int64_t Src1Offset = 0;
      if (!Src1Load.isDebugInstr() &&
          isMemLoad(Src1Load, Src1, Src1Base, Src1Offset) &&
          !hasOrderedMemOperand(Src1Load)) {
        auto AccLoadI = nextNonDebug(I, MBB);
        auto Bin1I =
            AccLoadI == MBB.end() ? MBB.end() : nextNonDebug(AccLoadI, MBB);
        auto Src2LoadI =
            Bin1I == MBB.end() ? MBB.end() : nextNonDebug(Bin1I, MBB);
        auto Bin2I =
            Src2LoadI == MBB.end() ? MBB.end() : nextNonDebug(Src2LoadI, MBB);
        auto StoreI =
            Bin2I == MBB.end() ? MBB.end() : nextNonDebug(Bin2I, MBB);

        Register Acc;
        Register AccBase;
        int64_t AccOffset = 0;
        Register Src2;
        Register Src2Base;
        int64_t Src2Offset = 0;
        Register Bin1Result;
        Register Bin1Src;
        Register Bin2Result;
        Register Bin2Src;
        Register StoreSrc;
        Register StoreBase;
        int64_t StoreOffset = 0;
        unsigned Opcode =
            Bin1I == MBB.end() ? 0 : getMemDestBinOpcode(Bin1I->getOpcode());
        if (AccLoadI != MBB.end() && Bin1I != MBB.end() &&
            Src2LoadI != MBB.end() && Bin2I != MBB.end() &&
            StoreI != MBB.end() &&
            Bin1I->getOpcode() == Bin2I->getOpcode() && Opcode != 0 &&
            isCommutableBinOpcode(Bin1I->getOpcode()) &&
            binOpcodeMatchesLoad(Bin1I->getOpcode(), Src1Load.getOpcode()) &&
            binOpcodeMatchesLoad(Bin1I->getOpcode(), AccLoadI->getOpcode()) &&
            binOpcodeMatchesLoad(Bin1I->getOpcode(), Src2LoadI->getOpcode()) &&
            isMemLoad(*AccLoadI, Acc, AccBase, AccOffset) &&
            isMemLoad(*Src2LoadI, Src2, Src2Base, Src2Offset) &&
            !hasOrderedMemOperand(*AccLoadI) &&
            !hasOrderedMemOperand(*Src2LoadI) &&
            !hasOrderedMemOperand(*Bin1I) && !hasOrderedMemOperand(*Bin2I) &&
            !hasOrderedMemOperand(*StoreI) &&
            isBinUsingLoadedValue(*Bin1I, Acc, Bin1Result, Bin1Src, TRI) &&
            regsOverlap(TRI, Bin1Result, Acc) &&
            regsOverlap(TRI, Bin1Src, Src1) &&
            isBinUsingLoadedValue(*Bin2I, Src2, Bin2Result, Bin2Src, TRI) &&
            regsOverlap(TRI, Bin2Src, Acc) &&
            isMemStore(*StoreI, StoreSrc, StoreBase, StoreOffset) &&
            StoreI->getOpcode() == getStoreOpcodeForLoad(AccLoadI->getOpcode()) &&
            !regsOverlap(TRI, Src1, Acc) &&
            regsOverlap(TRI, StoreSrc, Bin2Result) &&
            regsOverlap(TRI, StoreBase, AccBase) && StoreOffset == AccOffset &&
            regDefDeadOrDeadAfter(Bin1I, MBB, Bedrock::FLAGS, TRI) &&
            regDefDeadOrDeadAfter(Bin2I, MBB, Bedrock::FLAGS, TRI) &&
            (operandIsKill(*Bin2I, Acc, TRI) ||
             regDeadAfter(std::next(Bin2I), MBB, Acc, TRI)) &&
            (operandIsKill(*StoreI, Bin2Result, TRI) ||
             regDeadAfter(std::next(StoreI), MBB, Bin2Result, TRI))) {
          MachineInstrBuilder NewSrc2Load =
              BuildMI(MBB, AccLoadI, Src2LoadI->getDebugLoc(),
                      TII.get(Src2LoadI->getOpcode()), Acc)
                  .addReg(Src2Base,
                          getKillRegState(
                              operandIsKill(*Src2LoadI, Src2Base, TRI)))
                  .addImm(Src2Offset);
          NewSrc2Load.cloneMemRefs(*Src2LoadI);

          MachineInstrBuilder Combine =
              BuildMI(MBB, Bin1I, Bin1I->getDebugLoc(),
                      TII.get(Bin1I->getOpcode()), Acc)
                  .addReg(Acc)
                  .addReg(Src1,
                          getKillRegState(operandIsKill(*Bin1I, Src1, TRI)));
          Combine.setMIFlags(Bin1I->getFlags());

          MachineInstrBuilder MemBin =
              BuildMI(MBB, Bin2I, Bin2I->getDebugLoc(), TII.get(Opcode))
                  .addReg(Acc,
                          getKillRegState(operandIsKill(*Bin2I, Acc, TRI)))
                  .addReg(AccBase)
                  .addImm(AccOffset);
          MemBin.cloneMergedMemRefs({&*AccLoadI, &*StoreI});

          AccLoadI->eraseFromParent();
          Bin1I->eraseFromParent();
          Src2LoadI->eraseFromParent();
          Bin2I->eraseFromParent();
          StoreI->eraseFromParent();
          I = MBB.begin();
          Changed = true;
          continue;
        }
      }
    }

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

bool BedrockPeephole::foldMemoryImmBinStore(MachineBasicBlock &MBB,
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

bool BedrockPeephole::foldMemoryRegFlagOp(MachineBasicBlock &MBB,
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

    auto CanSkip = [&](const MachineInstr &MI) {
      return !MI.isTerminator() && !MI.isCall() &&
             !MI.hasUnmodeledSideEffects() && !MI.mayLoadOrStore() &&
             !instrTouchesReg(MI, Bedrock::FLAGS, TRI) &&
             !instrTouchesReg(MI, Loaded, TRI) &&
             !instrTouchesReg(MI, Base, TRI) &&
             (!IndexedLoad || !instrTouchesReg(MI, Index, TRI));
    };

    auto ZeroCmpOpcodeForSelfTest = [](unsigned TestOpcode,
                                       unsigned LoadOpcode) -> unsigned {
      switch (LoadOpcode) {
      default:
        return 0u;
      case Bedrock::MOV8rm:
      case Bedrock::MOV8idx1rm:
      case Bedrock::MOV8idx4rm:
      case Bedrock::MOV8idx4lrm:
        return TestOpcode == Bedrock::TEST8rr ? Bedrock::CMP8mi : 0;
      case Bedrock::MOV16rm:
      case Bedrock::MOV16idx1rm:
      case Bedrock::MOV16idx4rm:
      case Bedrock::MOV16idx4lrm:
        return TestOpcode == Bedrock::TEST16rr ? Bedrock::CMP16mi : 0;
      case Bedrock::MOV32rm:
      case Bedrock::MOV32idx1rm:
      case Bedrock::MOV32idx4rm:
      case Bedrock::MOV32idx4lrm:
        return TestOpcode == Bedrock::TEST32rr ? Bedrock::CMP32mi : 0;
      case Bedrock::MOV64rm:
      case Bedrock::MOV64idx1rm:
      case Bedrock::MOV64idx4rm:
      case Bedrock::MOV64idx4lrm:
        return TestOpcode == Bedrock::TEST64rr ? Bedrock::CMP64mi : 0;
      }
    };

    auto IsSelfTest = [&](MachineBasicBlock::iterator TestI,
                          unsigned &Opcode) {
      if (TestI == MBB.end() || TestI->getNumOperands() < 2 ||
          !TestI->getOperand(0).isReg() || !TestI->getOperand(1).isReg() ||
          !regsOverlap(TRI, TestI->getOperand(0).getReg(), Loaded) ||
          !regsOverlap(TRI, TestI->getOperand(1).getReg(), Loaded))
        return false;
      Opcode = ZeroCmpOpcodeForSelfTest(TestI->getOpcode(), Load.getOpcode());
      return Opcode != 0;
    };

    auto TestI = nextNonDebug(I, MBB);
    unsigned SelfTestOpcode = 0;
    while (TestI != MBB.end() && !IsSelfTest(TestI, SelfTestOpcode) &&
           CanSkip(*TestI))
      TestI = nextNonDebug(TestI, MBB);
    if (TestI != MBB.end() && IsSelfTest(TestI, SelfTestOpcode) &&
        (operandIsKill(*TestI, Loaded, TRI) ||
         regUnusedAfterInCFG(std::next(TestI), MBB, Loaded, TRI))) {
      auto ConsumerI = nextNonDebug(TestI, MBB);
      while (ConsumerI != MBB.end() && !getCondOperand(*ConsumerI) &&
             !instrTouchesReg(*ConsumerI, Bedrock::FLAGS, TRI))
        ConsumerI = nextNonDebug(ConsumerI, MBB);

      MachineOperand *CondOp =
          ConsumerI == MBB.end() ? nullptr : getCondOperand(*ConsumerI);
      std::optional<int64_t> CC =
          CondOp ? getCondCodeImm(*CondOp) : std::nullopt;
      if (CC && (*CC == BedrockCC::EQ || *CC == BedrockCC::NE) &&
          regDeadAfterInCFG(std::next(ConsumerI), MBB, Bedrock::FLAGS, TRI)) {
        unsigned Opcode = SelfTestOpcode;
        if (IndexedLoad)
          Opcode = getIndexedMemImmFlagOpcode(Opcode, Scale, LongIndex);
        if (Opcode != 0) {
          MachineInstrBuilder MIB =
              BuildMI(MBB, Load.getIterator(), TestI->getDebugLoc(),
                      TII.get(Opcode))
                  .addImm(0)
                  .addReg(Base);
          if (IndexedLoad)
            MIB.addReg(Index);
          MIB.addImm(Offset);
          MIB.cloneMemRefs(Load);

          Load.eraseFromParent();
          TestI->eraseFromParent();
          I = MBB.begin();
          Changed = true;
          continue;
        }
      }
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

bool BedrockPeephole::foldMemoryImmFlagOp(MachineBasicBlock &MBB,
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
    if (!IndexedLoad && !isMemLoad(Load, Loaded, Base, Offset)) {
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
           CanSkip(*CmpI, ArrayRef<Register>({Loaded, AndDst, Index}))) {
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
        unsigned EmitOpcode = TestOpcode;
        if (IndexedLoad)
          EmitOpcode = getIndexedMemImmFlagOpcode(TestOpcode, Scale, LongIndex);
        if (EmitOpcode == 0) {
          ++I;
          continue;
        }

        MachineInstrBuilder MIB =
            BuildMI(MBB, Load.getIterator(), CmpI->getDebugLoc(),
                    TII.get(EmitOpcode))
                .addImm(Mask)
                .addReg(Base);
        if (IndexedLoad)
          MIB.addReg(Index);
        MIB.addImm(Offset);

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
           CanSkip(*FlagI, ArrayRef<Register>({Loaded, Index})))
      FlagI = nextNonDebug(FlagI, MBB);
    if (FlagI == MBB.end()) {
      ++I;
      continue;
    }
    unsigned Opcode = getMemImmFlagOpcode(FlagI->getOpcode());
    bool OpcodeMatchesLoad =
        IndexedLoad
            ? flagImmOpcodeMatchesIndexedLoad(FlagI->getOpcode(),
                                              Load.getOpcode())
            : flagImmOpcodeMatchesLoad(FlagI->getOpcode(), Load.getOpcode());
    if (Opcode == 0 || !OpcodeMatchesLoad ||
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

    if (IndexedLoad)
      Opcode = getIndexedMemImmFlagOpcode(Opcode, Scale, LongIndex);
    if (Opcode == 0) {
      ++I;
      continue;
    }

    MachineInstrBuilder MIB =
        BuildMI(MBB, Load.getIterator(), FlagI->getDebugLoc(), TII.get(Opcode))
            .addImm(Imm)
            .addReg(Base);
    if (IndexedLoad)
      MIB.addReg(Index);
    MIB.addImm(Offset);

    Load.eraseFromParent();
    FlagI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldKnownZeroByteStoreToBSet(
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

bool BedrockPeephole::foldIndexedZeroStore(MachineBasicBlock &MBB,
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

static unsigned getLoadExtMemOpcode(unsigned LoadOpcode, unsigned ExtOpcode) {
  switch (ExtOpcode) {
  default:
    return 0;
  case Bedrock::EXTZL8rr:
    return LoadOpcode == Bedrock::MOV8rm ? Bedrock::EXTZL8rm : 0;
  case Bedrock::EXTZL16rr:
    return LoadOpcode == Bedrock::MOV16rm ? Bedrock::EXTZL16rm : 0;
  case Bedrock::EXTZQ8rr:
    return LoadOpcode == Bedrock::MOV8rm ? Bedrock::EXTZQ8rm : 0;
  case Bedrock::EXTZQ16rr:
    return LoadOpcode == Bedrock::MOV16rm ? Bedrock::EXTZQ16rm : 0;
  case Bedrock::EXTZQ32rr:
    return LoadOpcode == Bedrock::MOV32rm ? Bedrock::EXTZQ32rm : 0;
  case Bedrock::EXTSL8rr:
    return LoadOpcode == Bedrock::MOV8rm ? Bedrock::EXTSL8rm : 0;
  case Bedrock::EXTSL16rr:
    return LoadOpcode == Bedrock::MOV16rm ? Bedrock::EXTSL16rm : 0;
  case Bedrock::EXTSQ8rr:
    return LoadOpcode == Bedrock::MOV8rm ? Bedrock::EXTSQ8rm : 0;
  case Bedrock::EXTSQ16rr:
    return LoadOpcode == Bedrock::MOV16rm ? Bedrock::EXTSQ16rm : 0;
  case Bedrock::EXTSQ32rr:
    return LoadOpcode == Bedrock::MOV32rm ? Bedrock::EXTSQ32rm : 0;
  }
}

bool BedrockPeephole::foldLoadExt(MachineBasicBlock &MBB,
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

    Register Loaded, Base;
    int64_t Offset = 0;
    if (!isMemLoad(Load, Loaded, Base, Offset)) {
      ++I;
      continue;
    }

    auto ExtI = nextNonDebug(I, MBB);
    if (ExtI == MBB.end() || ExtI->getNumOperands() < 2 ||
        !ExtI->getOperand(0).isReg() || !ExtI->getOperand(1).isReg() ||
        !regsOverlap(TRI, ExtI->getOperand(1).getReg(), Loaded)) {
      ++I;
      continue;
    }

    unsigned NewOpcode = getLoadExtMemOpcode(Load.getOpcode(), ExtI->getOpcode());
    if (NewOpcode == 0) {
      ++I;
      continue;
    }

    Register Dst = ExtI->getOperand(0).getReg();
    if (!regsOverlap(TRI, Dst, Loaded) &&
        !regUnusedAfterInCFG(std::next(ExtI), MBB, Loaded, TRI)) {
      ++I;
      continue;
    }

    MachineInstrBuilder MIB =
        BuildMI(MBB, Load.getIterator(), ExtI->getDebugLoc(),
                TII.get(NewOpcode), Dst)
            .addReg(Base, getKillRegState(operandIsKill(Load, Base, TRI)))
            .addImm(Offset);
    MIB.cloneMemRefs(Load);
    MIB.setMIFlags(Load.getFlags() | ExtI->getFlags());

    Load.eraseFromParent();
    ExtI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

static unsigned getAbsMemRMOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::MOV8rm:
    return Bedrock::MOV8absrm;
  case Bedrock::MOV16rm:
    return Bedrock::MOV16absrm;
  case Bedrock::MOV32rm:
    return Bedrock::MOV32absrm;
  case Bedrock::MOV64rm:
    return Bedrock::MOV64absrm;
  case Bedrock::FMOV32rm:
    return Bedrock::FMOV32absrm;
  case Bedrock::FMOV64rm:
    return Bedrock::FMOV64absrm;
  case Bedrock::EXTZQ8rm:
    return Bedrock::EXTZQ8absrm;
  case Bedrock::EXTZQ16rm:
    return Bedrock::EXTZQ16absrm;
  case Bedrock::EXTZQ32rm:
    return Bedrock::EXTZQ32absrm;
  case Bedrock::EXTSQ8rm:
    return Bedrock::EXTSQ8absrm;
  case Bedrock::EXTSQ16rm:
    return Bedrock::EXTSQ16absrm;
  case Bedrock::EXTSQ32rm:
    return Bedrock::EXTSQ32absrm;
  case Bedrock::EXTZL8rm:
    return Bedrock::EXTZL8absrm;
  case Bedrock::EXTZL16rm:
    return Bedrock::EXTZL16absrm;
  case Bedrock::EXTSL8rm:
    return Bedrock::EXTSL8absrm;
  case Bedrock::EXTSL16rm:
    return Bedrock::EXTSL16absrm;
  }
}

static unsigned getAbsMemMROpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::MOV8mr:
    return Bedrock::MOV8absmr;
  case Bedrock::MOV16mr:
    return Bedrock::MOV16absmr;
  case Bedrock::MOV32mr:
    return Bedrock::MOV32absmr;
  case Bedrock::MOV64mr:
    return Bedrock::MOV64absmr;
  case Bedrock::FMOV32mr:
    return Bedrock::FMOV32absmr;
  case Bedrock::FMOV64mr:
    return Bedrock::FMOV64absmr;
  case Bedrock::EXTZQ8mr:
    return Bedrock::EXTZQ8absmr;
  case Bedrock::EXTZQ16mr:
    return Bedrock::EXTZQ16absmr;
  case Bedrock::EXTZQ32mr:
    return Bedrock::EXTZQ32absmr;
  case Bedrock::EXTSQ8mr:
    return Bedrock::EXTSQ8absmr;
  case Bedrock::EXTSQ16mr:
    return Bedrock::EXTSQ16absmr;
  case Bedrock::EXTSQ32mr:
    return Bedrock::EXTSQ32absmr;
  case Bedrock::EXTZL8mr:
    return Bedrock::EXTZL8absmr;
  case Bedrock::EXTZL16mr:
    return Bedrock::EXTZL16absmr;
  case Bedrock::EXTSL8mr:
    return Bedrock::EXTSL8absmr;
  case Bedrock::EXTSL16mr:
    return Bedrock::EXTSL16absmr;
  }
}

static unsigned getAbsMemMIOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::MOV8mi:
    return Bedrock::MOV8absmi;
  case Bedrock::MOV16mi:
    return Bedrock::MOV16absmi;
  case Bedrock::MOV32mi:
    return Bedrock::MOV32absmi;
  case Bedrock::MOV64mi:
    return Bedrock::MOV64absmi;
  }
}

static unsigned getAbsMemFlagMIOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::CMP8mi:
    return Bedrock::CMP8absmi;
  case Bedrock::CMP16mi:
    return Bedrock::CMP16absmi;
  case Bedrock::CMP32mi:
    return Bedrock::CMP32absmi;
  case Bedrock::CMP64mi:
    return Bedrock::CMP64absmi;
  case Bedrock::TEST8mi:
    return Bedrock::TEST8absmi;
  case Bedrock::TEST16mi:
    return Bedrock::TEST16absmi;
  case Bedrock::TEST32mi:
    return Bedrock::TEST32absmi;
  case Bedrock::TEST64mi:
    return Bedrock::TEST64absmi;
  }
}

static unsigned matchAbsMemMRFold(const MachineInstr &MI, Register &Src,
                                  Register &Base, int64_t &Offset) {
  unsigned NewOpcode = getAbsMemMROpcode(MI.getOpcode());
  if (NewOpcode == 0 || MI.getNumOperands() < 3 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
      !MI.getOperand(2).isImm())
    return 0;

  Src = MI.getOperand(0).getReg();
  Base = MI.getOperand(1).getReg();
  Offset = MI.getOperand(2).getImm();
  return NewOpcode;
}

static unsigned matchAbsMemMIFold(const MachineInstr &MI, int64_t &Imm,
                                  Register &Base, int64_t &Offset) {
  unsigned NewOpcode = getAbsMemMIOpcode(MI.getOpcode());
  if (NewOpcode == 0 || MI.getNumOperands() < 3 ||
      !MI.getOperand(0).isImm() || !MI.getOperand(1).isReg() ||
      !MI.getOperand(2).isImm())
    return 0;

  Imm = MI.getOperand(0).getImm();
  Base = MI.getOperand(1).getReg();
  Offset = MI.getOperand(2).getImm();
  return NewOpcode;
}

static unsigned matchAbsMemFlagMIFold(const MachineInstr &MI, int64_t &Imm,
                                      Register &Base, int64_t &Offset) {
  unsigned NewOpcode = getAbsMemFlagMIOpcode(MI.getOpcode());
  if (NewOpcode == 0 || MI.getNumOperands() < 3 ||
      !MI.getOperand(0).isImm() || !MI.getOperand(1).isReg() ||
      !MI.getOperand(2).isImm())
    return 0;

  Imm = MI.getOperand(0).getImm();
  Base = MI.getOperand(1).getReg();
  Offset = MI.getOperand(2).getImm();
  return NewOpcode;
}

static unsigned getAbsMemMMOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::MOV8mm:
    return Bedrock::MOV8mabs;
  case Bedrock::MOV16mm:
    return Bedrock::MOV16mabs;
  case Bedrock::MOV32mm:
    return Bedrock::MOV32mabs;
  case Bedrock::MOV64mm:
    return Bedrock::MOV64mabs;
  }
}

static unsigned getAbsMemMMOpcodeForLoad(unsigned Opcode) {
  return getAbsMemMMOpcode(getMovMMOpcode(Opcode, /*SrcPost=*/false,
                                          /*DstPost=*/false));
}

static unsigned getAbsMemIncDecOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::INC8m:
    return Bedrock::INC8absm;
  case Bedrock::INC16m:
    return Bedrock::INC16absm;
  case Bedrock::INC32m:
    return Bedrock::INC32absm;
  case Bedrock::INC64m:
    return Bedrock::INC64absm;
  case Bedrock::DEC8m:
    return Bedrock::DEC8absm;
  case Bedrock::DEC16m:
    return Bedrock::DEC16absm;
  case Bedrock::DEC32m:
    return Bedrock::DEC32absm;
  case Bedrock::DEC64m:
    return Bedrock::DEC64absm;
  }
}

static MachineOperand absTargetWithMemOffset(const MachineOperand &Target,
                                             int64_t Offset) {
  MachineOperand Result(Target);
  Result.setOffset(Target.getOffset() + Offset);
  return Result;
}

static bool addSignedNoOverflow(int64_t LHS, int64_t RHS, int64_t &Result) {
  if ((RHS > 0 && LHS > std::numeric_limits<int64_t>::max() - RHS) ||
      (RHS < 0 && LHS < std::numeric_limits<int64_t>::min() - RHS))
    return false;
  Result = LHS + RHS;
  return true;
}

static bool mulSignedNoOverflow(int64_t LHS, int64_t RHS, int64_t &Result) {
  return !__builtin_mul_overflow(LHS, RHS, &Result);
}

static unsigned absImmStoreImmPayloadWords(int64_t Imm) {
  if (Imm >= std::numeric_limits<int16_t>::min() &&
      Imm <= std::numeric_limits<int16_t>::max())
    return 1;
  if (Imm >= std::numeric_limits<int32_t>::min() &&
      Imm <= std::numeric_limits<int32_t>::max())
    return 2;
  return 4;
}

static unsigned absImmStoreAddressPayloadWords(uint64_t Address) {
  return Address <= std::numeric_limits<uint32_t>::max() ? 2 : 4;
}

static bool absImmStoreFits(unsigned AddressPayloadWords, int64_t Imm) {
  constexpr unsigned MaxBedrockInstructionWords = 8;
  return 2 + absImmStoreImmPayloadWords(Imm) + AddressPayloadWords <=
         MaxBedrockInstructionWords;
}

static bool absImmStoreFitsTarget(const MachineOperand &Target, int64_t Imm) {
  if (Target.isImm())
    return absImmStoreFits(absImmStoreAddressPayloadWords(Target.getImm()),
                           Imm);
  return absImmStoreFits(/*AddressPayloadWords=*/4, Imm);
}

bool BedrockPeephole::foldAbsMemoryOps(MachineBasicBlock &MBB,
                                       MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Addr = *I;
    if (Addr.isDebugInstr()) {
      ++I;
      continue;
    }

    if (Addr.getOpcode() != Bedrock::MOV64abs || Addr.getNumOperands() < 2 ||
        !Addr.getOperand(0).isReg() || !isAbsTargetOperand(Addr.getOperand(1))) {
      ++I;
      continue;
    }

    Register Base = Addr.getOperand(0).getReg();
    const MachineOperand &Target = Addr.getOperand(1);
    auto MemI = nextNonDebug(I, MBB);
    if (MemI == MBB.end()) {
      ++I;
      continue;
    }

    auto ReplaceAddrAndMem = [&](MachineInstrBuilder MIB,
                                 MachineInstr &Mem) {
      MIB.cloneMemRefs(Mem);
      Addr.eraseFromParent();
      Mem.eraseFromParent();
      I = MBB.begin();
      Changed = true;
    };

    {
      auto LoadI = prevNonDebug(I, MBB);
      Register Tmp, SrcBase;
      int64_t SrcOffset = 0;
      Register StoreSrc, DstBase;
      int64_t DstOffset = 0;
      unsigned NewOpcode =
          LoadI == MBB.end()
              ? 0
              : getAbsMemMMOpcodeForLoad(LoadI->getOpcode());
      if (NewOpcode != 0 &&
          isMemLoad(*LoadI, Tmp, SrcBase, SrcOffset) &&
          !hasOrderedMemOperand(*LoadI) &&
          MemI->getOpcode() == getStoreOpcodeForLoad(LoadI->getOpcode()) &&
          isMemStore(*MemI, StoreSrc, DstBase, DstOffset) &&
          regsOverlap(TRI, StoreSrc, Tmp) &&
          regsOverlap(TRI, DstBase, Base) &&
          (operandIsKill(*MemI, Tmp, TRI) ||
           regUnusedAfterInCFG(std::next(MemI), MBB, Tmp, TRI)) &&
          regUnusedAfterInCFG(std::next(MemI), MBB, Base, TRI)) {
        MachineInstrBuilder MIB =
            BuildMI(MBB, LoadI, MemI->getDebugLoc(), TII.get(NewOpcode))
                .addReg(SrcBase,
                        getKillRegState(operandIsKill(*LoadI, SrcBase, TRI)))
                .addImm(SrcOffset)
                .add(absTargetWithMemOffset(Target, DstOffset));
        MIB.cloneMergedMemRefs({&*LoadI, &*MemI});
        LoadI->eraseFromParent();
        Addr.eraseFromParent();
        MemI->eraseFromParent();
        I = MBB.begin();
        Changed = true;
        continue;
      }
    }

    if (MemI->getOpcode() == Bedrock::MOV64mr && MemI->getNumOperands() >= 3 &&
        MemI->getOperand(0).isReg() && MemI->getOperand(1).isReg() &&
        MemI->getOperand(2).isImm() &&
        regsOverlap(TRI, MemI->getOperand(0).getReg(), Base) &&
        !regsOverlap(TRI, MemI->getOperand(1).getReg(), Base) &&
        (operandIsKill(*MemI, Base, TRI) ||
         regUnusedAfterInCFG(std::next(MemI), MBB, Base, TRI))) {
      MachineInstrBuilder MIB =
          BuildMI(MBB, Addr.getIterator(), MemI->getDebugLoc(),
                  TII.get(Bedrock::MOV64symmr))
              .add(Target)
              .addReg(MemI->getOperand(1).getReg())
              .addImm(MemI->getOperand(2).getImm());
      ReplaceAddrAndMem(MIB, *MemI);
      continue;
    }

    if (!isAReg(Base)) {
      ++I;
      continue;
    }

    Register Dst, MemBase;
    int64_t Offset = 0;
    if (isMemLoad(*MemI, Dst, MemBase, Offset) &&
        regsOverlap(TRI, MemBase, Base)) {
      unsigned NewOpcode = getAbsMemRMOpcode(MemI->getOpcode());
      if (NewOpcode != 0 &&
          (regsOverlap(TRI, Dst, Base) ||
           regUnusedAfterInCFG(std::next(MemI), MBB, Base, TRI))) {
        MachineInstrBuilder MIB =
            BuildMI(MBB, Addr.getIterator(), MemI->getDebugLoc(),
                    TII.get(NewOpcode), Dst)
                .add(absTargetWithMemOffset(Target, Offset));
        ReplaceAddrAndMem(MIB, *MemI);
        continue;
      }
    }

    if (unsigned NewOpcode = getAbsMemRMOpcode(MemI->getOpcode())) {
      if (MemI->getNumOperands() >= 3 && MemI->getOperand(0).isReg() &&
          MemI->getOperand(1).isReg() && MemI->getOperand(2).isImm() &&
          regsOverlap(TRI, MemI->getOperand(1).getReg(), Base)) {
        Dst = MemI->getOperand(0).getReg();
        Offset = MemI->getOperand(2).getImm();
        if (regsOverlap(TRI, Dst, Base) ||
            regUnusedAfterInCFG(std::next(MemI), MBB, Base, TRI)) {
          MachineInstrBuilder MIB =
              BuildMI(MBB, Addr.getIterator(), MemI->getDebugLoc(),
                      TII.get(NewOpcode), Dst)
                  .add(absTargetWithMemOffset(Target, Offset));
          ReplaceAddrAndMem(MIB, *MemI);
          continue;
        }
      }
    }

    Register Src;
    if (unsigned NewOpcode =
            matchAbsMemMRFold(*MemI, Src, MemBase, Offset)) {
      if (regsOverlap(TRI, MemBase, Base) && !regsOverlap(TRI, Src, Base) &&
          regUnusedAfterInCFG(std::next(MemI), MBB, Base, TRI)) {
        MachineInstrBuilder MIB =
            BuildMI(MBB, Addr.getIterator(), MemI->getDebugLoc(),
                    TII.get(NewOpcode))
                .addReg(Src,
                        getKillRegState(operandIsKill(*MemI, Src, TRI)))
                .add(absTargetWithMemOffset(Target, Offset));
        ReplaceAddrAndMem(MIB, *MemI);
        continue;
      }
    }

    int64_t StoreImm = 0;
    if (unsigned NewOpcode =
            matchAbsMemMIFold(*MemI, StoreImm, MemBase, Offset)) {
      if (regsOverlap(TRI, MemBase, Base) &&
          absImmStoreFitsTarget(absTargetWithMemOffset(Target, Offset),
                                StoreImm) &&
          regUnusedAfterInCFG(std::next(MemI), MBB, Base, TRI)) {
        MachineInstrBuilder MIB =
            BuildMI(MBB, Addr.getIterator(), MemI->getDebugLoc(),
                    TII.get(NewOpcode))
                .addImm(StoreImm)
                .add(absTargetWithMemOffset(Target, Offset));
        ReplaceAddrAndMem(MIB, *MemI);
        continue;
      }
    }

    int64_t FlagImm = 0;
    if (unsigned NewOpcode =
            matchAbsMemFlagMIFold(*MemI, FlagImm, MemBase, Offset)) {
      if (regsOverlap(TRI, MemBase, Base) &&
          regUnusedAfterInCFG(std::next(MemI), MBB, Base, TRI)) {
        MachineInstrBuilder MIB =
            BuildMI(MBB, Addr.getIterator(), MemI->getDebugLoc(),
                    TII.get(NewOpcode))
                .addImm(FlagImm)
                .add(absTargetWithMemOffset(Target, Offset));
        ReplaceAddrAndMem(MIB, *MemI);
        continue;
      }
    }

    unsigned NewMMOpcode = getAbsMemMMOpcode(MemI->getOpcode());
    if (NewMMOpcode != 0 && MemI->getNumOperands() >= 4 &&
        MemI->getOperand(0).isReg() && MemI->getOperand(1).isImm() &&
        MemI->getOperand(2).isReg() && MemI->getOperand(3).isImm() &&
        regsOverlap(TRI, MemI->getOperand(2).getReg(), Base) &&
        !regsOverlap(TRI, MemI->getOperand(0).getReg(), Base) &&
        regUnusedAfterInCFG(std::next(MemI), MBB, Base, TRI)) {
      MachineInstrBuilder MIB =
          BuildMI(MBB, Addr.getIterator(), MemI->getDebugLoc(),
                  TII.get(NewMMOpcode))
              .addReg(MemI->getOperand(0).getReg(),
                      getKillRegState(operandIsKill(
                          *MemI, MemI->getOperand(0).getReg(), TRI)))
              .addImm(MemI->getOperand(1).getImm())
              .add(absTargetWithMemOffset(Target,
                                          MemI->getOperand(3).getImm()));
      ReplaceAddrAndMem(MIB, *MemI);
      continue;
    }

    unsigned NewIncDecOpcode = getAbsMemIncDecOpcode(MemI->getOpcode());
    if (NewIncDecOpcode != 0 && MemI->getNumOperands() >= 2 &&
        MemI->getOperand(0).isReg() && MemI->getOperand(1).isImm() &&
        regsOverlap(TRI, MemI->getOperand(0).getReg(), Base) &&
        regUnusedAfterInCFG(std::next(MemI), MBB, Base, TRI)) {
      MachineInstrBuilder MIB =
          BuildMI(MBB, Addr.getIterator(), MemI->getDebugLoc(),
                  TII.get(NewIncDecOpcode))
              .add(absTargetWithMemOffset(Target,
                                          MemI->getOperand(1).getImm()));
      ReplaceAddrAndMem(MIB, *MemI);
      continue;
    }

    ++I;
  }

  return Changed;
}

static bool matchDirectAbsMemoryOp(MachineInstr &MI,
                                   DirectAbsMemRunEntry &Entry) {
  Entry = DirectAbsMemRunEntry();
  Entry.MI = &MI;

  auto Set = [&](unsigned NewOpcode, DirectAbsMemShape Shape,
                 unsigned TargetIdx) {
    if (MI.getNumOperands() <= TargetIdx ||
        !isAbsTargetOperand(MI.getOperand(TargetIdx)))
      return false;
    Entry.NewOpcode = NewOpcode;
    Entry.Shape = Shape;
    Entry.Target = &MI.getOperand(TargetIdx);
    return true;
  };

  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::MOV8absrm:
    return Set(Bedrock::MOV8rm, DirectAbsMemShape::Load, 1);
  case Bedrock::MOV16absrm:
    return Set(Bedrock::MOV16rm, DirectAbsMemShape::Load, 1);
  case Bedrock::MOV32absrm:
    return Set(Bedrock::MOV32rm, DirectAbsMemShape::Load, 1);
  case Bedrock::MOV64absrm:
    return Set(Bedrock::MOV64rm, DirectAbsMemShape::Load, 1);
  case Bedrock::FMOV32absrm:
    return Set(Bedrock::FMOV32rm, DirectAbsMemShape::Load, 1);
  case Bedrock::FMOV64absrm:
    return Set(Bedrock::FMOV64rm, DirectAbsMemShape::Load, 1);
  case Bedrock::EXTZQ8absrm:
    return Set(Bedrock::EXTZQ8rm, DirectAbsMemShape::Load, 1);
  case Bedrock::EXTZQ16absrm:
    return Set(Bedrock::EXTZQ16rm, DirectAbsMemShape::Load, 1);
  case Bedrock::EXTZQ32absrm:
    return Set(Bedrock::EXTZQ32rm, DirectAbsMemShape::Load, 1);
  case Bedrock::EXTSQ8absrm:
    return Set(Bedrock::EXTSQ8rm, DirectAbsMemShape::Load, 1);
  case Bedrock::EXTSQ16absrm:
    return Set(Bedrock::EXTSQ16rm, DirectAbsMemShape::Load, 1);
  case Bedrock::EXTSQ32absrm:
    return Set(Bedrock::EXTSQ32rm, DirectAbsMemShape::Load, 1);
  case Bedrock::EXTZL8absrm:
    return Set(Bedrock::EXTZL8rm, DirectAbsMemShape::Load, 1);
  case Bedrock::EXTZL16absrm:
    return Set(Bedrock::EXTZL16rm, DirectAbsMemShape::Load, 1);
  case Bedrock::EXTSL8absrm:
    return Set(Bedrock::EXTSL8rm, DirectAbsMemShape::Load, 1);
  case Bedrock::EXTSL16absrm:
    return Set(Bedrock::EXTSL16rm, DirectAbsMemShape::Load, 1);

  case Bedrock::MOV8absmr:
    return Set(Bedrock::MOV8mr, DirectAbsMemShape::Store, 1);
  case Bedrock::MOV16absmr:
    return Set(Bedrock::MOV16mr, DirectAbsMemShape::Store, 1);
  case Bedrock::MOV32absmr:
    return Set(Bedrock::MOV32mr, DirectAbsMemShape::Store, 1);
  case Bedrock::MOV64absmr:
    return Set(Bedrock::MOV64mr, DirectAbsMemShape::Store, 1);
  case Bedrock::FMOV32absmr:
    return Set(Bedrock::FMOV32mr, DirectAbsMemShape::Store, 1);
  case Bedrock::FMOV64absmr:
    return Set(Bedrock::FMOV64mr, DirectAbsMemShape::Store, 1);
  case Bedrock::EXTZQ8absmr:
    return Set(Bedrock::EXTZQ8mr, DirectAbsMemShape::Store, 1);
  case Bedrock::EXTZQ16absmr:
    return Set(Bedrock::EXTZQ16mr, DirectAbsMemShape::Store, 1);
  case Bedrock::EXTZQ32absmr:
    return Set(Bedrock::EXTZQ32mr, DirectAbsMemShape::Store, 1);
  case Bedrock::EXTSQ8absmr:
    return Set(Bedrock::EXTSQ8mr, DirectAbsMemShape::Store, 1);
  case Bedrock::EXTSQ16absmr:
    return Set(Bedrock::EXTSQ16mr, DirectAbsMemShape::Store, 1);
  case Bedrock::EXTSQ32absmr:
    return Set(Bedrock::EXTSQ32mr, DirectAbsMemShape::Store, 1);
  case Bedrock::EXTZL8absmr:
    return Set(Bedrock::EXTZL8mr, DirectAbsMemShape::Store, 1);
  case Bedrock::EXTZL16absmr:
    return Set(Bedrock::EXTZL16mr, DirectAbsMemShape::Store, 1);
  case Bedrock::EXTSL8absmr:
    return Set(Bedrock::EXTSL8mr, DirectAbsMemShape::Store, 1);
  case Bedrock::EXTSL16absmr:
    return Set(Bedrock::EXTSL16mr, DirectAbsMemShape::Store, 1);

  case Bedrock::MOV8absmi:
    return Set(Bedrock::MOV8mi, DirectAbsMemShape::ImmMem, 1);
  case Bedrock::MOV16absmi:
    return Set(Bedrock::MOV16mi, DirectAbsMemShape::ImmMem, 1);
  case Bedrock::MOV32absmi:
    return Set(Bedrock::MOV32mi, DirectAbsMemShape::ImmMem, 1);
  case Bedrock::MOV64absmi:
    return Set(Bedrock::MOV64mi, DirectAbsMemShape::ImmMem, 1);
  case Bedrock::CMP8absmi:
    return Set(Bedrock::CMP8mi, DirectAbsMemShape::ImmMem, 1);
  case Bedrock::CMP16absmi:
    return Set(Bedrock::CMP16mi, DirectAbsMemShape::ImmMem, 1);
  case Bedrock::CMP32absmi:
    return Set(Bedrock::CMP32mi, DirectAbsMemShape::ImmMem, 1);
  case Bedrock::CMP64absmi:
    return Set(Bedrock::CMP64mi, DirectAbsMemShape::ImmMem, 1);
  case Bedrock::TEST8absmi:
    return Set(Bedrock::TEST8mi, DirectAbsMemShape::ImmMem, 1);
  case Bedrock::TEST16absmi:
    return Set(Bedrock::TEST16mi, DirectAbsMemShape::ImmMem, 1);
  case Bedrock::TEST32absmi:
    return Set(Bedrock::TEST32mi, DirectAbsMemShape::ImmMem, 1);
  case Bedrock::TEST64absmi:
    return Set(Bedrock::TEST64mi, DirectAbsMemShape::ImmMem, 1);

  case Bedrock::MOV8mabs:
    return Set(Bedrock::MOV8mm, DirectAbsMemShape::MemToAbs, 2);
  case Bedrock::MOV16mabs:
    return Set(Bedrock::MOV16mm, DirectAbsMemShape::MemToAbs, 2);
  case Bedrock::MOV32mabs:
    return Set(Bedrock::MOV32mm, DirectAbsMemShape::MemToAbs, 2);
  case Bedrock::MOV64mabs:
    return Set(Bedrock::MOV64mm, DirectAbsMemShape::MemToAbs, 2);

  case Bedrock::INC8absm:
    return Set(Bedrock::INC8m, DirectAbsMemShape::IncDec, 0);
  case Bedrock::INC16absm:
    return Set(Bedrock::INC16m, DirectAbsMemShape::IncDec, 0);
  case Bedrock::INC32absm:
    return Set(Bedrock::INC32m, DirectAbsMemShape::IncDec, 0);
  case Bedrock::INC64absm:
    return Set(Bedrock::INC64m, DirectAbsMemShape::IncDec, 0);
  case Bedrock::DEC8absm:
    return Set(Bedrock::DEC8m, DirectAbsMemShape::IncDec, 0);
  case Bedrock::DEC16absm:
    return Set(Bedrock::DEC16m, DirectAbsMemShape::IncDec, 0);
  case Bedrock::DEC32absm:
    return Set(Bedrock::DEC32m, DirectAbsMemShape::IncDec, 0);
  case Bedrock::DEC64absm:
    return Set(Bedrock::DEC64m, DirectAbsMemShape::IncDec, 0);
  }
}

static void rewriteDirectAbsMemoryOp(MachineBasicBlock &MBB,
                                     const BedrockInstrInfo &TII,
                                     const DirectAbsMemRunEntry &Entry,
                                     Register Base, int64_t Offset) {
  MachineInstr &Old = *Entry.MI;
  MachineInstrBuilder MIB;
  switch (Entry.Shape) {
  case DirectAbsMemShape::Load:
    MIB = BuildMI(MBB, Old.getIterator(), Old.getDebugLoc(),
                  TII.get(Entry.NewOpcode), Old.getOperand(0).getReg())
              .addReg(Base)
              .addImm(Offset);
    break;
  case DirectAbsMemShape::Store:
  case DirectAbsMemShape::ImmMem:
    MIB = BuildMI(MBB, Old.getIterator(), Old.getDebugLoc(),
                  TII.get(Entry.NewOpcode))
              .add(Old.getOperand(0))
              .addReg(Base)
              .addImm(Offset);
    break;
  case DirectAbsMemShape::MemToAbs:
    MIB = BuildMI(MBB, Old.getIterator(), Old.getDebugLoc(),
                  TII.get(Entry.NewOpcode))
              .add(Old.getOperand(0))
              .add(Old.getOperand(1))
              .addReg(Base)
              .addImm(Offset);
    break;
  case DirectAbsMemShape::IncDec:
    MIB = BuildMI(MBB, Old.getIterator(), Old.getDebugLoc(),
                  TII.get(Entry.NewOpcode))
              .addReg(Base)
              .addImm(Offset);
    break;
  }
  MIB.cloneMemRefs(Old);
  MIB.setMIFlags(Old.getFlags());
}

bool BedrockPeephole::foldDirectAbsMemoryRuns(MachineBasicBlock &MBB,
                                              MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  SmallVector<int64_t, 8> ConstantPoolOffsets;
  if (const MachineConstantPool *MCP = MF.getConstantPool()) {
    const auto &Constants = MCP->getConstants();
    ConstantPoolOffsets.reserve(Constants.size());
    int64_t Offset = 0;
    for (const MachineConstantPoolEntry &Entry : Constants) {
      Offset = alignTo(Offset, Entry.getAlign().value());
      ConstantPoolOffsets.push_back(Offset);
      Offset += Entry.getSizeInBytes(MF.getDataLayout());
    }
  }

  auto sameRunTarget = [&](const MachineOperand &A, const MachineOperand &B) {
    if (A.isCPI() && B.isCPI())
      return true;
    return sameAbsTargetIgnoringOffset(A, B);
  };

  auto getRunTargetOffset = [&](const MachineOperand &Target,
                                int64_t &Offset) {
    if (Target.isCPI()) {
      unsigned Index = Target.getIndex();
      if (Index >= ConstantPoolOffsets.size())
        return false;
      return addSignedNoOverflow(ConstantPoolOffsets[Index], Target.getOffset(),
                                 Offset);
    }
    Offset = Target.getOffset();
    return true;
  };

  for (auto I = MBB.begin(); I != MBB.end();) {
    if (I->isDebugInstr()) {
      ++I;
      continue;
    }

    DirectAbsMemRunEntry First;
    if (!matchDirectAbsMemoryOp(*I, First)) {
      ++I;
      continue;
    }

    Register Base = findScratchARegAt(I, MBB, TRI);
    if (!Base) {
      ++I;
      continue;
    }

    int64_t BaseOffset = 0;
    if (!getRunTargetOffset(*First.Target, BaseOffset)) {
      ++I;
      continue;
    }
    SmallVector<DirectAbsMemRunEntry, 8> Entries;
    Entries.push_back(First);

    for (auto Scan = nextNonDebug(I, MBB); Scan != MBB.end();
         Scan = nextNonDebug(Scan, MBB)) {
      if (Scan->isTerminator() || Scan->isCall() ||
          instrTouchesReg(*Scan, Base, TRI) ||
          instrHasRegMaskForReg(*Scan, Base, TRI))
        break;

      DirectAbsMemRunEntry Next;
      if (!matchDirectAbsMemoryOp(*Scan, Next))
        continue;
      if (!sameRunTarget(*First.Target, *Next.Target))
        continue;

      int64_t NextOffset = 0;
      if (!getRunTargetOffset(*Next.Target, NextOffset))
        continue;
      int64_t RelOffset = NextOffset - BaseOffset;
      if (!fitsDisp16(RelOffset))
        continue;
      Entries.push_back(Next);
    }

    if (Entries.size() < 2) {
      ++I;
      continue;
    }

    BuildMI(MBB, Entries.front().MI->getIterator(), Entries.front().MI->getDebugLoc(),
            TII.get(Bedrock::MOV64abs), Base)
        .add(*First.Target);

    for (const DirectAbsMemRunEntry &Entry : Entries)
      if (int64_t EntryOffset = 0; getRunTargetOffset(*Entry.Target, EntryOffset))
        rewriteDirectAbsMemoryOp(MBB, TII, Entry, Base, EntryOffset - BaseOffset);
    for (const DirectAbsMemRunEntry &Entry : Entries)
      Entry.MI->eraseFromParent();

    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldDirectAbsMemoryGlobalBases(MachineFunction &MF) const {
  if (MF.empty())
    return false;

  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  SmallVector<Register, 2> FreeBases;
  for (Register Reg = Bedrock::A6; Reg <= Bedrock::A7;
       Reg = Register(Reg + 1))
    if (!physRegUsedInFunction(MF, Reg, TRI))
      FreeBases.push_back(Reg);
  if (FreeBases.empty())
    return false;

  struct Candidate {
    const MachineOperand *Target = nullptr;
    int64_t MinOffset = 0;
    int64_t MaxOffset = 0;
    SmallVector<DirectAbsMemRunEntry, 16> Entries;
    unsigned Savings = 0;
    Register Base;
  };

  auto AddEntry = [](SmallVectorImpl<Candidate> &Candidates,
                     DirectAbsMemRunEntry Entry) {
    int64_t Offset = Entry.Target->getOffset();
    for (Candidate &Cand : Candidates) {
      if (!sameAbsTargetIgnoringOffset(*Cand.Target, *Entry.Target))
        continue;
      int64_t NewMin = std::min(Cand.MinOffset, Offset);
      int64_t NewMax = std::max(Cand.MaxOffset, Offset);
      if (!fitsDisp16(NewMax - NewMin))
        continue;
      Cand.MinOffset = NewMin;
      Cand.MaxOffset = NewMax;
      Cand.Entries.push_back(Entry);
      return;
    }

    Candidate Cand;
    Cand.Target = Entry.Target;
    Cand.MinOffset = Offset;
    Cand.MaxOffset = Offset;
    Cand.Entries.push_back(Entry);
    Candidates.push_back(Cand);
  };

  SmallVector<Candidate, 8> Candidates;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      DirectAbsMemRunEntry Entry;
      if (matchDirectAbsMemoryOp(MI, Entry))
        AddEntry(Candidates, Entry);
    }
  }

  if (Candidates.empty())
    return false;

  MachineInstr *PushM = nullptr;
  SmallVector<MachineInstr *, 4> PopMs;
  uint16_t PushPopMask = 0;
  bool HasPushPop = collectConsistentPushPopMask(MF, PushM, PopMs, PushPopMask);

  auto IsExit = [](const MachineBasicBlock &MBB, const MachineInstr &MI) {
    if (MI.getOpcode() == Bedrock::RET)
      return true;
    if (!MBB.succ_empty())
      return false;
    switch (MI.getOpcode()) {
    default:
      return false;
    case Bedrock::JMP:
    case Bedrock::JMPWpcrel:
    case Bedrock::JMPLpcrel:
      return true;
    }
  };

  SmallVector<MachineInstr *, 4> Exits;
  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB)
      if (!MI.isDebugInstr() && IsExit(MBB, MI))
        Exits.push_back(&MI);
  if (!HasPushPop && Exits.empty())
    return false;

  unsigned SaveCost = HasPushPop ? 0 : 2 + 2 * Exits.size();
  for (Candidate &Cand : Candidates) {
    if (Cand.Entries.size() < 2)
      continue;
    unsigned OldBytes = unsigned(Cand.Entries.size()) * 12;
    unsigned NewBytes = 10 + SaveCost + unsigned(Cand.Entries.size()) * 4;
    Cand.Savings = OldBytes > NewBytes ? OldBytes - NewBytes : 0;
  }

  llvm::sort(Candidates, [](const Candidate &L, const Candidate &R) {
    if (L.Savings != R.Savings)
      return L.Savings > R.Savings;
    return L.Entries.size() > R.Entries.size();
  });

  SmallVector<Candidate *, 2> Selected;
  for (Candidate &Cand : Candidates) {
    if (Selected.size() >= FreeBases.size())
      break;
    if (Cand.Savings == 0)
      break;
    Cand.Base = FreeBases[Selected.size()];
    Selected.push_back(&Cand);
  }
  if (Selected.empty())
    return false;

  uint16_t AddedMask = 0;
  for (const Candidate *Cand : Selected)
    if (std::optional<unsigned> Bit = getMaskBit(Cand->Base))
      AddedMask |= uint16_t(1) << *Bit;

  MachineBasicBlock &Entry = MF.front();
  MachineInstr *NewPush = nullptr;
  if (HasPushPop) {
    extendPushPopMask(MF, *PushM, PopMs, PushPopMask, AddedMask);
    for (MachineInstr &MI : Entry) {
      if (!MI.isDebugInstr() && MI.getFlag(MachineInstr::FrameSetup) &&
          pushPopMaskForInstr(MI)) {
        NewPush = &MI;
        break;
      }
    }
  } else {
    MachineBasicBlock::iterator Insert = Entry.begin();
    while (Insert != Entry.end() && Insert->isDebugInstr())
      ++Insert;
    DebugLoc DL = Insert != Entry.end() ? Insert->getDebugLoc() : DebugLoc();
    MachineInstrBuilder Push = buildPushForMask(Entry, Insert, DL, TII, AddedMask);
    Push.setMIFlag(MachineInstr::FrameSetup);
    NewPush = Push.getInstr();

    for (MachineInstr *Exit : Exits) {
      MachineBasicBlock &ExitMBB = *Exit->getParent();
      MachineInstrBuilder Pop =
          buildPopForMask(ExitMBB, Exit->getIterator(), Exit->getDebugLoc(), TII,
                          AddedMask);
      Pop.setMIFlag(MachineInstr::FrameDestroy);
    }
  }
  if (!NewPush)
    return false;

  MachineBasicBlock::iterator Insert = std::next(NewPush->getIterator());
  while (Insert != Entry.end() && Insert->isDebugInstr())
    ++Insert;
  for (const Candidate *Cand : Selected) {
    MachineOperand BaseTarget(*Cand->Target);
    BaseTarget.setOffset(Cand->MinOffset);
    BuildMI(Entry, Insert, NewPush->getDebugLoc(), TII.get(Bedrock::MOV64abs),
            Cand->Base)
        .add(BaseTarget);
  }

  for (Candidate *Cand : Selected) {
    for (const DirectAbsMemRunEntry &Entry : Cand->Entries) {
      MachineBasicBlock &MBB = *Entry.MI->getParent();
      rewriteDirectAbsMemoryOp(MBB, TII, Entry, Cand->Base,
                               Entry.Target->getOffset() - Cand->MinOffset);
    }
  }
  for (Candidate *Cand : Selected)
    for (const DirectAbsMemRunEntry &Entry : Cand->Entries)
      Entry.MI->eraseFromParent();

  for (Candidate *Cand : Selected) {
    if (!MF.front().isLiveIn(Cand->Base))
      MF.front().addLiveIn(Cand->Base);
    for (MachineBasicBlock &MBB : MF)
      if (&MBB != &MF.front() && !MBB.isLiveIn(Cand->Base))
        MBB.addLiveIn(Cand->Base);
  }

  return true;
}

bool BedrockPeephole::foldImmAbsMemoryOps(MachineBasicBlock &MBB,
                                          MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &AddrImm = *I;
    if (AddrImm.isDebugInstr()) {
      ++I;
      continue;
    }

    Register AddrD;
    int64_t AbsAddr = 0;
    if ((!isMov32Imm(AddrImm, AddrD, AbsAddr) &&
         !isMov64Imm(AddrImm, AddrD, AbsAddr)) ||
        !isDReg(AddrD)) {
      ++I;
      continue;
    }

    auto CopyI = nextNonDebug(I, MBB);
    if (CopyI == MBB.end() || CopyI->getOpcode() != Bedrock::MOV64rr ||
        CopyI->getNumOperands() < 2 || !CopyI->getOperand(0).isReg() ||
        !CopyI->getOperand(1).isReg() ||
        !regsOverlap(TRI, CopyI->getOperand(1).getReg(), AddrD) ||
        !isAReg(CopyI->getOperand(0).getReg())) {
      ++I;
      continue;
    }

    Register Base = CopyI->getOperand(0).getReg();
    bool AddrDStillHoldsAddress = true;
    MachineInstr *Mem = nullptr;
    auto Scan = nextNonDebug(CopyI, MBB);
    for (; Scan != MBB.end(); Scan = nextNonDebug(Scan, MBB)) {
      if (Scan->isTerminator() || Scan->isCall() ||
          instrHasRegMaskForReg(*Scan, Base, TRI))
        break;

      Register Dst, MemBase, Src;
      int64_t Offset = 0;
      bool IsBaseLoad =
          isMemLoad(*Scan, Dst, MemBase, Offset) &&
          regsOverlap(TRI, MemBase, Base);
      unsigned NewMROpcode =
          matchAbsMemMRFold(*Scan, Src, MemBase, Offset);
      bool IsBaseStore =
          NewMROpcode != 0 &&
          regsOverlap(TRI, MemBase, Base);
      int64_t StoreImm = 0;
      unsigned NewMIOpcode =
          matchAbsMemMIFold(*Scan, StoreImm, MemBase, Offset);
      bool IsBaseImmStore =
          NewMIOpcode != 0 &&
          regsOverlap(TRI, MemBase, Base);
      int64_t FlagImm = 0;
      unsigned NewFlagMIOpcode =
          matchAbsMemFlagMIFold(*Scan, FlagImm, MemBase, Offset);
      bool IsBaseImmFlag =
          NewFlagMIOpcode != 0 &&
          regsOverlap(TRI, MemBase, Base);
      unsigned NewMMOpcode = getAbsMemMMOpcode(Scan->getOpcode());
      bool IsBaseMemToMem =
          NewMMOpcode != 0 && Scan->getNumOperands() >= 4 &&
          Scan->getOperand(2).isReg() &&
          regsOverlap(TRI, Scan->getOperand(2).getReg(), Base);
      unsigned NewIncDecOpcode = getAbsMemIncDecOpcode(Scan->getOpcode());
      bool IsBaseIncDec =
          NewIncDecOpcode != 0 && Scan->getNumOperands() >= 2 &&
          Scan->getOperand(0).isReg() &&
          regsOverlap(TRI, Scan->getOperand(0).getReg(), Base);

      if (IsBaseLoad || IsBaseStore || IsBaseImmStore || IsBaseImmFlag ||
          IsBaseMemToMem || IsBaseIncDec) {
        Mem = &*Scan;
        break;
      }

      if (instrUsesReg(*Scan, Base, TRI) || instrDefinesReg(*Scan, Base, TRI))
        break;

      if (AddrDStillHoldsAddress && instrUsesReg(*Scan, AddrD, TRI)) {
        Mem = nullptr;
        break;
      }
      if (instrDefinesReg(*Scan, AddrD, TRI) ||
          instrHasRegMaskForReg(*Scan, AddrD, TRI))
        AddrDStillHoldsAddress = false;
    }

    if (!Mem) {
      ++I;
      continue;
    }

    if (AddrDStillHoldsAddress && instrUsesReg(*Mem, AddrD, TRI)) {
      ++I;
      continue;
    }

    if (!regUnusedAfterInCFG(std::next(Mem->getIterator()), MBB, Base, TRI)) {
      ++I;
      continue;
    }

    auto AbsAddress = [&](int64_t Offset) -> std::optional<int64_t> {
      int64_t FinalAddr = 0;
      if (!addSignedNoOverflow(AbsAddr, Offset, FinalAddr))
        return std::nullopt;
      return FinalAddr;
    };

    auto AbsOperand = [&](int64_t Offset) -> std::optional<MachineOperand> {
      std::optional<int64_t> FinalAddr = AbsAddress(Offset);
      if (!FinalAddr)
        return std::nullopt;
      return MachineOperand::CreateImm(*FinalAddr);
    };

    auto Replace = [&](MachineInstrBuilder MIB) {
      MIB.cloneMemRefs(*Mem);
      AddrImm.eraseFromParent();
      CopyI->eraseFromParent();
      Mem->eraseFromParent();
      I = MBB.begin();
      Changed = true;
    };

    Register Dst, MemBase;
    int64_t Offset = 0;
    if (isMemLoad(*Mem, Dst, MemBase, Offset) &&
        regsOverlap(TRI, MemBase, Base)) {
      unsigned NewOpcode = getAbsMemRMOpcode(Mem->getOpcode());
      std::optional<MachineOperand> Addr = AbsOperand(Offset);
      if (NewOpcode != 0 && Addr) {
        MachineInstrBuilder MIB =
            BuildMI(MBB, Mem->getIterator(), Mem->getDebugLoc(),
                    TII.get(NewOpcode), Dst)
                .add(*Addr);
        Replace(MIB);
        continue;
      }
    }

    if (unsigned NewOpcode = getAbsMemRMOpcode(Mem->getOpcode())) {
      if (Mem->getNumOperands() >= 3 && Mem->getOperand(0).isReg() &&
          Mem->getOperand(1).isReg() && Mem->getOperand(2).isImm() &&
          regsOverlap(TRI, Mem->getOperand(1).getReg(), Base)) {
        std::optional<MachineOperand> Addr =
            AbsOperand(Mem->getOperand(2).getImm());
        if (Addr) {
          MachineInstrBuilder MIB =
              BuildMI(MBB, Mem->getIterator(), Mem->getDebugLoc(),
                      TII.get(NewOpcode), Mem->getOperand(0).getReg())
                  .add(*Addr);
          Replace(MIB);
          continue;
        }
      }
    }

    Register Src;
    if (unsigned NewOpcode = matchAbsMemMRFold(*Mem, Src, MemBase, Offset)) {
      std::optional<MachineOperand> Addr = AbsOperand(Offset);
      if (regsOverlap(TRI, MemBase, Base) && Addr &&
          !regsOverlap(TRI, Src, Base)) {
        MachineInstrBuilder MIB =
            BuildMI(MBB, Mem->getIterator(), Mem->getDebugLoc(),
                    TII.get(NewOpcode))
                .addReg(Src, getKillRegState(operandIsKill(*Mem, Src, TRI)))
                .add(*Addr);
        Replace(MIB);
        continue;
      }
    }

    int64_t StoreImm = 0;
    if (unsigned NewOpcode =
            matchAbsMemMIFold(*Mem, StoreImm, MemBase, Offset)) {
      std::optional<int64_t> FinalAddr = AbsAddress(Offset);
      if (regsOverlap(TRI, MemBase, Base) && FinalAddr &&
          absImmStoreFits(absImmStoreAddressPayloadWords(*FinalAddr),
                          StoreImm)) {
        MachineInstrBuilder MIB =
            BuildMI(MBB, Mem->getIterator(), Mem->getDebugLoc(),
                    TII.get(NewOpcode))
                .addImm(StoreImm)
                .addImm(*FinalAddr);
        Replace(MIB);
        continue;
      }
    }

    int64_t FlagImm = 0;
    if (unsigned NewOpcode =
            matchAbsMemFlagMIFold(*Mem, FlagImm, MemBase, Offset)) {
      std::optional<MachineOperand> Addr = AbsOperand(Offset);
      if (regsOverlap(TRI, MemBase, Base) && Addr) {
        MachineInstrBuilder MIB =
            BuildMI(MBB, Mem->getIterator(), Mem->getDebugLoc(),
                    TII.get(NewOpcode))
                .addImm(FlagImm)
                .add(*Addr);
        Replace(MIB);
        continue;
      }
    }

    unsigned NewMMOpcode = getAbsMemMMOpcode(Mem->getOpcode());
    if (NewMMOpcode != 0 && Mem->getNumOperands() >= 4 &&
        Mem->getOperand(0).isReg() && Mem->getOperand(1).isImm() &&
        Mem->getOperand(2).isReg() && Mem->getOperand(3).isImm() &&
        regsOverlap(TRI, Mem->getOperand(2).getReg(), Base) &&
        !regsOverlap(TRI, Mem->getOperand(0).getReg(), Base)) {
      std::optional<MachineOperand> Addr = AbsOperand(Mem->getOperand(3).getImm());
      if (Addr) {
        MachineInstrBuilder MIB =
            BuildMI(MBB, Mem->getIterator(), Mem->getDebugLoc(),
                    TII.get(NewMMOpcode))
                .addReg(Mem->getOperand(0).getReg(),
                        getKillRegState(operandIsKill(
                            *Mem, Mem->getOperand(0).getReg(), TRI)))
                .addImm(Mem->getOperand(1).getImm())
                .add(*Addr);
        Replace(MIB);
        continue;
      }
    }

    unsigned NewIncDecOpcode = getAbsMemIncDecOpcode(Mem->getOpcode());
    if (NewIncDecOpcode != 0 && Mem->getNumOperands() >= 2 &&
        Mem->getOperand(0).isReg() && Mem->getOperand(1).isImm() &&
        regsOverlap(TRI, Mem->getOperand(0).getReg(), Base)) {
      std::optional<MachineOperand> Addr = AbsOperand(Mem->getOperand(1).getImm());
      if (Addr) {
        MachineInstrBuilder MIB =
            BuildMI(MBB, Mem->getIterator(), Mem->getDebugLoc(),
                    TII.get(NewIncDecOpcode))
                .add(*Addr);
        Replace(MIB);
        continue;
      }
    }

    ++I;
  }

  return Changed;
}

bool BedrockPeephole::foldByteLoadTestZeroBranch(
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

bool BedrockPeephole::foldByteLoadKnownZeroCmpBranch(
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

bool BedrockPeephole::foldImmStore(MachineBasicBlock &MBB,
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
    if (NewOpcode == 0 || StoreOpcode == 0 || MovImm.getNumOperands() < 2 ||
        !MovImm.getOperand(0).isReg() || !MovImm.getOperand(1).isImm()) {
      ++I;
      continue;
    }

    Register ValueReg = MovImm.getOperand(0).getReg();
    int64_t Imm = MovImm.getOperand(1).getImm();
    if (Imm == 0) {
      ++I;
      continue;
    }

    auto StoreI = nextNonDebug(I, MBB);
    if (StoreI == MBB.end() || StoreI->getOpcode() != StoreOpcode ||
        StoreI->getNumOperands() < 3 || !StoreI->getOperand(0).isReg() ||
        !StoreI->getOperand(1).isReg() || !StoreI->getOperand(2).isImm() ||
        !regsOverlap(TRI, StoreI->getOperand(0).getReg(), ValueReg) ||
        regsOverlap(TRI, StoreI->getOperand(1).getReg(), ValueReg) ||
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

bool BedrockPeephole::foldAbsZextToMov32Imm(MachineBasicBlock &MBB,
                                            MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Addr = *I;
    if (Addr.isDebugInstr()) {
      ++I;
      continue;
    }

    if (Addr.getOpcode() != Bedrock::MOV64abs || Addr.getNumOperands() < 2 ||
        !Addr.getOperand(0).isReg() || !isAbsTargetOperand(Addr.getOperand(1))) {
      ++I;
      continue;
    }

    Register Reg = Addr.getOperand(0).getReg();
    if (!isDReg(Reg)) {
      ++I;
      continue;
    }

    auto NarrowI = nextNonDebug(I, MBB);
    bool IsExt = NarrowI != MBB.end() &&
                 NarrowI->getOpcode() == Bedrock::EXTZQ32rr &&
                 NarrowI->getNumOperands() >= 2 &&
                 NarrowI->getOperand(0).isReg() &&
                 NarrowI->getOperand(1).isReg() &&
                 regsOverlap(TRI, NarrowI->getOperand(0).getReg(), Reg) &&
                 regsOverlap(TRI, NarrowI->getOperand(1).getReg(), Reg);
    bool IsAndMask =
        NarrowI != MBB.end() && NarrowI->getOpcode() == Bedrock::AND64ri &&
        NarrowI->getNumOperands() >= 3 && NarrowI->getOperand(0).isReg() &&
        NarrowI->getOperand(1).isReg() && NarrowI->getOperand(2).isImm() &&
        NarrowI->getOperand(2).getImm() == 0xffffffffLL &&
        regsOverlap(TRI, NarrowI->getOperand(0).getReg(), Reg) &&
        regsOverlap(TRI, NarrowI->getOperand(1).getReg(), Reg) &&
        regDefDeadOrDeadAfterInCFG(NarrowI, MBB, Bedrock::FLAGS, TRI);
    if (!IsExt && !IsAndMask) {
      ++I;
      continue;
    }

    BuildMI(MBB, Addr.getIterator(), Addr.getDebugLoc(),
            TII.get(Bedrock::MOV32imm), Reg)
        .add(Addr.getOperand(1));
    if (IsAndMask) {
      BuildMI(MBB, Addr.getIterator(), NarrowI->getDebugLoc(),
              TII.get(Bedrock::EXTZQ32rr), Reg)
          .addReg(Reg);
      NarrowI->eraseFromParent();
    }
    Addr.eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldSymbolicImm32HighOr(MachineBasicBlock &MBB,
                                              MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Mov = *I;
    if (Mov.isDebugInstr()) {
      ++I;
      continue;
    }

    if (Mov.getOpcode() != Bedrock::MOV32imm || Mov.getNumOperands() < 2 ||
        !Mov.getOperand(0).isReg() || !isAbsTargetOperand(Mov.getOperand(1))) {
      ++I;
      continue;
    }

    Register Reg = Mov.getOperand(0).getReg();
    if (!isDReg(Reg)) {
      ++I;
      continue;
    }

    auto ExtI = nextNonDebug(I, MBB);
    if (ExtI == MBB.end() || ExtI->getOpcode() != Bedrock::EXTZQ32rr ||
        ExtI->getNumOperands() < 2 || !ExtI->getOperand(0).isReg() ||
        !ExtI->getOperand(1).isReg() ||
        !regsOverlap(TRI, ExtI->getOperand(0).getReg(), Reg) ||
        !regsOverlap(TRI, ExtI->getOperand(1).getReg(), Reg)) {
      ++I;
      continue;
    }

    auto OrI = nextNonDebug(ExtI, MBB);
    MachineBasicBlock::iterator InsertI = I;
    if (OrI != MBB.end() && OrI->getOpcode() == Bedrock::MOV64ri &&
        !instrTouchesReg(*OrI, Reg, TRI) &&
        !instrTouchesReg(*OrI, Bedrock::FLAGS, TRI)) {
      OrI = nextNonDebug(OrI, MBB);
      InsertI = OrI;
    }
    if (OrI == MBB.end() || OrI->getNumOperands() < 3 ||
        !OrI->getOperand(0).isReg() || !OrI->getOperand(1).isReg() ||
        !regsOverlap(TRI, OrI->getOperand(0).getReg(), Reg) ||
        !regDefDeadOrDeadAfterInCFG(OrI, MBB, Bedrock::FLAGS, TRI)) {
      ++I;
      continue;
    }

    Register HighReg;
    int64_t HighImm = 0;
    bool UseHighImm = false;
    if (OrI->getOpcode() == Bedrock::OR64ri && OrI->getOperand(2).isImm() &&
        regsOverlap(TRI, OrI->getOperand(1).getReg(), Reg) &&
        OrI->getOperand(2).getImm() != 0) {
      HighImm = OrI->getOperand(2).getImm();
      UseHighImm = true;
    } else if (OrI->getOpcode() == Bedrock::OR64rr &&
               OrI->getOperand(2).isReg()) {
      Register LHS = OrI->getOperand(1).getReg();
      Register RHS = OrI->getOperand(2).getReg();
      if (regsOverlap(TRI, LHS, Reg) && !regsOverlap(TRI, RHS, Reg))
        HighReg = RHS;
      else if (regsOverlap(TRI, RHS, Reg) && !regsOverlap(TRI, LHS, Reg))
        HighReg = LHS;
      if (!HighReg.isValid() || !isIntReg(HighReg) ||
          regsOverlap(TRI, HighReg, Reg)) {
        ++I;
        continue;
      }
    } else {
      ++I;
      continue;
    }

    if (UseHighImm) {
      BuildMI(MBB, InsertI, OrI->getDebugLoc(), TII.get(Bedrock::MOV64ri),
              Reg)
          .addImm(HighImm);
    } else {
      BuildMI(MBB, InsertI, OrI->getDebugLoc(), TII.get(Bedrock::MOV64rr),
              Reg)
          .addReg(HighReg,
                  getKillRegState(operandIsKill(*OrI, HighReg, TRI)));
    }
    BuildMI(MBB, InsertI, Mov.getDebugLoc(), TII.get(Bedrock::OR32imm), Reg)
        .addReg(Reg)
        .add(Mov.getOperand(1));

    Mov.eraseFromParent();
    ExtI->eraseFromParent();
    OrI->eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldSmallMov64Imm(MachineBasicBlock &MBB,
                                            MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  auto FindUnusedDReg = [&](MachineBasicBlock::iterator At) {
    for (Register Reg = Bedrock::D0; Reg <= Bedrock::D5;
         Reg = Register(Reg + 1))
      if (regUnusedAfterInCFG(At, MBB, Reg, TRI))
        return Reg;
    return Register();
  };

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
    if (!Matched || Imm == 0) {
      ++I;
      continue;
    }

    if (Imm >= std::numeric_limits<int16_t>::min() &&
        Imm <= std::numeric_limits<int16_t>::max()) {
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
      continue;
    }

    if (Imm < 0 || Imm > std::numeric_limits<int32_t>::max()) {
      ++I;
      continue;
    }

    Register Scratch = FindUnusedDReg(MovImm.getIterator());
    if (!Scratch.isValid()) {
      ++I;
      continue;
    }

    DebugLoc DL = MovImm.getDebugLoc();
    BuildMI(MBB, MovImm.getIterator(), DL, TII.get(Bedrock::MOV32ri), Scratch)
        .addImm(Imm);
    BuildMI(MBB, MovImm.getIterator(), DL, TII.get(Bedrock::MOV64rr), Dst)
        .addReg(Scratch);
    MovImm.eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldClrStore(MachineBasicBlock &MBB,
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

bool BedrockPeephole::foldAImmCopyToDImm(MachineBasicBlock &MBB,
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

bool BedrockPeephole::foldDImmCopyToAImm(MachineBasicBlock &MBB,
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

    Register DReg;
    int64_t Imm = 0;
    bool IsMov32 = isMov32Imm(MovImm, DReg, Imm);
    bool IsMov64 = !IsMov32 && isMov64Imm(MovImm, DReg, Imm);
    if ((!IsMov32 && !IsMov64) || !isDReg(DReg)) {
      ++I;
      continue;
    }

    SmallVector<MachineInstr *, 4> Copies;
    bool Invalid = false;
    MachineBasicBlock::iterator Scan = nextNonDebug(I, MBB);
    for (; Scan != MBB.end(); Scan = nextNonDebug(Scan, MBB)) {
      MachineInstr &Use = *Scan;
      if (instrHasRegMaskForReg(Use, DReg, TRI)) {
        Invalid = true;
        break;
      }
      if (instrDefinesReg(Use, DReg, TRI))
        break;
      if (!instrUsesReg(Use, DReg, TRI))
        continue;

      if (Use.getOpcode() == Bedrock::MOV64rr && Use.getNumOperands() >= 2 &&
          Use.getOperand(0).isReg() && Use.getOperand(1).isReg() &&
          regsOverlap(TRI, Use.getOperand(1).getReg(), DReg) &&
          isAReg(Use.getOperand(0).getReg())) {
        Copies.push_back(&Use);
        if (Copies.size() >= 4) {
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
    if (!operandIsKill(*LastCopy, DReg, TRI) &&
        !regDeadAfter(std::next(LastCopy->getIterator()), MBB, DReg, TRI)) {
      ++I;
      continue;
    }

    unsigned NewMovOpcode = IsMov32 ? Bedrock::MOV32ri : Bedrock::MOV64ri;
    for (MachineInstr *Copy : Copies) {
      BuildMI(MBB, Copy->getIterator(), Copy->getDebugLoc(),
              TII.get(NewMovOpcode), Copy->getOperand(0).getReg())
          .addImm(Imm);
      Copy->eraseFromParent();
    }
    MovImm.eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

static unsigned getMov32IndexedToMemOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::MOV32idx1rm:
    return Bedrock::MOV32idx1mm;
  case Bedrock::MOV32idx4rm:
    return Bedrock::MOV32idx4mm;
  case Bedrock::MOV32idx4lrm:
    return Bedrock::MOV32idx4lmm;
  }
}

static unsigned getMov32MemToIndexedOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::MOV32idx1mr:
    return Bedrock::MOV32midx1;
  case Bedrock::MOV32idx4mr:
    return Bedrock::MOV32midx4;
  case Bedrock::MOV32idx4lmr:
    return Bedrock::MOV32midx4l;
  }
}

static bool matchMov32IndexedStore(const MachineInstr &MI, Register &Src,
                                   Register &Base, Register &Index,
                                   int64_t &Offset) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::MOV32idx1mr:
  case Bedrock::MOV32idx4mr:
  case Bedrock::MOV32idx4lmr:
    break;
  }
  if (MI.getNumOperands() < 4 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg() || !MI.getOperand(2).isReg() ||
      !MI.getOperand(3).isImm())
    return false;
  Src = MI.getOperand(0).getReg();
  Base = MI.getOperand(1).getReg();
  Index = MI.getOperand(2).getReg();
  Offset = MI.getOperand(3).getImm();
  return true;
}

bool BedrockPeephole::foldMemCopy(MachineBasicBlock &MBB,
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

    {
      Register IndexedTmp, IndexedBase, IndexedIndex;
      int64_t IndexedOffset = 0;
      unsigned Scale = 0;
      bool LongIndex = false;
      unsigned IndexedToMemOpcode = getMov32IndexedToMemOpcode(Load.getOpcode());
      if (IndexedToMemOpcode != 0 &&
          isIndexedMemLoad(Load, IndexedTmp, IndexedBase, IndexedIndex,
                           IndexedOffset, Scale, LongIndex) &&
          !hasOrderedMemOperand(Load)) {
        auto StoreI = nextNonDebug(I, MBB);
        Register StoreSrc, DstBase;
        int64_t DstOffset = 0;
        if (StoreI != MBB.end() && StoreI->getOpcode() == Bedrock::MOV32mr &&
            isMemStore(*StoreI, StoreSrc, DstBase, DstOffset) &&
            !hasOrderedMemOperand(*StoreI) &&
            regsOverlap(TRI, StoreSrc, IndexedTmp) &&
            !regsOverlap(TRI, DstBase, IndexedTmp) &&
            (operandIsKill(*StoreI, IndexedTmp, TRI) ||
             regUnusedAfterInCFG(std::next(StoreI), MBB, IndexedTmp, TRI))) {
          MachineInstrBuilder MIB =
              BuildMI(MBB, Load.getIterator(), Load.getDebugLoc(),
                      TII.get(IndexedToMemOpcode))
                  .addReg(IndexedBase)
                  .addReg(IndexedIndex,
                          getKillRegState(
                              operandIsKill(Load, IndexedIndex, TRI)))
                  .addImm(IndexedOffset)
                  .addReg(DstBase)
                  .addImm(DstOffset);
          MIB.cloneMergedMemRefs({&Load, &*StoreI});
          Load.eraseFromParent();
          StoreI->eraseFromParent();
          I = MBB.begin();
          Changed = true;
          continue;
        }
      }
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
    Register IndexedDstBase, IndexedDstIndex;
    int64_t IndexedDstOffset = 0;
    unsigned MemToIndexedOpcode = getMov32MemToIndexedOpcode(StoreI->getOpcode());
    if (!LoadPost && PlainLoadOpcode == Bedrock::MOV32rm &&
        MemToIndexedOpcode != 0 &&
        matchMov32IndexedStore(*StoreI, StoreSrc, IndexedDstBase,
                               IndexedDstIndex, IndexedDstOffset) &&
        !hasOrderedMemOperand(Load) && !hasOrderedMemOperand(*StoreI) &&
        regsOverlap(TRI, Tmp, StoreSrc) &&
        !regsOverlap(TRI, IndexedDstBase, Tmp) &&
        !regsOverlap(TRI, IndexedDstIndex, Tmp) &&
        (operandIsKill(*StoreI, Tmp, TRI) ||
         regUnusedAfterInCFG(std::next(StoreI), MBB, Tmp, TRI))) {
      MachineInstrBuilder MIB =
          BuildMI(MBB, Load.getIterator(), Load.getDebugLoc(),
                  TII.get(MemToIndexedOpcode))
              .addReg(SrcBase,
                      getKillRegState(operandIsKill(Load, SrcBase, TRI)))
              .addImm(SrcOffset)
              .addReg(IndexedDstBase)
              .addReg(IndexedDstIndex,
                      getKillRegState(
                          operandIsKill(*StoreI, IndexedDstIndex, TRI)))
              .addImm(IndexedDstOffset);
      MIB.cloneMergedMemRefs({&Load, &*StoreI});
      Load.eraseFromParent();
      StoreI->eraseFromParent();
      I = MBB.begin();
      Changed = true;
      continue;
    }

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

bool BedrockPeephole::foldLoadOp(MachineBasicBlock &MBB,
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

bool BedrockPeephole::foldLoadMAdd(MachineBasicBlock &MBB,
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

bool BedrockPeephole::foldAbsoluteStoreRuns(MachineBasicBlock &MBB,
                                               MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    if (I->isDebugInstr()) {
      ++I;
      continue;
    }

    const MachineOperand *Target = nullptr;
    Register Base;
    Register Src;
    int64_t Offset = 0;
    MachineInstr *Store = nullptr;
    if (!matchAbsQwordStorePair(I, MBB, TRI, Target, Base, Src, Offset,
                                Store)) {
      ++I;
      continue;
    }

    SmallVector<AbsStoreRunEntry, 16> Entries;
    Entries.push_back({&*I, Store, Offset});

    int64_t Stride = 0;
    auto Scan = nextNonDebug(Store->getIterator(), MBB);
    while (Scan != MBB.end()) {
      const MachineOperand *NextTarget = nullptr;
      Register NextBase;
      Register NextSrc;
      int64_t NextOffset = 0;
      MachineInstr *NextStore = nullptr;
      if (!matchAbsQwordStorePair(Scan, MBB, TRI, NextTarget, NextBase,
                                  NextSrc, NextOffset, NextStore) ||
          !regsOverlap(TRI, NextBase, Base) ||
          !regsOverlap(TRI, NextSrc, Src) ||
          !sameAbsTargetIgnoringOffset(*Target, *NextTarget))
        break;

      int64_t Step = NextOffset - Entries.back().Offset;
      if (Stride == 0) {
        if (Step != 8 && Step != -8)
          break;
        Stride = Step;
      } else if (Step != Stride) {
        break;
      }

      Entries.push_back({&*Scan, NextStore, NextOffset});
      Scan = nextNonDebug(NextStore->getIterator(), MBB);
    }

    if (Entries.size() < 3 || Stride == 0 ||
        !regUnusedAfterInCFG(Scan, MBB, Base, TRI)) {
      ++I;
      continue;
    }

    SmallVector<AbsStoreRunEntry, 16> Ordered(Entries);
    llvm::sort(Ordered, [](const AbsStoreRunEntry &L,
                           const AbsStoreRunEntry &R) {
      return L.Offset < R.Offset;
    });

    MachineOperand BaseTarget(*Target);
    BaseTarget.setOffset(Ordered.front().Offset);
    DebugLoc DL = Entries.front().Addr->getDebugLoc();
    auto Insert = Entries.front().Addr->getIterator();
    BuildMI(MBB, Insert, DL, TII.get(Bedrock::MOV64abs), Base).add(BaseTarget);
    for (const AbsStoreRunEntry &Entry : Ordered) {
      MachineInstrBuilder MIB =
          BuildMI(MBB, Insert, Entry.Store->getDebugLoc(),
                  TII.get(Bedrock::MOV64postmr))
              .addReg(Src)
              .addReg(Base);
      MIB.cloneMemRefs(*Entry.Store);
    }

    for (AbsStoreRunEntry &Entry : Entries) {
      Entry.Addr->eraseFromParent();
      Entry.Store->eraseFromParent();
    }

    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldAbsoluteLoadRuns(MachineBasicBlock &MBB,
                                           MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    if (I->isDebugInstr()) {
      ++I;
      continue;
    }

    const MachineOperand *Target = nullptr;
    Register Base;
    Register Dst;
    int64_t Offset = 0;
    MachineInstr *Load = nullptr;
    if (!matchAbsLoadPair(I, MBB, TRI, Target, Base, Dst, Offset, Load)) {
      ++I;
      continue;
    }

    SmallVector<AbsLoadRunEntry, 16> Entries;
    Entries.push_back({&*I, Load, Dst, Offset});

    auto Scan = nextNonDebug(Load->getIterator(), MBB);
    while (Scan != MBB.end()) {
      const MachineOperand *NextTarget = nullptr;
      Register NextBase;
      Register NextDst;
      int64_t NextOffset = 0;
      MachineInstr *NextLoad = nullptr;
      if (!matchAbsLoadPair(Scan, MBB, TRI, NextTarget, NextBase, NextDst,
                            NextOffset, NextLoad) ||
          !regsOverlap(TRI, NextBase, Base) ||
          !sameAbsTargetIgnoringOffset(*Target, *NextTarget))
        break;

      Entries.push_back({&*Scan, NextLoad, NextDst, NextOffset});
      Scan = nextNonDebug(NextLoad->getIterator(), MBB);
    }

    if (Entries.size() < 3 || !regUnusedAfterInCFG(Scan, MBB, Base, TRI)) {
      ++I;
      continue;
    }

    int64_t BaseOffset = Entries.front().Offset;
    bool OffsetsFit = true;
    for (const AbsLoadRunEntry &Entry : Entries) {
      int64_t RelOffset = Entry.Offset - BaseOffset;
      if (RelOffset < -32768 || RelOffset > 32767) {
        OffsetsFit = false;
        break;
      }
    }
    if (!OffsetsFit) {
      ++I;
      continue;
    }

    MachineOperand BaseTarget(*Target);
    BaseTarget.setOffset(BaseOffset);
    DebugLoc DL = Entries.front().Addr->getDebugLoc();
    auto Insert = Entries.front().Addr->getIterator();
    BuildMI(MBB, Insert, DL, TII.get(Bedrock::MOV64abs), Base).add(BaseTarget);
    for (const AbsLoadRunEntry &Entry : Entries) {
      MachineInstrBuilder MIB =
          BuildMI(MBB, Insert, Entry.Load->getDebugLoc(),
                  TII.get(Entry.Load->getOpcode()), Entry.Dst)
              .addReg(Base)
              .addImm(Entry.Offset - BaseOffset);
      MIB.cloneMemRefs(*Entry.Load);
    }

    for (AbsLoadRunEntry &Entry : Entries) {
      Entry.Addr->eraseFromParent();
      Entry.Load->eraseFromParent();
    }

    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldAbsoluteDestStoreRuns(MachineBasicBlock &MBB,
                                                MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  auto RewriteStore = [&](MachineInstr &Store, Register Base,
                          int64_t RelOffset) {
    MachineInstrBuilder MIB;
    switch (Store.getOpcode()) {
    default:
      return false;
    case Bedrock::MOV8mr:
    case Bedrock::MOV16mr:
    case Bedrock::MOV32mr:
    case Bedrock::MOV64mr:
      MIB = BuildMI(MBB, Store.getIterator(), Store.getDebugLoc(),
                    TII.get(Store.getOpcode()))
                .add(Store.getOperand(0))
                .addReg(Base)
                .addImm(RelOffset);
      break;
    case Bedrock::MOV8mm:
    case Bedrock::MOV16mm:
    case Bedrock::MOV32mm:
    case Bedrock::MOV64mm:
      MIB = BuildMI(MBB, Store.getIterator(), Store.getDebugLoc(),
                    TII.get(Store.getOpcode()))
                .add(Store.getOperand(0))
                .add(Store.getOperand(1))
                .addReg(Base)
                .addImm(RelOffset);
      break;
    }
    MIB.cloneMemRefs(Store);
    Store.eraseFromParent();
    return true;
  };

  for (auto I = MBB.begin(); I != MBB.end();) {
    if (I->isDebugInstr()) {
      ++I;
      continue;
    }

    const MachineOperand *Target = nullptr;
    Register Base;
    int64_t Offset = 0;
    MachineInstr *Store = nullptr;
    if (!matchAbsDestStorePair(I, MBB, TRI, Target, Base, Offset, Store)) {
      ++I;
      continue;
    }

    SmallVector<AbsDestStoreRunEntry, 16> Entries;
    Entries.push_back({&*I, Store, Offset});

    auto Scan = nextNonDebug(Store->getIterator(), MBB);
    bool StoppedAtBaseClobber = false;
    while (Scan != MBB.end() && !StoppedAtBaseClobber) {
      const MachineOperand *NextTarget = nullptr;
      Register NextBase;
      int64_t NextOffset = 0;
      MachineInstr *NextStore = nullptr;
      if (matchAbsDestStorePair(Scan, MBB, TRI, NextTarget, NextBase,
                                NextOffset, NextStore) &&
          regsOverlap(TRI, NextBase, Base) &&
          sameAbsTargetIgnoringOffset(*Target, *NextTarget)) {
        Entries.push_back({&*Scan, NextStore, NextOffset});
        Scan = nextNonDebug(NextStore->getIterator(), MBB);
        continue;
      }

      if (instrDefinesReg(*Scan, Base, TRI) ||
          instrHasRegMaskForReg(*Scan, Base, TRI)) {
        StoppedAtBaseClobber = true;
        break;
      }
      if (instrTouchesReg(*Scan, Base, TRI) || Scan->isTerminator())
        break;

      Scan = nextNonDebug(Scan, MBB);
    }

    if (Entries.size() < 3 ||
        (!StoppedAtBaseClobber &&
         !regUnusedAfterInCFG(Scan, MBB, Base, TRI))) {
      ++I;
      continue;
    }

    int64_t BaseOffset = Entries.front().Offset;
    bool OffsetsFit = true;
    for (const AbsDestStoreRunEntry &Entry : Entries) {
      int64_t RelOffset = Entry.Offset - BaseOffset;
      if (RelOffset < -32768 || RelOffset > 32767) {
        OffsetsFit = false;
        break;
      }
    }
    if (!OffsetsFit) {
      ++I;
      continue;
    }

    MachineOperand BaseTarget(*Target);
    BaseTarget.setOffset(BaseOffset);
    DebugLoc DL = Entries.front().Addr->getDebugLoc();
    BuildMI(MBB, Entries.front().Addr->getIterator(), DL,
            TII.get(Bedrock::MOV64abs), Base)
        .add(BaseTarget);

    bool RewrittenAll = true;
    for (const AbsDestStoreRunEntry &Entry : Entries) {
      int64_t RelOffset = Entry.Offset - BaseOffset;
      if (!RewriteStore(*Entry.Store, Base, RelOffset)) {
        RewrittenAll = false;
        break;
      }
    }
    if (!RewrittenAll) {
      ++I;
      continue;
    }

    for (AbsDestStoreRunEntry &Entry : Entries)
      Entry.Addr->eraseFromParent();

    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldAbsBaseToNearbyOffset(MachineBasicBlock &MBB,
                                                MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  auto ClearKillsInRange = [&](MachineBasicBlock::iterator Begin,
                               MachineBasicBlock::iterator End, Register Reg) {
    for (auto I = Begin; I != End; ++I) {
      if (I->isDebugInstr())
        continue;
      for (MachineOperand &MO : I->operands())
        if (MO.isReg() && !MO.isDef() && operandTouchesReg(MO, Reg, TRI))
          MO.setIsKill(false);
    }
  };

  auto HasPostMemoryBaseUse = [&](const MachineInstr &MI, Register Reg) {
    for (unsigned OpNo = 0, E = MI.getNumOperands(); OpNo != E; ++OpNo)
      if (isPostMemoryBaseOperand(MI, OpNo, Reg, TRI))
        return true;
    switch (MI.getOpcode()) {
    default:
      return false;
    case Bedrock::REPMOV32mmpostboth:
    case Bedrock::REPMOV64mmpostboth:
    case Bedrock::REPNEMOV32mmpostboth:
    case Bedrock::REPMOV8postmr64:
    case Bedrock::REPMOV32postmr:
    case Bedrock::REPNEMOV32postrm:
    case Bedrock::REPADD32postrm:
    case Bedrock::REPGTCMP32postrm:
      for (const MachineOperand &MO : MI.operands())
        if (MO.isReg() && operandTouchesReg(MO, Reg, TRI))
          return true;
      return false;
    }
  };

  auto MatchSelfAddImm = [&](const MachineInstr &MI, Register Reg,
                             int64_t &Amount) {
    if (MI.getOpcode() != Bedrock::ADD64ri || MI.getNumOperands() < 3 ||
        !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
        !MI.getOperand(2).isImm() ||
        !regsOverlap(TRI, MI.getOperand(0).getReg(), Reg) ||
        !regsOverlap(TRI, MI.getOperand(1).getReg(), Reg))
      return false;
    Amount = MI.getOperand(2).getImm();
    return true;
  };

  auto MatchPostBaseUpdate = [&](MachineInstr &MI, Register Reg,
                                 int64_t &Amount) {
    Register MemReg;
    Register MemBase;
    int64_t Offset = 0;
    unsigned PlainOpcode = 0;
    bool PostInc = false;
    if (isMemCopyLoad(MI, MemReg, MemBase, Offset, PlainOpcode, PostInc) &&
        PostInc && regsOverlap(TRI, MemBase, Reg)) {
      Amount = memSizeForOpcode(PlainOpcode);
      return Amount != 0;
    }
    if (isMemCopyStore(MI, MemReg, MemBase, Offset, PlainOpcode, PostInc) &&
        PostInc && regsOverlap(TRI, MemBase, Reg)) {
      Amount = memSizeForOpcode(PlainOpcode);
      return Amount != 0;
    }

    auto MatchKnownRepCount = [&](Register CountReg, int64_t &Count) {
      auto Prev = prevNonDebug(MI.getIterator(), MBB);
      if (Prev == MBB.end())
        return false;
      Register DefReg;
      int64_t Imm = 0;
      if (!isMov32Imm(*Prev, DefReg, Imm) && !isMov64Imm(*Prev, DefReg, Imm))
        return false;
      if (!regsOverlap(TRI, DefReg, CountReg) || Imm < 0)
        return false;
      Count = Imm;
      return true;
    };

    auto ResolveRepStep = [&](unsigned Opcode, int64_t RawStep,
                              int64_t &Step) {
      if (RawStep == Bedrock::UpdatePostInc) {
        switch (Opcode) {
        default:
          return false;
        case Bedrock::REPMOV8postmr64:
          Step = 1;
          return true;
        case Bedrock::REPMOV32postmr:
          Step = 4;
          return true;
        }
      }
      if (RawStep == Bedrock::UpdatePostDec) {
        switch (Opcode) {
        default:
          return false;
        case Bedrock::REPMOV8postmr64:
          Step = -1;
          return true;
        case Bedrock::REPMOV32postmr:
          Step = -4;
          return true;
        }
      }
      Step = RawStep;
      return true;
    };

    switch (MI.getOpcode()) {
    default:
      break;
    case Bedrock::REPMOV8postmr64:
    case Bedrock::REPMOV32postmr: {
      if (MI.getNumOperands() < 5 || !MI.getOperand(1).isReg() ||
          !MI.getOperand(3).isReg() || !MI.getOperand(4).isImm() ||
          !regsOverlap(TRI, MI.getOperand(3).getReg(), Reg))
        break;
      int64_t Count = 0;
      int64_t Step = 0;
      if (!MatchKnownRepCount(MI.getOperand(1).getReg(), Count) ||
          !ResolveRepStep(MI.getOpcode(), MI.getOperand(4).getImm(), Step) ||
          !mulSignedNoOverflow(Count, Step, Amount))
        break;
      return true;
    }
    }
    return false;
  };

  for (auto I = MBB.begin(); I != MBB.end();) {
    if (I->isDebugInstr()) {
      ++I;
      continue;
    }

    MachineInstr &BaseMov = *I;
    if (BaseMov.getOpcode() != Bedrock::MOV64abs ||
        BaseMov.getNumOperands() < 2 || !BaseMov.getOperand(0).isReg() ||
        !isIntReg(BaseMov.getOperand(0).getReg()) ||
        !isAbsTargetOperand(BaseMov.getOperand(1))) {
      ++I;
      continue;
    }

    Register Base = BaseMov.getOperand(0).getReg();
    const MachineOperand &Target = BaseMov.getOperand(1);

    if (!isAReg(Base)) {
      auto Scan = nextNonDebug(I, MBB);
      bool Rewritten = false;
      while (Scan != MBB.end()) {
        if (Scan->getOpcode() == Bedrock::MOV64abs &&
            Scan->getNumOperands() >= 2 && Scan->getOperand(0).isReg() &&
            regsOverlap(TRI, Scan->getOperand(0).getReg(), Base) &&
            isAbsTargetOperand(Scan->getOperand(1))) {
          const MachineOperand &NextTarget = Scan->getOperand(1);
          int64_t Delta = NextTarget.getOffset() - Target.getOffset();
          if (!sameAbsTargetIgnoringOffset(Target, NextTarget) ||
              Delta < -32768 || Delta > 32767 ||
              !regDeadAfterInCFG(Scan, MBB, Bedrock::FLAGS, TRI))
            break;

          ClearKillsInRange(std::next(I), Scan, Base);
          if (Delta == 0) {
            Scan->eraseFromParent();
          } else {
            BuildMI(MBB, Scan, Scan->getDebugLoc(), TII.get(Bedrock::ADD64ri),
                    Base)
                .addReg(Base)
                .addImm(Delta);
            Scan->eraseFromParent();
          }
          I = MBB.begin();
          Changed = true;
          Rewritten = true;
          break;
        }

        if (Scan->isTerminator() || instrDefinesReg(*Scan, Base, TRI) ||
            instrHasRegMaskForReg(*Scan, Base, TRI))
          break;
        Scan = nextNonDebug(Scan, MBB);
      }

      if (!Rewritten) {
        ++I;
      }
      continue;
    }

    int64_t BaseOffset = Target.getOffset();
    int64_t CurrentOffset = BaseOffset;
    auto Scan = nextNonDebug(I, MBB);
    bool Rewritten = false;
    while (Scan != MBB.end()) {
      if (Scan->getOpcode() == Bedrock::MOV64abs &&
          Scan->getNumOperands() >= 2 && Scan->getOperand(0).isReg() &&
          isAbsTargetOperand(Scan->getOperand(1))) {
        Register NextDst = Scan->getOperand(0).getReg();
        const MachineOperand &NextTarget = Scan->getOperand(1);
        if (!sameAbsTargetIgnoringOffset(Target, NextTarget) &&
            !regsOverlap(TRI, NextDst, Base)) {
          Scan = nextNonDebug(Scan, MBB);
          continue;
        }
        int64_t Delta = NextTarget.getOffset() - CurrentOffset;
        if (!sameAbsTargetIgnoringOffset(Target, NextTarget) ||
            Delta < -32768 || Delta > 32767)
          break;

        if (regsOverlap(TRI, NextDst, Base) && Delta == 0) {
          ClearKillsInRange(std::next(I), Scan, Base);
          Scan->eraseFromParent();
          I = MBB.begin();
          Changed = true;
          Rewritten = true;
          break;
        }

        if (regsOverlap(TRI, NextDst, Base)) {
          if (!regDeadAfterInCFG(Scan, MBB, Bedrock::FLAGS, TRI))
            break;

          ClearKillsInRange(std::next(I), Scan, Base);
          BuildMI(MBB, Scan, Scan->getDebugLoc(), TII.get(Bedrock::ADD64ri),
                  Base)
              .addReg(Base)
              .addImm(Delta);
          Scan->eraseFromParent();
          I = MBB.begin();
          Changed = true;
          Rewritten = true;
          break;
        }

        if (Delta == 0) {
          ClearKillsInRange(std::next(I), Scan, Base);
          BuildMI(MBB, Scan, Scan->getDebugLoc(), TII.get(Bedrock::MOV64rr),
                  NextDst)
              .addReg(Base);
          Scan->eraseFromParent();
          I = MBB.begin();
          Changed = true;
          Rewritten = true;
          break;
        }

        if (!isAReg(NextDst))
          break;

        ClearKillsInRange(std::next(I), Scan, Base);
        BuildMI(MBB, Scan, Scan->getDebugLoc(), TII.get(Bedrock::LEAri),
                NextDst)
            .addReg(Base)
            .addImm(Delta);
        Scan->eraseFromParent();
        I = MBB.begin();
        Changed = true;
        Rewritten = true;
        break;
      }

      int64_t Addend = 0;
      if (MatchSelfAddImm(*Scan, Base, Addend)) {
        if (!addSignedNoOverflow(CurrentOffset, Addend, CurrentOffset))
          break;
        Scan = nextNonDebug(Scan, MBB);
        continue;
      }

      if (MatchPostBaseUpdate(*Scan, Base, Addend)) {
        if (!addSignedNoOverflow(CurrentOffset, Addend, CurrentOffset))
          break;
        Scan = nextNonDebug(Scan, MBB);
        continue;
      }

      if (Scan->isTerminator() || instrDefinesReg(*Scan, Base, TRI) ||
          instrHasRegMaskForReg(*Scan, Base, TRI) ||
          HasPostMemoryBaseUse(*Scan, Base))
        break;

      Scan = nextNonDebug(Scan, MBB);
    }

    if (!Rewritten)
      ++I;
  }

  return Changed;
}

bool BedrockPeephole::foldAbsIndexedAddressRuns(MachineBasicBlock &MBB,
                                                MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  auto ClearKillsInRange = [&](MachineBasicBlock::iterator Begin,
                               MachineBasicBlock::iterator End, Register Reg) {
    for (auto I = Begin; I != End; ++I) {
      if (I->isDebugInstr())
        continue;
      for (MachineOperand &MO : I->operands())
        if (MO.isReg() && !MO.isDef() && operandTouchesReg(MO, Reg, TRI))
          MO.setIsKill(false);
    }
  };

  auto MatchAbsIndexedAddress = [&](MachineBasicBlock::iterator AddrI,
                                    const MachineOperand *&Target,
                                    Register &Base, Register &Index,
                                    MachineInstr *&Add) {
    MachineInstr &Addr = *AddrI;
    if (Addr.getOpcode() != Bedrock::MOV64abs || Addr.getNumOperands() < 2 ||
        !Addr.getOperand(0).isReg() || !isAReg(Addr.getOperand(0).getReg()) ||
        !isAbsTargetOperand(Addr.getOperand(1)))
      return false;

    auto AddI = nextNonDebug(AddrI, MBB);
    if (AddI == MBB.end() || AddI->getOpcode() != Bedrock::ADD64rr ||
        AddI->getNumOperands() < 3 || !AddI->getOperand(0).isReg() ||
        !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg())
      return false;

    Base = Addr.getOperand(0).getReg();
    if (!regsOverlap(TRI, AddI->getOperand(0).getReg(), Base) ||
        !regsOverlap(TRI, AddI->getOperand(1).getReg(), Base))
      return false;

    Index = AddI->getOperand(2).getReg();
    if (!isIntReg(Index) || regsOverlap(TRI, Base, Index))
      return false;

    Target = &Addr.getOperand(1);
    Add = &*AddI;
    return true;
  };

  for (auto I = MBB.begin(); I != MBB.end();) {
    if (I->isDebugInstr()) {
      ++I;
      continue;
    }

    const MachineOperand *Target = nullptr;
    Register Base;
    Register Index;
    MachineInstr *Add = nullptr;
    if (!MatchAbsIndexedAddress(I, Target, Base, Index, Add)) {
      ++I;
      continue;
    }

    auto Scan = nextNonDebug(Add->getIterator(), MBB);
    bool Rewritten = false;
    while (Scan != MBB.end()) {
      const MachineOperand *NextTarget = nullptr;
      Register NextBase;
      Register NextIndex;
      MachineInstr *NextAdd = nullptr;
      if (MatchAbsIndexedAddress(Scan, NextTarget, NextBase, NextIndex,
                                 NextAdd)) {
        int64_t Delta = NextTarget->getOffset() - Target->getOffset();
        if (!sameAbsTargetIgnoringOffset(*Target, *NextTarget) ||
            !regsOverlap(TRI, NextIndex, Index) ||
            regsOverlap(TRI, NextBase, Base) ||
            regsOverlap(TRI, NextBase, Index) || Delta <= 0 || Delta > 64 ||
            !regDefDeadOrDeadAfterInCFG(NextAdd->getIterator(), MBB,
                                        Bedrock::FLAGS, TRI))
          break;

        ClearKillsInRange(std::next(I), Scan, Base);
        ClearKillsInRange(std::next(I), Scan, Index);
        auto Insert = Scan;
        DebugLoc DL = Scan->getDebugLoc();
        BuildMI(MBB, Insert, DL, TII.get(Bedrock::MOV64rr), NextBase)
            .addReg(Base);
        BuildMI(MBB, Insert, NextAdd->getDebugLoc(), TII.get(Bedrock::ADD64ri),
                NextBase)
            .addReg(NextBase)
            .addImm(Delta);
        Scan->eraseFromParent();
        NextAdd->eraseFromParent();
        I = MBB.begin();
        Changed = true;
        Rewritten = true;
        break;
      }

      if (Scan->isTerminator() || instrDefinesReg(*Scan, Base, TRI) ||
          instrDefinesReg(*Scan, Index, TRI) ||
          instrHasRegMaskForReg(*Scan, Base, TRI) ||
          instrHasRegMaskForReg(*Scan, Index, TRI))
        break;

      Scan = nextNonDebug(Scan, MBB);
    }

    if (!Rewritten)
      ++I;
  }

  return Changed;
}

bool BedrockPeephole::foldStridedImmQwordStores(MachineBasicBlock &MBB,
                                                MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  auto IsSafeStride = [](int64_t Stride) {
    return Stride == 0 || (Stride > 0 && Stride <= 65535);
  };

  auto ClearStoreKills = [&](MachineInstr &Store, Register ValueReg,
                             Register Base) {
    for (MachineOperand &MO : Store.operands()) {
      if (!MO.isReg())
        continue;
      if (operandTouchesReg(MO, ValueReg, TRI) ||
          operandTouchesReg(MO, Base, TRI))
        MO.setIsKill(false);
    }
  };

  for (auto I = MBB.begin(); I != MBB.end();) {
    if (I->isDebugInstr()) {
      ++I;
      continue;
    }

    ImmQwordStoreRunEntry First;
    if (!matchImmQwordStoreEntry(I, MBB, TRI, First)) {
      ++I;
      continue;
    }

    SmallVector<ImmQwordStoreRunEntry, 16> Entries;
    Entries.push_back(First);

    int64_t AddrStride = 0;
    int64_t ValueStride = 0;
    bool Failed = false;
    auto Scan = nextNonDebug(First.Store->getIterator(), MBB);
    while (Scan != MBB.end()) {
      ImmQwordStoreRunEntry Next;
      if (matchImmQwordStoreEntry(Scan, MBB, TRI, Next) &&
          regsOverlap(TRI, Next.Base, First.Base) &&
          regsOverlap(TRI, Next.ValueReg, First.ValueReg)) {
        int64_t NextAddrStride = Next.Addr - Entries.back().Addr;
        int64_t NextValueStride = Next.Value - Entries.back().Value;
        if (Entries.size() == 1) {
          AddrStride = NextAddrStride;
          ValueStride = NextValueStride;
          if ((AddrStride == 0 && ValueStride == 0) ||
              !IsSafeStride(AddrStride) || !IsSafeStride(ValueStride)) {
            Failed = true;
            break;
          }
        } else if (NextAddrStride != AddrStride ||
                   NextValueStride != ValueStride) {
          break;
        }

        if (!regDeadAfterInCFG(Scan, MBB, Bedrock::FLAGS, TRI)) {
          Failed = true;
          break;
        }

        Entries.push_back(Next);
        Scan = nextNonDebug(Next.Store->getIterator(), MBB);
        continue;
      }

      if (Scan->isTerminator() ||
          instrTouchesReg(*Scan, First.Base, TRI) ||
          instrTouchesReg(*Scan, First.ValueReg, TRI) ||
          instrHasRegMaskForReg(*Scan, First.Base, TRI) ||
          instrHasRegMaskForReg(*Scan, First.ValueReg, TRI))
        break;

      Scan = nextNonDebug(Scan, MBB);
    }

    if (Failed || Entries.size() < 3) {
      ++I;
      continue;
    }

    for (unsigned Index = 0; Index != Entries.size(); ++Index)
      if (Index + 1 != Entries.size())
        ClearStoreKills(*Entries[Index].Store, First.ValueReg, First.Base);

    for (unsigned Index = 1; Index != Entries.size(); ++Index) {
      ImmQwordStoreRunEntry &Entry = Entries[Index];
      auto Insert = Entry.AddrImm->getIterator();
      DebugLoc DL = Entry.AddrImm->getDebugLoc();
      if (AddrStride != 0)
        BuildMI(MBB, Insert, DL, TII.get(Bedrock::ADD64ri), First.Base)
            .addReg(First.Base)
            .addImm(AddrStride);
      if (ValueStride != 0)
        BuildMI(MBB, Insert, Entry.ValueImm->getDebugLoc(),
                TII.get(Bedrock::ADD64ri), First.ValueReg)
            .addReg(First.ValueReg)
            .addImm(ValueStride);

      Entry.AddrImm->eraseFromParent();
      Entry.AddrCopy->eraseFromParent();
      Entry.ValueImm->eraseFromParent();
    }

    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldPostIncQwordZeroStoreRuns(
    MachineBasicBlock &MBB, MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &First = *I;
    if (First.isDebugInstr() || First.getOpcode() != Bedrock::MOV64postmr ||
        First.getNumOperands() < 2 || !First.getOperand(0).isReg() ||
        !First.getOperand(1).isReg() || hasOrderedMemOperand(First)) {
      ++I;
      continue;
    }

    Register Src = First.getOperand(0).getReg();
    Register Base = First.getOperand(1).getReg();
    if (regsOverlap(TRI, Src, Base) ||
        !findLastConstDefBefore(First, Src, 0, TRI) ||
        !regDeadAfter(First.getIterator(), MBB, Bedrock::FLAGS, TRI)) {
      ++I;
      continue;
    }

    SmallVector<MachineInstr *, 16> Stores;
    Stores.push_back(&First);
    auto Scan = nextNonDebug(I, MBB);
    while (Scan != MBB.end() && Scan->getOpcode() == Bedrock::MOV64postmr &&
           Scan->getNumOperands() >= 2 && Scan->getOperand(0).isReg() &&
           Scan->getOperand(1).isReg() && !hasOrderedMemOperand(*Scan) &&
           regsOverlap(TRI, Scan->getOperand(0).getReg(), Src) &&
           regsOverlap(TRI, Scan->getOperand(1).getReg(), Base)) {
      Stores.push_back(&*Scan);
      Scan = nextNonDebug(Scan, MBB);
    }

    if (Stores.size() < 3 ||
        Stores.size() > size_t(std::numeric_limits<int32_t>::max() / 2)) {
      ++I;
      continue;
    }

    Register Counter = findScratchDRegAt(I, MBB, TRI, Src, Base);
    if (!Counter.isValid()) {
      ++I;
      continue;
    }

    SmallVector<const MachineInstr *, 16> MemRefs;
    for (MachineInstr *Store : Stores)
      MemRefs.push_back(Store);

    DebugLoc DL = First.getDebugLoc();
    int64_t Count = int64_t(Stores.size()) * 2;
    BuildMI(MBB, I, DL, TII.get(Bedrock::MOV32ri), Counter).addImm(Count);
    MachineInstrBuilder Rep =
        BuildMI(MBB, I, DL, TII.get(Bedrock::REPMOV32postmr), Counter)
            .addReg(Counter)
            .addReg(Src)
            .addReg(Base)
            .addImm(Bedrock::UpdatePostInc);
    Rep.cloneMergedMemRefs(MemRefs);

    for (MachineInstr *Store : Stores)
      Store->eraseFromParent();

    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldOffsetQwordZeroStoreRuns(
    MachineBasicBlock &MBB, MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &First = *I;
    Register Src;
    Register Base;
    int64_t FirstOffset = 0;
    if (First.isDebugInstr() || !isMemStore(First, Src, Base, FirstOffset) ||
        First.getOpcode() != Bedrock::MOV64mr || hasOrderedMemOperand(First) ||
        (!isAReg(Base) && Base != Bedrock::SP) || regsOverlap(TRI, Src, Base) ||
        !findLastConstDefBefore(First, Src, 0, TRI) ||
        !regDeadAfter(I, MBB, Bedrock::FLAGS, TRI)) {
      ++I;
      continue;
    }

    Register AliasBase;
    int64_t AliasOffset = 0;
    getLeaAliasForBase(I, MBB, TRI, Base, AliasBase, AliasOffset);

    int64_t NormalizedOffset = 0;
    if (!matchOffsetQwordZeroStore(I, MBB, TRI, Base, AliasBase, AliasOffset,
                                   Src, NormalizedOffset)) {
      ++I;
      continue;
    }

    SmallVector<OffsetQwordZeroStoreEntry, 16> Entries;
    Entries.push_back({&First, NormalizedOffset});

    int64_t Stride = 0;
    auto Scan = nextNonDebug(I, MBB);
    while (Scan != MBB.end()) {
      int64_t NextOffset = 0;
      if (!matchOffsetQwordZeroStore(Scan, MBB, TRI, Base, AliasBase,
                                     AliasOffset, Src, NextOffset))
        break;

      int64_t NextStride = NextOffset - Entries.back().Offset;
      if (Stride == 0) {
        if (NextStride != 8 && NextStride != -8)
          break;
        Stride = NextStride;
      } else if (NextStride != Stride) {
        break;
      }

      Entries.push_back({&*Scan, NextOffset});
      Scan = nextNonDebug(Scan, MBB);
    }

    int64_t StartOffset =
        Stride < 0 ? Entries.back().Offset : Entries.front().Offset;
    if (Entries.size() < 3 ||
        Entries.size() > size_t(std::numeric_limits<int32_t>::max() / 2) ||
        (StartOffset != 0 && !fitsDisp16(StartOffset))) {
      ++I;
      continue;
    }

    Register Counter = findScratchDRegAt(I, MBB, TRI, Src, Base);
    if (!Counter.isValid()) {
      ++I;
      continue;
    }

    bool BaseLiveAfterRun = !regUnusedAfterInCFG(Scan, MBB, Base, TRI);
    Register RepBase = Base;
    Register ScratchBase;
    if (BaseLiveAfterRun) {
      ScratchBase = findScratchARegAt(I, MBB, TRI, Base, Src);
      if (!ScratchBase.isValid()) {
        ++I;
        continue;
      }
      RepBase = ScratchBase;
    }

    SmallVector<const MachineInstr *, 16> MemRefs;
    for (const OffsetQwordZeroStoreEntry &Entry : Entries)
      MemRefs.push_back(Entry.Store);

    DebugLoc DL = First.getDebugLoc();
    if (BaseLiveAfterRun) {
      BuildMI(MBB, I, DL, TII.get(Bedrock::LEAri), RepBase)
          .addReg(Base)
          .addImm(StartOffset);
    } else if (StartOffset != 0) {
      BuildMI(MBB, I, DL, TII.get(Bedrock::ADD64ri), Base)
          .addReg(Base)
          .addImm(StartOffset);
    }

    int64_t Count = int64_t(Entries.size()) * 2;
    BuildMI(MBB, I, DL, TII.get(Bedrock::MOV32ri), Counter).addImm(Count);
    MachineInstrBuilder Rep =
        BuildMI(MBB, I, DL, TII.get(Bedrock::REPMOV32postmr), Counter)
            .addReg(Counter)
            .addReg(Src)
            .addReg(RepBase)
            .addImm(Bedrock::UpdatePostInc);
    Rep.cloneMergedMemRefs(MemRefs);

    for (OffsetQwordZeroStoreEntry &Entry : Entries)
      Entry.Store->eraseFromParent();

    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

bool BedrockPeephole::foldPostInc(MachineBasicBlock &MBB,
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

bool BedrockPeephole::foldCrossBlockPostInc(MachineBasicBlock &MBB,
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

bool BedrockPeephole::foldLea(MachineBasicBlock &MBB,
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
           regDefDeadOrDeadAfterInCFG(AddI, MBB, Bedrock::FLAGS, TRI))) {
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
           regDefDeadOrDeadAfterInCFG(AddI, MBB, Bedrock::FLAGS, TRI))) {
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
        (isAReg(Shl.getOperand(0).getReg()) ||
         isDReg(Shl.getOperand(0).getReg())) &&
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
        (isAReg(Shl.getOperand(0).getReg()) ||
         isDReg(Shl.getOperand(0).getReg())) &&
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
      auto AddrCopyI = MBB.end();
      Register AddrFromCopy;
      if (AddBaseI != MBB.end() && AddBaseI->getNumOperands() >= 3 &&
          AddBaseI->getOperand(0).isReg()) {
        Register AddBaseDst = AddBaseI->getOperand(0).getReg();
        if (isDReg(AddBaseDst) && regsOverlap(TRI, AddBaseDst, Bias)) {
          for (auto Scan = nextNonDebug(AddBaseI, MBB); Scan != MBB.end();
               Scan = nextNonDebug(Scan, MBB)) {
            if (Scan->getOpcode() == Bedrock::MOV64rr &&
                Scan->getNumOperands() >= 2 && Scan->getOperand(0).isReg() &&
                Scan->getOperand(1).isReg() &&
                isAReg(Scan->getOperand(0).getReg()) &&
                regsOverlap(TRI, Scan->getOperand(1).getReg(), AddBaseDst)) {
              AddrCopyI = Scan;
              AddrFromCopy = Scan->getOperand(0).getReg();
              break;
            }
            if (instrTouchesReg(*Scan, AddBaseDst, TRI))
              break;
          }
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
        Register AddBaseDst = AddBaseI->getOperand(0).getReg();
        bool UsesAddrCopy = AddrCopyI != MBB.end();
        Register Addr = UsesAddrCopy ? AddrFromCopy : AddBaseDst;
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
        bool CopyPathClobbersBase = false;
        if (UsesAddrCopy) {
          for (auto Scan = nextNonDebug(AddBaseI, MBB); Scan != AddrCopyI;
               Scan = nextNonDebug(Scan, MBB)) {
            if (instrDefinesReg(*Scan, Base, TRI)) {
              CopyPathClobbersBase = true;
              break;
            }
          }
        }
        if (isAReg(Addr) && isAReg(Base) && !regsOverlap(TRI, Base, Bias) &&
            !regsOverlap(TRI, Base, Numer) && !InterveningClobbersBase &&
            !CopyPathClobbersBase &&
            (UsesAddrCopy || operandIsKill(*AddBiasI, Numer, TRI) ||
             regDeadAfter(std::next(AddBiasI), MBB, Numer, TRI)) &&
            (regsOverlap(TRI, Addr, Bias) ||
             (UsesAddrCopy &&
              (operandIsKill(*AddrCopyI, Bias, TRI) ||
               regDeadAfter(std::next(AddrCopyI), MBB, Bias, TRI))) ||
             operandIsKill(*AddBaseI, Bias, TRI) ||
             regDeadAfter(std::next(AddBaseI), MBB, Bias, TRI))) {
          if (UsesAddrCopy) {
            if (MF.getFunction().hasMinSize()) {
              BuildMI(MBB, SarSignI, AddBiasI->getDebugLoc(),
                      TII.get(Bedrock::DIVS64ri), Bias)
                  .addReg(Bias)
                  .addImm(4);
            } else {
              BuildMI(MBB, AndI, AndI->getDebugLoc(),
                      TII.get(Bedrock::SAR64ri), Bias)
                  .addReg(Bias)
                  .addImm(2);
            }
            BuildMI(MBB, AddrCopyI, AddBaseI->getDebugLoc(),
                    TII.get(Bedrock::LEA4), Addr)
                .addReg(Base)
                .addReg(Bias);
            for (auto Scan = nextNonDebug(AddBaseI, MBB); Scan != AddrCopyI;
                 Scan = nextNonDebug(Scan, MBB)) {
              for (MachineOperand &MO : Scan->operands())
                if (operandTouchesReg(MO, Base, TRI) && MO.readsReg())
                  MO.setIsKill(false);
            }
          } else if (MF.getFunction().hasMinSize()) {
            BuildMI(MBB, Shl.getIterator(), AddBiasI->getDebugLoc(),
                    TII.get(Bedrock::DIVS64ri), Numer)
                .addReg(Numer)
                .addImm(4);
            BuildMI(MBB, Shl.getIterator(), AddBaseI->getDebugLoc(),
                    TII.get(Bedrock::LEA4), Addr)
                .addReg(Base)
                .addReg(Numer);
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
            BuildMI(MBB, Shl.getIterator(), AddBaseI->getDebugLoc(),
                    TII.get(Bedrock::LEA4), Addr)
                .addReg(Base)
                .addReg(Numer);
          }

          if (!UsesAddrCopy || MF.getFunction().hasMinSize()) {
            SarSignI->eraseFromParent();
            ShrBiasI->eraseFromParent();
            AddBiasI->eraseFromParent();
          }
          if (!UsesAddrCopy)
            Shl.eraseFromParent();
          AndI->eraseFromParent();
          AddBaseI->eraseFromParent();
          if (UsesAddrCopy)
            AddrCopyI->eraseFromParent();
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

bool BedrockPeephole::foldShortLeaAliasCopies(MachineBasicBlock &MBB,
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

bool BedrockPeephole::foldARegAliasCopies(MachineBasicBlock &MBB,
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

bool BedrockPeephole::foldAliasBackCopies(MachineBasicBlock &MBB,
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

static bool canForwardCopyIntoStore(unsigned CopyOpcode, unsigned StoreOpcode,
                                    Register Src) {
  switch (CopyOpcode) {
  default:
    return false;
  case Bedrock::MOV8rr:
    return StoreOpcode == Bedrock::MOV8mr;
  case Bedrock::MOV16rr:
    return StoreOpcode == Bedrock::MOV16mr ||
           (StoreOpcode == Bedrock::MOV8mr && isIntReg(Src));
  case Bedrock::MOV32rr:
    return StoreOpcode == Bedrock::MOV32mr ||
           ((StoreOpcode == Bedrock::MOV16mr ||
             StoreOpcode == Bedrock::MOV8mr) &&
            isIntReg(Src));
  case Bedrock::MOV64rr:
    return StoreOpcode == Bedrock::MOV64mr ||
           ((StoreOpcode == Bedrock::MOV32mr ||
             StoreOpcode == Bedrock::MOV16mr ||
             StoreOpcode == Bedrock::MOV8mr) &&
            isIntReg(Src));
  }
}

bool BedrockPeephole::foldCopyStoreForward(MachineBasicBlock &MBB,
                                           MachineFunction &MF) const {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (auto I = MBB.begin(); I != MBB.end();) {
    MachineInstr &Copy = *I;
    if (Copy.isDebugInstr() || Copy.getNumOperands() < 2 ||
        !Copy.getOperand(0).isReg() || !Copy.getOperand(1).isReg()) {
      ++I;
      continue;
    }

    Register Tmp = Copy.getOperand(0).getReg();
    Register Src = Copy.getOperand(1).getReg();
    if (regsOverlap(TRI, Tmp, Src)) {
      ++I;
      continue;
    }

    auto StoreI = nextNonDebug(I, MBB);
    Register StoreSrc;
    Register Base;
    int64_t Offset = 0;
    if (StoreI == MBB.end() || !isMemStore(*StoreI, StoreSrc, Base, Offset) ||
        !canForwardCopyIntoStore(Copy.getOpcode(), StoreI->getOpcode(), Src) ||
        !regsOverlap(TRI, StoreSrc, Tmp) || regsOverlap(TRI, Base, Tmp) ||
        (!operandIsKill(*StoreI, Tmp, TRI) &&
         !regDeadAfter(std::next(StoreI), MBB, Tmp, TRI))) {
      ++I;
      continue;
    }

    MachineOperand &StoreSrcMO = StoreI->getOperand(0);
    StoreSrcMO.setReg(Src);
    StoreSrcMO.setIsKill(false);
    Copy.eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

static bool isExtMemLoadOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
  case Bedrock::EXTZQ8rm:
  case Bedrock::EXTZQ16rm:
  case Bedrock::EXTZQ32rm:
  case Bedrock::EXTSQ8rm:
  case Bedrock::EXTSQ16rm:
  case Bedrock::EXTSQ32rm:
  case Bedrock::EXTZL8rm:
  case Bedrock::EXTZL16rm:
  case Bedrock::EXTSL8rm:
  case Bedrock::EXTSL16rm:
  case Bedrock::EXTZQ8absrm:
  case Bedrock::EXTZQ16absrm:
  case Bedrock::EXTZQ32absrm:
  case Bedrock::EXTSQ8absrm:
  case Bedrock::EXTSQ16absrm:
  case Bedrock::EXTSQ32absrm:
  case Bedrock::EXTZL8absrm:
  case Bedrock::EXTZL16absrm:
  case Bedrock::EXTSL8absrm:
  case Bedrock::EXTSL16absrm:
    return true;
  }
}

static bool extMemLoadCanDefineDReg(unsigned Opcode, Register Reg) {
  switch (Opcode) {
  default:
    return false;
  case Bedrock::EXTZQ8rm:
  case Bedrock::EXTZQ16rm:
  case Bedrock::EXTZQ32rm:
  case Bedrock::EXTSQ8rm:
  case Bedrock::EXTSQ16rm:
  case Bedrock::EXTSQ32rm:
  case Bedrock::EXTZQ8absrm:
  case Bedrock::EXTZQ16absrm:
  case Bedrock::EXTZQ32absrm:
  case Bedrock::EXTSQ8absrm:
  case Bedrock::EXTSQ16absrm:
  case Bedrock::EXTSQ32absrm:
    return isDReg(Reg);
  case Bedrock::EXTZL8rm:
  case Bedrock::EXTZL16rm:
  case Bedrock::EXTSL8rm:
  case Bedrock::EXTSL16rm:
  case Bedrock::EXTZL8absrm:
  case Bedrock::EXTZL16absrm:
  case Bedrock::EXTSL8absrm:
  case Bedrock::EXTSL16absrm:
    return isDReg(Reg);
  }
}

static MachineInstr *
findTrailingExtMemLoadDef(MachineBasicBlock &MBB, Register Alias, Register CopyDst,
                          const TargetRegisterInfo &TRI) {
  for (auto RI = MBB.rbegin(), RE = MBB.rend(); RI != RE; ++RI) {
    MachineInstr &MI = *RI;
    if (MI.isDebugInstr())
      continue;
    if (instrHasRegMaskForReg(MI, Alias, TRI) ||
        instrHasRegMaskForReg(MI, CopyDst, TRI))
      return nullptr;

    bool TouchesAlias = instrTouchesReg(MI, Alias, TRI);
    bool TouchesDst = instrTouchesReg(MI, CopyDst, TRI);
    if (!TouchesAlias && !TouchesDst)
      continue;
    if (TouchesDst)
      return nullptr;
    if (isExtMemLoadOpcode(MI.getOpcode()) && MI.getNumOperands() >= 1 &&
        MI.getOperand(0).isReg() &&
        regsOverlap(TRI, MI.getOperand(0).getReg(), Alias) &&
        extMemLoadCanDefineDReg(MI.getOpcode(), CopyDst))
      return &MI;
    return nullptr;
  }
  return nullptr;
}

bool BedrockPeephole::foldCrossBlockExtMemResultCopies(
    MachineFunction &MF) const {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  bool LocalChanged = true;
  while (LocalChanged) {
    LocalChanged = false;

    for (MachineBasicBlock &CopyMBB : MF) {
      if (CopyMBB.pred_size() != 1)
        continue;

      for (auto I = CopyMBB.begin(); I != CopyMBB.end(); ++I) {
        MachineInstr &Copy = *I;
        if (Copy.isDebugInstr() || Copy.getOpcode() != Bedrock::MOV64rr ||
            Copy.getNumOperands() < 2 || !Copy.getOperand(0).isReg() ||
            !Copy.getOperand(1).isReg())
          continue;

        Register CopyDst = Copy.getOperand(0).getReg();
        Register Alias = Copy.getOperand(1).getReg();
        if (!isDReg(CopyDst) || !isAReg(Alias) || CopyMBB.isLiveIn(CopyDst) ||
            !CopyMBB.isLiveIn(Alias) || regsOverlap(TRI, CopyDst, Alias))
          continue;

        bool Failed = false;
        for (auto Scan = CopyMBB.begin(); Scan != I; ++Scan) {
          if (Scan->isDebugInstr())
            continue;
          if (instrHasRegMaskForReg(*Scan, Alias, TRI) ||
              instrHasRegMaskForReg(*Scan, CopyDst, TRI) ||
              instrTouchesReg(*Scan, Alias, TRI) ||
              instrTouchesReg(*Scan, CopyDst, TRI)) {
            Failed = true;
            break;
          }
        }
        if (Failed)
          continue;

        for (auto Scan = nextNonDebug(I, CopyMBB); Scan != CopyMBB.end();
             Scan = nextNonDebug(Scan, CopyMBB)) {
          if (instrHasRegMaskForReg(*Scan, Alias, TRI) ||
              instrTouchesReg(*Scan, Alias, TRI)) {
            Failed = true;
            break;
          }
        }
        if (Failed)
          continue;

        SmallVector<MachineBasicBlock *, 4> Path;
        Path.push_back(&CopyMBB);

        MachineBasicBlock *NextOnPath = &CopyMBB;
        MachineBasicBlock *Cur = CopyMBB.getSinglePredecessor();
        MachineInstr *ExtDef = nullptr;
        MachineBasicBlock *DefMBB = nullptr;
        while (Cur) {
          for (MachineBasicBlock *Succ : Cur->successors()) {
            if (Succ == NextOnPath)
              continue;
            if (Succ->isLiveIn(Alias) || Succ->isLiveIn(CopyDst)) {
              Failed = true;
              break;
            }
          }
          if (Failed)
            break;

          if (MachineInstr *Def = findTrailingExtMemLoadDef(*Cur, Alias, CopyDst,
                                                            TRI)) {
            if (Cur->succ_size() != 1 || *Cur->succ_begin() != NextOnPath) {
              Failed = true;
              break;
            }
            ExtDef = Def;
            DefMBB = Cur;
            break;
          }

          for (MachineInstr &MI : *Cur) {
            if (MI.isDebugInstr())
              continue;
            if (instrHasRegMaskForReg(MI, Alias, TRI) ||
                instrHasRegMaskForReg(MI, CopyDst, TRI) ||
                instrTouchesReg(MI, Alias, TRI) ||
                instrTouchesReg(MI, CopyDst, TRI)) {
              Failed = true;
              break;
            }
          }
          if (Failed || Cur->pred_size() != 1)
            break;

          Path.push_back(Cur);
          NextOnPath = Cur;
          Cur = Cur->getSinglePredecessor();
        }

        if (Failed || !ExtDef || !DefMBB)
          continue;

        ExtDef->getOperand(0).setReg(CopyDst);
        Copy.eraseFromParent();
        for (MachineBasicBlock *MBB : Path) {
          if (MBB->isLiveIn(Alias))
            MBB->removeLiveIn(Alias);
          if (!MBB->isLiveIn(CopyDst))
            MBB->addLiveIn(CopyDst);
        }

        Changed = true;
        LocalChanged = true;
        break;
      }

      if (LocalChanged)
        break;
    }
  }

  return Changed;
}

bool BedrockPeephole::foldRegCopyCoalescing(MachineBasicBlock &MBB,
                                            MachineFunction &MF) const {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  auto CanRewriteOperand = [&](const MachineOperand &MO, Register Reg) {
    return operandTouchesReg(MO, Reg, TRI) && !MO.isImplicit() &&
           MO.getSubReg() == 0;
  };

  auto ReachesRetLiveOut = [&](MachineBasicBlock::iterator From, Register Reg) {
    return (Reg == Bedrock::D0 || Reg == Bedrock::D1 ||
            Reg == Bedrock::A0) &&
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
    bool SameCopyBank =
        (isDReg(Alias) && isDReg(Src)) ||
        ((isAReg(Alias) || Alias == Bedrock::SP) &&
         (isAReg(Src) || Src == Bedrock::SP));
    if (!SameCopyBank || Alias == Bedrock::SP || Src == Bedrock::SP ||
        regsOverlap(TRI, Alias, Src)) {
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

      if (ClobbersSrcValue && TouchesSrc) {
        Failed = true;
        break;
      }

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

bool BedrockPeephole::foldIndexedMem(MachineBasicBlock &MBB,
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

    bool BaseIsPtr = isAReg(Base) || Base == Bedrock::SP;
    bool KeepAddrBase = false;
    if (!BaseIsPtr) {
      if (ExtI->getOpcode() != Bedrock::MOV64rr || !isDReg(Base))
        return false;
      if (MF.getFunction().hasMinSize() || MF.getFunction().hasOptSize())
        return false;
      KeepAddrBase = true;
    }

    if (!isAReg(Addr) || Addr == Bedrock::SP ||
        regsOverlap(TRI, Addr, Base))
      return false;
    Register IndexedBase = KeepAddrBase ? Addr : Base;
    int64_t IndexedBaseOffset = KeepAddrBase ? 0 : BaseOffset;

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

    Register ScaledIndex = regsOverlap(TRI, AddI->getOperand(1).getReg(), Addr)
                               ? AddI->getOperand(2).getReg()
                               : AddI->getOperand(1).getReg();
    if (!isDReg(ScaledIndex) || regsOverlap(TRI, ScaledIndex, IndexedBase) ||
        (KeepAddrBase && regsOverlap(TRI, ScaledIndex, Base)))
      return false;

    Register Index = ScaledIndex;
    unsigned IndexedScale = 1;
    bool LongIndex = false;
    MachineInstr *ScaledShl = nullptr;
    MachineInstr *ScaledExt = nullptr;
    MachineInstr *IndexDef = findLastDefBefore(*ExtI, ScaledIndex, TRI);
    if (IndexDef && IndexDef->getOpcode() == Bedrock::SHL64ri &&
        IndexDef->getNumOperands() >= 3 && IndexDef->getOperand(0).isReg() &&
        IndexDef->getOperand(1).isReg() && IndexDef->getOperand(2).isImm() &&
        IndexDef->getOperand(2).getImm() == 2 &&
        regsOverlap(TRI, IndexDef->getOperand(0).getReg(), ScaledIndex) &&
        regsOverlap(TRI, IndexDef->getOperand(1).getReg(), ScaledIndex)) {
      ScaledShl = IndexDef;
      ScaledExt = findLastDefBefore(*ScaledShl, ScaledIndex, TRI);
      if (!ScaledExt || ScaledExt->getParent() != &MBB ||
          (ScaledExt->getOpcode() != Bedrock::EXTZQ32rr &&
           ScaledExt->getOpcode() != Bedrock::EXTSQ32rr) ||
          ScaledExt->getNumOperands() < 2 ||
          !ScaledExt->getOperand(0).isReg() ||
          !ScaledExt->getOperand(1).isReg() ||
          !regsOverlap(TRI, ScaledExt->getOperand(0).getReg(),
                       ScaledIndex) ||
          !isDReg(ScaledExt->getOperand(1).getReg()) ||
          (instrDefinesReg(*ScaledShl, Bedrock::FLAGS, TRI) &&
           !regDefDeadOrDeadAfter(ScaledShl->getIterator(), MBB,
                                  Bedrock::FLAGS, TRI)) ||
          (instrDefinesReg(*ScaledExt, Bedrock::FLAGS, TRI) &&
           !regDefDeadOrDeadAfter(ScaledExt->getIterator(), MBB,
                                  Bedrock::FLAGS, TRI)))
        return false;

      Index = ScaledExt->getOperand(1).getReg();
      if (regsOverlap(TRI, Index, IndexedBase) ||
          (KeepAddrBase && regsOverlap(TRI, Index, Base)))
        return false;

      for (auto Scan = nextNonDebug(ScaledExt->getIterator(), MBB);
           Scan != MBB.end(); Scan = nextNonDebug(Scan, MBB)) {
        if (&*Scan == ScaledShl || &*Scan == &*ExtI) {
          continue;
        }
        if (&*Scan == &*AddI)
          break;
        if (instrUsesReg(*Scan, ScaledIndex, TRI) ||
            instrDefinesReg(*Scan, Index, TRI) ||
            instrDefinesReg(*Scan, IndexedBase, TRI))
          return false;
      }
      if (!regUnusedAfterInCFG(nextNonDebug(AddI, MBB), MBB, ScaledIndex, TRI))
        return false;

      IndexedScale = 4;
      LongIndex = true;
    }

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
        Use.NewOpcode =
            getIndexedMemLoadOpcode(MI.getOpcode(), IndexedScale, LongIndex);
        return Use.NewOpcode != 0;
      case Bedrock::MOV8mr:
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
        Use.NewOpcode =
            getIndexedMemStoreOpcode(MI.getOpcode(), IndexedScale, LongIndex);
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
          if (IndexedScale == 1 && !LongIndex)
            Use.NewOpcode = Bedrock::MOV32idx1mm;
          else if (IndexedScale == 4 && LongIndex)
            Use.NewOpcode = Bedrock::MOV32idx4lmm;
          else
            return false;
        } else {
          Use.Kind = Scale1IndexedUse::MemToMemDst;
          Use.OtherBase = MI.getOperand(0).getReg();
          Use.OtherOffset = MI.getOperand(1).getImm();
          Use.Offset = MI.getOperand(3).getImm();
          if (IndexedScale == 1 && !LongIndex)
            Use.NewOpcode = Bedrock::MOV32midx1;
          else if (IndexedScale == 4 && LongIndex)
            Use.NewOpcode = Bedrock::MOV32midx4l;
          else
            return false;
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
        Use.NewOpcode =
            getIndexedMemSourceOpcode(MI.getOpcode(), IndexedScale, LongIndex);
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
            IndexedScale, LongIndex, false);
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
            IndexedScale, LongIndex, true);
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
        Use.NewOpcode = getIndexedMemImmFlagOpcode(MI.getOpcode(),
                                                   IndexedScale, LongIndex);
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
        if (IndexedScale == 1 && !LongIndex)
          Use.NewOpcode = MI.getOpcode() == Bedrock::INC32m
                              ? Bedrock::INC32idx1m
                              : Bedrock::DEC32idx1m;
        else if (IndexedScale == 4 && LongIndex)
          Use.NewOpcode = MI.getOpcode() == Bedrock::INC32m
                              ? Bedrock::INC32idx4lm
                              : Bedrock::DEC32idx4lm;
        else
          return false;
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
            instrHasRegMaskForReg(*Scan, IndexedBase, TRI) ||
            instrHasRegMaskForReg(*Scan, Index, TRI))
          return false;

        bool UsesAddr = instrUsesReg(*Scan, Addr, TRI);
        if (!UsesAddr) {
          if (instrDefinesReg(*Scan, Addr, TRI))
            break;
          if (instrDefinesReg(*Scan, IndexedBase, TRI) ||
              instrDefinesReg(*Scan, Index, TRI))
            return false;
          ++Scan;
          continue;
        }

        Scale1IndexedUse Use;
        if (!MatchUse(*Scan, Use))
          return false;
        bool DefinesIndex = instrDefinesReg(*Scan, Index, TRI);
        if (instrDefinesReg(*Scan, IndexedBase, TRI) ||
            (DefinesIndex && Use.Kind != Scale1IndexedUse::Load))
          return false;
        Uses.push_back(Use);
        if (Uses.size() > 4)
          return false;
        if (DefinesIndex) {
          auto Next = nextNonDebug(Scan, *Block);
          if (!regUnusedAfterInCFG(Next, *Block, Addr, TRI))
            return false;
          break;
        }
        ++Scan;
      }
    }

    if (Uses.empty())
      return false;

    for (MachineBasicBlock *Block : Region) {
      if (Block == &MBB)
        continue;
      if (IndexedBase != Bedrock::SP && !Block->isLiveIn(IndexedBase))
        Block->addLiveIn(IndexedBase);
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
            .addReg(IndexedBase)
            .addReg(Index)
            .addImm(IndexedBaseOffset + Use.Offset);
        break;
      case Scale1IndexedUse::Store:
        MIB.addReg(Use.Reg)
            .addReg(IndexedBase)
            .addReg(Index)
            .addImm(IndexedBaseOffset + Use.Offset);
        break;
      case Scale1IndexedUse::MemToMemSrc:
        MIB.addReg(IndexedBase)
            .addReg(Index)
            .addImm(IndexedBaseOffset + Use.Offset)
            .addReg(Use.OtherBase)
            .addImm(Use.OtherOffset);
        break;
      case Scale1IndexedUse::MemToMemDst:
        MIB.addReg(Use.OtherBase)
            .addImm(Use.OtherOffset)
            .addReg(IndexedBase)
            .addReg(Index)
            .addImm(IndexedBaseOffset + Use.Offset);
        break;
      case Scale1IndexedUse::BinRM:
        MIB.addReg(Use.Reg, RegState::Define)
            .addReg(MI.getOperand(1).getReg())
            .addReg(IndexedBase)
            .addReg(Index)
            .addImm(IndexedBaseOffset + Use.Offset);
        break;
      case Scale1IndexedUse::CmpRM:
      case Scale1IndexedUse::TestRM:
        MIB.addReg(Use.Reg)
            .addReg(IndexedBase)
            .addReg(Index)
            .addImm(IndexedBaseOffset + Use.Offset);
        break;
      case Scale1IndexedUse::CmpMR:
      case Scale1IndexedUse::TestMR:
        MIB.addReg(Use.Reg)
            .addReg(IndexedBase)
            .addReg(Index)
            .addImm(IndexedBaseOffset + Use.Offset);
        break;
      case Scale1IndexedUse::ImmFlag:
        MIB.addImm(Use.Imm)
            .addReg(IndexedBase)
            .addReg(Index)
            .addImm(IndexedBaseOffset + Use.Offset);
        break;
      case Scale1IndexedUse::Inc:
      case Scale1IndexedUse::Dec:
        MIB.addReg(IndexedBase).addReg(Index).addImm(IndexedBaseOffset +
                                                     Use.Offset);
        break;
      }
      MIB.cloneMemRefs(MI);
    }

    for (Scale1IndexedUse &Use : Uses)
      Use.MI->eraseFromParent();
    if (!KeepAddrBase)
      ExtI->eraseFromParent();
    AddI->eraseFromParent();
    if (ScaledShl)
      ScaledShl->eraseFromParent();
    if (ScaledExt)
      ScaledExt->eraseFromParent();
    if (!KeepAddrBase)
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

    if ((Ext.getOpcode() == Bedrock::EXTZQ8rr ||
         Ext.getOpcode() == Bedrock::EXTZQ32rr) &&
        Ext.getNumOperands() >= 2 && Ext.getOperand(0).isReg() &&
        isDReg(Ext.getOperand(0).getReg())) {
      Register Index = Ext.getOperand(0).getReg();
      auto ShlI = nextNonDebug(I, MBB);
      if (ShlI != MBB.end() && ShlI->getOpcode() == Bedrock::SHL64ri &&
          ShlI->getNumOperands() >= 3 && ShlI->getOperand(0).isReg() &&
          ShlI->getOperand(1).isReg() && ShlI->getOperand(2).isImm() &&
          ShlI->getOperand(2).getImm() == 2 &&
          regsOverlap(TRI, ShlI->getOperand(0).getReg(), Index) &&
          regsOverlap(TRI, ShlI->getOperand(1).getReg(), Index) &&
          regDefDeadOrDeadAfter(ShlI, MBB, Bedrock::FLAGS, TRI)) {
        auto MemI = nextNonDebug(ShlI, MBB);
        unsigned NewOpcode =
            MemI == MBB.end()
                ? 0
                : getScale4LongOpcodeForScale1Indexed(MemI->getOpcode());
        if (MemI != MBB.end() && MemI->getOpcode() == Bedrock::MOV32midx1 &&
            MemI->getNumOperands() >= 5 && MemI->getOperand(0).isReg() &&
            MemI->getOperand(1).isImm() && MemI->getOperand(2).isReg() &&
            MemI->getOperand(3).isReg() && MemI->getOperand(4).isImm() &&
            regsOverlap(TRI, MemI->getOperand(3).getReg(), Index)) {
          auto AfterMem = nextNonDebug(MemI, MBB);
          bool IndexKilled = operandIsKill(*MemI, Index, TRI);
          if (IndexKilled || regUnusedAfterInCFG(AfterMem, MBB, Index, TRI)) {
            MachineInstrBuilder MIB =
                BuildMI(MBB, ShlI, MemI->getDebugLoc(),
                        TII.get(Bedrock::MOV32midx4l));
            MIB.addReg(MemI->getOperand(0).getReg())
                .addImm(MemI->getOperand(1).getImm())
                .addReg(MemI->getOperand(2).getReg())
                .addReg(Index, getKillRegState(IndexKilled))
                .addImm(MemI->getOperand(4).getImm());
            MIB.cloneMemRefs(*MemI);
            ShlI->eraseFromParent();
            MemI->eraseFromParent();
            I = MBB.begin();
            Changed = true;
            continue;
          }
        }

        if (NewOpcode != 0 && MemI->getNumOperands() >= 4 &&
            MemI->getOperand(1).isReg() && MemI->getOperand(2).isReg() &&
            MemI->getOperand(3).isImm() &&
            regsOverlap(TRI, MemI->getOperand(2).getReg(), Index)) {
          auto AfterMem = nextNonDebug(MemI, MBB);
          bool IndexKilled = operandIsKill(*MemI, Index, TRI);
          if (instrDefinesReg(*MemI, Index, TRI) || IndexKilled ||
              regUnusedAfterInCFG(AfterMem, MBB, Index, TRI)) {
            MachineInstrBuilder MIB =
                BuildMI(MBB, ShlI, MemI->getDebugLoc(), TII.get(NewOpcode));
            if (MemI->getOperand(0).isReg())
              MIB.addReg(MemI->getOperand(0).getReg(),
                         getDefRegState(MemI->getOperand(0).isDef()) |
                             getKillRegState(MemI->getOperand(0).isKill()));
            MIB.addReg(MemI->getOperand(1).getReg())
                .addReg(Index, getKillRegState(IndexKilled))
                .addImm(MemI->getOperand(3).getImm());
            MIB.cloneMemRefs(*MemI);
            ShlI->eraseFromParent();
            MemI->eraseFromParent();
            I = MBB.begin();
            Changed = true;
            continue;
          }
        }
      }
    }

    if ((Ext.getOpcode() == Bedrock::LEAri && Ext.getNumOperands() >= 3 &&
         Ext.getOperand(0).isReg() && Ext.getOperand(1).isReg() &&
         Ext.getOperand(2).isImm()) ||
        (Ext.getOpcode() == Bedrock::MOV64rr && Ext.getNumOperands() >= 2 &&
         Ext.getOperand(0).isReg() && Ext.getOperand(1).isReg())) {
      Register Addr = Ext.getOperand(0).getReg();
      Register CopySrc = Ext.getOperand(1).getReg();
      Register Base = CopySrc;
      int64_t BaseOffset = 0;
      if (Ext.getOpcode() == Bedrock::LEAri) {
        BaseOffset = Ext.getOperand(2).getImm();
      }
      if (!isAReg(Addr) || Addr == Bedrock::SP ||
          regsOverlap(TRI, Addr, CopySrc)) {
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

      bool AddrIsLHS = regsOverlap(TRI, AddI->getOperand(1).getReg(), Addr);
      bool AddrIsRHS = regsOverlap(TRI, AddI->getOperand(2).getReg(), Addr);
      if (AddrIsLHS == AddrIsRHS) {
        ++I;
        continue;
      }
      Register Other = AddrIsLHS ? AddI->getOperand(2).getReg()
                                 : AddI->getOperand(1).getReg();
      Register Index;
      if (Ext.getOpcode() == Bedrock::LEAri || isPtrReg(CopySrc)) {
        Base = CopySrc;
        Index = Other;
      } else if (isDReg(CopySrc) && isPtrReg(Other)) {
        Base = Other;
        Index = CopySrc;
      } else {
        ++I;
        continue;
      }
      if (!isPtrReg(Base) || !isDReg(Index)) {
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
      case Bedrock::MOV8mr:
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
        Ext.getOperand(1).isReg() && isAReg(Ext.getOperand(0).getReg()) &&
        isIntReg(Ext.getOperand(1).getReg())) {
      Register Addr = Ext.getOperand(0).getReg();
      Register Index32 = Ext.getOperand(1).getReg();
      auto AddI = nextNonDebug(I, MBB);
      if (AddI == MBB.end() || AddI->getOpcode() != Bedrock::ADD64rr ||
          AddI->getNumOperands() < 3 || !AddI->getOperand(0).isReg() ||
          !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg() ||
          !regsOverlap(TRI, AddI->getOperand(0).getReg(), Addr) ||
          !regsOverlap(TRI, AddI->getOperand(1).getReg(), Addr) ||
          (instrDefinesReg(*AddI, Bedrock::FLAGS, TRI) &&
           !regDefDeadOrDeadAfterInCFG(AddI, MBB, Bedrock::FLAGS, TRI))) {
        ++I;
        continue;
      }

      Register Base = AddI->getOperand(2).getReg();
      if (!(isAReg(Base) || Base == Bedrock::SP) ||
          regsOverlap(TRI, Base, Addr)) {
        ++I;
        continue;
      }

      auto MemI = MBB.end();
      unsigned NewMemOpcode = 0;
      Register MemReg;
      int64_t MemOffset = 0;
      bool IsLoad = false;
      for (auto Scan = nextNonDebug(AddI, MBB); Scan != MBB.end();
           Scan = nextNonDebug(Scan, MBB)) {
        if (Scan->isCall() || Scan->isBranch() || Scan->isTerminator() ||
            instrHasRegMaskForReg(*Scan, Addr, TRI) ||
            instrHasRegMaskForReg(*Scan, Base, TRI) ||
            instrHasRegMaskForReg(*Scan, Index32, TRI))
          break;

        if (Scan->getNumOperands() >= 3 && Scan->getOperand(0).isReg() &&
            Scan->getOperand(1).isReg() && Scan->getOperand(2).isImm() &&
            regsOverlap(TRI, Scan->getOperand(1).getReg(), Addr)) {
          unsigned LoadOpcode =
              getIndexedMemLoadOpcode(Scan->getOpcode(), 1, false);
          unsigned StoreOpcode =
              getIndexedMemStoreOpcode(Scan->getOpcode(), 1, false);
          if (LoadOpcode != 0 || StoreOpcode != 0) {
            MemI = Scan;
            NewMemOpcode = LoadOpcode != 0 ? LoadOpcode : StoreOpcode;
            MemReg = Scan->getOperand(0).getReg();
            MemOffset = Scan->getOperand(2).getImm();
            IsLoad = LoadOpcode != 0;
            break;
          }
        }

        if (instrTouchesReg(*Scan, Addr, TRI) || instrTouchesReg(*Scan, Base, TRI) ||
            instrTouchesReg(*Scan, Index32, TRI))
          break;
      }

      if (MemI == MBB.end()) {
        ++I;
        continue;
      }

      auto AfterMem = nextNonDebug(MemI, MBB);
      if (!regUnusedBeforeEndOrDef(AfterMem, MBB, Addr, TRI)) {
        ++I;
        continue;
      }

      auto FindScopedScratch = [&](Register AvoidA, Register AvoidB) {
        for (Register Reg = Bedrock::D0; Reg <= Bedrock::D7;
             Reg = Register(Reg + 1)) {
          if (regsOverlap(TRI, Reg, AvoidA) || regsOverlap(TRI, Reg, AvoidB))
            continue;
          if (!regDeadAfter(AfterMem, MBB, Reg, TRI))
            continue;
          bool Touched = false;
          for (auto Scan = nextNonDebug(I, MBB); Scan != MemI;
               Scan = nextNonDebug(Scan, MBB)) {
            if (instrTouchesReg(*Scan, Reg, TRI) ||
                instrHasRegMaskForReg(*Scan, Reg, TRI)) {
              Touched = true;
              break;
            }
          }
          if (!Touched)
            return Reg;
        }
        return Register();
      };

      Register Scratch = FindScopedScratch(Index32, MemReg);
      bool ReuseIndexReg = false;
      if (!Scratch && isDReg(Index32) && !regsOverlap(TRI, Index32, MemReg) &&
          regUnusedBeforeEndOrDef(AfterMem, MBB, Index32, TRI)) {
        Scratch = Index32;
        ReuseIndexReg = true;
      }
      if (!Scratch) {
        ++I;
        continue;
      }

      BuildMI(MBB, Ext.getIterator(), Ext.getDebugLoc(),
              TII.get(Ext.getOpcode()), Scratch)
          .addReg(Index32, getKillRegState(ReuseIndexReg || operandIsKill(Ext, Index32, TRI)));
      MachineInstrBuilder NewMem = BuildMI(MBB, MemI->getIterator(),
                                           MemI->getDebugLoc(),
                                           TII.get(NewMemOpcode));
      if (IsLoad)
        NewMem.addReg(MemReg, RegState::Define);
      else
        NewMem.addReg(MemReg,
                      getKillRegState(operandIsKill(*MemI, MemReg, TRI)));
      NewMem.addReg(Base).addReg(Scratch, RegState::Kill).addImm(MemOffset);
      NewMem.cloneMemRefs(*MemI);

      Ext.eraseFromParent();
      AddI->eraseFromParent();
      MemI->eraseFromParent();
      I = MBB.begin();
      Changed = true;
      continue;
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

bool BedrockPeephole::foldIndexedMemFromDataBase(
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

bool BedrockPeephole::foldIndexedAddFromAbsBase(MachineBasicBlock &MBB,
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
