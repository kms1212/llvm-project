//===-- BedrockPeephole.h - Bedrock post-RA peepholes -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_BEDROCKPEEPHOLE_H
#define LLVM_LIB_TARGET_BEDROCK_BEDROCKPEEPHOLE_H

#include "Bedrock.h"
#include "BedrockInstrInfo.h"
#include "BedrockSubtarget.h"
#include "MCTargetDesc/BedrockCondCode.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Alignment.h"
#include <algorithm>
#include <iterator>
#include <limits>
#include <optional>
#include <utility>


using namespace llvm;

#define BEDROCK_PEEPHOLE_PASS_NAME "Bedrock post-RA peephole optimizations"

class BedrockPeephole : public MachineFunctionPass {
  BedrockPeepholeProfile Profile = BedrockPeepholeProfile::O0;

public:
  static char ID;

  BedrockPeephole(BedrockPeepholeProfile Profile)
      : MachineFunctionPass(ID), Profile(Profile) {
    initializeBedrockPeepholePass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return BEDROCK_PEEPHOLE_PASS_NAME; }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  bool foldIdentityMoves(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldArgTruncBitOps(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldCompactUnary(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldDecCmpBranch(MachineBasicBlock &MBB, MachineFunction &MF,
                        uint32_t KnownZeroIn) const;
  bool foldCountedBranchShortcuts(MachineBasicBlock &MBB,
                                  MachineFunction &MF) const;
  bool foldTopTestIncLoopToIJcc(MachineFunction &MF) const;
  bool foldSequentialEqImmCompareChain(MachineFunction &MF) const;
  bool foldKnownZeroCmp(MachineBasicBlock &MBB, MachineFunction &MF,
                        uint32_t KnownZeroIn) const;
  bool foldEqNeZeroCmpToTest(MachineBasicBlock &MBB, MachineFunction &MF,
                             uint32_t KnownZeroIn) const;
  bool foldClrZeroCmpToTest(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldClrZeroMemCmp(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldAndTestToImmTest(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldCondZextAddToInc(MachineFunction &MF) const;
  bool foldMinSizeLoopCounter32(MachineFunction &MF) const;
  bool foldSelfZextI32LoopCounts(MachineFunction &MF) const;
  bool foldTopTestCountedLoop(
      MachineBasicBlock &MBB, MachineFunction &MF,
      const DenseMap<MachineBasicBlock *, uint32_t> &KnownZeroIns) const;
  bool foldPositiveCountedLoopPretest(
      MachineFunction &MF,
      const DenseMap<MachineBasicBlock *, uint32_t> &KnownZeroIns) const;
  bool foldPositiveCountedLoopHeaderPretest(
      MachineFunction &MF,
      const DenseMap<MachineBasicBlock *, uint32_t> &KnownZeroIns) const;
  bool foldSMaxIndexLoopBound(MachineFunction &MF) const;
  bool foldSMaxCountdownPretest(
      MachineFunction &MF,
      const DenseMap<MachineBasicBlock *, uint32_t> &KnownZeroIns) const;
  bool foldSMaxPretestZeroReturn(MachineFunction &MF) const;
  bool foldCountedLoopIndexResult(
      MachineFunction &MF,
      const DenseMap<MachineBasicBlock *, uint32_t> &KnownZeroIns) const;
  bool foldNarrowLoopCountCopies(MachineFunction &MF) const;
  bool foldAccumulatorLoopUseInputCount(MachineFunction &MF) const;
  bool foldDeadPlainDefs(MachineFunction &MF) const;
  bool foldDeadClrs(MachineFunction &MF) const;
  bool foldZeroRegCopies(MachineFunction &MF) const;
  bool foldFallthroughJumps(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldPrologue(MachineFunction &MF) const;
  bool foldEpilogue(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldFPrologue(MachineFunction &MF) const;
  bool foldFEpilogue(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldMemoryBitOps(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldMemoryBinStore(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldMemoryBinStoreAcrossDef(MachineBasicBlock &MBB,
                                   MachineFunction &MF) const;
  bool foldMemoryBinStoreWithLoadedSource(MachineBasicBlock &MBB,
                                          MachineFunction &MF) const;
  bool foldMemoryImmBinStore(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldMemoryImmFlagOp(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldMemoryRegFlagOp(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldKnownZeroByteStoreToBSet(MachineBasicBlock &MBB,
                                    MachineFunction &MF) const;
  bool foldIndexedZeroStore(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldByteLoadTestZeroBranch(MachineBasicBlock &MBB,
                                  MachineFunction &MF) const;
  bool foldByteLoadKnownZeroCmpBranch(MachineBasicBlock &MBB,
                                      MachineFunction &MF,
                                      uint32_t KnownZeroIn) const;
  bool foldImmCmp(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldCmpOneBranch(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldImmMul(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldImmStore(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldSmallMov64Imm(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldClrStore(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldAImmCopyToDImm(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldMemCopy(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldEntryLiveInStores(MachineFunction &MF) const;
  bool foldStackStoreLoadForward(MachineBasicBlock &MBB,
                                 MachineFunction &MF) const;
  bool foldStackPointerCopyMemBase(MachineBasicBlock &MBB,
                                   MachineFunction &MF) const;
  bool foldA6BaseCopyStackSpill(MachineFunction &MF) const;
  bool foldStackReloadFromZextCount(MachineFunction &MF) const;
  bool foldStackConstLoads(MachineFunction &MF) const;
  bool foldStackZeroCmp(MachineFunction &MF) const;
  bool foldStackSlotsToARegs(MachineFunction &MF) const;
  bool foldSmallConstMultiply(MachineFunction &MF) const;
  bool foldMinSizeDivmodConstAccumulate(MachineFunction &MF) const;
  bool foldDeadFrameTopPadding(MachineFunction &MF) const;
  bool foldDeadStackAdjust(MachineFunction &MF) const;
  bool foldTailCallReturn(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldLoadOp(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldLoopCarriedLoadUpdateCopies(MachineBasicBlock &MBB,
                                       MachineFunction &MF) const;
  bool foldLoopCarriedLoadAddCopies(MachineBasicBlock &MBB,
                                    MachineFunction &MF) const;
  bool foldLoadMAdd(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldRegMAddAccumulator(MachineBasicBlock &MBB,
                              MachineFunction &MF) const;
  bool foldProductChainMAdd(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldShiftedBSet(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool shrinkUnusedPushPopMask(MachineFunction &MF) const;
  bool foldDivMod(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldPostInc(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldCrossBlockPostInc(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldMinSizeMAddWindowBaseBias(MachineFunction &MF) const;
  bool foldLea(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldStackBaseLeaOffsets(MachineBasicBlock &MBB,
                               MachineFunction &MF) const;
  bool foldStackBaseBiasOriginalUses(MachineFunction &MF) const;
  bool foldRepeatedStackAddressLeas(MachineFunction &MF) const;
  bool foldRepeatedStackAddressLeasWithBorrowedBase(MachineFunction &MF) const;
  bool foldRepeatedStackAddressLeasWithScopedBase(MachineFunction &MF) const;
  bool foldShortLeaAliasCopies(MachineBasicBlock &MBB,
                               MachineFunction &MF) const;
  bool foldARegAliasCopies(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldAliasBackCopies(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldDRegCopyCoalescing(MachineBasicBlock &MBB,
                              MachineFunction &MF) const;
  bool foldIndexedMem(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldIndexedMemFromDataBase(MachineBasicBlock &MBB,
                                  MachineFunction &MF) const;
  bool foldIndexedAddFromAbsBase(MachineBasicBlock &MBB,
                                 MachineFunction &MF) const;
  bool foldSum(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldSumReturnCopy(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldSignedClampReturn(MachineBasicBlock &MBB, MachineFunction &MF) const;
  bool foldMinMaxBranchDiamond(MachineFunction &MF) const;
  bool foldMinSizeA32ToCalleeSavedDRegs(MachineFunction &MF) const;
  bool foldSingleInstructionRepLoops(MachineFunction &MF) const;
  bool foldByteIndexedMemUtilityLoops(MachineFunction &MF) const;
  bool foldWideZeroFillDjtLoops(MachineFunction &MF) const;
  bool foldPositiveConstRepPretests(MachineFunction &MF) const;
  bool foldZeroExitRepgTailLoops(MachineFunction &MF) const;
  bool foldAscendingLoadProgressionLoops(MachineFunction &MF) const;
  bool foldAscendingStoreProgressionLoops(MachineFunction &MF) const;
  bool foldAscendingAddressStoreLoops(MachineFunction &MF) const;
  bool foldAscendingConstStoreLoops(MachineFunction &MF) const;
  bool foldAscendingMultiStoreLoops(MachineFunction &MF) const;
  bool foldMixedZeroStoreLoops(MachineFunction &MF) const;
  bool foldByteZeroOffsetStoreLoops(MachineFunction &MF) const;
  bool foldByteOffsetStoreLoops(MachineFunction &MF) const;
  bool foldScanUntilZeroRepne(MachineFunction &MF) const;
  bool normalizeA32Arithmetic(MachineBasicBlock &MBB,
                              MachineFunction &MF) const;
  bool legalizeLargeStackAdjustments(MachineFunction &MF) const;
};

static inline bool profileAtLeast(BedrockPeepholeProfile Profile,
                           BedrockPeepholeProfile Level) {
  return static_cast<unsigned>(Profile) >= static_cast<unsigned>(Level);
}

static inline std::optional<unsigned> getMaskBit(Register Reg) {
  if (Reg >= Bedrock::D0 && Reg <= Bedrock::D7)
    return Reg - Bedrock::D0;
  if (Reg >= Bedrock::A0 && Reg <= Bedrock::A7)
    return 8 + Reg - Bedrock::A0;
  return std::nullopt;
}

static inline std::optional<unsigned> getFMaskBit(Register Reg) {
  if (Reg >= Bedrock::F0 && Reg <= Bedrock::F15)
    return Reg - Bedrock::F0;
  return std::nullopt;
}

static inline bool isDReg(Register Reg) {
  return Reg >= Bedrock::D0 && Reg <= Bedrock::D7;
}

static inline bool isAReg(Register Reg) {
  return Reg >= Bedrock::A0 && Reg <= Bedrock::A7;
}

static inline bool isPtrReg(Register Reg) {
  return isAReg(Reg) || Reg == Bedrock::SP || Reg == Bedrock::PC;
}

static inline bool isFReg(Register Reg) {
  return Reg >= Bedrock::F0 && Reg <= Bedrock::F15;
}

static inline bool isIntReg(Register Reg) { return isDReg(Reg) || isAReg(Reg); }

static inline bool fitsDisp16(int64_t Offset) {
  return Offset >= std::numeric_limits<int16_t>::min() &&
         Offset <= std::numeric_limits<int16_t>::max();
}

static inline Register getRegForMaskBit(unsigned Bit) {
  return Bit < 8 ? Register(Bedrock::D0 + Bit)
                 : Register(Bedrock::A0 + Bit - 8);
}

static inline Register getFRegForMaskBit(unsigned Bit) {
  return Register(Bedrock::F0 + Bit);
}

static inline std::optional<Register> getSavedDReg(uint16_t Mask) {
  for (int Bit = 7; Bit >= 0; --Bit) {
    if ((Mask & (uint16_t(1) << Bit)) != 0)
      return getRegForMaskBit(Bit);
  }
  return std::nullopt;
}

static inline unsigned popcountMask(uint16_t Mask) {
  unsigned Count = 0;
  for (unsigned Bit = 0; Bit != 16; ++Bit)
    if ((Mask & (uint16_t(1) << Bit)) != 0)
      ++Count;
  return Count;
}

static inline std::optional<Register> singleRegFromMask(uint16_t Mask) {
  if (popcountMask(Mask) != 1)
    return std::nullopt;
  for (unsigned Bit = 0; Bit != 16; ++Bit)
    if ((Mask & (uint16_t(1) << Bit)) != 0)
      return getRegForMaskBit(Bit);
  return std::nullopt;
}

static inline bool fitsImm6(int64_t Value) {
  return Value >= 0 && Value < 64 && Value != 50 && Value != 51 &&
         Value != 52;
}

static inline MachineBasicBlock::iterator nextNonDebug(MachineBasicBlock::iterator I,
                                                MachineBasicBlock &MBB) {
  for (++I; I != MBB.end() && I->isDebugInstr(); ++I)
    ;
  return I;
}

static inline MachineBasicBlock::iterator prevNonDebug(MachineBasicBlock::iterator I,
                                                MachineBasicBlock &MBB) {
  while (I != MBB.begin()) {
    --I;
    if (!I->isDebugInstr())
      return I;
  }
  return MBB.end();
}

static inline bool regsOverlap(const TargetRegisterInfo &TRI, Register A, Register B) {
  return A.isValid() && B.isValid() && TRI.regsOverlap(A, B);
}

static inline bool operandTouchesReg(const MachineOperand &MO, Register Reg,
                              const TargetRegisterInfo &TRI) {
  return MO.isReg() && regsOverlap(TRI, MO.getReg(), Reg);
}

static inline bool instrUsesReg(const MachineInstr &MI, Register Reg,
                         const TargetRegisterInfo &TRI) {
  for (const MachineOperand &MO : MI.operands())
    if (operandTouchesReg(MO, Reg, TRI) && MO.readsReg())
      return true;
  for (MCPhysReg ImpUse : MI.getDesc().implicit_uses())
    if (regsOverlap(TRI, Register(ImpUse), Reg))
      return true;
  return false;
}

static inline bool instrDefinesReg(const MachineInstr &MI, Register Reg,
                            const TargetRegisterInfo &TRI) {
  for (const MachineOperand &MO : MI.operands())
    if (operandTouchesReg(MO, Reg, TRI) && MO.isDef())
      return true;
  for (MCPhysReg ImpDef : MI.getDesc().implicit_defs())
    if (regsOverlap(TRI, Register(ImpDef), Reg))
      return true;
  return false;
}

static inline bool instrDefinesDeadReg(const MachineInstr &MI, Register Reg,
                                const TargetRegisterInfo &TRI) {
  for (const MachineOperand &MO : MI.operands())
    if (operandTouchesReg(MO, Reg, TRI) && MO.isDef() && MO.isDead())
      return true;
  return false;
}

static inline bool instrTouchesReg(const MachineInstr &MI, Register Reg,
                            const TargetRegisterInfo &TRI) {
  return instrUsesReg(MI, Reg, TRI) || instrDefinesReg(MI, Reg, TRI);
}

static inline void
collectFlowSuccessors(MachineBasicBlock &MBB,
                      SmallVectorImpl<MachineBasicBlock *> &Targets) {
  SmallPtrSet<MachineBasicBlock *, 8> Seen;
  auto AddTarget = [&](MachineBasicBlock *Target) {
    if (Target && Seen.insert(Target).second)
      Targets.push_back(Target);
  };

  for (MachineBasicBlock *Succ : MBB.successors())
    AddTarget(Succ);

  for (MachineInstr &MI : MBB) {
    if (MI.isDebugInstr())
      continue;
    for (MachineOperand &MO : MI.operands())
      if (MO.isMBB())
        AddTarget(MO.getMBB());
  }
}

static inline bool operandIsKill(const MachineInstr &MI, Register Reg,
                          const TargetRegisterInfo &TRI) {
  for (const MachineOperand &MO : MI.operands())
    if (operandTouchesReg(MO, Reg, TRI) && MO.readsReg() && MO.isKill())
      return true;
  return false;
}

static inline bool instrHasRegMaskForReg(const MachineInstr &MI, Register Reg,
                                  const TargetRegisterInfo &TRI);

static inline bool regDeadAfter(MachineBasicBlock::iterator From,
                         MachineBasicBlock &MBB, Register Reg,
                         const TargetRegisterInfo &TRI) {
  for (auto I = From; I != MBB.end(); ++I) {
    if (I->isDebugInstr())
      continue;
    if (instrUsesReg(*I, Reg, TRI))
      return false;
    if (instrDefinesReg(*I, Reg, TRI))
      return true;
  }
  return MBB.succ_empty();
}

static inline bool regDeadOrClobberedAfter(MachineBasicBlock::iterator From,
                                    MachineBasicBlock &MBB, Register Reg,
                                    const TargetRegisterInfo &TRI) {
  for (auto I = From; I != MBB.end(); ++I) {
    if (I->isDebugInstr())
      continue;
    if (instrUsesReg(*I, Reg, TRI))
      return false;
    if (instrDefinesReg(*I, Reg, TRI) || instrHasRegMaskForReg(*I, Reg, TRI))
      return true;
  }
  return MBB.succ_empty();
}

static inline bool regUnusedBeforeEndOrDef(MachineBasicBlock::iterator From,
                                    MachineBasicBlock &MBB, Register Reg,
                                    const TargetRegisterInfo &TRI) {
  for (auto I = From; I != MBB.end(); ++I) {
    if (I->isDebugInstr())
      continue;
    if (instrUsesReg(*I, Reg, TRI))
      return false;
    if (instrDefinesReg(*I, Reg, TRI))
      return true;
  }
  return true;
}

static inline bool regDefDeadOrDeadAfter(MachineBasicBlock::iterator DefI,
                                  MachineBasicBlock &MBB, Register Reg,
                                  const TargetRegisterInfo &TRI) {
  return instrDefinesDeadReg(*DefI, Reg, TRI) ||
         regDeadAfter(std::next(DefI), MBB, Reg, TRI);
}

static inline bool regDefDeadOrClobberedAfter(MachineBasicBlock::iterator DefI,
                                       MachineBasicBlock &MBB, Register Reg,
                                       const TargetRegisterInfo &TRI) {
  return instrDefinesDeadReg(*DefI, Reg, TRI) ||
         regDeadOrClobberedAfter(std::next(DefI), MBB, Reg, TRI);
}

static inline bool
regDeadFromBlockStartInCFG(MachineBasicBlock &MBB, Register Reg,
                           const TargetRegisterInfo &TRI,
                           SmallPtrSetImpl<MachineBasicBlock *> &Visiting) {
  if (!Visiting.insert(&MBB).second)
    return false;

  for (MachineInstr &MI : MBB) {
    if (MI.isDebugInstr())
      continue;
    if (instrUsesReg(MI, Reg, TRI)) {
      Visiting.erase(&MBB);
      return false;
    }
    if (instrDefinesReg(MI, Reg, TRI)) {
      Visiting.erase(&MBB);
      return true;
    }
  }

  SmallVector<MachineBasicBlock *, 4> Targets;
  collectFlowSuccessors(MBB, Targets);
  bool Dead = Targets.empty();
  for (MachineBasicBlock *Succ : Targets) {
    if (!regDeadFromBlockStartInCFG(*Succ, Reg, TRI, Visiting)) {
      Dead = false;
      break;
    }
    Dead = true;
  }

  Visiting.erase(&MBB);
  return Dead;
}

static inline bool
regUnusedFromBlockStartInCFG(MachineBasicBlock &MBB, Register Reg,
                             const TargetRegisterInfo &TRI,
                             SmallPtrSetImpl<MachineBasicBlock *> &Visited) {
  if (!Visited.insert(&MBB).second)
    return true;

  for (MachineInstr &MI : MBB) {
    if (MI.isDebugInstr())
      continue;
    if (instrUsesReg(MI, Reg, TRI))
      return false;
    if (instrDefinesReg(MI, Reg, TRI))
      return true;
  }

  SmallVector<MachineBasicBlock *, 4> Targets;
  collectFlowSuccessors(MBB, Targets);
  for (MachineBasicBlock *Succ : Targets)
    if (!regUnusedFromBlockStartInCFG(*Succ, Reg, TRI, Visited))
      return false;
  return true;
}

static inline bool regUnusedAfterInCFG(MachineBasicBlock::iterator From,
                                MachineBasicBlock &MBB, Register Reg,
                                const TargetRegisterInfo &TRI) {
  for (auto I = From; I != MBB.end(); ++I) {
    if (I->isDebugInstr())
      continue;
    if (instrUsesReg(*I, Reg, TRI))
      return false;
    if (instrDefinesReg(*I, Reg, TRI))
      return true;
  }

  SmallPtrSet<MachineBasicBlock *, 8> Visited;
  SmallVector<MachineBasicBlock *, 4> Targets;
  collectFlowSuccessors(MBB, Targets);
  for (MachineBasicBlock *Succ : Targets)
    if (!regUnusedFromBlockStartInCFG(*Succ, Reg, TRI, Visited))
      return false;
  return true;
}

static inline bool regDeadAfterInCFG(MachineBasicBlock::iterator From,
                              MachineBasicBlock &MBB, Register Reg,
                              const TargetRegisterInfo &TRI) {
  for (auto I = From; I != MBB.end(); ++I) {
    if (I->isDebugInstr())
      continue;
    if (instrUsesReg(*I, Reg, TRI))
      return false;
    if (instrDefinesReg(*I, Reg, TRI))
      return true;
  }

  SmallVector<MachineBasicBlock *, 4> Targets;
  collectFlowSuccessors(MBB, Targets);
  if (Targets.empty())
    return true;

  SmallPtrSet<MachineBasicBlock *, 8> Visiting;
  for (MachineBasicBlock *Succ : Targets)
    if (!regDeadFromBlockStartInCFG(*Succ, Reg, TRI, Visiting))
      return false;
  return true;
}

static inline bool regDefDeadOrDeadAfterInCFG(MachineBasicBlock::iterator DefI,
                                       MachineBasicBlock &MBB, Register Reg,
                                       const TargetRegisterInfo &TRI) {
  return instrDefinesDeadReg(*DefI, Reg, TRI) ||
         regDeadAfterInCFG(std::next(DefI), MBB, Reg, TRI);
}

static inline bool
regReachesRetBeforeTouch(MachineBasicBlock::iterator From,
                         MachineBasicBlock &MBB, Register Reg,
                         const TargetRegisterInfo &TRI,
                         SmallPtrSetImpl<MachineBasicBlock *> &Visiting) {
  for (auto I = From; I != MBB.end(); ++I) {
    if (I->isDebugInstr())
      continue;
    if (I->getOpcode() == Bedrock::RET)
      return true;
    if (instrTouchesReg(*I, Reg, TRI))
      return false;
  }

  if (!Visiting.insert(&MBB).second)
    return false;
  bool ReachesRet = false;
  for (MachineBasicBlock *Succ : MBB.successors()) {
    if (regReachesRetBeforeTouch(Succ->begin(), *Succ, Reg, TRI, Visiting)) {
      ReachesRet = true;
      break;
    }
  }
  Visiting.erase(&MBB);
  return ReachesRet;
}

static inline bool regReachesRetBeforeTouch(MachineBasicBlock::iterator From,
                                     MachineBasicBlock &MBB, Register Reg,
                                     const TargetRegisterInfo &TRI) {
  SmallPtrSet<MachineBasicBlock *, 8> Visiting;
  return regReachesRetBeforeTouch(From, MBB, Reg, TRI, Visiting);
}

static inline bool functionUsesReg(const MachineFunction &MF, Register Reg,
                            const MachineInstr *Ignore,
                            const TargetRegisterInfo &TRI) {
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB)
      if (&MI != Ignore && !MI.isDebugInstr() && instrUsesReg(MI, Reg, TRI))
        return true;
  return false;
}

static inline bool isAddImmToReg(const MachineInstr &MI, Register Reg, int64_t Amount,
                          const TargetRegisterInfo &TRI) {
  if (MI.getOpcode() != Bedrock::ADD64ri || MI.getNumOperands() < 3 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
      !MI.getOperand(2).isImm())
    return false;
  return regsOverlap(TRI, MI.getOperand(0).getReg(), Reg) &&
         regsOverlap(TRI, MI.getOperand(1).getReg(), Reg) &&
         MI.getOperand(2).getImm() == Amount;
}

static inline MachineBasicBlock::iterator
findBaseAddBeforeBaseTouch(MachineBasicBlock &MBB, Register Base,
                           int64_t Amount, const TargetRegisterInfo &TRI) {
  for (MachineBasicBlock::iterator Scan = MBB.begin(); Scan != MBB.end();
       Scan = nextNonDebug(Scan, MBB)) {
    if (Scan->isDebugInstr())
      continue;
    if (isAddImmToReg(*Scan, Base, Amount, TRI))
      return Scan;
    if (Scan->isBranch() || Scan->isCall() || Scan->isReturn() ||
        Scan->isTerminator() || instrTouchesReg(*Scan, Base, TRI))
      break;
  }
  return MBB.end();
}

static inline MachineInstr *findPostInc(MachineBasicBlock::iterator From,
                                 MachineBasicBlock &MBB, Register Base,
                                 int64_t Size, const TargetRegisterInfo &TRI) {
  if (!isAReg(Base) || Size <= 0)
    return nullptr;
  for (auto I = std::next(From); I != MBB.end(); ++I) {
    if (I->isDebugInstr())
      continue;
    if (isAddImmToReg(*I, Base, Size, TRI))
      return &*I;
    if (instrTouchesReg(*I, Base, TRI))
      return nullptr;
  }
  return nullptr;
}

static inline unsigned memSizeForOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::MOV8rm:
  case Bedrock::MOV8idx1rm:
  case Bedrock::MOV8idx4rm:
  case Bedrock::MOV8idx4lrm:
  case Bedrock::MOV8mr:
  case Bedrock::MOV8idx1mr:
  case Bedrock::MOV8idx4mr:
  case Bedrock::MOV8idx4lmr:
  case Bedrock::MOV8mm:
  case Bedrock::MOV8mmpostboth:
  case Bedrock::MADD8mrr:
  case Bedrock::MADD8postmrr:
  case Bedrock::ADD8rm:
  case Bedrock::ADD8mr:
  case Bedrock::ADD8mi:
  case Bedrock::SUB8rm:
  case Bedrock::SUB8mr:
  case Bedrock::SUB8mi:
  case Bedrock::AND8rm:
  case Bedrock::AND8mr:
  case Bedrock::AND8mi:
  case Bedrock::OR8rm:
  case Bedrock::OR8mr:
  case Bedrock::OR8mi:
  case Bedrock::XOR8rm:
  case Bedrock::XOR8mr:
  case Bedrock::XOR8mi:
  case Bedrock::INC8m:
  case Bedrock::DEC8m:
  case Bedrock::CMP8rm:
  case Bedrock::CMP8idx1rm:
  case Bedrock::CMP8idx4rm:
  case Bedrock::CMP8idx4lrm:
  case Bedrock::CMP8mr:
  case Bedrock::CMP8idx1mr:
  case Bedrock::CMP8idx4mr:
  case Bedrock::CMP8idx4lmr:
  case Bedrock::CMP8mi:
  case Bedrock::CMP8idx1mi:
  case Bedrock::CMP8idx4mi:
  case Bedrock::CMP8idx4lmi:
  case Bedrock::TEST8rm:
  case Bedrock::TEST8idx1rm:
  case Bedrock::TEST8idx4rm:
  case Bedrock::TEST8idx4lrm:
  case Bedrock::TEST8mr:
  case Bedrock::TEST8idx1mr:
  case Bedrock::TEST8idx4mr:
  case Bedrock::TEST8idx4lmr:
  case Bedrock::TEST8mi:
  case Bedrock::TEST8idx1mi:
  case Bedrock::TEST8idx4mi:
  case Bedrock::TEST8idx4lmi:
    return 1;
  case Bedrock::MOV16rm:
  case Bedrock::MOV16idx1rm:
  case Bedrock::MOV16idx4rm:
  case Bedrock::MOV16idx4lrm:
  case Bedrock::MOV16mr:
  case Bedrock::MOV16idx1mr:
  case Bedrock::MOV16idx4mr:
  case Bedrock::MOV16idx4lmr:
  case Bedrock::MOV16mm:
  case Bedrock::MOV16mmpostboth:
  case Bedrock::MADD16mrr:
  case Bedrock::MADD16postmrr:
  case Bedrock::ADD16rm:
  case Bedrock::ADD16mr:
  case Bedrock::ADD16mi:
  case Bedrock::SUB16rm:
  case Bedrock::SUB16mr:
  case Bedrock::SUB16mi:
  case Bedrock::AND16rm:
  case Bedrock::AND16mr:
  case Bedrock::AND16mi:
  case Bedrock::OR16rm:
  case Bedrock::OR16mr:
  case Bedrock::OR16mi:
  case Bedrock::XOR16rm:
  case Bedrock::XOR16mr:
  case Bedrock::XOR16mi:
  case Bedrock::INC16m:
  case Bedrock::DEC16m:
  case Bedrock::CMP16rm:
  case Bedrock::CMP16idx1rm:
  case Bedrock::CMP16idx4rm:
  case Bedrock::CMP16idx4lrm:
  case Bedrock::CMP16mr:
  case Bedrock::CMP16idx1mr:
  case Bedrock::CMP16idx4mr:
  case Bedrock::CMP16idx4lmr:
  case Bedrock::CMP16mi:
  case Bedrock::CMP16idx1mi:
  case Bedrock::CMP16idx4mi:
  case Bedrock::CMP16idx4lmi:
  case Bedrock::TEST16rm:
  case Bedrock::TEST16idx1rm:
  case Bedrock::TEST16idx4rm:
  case Bedrock::TEST16idx4lrm:
  case Bedrock::TEST16mr:
  case Bedrock::TEST16idx1mr:
  case Bedrock::TEST16idx4mr:
  case Bedrock::TEST16idx4lmr:
  case Bedrock::TEST16mi:
  case Bedrock::TEST16idx1mi:
  case Bedrock::TEST16idx4mi:
  case Bedrock::TEST16idx4lmi:
    return 2;
  case Bedrock::MOV32rm:
  case Bedrock::MOV32idx1rm:
  case Bedrock::MOV32idx4rm:
  case Bedrock::MOV32idx4lrm:
  case Bedrock::MOV32mr:
  case Bedrock::MOV32idx1mr:
  case Bedrock::MOV32idx4mr:
  case Bedrock::MOV32idx4lmr:
  case Bedrock::ADD32idx1rm:
  case Bedrock::ADD32idx4rm:
  case Bedrock::ADD32idx4lrm:
  case Bedrock::SUB32idx1rm:
  case Bedrock::SUB32idx4rm:
  case Bedrock::SUB32idx4lrm:
  case Bedrock::AND32idx1rm:
  case Bedrock::AND32idx4rm:
  case Bedrock::AND32idx4lrm:
  case Bedrock::OR32idx1rm:
  case Bedrock::OR32idx4rm:
  case Bedrock::OR32idx4lrm:
  case Bedrock::XOR32idx1rm:
  case Bedrock::XOR32idx4rm:
  case Bedrock::XOR32idx4lrm:
  case Bedrock::MULU32idx1rm:
  case Bedrock::MULU32idx4rm:
  case Bedrock::MULU32idx4lrm:
  case Bedrock::INC32idx1m:
  case Bedrock::INC32idx4m:
  case Bedrock::INC32idx4lm:
  case Bedrock::DEC32idx1m:
  case Bedrock::DEC32idx4m:
  case Bedrock::DEC32idx4lm:
  case Bedrock::MOV32mm:
  case Bedrock::MOV32mmpostboth:
  case Bedrock::FMOV32rm:
  case Bedrock::FMOV32mr:
  case Bedrock::MADD32mrr:
  case Bedrock::MADD32postmrr:
  case Bedrock::ADD32rm:
  case Bedrock::ADD32mr:
  case Bedrock::ADD32mi:
  case Bedrock::SUB32rm:
  case Bedrock::SUB32mr:
  case Bedrock::SUB32mi:
  case Bedrock::AND32rm:
  case Bedrock::AND32mr:
  case Bedrock::AND32mi:
  case Bedrock::OR32rm:
  case Bedrock::OR32mr:
  case Bedrock::OR32mi:
  case Bedrock::XOR32rm:
  case Bedrock::XOR32mr:
  case Bedrock::XOR32mi:
  case Bedrock::INC32m:
  case Bedrock::DEC32m:
  case Bedrock::FADD32rm:
  case Bedrock::FSUB32rm:
  case Bedrock::FMUL32rm:
  case Bedrock::FDIV32rm:
  case Bedrock::FMADD32rmr:
  case Bedrock::FMADD32mrr:
  case Bedrock::FMSUB32rmr:
  case Bedrock::FMSUB32mrr:
  case Bedrock::FNMADD32rmr:
  case Bedrock::FNMADD32mrr:
  case Bedrock::FNMSUB32rmr:
  case Bedrock::FNMSUB32mrr:
  case Bedrock::FABS32rm:
  case Bedrock::FABS32mr:
  case Bedrock::FNEG32rm:
  case Bedrock::FNEG32mr:
  case Bedrock::FSQRT32rm:
  case Bedrock::FSQRT32mr:
  case Bedrock::FCMP32rm:
  case Bedrock::CMP32rm:
  case Bedrock::CMP32idx1rm:
  case Bedrock::CMP32idx4rm:
  case Bedrock::CMP32idx4lrm:
  case Bedrock::CMP32mr:
  case Bedrock::CMP32idx1mr:
  case Bedrock::CMP32idx4mr:
  case Bedrock::CMP32idx4lmr:
  case Bedrock::CMP32mi:
  case Bedrock::CMP32idx1mi:
  case Bedrock::CMP32idx4mi:
  case Bedrock::CMP32idx4lmi:
  case Bedrock::TEST32rm:
  case Bedrock::TEST32idx1rm:
  case Bedrock::TEST32idx4rm:
  case Bedrock::TEST32idx4lrm:
  case Bedrock::TEST32mr:
  case Bedrock::TEST32idx1mr:
  case Bedrock::TEST32idx4mr:
  case Bedrock::TEST32idx4lmr:
  case Bedrock::TEST32mi:
  case Bedrock::TEST32idx1mi:
  case Bedrock::TEST32idx4mi:
  case Bedrock::TEST32idx4lmi:
    return 4;
  case Bedrock::MOV64rm:
  case Bedrock::MOV64idx1rm:
  case Bedrock::MOV64idx4rm:
  case Bedrock::MOV64idx4lrm:
  case Bedrock::MOV64mr:
  case Bedrock::MOV64idx1mr:
  case Bedrock::MOV64idx4mr:
  case Bedrock::MOV64idx4lmr:
  case Bedrock::MOV64mm:
  case Bedrock::MOV64mmpostboth:
  case Bedrock::FMOV64rm:
  case Bedrock::FMOV64mr:
  case Bedrock::MADD64mrr:
  case Bedrock::MADD64postmrr:
  case Bedrock::ADD64rm:
  case Bedrock::ADD64mr:
  case Bedrock::ADD64mi:
  case Bedrock::SUB64rm:
  case Bedrock::SUB64mr:
  case Bedrock::SUB64mi:
  case Bedrock::AND64rm:
  case Bedrock::AND64mr:
  case Bedrock::AND64mi:
  case Bedrock::OR64rm:
  case Bedrock::OR64mr:
  case Bedrock::OR64mi:
  case Bedrock::XOR64rm:
  case Bedrock::XOR64mr:
  case Bedrock::XOR64mi:
  case Bedrock::INC64m:
  case Bedrock::DEC64m:
  case Bedrock::CLRm:
  case Bedrock::FADD64rm:
  case Bedrock::FSUB64rm:
  case Bedrock::FMUL64rm:
  case Bedrock::FDIV64rm:
  case Bedrock::FMADD64rmr:
  case Bedrock::FMADD64mrr:
  case Bedrock::FMSUB64rmr:
  case Bedrock::FMSUB64mrr:
  case Bedrock::FNMADD64rmr:
  case Bedrock::FNMADD64mrr:
  case Bedrock::FNMSUB64rmr:
  case Bedrock::FNMSUB64mrr:
  case Bedrock::FABS64rm:
  case Bedrock::FABS64mr:
  case Bedrock::FNEG64rm:
  case Bedrock::FNEG64mr:
  case Bedrock::FSQRT64rm:
  case Bedrock::FSQRT64mr:
  case Bedrock::FCMP64rm:
  case Bedrock::CMP64rm:
  case Bedrock::CMP64idx1rm:
  case Bedrock::CMP64idx4rm:
  case Bedrock::CMP64idx4lrm:
  case Bedrock::CMP64mr:
  case Bedrock::CMP64idx1mr:
  case Bedrock::CMP64idx4mr:
  case Bedrock::CMP64idx4lmr:
  case Bedrock::CMP64mi:
  case Bedrock::CMP64idx1mi:
  case Bedrock::CMP64idx4mi:
  case Bedrock::CMP64idx4lmi:
  case Bedrock::TEST64rm:
  case Bedrock::TEST64idx1rm:
  case Bedrock::TEST64idx4rm:
  case Bedrock::TEST64idx4lrm:
  case Bedrock::TEST64mr:
  case Bedrock::TEST64idx1mr:
  case Bedrock::TEST64idx4mr:
  case Bedrock::TEST64idx4lmr:
  case Bedrock::TEST64mi:
  case Bedrock::TEST64idx1mi:
  case Bedrock::TEST64idx4mi:
  case Bedrock::TEST64idx4lmi:
  case Bedrock::FCLRm:
    return 8;
  }
}

static inline unsigned getPostLoadOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::MOV8rm:
    return Bedrock::MOV8postrm;
  case Bedrock::MOV16rm:
    return Bedrock::MOV16postrm;
  case Bedrock::MOV32rm:
    return Bedrock::MOV32postrm;
  case Bedrock::MOV64rm:
    return Bedrock::MOV64postrm;
  }
}

static inline bool postLoadSupportsAReg(unsigned Opcode) {
  return Opcode == Bedrock::MOV32rm || Opcode == Bedrock::MOV64rm;
}

static inline unsigned getPostStoreOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::MOV8mr:
    return Bedrock::MOV8postmr;
  case Bedrock::MOV16mr:
    return Bedrock::MOV16postmr;
  case Bedrock::MOV32mr:
    return Bedrock::MOV32postmr;
  case Bedrock::MOV64mr:
    return Bedrock::MOV64postmr;
  }
}

static inline unsigned getStoreOpcodeForLoad(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::MOV8rm:
    return Bedrock::MOV8mr;
  case Bedrock::MOV16rm:
    return Bedrock::MOV16mr;
  case Bedrock::MOV32rm:
    return Bedrock::MOV32mr;
  case Bedrock::MOV64rm:
    return Bedrock::MOV64mr;
  }
}

static inline unsigned getRegMoveOpcodeForLoad(unsigned Opcode, Register Dst,
                                        Register Src) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::MOV8rm:
    return isDReg(Dst) && isDReg(Src) ? Bedrock::MOV8rr : 0;
  case Bedrock::MOV16rm:
    return isDReg(Dst) && isDReg(Src) ? Bedrock::MOV16rr : 0;
  case Bedrock::MOV32rm:
    return isIntReg(Dst) && isIntReg(Src) ? Bedrock::MOV32rr : 0;
  case Bedrock::MOV64rm:
    return isIntReg(Dst) && isIntReg(Src) ? Bedrock::MOV64rr : 0;
  }
}

static inline unsigned getPlainLoadOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return Opcode;
  case Bedrock::MOV8postrm:
    return Bedrock::MOV8rm;
  case Bedrock::MOV16postrm:
    return Bedrock::MOV16rm;
  case Bedrock::MOV32postrm:
    return Bedrock::MOV32rm;
  case Bedrock::MOV64postrm:
    return Bedrock::MOV64rm;
  }
}

static inline unsigned getRegMoveOpcodeForMaybePostLoad(unsigned Opcode, Register Dst,
                                                 Register Src) {
  return getRegMoveOpcodeForLoad(getPlainLoadOpcode(Opcode), Dst, Src);
}

static inline bool isStackAdjust(const MachineInstr &MI, unsigned Opcode,
                          int64_t &Amount) {
  if (MI.getOpcode() != Opcode || MI.getNumOperands() < 3 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
      !MI.getOperand(2).isImm())
    return false;
  if (MI.getOperand(0).getReg() != Bedrock::SP ||
      MI.getOperand(1).getReg() != Bedrock::SP)
    return false;
  Amount = MI.getOperand(2).getImm();
  return Amount >= 0;
}

static inline bool isFrameMetaInstruction(const MachineInstr &MI) {
  return MI.isDebugInstr() || MI.getOpcode() == TargetOpcode::CFI_INSTRUCTION;
}

static inline bool isCalleeSaveStore(const MachineInstr &MI, Register &Reg,
                              int64_t &Offset) {
  if (MI.getOpcode() != Bedrock::MOV64mr || MI.getNumOperands() < 3 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
      !MI.getOperand(2).isImm())
    return false;
  if (MI.getOperand(1).getReg() != Bedrock::SP)
    return false;
  Reg = MI.getOperand(0).getReg();
  Offset = MI.getOperand(2).getImm();
  return getMaskBit(Reg).has_value();
}

static inline bool isCalleeSaveLoad(const MachineInstr &MI, Register &Reg,
                             int64_t &Offset) {
  if (MI.getOpcode() != Bedrock::MOV64rm || MI.getNumOperands() < 3 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
      !MI.getOperand(2).isImm())
    return false;
  if (MI.getOperand(1).getReg() != Bedrock::SP)
    return false;
  Reg = MI.getOperand(0).getReg();
  Offset = MI.getOperand(2).getImm();
  return getMaskBit(Reg).has_value();
}

static inline bool isCalleeSaveSlotAccess(const MachineFunction &MF,
                                          Register Reg, int64_t Offset) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  for (const CalleeSavedInfo &CSI : MFI.getCalleeSavedInfo()) {
    if (CSI.getReg().id() != Reg.id())
      continue;
    int64_t SlotOffset =
        MFI.getObjectOffset(CSI.getFrameIdx()) + MFI.getStackSize();
    if (Offset == SlotOffset)
      return true;
  }
  return false;
}

static inline bool isCalleeSaveSpill(const MachineFunction &MF,
                                     const MachineInstr &MI) {
  Register Reg;
  int64_t Offset = 0;
  return isCalleeSaveStore(MI, Reg, Offset) &&
         isCalleeSaveSlotAccess(MF, Reg, Offset);
}

static inline bool isCalleeSaveRestore(const MachineFunction &MF,
                                       const MachineInstr &MI) {
  Register Reg;
  int64_t Offset = 0;
  return isCalleeSaveLoad(MI, Reg, Offset) &&
         isCalleeSaveSlotAccess(MF, Reg, Offset);
}

static inline bool isCalleeSaveFStore(const MachineInstr &MI, Register &Reg,
                                      int64_t &Offset) {
  if (MI.getOpcode() != Bedrock::FMOV64mr || MI.getNumOperands() < 3 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
      !MI.getOperand(2).isImm())
    return false;
  if (MI.getOperand(1).getReg() != Bedrock::SP)
    return false;
  Reg = MI.getOperand(0).getReg();
  Offset = MI.getOperand(2).getImm();
  return getFMaskBit(Reg).has_value();
}

static inline bool isCalleeSaveFLoad(const MachineInstr &MI, Register &Reg,
                                     int64_t &Offset) {
  if (MI.getOpcode() != Bedrock::FMOV64rm || MI.getNumOperands() < 3 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
      !MI.getOperand(2).isImm())
    return false;
  if (MI.getOperand(1).getReg() != Bedrock::SP)
    return false;
  Reg = MI.getOperand(0).getReg();
  Offset = MI.getOperand(2).getImm();
  return getFMaskBit(Reg).has_value();
}

static inline bool expectedStoreOffset(uint16_t Mask, Register Reg, int64_t Total,
                                int64_t Offset) {
  unsigned Seen = 0;
  for (unsigned Bit = 0; Bit != 16; ++Bit) {
    if ((Mask & (uint16_t(1) << Bit)) == 0)
      continue;
    ++Seen;
    if (getRegForMaskBit(Bit) == Reg)
      return Offset == Total - int64_t(Seen) * 8;
  }
  return false;
}

static inline bool expectedFStoreOffset(uint16_t Mask, Register Reg,
                                        int64_t Total, int64_t Offset) {
  unsigned Seen = 0;
  for (unsigned Bit = 0; Bit != 16; ++Bit) {
    if ((Mask & (uint16_t(1) << Bit)) == 0)
      continue;
    ++Seen;
    if (getFRegForMaskBit(Bit) == Reg)
      return Offset == Total - int64_t(Seen) * 8;
  }
  return false;
}

static inline void addImplicitPushRegs(MachineInstrBuilder MIB, uint16_t Mask) {
  for (unsigned Bit = 0; Bit != 16; ++Bit) {
    if ((Mask & (uint16_t(1) << Bit)) != 0)
      MIB.addReg(getRegForMaskBit(Bit), RegState::Implicit | RegState::Kill);
  }
}

static inline void addImplicitPopRegs(MachineInstrBuilder MIB, uint16_t Mask) {
  for (unsigned Bit = 0; Bit != 16; ++Bit) {
    if ((Mask & (uint16_t(1) << Bit)) != 0) {
      MIB.addReg(getRegForMaskBit(Bit), RegState::Implicit | RegState::Define);
    }
  }
}

static inline unsigned pushOpcodeForReg(Register Reg) {
  if (isDReg(Reg))
    return Bedrock::PUSHD;
  if (isAReg(Reg))
    return Bedrock::PUSHA;
  return 0;
}

static inline unsigned popOpcodeForReg(Register Reg) {
  if (isDReg(Reg))
    return Bedrock::POPD;
  if (isAReg(Reg))
    return Bedrock::POPA;
  return 0;
}

static inline bool isPushOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
  case Bedrock::PUSHD:
  case Bedrock::PUSHA:
  case Bedrock::PUSHM:
    return true;
  }
}

static inline bool isPopOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
  case Bedrock::POPD:
  case Bedrock::POPA:
  case Bedrock::POPM:
    return true;
  }
}

static inline bool isPushPopOpcode(unsigned Opcode) {
  return isPushOpcode(Opcode) || isPopOpcode(Opcode);
}

static inline MachineInstrBuilder
buildPushForMask(MachineBasicBlock &MBB, MachineBasicBlock::iterator Insert,
                 DebugLoc DL, const BedrockInstrInfo &TII, uint16_t Mask) {
  if (std::optional<Register> Reg = singleRegFromMask(Mask)) {
    unsigned Opcode = pushOpcodeForReg(*Reg);
    assert(Opcode != 0 && "single push mask must name a pushable register");
    return BuildMI(MBB, Insert, DL, TII.get(Opcode))
        .addReg(*Reg, RegState::Kill);
  }

  MachineInstrBuilder Push =
      BuildMI(MBB, Insert, DL, TII.get(Bedrock::PUSHM)).addImm(Mask);
  addImplicitPushRegs(Push, Mask);
  return Push;
}

static inline MachineInstrBuilder
buildPopForMask(MachineBasicBlock &MBB, MachineBasicBlock::iterator Insert,
                DebugLoc DL, const BedrockInstrInfo &TII, uint16_t Mask) {
  if (std::optional<Register> Reg = singleRegFromMask(Mask)) {
    unsigned Opcode = popOpcodeForReg(*Reg);
    assert(Opcode != 0 && "single pop mask must name a poppable register");
    return BuildMI(MBB, Insert, DL, TII.get(Opcode), *Reg);
  }

  MachineInstrBuilder Pop =
      BuildMI(MBB, Insert, DL, TII.get(Bedrock::POPM)).addImm(Mask);
  addImplicitPopRegs(Pop, Mask);
  return Pop;
}

static inline void addImplicitFPushRegs(MachineInstrBuilder MIB,
                                        uint16_t Mask) {
  for (unsigned Bit = 0; Bit != 16; ++Bit) {
    if ((Mask & (uint16_t(1) << Bit)) != 0)
      MIB.addReg(getFRegForMaskBit(Bit), RegState::Implicit | RegState::Kill);
  }
}

static inline void addImplicitFPopRegs(MachineInstrBuilder MIB,
                                       uint16_t Mask) {
  for (unsigned Bit = 0; Bit != 16; ++Bit) {
    if ((Mask & (uint16_t(1) << Bit)) != 0)
      MIB.addReg(getFRegForMaskBit(Bit), RegState::Implicit | RegState::Define);
  }
}

static inline MachineInstrBuilder
buildFPushForMask(MachineBasicBlock &MBB, MachineBasicBlock::iterator Insert,
                  DebugLoc DL, const BedrockInstrInfo &TII, uint16_t Mask) {
  MachineInstrBuilder Push =
      BuildMI(MBB, Insert, DL, TII.get(Bedrock::FPUSHM)).addImm(Mask);
  addImplicitFPushRegs(Push, Mask);
  return Push;
}

static inline MachineInstrBuilder
buildFPopForMask(MachineBasicBlock &MBB, MachineBasicBlock::iterator Insert,
                 DebugLoc DL, const BedrockInstrInfo &TII, uint16_t Mask) {
  MachineInstrBuilder Pop =
      BuildMI(MBB, Insert, DL, TII.get(Bedrock::FPOPM)).addImm(Mask);
  addImplicitFPopRegs(Pop, Mask);
  return Pop;
}

static inline void addImplicitSumRegs(MachineInstrBuilder MIB, uint16_t Mask) {
  for (unsigned Bit = 0; Bit != 16; ++Bit) {
    if ((Mask & (uint16_t(1) << Bit)) != 0)
      MIB.addReg(getRegForMaskBit(Bit), RegState::Implicit);
  }
}

static inline bool isMemoryBaseOperand(const MachineInstr &MI, unsigned OpNo,
                                Register Base, const TargetRegisterInfo &TRI);

struct A32DRemap {
  Register Src;
  Register Dst;
};

static inline Register remapReg(Register Reg, ArrayRef<A32DRemap> Remaps) {
  for (const A32DRemap &Remap : Remaps)
    if (Reg == Remap.Src)
      return Remap.Dst;
  return Reg;
}

static inline bool instrHasRegMaskForReg(const MachineInstr &MI, Register Reg,
                                  const TargetRegisterInfo &TRI) {
  for (const MachineOperand &MO : MI.operands())
    if (MO.isRegMask() && MO.clobbersPhysReg(Reg))
      return true;
  return false;
}

static inline bool isPostMemoryBaseOperand(const MachineInstr &MI, unsigned OpNo,
                                    Register Base,
                                    const TargetRegisterInfo &TRI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::MOV32postrm:
  case Bedrock::MOV32postmr:
    break;
  }
  if (OpNo != 1 || MI.getNumOperands() < 2)
    return false;
  const MachineOperand &MO = MI.getOperand(OpNo);
  return MO.isReg() && MO.readsReg() && !MO.isDef() &&
         regsOverlap(TRI, MO.getReg(), Base);
}

static inline bool canRemapA32RegInOpcode(const MachineInstr &MI, Register AReg,
                                   const TargetRegisterInfo &TRI) {
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    const MachineOperand &MO = MI.getOperand(I);
    if (!operandTouchesReg(MO, AReg, TRI))
      continue;
    if (MO.getSubReg() != 0)
      return false;
    if (isMemoryBaseOperand(MI, I, AReg, TRI) ||
        isPostMemoryBaseOperand(MI, I, AReg, TRI))
      return false;
    if (MO.isImplicit() && MI.getOpcode() != Bedrock::SUM32d)
      return false;
  }

  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::CLR64r:
  case Bedrock::MOV32ri:
  case Bedrock::MOV32rm:
  case Bedrock::MOV32postrm:
  case Bedrock::MOV32rr:
  case Bedrock::MOV32mr:
  case Bedrock::MOV32postmr:
  case Bedrock::ADD32rr:
  case Bedrock::ADD32ri:
  case Bedrock::SUB32rr:
  case Bedrock::SUB32ri:
  case Bedrock::INC32r:
  case Bedrock::DEC32r:
  case Bedrock::SUM32d:
    return true;
  case Bedrock::MAXS32rr:
  case Bedrock::MAXU32rr:
  case Bedrock::MINS32rr:
  case Bedrock::MINU32rr:
    return MI.getNumOperands() >= 3 &&
           !operandTouchesReg(MI.getOperand(0), AReg, TRI) &&
           !operandTouchesReg(MI.getOperand(1), AReg, TRI);
  case Bedrock::CMP32rr:
    return MI.getNumOperands() >= 2 &&
           !operandTouchesReg(MI.getOperand(0), AReg, TRI);
  }
}

static inline bool canRemapA32RegToD(const MachineFunction &MF, Register AReg,
                              const TargetRegisterInfo &TRI) {
  if (MF.front().isLiveIn(AReg))
    return false;

  bool Touched = false;
  bool Defined = false;
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      if (instrHasRegMaskForReg(MI, AReg, TRI))
        return false;
      if (!instrTouchesReg(MI, AReg, TRI))
        continue;
      if (!canRemapA32RegInOpcode(MI, AReg, TRI))
        return false;
      Touched = true;
      if (instrDefinesReg(MI, AReg, TRI))
        Defined = true;
    }
  }
  return Touched && Defined;
}

static inline bool physRegUsedInFunction(const MachineFunction &MF, Register Reg,
                                  const TargetRegisterInfo &TRI) {
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineBasicBlock::RegisterMaskPair &LiveIn : MBB.liveins())
      if (regsOverlap(TRI, Register(LiveIn.PhysReg), Reg))
        return true;
    for (const MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      if ((MI.getOpcode() == Bedrock::PUSHM ||
           MI.getOpcode() == Bedrock::POPM) &&
          MI.getNumOperands() >= 1 && MI.getOperand(0).isImm()) {
        if (std::optional<unsigned> Bit = getMaskBit(Reg)) {
          uint16_t Mask = static_cast<uint16_t>(MI.getOperand(0).getImm());
          if ((Mask & (uint16_t(1) << *Bit)) != 0)
            return true;
        }
      }
      for (const MachineOperand &MO : MI.operands())
        if (operandTouchesReg(MO, Reg, TRI))
          return true;
    }
  }
  return false;
}

static inline std::optional<uint16_t> pushPopMaskForInstr(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return std::nullopt;
  case Bedrock::PUSHM:
  case Bedrock::POPM:
    if (MI.getNumOperands() < 1 || !MI.getOperand(0).isImm())
      return std::nullopt;
    return static_cast<uint16_t>(MI.getOperand(0).getImm());
  case Bedrock::PUSHD:
  case Bedrock::PUSHA:
    if (MI.getNumOperands() < 1 || !MI.getOperand(0).isReg())
      return std::nullopt;
    if (std::optional<unsigned> Bit = getMaskBit(MI.getOperand(0).getReg()))
      return uint16_t(1) << *Bit;
    return std::nullopt;
  case Bedrock::POPD:
  case Bedrock::POPA:
    if (MI.getNumOperands() < 1 || !MI.getOperand(0).isReg())
      return std::nullopt;
    if (std::optional<unsigned> Bit = getMaskBit(MI.getOperand(0).getReg()))
      return uint16_t(1) << *Bit;
    return std::nullopt;
  }
}

static inline std::optional<Register>
getSavedDRegFromPushPopInstr(const MachineInstr &MI) {
  std::optional<uint16_t> Mask = pushPopMaskForInstr(MI);
  if (!Mask)
    return std::nullopt;
  return getSavedDReg(*Mask);
}

static inline unsigned estimateMov32RegSize(Register Dst, Register Src) {
  return isAReg(Dst) && isAReg(Src) ? 4 : 2;
}

static inline unsigned estimateMov32ImmSize(Register Reg) {
  return isDReg(Reg) ? 4 : 6;
}

static inline unsigned estimateMov32MemSize(Register Reg, bool HasExtraAddrWord) {
  unsigned Size = isDReg(Reg) ? 2 : 4;
  return HasExtraAddrWord ? Size + 2 : Size;
}

static inline unsigned estimateA32BinRegSize(Register Dst, Register RHS) {
  return isDReg(Dst) && isDReg(RHS) ? 2 : 4;
}

static inline unsigned estimateA32IncDecSize(Register Reg) {
  return isDReg(Reg) ? 2 : 4;
}

static inline unsigned estimateA32ImmBinSize() { return 6; }

static inline unsigned estimateA32MinMaxSize() { return 4; }

static inline unsigned estimateA32CmpRhsSize(Register RHS) {
  return isDReg(RHS) ? 2 : 4;
}

static inline bool hasExtraAddrWord(const MachineInstr &MI) {
  return MI.getNumOperands() < 3 || !MI.getOperand(2).isImm() ||
         MI.getOperand(2).getImm() != 0;
}

static inline unsigned estimateMinSizeA32InstSize(const MachineInstr &MI,
                                           ArrayRef<A32DRemap> Remaps) {
  auto RegOp = [&](unsigned OpNo) {
    return remapReg(MI.getOperand(OpNo).getReg(), Remaps);
  };

  switch (MI.getOpcode()) {
  default:
    return 0;
  case Bedrock::CLR64r:
    return 2;
  case Bedrock::MOV32ri:
    if (MI.getNumOperands() < 1 || !MI.getOperand(0).isReg())
      return 0;
    return estimateMov32ImmSize(RegOp(0));
  case Bedrock::MOV32rm:
    if (MI.getNumOperands() < 1 || !MI.getOperand(0).isReg())
      return 0;
    return estimateMov32MemSize(RegOp(0), hasExtraAddrWord(MI));
  case Bedrock::MOV32postrm:
    if (MI.getNumOperands() < 1 || !MI.getOperand(0).isReg())
      return 0;
    return estimateMov32MemSize(RegOp(0), true);
  case Bedrock::MOV32mr:
    if (MI.getNumOperands() < 1 || !MI.getOperand(0).isReg())
      return 0;
    return estimateMov32MemSize(RegOp(0), hasExtraAddrWord(MI));
  case Bedrock::MOV32postmr:
    if (MI.getNumOperands() < 1 || !MI.getOperand(0).isReg())
      return 0;
    return estimateMov32MemSize(RegOp(0), true);
  case Bedrock::MOV32rr:
    if (MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
        !MI.getOperand(1).isReg())
      return 0;
    return estimateMov32RegSize(RegOp(0), RegOp(1));
  case Bedrock::ADD32rr:
  case Bedrock::SUB32rr:
    if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
        !MI.getOperand(2).isReg())
      return 0;
    return estimateA32BinRegSize(RegOp(0), RegOp(2));
  case Bedrock::ADD32ri:
  case Bedrock::SUB32ri:
    return estimateA32ImmBinSize();
  case Bedrock::INC32r:
  case Bedrock::DEC32r:
    if (MI.getNumOperands() < 1 || !MI.getOperand(0).isReg())
      return 0;
    return estimateA32IncDecSize(RegOp(0));
  case Bedrock::MAXS32rr:
  case Bedrock::MAXU32rr:
  case Bedrock::MINS32rr:
  case Bedrock::MINU32rr:
    if (MI.getNumOperands() < 3 || !MI.getOperand(2).isReg())
      return 0;
    return estimateA32MinMaxSize();
  case Bedrock::CMP32rr:
    if (MI.getNumOperands() < 2 || !MI.getOperand(1).isReg())
      return 0;
    return estimateA32CmpRhsSize(RegOp(1));
  case Bedrock::SUM32d:
    return 8;
  }
}

static inline unsigned estimateMinSizeA32FunctionSize(const MachineFunction &MF,
                                               ArrayRef<A32DRemap> Remaps,
                                               unsigned ExtraSaveRestoreBytes) {
  unsigned Size = ExtraSaveRestoreBytes;
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB)
      if (!MI.isDebugInstr())
        Size += estimateMinSizeA32InstSize(MI, Remaps);
  return Size;
}

static inline void remapA32Operands(MachineFunction &MF, ArrayRef<A32DRemap> Remaps) {
  for (MachineBasicBlock &MBB : MF) {
    for (const A32DRemap &Remap : Remaps) {
      if (MBB.isLiveIn(Remap.Src)) {
        MBB.removeLiveIn(Remap.Src);
        if (!MBB.isLiveIn(Remap.Dst))
          MBB.addLiveIn(Remap.Dst);
      }
    }

    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      if (MI.getOpcode() == Bedrock::SUM32d && MI.getNumOperands() >= 1 &&
          MI.getOperand(0).isReg() && MI.getNumOperands() >= 2 &&
          MI.getOperand(1).isImm()) {
        uint16_t Mask = static_cast<uint16_t>(MI.getOperand(1).getImm());
        for (const A32DRemap &Remap : Remaps) {
          std::optional<unsigned> SrcBit = getMaskBit(Remap.Src);
          std::optional<unsigned> DstBit = getMaskBit(Remap.Dst);
          if (!SrcBit || !DstBit)
            continue;
          if ((Mask & (uint16_t(1) << *SrcBit)) != 0) {
            Mask &= ~(uint16_t(1) << *SrcBit);
            Mask |= uint16_t(1) << *DstBit;
          }
        }
        MI.getOperand(1).setImm(Mask);
      }

      for (MachineOperand &MO : MI.operands()) {
        if (!MO.isReg())
          continue;
        Register NewReg = remapReg(MO.getReg(), Remaps);
        if (NewReg != MO.getReg())
          MO.setReg(NewReg);
      }
    }
  }
}

static inline unsigned getSumOpcode(unsigned AddOpcode, Register Dst) {
  switch (AddOpcode) {
  default:
    return 0;
  case Bedrock::ADD8rr:
    return isDReg(Dst) ? Bedrock::SUM8d : 0;
  case Bedrock::ADD16rr:
    return isDReg(Dst) ? Bedrock::SUM16d : 0;
  case Bedrock::ADD32rr:
    return isDReg(Dst) ? Bedrock::SUM32d : 0;
  case Bedrock::ADD64rr:
    return isDReg(Dst) ? Bedrock::SUM64d : Bedrock::SUM64a;
  }
}

static inline unsigned getMemBitOpcode(unsigned RegBitOpcode) {
  switch (RegBitOpcode) {
  default:
    return 0;
  case Bedrock::BSET8ri:
    return Bedrock::BSET8mi;
  case Bedrock::BSET16ri:
    return Bedrock::BSET16mi;
  case Bedrock::BSET32ri:
    return Bedrock::BSET32mi;
  case Bedrock::BSET64ri:
    return Bedrock::BSET64mi;
  case Bedrock::BCLR8ri:
    return Bedrock::BCLR8mi;
  case Bedrock::BCLR16ri:
    return Bedrock::BCLR16mi;
  case Bedrock::BCLR32ri:
    return Bedrock::BCLR32mi;
  case Bedrock::BCLR64ri:
    return Bedrock::BCLR64mi;
  case Bedrock::BCHG8ri:
    return Bedrock::BCHG8mi;
  case Bedrock::BCHG16ri:
    return Bedrock::BCHG16mi;
  case Bedrock::BCHG32ri:
    return Bedrock::BCHG32mi;
  case Bedrock::BCHG64ri:
    return Bedrock::BCHG64mi;
  }
}

static inline unsigned getMovMMOpcode(unsigned LoadOpcode, bool SrcPost,
                               bool DstPost) {
  switch (LoadOpcode) {
  default:
    return 0;
  case Bedrock::MOV8rm:
    return SrcPost == DstPost
               ? (SrcPost ? Bedrock::MOV8mmpostboth : Bedrock::MOV8mm)
               : 0;
  case Bedrock::MOV16rm:
    return SrcPost == DstPost
               ? (SrcPost ? Bedrock::MOV16mmpostboth : Bedrock::MOV16mm)
               : 0;
  case Bedrock::MOV32rm:
    return SrcPost == DstPost
               ? (SrcPost ? Bedrock::MOV32mmpostboth : Bedrock::MOV32mm)
               : 0;
  case Bedrock::MOV64rm:
    return SrcPost == DstPost
               ? (SrcPost ? Bedrock::MOV64mmpostboth : Bedrock::MOV64mm)
               : 0;
  }
}

static inline unsigned getMovMIOpcode(unsigned RegImmOpcode) {
  switch (RegImmOpcode) {
  default:
    return 0;
  case Bedrock::MOV8ri:
    return Bedrock::MOV8mi;
  case Bedrock::MOV16ri:
    return Bedrock::MOV16mi;
  case Bedrock::MOV32ri:
    return Bedrock::MOV32mi;
  case Bedrock::MOV64ri:
    return Bedrock::MOV64mi;
  }
}

static inline unsigned getMovMROpcodeForImmOpcode(unsigned RegImmOpcode) {
  switch (RegImmOpcode) {
  default:
    return 0;
  case Bedrock::MOV8ri:
    return Bedrock::MOV8mr;
  case Bedrock::MOV16ri:
    return Bedrock::MOV16mr;
  case Bedrock::MOV32ri:
    return Bedrock::MOV32mr;
  case Bedrock::MOV64ri:
    return Bedrock::MOV64mr;
  }
}

static inline unsigned getRepMovMMOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::MOV8mmpostboth:
    return Bedrock::REPMOV8mmpostboth64;
  case Bedrock::MOV32mmpostboth:
    return Bedrock::REPMOV32mmpostboth;
  case Bedrock::MOV64mmpostboth:
    return Bedrock::REPMOV64mmpostboth;
  }
}

static inline unsigned getRepMovPostMROpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::MOV8postmr:
    return Bedrock::REPMOV8postmr64;
  case Bedrock::MOV32postmr:
    return Bedrock::REPMOV32postmr;
  }
}

static inline unsigned getMemSourceOpcode(unsigned RegOpcode, bool PostInc) {
  switch (RegOpcode) {
  default:
    return 0;
  case Bedrock::ADD8rr:
    return PostInc ? Bedrock::ADD8postrm : Bedrock::ADD8rm;
  case Bedrock::ADD16rr:
    return PostInc ? Bedrock::ADD16postrm : Bedrock::ADD16rm;
  case Bedrock::ADD32rr:
    return PostInc ? Bedrock::ADD32postrm : Bedrock::ADD32rm;
  case Bedrock::ADD64rr:
    return PostInc ? Bedrock::ADD64postrm : Bedrock::ADD64rm;
  case Bedrock::SUB8rr:
    return PostInc ? Bedrock::SUB8postrm : Bedrock::SUB8rm;
  case Bedrock::SUB16rr:
    return PostInc ? Bedrock::SUB16postrm : Bedrock::SUB16rm;
  case Bedrock::SUB32rr:
    return PostInc ? Bedrock::SUB32postrm : Bedrock::SUB32rm;
  case Bedrock::SUB64rr:
    return PostInc ? Bedrock::SUB64postrm : Bedrock::SUB64rm;
  case Bedrock::AND8rr:
    return PostInc ? Bedrock::AND8postrm : Bedrock::AND8rm;
  case Bedrock::AND16rr:
    return PostInc ? Bedrock::AND16postrm : Bedrock::AND16rm;
  case Bedrock::AND32rr:
    return PostInc ? Bedrock::AND32postrm : Bedrock::AND32rm;
  case Bedrock::AND64rr:
    return PostInc ? Bedrock::AND64postrm : Bedrock::AND64rm;
  case Bedrock::OR8rr:
    return PostInc ? Bedrock::OR8postrm : Bedrock::OR8rm;
  case Bedrock::OR16rr:
    return PostInc ? Bedrock::OR16postrm : Bedrock::OR16rm;
  case Bedrock::OR32rr:
    return PostInc ? Bedrock::OR32postrm : Bedrock::OR32rm;
  case Bedrock::OR64rr:
    return PostInc ? Bedrock::OR64postrm : Bedrock::OR64rm;
  case Bedrock::XOR8rr:
    return PostInc ? Bedrock::XOR8postrm : Bedrock::XOR8rm;
  case Bedrock::XOR16rr:
    return PostInc ? Bedrock::XOR16postrm : Bedrock::XOR16rm;
  case Bedrock::XOR32rr:
    return PostInc ? Bedrock::XOR32postrm : Bedrock::XOR32rm;
  case Bedrock::XOR64rr:
    return PostInc ? Bedrock::XOR64postrm : Bedrock::XOR64rm;
  case Bedrock::MULU8rr:
    return PostInc ? 0 : Bedrock::MULU8rm;
  case Bedrock::MULU16rr:
    return PostInc ? 0 : Bedrock::MULU16rm;
  case Bedrock::MULU32rr:
    return PostInc ? 0 : Bedrock::MULU32rm;
  case Bedrock::MULU64rr:
    return PostInc ? 0 : Bedrock::MULU64rm;
  }
}

static inline unsigned getIndexedMemSourceOpcode(unsigned RegOpcode, unsigned Scale,
                                          bool LongIndex) {
  if (Scale == 1 && !LongIndex) {
    switch (RegOpcode) {
    default:
      return 0;
    case Bedrock::ADD32rr:
      return Bedrock::ADD32idx1rm;
    case Bedrock::SUB32rr:
      return Bedrock::SUB32idx1rm;
    case Bedrock::AND32rr:
      return Bedrock::AND32idx1rm;
    case Bedrock::OR32rr:
      return Bedrock::OR32idx1rm;
    case Bedrock::XOR32rr:
      return Bedrock::XOR32idx1rm;
    case Bedrock::MULU32rr:
      return Bedrock::MULU32idx1rm;
    }
  }
  if (Scale == 4 && !LongIndex) {
    switch (RegOpcode) {
    default:
      return 0;
    case Bedrock::ADD32rr:
      return Bedrock::ADD32idx4rm;
    case Bedrock::SUB32rr:
      return Bedrock::SUB32idx4rm;
    case Bedrock::AND32rr:
      return Bedrock::AND32idx4rm;
    case Bedrock::OR32rr:
      return Bedrock::OR32idx4rm;
    case Bedrock::XOR32rr:
      return Bedrock::XOR32idx4rm;
    case Bedrock::MULU32rr:
      return Bedrock::MULU32idx4rm;
    }
  }
  if (Scale == 4 && LongIndex) {
    switch (RegOpcode) {
    default:
      return 0;
    case Bedrock::ADD32rr:
      return Bedrock::ADD32idx4lrm;
    case Bedrock::SUB32rr:
      return Bedrock::SUB32idx4lrm;
    case Bedrock::AND32rr:
      return Bedrock::AND32idx4lrm;
    case Bedrock::OR32rr:
      return Bedrock::OR32idx4lrm;
    case Bedrock::XOR32rr:
      return Bedrock::XOR32idx4lrm;
    case Bedrock::MULU32rr:
      return Bedrock::MULU32idx4lrm;
    }
  }
  return 0;
}

static inline bool memSourceFoldSavesSizeWithoutPostInc(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
  case Bedrock::MULU8rr:
  case Bedrock::MULU16rr:
  case Bedrock::MULU32rr:
  case Bedrock::MULU64rr:
    return true;
  }
}

static inline unsigned getRepPostMemSourceOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::ADD32postrm:
    return Bedrock::REPADD32postrm;
  }
}

static inline unsigned getMemDestBinOpcode(unsigned RegOpcode) {
  switch (RegOpcode) {
  default:
    return 0;
  case Bedrock::ADD8rr:
    return Bedrock::ADD8mr;
  case Bedrock::ADD16rr:
    return Bedrock::ADD16mr;
  case Bedrock::ADD32rr:
    return Bedrock::ADD32mr;
  case Bedrock::ADD64rr:
    return Bedrock::ADD64mr;
  case Bedrock::SUB8rr:
    return Bedrock::SUB8mr;
  case Bedrock::SUB16rr:
    return Bedrock::SUB16mr;
  case Bedrock::SUB32rr:
    return Bedrock::SUB32mr;
  case Bedrock::SUB64rr:
    return Bedrock::SUB64mr;
  case Bedrock::AND8rr:
    return Bedrock::AND8mr;
  case Bedrock::AND16rr:
    return Bedrock::AND16mr;
  case Bedrock::AND32rr:
    return Bedrock::AND32mr;
  case Bedrock::AND64rr:
    return Bedrock::AND64mr;
  case Bedrock::OR8rr:
    return Bedrock::OR8mr;
  case Bedrock::OR16rr:
    return Bedrock::OR16mr;
  case Bedrock::OR32rr:
    return Bedrock::OR32mr;
  case Bedrock::OR64rr:
    return Bedrock::OR64mr;
  case Bedrock::XOR8rr:
    return Bedrock::XOR8mr;
  case Bedrock::XOR16rr:
    return Bedrock::XOR16mr;
  case Bedrock::XOR32rr:
    return Bedrock::XOR32mr;
  case Bedrock::XOR64rr:
    return Bedrock::XOR64mr;
  }
}

static inline unsigned getMemDestImmBinOpcode(unsigned RegOpcode) {
  switch (RegOpcode) {
  default:
    return 0;
  case Bedrock::ADD8ri:
    return Bedrock::ADD8mi;
  case Bedrock::ADD16ri:
    return Bedrock::ADD16mi;
  case Bedrock::ADD32ri:
    return Bedrock::ADD32mi;
  case Bedrock::ADD64ri:
    return Bedrock::ADD64mi;
  case Bedrock::SUB8ri:
    return Bedrock::SUB8mi;
  case Bedrock::SUB16ri:
    return Bedrock::SUB16mi;
  case Bedrock::SUB32ri:
    return Bedrock::SUB32mi;
  case Bedrock::SUB64ri:
    return Bedrock::SUB64mi;
  case Bedrock::AND8ri:
    return Bedrock::AND8mi;
  case Bedrock::AND16ri:
    return Bedrock::AND16mi;
  case Bedrock::AND32ri:
    return Bedrock::AND32mi;
  case Bedrock::AND64ri:
  case Bedrock::AND64ai:
    return Bedrock::AND64mi;
  case Bedrock::OR8ri:
    return Bedrock::OR8mi;
  case Bedrock::OR16ri:
    return Bedrock::OR16mi;
  case Bedrock::OR32ri:
    return Bedrock::OR32mi;
  case Bedrock::OR64ri:
  case Bedrock::OR64ai:
    return Bedrock::OR64mi;
  case Bedrock::XOR8ri:
    return Bedrock::XOR8mi;
  case Bedrock::XOR16ri:
    return Bedrock::XOR16mi;
  case Bedrock::XOR32ri:
    return Bedrock::XOR32mi;
  case Bedrock::XOR64ri:
  case Bedrock::XOR64ai:
    return Bedrock::XOR64mi;
  }
}

static inline unsigned getMemImmFlagOpcode(unsigned RegOpcode) {
  switch (RegOpcode) {
  default:
    return 0;
  case Bedrock::CMP8ri:
    return Bedrock::CMP8mi;
  case Bedrock::CMP16ri:
    return Bedrock::CMP16mi;
  case Bedrock::CMP32ri:
    return Bedrock::CMP32mi;
  case Bedrock::CMP64ri:
    return Bedrock::CMP64mi;
  case Bedrock::TEST8ri:
    return Bedrock::TEST8mi;
  case Bedrock::TEST16ri:
    return Bedrock::TEST16mi;
  case Bedrock::TEST32ri:
    return Bedrock::TEST32mi;
  case Bedrock::TEST64ri:
    return Bedrock::TEST64mi;
  }
}

static inline bool canEncodeMemImmFlagOpcode(unsigned RegOpcode, int64_t Imm) {
  switch (RegOpcode) {
  default:
    return false;
  case Bedrock::CMP8ri:
  case Bedrock::CMP16ri:
  case Bedrock::CMP32ri:
  case Bedrock::CMP64ri:
    return true;
  case Bedrock::TEST8ri:
  case Bedrock::TEST16ri:
  case Bedrock::TEST32ri:
  case Bedrock::TEST64ri:
    return fitsImm6(Imm);
  }
}

static inline bool flagImmOpcodeMatchesLoad(unsigned FlagOpcode, unsigned LoadOpcode) {
  switch (LoadOpcode) {
  default:
    return false;
  case Bedrock::MOV8rm:
    return FlagOpcode == Bedrock::CMP8ri || FlagOpcode == Bedrock::TEST8ri;
  case Bedrock::MOV16rm:
    return FlagOpcode == Bedrock::CMP16ri || FlagOpcode == Bedrock::TEST16ri;
  case Bedrock::MOV32rm:
    return FlagOpcode == Bedrock::CMP32ri || FlagOpcode == Bedrock::TEST32ri;
  case Bedrock::MOV64rm:
    return FlagOpcode == Bedrock::CMP64ri || FlagOpcode == Bedrock::TEST64ri;
  }
}

static inline unsigned getMemRegFlagOpcode(unsigned FlagOpcode, unsigned LoadOpcode,
                                    bool LoadedIsLHS) {
  switch (LoadOpcode) {
  default:
    return 0;
  case Bedrock::MOV8rm:
    if (FlagOpcode == Bedrock::CMP8rr)
      return LoadedIsLHS ? Bedrock::CMP8mr : Bedrock::CMP8rm;
    if (FlagOpcode == Bedrock::TEST8rr)
      return LoadedIsLHS ? Bedrock::TEST8mr : Bedrock::TEST8rm;
    return 0;
  case Bedrock::MOV16rm:
    if (FlagOpcode == Bedrock::CMP16rr)
      return LoadedIsLHS ? Bedrock::CMP16mr : Bedrock::CMP16rm;
    if (FlagOpcode == Bedrock::TEST16rr)
      return LoadedIsLHS ? Bedrock::TEST16mr : Bedrock::TEST16rm;
    return 0;
  case Bedrock::MOV32rm:
    if (FlagOpcode == Bedrock::CMP32rr)
      return LoadedIsLHS ? Bedrock::CMP32mr : Bedrock::CMP32rm;
    if (FlagOpcode == Bedrock::TEST32rr)
      return LoadedIsLHS ? Bedrock::TEST32mr : Bedrock::TEST32rm;
    return 0;
  case Bedrock::MOV64rm:
    if (FlagOpcode == Bedrock::CMP64rr)
      return LoadedIsLHS ? Bedrock::CMP64mr : Bedrock::CMP64rm;
    if (FlagOpcode == Bedrock::TEST64rr)
      return LoadedIsLHS ? Bedrock::TEST64mr : Bedrock::TEST64rm;
    return 0;
  }
}

static inline unsigned getIndexedMemRegFlagOpcode(unsigned FlagOpcode, unsigned Scale,
                                           bool LongIndex, bool LoadedIsLHS) {
  if (Scale == 1 && !LongIndex) {
    if (FlagOpcode == Bedrock::CMP8rr)
      return LoadedIsLHS ? Bedrock::CMP8idx1mr : Bedrock::CMP8idx1rm;
    if (FlagOpcode == Bedrock::TEST8rr)
      return LoadedIsLHS ? Bedrock::TEST8idx1mr : Bedrock::TEST8idx1rm;
    if (FlagOpcode == Bedrock::CMP16rr)
      return LoadedIsLHS ? Bedrock::CMP16idx1mr : Bedrock::CMP16idx1rm;
    if (FlagOpcode == Bedrock::TEST16rr)
      return LoadedIsLHS ? Bedrock::TEST16idx1mr : Bedrock::TEST16idx1rm;
    if (FlagOpcode == Bedrock::CMP32rr)
      return LoadedIsLHS ? Bedrock::CMP32idx1mr : Bedrock::CMP32idx1rm;
    if (FlagOpcode == Bedrock::TEST32rr)
      return LoadedIsLHS ? Bedrock::TEST32idx1mr : Bedrock::TEST32idx1rm;
    if (FlagOpcode == Bedrock::CMP64rr)
      return LoadedIsLHS ? Bedrock::CMP64idx1mr : Bedrock::CMP64idx1rm;
    if (FlagOpcode == Bedrock::TEST64rr)
      return LoadedIsLHS ? Bedrock::TEST64idx1mr : Bedrock::TEST64idx1rm;
    return 0;
  }
  if (Scale == 4 && !LongIndex) {
    if (FlagOpcode == Bedrock::CMP8rr)
      return LoadedIsLHS ? Bedrock::CMP8idx4mr : Bedrock::CMP8idx4rm;
    if (FlagOpcode == Bedrock::TEST8rr)
      return LoadedIsLHS ? Bedrock::TEST8idx4mr : Bedrock::TEST8idx4rm;
    if (FlagOpcode == Bedrock::CMP16rr)
      return LoadedIsLHS ? Bedrock::CMP16idx4mr : Bedrock::CMP16idx4rm;
    if (FlagOpcode == Bedrock::TEST16rr)
      return LoadedIsLHS ? Bedrock::TEST16idx4mr : Bedrock::TEST16idx4rm;
    if (FlagOpcode == Bedrock::CMP32rr)
      return LoadedIsLHS ? Bedrock::CMP32idx4mr : Bedrock::CMP32idx4rm;
    if (FlagOpcode == Bedrock::TEST32rr)
      return LoadedIsLHS ? Bedrock::TEST32idx4mr : Bedrock::TEST32idx4rm;
    if (FlagOpcode == Bedrock::CMP64rr)
      return LoadedIsLHS ? Bedrock::CMP64idx4mr : Bedrock::CMP64idx4rm;
    if (FlagOpcode == Bedrock::TEST64rr)
      return LoadedIsLHS ? Bedrock::TEST64idx4mr : Bedrock::TEST64idx4rm;
    return 0;
  }
  if (Scale == 4 && LongIndex) {
    if (FlagOpcode == Bedrock::CMP8rr)
      return LoadedIsLHS ? Bedrock::CMP8idx4lmr : Bedrock::CMP8idx4lrm;
    if (FlagOpcode == Bedrock::TEST8rr)
      return LoadedIsLHS ? Bedrock::TEST8idx4lmr : Bedrock::TEST8idx4lrm;
    if (FlagOpcode == Bedrock::CMP16rr)
      return LoadedIsLHS ? Bedrock::CMP16idx4lmr : Bedrock::CMP16idx4lrm;
    if (FlagOpcode == Bedrock::TEST16rr)
      return LoadedIsLHS ? Bedrock::TEST16idx4lmr : Bedrock::TEST16idx4lrm;
    if (FlagOpcode == Bedrock::CMP32rr)
      return LoadedIsLHS ? Bedrock::CMP32idx4lmr : Bedrock::CMP32idx4lrm;
    if (FlagOpcode == Bedrock::TEST32rr)
      return LoadedIsLHS ? Bedrock::TEST32idx4lmr : Bedrock::TEST32idx4lrm;
    if (FlagOpcode == Bedrock::CMP64rr)
      return LoadedIsLHS ? Bedrock::CMP64idx4lmr : Bedrock::CMP64idx4lrm;
    if (FlagOpcode == Bedrock::TEST64rr)
      return LoadedIsLHS ? Bedrock::TEST64idx4lmr : Bedrock::TEST64idx4lrm;
  }
  return 0;
}

static inline unsigned getIndexedMemLoadOpcode(unsigned LoadOpcode, unsigned Scale,
                                        bool LongIndex) {
  if (Scale == 1 && !LongIndex) {
    switch (LoadOpcode) {
    default:
      return 0;
    case Bedrock::MOV8rm:
      return Bedrock::MOV8idx1rm;
    case Bedrock::MOV16rm:
      return Bedrock::MOV16idx1rm;
    case Bedrock::MOV32rm:
      return Bedrock::MOV32idx1rm;
    case Bedrock::MOV64rm:
      return Bedrock::MOV64idx1rm;
    }
  }
  if (Scale == 4 && !LongIndex) {
    switch (LoadOpcode) {
    default:
      return 0;
    case Bedrock::MOV8rm:
      return Bedrock::MOV8idx4rm;
    case Bedrock::MOV16rm:
      return Bedrock::MOV16idx4rm;
    case Bedrock::MOV32rm:
      return Bedrock::MOV32idx4rm;
    case Bedrock::MOV64rm:
      return Bedrock::MOV64idx4rm;
    }
  }
  if (Scale == 4 && LongIndex) {
    switch (LoadOpcode) {
    default:
      return 0;
    case Bedrock::MOV8rm:
      return Bedrock::MOV8idx4lrm;
    case Bedrock::MOV16rm:
      return Bedrock::MOV16idx4lrm;
    case Bedrock::MOV32rm:
      return Bedrock::MOV32idx4lrm;
    case Bedrock::MOV64rm:
      return Bedrock::MOV64idx4lrm;
    }
  }
  return 0;
}

static inline unsigned getIndexedMemStoreOpcode(unsigned StoreOpcode, unsigned Scale,
                                         bool LongIndex) {
  if (Scale == 1 && !LongIndex) {
    switch (StoreOpcode) {
    default:
      return 0;
    case Bedrock::MOV8mr:
      return Bedrock::MOV8idx1mr;
    case Bedrock::MOV16mr:
      return Bedrock::MOV16idx1mr;
    case Bedrock::MOV32mr:
      return Bedrock::MOV32idx1mr;
    case Bedrock::MOV64mr:
      return Bedrock::MOV64idx1mr;
    }
  }
  if (Scale == 4 && !LongIndex) {
    switch (StoreOpcode) {
    default:
      return 0;
    case Bedrock::MOV8mr:
      return Bedrock::MOV8idx4mr;
    case Bedrock::MOV16mr:
      return Bedrock::MOV16idx4mr;
    case Bedrock::MOV32mr:
      return Bedrock::MOV32idx4mr;
    case Bedrock::MOV64mr:
      return Bedrock::MOV64idx4mr;
    }
  }
  if (Scale == 4 && LongIndex) {
    switch (StoreOpcode) {
    default:
      return 0;
    case Bedrock::MOV8mr:
      return Bedrock::MOV8idx4lmr;
    case Bedrock::MOV16mr:
      return Bedrock::MOV16idx4lmr;
    case Bedrock::MOV32mr:
      return Bedrock::MOV32idx4lmr;
    case Bedrock::MOV64mr:
      return Bedrock::MOV64idx4lmr;
    }
  }
  return 0;
}

static inline unsigned getIndexedMemImmFlagOpcode(unsigned FlagOpcode, unsigned Scale,
                                           bool LongIndex) {
  if (Scale == 1 && !LongIndex) {
    switch (FlagOpcode) {
    default:
      return 0;
    case Bedrock::CMP8mi:
      return Bedrock::CMP8idx1mi;
    case Bedrock::CMP16mi:
      return Bedrock::CMP16idx1mi;
    case Bedrock::CMP32mi:
      return Bedrock::CMP32idx1mi;
    case Bedrock::CMP64mi:
      return Bedrock::CMP64idx1mi;
    case Bedrock::TEST8mi:
      return Bedrock::TEST8idx1mi;
    case Bedrock::TEST16mi:
      return Bedrock::TEST16idx1mi;
    case Bedrock::TEST32mi:
      return Bedrock::TEST32idx1mi;
    case Bedrock::TEST64mi:
      return Bedrock::TEST64idx1mi;
    }
  }
  if (Scale == 4 && !LongIndex) {
    switch (FlagOpcode) {
    default:
      return 0;
    case Bedrock::CMP8mi:
      return Bedrock::CMP8idx4mi;
    case Bedrock::CMP16mi:
      return Bedrock::CMP16idx4mi;
    case Bedrock::CMP32mi:
      return Bedrock::CMP32idx4mi;
    case Bedrock::CMP64mi:
      return Bedrock::CMP64idx4mi;
    case Bedrock::TEST8mi:
      return Bedrock::TEST8idx4mi;
    case Bedrock::TEST16mi:
      return Bedrock::TEST16idx4mi;
    case Bedrock::TEST32mi:
      return Bedrock::TEST32idx4mi;
    case Bedrock::TEST64mi:
      return Bedrock::TEST64idx4mi;
    }
  }
  if (Scale == 4 && LongIndex) {
    switch (FlagOpcode) {
    default:
      return 0;
    case Bedrock::CMP8mi:
      return Bedrock::CMP8idx4lmi;
    case Bedrock::CMP16mi:
      return Bedrock::CMP16idx4lmi;
    case Bedrock::CMP32mi:
      return Bedrock::CMP32idx4lmi;
    case Bedrock::CMP64mi:
      return Bedrock::CMP64idx4lmi;
    case Bedrock::TEST8mi:
      return Bedrock::TEST8idx4lmi;
    case Bedrock::TEST16mi:
      return Bedrock::TEST16idx4lmi;
    case Bedrock::TEST32mi:
      return Bedrock::TEST32idx4lmi;
    case Bedrock::TEST64mi:
      return Bedrock::TEST64idx4lmi;
    }
  }
  return 0;
}

static inline unsigned getMemTestOpcodeForAndImm(unsigned AndOpcode) {
  switch (AndOpcode) {
  default:
    return 0;
  case Bedrock::AND8ri:
    return Bedrock::TEST8mi;
  case Bedrock::AND16ri:
    return Bedrock::TEST16mi;
  case Bedrock::AND32ri:
    return Bedrock::TEST32mi;
  case Bedrock::AND64ri:
  case Bedrock::AND64ai:
    return Bedrock::TEST64mi;
  }
}

static inline unsigned getRegTestOpcodeForAndImm(unsigned AndOpcode) {
  switch (AndOpcode) {
  default:
    return 0;
  case Bedrock::AND8ri:
    return Bedrock::TEST8ri;
  case Bedrock::AND16ri:
    return Bedrock::TEST16ri;
  case Bedrock::AND32ri:
    return Bedrock::TEST32ri;
  case Bedrock::AND64ri:
  case Bedrock::AND64ai:
    return Bedrock::TEST64ri;
  }
}

static inline bool cmpZeroOpcodeMatchesAndImm(unsigned CmpOpcode, unsigned AndOpcode) {
  switch (AndOpcode) {
  default:
    return false;
  case Bedrock::AND8ri:
    return CmpOpcode == Bedrock::CMP8ri;
  case Bedrock::AND16ri:
    return CmpOpcode == Bedrock::CMP16ri;
  case Bedrock::AND32ri:
    return CmpOpcode == Bedrock::CMP32ri;
  case Bedrock::AND64ri:
  case Bedrock::AND64ai:
    return CmpOpcode == Bedrock::CMP64ri;
  }
}

static inline bool cmpRROpcodeMatchesAndImm(unsigned CmpOpcode, unsigned AndOpcode) {
  switch (AndOpcode) {
  default:
    return false;
  case Bedrock::AND8ri:
    return CmpOpcode == Bedrock::CMP8rr;
  case Bedrock::AND16ri:
    return CmpOpcode == Bedrock::CMP16rr;
  case Bedrock::AND32ri:
    return CmpOpcode == Bedrock::CMP32rr;
  case Bedrock::AND64ri:
  case Bedrock::AND64ai:
    return CmpOpcode == Bedrock::CMP64rr;
  }
}

static inline bool testSelfOpcodeMatchesAndImm(unsigned TestOpcode,
                                        unsigned AndOpcode) {
  switch (AndOpcode) {
  default:
    return false;
  case Bedrock::AND8ri:
    return TestOpcode == Bedrock::TEST8rr;
  case Bedrock::AND16ri:
    return TestOpcode == Bedrock::TEST16rr;
  case Bedrock::AND32ri:
    return TestOpcode == Bedrock::TEST32rr;
  case Bedrock::AND64ri:
  case Bedrock::AND64ai:
    return TestOpcode == Bedrock::TEST64rr;
  }
}

static inline unsigned getOppositeAddSubMemImmOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::ADD8mi:
    return Bedrock::SUB8mi;
  case Bedrock::ADD16mi:
    return Bedrock::SUB16mi;
  case Bedrock::ADD32mi:
    return Bedrock::SUB32mi;
  case Bedrock::ADD64mi:
    return Bedrock::SUB64mi;
  case Bedrock::SUB8mi:
    return Bedrock::ADD8mi;
  case Bedrock::SUB16mi:
    return Bedrock::ADD16mi;
  case Bedrock::SUB32mi:
    return Bedrock::ADD32mi;
  case Bedrock::SUB64mi:
    return Bedrock::ADD64mi;
  }
}

static inline unsigned getIncDecRegOpcode(unsigned Opcode, int64_t Imm) {
  bool IsInc = false;
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::ADD8ri:
    IsInc = Imm == 1;
    return IsInc ? Bedrock::INC8r : Imm == -1 ? Bedrock::DEC8r : 0;
  case Bedrock::ADD16ri:
    IsInc = Imm == 1;
    return IsInc ? Bedrock::INC16r : Imm == -1 ? Bedrock::DEC16r : 0;
  case Bedrock::ADD32ri:
    IsInc = Imm == 1;
    return IsInc ? Bedrock::INC32r : Imm == -1 ? Bedrock::DEC32r : 0;
  case Bedrock::ADD64ri:
    IsInc = Imm == 1;
    return IsInc ? Bedrock::INC64r : Imm == -1 ? Bedrock::DEC64r : 0;
  case Bedrock::SUB8ri:
    IsInc = Imm == -1;
    return IsInc ? Bedrock::INC8r : Imm == 1 ? Bedrock::DEC8r : 0;
  case Bedrock::SUB16ri:
    IsInc = Imm == -1;
    return IsInc ? Bedrock::INC16r : Imm == 1 ? Bedrock::DEC16r : 0;
  case Bedrock::SUB32ri:
    IsInc = Imm == -1;
    return IsInc ? Bedrock::INC32r : Imm == 1 ? Bedrock::DEC32r : 0;
  case Bedrock::SUB64ri:
    IsInc = Imm == -1;
    return IsInc ? Bedrock::INC64r : Imm == 1 ? Bedrock::DEC64r : 0;
  }
}

static inline unsigned getIncDecMemOpcode(unsigned Opcode, int64_t Imm) {
  bool IsInc = false;
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::ADD8mi:
    IsInc = Imm == 1;
    return IsInc ? Bedrock::INC8m : Imm == -1 ? Bedrock::DEC8m : 0;
  case Bedrock::ADD16mi:
    IsInc = Imm == 1;
    return IsInc ? Bedrock::INC16m : Imm == -1 ? Bedrock::DEC16m : 0;
  case Bedrock::ADD32mi:
    IsInc = Imm == 1;
    return IsInc ? Bedrock::INC32m : Imm == -1 ? Bedrock::DEC32m : 0;
  case Bedrock::ADD64mi:
    IsInc = Imm == 1;
    return IsInc ? Bedrock::INC64m : Imm == -1 ? Bedrock::DEC64m : 0;
  case Bedrock::SUB8mi:
    IsInc = Imm == -1;
    return IsInc ? Bedrock::INC8m : Imm == 1 ? Bedrock::DEC8m : 0;
  case Bedrock::SUB16mi:
    IsInc = Imm == -1;
    return IsInc ? Bedrock::INC16m : Imm == 1 ? Bedrock::DEC16m : 0;
  case Bedrock::SUB32mi:
    IsInc = Imm == -1;
    return IsInc ? Bedrock::INC32m : Imm == 1 ? Bedrock::DEC32m : 0;
  case Bedrock::SUB64mi:
    IsInc = Imm == -1;
    return IsInc ? Bedrock::INC64m : Imm == 1 ? Bedrock::DEC64m : 0;
  }
}

static inline bool isCommutableBinOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
  case Bedrock::ADD8rr:
  case Bedrock::ADD16rr:
  case Bedrock::ADD32rr:
  case Bedrock::ADD64rr:
  case Bedrock::AND8rr:
  case Bedrock::AND16rr:
  case Bedrock::AND32rr:
  case Bedrock::AND64rr:
  case Bedrock::OR8rr:
  case Bedrock::OR16rr:
  case Bedrock::OR32rr:
  case Bedrock::OR64rr:
  case Bedrock::XOR8rr:
  case Bedrock::XOR16rr:
  case Bedrock::XOR32rr:
  case Bedrock::XOR64rr:
    return true;
  }
}

static inline bool binOpcodeMatchesLoad(unsigned BinOpcode, unsigned LoadOpcode) {
  switch (LoadOpcode) {
  default:
    return false;
  case Bedrock::MOV8rm:
    return BinOpcode == Bedrock::ADD8rr || BinOpcode == Bedrock::SUB8rr ||
           BinOpcode == Bedrock::AND8rr || BinOpcode == Bedrock::OR8rr ||
           BinOpcode == Bedrock::XOR8rr || BinOpcode == Bedrock::ADD8ri ||
           BinOpcode == Bedrock::SUB8ri || BinOpcode == Bedrock::AND8ri ||
           BinOpcode == Bedrock::OR8ri || BinOpcode == Bedrock::XOR8ri;
  case Bedrock::MOV16rm:
    return BinOpcode == Bedrock::ADD16rr || BinOpcode == Bedrock::SUB16rr ||
           BinOpcode == Bedrock::AND16rr || BinOpcode == Bedrock::OR16rr ||
           BinOpcode == Bedrock::XOR16rr || BinOpcode == Bedrock::ADD16ri ||
           BinOpcode == Bedrock::SUB16ri || BinOpcode == Bedrock::AND16ri ||
           BinOpcode == Bedrock::OR16ri || BinOpcode == Bedrock::XOR16ri;
  case Bedrock::MOV32rm:
    return BinOpcode == Bedrock::ADD32rr || BinOpcode == Bedrock::SUB32rr ||
           BinOpcode == Bedrock::AND32rr || BinOpcode == Bedrock::OR32rr ||
           BinOpcode == Bedrock::XOR32rr || BinOpcode == Bedrock::ADD32ri ||
           BinOpcode == Bedrock::SUB32ri || BinOpcode == Bedrock::AND32ri ||
           BinOpcode == Bedrock::OR32ri || BinOpcode == Bedrock::XOR32ri;
  case Bedrock::MOV64rm:
    return BinOpcode == Bedrock::ADD64rr || BinOpcode == Bedrock::SUB64rr ||
           BinOpcode == Bedrock::AND64rr || BinOpcode == Bedrock::OR64rr ||
           BinOpcode == Bedrock::XOR64rr || BinOpcode == Bedrock::ADD64ri ||
           BinOpcode == Bedrock::SUB64ri || BinOpcode == Bedrock::AND64ri ||
           BinOpcode == Bedrock::OR64ri || BinOpcode == Bedrock::XOR64ri ||
           BinOpcode == Bedrock::AND64ai || BinOpcode == Bedrock::OR64ai ||
           BinOpcode == Bedrock::XOR64ai;
  }
}

static inline unsigned getMAddMemOpcode(unsigned MulOpcode, bool PostInc) {
  switch (MulOpcode) {
  default:
    return 0;
  case Bedrock::MULU8rr:
    return PostInc ? Bedrock::MADD8postmrr : Bedrock::MADD8mrr;
  case Bedrock::MULU16rr:
    return PostInc ? Bedrock::MADD16postmrr : Bedrock::MADD16mrr;
  case Bedrock::MULU32rr:
    return PostInc ? Bedrock::MADD32postmrr : Bedrock::MADD32mrr;
  case Bedrock::MULU64rr:
    return PostInc ? Bedrock::MADD64postmrr : Bedrock::MADD64mrr;
  }
}

static inline unsigned getDivModOpcode(unsigned DivOpcode) {
  switch (DivOpcode) {
  default:
    return 0;
  case Bedrock::DIVS8rr:
    return Bedrock::DIVMODS8rr;
  case Bedrock::DIVS16rr:
    return Bedrock::DIVMODS16rr;
  case Bedrock::DIVS32rr:
    return Bedrock::DIVMODS32rr;
  case Bedrock::DIVS64rr:
    return Bedrock::DIVMODS64rr;
  case Bedrock::DIVU8rr:
    return Bedrock::DIVMODU8rr;
  case Bedrock::DIVU16rr:
    return Bedrock::DIVMODU16rr;
  case Bedrock::DIVU32rr:
    return Bedrock::DIVMODU32rr;
  case Bedrock::DIVU64rr:
    return Bedrock::DIVMODU64rr;
  }
}

static inline unsigned getDivModMulOpcode(unsigned DivOpcode) {
  switch (DivOpcode) {
  default:
    return 0;
  case Bedrock::DIVS8rr:
  case Bedrock::DIVU8rr:
    return Bedrock::MULU8rr;
  case Bedrock::DIVS16rr:
  case Bedrock::DIVU16rr:
    return Bedrock::MULU16rr;
  case Bedrock::DIVS32rr:
  case Bedrock::DIVU32rr:
    return Bedrock::MULU32rr;
  case Bedrock::DIVS64rr:
  case Bedrock::DIVU64rr:
    return Bedrock::MULU64rr;
  }
}

static inline unsigned getDivModSubOpcode(unsigned DivOpcode) {
  switch (DivOpcode) {
  default:
    return 0;
  case Bedrock::DIVS8rr:
  case Bedrock::DIVU8rr:
    return Bedrock::SUB8rr;
  case Bedrock::DIVS16rr:
  case Bedrock::DIVU16rr:
    return Bedrock::SUB16rr;
  case Bedrock::DIVS32rr:
  case Bedrock::DIVU32rr:
    return Bedrock::SUB32rr;
  case Bedrock::DIVS64rr:
  case Bedrock::DIVU64rr:
    return Bedrock::SUB64rr;
  }
}

static inline bool isLoadForBitOp(const MachineInstr &MI, Register &Dst,
                           Register &Base, int64_t &Offset) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::MOV8rm:
  case Bedrock::MOV16rm:
  case Bedrock::MOV32rm:
  case Bedrock::MOV64rm:
    break;
  }
  if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg() || !MI.getOperand(2).isImm())
    return false;
  Dst = MI.getOperand(0).getReg();
  Base = MI.getOperand(1).getReg();
  Offset = MI.getOperand(2).getImm();
  return true;
}

static inline bool isStoreForBitOp(const MachineInstr &MI, Register Src, Register Base,
                            int64_t Offset) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::MOV8mr:
  case Bedrock::MOV16mr:
  case Bedrock::MOV32mr:
  case Bedrock::MOV64mr:
    break;
  }
  return MI.getNumOperands() >= 3 && MI.getOperand(0).isReg() &&
         MI.getOperand(1).isReg() && MI.getOperand(2).isImm() &&
         MI.getOperand(0).getReg() == Src &&
         MI.getOperand(1).getReg() == Base &&
         MI.getOperand(2).getImm() == Offset;
}

static inline bool isMemLoad(const MachineInstr &MI, Register &Dst, Register &Base,
                      int64_t &Offset) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::MOV8rm:
  case Bedrock::MOV16rm:
  case Bedrock::MOV32rm:
  case Bedrock::MOV64rm:
    break;
  }
  if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg() || !MI.getOperand(2).isImm())
    return false;
  Dst = MI.getOperand(0).getReg();
  Base = MI.getOperand(1).getReg();
  Offset = MI.getOperand(2).getImm();
  return true;
}

static inline bool isIndexedMemLoad(const MachineInstr &MI, Register &Dst,
                             Register &Base, Register &Index, int64_t &Offset,
                             unsigned &Scale, bool &LongIndex) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::MOV32idx1rm:
    Scale = 1;
    LongIndex = false;
    break;
  case Bedrock::MOV32idx4rm:
    Scale = 4;
    LongIndex = false;
    break;
  case Bedrock::MOV32idx4lrm:
    Scale = 4;
    LongIndex = true;
    break;
  }
  if (MI.getNumOperands() < 4 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg() || !MI.getOperand(2).isReg() ||
      !MI.getOperand(3).isImm())
    return false;
  Dst = MI.getOperand(0).getReg();
  Base = MI.getOperand(1).getReg();
  Index = MI.getOperand(2).getReg();
  Offset = MI.getOperand(3).getImm();
  return true;
}

static inline bool isMemCopyLoad(const MachineInstr &MI, Register &Dst, Register &Base,
                          int64_t &Offset, unsigned &PlainOpcode,
                          bool &PostInc) {
  PostInc = false;
  PlainOpcode = MI.getOpcode();
  if (isMemLoad(MI, Dst, Base, Offset))
    return true;

  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::MOV8postrm:
    PlainOpcode = Bedrock::MOV8rm;
    break;
  case Bedrock::MOV16postrm:
    PlainOpcode = Bedrock::MOV16rm;
    break;
  case Bedrock::MOV32postrm:
    PlainOpcode = Bedrock::MOV32rm;
    break;
  case Bedrock::MOV64postrm:
    PlainOpcode = Bedrock::MOV64rm;
    break;
  }

  if (MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg())
    return false;
  Dst = MI.getOperand(0).getReg();
  Base = MI.getOperand(1).getReg();
  Offset = 0;
  PostInc = true;
  return true;
}

static inline bool isMAddLoad(const MachineInstr &MI, Register &Dst, Register &Base,
                       int64_t &Offset, bool &PostInc) {
  PostInc = false;
  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::MOV8postrm:
  case Bedrock::MOV16postrm:
  case Bedrock::MOV32postrm:
  case Bedrock::MOV64postrm:
    if (MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
        !MI.getOperand(1).isReg())
      return false;
    Dst = MI.getOperand(0).getReg();
    Base = MI.getOperand(1).getReg();
    Offset = 0;
    PostInc = true;
    return true;
  case Bedrock::MOV8rm:
  case Bedrock::MOV16rm:
  case Bedrock::MOV32rm:
  case Bedrock::MOV64rm:
    if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
        !MI.getOperand(1).isReg() || !MI.getOperand(2).isImm())
      return false;
    Dst = MI.getOperand(0).getReg();
    Base = MI.getOperand(1).getReg();
    Offset = MI.getOperand(2).getImm();
    return true;
  }
}

static inline bool isMemStore(const MachineInstr &MI, Register &Src, Register &Base,
                       int64_t &Offset) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::MOV8mr:
  case Bedrock::MOV16mr:
  case Bedrock::MOV32mr:
  case Bedrock::MOV64mr:
    break;
  }
  if (MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg() || !MI.getOperand(2).isImm())
    return false;
  Src = MI.getOperand(0).getReg();
  Base = MI.getOperand(1).getReg();
  Offset = MI.getOperand(2).getImm();
  return true;
}

static inline bool isMemCopyStore(const MachineInstr &MI, Register &Src,
                           Register &Base, int64_t &Offset,
                           unsigned &PlainOpcode, bool &PostInc) {
  PostInc = false;
  PlainOpcode = MI.getOpcode();
  if (isMemStore(MI, Src, Base, Offset))
    return true;

  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::MOV8postmr:
    PlainOpcode = Bedrock::MOV8mr;
    break;
  case Bedrock::MOV16postmr:
    PlainOpcode = Bedrock::MOV16mr;
    break;
  case Bedrock::MOV32postmr:
    PlainOpcode = Bedrock::MOV32mr;
    break;
  case Bedrock::MOV64postmr:
    PlainOpcode = Bedrock::MOV64mr;
    break;
  }

  if (MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg())
    return false;
  Src = MI.getOperand(0).getReg();
  Base = MI.getOperand(1).getReg();
  Offset = 0;
  PostInc = true;
  return true;
}

static inline bool isMemoryBaseOperand(const MachineInstr &MI, unsigned OpNo,
                                Register Base, const TargetRegisterInfo &TRI) {
  if (OpNo + 1 >= MI.getNumOperands())
    return false;
  const MachineOperand &MO = MI.getOperand(OpNo);
  const MachineOperand &Next = MI.getOperand(OpNo + 1);
  return MO.isReg() && MO.readsReg() && !MO.isDef() &&
         regsOverlap(TRI, MO.getReg(), Base) && Next.isImm();
}

static inline bool onlyUsesRegAsMemoryBase(const MachineInstr &MI, Register Base,
                                    const TargetRegisterInfo &TRI) {
  bool SawBase = false;
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    const MachineOperand &MO = MI.getOperand(I);
    if (!operandTouchesReg(MO, Base, TRI))
      continue;
    if (!isMemoryBaseOperand(MI, I, Base, TRI))
      return false;
    SawBase = true;
  }
  return SawBase;
}

static inline void replaceMemoryBase(MachineInstr &MI, Register OldBase,
                              Register NewBase, const TargetRegisterInfo &TRI) {
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    MachineOperand &MO = MI.getOperand(I);
    if (isMemoryBaseOperand(MI, I, OldBase, TRI))
      MO.setReg(NewBase);
  }
}

static inline std::optional<unsigned> getIndexedMemoryBaseOperandNo(unsigned Opcode) {
  switch (Opcode) {
  default:
    return std::nullopt;
  case Bedrock::MOV32idx1rm:
  case Bedrock::MOV32idx4rm:
  case Bedrock::MOV32idx4lrm:
  case Bedrock::MOV32idx1mr:
  case Bedrock::MOV32idx4mr:
  case Bedrock::MOV32idx4lmr:
  case Bedrock::MOV32idx1mm:
  case Bedrock::MOV32idx4mm:
  case Bedrock::MOV32idx4lmm:
  case Bedrock::MOV32midx1:
  case Bedrock::MOV32midx4:
  case Bedrock::MOV32midx4l:
  case Bedrock::ADD32idx1rm:
  case Bedrock::ADD32idx4rm:
  case Bedrock::ADD32idx4lrm:
  case Bedrock::SUB32idx1rm:
  case Bedrock::SUB32idx4rm:
  case Bedrock::SUB32idx4lrm:
  case Bedrock::AND32idx1rm:
  case Bedrock::AND32idx4rm:
  case Bedrock::AND32idx4lrm:
  case Bedrock::OR32idx1rm:
  case Bedrock::OR32idx4rm:
  case Bedrock::OR32idx4lrm:
  case Bedrock::XOR32idx1rm:
  case Bedrock::XOR32idx4rm:
  case Bedrock::XOR32idx4lrm:
  case Bedrock::MULU32idx1rm:
  case Bedrock::MULU32idx4rm:
  case Bedrock::MULU32idx4lrm:
  case Bedrock::CMP32idx1rm:
  case Bedrock::CMP32idx4rm:
  case Bedrock::CMP32idx4lrm:
  case Bedrock::CMP32idx1mr:
  case Bedrock::CMP32idx4mr:
  case Bedrock::CMP32idx4lmr:
  case Bedrock::TEST32idx1rm:
  case Bedrock::TEST32idx4rm:
  case Bedrock::TEST32idx4lrm:
  case Bedrock::TEST32idx1mr:
  case Bedrock::TEST32idx4mr:
  case Bedrock::TEST32idx4lmr:
    return 1;
  case Bedrock::INC32idx1m:
  case Bedrock::INC32idx4m:
  case Bedrock::INC32idx4lm:
  case Bedrock::DEC32idx1m:
  case Bedrock::DEC32idx4m:
  case Bedrock::DEC32idx4lm:
    return 0;
  }
}

static inline bool isIndexedMemoryBaseOperand(const MachineInstr &MI, unsigned OpNo,
                                       Register Base,
                                       const TargetRegisterInfo &TRI) {
  std::optional<unsigned> BaseOp =
      getIndexedMemoryBaseOperandNo(MI.getOpcode());
  if (!BaseOp || *BaseOp != OpNo || OpNo >= MI.getNumOperands())
    return false;
  const MachineOperand &MO = MI.getOperand(OpNo);
  return MO.isReg() && MO.readsReg() && !MO.isDef() &&
         regsOverlap(TRI, MO.getReg(), Base);
}

static inline bool onlyUsesRegAsAnyMemoryBase(const MachineInstr &MI, Register Base,
                                       const TargetRegisterInfo &TRI) {
  bool SawBase = false;
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    const MachineOperand &MO = MI.getOperand(I);
    if (!operandTouchesReg(MO, Base, TRI))
      continue;
    if (!isMemoryBaseOperand(MI, I, Base, TRI) &&
        !isIndexedMemoryBaseOperand(MI, I, Base, TRI))
      return false;
    SawBase = true;
  }
  return SawBase;
}

static inline void replaceAnyMemoryBase(MachineInstr &MI, Register OldBase,
                                 Register NewBase,
                                 const TargetRegisterInfo &TRI) {
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    MachineOperand &MO = MI.getOperand(I);
    if (isMemoryBaseOperand(MI, I, OldBase, TRI) ||
        isIndexedMemoryBaseOperand(MI, I, OldBase, TRI))
      MO.setReg(NewBase);
  }
}

struct EntryTrackedValue {
  Register Origin;
  unsigned Width = 0;
  SmallVector<MachineInstr *, 4> Defs;
};

struct EntryStoreRewrite {
  MachineInstr *Store = nullptr;
  unsigned Opcode = 0;
  Register Origin;
  int64_t Offset = 0;
  EntryTrackedValue Value;
};

struct StackConstStore {
  MachineInstr *ImmDef = nullptr;
  MachineInstr *Store = nullptr;
  MachineBasicBlock *MBB = nullptr;
  Register Reg;
  int64_t Offset = 0;
  int64_t Value = 0;
  unsigned Size = 4;
};

struct StackConstMulReplacement {
  MachineInstr *Load = nullptr;
  MachineInstr *Mul = nullptr;
  Register Product;
  Register Value;
  Register Scratch;
  unsigned Shift = 0;
};

static inline std::optional<unsigned> getShiftForSmallConstMul(int64_t Value) {
  switch (Value) {
  default:
    return std::nullopt;
  case 3:
    return 1;
  case 5:
    return 2;
  }
}

static inline bool isMov32Imm(const MachineInstr &MI, Register &Dst, int64_t &Imm) {
  if (MI.getOpcode() != Bedrock::MOV32ri || MI.getNumOperands() < 2 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isImm())
    return false;
  Dst = MI.getOperand(0).getReg();
  Imm = MI.getOperand(1).getImm();
  return true;
}

static inline bool isMov64Imm(const MachineInstr &MI, Register &Dst, int64_t &Imm) {
  if (MI.getOpcode() != Bedrock::MOV64ri || MI.getNumOperands() < 2 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isImm())
    return false;
  Dst = MI.getOperand(0).getReg();
  Imm = MI.getOperand(1).getImm();
  return true;
}

static inline bool isMovImmForCmp(const MachineInstr &MI, Register &Dst,
                           int64_t &Imm) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::MOV8ri:
  case Bedrock::MOV16ri:
  case Bedrock::MOV32ri:
  case Bedrock::MOV64ri:
    break;
  }
  if (MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isImm())
    return false;
  Dst = MI.getOperand(0).getReg();
  Imm = MI.getOperand(1).getImm();
  return true;
}

static inline unsigned getProfitableCmpImmOpcode(unsigned MovOpcode,
                                          unsigned CmpOpcode, int64_t Imm) {
  if (Imm == 0)
    return 0;
  switch (CmpOpcode) {
  default:
    return 0;
  case Bedrock::CMP8rr:
    return MovOpcode == Bedrock::MOV8ri ? Bedrock::CMP8ri : 0;
  case Bedrock::CMP16rr:
    return MovOpcode == Bedrock::MOV16ri ? Bedrock::CMP16ri : 0;
  case Bedrock::CMP32rr:
    if (MovOpcode == Bedrock::MOV32ri)
      return Bedrock::CMP32ri;
    return 0;
  case Bedrock::CMP64rr:
    if (MovOpcode == Bedrock::MOV64ri && Imm > 0 && fitsImm6(Imm))
      return Bedrock::CMP64ri;
    return 0;
  }
}

static inline bool isStoreSrcCompatible(unsigned Opcode, Register Reg) {
  switch (Opcode) {
  default:
    return false;
  case Bedrock::MOV8mr:
  case Bedrock::MOV16mr:
  case Bedrock::MOV32mr:
    return isDReg(Reg);
  case Bedrock::MOV64mr:
    return isDReg(Reg) || isAReg(Reg);
  }
}

static inline bool isTrackableRegMove(const MachineInstr &MI, Register &Dst,
                               Register &Src, unsigned &Width) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::MOV8rr:
  case Bedrock::TRUNC64to8:
  case Bedrock::TRUNC32to8:
  case Bedrock::TRUNC16to8:
    Width = 8;
    break;
  case Bedrock::MOV16rr:
  case Bedrock::TRUNC64to16:
  case Bedrock::TRUNC32to16:
    Width = 16;
    break;
  case Bedrock::MOV32rr:
  case Bedrock::TRUNC64to32:
    Width = 32;
    break;
  case Bedrock::MOV64rr:
    Width = 64;
    break;
  }
  if (MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg())
    return false;
  Dst = MI.getOperand(0).getReg();
  Src = MI.getOperand(1).getReg();
  return true;
}

static inline bool hasOrderedMemOperand(const MachineInstr &MI) {
  for (MachineMemOperand *MMO : MI.memoperands())
    if (MMO->isVolatile() || MMO->isAtomic())
      return true;
  return false;
}

static inline bool instrHasSPMemOffset(const MachineInstr &MI, int64_t Offset,
                                const TargetRegisterInfo &TRI) {
  if (!MI.mayLoadOrStore())
    return false;
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I)
    if (isMemoryBaseOperand(MI, I, Bedrock::SP, TRI) &&
        MI.getOperand(I + 1).getImm() == Offset)
      return true;
  return false;
}

static inline bool rangesOverlap(int64_t AOffset, unsigned ASize, int64_t BOffset,
                          unsigned BSize) {
  int64_t AEnd = AOffset + int64_t(ASize);
  int64_t BEnd = BOffset + int64_t(BSize);
  return AOffset < BEnd && BOffset < AEnd;
}

static inline bool instrHasOverlappingSPMemRange(const MachineInstr &MI,
                                          int64_t Offset, unsigned Size,
                                          const TargetRegisterInfo &TRI) {
  if (!MI.mayLoadOrStore())
    return false;
  unsigned AccessSize = memSizeForOpcode(MI.getOpcode());
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    if (!isMemoryBaseOperand(MI, I, Bedrock::SP, TRI))
      continue;
    if (AccessSize == 0)
      return true;
    if (rangesOverlap(Offset, Size, MI.getOperand(I + 1).getImm(), AccessSize))
      return true;
  }
  return false;
}

static inline bool
stackOffsetUsedOutsideSet(MachineFunction &MF, int64_t Offset,
                          const SmallPtrSetImpl<MachineInstr *> &Ignored,
                          const TargetRegisterInfo &TRI) {
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || Ignored.contains(&MI))
        continue;
      if (instrHasSPMemOffset(MI, Offset, TRI))
        return true;
    }
  }
  return false;
}

static inline bool
stackRangeUsedOutsideSet(MachineFunction &MF, int64_t Offset, unsigned Size,
                         const SmallPtrSetImpl<MachineInstr *> &Ignored,
                         const TargetRegisterInfo &TRI) {
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || Ignored.contains(&MI))
        continue;
      if (instrHasOverlappingSPMemRange(MI, Offset, Size, TRI))
        return true;
    }
  }
  return false;
}

static inline bool instrPrecedesInBlock(const MachineInstr *A, const MachineInstr *B) {
  if (!A || !B || A->getParent() != B->getParent())
    return false;
  for (const MachineInstr &MI : *A->getParent()) {
    if (&MI == A)
      return true;
    if (&MI == B)
      return false;
  }
  return false;
}

static inline bool allPredecessorPathsReachBlock(
    const MachineBasicBlock *MBB, const MachineBasicBlock *Target,
    const MachineBasicBlock *UseMBB,
    SmallPtrSetImpl<const MachineBasicBlock *> &Visiting,
    SmallPtrSetImpl<const MachineBasicBlock *> &Proven) {
  if (MBB == Target)
    return true;
  if (Proven.contains(MBB))
    return true;
  if (!Visiting.insert(MBB).second)
    return false;
  if (MBB->pred_empty()) {
    Visiting.erase(MBB);
    return false;
  }

  bool SawPred = false;
  for (const MachineBasicBlock *Pred : MBB->predecessors()) {
    if (Pred == UseMBB)
      continue;
    SawPred = true;
    if (!allPredecessorPathsReachBlock(Pred, Target, UseMBB, Visiting,
                                       Proven)) {
      Visiting.erase(MBB);
      return false;
    }
  }
  if (!SawPred) {
    Visiting.erase(MBB);
    return false;
  }

  Visiting.erase(MBB);
  Proven.insert(MBB);
  return true;
}

static inline bool stackConstStoreAvailableAtUse(const StackConstStore &Slot,
                                          const MachineInstr &Use) {
  const MachineBasicBlock *UseMBB = Use.getParent();
  if (Slot.MBB == UseMBB)
    return instrPrecedesInBlock(Slot.Store, &Use);

  if (Slot.MBB == &Slot.MBB->getParent()->front())
    return true;

  bool SawPred = false;
  SmallPtrSet<const MachineBasicBlock *, 8> Visiting;
  SmallPtrSet<const MachineBasicBlock *, 8> Proven;
  for (const MachineBasicBlock *Pred : UseMBB->predecessors()) {
    if (Pred == UseMBB)
      continue;
    SawPred = true;
    if (!allPredecessorPathsReachBlock(Pred, Slot.MBB, UseMBB, Visiting,
                                       Proven))
      return false;
  }
  return SawPred;
}

static inline bool isPromotableStackSlotAccess(const MachineInstr &MI,
                                        int64_t &Offset) {
  if (hasOrderedMemOperand(MI))
    return false;

  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::MOV32rm:
  case Bedrock::MOV32mr:
    if (MI.getNumOperands() < 3 || !MI.getOperand(1).isReg() ||
        !MI.getOperand(2).isImm() || MI.getOperand(1).getReg() != Bedrock::SP)
      return false;
    Offset = MI.getOperand(2).getImm();
    return true;
  case Bedrock::INC32m:
  case Bedrock::DEC32m:
    if (MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
        !MI.getOperand(1).isImm() || MI.getOperand(0).getReg() != Bedrock::SP)
      return false;
    Offset = MI.getOperand(1).getImm();
    return true;
  case Bedrock::ADD32rm:
    if (MI.getNumOperands() < 4 || !MI.getOperand(2).isReg() ||
        !MI.getOperand(3).isImm() || MI.getOperand(2).getReg() != Bedrock::SP)
      return false;
    Offset = MI.getOperand(3).getImm();
    return true;
  }
}

static inline Register findScratchDRegAt(MachineBasicBlock::iterator Insert,
                                  MachineBasicBlock &MBB,
                                  const TargetRegisterInfo &TRI,
                                  Register AvoidA = Register(),
                                  Register AvoidB = Register()) {
  for (Register Reg = Bedrock::D0; Reg <= Bedrock::D7;
       Reg = Register(Reg + 1)) {
    if (regsOverlap(TRI, Reg, AvoidA) || regsOverlap(TRI, Reg, AvoidB))
      continue;
    if (regDeadAfter(Insert, MBB, Reg, TRI))
      return Reg;
  }
  return Register();
}

static inline bool
canPromoteStackSlotToAReg(MachineFunction &MF, int64_t Offset,
                          const SmallVectorImpl<MachineInstr *> &Accesses,
                          const TargetRegisterInfo &TRI) {
  SmallPtrSet<MachineInstr *, 8> Ignored;
  for (MachineInstr *MI : Accesses)
    Ignored.insert(MI);

  if (stackRangeUsedOutsideSet(MF, Offset, 4, Ignored, TRI)) {
    return false;
  }

  for (MachineInstr *MI : Accesses) {
    if (MI->getOpcode() == Bedrock::INC32m ||
        MI->getOpcode() == Bedrock::DEC32m) {
      if (!regDefDeadOrDeadAfterInCFG(MI->getIterator(), *MI->getParent(),
                                      Bedrock::FLAGS, TRI)) {
        return false;
      }
      continue;
    }
    if (MI->getOpcode() != Bedrock::ADD32rm)
      continue;
    Register Dst = MI->getOperand(0).getReg();
    Register LHS = MI->getOperand(1).getReg();
    if (!findScratchDRegAt(MI->getIterator(), *MI->getParent(), TRI, Dst,
                           LHS)) {
      return false;
    }
  }
  return true;
}

static inline unsigned estimateStackSlotPromotionSavings(
    const SmallVectorImpl<MachineInstr *> &Accesses) {
  unsigned Savings = 0;
  for (const MachineInstr *MI : Accesses) {
    switch (MI->getOpcode()) {
    default:
      break;
    case Bedrock::MOV32rm:
    case Bedrock::MOV32mr:
    case Bedrock::INC32m:
    case Bedrock::DEC32m:
      Savings += 2;
      break;
    }
  }
  return Savings;
}

static inline bool collectConsistentPushPopMask(MachineFunction &MF,
                                         MachineInstr *&PushM,
                                         SmallVectorImpl<MachineInstr *> &PopMs,
                                         uint16_t &Mask) {
  PushM = nullptr;
  PopMs.clear();
  Mask = 0;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      if (MI.getOpcode() == Bedrock::PUSHD ||
          MI.getOpcode() == Bedrock::PUSHA ||
          MI.getOpcode() == Bedrock::PUSHM) {
        std::optional<uint16_t> InstrMask = pushPopMaskForInstr(MI);
        if (PushM || !MI.getFlag(MachineInstr::FrameSetup) || !InstrMask)
          return false;
        PushM = &MI;
        Mask = *InstrMask;
        continue;
      }
      if (MI.getOpcode() == Bedrock::POPD || MI.getOpcode() == Bedrock::POPA ||
          MI.getOpcode() == Bedrock::POPM) {
        if (!MI.getFlag(MachineInstr::FrameDestroy) || !pushPopMaskForInstr(MI))
          return false;
        PopMs.push_back(&MI);
      }
    }
  }

  if (!PushM || PopMs.empty() || Mask == 0)
    return false;
  for (MachineInstr *PopM : PopMs) {
    std::optional<uint16_t> PopMask = pushPopMaskForInstr(*PopM);
    if (!PopMask || *PopMask != Mask)
      return false;
  }
  return true;
}

static inline void replaceWithPushPopMask(MachineFunction &MF, MachineInstr &PushM,
                                   ArrayRef<MachineInstr *> PopMs,
                                   uint16_t Mask) {
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());

  MachineBasicBlock &PushBB = *PushM.getParent();
  MachineInstrBuilder Push = buildPushForMask(PushBB, PushM.getIterator(),
                                              PushM.getDebugLoc(), TII, Mask);
  Push.setMIFlags(PushM.getFlags());
  PushM.eraseFromParent();

  for (MachineInstr *PopM : PopMs) {
    MachineBasicBlock &PopBB = *PopM->getParent();
    MachineInstrBuilder Pop = buildPopForMask(PopBB, PopM->getIterator(),
                                              PopM->getDebugLoc(), TII, Mask);
    Pop.setMIFlags(PopM->getFlags());
    PopM->eraseFromParent();
  }
}

static inline void extendPushPopMask(MachineFunction &MF, MachineInstr &PushM,
                              ArrayRef<MachineInstr *> PopMs, uint16_t OldMask,
                              uint16_t AddedMask) {
  uint16_t NewMask = OldMask | AddedMask;
  if (NewMask == OldMask)
    return;
  replaceWithPushPopMask(MF, PushM, PopMs, NewMask);
}

static inline bool
regTouchedOutsideInstrs(const MachineFunction &MF, Register Reg,
                        const SmallPtrSetImpl<const MachineInstr *> &Ignored,
                        const TargetRegisterInfo &TRI) {
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB)
      if (!MI.isDebugInstr() && !Ignored.contains(&MI) &&
          instrTouchesReg(MI, Reg, TRI))
        return true;
  return false;
}

static inline void collectStackConstStores(MachineFunction &MF,
                                    const TargetRegisterInfo &TRI,
                                    SmallVectorImpl<StackConstStore> &Slots) {
  for (MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(); I != MBB.end();) {
      MachineInstr &ImmDef = *I;
      ++I;
      if (ImmDef.isDebugInstr())
        continue;

      Register Reg;
      int64_t Value = 0;
      unsigned ConstSize = 4;
      bool IsConstDef = isMov32Imm(ImmDef, Reg, Value);
      bool IsZeroRegDef = false;
      if (!IsConstDef && isMov64Imm(ImmDef, Reg, Value)) {
        ConstSize = 8;
        IsConstDef = true;
      }
      if (!IsConstDef && ImmDef.getOpcode() == Bedrock::CLR64r &&
          ImmDef.getNumOperands() >= 1 && ImmDef.getOperand(0).isReg()) {
        Reg = ImmDef.getOperand(0).getReg();
        Value = 0;
        ConstSize = 8;
        IsConstDef = true;
        IsZeroRegDef = true;
      }
      if (!IsConstDef)
        continue;

      auto StoreI = nextNonDebug(ImmDef.getIterator(), MBB);
      if (StoreI == MBB.end() || hasOrderedMemOperand(*StoreI))
        continue;
      unsigned StoreSize = memSizeForOpcode(StoreI->getOpcode());
      if (StoreI->getOpcode() != Bedrock::MOV32mr &&
          StoreI->getOpcode() != Bedrock::MOV64mr)
        continue;
      if (!IsZeroRegDef && StoreSize != ConstSize)
        continue;

      Register StoreSrc;
      Register Base;
      int64_t Offset = 0;
      if (!isMemStore(*StoreI, StoreSrc, Base, Offset) || Base != Bedrock::SP ||
          !regsOverlap(TRI, StoreSrc, Reg))
        continue;

      bool Duplicate = false;
      for (StackConstStore &Slot : Slots) {
        if (Slot.Offset != Offset)
          continue;
        Slot.Store = nullptr;
        Duplicate = true;
      }
      if (Duplicate)
        continue;

      StackConstStore Slot;
      Slot.ImmDef = &ImmDef;
      Slot.Store = &*StoreI;
      Slot.MBB = &MBB;
      Slot.Reg = Reg;
      Slot.Offset = Offset;
      Slot.Value = Value;
      Slot.Size = StoreSize;
      Slots.push_back(Slot);
    }
  }
}

static inline bool hasAvailableStackConstStore(ArrayRef<StackConstStore> Slots,
                                        const MachineInstr &Use, int64_t Offset,
                                        unsigned Size) {
  for (const StackConstStore &Slot : Slots) {
    if (!Slot.Store || Slot.Offset != Offset || Slot.Size != Size)
      continue;
    if (stackConstStoreAvailableAtUse(Slot, Use))
      return true;
  }
  return false;
}

static inline bool isReplaceableStackConstMul(const StackConstStore &Slot,
                                       MachineInstr &MI,
                                       StackConstMulReplacement &Replacement,
                                       bool UseImmediateMul,
                                       const TargetRegisterInfo &TRI) {
  if (MI.getOpcode() == Bedrock::MULU32rm) {
    if (!UseImmediateMul || hasOrderedMemOperand(MI) ||
        !stackConstStoreAvailableAtUse(Slot, MI) || MI.getNumOperands() < 4 ||
        !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
        !MI.getOperand(2).isReg() || !MI.getOperand(3).isImm())
      return false;

    Register Product = MI.getOperand(0).getReg();
    Register LHS = MI.getOperand(1).getReg();
    Register Base = MI.getOperand(2).getReg();
    int64_t Offset = MI.getOperand(3).getImm();
    if (!isDReg(Product) || Base != Bedrock::SP || Offset != Slot.Offset ||
        !regsOverlap(TRI, Product, LHS))
      return false;

    Replacement.Load = nullptr;
    Replacement.Mul = &MI;
    Replacement.Product = Product;
    Replacement.Value = Product;
    Replacement.Scratch = Product;
    return true;
  }

  MachineInstr &Load = MI;
  if (Load.getOpcode() != Bedrock::MOV32rm || hasOrderedMemOperand(Load) ||
      !stackConstStoreAvailableAtUse(Slot, Load))
    return false;

  Register ConstReg;
  Register Base;
  int64_t Offset = 0;
  if (!isMemLoad(Load, ConstReg, Base, Offset) || Base != Bedrock::SP ||
      Offset != Slot.Offset)
    return false;

  auto MulI = nextNonDebug(Load.getIterator(), *Load.getParent());
  if (MulI == Load.getParent()->end() || MulI->getOpcode() != Bedrock::MULU32rr)
    return false;

  MachineInstr &Mul = *MulI;
  if (Mul.getNumOperands() < 3 || !Mul.getOperand(0).isReg() ||
      !Mul.getOperand(1).isReg() || !Mul.getOperand(2).isReg())
    return false;

  std::optional<unsigned> Shift = getShiftForSmallConstMul(Slot.Value);
  if (!UseImmediateMul && !Shift)
    return false;

  Register Product = Mul.getOperand(0).getReg();
  Register LHS = Mul.getOperand(1).getReg();
  Register RHS = Mul.getOperand(2).getReg();
  if (!regsOverlap(TRI, Product, LHS))
    return false;

  if (!regDeadAfter(std::next(MulI), *Load.getParent(), Bedrock::FLAGS, TRI))
    return false;

  Register Value;
  Register Scratch;
  if (regsOverlap(TRI, RHS, ConstReg) && !regsOverlap(TRI, Product, ConstReg)) {
    Value = Product;
    Scratch = ConstReg;
    if (!operandIsKill(Mul, ConstReg, TRI) &&
        !regDeadAfter(std::next(MulI), *Load.getParent(), ConstReg, TRI))
      return false;
  } else if (regsOverlap(TRI, Product, ConstReg) &&
             regsOverlap(TRI, LHS, ConstReg) &&
             !regsOverlap(TRI, RHS, ConstReg)) {
    Value = RHS;
    Scratch = Product;
  } else {
    return false;
  }

  if (!isDReg(Product) || !isDReg(Value) || !isDReg(Scratch))
    return false;

  Replacement.Load = &Load;
  Replacement.Mul = &Mul;
  Replacement.Product = Product;
  Replacement.Value = Value;
  Replacement.Scratch = Scratch;
  Replacement.Shift = Shift.value_or(0);
  return true;
}

static inline void addMaybePostMemOperand(MachineInstrBuilder MIB, Register Base,
                                   int64_t Offset, bool PostInc) {
  MIB.addReg(Base);
  if (!PostInc)
    MIB.addImm(Offset);
}

static inline bool isBinUsingLoadedValue(const MachineInstr &MI, Register Loaded,
                                  Register &Result, Register &Src,
                                  const TargetRegisterInfo &TRI) {
  if (getMemDestBinOpcode(MI.getOpcode()) == 0 || MI.getNumOperands() < 3 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
      !MI.getOperand(2).isReg())
    return false;

  Register Dst = MI.getOperand(0).getReg();
  Register LHS = MI.getOperand(1).getReg();
  Register RHS = MI.getOperand(2).getReg();
  if (!regsOverlap(TRI, Dst, LHS))
    return false;

  if (isCommutableBinOpcode(MI.getOpcode()) && regsOverlap(TRI, RHS, Loaded) &&
      !regsOverlap(TRI, Dst, Loaded)) {
    Result = Dst;
    Src = Dst;
    return true;
  }
  if (regsOverlap(TRI, Dst, Loaded) && !regsOverlap(TRI, RHS, Loaded)) {
    Result = Dst;
    Src = RHS;
    return true;
  }
  return false;
}

static inline bool isImmBinUsingLoadedValue(const MachineInstr &MI, Register Loaded,
                                     Register &Result, int64_t &Imm,
                                     const TargetRegisterInfo &TRI) {
  if (getMemDestImmBinOpcode(MI.getOpcode()) == 0 || MI.getNumOperands() < 3 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
      !MI.getOperand(2).isImm())
    return false;

  Register Dst = MI.getOperand(0).getReg();
  Register LHS = MI.getOperand(1).getReg();
  if (!regsOverlap(TRI, Dst, LHS) || !regsOverlap(TRI, Dst, Loaded))
    return false;

  Result = Dst;
  Imm = MI.getOperand(2).getImm();
  return true;
}

static inline unsigned cmpOpcodeForIncDec(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::INC8r:
  case Bedrock::DEC8r:
    return Bedrock::CMP8rr;
  case Bedrock::INC16r:
  case Bedrock::DEC16r:
    return Bedrock::CMP16rr;
  case Bedrock::INC32r:
  case Bedrock::DEC32r:
    return Bedrock::CMP32rr;
  case Bedrock::INC64r:
  case Bedrock::DEC64r:
    return Bedrock::CMP64rr;
  }
}

static inline bool isIncDecRegInstr(const MachineInstr &MI, Register &Reg,
                             unsigned &CmpOpcode,
                             const TargetRegisterInfo &TRI) {
  CmpOpcode = cmpOpcodeForIncDec(MI.getOpcode());
  if (CmpOpcode == 0 || MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg())
    return false;
  if (!regsOverlap(TRI, MI.getOperand(0).getReg(), MI.getOperand(1).getReg()))
    return false;
  Reg = MI.getOperand(0).getReg();
  return true;
}

static inline bool isClrReg(const MachineInstr &MI, Register &Reg) {
  if (MI.getOpcode() != Bedrock::CLR64r || MI.getNumOperands() < 1 ||
      !MI.getOperand(0).isReg())
    return false;
  Reg = MI.getOperand(0).getReg();
  return true;
}

static inline bool isZeroRegDef(const MachineInstr &MI, Register &Reg) {
  if (isClrReg(MI, Reg))
    return true;
  if (MI.getOpcode() != Bedrock::MOV64ri || MI.getNumOperands() < 2 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isImm() ||
      MI.getOperand(1).getImm() != 0)
    return false;
  Reg = MI.getOperand(0).getReg();
  return true;
}

static inline MachineInstr *findRemovableClrDefBefore(MachineBasicBlock &MBB,
                                               MachineBasicBlock::iterator Use,
                                               Register Reg,
                                               const TargetRegisterInfo &TRI) {
  for (auto I = Use; I != MBB.begin();) {
    --I;
    if (I->isDebugInstr())
      continue;

    Register ClrReg;
    if (isClrReg(*I, ClrReg) && regsOverlap(TRI, ClrReg, Reg))
      return &*I;
    if (instrUsesReg(*I, Reg, TRI) || instrDefinesReg(*I, Reg, TRI))
      return nullptr;
  }
  return nullptr;
}

static inline MachineInstr *findRemovableZeroDefBefore(MachineBasicBlock &MBB,
                                                MachineBasicBlock::iterator Use,
                                                Register Reg,
                                                const TargetRegisterInfo &TRI) {
  for (auto I = Use; I != MBB.begin();) {
    --I;
    if (I->isDebugInstr())
      continue;

    Register ZeroReg;
    if (isZeroRegDef(*I, ZeroReg) && regsOverlap(TRI, ZeroReg, Reg))
      return &*I;
    if (instrUsesReg(*I, Reg, TRI) || instrDefinesReg(*I, Reg, TRI))
      return nullptr;
  }
  return nullptr;
}

static inline bool isPlainRetBlock(const MachineBasicBlock &MBB) {
  bool SawRet = false;
  for (const MachineInstr &MI : MBB) {
    if (MI.isDebugInstr())
      continue;
    if (SawRet || MI.getOpcode() != Bedrock::RET)
      return false;
    SawRet = true;
  }
  return SawRet;
}

static inline MachineBasicBlock *createZeroReturnBlock(MachineFunction &MF,
                                                MachineBasicBlock &InsertAfter,
                                                const DebugLoc &DL,
                                                const BedrockInstrInfo &TII) {
  MachineBasicBlock *Zero =
      MF.CreateMachineBasicBlock(InsertAfter.getBasicBlock());
  MF.insert(std::next(InsertAfter.getIterator()), Zero);
  BuildMI(*Zero, Zero->end(), DL, TII.get(Bedrock::CLR64r), Bedrock::D0);
  BuildMI(*Zero, Zero->end(), DL, TII.get(Bedrock::RET));
  return Zero;
}

static inline std::optional<int64_t> getCondCodeImm(const MachineOperand &MO) {
  if (MO.isImm())
    return MO.getImm();
  if (MO.isCImm())
    return MO.getCImm()->getSExtValue();
  return std::nullopt;
}

static inline void setCondCodeImm(MachineOperand &MO, int64_t CC) {
  if (MO.isImm())
    MO.setImm(CC);
  else
    MO.ChangeToImmediate(CC);
}

static inline std::optional<int64_t> invertCondCode(int64_t CC) {
  switch (CC) {
  default:
    return std::nullopt;
  case BedrockCC::T:
    return BedrockCC::F;
  case BedrockCC::F:
    return BedrockCC::T;
  case BedrockCC::EQ:
    return BedrockCC::NE;
  case BedrockCC::NE:
    return BedrockCC::EQ;
  case BedrockCC::ULT:
    return BedrockCC::UGE;
  case BedrockCC::UGE:
    return BedrockCC::ULT;
  case BedrockCC::MI:
    return BedrockCC::PL;
  case BedrockCC::PL:
    return BedrockCC::MI;
  case BedrockCC::VS:
    return BedrockCC::VC;
  case BedrockCC::VC:
    return BedrockCC::VS;
  case BedrockCC::ULE:
    return BedrockCC::UGT;
  case BedrockCC::UGT:
    return BedrockCC::ULE;
  case BedrockCC::LT:
    return BedrockCC::GE;
  case BedrockCC::GE:
    return BedrockCC::LT;
  case BedrockCC::LE:
    return BedrockCC::GT;
  case BedrockCC::GT:
    return BedrockCC::LE;
  }
}

static inline bool isEqNeBranch(const MachineInstr &MI) {
  if (MI.getOpcode() != Bedrock::JCC || MI.getNumOperands() < 2)
    return false;
  std::optional<int64_t> CC = getCondCodeImm(MI.getOperand(1));
  if (!CC)
    return false;
  return *CC == BedrockCC::EQ || *CC == BedrockCC::NE;
}

static inline unsigned getCondOperandNo(unsigned Opcode) {
  switch (Opcode) {
  default:
    return std::numeric_limits<unsigned>::max();
  case Bedrock::JCC:
    return 1;
  case Bedrock::MOVCC8rr:
  case Bedrock::MOVCC16rr:
  case Bedrock::MOVCC32rr:
  case Bedrock::MOVCC64rr:
  case Bedrock::MOVCC8rm:
  case Bedrock::MOVCC16rm:
  case Bedrock::MOVCC32rm:
  case Bedrock::MOVCC64rm:
  case Bedrock::MOVCC8mr:
  case Bedrock::MOVCC16mr:
  case Bedrock::MOVCC32mr:
  case Bedrock::MOVCC64mr:
    return 3;
  }
}

static inline MachineBasicBlock::iterator firstNonDebug(MachineBasicBlock &MBB) {
  auto I = MBB.begin();
  while (I != MBB.end() && I->isDebugInstr())
    ++I;
  return I;
}

static inline MachineOperand *getCondOperand(MachineInstr &MI) {
  unsigned OpNo = getCondOperandNo(MI.getOpcode());
  if (OpNo >= MI.getNumOperands())
    return nullptr;
  return &MI.getOperand(OpNo);
}

static inline std::optional<int64_t> mapZeroCmpCondToTest(int64_t CC, bool ZeroIsLHS) {
  switch (CC) {
  default:
    return std::nullopt;
  case BedrockCC::EQ:
  case BedrockCC::NE:
    return CC;
  case BedrockCC::LT:
    return ZeroIsLHS ? BedrockCC::GT : BedrockCC::LT;
  case BedrockCC::GE:
    return ZeroIsLHS ? BedrockCC::LE : BedrockCC::GE;
  case BedrockCC::LE:
    return ZeroIsLHS ? BedrockCC::GE : BedrockCC::LE;
  case BedrockCC::GT:
    return ZeroIsLHS ? BedrockCC::LT : BedrockCC::GT;
  }
}

static inline unsigned getMinMaxOpcodeForCmp(unsigned CmpOpcode, int64_t CC,
                                      bool TrueSelectsLHS) {
  bool IsSigned = false;
  bool IsMin = false;
  switch (CC) {
  default:
    return 0;
  case BedrockCC::LT:
  case BedrockCC::LE:
    IsSigned = true;
    IsMin = TrueSelectsLHS;
    break;
  case BedrockCC::GT:
  case BedrockCC::GE:
    IsSigned = true;
    IsMin = !TrueSelectsLHS;
    break;
  case BedrockCC::ULT:
  case BedrockCC::ULE:
    IsSigned = false;
    IsMin = TrueSelectsLHS;
    break;
  case BedrockCC::UGT:
  case BedrockCC::UGE:
    IsSigned = false;
    IsMin = !TrueSelectsLHS;
    break;
  }

  switch (CmpOpcode) {
  default:
    return 0;
  case Bedrock::CMP8rr:
    return IsSigned ? (IsMin ? Bedrock::MINS8rr : Bedrock::MAXS8rr)
                    : (IsMin ? Bedrock::MINU8rr : Bedrock::MAXU8rr);
  case Bedrock::CMP16rr:
    return IsSigned ? (IsMin ? Bedrock::MINS16rr : Bedrock::MAXS16rr)
                    : (IsMin ? Bedrock::MINU16rr : Bedrock::MAXU16rr);
  case Bedrock::CMP32rr:
    return IsSigned ? (IsMin ? Bedrock::MINS32rr : Bedrock::MAXS32rr)
                    : (IsMin ? Bedrock::MINU32rr : Bedrock::MAXU32rr);
  case Bedrock::CMP64rr:
    return IsSigned ? (IsMin ? Bedrock::MINS64rr : Bedrock::MAXS64rr)
                    : (IsMin ? Bedrock::MINU64rr : Bedrock::MAXU64rr);
  }
}

static inline bool minMaxOpcodeCanUseRegs(unsigned Opcode, Register Dst,
                                   Register Src) {
  switch (Opcode) {
  default:
    return false;
  case Bedrock::MINS32rr:
  case Bedrock::MAXS32rr:
  case Bedrock::MINU32rr:
  case Bedrock::MAXU32rr:
    return isDReg(Dst) && isIntReg(Src);
  case Bedrock::MINS8rr:
  case Bedrock::MAXS8rr:
  case Bedrock::MINU8rr:
  case Bedrock::MAXU8rr:
  case Bedrock::MINS16rr:
  case Bedrock::MAXS16rr:
  case Bedrock::MINU16rr:
  case Bedrock::MAXU16rr:
  case Bedrock::MINS64rr:
  case Bedrock::MAXS64rr:
  case Bedrock::MINU64rr:
  case Bedrock::MAXU64rr:
    return isDReg(Dst) && isDReg(Src);
  }
}

static inline unsigned getDJccOpcodeForDec(unsigned DecOpcode) {
  switch (DecOpcode) {
  default:
    return 0;
  case Bedrock::DEC8r:
    return Bedrock::DJCC8r;
  case Bedrock::DEC16r:
    return Bedrock::DJCC16r;
  case Bedrock::DEC32r:
    return Bedrock::DJCC32r;
  case Bedrock::DEC64r:
    return Bedrock::DJCC64r;
  }
}

static inline unsigned getIJccOpcodeForIncCmp(unsigned IncOpcode, unsigned CmpOpcode) {
  switch (IncOpcode) {
  default:
    return 0;
  case Bedrock::INC32r:
    return CmpOpcode == Bedrock::CMP32rr ? Bedrock::IJCC32r : 0;
  case Bedrock::INC64r:
    return CmpOpcode == Bedrock::CMP64rr ? Bedrock::IJCC64r : 0;
  }
}

static inline bool blockHasLiveInReg(const MachineBasicBlock &MBB, Register Reg,
                              const TargetRegisterInfo &TRI);

static inline std::optional<int64_t> getConstDefForReg(const MachineInstr &MI,
                                                Register Reg,
                                                const TargetRegisterInfo &TRI) {
  if (!instrDefinesReg(MI, Reg, TRI) || MI.getNumOperands() < 1 ||
      !MI.getOperand(0).isReg() ||
      !regsOverlap(TRI, MI.getOperand(0).getReg(), Reg))
    return std::nullopt;
  if (MI.getOpcode() == Bedrock::CLR64r)
    return 0;
  if ((MI.getOpcode() == Bedrock::MOV32ri ||
       MI.getOpcode() == Bedrock::MOV64ri) &&
      MI.getNumOperands() >= 2 && MI.getOperand(1).isImm())
    return MI.getOperand(1).getImm();
  return std::nullopt;
}

static inline MachineInstr *findLastConstDefBefore(MachineInstr &Use, Register Reg,
                                            int64_t Value,
                                            const TargetRegisterInfo &TRI) {
  MachineFunction &MF = *Use.getMF();
  MachineInstr *LastDef = nullptr;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (&MI == &Use)
        return LastDef;
      if (MI.isDebugInstr())
        continue;
      if (!instrDefinesReg(MI, Reg, TRI))
        continue;
      std::optional<int64_t> Const = getConstDefForReg(MI, Reg, TRI);
      LastDef = Const && *Const == Value ? &MI : nullptr;
    }
  }
  return nullptr;
}

static inline MachineInstr *findLastConstDefBeforeAny(MachineInstr &Use, Register Reg,
                                               int64_t &Value,
                                               const TargetRegisterInfo &TRI) {
  MachineFunction &MF = *Use.getMF();
  MachineInstr *LastDef = nullptr;
  std::optional<int64_t> LastValue;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (&MI == &Use) {
        if (!LastDef || !LastValue)
          return nullptr;
        Value = *LastValue;
        return LastDef;
      }
      if (MI.isDebugInstr())
        continue;
      if (!instrDefinesReg(MI, Reg, TRI))
        continue;
      LastValue = getConstDefForReg(MI, Reg, TRI);
      LastDef = LastValue ? &MI : nullptr;
    }
  }
  return nullptr;
}

static inline void removeRegLiveInsWithoutUses(MachineFunction &MF, Register Reg,
                                        const TargetRegisterInfo &TRI) {
  for (MachineBasicBlock &MBB : MF) {
    if (!MBB.isLiveIn(Reg))
      continue;
    bool UsesReg = false;
    for (MachineInstr &MI : MBB) {
      if (!MI.isDebugInstr() && instrUsesReg(MI, Reg, TRI)) {
        UsesReg = true;
        break;
      }
    }
    if (!UsesReg)
      MBB.removeLiveIn(Reg);
  }
}

static inline bool regUnusedAfterInstrInLayout(MachineInstr &Def, Register Reg,
                                        const TargetRegisterInfo &TRI) {
  MachineFunction &MF = *Def.getMF();
  bool PastDef = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (!PastDef) {
        if (&MI == &Def)
          PastDef = true;
        continue;
      }
      if (MI.isDebugInstr())
        continue;
      if (instrUsesReg(MI, Reg, TRI))
        return false;
      if (instrDefinesReg(MI, Reg, TRI))
        return true;
    }
  }
  return true;
}

static inline bool replaceZeroRegCopiesAfterDef(MachineInstr &ZeroDef, Register Reg,
                                         MachineFunction &MF,
                                         const BedrockInstrInfo &TII,
                                         const TargetRegisterInfo &TRI) {
  bool Changed = false;
  MachineBasicBlock &MBB = *ZeroDef.getParent();
  for (auto I = std::next(ZeroDef.getIterator()); I != MBB.end();) {
    MachineInstr &MI = *I++;
    if (MI.isDebugInstr())
      continue;
    if (instrDefinesReg(MI, Reg, TRI))
      return Changed;
    if (!instrUsesReg(MI, Reg, TRI))
      continue;
    if ((MI.getOpcode() != Bedrock::MOV32rr &&
         MI.getOpcode() != Bedrock::MOV64rr) ||
        MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
        !MI.getOperand(1).isReg() ||
        !regsOverlap(TRI, MI.getOperand(1).getReg(), Reg))
      return Changed;

    Register Dst = MI.getOperand(0).getReg();
    if (!isIntReg(Dst) || regsOverlap(TRI, Dst, Reg))
      return Changed;

    BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(), TII.get(Bedrock::CLR64r),
            Dst);
    MI.eraseFromParent();
    Changed = true;
  }
  return Changed;
}

static inline MachineInstr *findLastDefBefore(MachineInstr &Use, Register Reg,
                                       const TargetRegisterInfo &TRI) {
  MachineFunction &MF = *Use.getMF();
  MachineInstr *LastDef = nullptr;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (&MI == &Use)
        return LastDef;
      if (!MI.isDebugInstr() && instrDefinesReg(MI, Reg, TRI))
        LastDef = &MI;
    }
  }
  return nullptr;
}

static inline bool onlyUsesRegAsI32Before(MachineInstr &Def, MachineInstr &Limit,
                                   Register Reg,
                                   const TargetRegisterInfo &TRI) {
  MachineFunction &MF = *Def.getMF();
  bool PastDef = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (!PastDef) {
        if (&MI == &Def)
          PastDef = true;
        continue;
      }
      if (&MI == &Limit)
        return true;
      if (MI.isDebugInstr())
        continue;
      if (instrDefinesReg(MI, Reg, TRI))
        return false;
      if (!instrUsesReg(MI, Reg, TRI))
        continue;

      bool Allowed = false;
      if (MI.getOpcode() == Bedrock::LEA4L && MI.getNumOperands() >= 3 &&
          MI.getOperand(2).isReg() &&
          regsOverlap(TRI, MI.getOperand(2).getReg(), Reg))
        Allowed = true;
      if (!Allowed)
        return false;
    }
  }
  return false;
}

static inline bool instrOnlyUsesRegAsI32Count(const MachineInstr &MI, Register Reg,
                                       const TargetRegisterInfo &TRI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::TEST32rr:
    return MI.getNumOperands() >= 2 && MI.getOperand(0).isReg() &&
           MI.getOperand(1).isReg() &&
           regsOverlap(TRI, MI.getOperand(0).getReg(), Reg) &&
           regsOverlap(TRI, MI.getOperand(1).getReg(), Reg);
  case Bedrock::DEC32r:
    return MI.getNumOperands() >= 2 && MI.getOperand(0).isReg() &&
           MI.getOperand(1).isReg() &&
           regsOverlap(TRI, MI.getOperand(0).getReg(), Reg) &&
           regsOverlap(TRI, MI.getOperand(1).getReg(), Reg);
  case Bedrock::CMP32rr:
    return MI.getNumOperands() >= 2 && MI.getOperand(0).isReg() &&
           MI.getOperand(1).isReg();
  }
}

static inline bool onlyUsedAsI32LoopCountAfter(MachineInstr &Def, Register Reg,
                                        const TargetRegisterInfo &TRI) {
  MachineFunction &MF = *Def.getMF();
  bool PastDef = false;
  bool SawUse = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (!PastDef) {
        if (&MI == &Def)
          PastDef = true;
        continue;
      }
      if (MI.isDebugInstr())
        continue;
      if (!instrTouchesReg(MI, Reg, TRI))
        continue;
      if (!instrOnlyUsesRegAsI32Count(MI, Reg, TRI))
        return false;
      SawUse = true;
    }
  }
  return SawUse;
}

static constexpr uint32_t AllGPRZeroMask = (1u << 16) - 1u;

static inline void clearKnownZeroReg(uint32_t &Mask, Register Reg,
                              const TargetRegisterInfo &TRI) {
  if (!Reg.isValid())
    return;
  for (unsigned Bit = 0; Bit != 16; ++Bit) {
    if (!(Mask & (1u << Bit)))
      continue;
    if (regsOverlap(TRI, Reg, getRegForMaskBit(Bit)))
      Mask &= ~(1u << Bit);
  }
}

static inline bool maskHasKnownZeroReg(uint32_t Mask, Register Reg,
                                const TargetRegisterInfo &TRI) {
  for (unsigned Bit = 0; Bit != 16; ++Bit)
    if ((Mask & (1u << Bit)) && regsOverlap(TRI, Reg, getRegForMaskBit(Bit)))
      return true;
  return false;
}

static inline bool isKnownZeroCmpForReg(const MachineInstr &MI, Register Reg,
                                 uint32_t KnownZero, unsigned CmpOpcode,
                                 const TargetRegisterInfo &TRI) {
  if (MI.getOpcode() != CmpOpcode || MI.getNumOperands() < 2 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg())
    return false;
  Register LHS = MI.getOperand(0).getReg();
  Register RHS = MI.getOperand(1).getReg();
  return (regsOverlap(TRI, LHS, Reg) &&
          maskHasKnownZeroReg(KnownZero, RHS, TRI)) ||
         (maskHasKnownZeroReg(KnownZero, LHS, TRI) &&
          regsOverlap(TRI, RHS, Reg));
}

static inline bool isIntCmpOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
  case Bedrock::CMP8rr:
  case Bedrock::CMP16rr:
  case Bedrock::CMP32rr:
  case Bedrock::CMP64rr:
    return true;
  }
}

static inline unsigned getTestOpcodeForCmp(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::CMP8rr:
    return Bedrock::TEST8rr;
  case Bedrock::CMP16rr:
    return Bedrock::TEST16rr;
  case Bedrock::CMP32rr:
    return Bedrock::TEST32rr;
  case Bedrock::CMP64rr:
    return Bedrock::TEST64rr;
  }
}

static inline Register preferredKnownZeroReg(uint32_t KnownZero,
                                      const MachineBasicBlock &MBB,
                                      Register Current,
                                      const TargetRegisterInfo &TRI,
                                      bool AllowARegs) {
  auto TryReg = [&](Register Candidate) -> Register {
    if (!maskHasKnownZeroReg(KnownZero, Candidate, TRI))
      return Register();
    if (regsOverlap(TRI, Candidate, Current) || MBB.isLiveIn(Candidate))
      return Candidate;
    return Register();
  };

  for (Register Reg = Bedrock::D0; Reg <= Bedrock::D7; Reg = Register(Reg + 1))
    if (Register Candidate = TryReg(Reg))
      return Candidate;

  if (AllowARegs)
    for (Register Reg = Bedrock::A0; Reg <= Bedrock::A7;
         Reg = Register(Reg + 1))
      if (Register Candidate = TryReg(Reg))
        return Candidate;

  return Current;
}

static inline void transferKnownZero(const MachineInstr &MI, uint32_t &Mask,
                              const TargetRegisterInfo &TRI) {
  if (MI.isDebugInstr())
    return;

  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isRegMask())
      continue;
    for (unsigned Bit = 0; Bit != 16; ++Bit) {
      if (!(Mask & (1u << Bit)))
        continue;
      if (MO.clobbersPhysReg(getRegForMaskBit(Bit).asMCReg()))
        Mask &= ~(1u << Bit);
    }
  }

  for (const MachineOperand &MO : MI.operands())
    if (MO.isReg() && MO.isDef())
      clearKnownZeroReg(Mask, MO.getReg(), TRI);

  Register ClrReg;
  if (isClrReg(MI, ClrReg)) {
    if (std::optional<unsigned> Bit = getMaskBit(ClrReg))
      Mask |= 1u << *Bit;
  }
}

static inline DenseMap<MachineBasicBlock *, uint32_t>
computeKnownZeroIns(MachineFunction &MF, const TargetRegisterInfo &TRI) {
  DenseMap<MachineBasicBlock *, uint32_t> In;
  DenseMap<MachineBasicBlock *, uint32_t> Out;

  for (MachineBasicBlock &MBB : MF) {
    In[&MBB] = &MBB == &MF.front() ? 0 : AllGPRZeroMask;
    Out[&MBB] = &MBB == &MF.front() ? 0 : AllGPRZeroMask;
  }

  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (MachineBasicBlock &MBB : MF) {
      uint32_t NewIn = 0;
      if (&MBB == &MF.front()) {
        NewIn = 0;
      } else if (MBB.pred_empty()) {
        NewIn = 0;
      } else {
        NewIn = AllGPRZeroMask;
        for (MachineBasicBlock *Pred : MBB.predecessors())
          NewIn &= Out.lookup(Pred);
      }

      uint32_t NewOut = NewIn;
      for (const MachineInstr &MI : MBB)
        transferKnownZero(MI, NewOut, TRI);

      if (In[&MBB] != NewIn || Out[&MBB] != NewOut) {
        In[&MBB] = NewIn;
        Out[&MBB] = NewOut;
        Changed = true;
      }
    }
  }

  return In;
}

static inline bool isIdentityMove(const MachineInstr &MI,
                           const TargetRegisterInfo &TRI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::MOV8rr:
  case Bedrock::MOV16rr:
  case Bedrock::MOV32rr:
  case Bedrock::MOV64rr:
  case Bedrock::FMOV32rr:
  case Bedrock::FMOV64rr:
  case Bedrock::TRUNC64to32:
  case Bedrock::TRUNC64to16:
  case Bedrock::TRUNC64to8:
  case Bedrock::TRUNC32to16:
  case Bedrock::TRUNC32to8:
  case Bedrock::TRUNC16to8:
    break;
  }
  return MI.getNumOperands() >= 2 && MI.getOperand(0).isReg() &&
         MI.getOperand(1).isReg() &&
         regsOverlap(TRI, MI.getOperand(0).getReg(), MI.getOperand(1).getReg());
}

namespace {
struct TruncAnd32Arm {
  MachineInstr *Trunc = nullptr;
  MachineInstr *And = nullptr;
  Register Src;
  Register Tmp;
  int64_t Imm = 0;
};
} // end anonymous namespace

static inline bool matchTruncAnd32(MachineInstr &Trunc, MachineBasicBlock &MBB,
                            TruncAnd32Arm &Arm, const TargetRegisterInfo &TRI) {
  if (Trunc.getOpcode() != Bedrock::TRUNC64to32 || Trunc.getNumOperands() < 2 ||
      !Trunc.getOperand(0).isReg() || !Trunc.getOperand(1).isReg())
    return false;

  auto AndI = nextNonDebug(Trunc.getIterator(), MBB);
  if (AndI == MBB.end() || AndI->getOpcode() != Bedrock::AND32ri ||
      AndI->getNumOperands() < 3 || !AndI->getOperand(0).isReg() ||
      !AndI->getOperand(1).isReg() || !AndI->getOperand(2).isImm())
    return false;

  Register Tmp = Trunc.getOperand(0).getReg();
  if (!isDReg(Tmp) || !isDReg(Trunc.getOperand(1).getReg()) ||
      !regsOverlap(TRI, AndI->getOperand(0).getReg(), Tmp) ||
      !regsOverlap(TRI, AndI->getOperand(1).getReg(), Tmp))
    return false;

  Arm.Trunc = &Trunc;
  Arm.And = &*AndI;
  Arm.Src = Trunc.getOperand(1).getReg();
  Arm.Tmp = Tmp;
  Arm.Imm = AndI->getOperand(2).getImm();
  return true;
}

namespace {
struct EqImmCompareBlock {
  MachineBasicBlock *MBB = nullptr;
  MachineInstr *Cmp = nullptr;
  MachineInstr *Branch = nullptr;
  MachineBasicBlock *CaseTarget = nullptr;
  MachineBasicBlock *Fallthrough = nullptr;
  Register Reg;
  int64_t Imm = 0;
};
} // end anonymous namespace

static inline bool matchEqImmCompareBlock(MachineBasicBlock &MBB,
                                   EqImmCompareBlock &Out) {
  MachineBasicBlock::iterator BranchI = MBB.getLastNonDebugInstr();
  if (BranchI == MBB.end() || BranchI->getOpcode() != Bedrock::JCC ||
      BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB())
    return false;

  std::optional<int64_t> CC = getCondCodeImm(BranchI->getOperand(1));
  if (!CC || *CC != BedrockCC::EQ)
    return false;

  MachineBasicBlock::iterator CmpI = prevNonDebug(BranchI, MBB);
  if (CmpI == MBB.end() || CmpI->getOpcode() != Bedrock::CMP32ri ||
      CmpI->getNumOperands() < 2 || !CmpI->getOperand(0).isReg() ||
      !CmpI->getOperand(1).isImm())
    return false;

  MachineFunction::iterator Next = std::next(MBB.getIterator());
  if (Next == MBB.getParent()->end())
    return false;

  MachineBasicBlock *Fallthrough = &*Next;
  MachineBasicBlock *CaseTarget = BranchI->getOperand(0).getMBB();
  if (!CaseTarget || CaseTarget == Fallthrough || !MBB.isSuccessor(Fallthrough))
    return false;

  Register Reg = CmpI->getOperand(0).getReg();
  int64_t Imm = CmpI->getOperand(1).getImm();
  Out.MBB = &MBB;
  Out.Cmp = &*CmpI;
  Out.Branch = &*BranchI;
  Out.CaseTarget = CaseTarget;
  Out.Fallthrough = Fallthrough;
  Out.Reg = Reg;
  Out.Imm = Imm;
  return true;
}

static inline unsigned getMemImmCmpOpcodeForMemRegCmp(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::CMP8rm:
  case Bedrock::CMP8mr:
    return Bedrock::CMP8mi;
  case Bedrock::CMP16rm:
  case Bedrock::CMP16mr:
    return Bedrock::CMP16mi;
  case Bedrock::CMP32rm:
  case Bedrock::CMP32mr:
    return Bedrock::CMP32mi;
  case Bedrock::CMP64rm:
  case Bedrock::CMP64mr:
    return Bedrock::CMP64mi;
  }
}

static inline bool isReturnValueReg(Register Reg, const TargetRegisterInfo &TRI) {
  return regsOverlap(TRI, Reg, Bedrock::D0) ||
         regsOverlap(TRI, Reg, Bedrock::D1) ||
         regsOverlap(TRI, Reg, Bedrock::A0) ||
         regsOverlap(TRI, Reg, Bedrock::F0);
}

static inline bool isPlainDeadDefCandidate(const MachineInstr &MI, Register &Reg) {
  if (MI.getDesc().mayLoad() || MI.getDesc().mayStore() ||
      MI.getDesc().hasUnmodeledSideEffects() || MI.isTerminator() ||
      MI.isCall() || MI.isReturn() || MI.getNumExplicitDefs() != 1 ||
      !MI.implicit_operands().empty())
    return false;

  const MachineOperand &Def = MI.getOperand(0);
  if (!Def.isReg() || !Def.isDef())
    return false;

  switch (MI.getOpcode()) {
  default:
    return false;
  case Bedrock::CLR64r:
  case Bedrock::MOV8rr:
  case Bedrock::MOV16rr:
  case Bedrock::MOV32rr:
  case Bedrock::MOV64rr:
  case Bedrock::MOV8ri:
  case Bedrock::MOV16ri:
  case Bedrock::MOV32ri:
  case Bedrock::MOV64ri:
  case Bedrock::LEAri:
  case Bedrock::LEA4:
  case Bedrock::LEA4L:
    Reg = Def.getReg();
    return Reg != Bedrock::SP;
  }
}

static inline bool findCountedLoopHeaderInfo(MachineBasicBlock &Header,
                                      uint32_t HeaderKnownZero,
                                      Register &CountReg,
                                      const TargetRegisterInfo &TRI);

static inline bool isUnaryRegOp(const MachineInstr &MI, unsigned Opcode, Register Reg,
                         const TargetRegisterInfo &TRI);

namespace {
struct ExitCopyInfo {
  MachineBasicBlock *MBB = nullptr;
  MachineInstr *Branch = nullptr;
  MachineInstr *Copy = nullptr;
  Register Source;
};
} // end anonymous namespace

static inline MachineInstr *findJccTo(MachineBasicBlock &MBB,
                               MachineBasicBlock &Target) {
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end() || I->getOpcode() != Bedrock::JCC ||
      I->getNumOperands() < 2 || !I->getOperand(0).isMBB() ||
      I->getOperand(0).getMBB() != &Target)
    return nullptr;
  return &*I;
}

static inline MachineInstr *findCopyToBefore(MachineInstr &Before, Register Dst,
                                      Register &Src,
                                      const TargetRegisterInfo &TRI) {
  MachineBasicBlock &MBB = *Before.getParent();
  MachineBasicBlock::iterator CopyI = prevNonDebug(Before.getIterator(), MBB);
  if (CopyI == MBB.end() || CopyI->getOpcode() != Bedrock::MOV64rr ||
      CopyI->getNumOperands() < 2 || !CopyI->getOperand(0).isReg() ||
      !CopyI->getOperand(1).isReg() ||
      !regsOverlap(TRI, CopyI->getOperand(0).getReg(), Dst))
    return nullptr;
  Src = CopyI->getOperand(1).getReg();
  return &*CopyI;
}

static inline bool findCountedLoopHeaderInfo(MachineBasicBlock &Header,
                                      uint32_t HeaderKnownZero,
                                      Register &CountReg,
                                      const TargetRegisterInfo &TRI) {
  for (MachineInstr &MI : Header) {
    if (MI.isDebugInstr())
      continue;
    if (MI.getOpcode() != Bedrock::CMP64rr || MI.getNumOperands() < 2 ||
        !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg())
      return false;

    Register A = MI.getOperand(0).getReg();
    Register B = MI.getOperand(1).getReg();
    bool AZero = maskHasKnownZeroReg(HeaderKnownZero, A, TRI);
    bool BZero = maskHasKnownZeroReg(HeaderKnownZero, B, TRI);
    if (AZero == BZero)
      return false;
    CountReg = AZero ? B : A;
    return true;
  }
  return false;
}

static inline bool isUnaryRegOp(const MachineInstr &MI, unsigned Opcode, Register Reg,
                         const TargetRegisterInfo &TRI) {
  return MI.getOpcode() == Opcode && MI.getNumOperands() >= 2 &&
         MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
         regsOverlap(TRI, MI.getOperand(0).getReg(), Reg) &&
         regsOverlap(TRI, MI.getOperand(1).getReg(), Reg);
}

static inline bool flagsDeadOrUsedByEqNeBranch(MachineInstr &MI,
                                        const TargetRegisterInfo &TRI) {
  MachineBasicBlock &MBB = *MI.getParent();
  auto Next = nextNonDebug(MI.getIterator(), MBB);
  if (regDeadAfterInCFG(Next, MBB, Bedrock::FLAGS, TRI))
    return true;
  return Next != MBB.end() && isEqNeBranch(*Next);
}

static inline std::optional<unsigned>
getRepeatCounterUseOperand(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return std::nullopt;
  case Bedrock::REPMOV32mmpostboth:
  case Bedrock::REPMOV64mmpostboth:
  case Bedrock::REPNEMOV32mmpostboth:
  case Bedrock::REPMOV32postmr:
  case Bedrock::REPGTCMP32postrm:
    return 1;
  case Bedrock::REPNEMOV32postrm:
  case Bedrock::REPADD32postrm:
    return 2;
  }
}

static inline bool isRepeatCountUse(const MachineInstr &MI, Register Count,
                             const TargetRegisterInfo &TRI) {
  std::optional<unsigned> CountUse = getRepeatCounterUseOperand(MI);
  if (!CountUse || MI.getNumOperands() <= *CountUse ||
      !MI.getOperand(0).isReg() || !MI.getOperand(*CountUse).isReg() ||
      !regsOverlap(TRI, MI.getOperand(0).getReg(), Count) ||
      !regsOverlap(TRI, MI.getOperand(*CountUse).getReg(), Count))
    return false;

  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    if (I == 0 || I == *CountUse)
      continue;
    if (operandTouchesReg(MI.getOperand(I), Count, TRI))
      return false;
  }
  return true;
}

static inline bool blockHasLiveInReg(const MachineBasicBlock &MBB, Register Reg,
                              const TargetRegisterInfo &TRI) {
  for (const MachineBasicBlock::RegisterMaskPair &LiveIn : MBB.liveins())
    if (regsOverlap(TRI, Register(LiveIn.PhysReg), Reg))
      return true;
  return false;
}

static inline bool
collectSelfZextI32CountUses(MachineInstr &Ext, Register Count,
                            const TargetRegisterInfo &TRI,
                            SmallVectorImpl<MachineInstr *> &Tests,
                            SmallVectorImpl<MachineInstr *> &Decs,
                            SmallVectorImpl<MachineInstr *> &DJccs) {
  if (!regDeadAfterInCFG(std::next(Ext.getIterator()), *Ext.getParent(),
                         Bedrock::FLAGS, TRI))
    return false;

  SmallVector<std::pair<MachineBasicBlock *, MachineBasicBlock::iterator>, 8>
      Worklist;
  Worklist.push_back({Ext.getParent(), std::next(Ext.getIterator())});
  SmallPtrSet<MachineBasicBlock *, 16> Visited;
  bool SawUse = false;

  while (!Worklist.empty()) {
    auto [MBB, Start] = Worklist.pop_back_val();
    if (!Visited.insert(MBB).second)
      continue;

    bool PathEnded = false;
    for (auto I = Start; I != MBB->end(); ++I) {
      MachineInstr &MI = *I;
      if (MI.isDebugInstr())
        continue;

      bool HasRegMask = instrHasRegMaskForReg(MI, Count, TRI);
      if (!HasRegMask && !instrTouchesReg(MI, Count, TRI))
        continue;

      if (MI.getOpcode() == Bedrock::TEST64rr &&
          isUnaryRegOp(MI, Bedrock::TEST64rr, Count, TRI)) {
        if (!flagsDeadOrUsedByEqNeBranch(MI, TRI))
          return false;
        Tests.push_back(&MI);
        SawUse = true;
        continue;
      }
      if (MI.getOpcode() == Bedrock::TEST32rr &&
          isUnaryRegOp(MI, Bedrock::TEST32rr, Count, TRI)) {
        SawUse = true;
        continue;
      }
      if (MI.getOpcode() == Bedrock::DEC64r &&
          isUnaryRegOp(MI, Bedrock::DEC64r, Count, TRI)) {
        if (!flagsDeadOrUsedByEqNeBranch(MI, TRI))
          return false;
        Decs.push_back(&MI);
        SawUse = true;
        continue;
      }
      if (MI.getOpcode() == Bedrock::DEC32r &&
          isUnaryRegOp(MI, Bedrock::DEC32r, Count, TRI)) {
        SawUse = true;
        continue;
      }
      if (MI.getOpcode() == Bedrock::DJCC64r &&
          isUnaryRegOp(MI, Bedrock::DJCC64r, Count, TRI)) {
        DJccs.push_back(&MI);
        SawUse = true;
        continue;
      }
      if (MI.getOpcode() == Bedrock::DJCC32r &&
          isUnaryRegOp(MI, Bedrock::DJCC32r, Count, TRI)) {
        SawUse = true;
        continue;
      }
      if (isRepeatCountUse(MI, Count, TRI)) {
        SawUse = true;
        PathEnded = true;
        break;
      }

      if (instrUsesReg(MI, Count, TRI))
        return false;
      if (HasRegMask || instrDefinesReg(MI, Count, TRI)) {
        PathEnded = true;
        break;
      }
      return false;
    }

    if (PathEnded)
      continue;
    for (MachineBasicBlock *Succ : MBB->successors())
      if (blockHasLiveInReg(*Succ, Count, TRI))
        Worklist.push_back({Succ, Succ->begin()});
  }

  return SawUse;
}

static inline bool
collectLoopBlocksUntilExit(MachineBasicBlock &Header, MachineBasicBlock &Exit,
                           SmallPtrSetImpl<MachineBasicBlock *> &LoopBlocks) {
  SmallVector<MachineBasicBlock *, 8> Worklist;
  Worklist.push_back(&Header);

  while (!Worklist.empty()) {
    MachineBasicBlock *MBB = Worklist.pop_back_val();
    if (MBB == &Exit)
      continue;
    if (!LoopBlocks.insert(MBB).second)
      continue;
    if (LoopBlocks.size() > 16)
      return false;
    for (MachineBasicBlock *Succ : MBB->successors())
      if (Succ != &Exit)
        Worklist.push_back(Succ);
  }

  return true;
}

static inline bool loopOnlyTouchesCountAt(ArrayRef<MachineBasicBlock *> Blocks,
                                   MachineInstr &Cmp, MachineInstr &Dec,
                                   Register OldCountReg, Register NewCountReg,
                                   const TargetRegisterInfo &TRI) {
  for (MachineBasicBlock *MBB : Blocks) {
    for (MachineInstr &MI : *MBB) {
      if (MI.isDebugInstr() || &MI == &Cmp || &MI == &Dec)
        continue;
      if (!regsOverlap(TRI, OldCountReg, NewCountReg) &&
          instrTouchesReg(MI, OldCountReg, TRI))
        return false;
      if (instrTouchesReg(MI, NewCountReg, TRI))
        return false;
    }
  }
  return true;
}

static inline bool findCountedLoopLatch(MachineBasicBlock &Header, Register CountReg,
                                 Register IndexReg, MachineInstr *&Dec,
                                 MachineInstr *&Inc,
                                 const TargetRegisterInfo &TRI) {
  Dec = nullptr;
  Inc = nullptr;

  for (MachineBasicBlock *Pred : Header.predecessors()) {
    if (Pred == &Header)
      continue;
    MachineBasicBlock::iterator Last = Pred->getLastNonDebugInstr();
    if (Last == Pred->end() || Last->getOpcode() != Bedrock::JMP ||
        Last->getNumOperands() < 1 || !Last->getOperand(0).isMBB() ||
        Last->getOperand(0).getMBB() != &Header)
      continue;

    MachineInstr *PredDec = nullptr;
    MachineInstr *PredInc = nullptr;
    for (MachineInstr &MI : *Pred) {
      if (MI.isDebugInstr())
        continue;
      if (isUnaryRegOp(MI, Bedrock::DEC64r, CountReg, TRI) ||
          isUnaryRegOp(MI, Bedrock::DEC32r, CountReg, TRI)) {
        if (PredDec)
          return false;
        PredDec = &MI;
        continue;
      }
      if (isUnaryRegOp(MI, Bedrock::INC32r, IndexReg, TRI) ||
          isUnaryRegOp(MI, Bedrock::INC64r, IndexReg, TRI)) {
        if (PredInc)
          return false;
        PredInc = &MI;
        continue;
      }
    }

    if (!PredDec || !PredInc)
      continue;
    if (Dec || Inc)
      return false;
    Dec = PredDec;
    Inc = PredInc;
  }

  return Dec && Inc;
}

static inline bool findCountedLoopJccLatch(MachineBasicBlock &Header,
                                    Register CountReg, Register IndexReg,
                                    MachineInstr *&Dec, MachineInstr *&Inc,
                                    const TargetRegisterInfo &TRI) {
  Dec = nullptr;
  Inc = nullptr;

  for (MachineBasicBlock *Pred : Header.predecessors()) {
    if (Pred == &Header)
      continue;
    MachineInstr *Branch = findJccTo(*Pred, Header);
    if (!Branch)
      continue;

    MachineInstr *PredDec = nullptr;
    MachineInstr *PredInc = nullptr;
    for (MachineInstr &MI : *Pred) {
      if (MI.isDebugInstr())
        continue;
      if (isUnaryRegOp(MI, Bedrock::DEC64r, CountReg, TRI) ||
          isUnaryRegOp(MI, Bedrock::DEC32r, CountReg, TRI)) {
        if (PredDec)
          return false;
        PredDec = &MI;
        continue;
      }
      if (isUnaryRegOp(MI, Bedrock::INC32r, IndexReg, TRI) ||
          isUnaryRegOp(MI, Bedrock::INC64r, IndexReg, TRI)) {
        if (PredInc)
          return false;
        PredInc = &MI;
        continue;
      }
    }

    if (!PredDec || !PredInc)
      continue;
    if (Dec || Inc)
      return false;
    Dec = PredDec;
    Inc = PredInc;
  }

  return Dec && Inc;
}

static inline bool validateCountAndIndexDefs(MachineFunction &MF, Register CountReg,
                                      Register IndexReg, Register LimitReg,
                                      MachineInstr *Dec, MachineInstr *Inc,
                                      MachineInstr *&CountInit,
                                      MachineInstr *&IndexInit,
                                      const TargetRegisterInfo &TRI) {
  CountInit = nullptr;
  IndexInit = nullptr;

  uint32_t EntryKnownZero = 0;
  for (MachineInstr &MI : MF.front()) {
    if (MI.isDebugInstr())
      continue;

    if (instrDefinesReg(MI, CountReg, TRI)) {
      if (CountInit || &MI == Dec)
        return false;
      if ((MI.getOpcode() != Bedrock::EXTZQ32rr &&
           MI.getOpcode() != Bedrock::MOV64rr) ||
          MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
          !MI.getOperand(1).isReg() ||
          !regsOverlap(TRI, MI.getOperand(0).getReg(), CountReg) ||
          !regsOverlap(TRI, MI.getOperand(1).getReg(), LimitReg))
        return false;
      CountInit = &MI;
    }

    if (instrDefinesReg(MI, IndexReg, TRI)) {
      if (IndexInit || &MI == Inc)
        return false;
      Register ClrDst;
      bool IsZeroDef =
          isClrReg(MI, ClrDst) && regsOverlap(TRI, ClrDst, IndexReg);
      if (!IsZeroDef && MI.getOpcode() == Bedrock::MOV64rr &&
          MI.getNumOperands() >= 2 && MI.getOperand(0).isReg() &&
          MI.getOperand(1).isReg() &&
          regsOverlap(TRI, MI.getOperand(0).getReg(), IndexReg) &&
          maskHasKnownZeroReg(EntryKnownZero, MI.getOperand(1).getReg(), TRI))
        IsZeroDef = true;
      if (!IsZeroDef)
        return false;
      IndexInit = &MI;
    }

    transferKnownZero(MI, EntryKnownZero, TRI);
  }

  if (!CountInit || !IndexInit)
    return false;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      if (instrDefinesReg(MI, CountReg, TRI) && &MI != CountInit && &MI != Dec)
        return false;
      if (instrDefinesReg(MI, IndexReg, TRI) && &MI != IndexInit && &MI != Inc)
        return false;
    }
  }

  return true;
}

static inline unsigned getTestOpcodeForZeroStackCmp(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::CMP8rm:
  case Bedrock::CMP8mr:
    return Bedrock::TEST8rr;
  case Bedrock::CMP16rm:
  case Bedrock::CMP16mr:
    return Bedrock::TEST16rr;
  case Bedrock::CMP32rm:
  case Bedrock::CMP32mr:
    return Bedrock::TEST32rr;
  case Bedrock::CMP64rm:
  case Bedrock::CMP64mr:
    return Bedrock::TEST64rr;
  }
}

static inline bool isZeroStackCmpUse(const MachineInstr &MI,
                              const StackConstStore &Slot, Register &Reg,
                              unsigned &TestOpcode) {
  TestOpcode = getTestOpcodeForZeroStackCmp(MI.getOpcode());
  if (TestOpcode == 0 || hasOrderedMemOperand(MI) ||
      memSizeForOpcode(MI.getOpcode()) != Slot.Size ||
      MI.getNumOperands() < 3 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg() || !MI.getOperand(2).isImm() ||
      MI.getOperand(1).getReg() != Bedrock::SP ||
      MI.getOperand(2).getImm() != Slot.Offset)
    return false;
  Reg = MI.getOperand(0).getReg();
  return isDReg(Reg);
}

static inline bool hasEqNeBranchBeforeFlagsClobber(MachineInstr &FlagDef,
                                            const TargetRegisterInfo &TRI) {
  MachineBasicBlock &MBB = *FlagDef.getParent();
  auto BranchI = nextNonDebug(FlagDef.getIterator(), MBB);
  for (; BranchI != MBB.end() && !isEqNeBranch(*BranchI);
       BranchI = nextNonDebug(BranchI, MBB)) {
    if (BranchI->isCall() || BranchI->isTerminator() ||
        instrTouchesReg(*BranchI, Bedrock::FLAGS, TRI))
      return false;
  }
  return BranchI != MBB.end();
}

static inline bool isZeroStackLoadCmpUse(MachineInstr &Load,
                                  const StackConstStore &Slot, Register &Reg,
                                  unsigned &TestOpcode, MachineInstr *&Cmp,
                                  const TargetRegisterInfo &TRI) {
  Register Loaded;
  Register Base;
  int64_t Offset = 0;
  if (!isMemLoad(Load, Loaded, Base, Offset) || Base != Bedrock::SP ||
      Offset != Slot.Offset ||
      memSizeForOpcode(Load.getOpcode()) != Slot.Size ||
      hasOrderedMemOperand(Load))
    return false;

  MachineBasicBlock &MBB = *Load.getParent();
  auto CmpI = nextNonDebug(Load.getIterator(), MBB);
  if (CmpI == MBB.end() || CmpI->getNumOperands() < 2 ||
      !CmpI->getOperand(0).isReg() || !CmpI->getOperand(1).isReg())
    return false;

  TestOpcode = getTestOpcodeForCmp(CmpI->getOpcode());
  if (TestOpcode == 0 || memSizeForOpcode(Load.getOpcode()) != Slot.Size)
    return false;

  Register LHS = CmpI->getOperand(0).getReg();
  Register RHS = CmpI->getOperand(1).getReg();
  bool LHSLoaded = regsOverlap(TRI, LHS, Loaded);
  bool RHSLoaded = regsOverlap(TRI, RHS, Loaded);
  if (LHSLoaded == RHSLoaded)
    return false;

  Reg = LHSLoaded ? RHS : LHS;
  if (!isDReg(Reg) || (!operandIsKill(*CmpI, Loaded, TRI) &&
                       !regUnusedAfterInCFG(std::next(CmpI), MBB, Loaded, TRI)))
    return false;

  Cmp = &*CmpI;
  return true;
}

static inline bool isCopyRegToReg(const MachineInstr &MI, Register Dst, Register Src,
                           const TargetRegisterInfo &TRI) {
  if ((MI.getOpcode() != Bedrock::MOV32rr &&
       MI.getOpcode() != Bedrock::MOV64rr) ||
      MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg())
    return false;
  return regsOverlap(TRI, MI.getOperand(0).getReg(), Dst) &&
         regsOverlap(TRI, MI.getOperand(1).getReg(), Src);
}

static inline bool canRewriteProductAccumInstr(const MachineInstr &MI,
                                        Register Product, Register Acc,
                                        Register Coeff,
                                        const TargetRegisterInfo &TRI) {
  if (MI.getOpcode() != Bedrock::ADD32rr || MI.getNumOperands() < 3 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
      !MI.getOperand(2).isReg())
    return false;
  if (!regsOverlap(TRI, MI.getOperand(0).getReg(), Product) ||
      !regsOverlap(TRI, MI.getOperand(1).getReg(), Product))
    return false;
  Register RHS = MI.getOperand(2).getReg();
  return !regsOverlap(TRI, RHS, Acc) && !regsOverlap(TRI, RHS, Product) &&
         !regsOverlap(TRI, RHS, Coeff);
}

static inline void replaceRegOperandUsesAndDefs(MachineInstr &MI, Register OldReg,
                                         Register NewReg,
                                         const TargetRegisterInfo &TRI) {
  for (MachineOperand &MO : MI.operands())
    if (MO.isReg() && regsOverlap(TRI, MO.getReg(), OldReg))
      MO.setReg(NewReg);
}

static inline bool canRewriteExplicitRegTouches(const MachineInstr &MI, Register Reg,
                                         const TargetRegisterInfo &TRI) {
  if (instrHasRegMaskForReg(MI, Reg, TRI))
    return false;
  for (MCPhysReg ImpUse : MI.getDesc().implicit_uses())
    if (regsOverlap(TRI, Register(ImpUse), Reg))
      return false;
  for (MCPhysReg ImpDef : MI.getDesc().implicit_defs())
    if (regsOverlap(TRI, Register(ImpDef), Reg))
      return false;
  for (const MachineOperand &MO : MI.operands())
    if (operandTouchesReg(MO, Reg, TRI) && MO.isImplicit())
      return false;
  return true;
}

struct MAddProductTerm {
  MachineInstr *Load = nullptr;
  MachineInstr *Mul = nullptr;
  MachineInstr *Add = nullptr;
  Register Tmp;
  Register Base;
  int64_t Offset = 0;
  bool PostInc = false;
  Register Coeff;
  Register Product;
};

static inline bool matchMAddProductTerm(MachineBasicBlock::iterator LoadI,
                                 MachineBasicBlock &MBB, MAddProductTerm &Term,
                                 const TargetRegisterInfo &TRI) {
  MachineInstr &Load = *LoadI;
  Register Tmp, Base;
  int64_t Offset = 0;
  bool PostInc = false;
  if (!isMAddLoad(Load, Tmp, Base, Offset, PostInc) ||
      (Load.getOpcode() != Bedrock::MOV32rm &&
       Load.getOpcode() != Bedrock::MOV32postrm))
    return false;

  auto MulI = nextNonDebug(LoadI, MBB);
  if (MulI == MBB.end() || MulI->getOpcode() != Bedrock::MULU32rr ||
      MulI->getNumOperands() < 3 || !MulI->getOperand(0).isReg() ||
      !MulI->getOperand(1).isReg() || !MulI->getOperand(2).isReg())
    return false;
  if (!regsOverlap(TRI, MulI->getOperand(0).getReg(), Tmp) ||
      !regsOverlap(TRI, MulI->getOperand(1).getReg(), Tmp) ||
      regsOverlap(TRI, MulI->getOperand(2).getReg(), Tmp))
    return false;

  Term.Load = &Load;
  Term.Mul = &*MulI;
  Term.Tmp = Tmp;
  Term.Base = Base;
  Term.Offset = Offset;
  Term.PostInc = PostInc;
  Term.Coeff = MulI->getOperand(2).getReg();
  Term.Product = MulI->getOperand(0).getReg();
  return true;
}

static inline Register findUnusedCallerDReg(const MachineFunction &MF,
                                     const TargetRegisterInfo &TRI) {
  for (Register Reg = Bedrock::D0; Reg <= Bedrock::D5;
       Reg = Register(Reg + 1)) {
    bool Used = false;
    for (const MachineBasicBlock &MBB : MF) {
      for (const MachineInstr &MI : MBB) {
        if (!MI.isDebugInstr() && instrUsesReg(MI, Reg, TRI)) {
          Used = true;
          break;
        }
      }
      if (Used)
        break;
    }
    if (!Used)
      return Reg;
  }
  return Register();
}

static inline MachineBasicBlock *findSingleNonSelfPredecessor(MachineBasicBlock &MBB) {
  MachineBasicBlock *Found = nullptr;
  SmallPtrSet<MachineBasicBlock *, 4> Seen;
  for (MachineBasicBlock *Pred : MBB.predecessors()) {
    if (!Seen.insert(Pred).second)
      continue;
    if (Pred == &MBB)
      continue;
    if (Found)
      return nullptr;
    Found = Pred;
  }
  return Found;
}

static inline bool isPositiveCountPretest(MachineBasicBlock &Header,
                                   MachineBasicBlock &Body, Register CountReg,
                                   const TargetRegisterInfo &TRI) {
  if (!Header.isSuccessor(&Body))
    return false;

  MachineBasicBlock::iterator BranchI = Header.getLastNonDebugInstr();
  if (BranchI == Header.end() || BranchI->getOpcode() != Bedrock::JCC ||
      BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB())
    return false;
  if (BranchI == Header.begin())
    return false;
  std::optional<int64_t> BranchCC = getCondCodeImm(BranchI->getOperand(1));
  if (!BranchCC || *BranchCC != BedrockCC::LE ||
      BranchI->getOperand(0).getMBB() == &Body)
    return false;

  MachineBasicBlock::iterator TestI = prevNonDebug(BranchI, Header);
  if (TestI == Header.end() || TestI->getOpcode() != Bedrock::TEST32rr ||
      TestI->getNumOperands() < 2 || !TestI->getOperand(0).isReg() ||
      !TestI->getOperand(1).isReg())
    return false;

  return regsOverlap(TRI, TestI->getOperand(0).getReg(), CountReg) &&
         regsOverlap(TRI, TestI->getOperand(1).getReg(), CountReg);
}

static inline bool isZeroExitCountPretest(MachineBasicBlock &Header,
                                   MachineBasicBlock &Body, Register CountReg,
                                   const TargetRegisterInfo &TRI) {
  if (!Header.isSuccessor(&Body))
    return false;

  MachineBasicBlock::iterator BranchI = Header.getLastNonDebugInstr();
  if (BranchI == Header.end() || BranchI->getOpcode() != Bedrock::JCC ||
      BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB())
    return false;
  if (BranchI == Header.begin())
    return false;
  std::optional<int64_t> BranchCC = getCondCodeImm(BranchI->getOperand(1));
  if (!BranchCC || *BranchCC != BedrockCC::EQ ||
      BranchI->getOperand(0).getMBB() == &Body)
    return false;

  MachineBasicBlock::iterator TestI = prevNonDebug(BranchI, Header);
  if (TestI == Header.end() ||
      (TestI->getOpcode() != Bedrock::TEST32rr &&
       TestI->getOpcode() != Bedrock::TEST64rr) ||
      TestI->getNumOperands() < 2 || !TestI->getOperand(0).isReg() ||
      !TestI->getOperand(1).isReg())
    return false;

  return regsOverlap(TRI, TestI->getOperand(0).getReg(), CountReg) &&
         regsOverlap(TRI, TestI->getOperand(1).getReg(), CountReg);
}

static inline bool findPositiveConstDefInBlockBefore(MachineBasicBlock &MBB,
                                              MachineBasicBlock::iterator UseI,
                                              Register Reg,
                                              const TargetRegisterInfo &TRI) {
  for (auto I = UseI; I != MBB.begin();) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (!instrDefinesReg(*I, Reg, TRI))
      continue;
    std::optional<int64_t> Const = getConstDefForReg(*I, Reg, TRI);
    return Const && *Const > 0;
  }
  return false;
}

static inline bool buildRepMovMM(MachineBasicBlock &MBB, MachineInstr &BodyMI,
                          Register CountReg, const BedrockInstrInfo &TII) {
  unsigned RepOpcode = getRepMovMMOpcode(BodyMI.getOpcode());
  if (RepOpcode == 0 || BodyMI.getNumOperands() < 2 ||
      !BodyMI.getOperand(0).isReg() || !BodyMI.getOperand(1).isReg())
    return false;

  MachineInstrBuilder MIB =
      BuildMI(MBB, BodyMI.getIterator(), BodyMI.getDebugLoc(),
              TII.get(RepOpcode), CountReg)
          .addReg(CountReg)
          .addReg(BodyMI.getOperand(0).getReg())
          .addImm(Bedrock::UpdatePostInc)
          .addReg(BodyMI.getOperand(1).getReg());
  MIB.addImm(Bedrock::UpdatePostInc);
  MIB.cloneMemRefs(BodyMI);
  return true;
}

static inline bool buildRepPostMemStore(MachineBasicBlock &MBB, MachineInstr &BodyMI,
                                 Register CountReg,
                                 const BedrockInstrInfo &TII) {
  unsigned RepOpcode = getRepMovPostMROpcode(BodyMI.getOpcode());
  if (RepOpcode == 0 || BodyMI.getNumOperands() < 2 ||
      !BodyMI.getOperand(0).isReg() || !BodyMI.getOperand(1).isReg())
    return false;

  MachineInstrBuilder MIB =
      BuildMI(MBB, BodyMI.getIterator(), BodyMI.getDebugLoc(),
              TII.get(RepOpcode), CountReg)
          .addReg(CountReg)
          .addReg(BodyMI.getOperand(0).getReg())
          .addReg(BodyMI.getOperand(1).getReg());
  MIB.addImm(Bedrock::UpdatePostInc);
  MIB.cloneMemRefs(BodyMI);
  return true;
}

static inline bool buildRepPostMemSource(MachineBasicBlock &MBB, MachineInstr &BodyMI,
                                  Register CountReg,
                                  const BedrockInstrInfo &TII) {
  unsigned RepOpcode = getRepPostMemSourceOpcode(BodyMI.getOpcode());
  if (RepOpcode == 0 || BodyMI.getNumOperands() < 3 ||
      !BodyMI.getOperand(0).isReg() || !BodyMI.getOperand(1).isReg() ||
      !BodyMI.getOperand(2).isReg())
    return false;

  Register Acc = BodyMI.getOperand(0).getReg();
  Register LHS = BodyMI.getOperand(1).getReg();
  if (Acc != LHS)
    return false;

  MachineInstrBuilder MIB = BuildMI(MBB, BodyMI.getIterator(),
                                    BodyMI.getDebugLoc(), TII.get(RepOpcode))
                                .addReg(CountReg, RegState::Define)
                                .addReg(Acc, RegState::Define)
                                .addReg(CountReg)
                                .addReg(Acc)
                                .addReg(BodyMI.getOperand(2).getReg());
  MIB.addImm(Bedrock::UpdatePostInc);
  MIB.cloneMemRefs(BodyMI);
  return true;
}

static inline MachineInstr *findLeaDefBefore(MachineInstr &Use, Register Reg,
                                      Register Base, int64_t Offset,
                                      const TargetRegisterInfo &TRI) {
  MachineFunction &MF = *Use.getMF();
  MachineInstr *LastDef = nullptr;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (&MI == &Use)
        return LastDef;
      if (MI.isDebugInstr())
        continue;
      if (!instrDefinesReg(MI, Reg, TRI))
        continue;
      LastDef = nullptr;
      if (MI.getOpcode() == Bedrock::LEAri && MI.getNumOperands() >= 3 &&
          MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
          MI.getOperand(2).isImm() &&
          regsOverlap(TRI, MI.getOperand(0).getReg(), Reg) &&
          regsOverlap(TRI, MI.getOperand(1).getReg(), Base) &&
          MI.getOperand(2).getImm() == Offset)
        LastDef = &MI;
    }
  }
  return nullptr;
}

static inline Register findScratchARegForLoop(MachineBasicBlock &Header,
                                       MachineBasicBlock &Body,
                                       MachineBasicBlock &Exit,
                                       const TargetRegisterInfo &TRI,
                                       Register AvoidA = Register(),
                                       Register AvoidB = Register()) {
  for (Register Reg = Bedrock::A0; Reg <= Bedrock::A7;
       Reg = Register(Reg + 1)) {
    if (Reg == Bedrock::SP || regsOverlap(TRI, Reg, AvoidA) ||
        regsOverlap(TRI, Reg, AvoidB))
      continue;

    bool TouchedInLoop = false;
    for (MachineInstr &MI : Header)
      if (!MI.isDebugInstr() && instrTouchesReg(MI, Reg, TRI)) {
        TouchedInLoop = true;
        break;
      }
    if (TouchedInLoop)
      continue;
    for (MachineInstr &MI : Body)
      if (!MI.isDebugInstr() && instrTouchesReg(MI, Reg, TRI)) {
        TouchedInLoop = true;
        break;
      }
    if (TouchedInLoop)
      continue;

    SmallPtrSet<MachineBasicBlock *, 8> Visiting;
    if (regDeadFromBlockStartInCFG(Exit, Reg, TRI, Visiting))
      return Reg;
  }
  return Register();
}

static inline Register findScratchDRegForLoop(MachineBasicBlock &Header,
                                       MachineBasicBlock &Body,
                                       MachineBasicBlock &Exit,
                                       const TargetRegisterInfo &TRI,
                                       Register AvoidA = Register(),
                                       Register AvoidB = Register()) {
  for (Register Reg = Bedrock::D0; Reg <= Bedrock::D7;
       Reg = Register(Reg + 1)) {
    if (regsOverlap(TRI, Reg, AvoidA) || regsOverlap(TRI, Reg, AvoidB))
      continue;

    bool TouchedInLoop = false;
    for (MachineInstr &MI : Header)
      if (!MI.isDebugInstr() && instrTouchesReg(MI, Reg, TRI)) {
        TouchedInLoop = true;
        break;
      }
    if (TouchedInLoop)
      continue;
    for (MachineInstr &MI : Body)
      if (!MI.isDebugInstr() && instrTouchesReg(MI, Reg, TRI)) {
        TouchedInLoop = true;
        break;
      }
    if (TouchedInLoop)
      continue;

    SmallPtrSet<MachineBasicBlock *, 8> Visiting;
    if (regDeadFromBlockStartInCFG(Exit, Reg, TRI, Visiting))
      return Reg;
  }
  return Register();
}

static inline MachineInstr *findConstStackStoreForLoad(MachineInstr &Load,
                                                int64_t &Value,
                                                const TargetRegisterInfo &TRI) {
  Register Dst;
  Register Base;
  int64_t Offset = 0;
  if (!isMemLoad(Load, Dst, Base, Offset) || Base != Bedrock::SP)
    return nullptr;
  unsigned Size = memSizeForOpcode(Load.getOpcode());
  if (Size == 0)
    return nullptr;

  MachineFunction &MF = *Load.getMF();
  MachineInstr *LastStore = nullptr;
  std::optional<int64_t> LastValue;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (&MI == &Load) {
        if (!LastStore || !LastValue)
          return nullptr;
        Value = *LastValue;
        return LastStore;
      }
      if (MI.isDebugInstr())
        continue;
      if (!instrHasOverlappingSPMemRange(MI, Offset, Size, TRI))
        continue;

      Register Src;
      Register StoreBase;
      int64_t StoreOffset = 0;
      if (!isMemStore(MI, Src, StoreBase, StoreOffset) ||
          StoreBase != Bedrock::SP || StoreOffset != Offset ||
          memSizeForOpcode(MI.getOpcode()) != Size) {
        LastStore = nullptr;
        LastValue = std::nullopt;
        continue;
      }

      int64_t StoreValue = 0;
      MachineInstr *ConstDef =
          findLastConstDefBeforeAny(MI, Src, StoreValue, TRI);
      LastStore = ConstDef ? &MI : nullptr;
      LastValue = ConstDef ? std::optional<int64_t>(StoreValue) : std::nullopt;
    }
  }
  return nullptr;
}

static inline bool findConstOrStackLoadBefore(MachineInstr &Use, Register Reg,
                                       int64_t &Value,
                                       MachineInstr *&MaterializedDef,
                                       const TargetRegisterInfo &TRI) {
  MaterializedDef = nullptr;
  if (findLastConstDefBeforeAny(Use, Reg, Value, TRI))
    return true;

  MachineInstr *Def = findLastDefBefore(Use, Reg, TRI);
  if (!Def)
    return false;

  Register LoadDst;
  Register LoadBase;
  int64_t LoadOffset = 0;
  if (!isMemLoad(*Def, LoadDst, LoadBase, LoadOffset) ||
      !regsOverlap(TRI, LoadDst, Reg) || LoadBase != Bedrock::SP)
    return false;

  int64_t StackValue = 0;
  if (!findConstStackStoreForLoad(*Def, StackValue, TRI))
    return false;
  Value = StackValue;
  MaterializedDef = Def;
  return true;
}

static inline void removeAllSuccessors(MachineBasicBlock &MBB) {
  SmallVector<MachineBasicBlock *, 4> Succs(MBB.successors());
  for (MachineBasicBlock *Succ : Succs)
    MBB.removeSuccessor(Succ);
}

static inline void eraseAllNonDebugInstrs(MachineBasicBlock &MBB) {
  for (auto I = MBB.begin(); I != MBB.end();) {
    if (I->isDebugInstr()) {
      ++I;
      continue;
    }
    MachineInstr *MI = &*I++;
    MI->eraseFromParent();
  }
}

static inline MachineInstr *findLeadingZeroRegDef(MachineBasicBlock &MBB, Register Reg,
                                           const TargetRegisterInfo &TRI) {
  MachineInstr *Zero = nullptr;
  for (MachineInstr &MI : MBB) {
    if (MI.isDebugInstr())
      continue;
    Register ClrReg;
    if (!Zero && isClrReg(MI, ClrReg) && regsOverlap(TRI, ClrReg, Reg)) {
      Zero = &MI;
      continue;
    }
    if (instrTouchesReg(MI, Reg, TRI))
      return nullptr;
  }
  return Zero;
}

static inline MachineBasicBlock *
findHeaderPredecessorExcluding(MachineBasicBlock &Header,
                               MachineBasicBlock &Excluded) {
  MachineBasicBlock *Found = nullptr;
  SmallPtrSet<MachineBasicBlock *, 4> Seen;
  for (MachineBasicBlock *Pred : Header.predecessors()) {
    if (!Seen.insert(Pred).second || Pred == &Excluded)
      continue;
    if (Found)
      return nullptr;
    Found = Pred;
  }
  return Found;
}

#endif // LLVM_LIB_TARGET_BEDROCK_BEDROCKPEEPHOLE_H
