//===-- BedrockPushPopMerge.cpp - Bedrock PUSHM/POPM formation ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

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

#define DEBUG_TYPE "bedrock-push-pop-merge"
#define PASS_NAME "Bedrock PUSHM/POPM formation"

namespace {
class BedrockPushPopMerge : public MachineFunctionPass {
  BedrockPeepholeProfile Profile = BedrockPeepholeProfile::O0;

public:
  static char ID;

  BedrockPushPopMerge(BedrockPeepholeProfile Profile)
      : MachineFunctionPass(ID), Profile(Profile) {
    initializeBedrockPushPopMergePass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return PASS_NAME; }

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
} // end anonymous namespace

char BedrockPushPopMerge::ID = 0;

INITIALIZE_PASS(BedrockPushPopMerge, DEBUG_TYPE, PASS_NAME, false, false)

static bool profileAtLeast(BedrockPeepholeProfile Profile,
                           BedrockPeepholeProfile Level) {
  return static_cast<unsigned>(Profile) >= static_cast<unsigned>(Level);
}

static std::optional<unsigned> getMaskBit(Register Reg) {
  if (Reg >= Bedrock::D0 && Reg <= Bedrock::D7)
    return Reg - Bedrock::D0;
  if (Reg >= Bedrock::A0 && Reg <= Bedrock::A7)
    return 8 + Reg - Bedrock::A0;
  return std::nullopt;
}

static bool isDReg(Register Reg) {
  return Reg >= Bedrock::D0 && Reg <= Bedrock::D7;
}

static bool isAReg(Register Reg) {
  return Reg >= Bedrock::A0 && Reg <= Bedrock::A7;
}

static bool isIntReg(Register Reg) { return isDReg(Reg) || isAReg(Reg); }

static bool fitsDisp16(int64_t Offset) {
  return Offset >= std::numeric_limits<int16_t>::min() &&
         Offset <= std::numeric_limits<int16_t>::max();
}

static Register getRegForMaskBit(unsigned Bit) {
  return Bit < 8 ? Register(Bedrock::D0 + Bit)
                 : Register(Bedrock::A0 + Bit - 8);
}

static std::optional<Register> getSavedDReg(uint16_t Mask) {
  for (int Bit = 7; Bit >= 0; --Bit) {
    if ((Mask & (uint16_t(1) << Bit)) != 0)
      return getRegForMaskBit(Bit);
  }
  return std::nullopt;
}

static unsigned popcountMask(uint16_t Mask) {
  unsigned Count = 0;
  for (unsigned Bit = 0; Bit != 16; ++Bit)
    if ((Mask & (uint16_t(1) << Bit)) != 0)
      ++Count;
  return Count;
}

static std::optional<Register> singleRegFromMask(uint16_t Mask) {
  if (popcountMask(Mask) != 1)
    return std::nullopt;
  for (unsigned Bit = 0; Bit != 16; ++Bit)
    if ((Mask & (uint16_t(1) << Bit)) != 0)
      return getRegForMaskBit(Bit);
  return std::nullopt;
}

static bool fitsImm6(uint64_t Value) { return Value < 64; }

static MachineBasicBlock::iterator nextNonDebug(MachineBasicBlock::iterator I,
                                                MachineBasicBlock &MBB) {
  for (++I; I != MBB.end() && I->isDebugInstr(); ++I)
    ;
  return I;
}

static MachineBasicBlock::iterator prevNonDebug(MachineBasicBlock::iterator I,
                                                MachineBasicBlock &MBB) {
  while (I != MBB.begin()) {
    --I;
    if (!I->isDebugInstr())
      return I;
  }
  return MBB.end();
}

static bool regsOverlap(const TargetRegisterInfo &TRI, Register A, Register B) {
  return A.isValid() && B.isValid() && TRI.regsOverlap(A, B);
}

static bool operandTouchesReg(const MachineOperand &MO, Register Reg,
                              const TargetRegisterInfo &TRI) {
  return MO.isReg() && regsOverlap(TRI, MO.getReg(), Reg);
}

static bool instrUsesReg(const MachineInstr &MI, Register Reg,
                         const TargetRegisterInfo &TRI) {
  for (const MachineOperand &MO : MI.operands())
    if (operandTouchesReg(MO, Reg, TRI) && MO.readsReg())
      return true;
  for (MCPhysReg ImpUse : MI.getDesc().implicit_uses())
    if (regsOverlap(TRI, Register(ImpUse), Reg))
      return true;
  return false;
}

static bool instrDefinesReg(const MachineInstr &MI, Register Reg,
                            const TargetRegisterInfo &TRI) {
  for (const MachineOperand &MO : MI.operands())
    if (operandTouchesReg(MO, Reg, TRI) && MO.isDef())
      return true;
  for (MCPhysReg ImpDef : MI.getDesc().implicit_defs())
    if (regsOverlap(TRI, Register(ImpDef), Reg))
      return true;
  return false;
}

static bool instrDefinesDeadReg(const MachineInstr &MI, Register Reg,
                                const TargetRegisterInfo &TRI) {
  for (const MachineOperand &MO : MI.operands())
    if (operandTouchesReg(MO, Reg, TRI) && MO.isDef() && MO.isDead())
      return true;
  return false;
}

static bool instrTouchesReg(const MachineInstr &MI, Register Reg,
                            const TargetRegisterInfo &TRI) {
  return instrUsesReg(MI, Reg, TRI) || instrDefinesReg(MI, Reg, TRI);
}

static void
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

static bool operandIsKill(const MachineInstr &MI, Register Reg,
                          const TargetRegisterInfo &TRI) {
  for (const MachineOperand &MO : MI.operands())
    if (operandTouchesReg(MO, Reg, TRI) && MO.readsReg() && MO.isKill())
      return true;
  return false;
}

static bool instrHasRegMaskForReg(const MachineInstr &MI, Register Reg,
                                  const TargetRegisterInfo &TRI);

static bool regDeadAfter(MachineBasicBlock::iterator From,
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

static bool regDeadOrClobberedAfter(MachineBasicBlock::iterator From,
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

static bool regUnusedBeforeEndOrDef(MachineBasicBlock::iterator From,
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

static bool regDefDeadOrDeadAfter(MachineBasicBlock::iterator DefI,
                                  MachineBasicBlock &MBB, Register Reg,
                                  const TargetRegisterInfo &TRI) {
  return instrDefinesDeadReg(*DefI, Reg, TRI) ||
         regDeadAfter(std::next(DefI), MBB, Reg, TRI);
}

static bool regDefDeadOrClobberedAfter(MachineBasicBlock::iterator DefI,
                                       MachineBasicBlock &MBB, Register Reg,
                                       const TargetRegisterInfo &TRI) {
  return instrDefinesDeadReg(*DefI, Reg, TRI) ||
         regDeadOrClobberedAfter(std::next(DefI), MBB, Reg, TRI);
}

static bool
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

static bool
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

static bool regUnusedAfterInCFG(MachineBasicBlock::iterator From,
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

static bool regDeadAfterInCFG(MachineBasicBlock::iterator From,
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

static bool regDefDeadOrDeadAfterInCFG(MachineBasicBlock::iterator DefI,
                                       MachineBasicBlock &MBB, Register Reg,
                                       const TargetRegisterInfo &TRI) {
  return instrDefinesDeadReg(*DefI, Reg, TRI) ||
         regDeadAfterInCFG(std::next(DefI), MBB, Reg, TRI);
}

static bool
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

static bool regReachesRetBeforeTouch(MachineBasicBlock::iterator From,
                                     MachineBasicBlock &MBB, Register Reg,
                                     const TargetRegisterInfo &TRI) {
  SmallPtrSet<MachineBasicBlock *, 8> Visiting;
  return regReachesRetBeforeTouch(From, MBB, Reg, TRI, Visiting);
}

static bool functionUsesReg(const MachineFunction &MF, Register Reg,
                            const MachineInstr *Ignore,
                            const TargetRegisterInfo &TRI) {
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB)
      if (&MI != Ignore && !MI.isDebugInstr() && instrUsesReg(MI, Reg, TRI))
        return true;
  return false;
}

static bool isAddImmToReg(const MachineInstr &MI, Register Reg, int64_t Amount,
                          const TargetRegisterInfo &TRI) {
  if (MI.getOpcode() != Bedrock::ADD64ri || MI.getNumOperands() < 3 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
      !MI.getOperand(2).isImm())
    return false;
  return regsOverlap(TRI, MI.getOperand(0).getReg(), Reg) &&
         regsOverlap(TRI, MI.getOperand(1).getReg(), Reg) &&
         MI.getOperand(2).getImm() == Amount;
}

static MachineBasicBlock::iterator
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

static MachineInstr *findPostInc(MachineBasicBlock::iterator From,
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

static unsigned memSizeForOpcode(unsigned Opcode) {
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

static unsigned getPostLoadOpcode(unsigned Opcode) {
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

static bool postLoadSupportsAReg(unsigned Opcode) {
  return Opcode == Bedrock::MOV32rm || Opcode == Bedrock::MOV64rm;
}

static unsigned getPostStoreOpcode(unsigned Opcode) {
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

static unsigned getStoreOpcodeForLoad(unsigned Opcode) {
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

static unsigned getRegMoveOpcodeForLoad(unsigned Opcode, Register Dst,
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

static unsigned getPlainLoadOpcode(unsigned Opcode) {
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

static unsigned getRegMoveOpcodeForMaybePostLoad(unsigned Opcode, Register Dst,
                                                 Register Src) {
  return getRegMoveOpcodeForLoad(getPlainLoadOpcode(Opcode), Dst, Src);
}

static bool isStackAdjust(const MachineInstr &MI, unsigned Opcode,
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

static bool isCalleeSaveStore(const MachineInstr &MI, Register &Reg,
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

static bool isCalleeSaveLoad(const MachineInstr &MI, Register &Reg,
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

static bool expectedStoreOffset(uint16_t Mask, Register Reg, int64_t Total,
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

static void addImplicitPushRegs(MachineInstrBuilder MIB, uint16_t Mask) {
  for (unsigned Bit = 0; Bit != 16; ++Bit) {
    if ((Mask & (uint16_t(1) << Bit)) != 0)
      MIB.addReg(getRegForMaskBit(Bit), RegState::Implicit | RegState::Kill);
  }
}

static void addImplicitPopRegs(MachineInstrBuilder MIB, uint16_t Mask) {
  for (unsigned Bit = 0; Bit != 16; ++Bit) {
    if ((Mask & (uint16_t(1) << Bit)) != 0) {
      MIB.addReg(getRegForMaskBit(Bit), RegState::Implicit | RegState::Define);
    }
  }
}

static unsigned pushOpcodeForReg(Register Reg) {
  if (isDReg(Reg))
    return Bedrock::PUSHD;
  if (isAReg(Reg))
    return Bedrock::PUSHA;
  return 0;
}

static unsigned popOpcodeForReg(Register Reg) {
  if (isDReg(Reg))
    return Bedrock::POPD;
  if (isAReg(Reg))
    return Bedrock::POPA;
  return 0;
}

static bool isPushOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
  case Bedrock::PUSHD:
  case Bedrock::PUSHA:
  case Bedrock::PUSHM:
    return true;
  }
}

static bool isPopOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return false;
  case Bedrock::POPD:
  case Bedrock::POPA:
  case Bedrock::POPM:
    return true;
  }
}

static bool isPushPopOpcode(unsigned Opcode) {
  return isPushOpcode(Opcode) || isPopOpcode(Opcode);
}

static MachineInstrBuilder
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

static MachineInstrBuilder
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

static void addImplicitSumRegs(MachineInstrBuilder MIB, uint16_t Mask) {
  for (unsigned Bit = 0; Bit != 16; ++Bit) {
    if ((Mask & (uint16_t(1) << Bit)) != 0)
      MIB.addReg(getRegForMaskBit(Bit), RegState::Implicit);
  }
}

static bool isMemoryBaseOperand(const MachineInstr &MI, unsigned OpNo,
                                Register Base, const TargetRegisterInfo &TRI);

struct A32DRemap {
  Register Src;
  Register Dst;
};

static Register remapReg(Register Reg, ArrayRef<A32DRemap> Remaps) {
  for (const A32DRemap &Remap : Remaps)
    if (Reg == Remap.Src)
      return Remap.Dst;
  return Reg;
}

static bool instrHasRegMaskForReg(const MachineInstr &MI, Register Reg,
                                  const TargetRegisterInfo &TRI) {
  for (const MachineOperand &MO : MI.operands())
    if (MO.isRegMask() && MO.clobbersPhysReg(Reg))
      return true;
  return false;
}

static bool isPostMemoryBaseOperand(const MachineInstr &MI, unsigned OpNo,
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

static bool canRemapA32RegInOpcode(const MachineInstr &MI, Register AReg,
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

static bool canRemapA32RegToD(const MachineFunction &MF, Register AReg,
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

static bool physRegUsedInFunction(const MachineFunction &MF, Register Reg,
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

static std::optional<uint16_t> pushPopMaskForInstr(const MachineInstr &MI) {
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

static std::optional<Register>
getSavedDRegFromPushPopInstr(const MachineInstr &MI) {
  std::optional<uint16_t> Mask = pushPopMaskForInstr(MI);
  if (!Mask)
    return std::nullopt;
  return getSavedDReg(*Mask);
}

// These byte counts mirror the generated assembler encodings for the A32->D32
// minsize remap cost model.
static unsigned estimateMov32RegSize(Register Dst, Register Src) {
  return isAReg(Dst) && isAReg(Src) ? 4 : 2;
}

static unsigned estimateMov32ImmSize(Register Reg) {
  return isDReg(Reg) ? 4 : 6;
}

static unsigned estimateMov32MemSize(Register Reg, bool HasExtraAddrWord) {
  unsigned Size = isDReg(Reg) ? 2 : 4;
  return HasExtraAddrWord ? Size + 2 : Size;
}

static unsigned estimateA32BinRegSize(Register Dst, Register RHS) {
  return isDReg(Dst) && isDReg(RHS) ? 2 : 4;
}

static unsigned estimateA32IncDecSize(Register Reg) {
  return isDReg(Reg) ? 2 : 4;
}

static unsigned estimateA32ImmBinSize() { return 6; }

static unsigned estimateA32MinMaxSize() { return 4; }

static unsigned estimateA32CmpRhsSize(Register RHS) {
  return isDReg(RHS) ? 2 : 4;
}

static bool hasExtraAddrWord(const MachineInstr &MI) {
  return MI.getNumOperands() < 3 || !MI.getOperand(2).isImm() ||
         MI.getOperand(2).getImm() != 0;
}

static unsigned estimateMinSizeA32InstSize(const MachineInstr &MI,
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

static unsigned estimateMinSizeA32FunctionSize(const MachineFunction &MF,
                                               ArrayRef<A32DRemap> Remaps,
                                               unsigned ExtraSaveRestoreBytes) {
  unsigned Size = ExtraSaveRestoreBytes;
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB)
      if (!MI.isDebugInstr())
        Size += estimateMinSizeA32InstSize(MI, Remaps);
  return Size;
}

static void remapA32Operands(MachineFunction &MF, ArrayRef<A32DRemap> Remaps) {
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

static unsigned getSumOpcode(unsigned AddOpcode, Register Dst) {
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

static unsigned getMemBitOpcode(unsigned RegBitOpcode) {
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

static unsigned getMovMMOpcode(unsigned LoadOpcode, bool SrcPost,
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

static unsigned getMovMIOpcode(unsigned RegImmOpcode) {
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

static unsigned getMovMROpcodeForImmOpcode(unsigned RegImmOpcode) {
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

static unsigned getRepMovMMOpcode(unsigned Opcode) {
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

static unsigned getRepMovPostMROpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::MOV8postmr:
    return Bedrock::REPMOV8postmr64;
  case Bedrock::MOV32postmr:
    return Bedrock::REPMOV32postmr;
  }
}

static unsigned getMemSourceOpcode(unsigned RegOpcode, bool PostInc) {
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

static unsigned getIndexedMemSourceOpcode(unsigned RegOpcode, unsigned Scale,
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

static bool memSourceFoldSavesSizeWithoutPostInc(unsigned Opcode) {
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

static unsigned getRepPostMemSourceOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case Bedrock::ADD32postrm:
    return Bedrock::REPADD32postrm;
  }
}

static unsigned getMemDestBinOpcode(unsigned RegOpcode) {
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

static unsigned getMemDestImmBinOpcode(unsigned RegOpcode) {
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
    return Bedrock::AND64mi;
  case Bedrock::OR8ri:
    return Bedrock::OR8mi;
  case Bedrock::OR16ri:
    return Bedrock::OR16mi;
  case Bedrock::OR32ri:
    return Bedrock::OR32mi;
  case Bedrock::OR64ri:
    return Bedrock::OR64mi;
  case Bedrock::XOR8ri:
    return Bedrock::XOR8mi;
  case Bedrock::XOR16ri:
    return Bedrock::XOR16mi;
  case Bedrock::XOR32ri:
    return Bedrock::XOR32mi;
  case Bedrock::XOR64ri:
    return Bedrock::XOR64mi;
  }
}

static unsigned getMemImmFlagOpcode(unsigned RegOpcode) {
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

static bool canEncodeMemImmFlagOpcode(unsigned RegOpcode, int64_t Imm) {
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
    return Imm >= 0 && Imm < 64;
  }
}

static bool flagImmOpcodeMatchesLoad(unsigned FlagOpcode, unsigned LoadOpcode) {
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

static unsigned getMemRegFlagOpcode(unsigned FlagOpcode, unsigned LoadOpcode,
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

static unsigned getIndexedMemRegFlagOpcode(unsigned FlagOpcode, unsigned Scale,
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

static unsigned getIndexedMemLoadOpcode(unsigned LoadOpcode, unsigned Scale,
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

static unsigned getIndexedMemStoreOpcode(unsigned StoreOpcode, unsigned Scale,
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

static unsigned getIndexedMemImmFlagOpcode(unsigned FlagOpcode, unsigned Scale,
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

static unsigned getMemTestOpcodeForAndImm(unsigned AndOpcode) {
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
    return Bedrock::TEST64mi;
  }
}

static unsigned getRegTestOpcodeForAndImm(unsigned AndOpcode) {
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
    return Bedrock::TEST64ri;
  }
}

static bool cmpZeroOpcodeMatchesAndImm(unsigned CmpOpcode, unsigned AndOpcode) {
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
    return CmpOpcode == Bedrock::CMP64ri;
  }
}

static bool cmpRROpcodeMatchesAndImm(unsigned CmpOpcode, unsigned AndOpcode) {
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
    return CmpOpcode == Bedrock::CMP64rr;
  }
}

static bool testSelfOpcodeMatchesAndImm(unsigned TestOpcode,
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
    return TestOpcode == Bedrock::TEST64rr;
  }
}

static unsigned getOppositeAddSubMemImmOpcode(unsigned Opcode) {
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

static unsigned getIncDecRegOpcode(unsigned Opcode, int64_t Imm) {
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

static unsigned getIncDecMemOpcode(unsigned Opcode, int64_t Imm) {
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

static bool isCommutableBinOpcode(unsigned Opcode) {
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

static bool binOpcodeMatchesLoad(unsigned BinOpcode, unsigned LoadOpcode) {
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
           BinOpcode == Bedrock::OR64ri || BinOpcode == Bedrock::XOR64ri;
  }
}

static unsigned getMAddMemOpcode(unsigned MulOpcode, bool PostInc) {
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

static unsigned getDivModOpcode(unsigned DivOpcode) {
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

static unsigned getDivModMulOpcode(unsigned DivOpcode) {
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

static unsigned getDivModSubOpcode(unsigned DivOpcode) {
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

static bool isLoadForBitOp(const MachineInstr &MI, Register &Dst,
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

static bool isStoreForBitOp(const MachineInstr &MI, Register Src, Register Base,
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

static bool isMemLoad(const MachineInstr &MI, Register &Dst, Register &Base,
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

static bool isIndexedMemLoad(const MachineInstr &MI, Register &Dst,
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

static bool isMemCopyLoad(const MachineInstr &MI, Register &Dst, Register &Base,
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

static bool isMAddLoad(const MachineInstr &MI, Register &Dst, Register &Base,
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

static bool isMemStore(const MachineInstr &MI, Register &Src, Register &Base,
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

static bool isMemCopyStore(const MachineInstr &MI, Register &Src,
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

static bool isMemoryBaseOperand(const MachineInstr &MI, unsigned OpNo,
                                Register Base, const TargetRegisterInfo &TRI) {
  if (OpNo + 1 >= MI.getNumOperands())
    return false;
  const MachineOperand &MO = MI.getOperand(OpNo);
  const MachineOperand &Next = MI.getOperand(OpNo + 1);
  return MO.isReg() && MO.readsReg() && !MO.isDef() &&
         regsOverlap(TRI, MO.getReg(), Base) && Next.isImm();
}

static bool onlyUsesRegAsMemoryBase(const MachineInstr &MI, Register Base,
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

static void replaceMemoryBase(MachineInstr &MI, Register OldBase,
                              Register NewBase, const TargetRegisterInfo &TRI) {
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    MachineOperand &MO = MI.getOperand(I);
    if (isMemoryBaseOperand(MI, I, OldBase, TRI))
      MO.setReg(NewBase);
  }
}

static std::optional<unsigned> getIndexedMemoryBaseOperandNo(unsigned Opcode) {
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

static bool isIndexedMemoryBaseOperand(const MachineInstr &MI, unsigned OpNo,
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

static bool onlyUsesRegAsAnyMemoryBase(const MachineInstr &MI, Register Base,
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

static void replaceAnyMemoryBase(MachineInstr &MI, Register OldBase,
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

static std::optional<unsigned> getShiftForSmallConstMul(int64_t Value) {
  switch (Value) {
  default:
    return std::nullopt;
  case 3:
    return 1;
  case 5:
    return 2;
  }
}

static bool isMov32Imm(const MachineInstr &MI, Register &Dst, int64_t &Imm) {
  if (MI.getOpcode() != Bedrock::MOV32ri || MI.getNumOperands() < 2 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isImm())
    return false;
  Dst = MI.getOperand(0).getReg();
  Imm = MI.getOperand(1).getImm();
  return true;
}

static bool isMov64Imm(const MachineInstr &MI, Register &Dst, int64_t &Imm) {
  if (MI.getOpcode() != Bedrock::MOV64ri || MI.getNumOperands() < 2 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isImm())
    return false;
  Dst = MI.getOperand(0).getReg();
  Imm = MI.getOperand(1).getImm();
  return true;
}

static bool isMovImmForCmp(const MachineInstr &MI, Register &Dst,
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

static unsigned getProfitableCmpImmOpcode(unsigned MovOpcode,
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
    return MovOpcode == Bedrock::MOV32ri && Imm > 0 && Imm < 64
               ? Bedrock::CMP32ri
               : 0;
  case Bedrock::CMP64rr:
    return MovOpcode == Bedrock::MOV64ri && Imm > 0 && Imm < 64
               ? Bedrock::CMP64ri
               : 0;
  }
}

static bool isStoreSrcCompatible(unsigned Opcode, Register Reg) {
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

static bool isTrackableRegMove(const MachineInstr &MI, Register &Dst,
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

static bool hasOrderedMemOperand(const MachineInstr &MI) {
  for (MachineMemOperand *MMO : MI.memoperands())
    if (MMO->isVolatile() || MMO->isAtomic())
      return true;
  return false;
}

static bool instrHasSPMemOffset(const MachineInstr &MI, int64_t Offset,
                                const TargetRegisterInfo &TRI) {
  if (!MI.mayLoadOrStore())
    return false;
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I)
    if (isMemoryBaseOperand(MI, I, Bedrock::SP, TRI) &&
        MI.getOperand(I + 1).getImm() == Offset)
      return true;
  return false;
}

static bool rangesOverlap(int64_t AOffset, unsigned ASize, int64_t BOffset,
                          unsigned BSize) {
  int64_t AEnd = AOffset + int64_t(ASize);
  int64_t BEnd = BOffset + int64_t(BSize);
  return AOffset < BEnd && BOffset < AEnd;
}

static bool instrHasOverlappingSPMemRange(const MachineInstr &MI,
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

static bool
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

static bool
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

static bool instrPrecedesInBlock(const MachineInstr *A, const MachineInstr *B) {
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

static bool allPredecessorPathsReachBlock(
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

static bool stackConstStoreAvailableAtUse(const StackConstStore &Slot,
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

static bool isPromotableStackSlotAccess(const MachineInstr &MI,
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

static Register findScratchDRegAt(MachineBasicBlock::iterator Insert,
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

static bool
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

static unsigned estimateStackSlotPromotionSavings(
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

static bool collectConsistentPushPopMask(MachineFunction &MF,
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

static void replaceWithPushPopMask(MachineFunction &MF, MachineInstr &PushM,
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

static void extendPushPopMask(MachineFunction &MF, MachineInstr &PushM,
                              ArrayRef<MachineInstr *> PopMs, uint16_t OldMask,
                              uint16_t AddedMask) {
  uint16_t NewMask = OldMask | AddedMask;
  if (NewMask == OldMask)
    return;
  replaceWithPushPopMask(MF, PushM, PopMs, NewMask);
}

static bool
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

static void collectStackConstStores(MachineFunction &MF,
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

static bool hasAvailableStackConstStore(ArrayRef<StackConstStore> Slots,
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

static bool isReplaceableStackConstMul(const StackConstStore &Slot,
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

static void addMaybePostMemOperand(MachineInstrBuilder MIB, Register Base,
                                   int64_t Offset, bool PostInc) {
  MIB.addReg(Base);
  if (!PostInc)
    MIB.addImm(Offset);
}

static bool isBinUsingLoadedValue(const MachineInstr &MI, Register Loaded,
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

static bool isImmBinUsingLoadedValue(const MachineInstr &MI, Register Loaded,
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

static unsigned cmpOpcodeForIncDec(unsigned Opcode) {
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

static bool isIncDecRegInstr(const MachineInstr &MI, Register &Reg,
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

static bool isClrReg(const MachineInstr &MI, Register &Reg) {
  if (MI.getOpcode() != Bedrock::CLR64r || MI.getNumOperands() < 1 ||
      !MI.getOperand(0).isReg())
    return false;
  Reg = MI.getOperand(0).getReg();
  return true;
}

static bool isZeroRegDef(const MachineInstr &MI, Register &Reg) {
  if (isClrReg(MI, Reg))
    return true;
  if (MI.getOpcode() != Bedrock::MOV64ri || MI.getNumOperands() < 2 ||
      !MI.getOperand(0).isReg() || !MI.getOperand(1).isImm() ||
      MI.getOperand(1).getImm() != 0)
    return false;
  Reg = MI.getOperand(0).getReg();
  return true;
}

static MachineInstr *findRemovableClrDefBefore(MachineBasicBlock &MBB,
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

static MachineInstr *findRemovableZeroDefBefore(MachineBasicBlock &MBB,
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

static bool isPlainRetBlock(const MachineBasicBlock &MBB) {
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

static MachineBasicBlock *createZeroReturnBlock(MachineFunction &MF,
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

static std::optional<int64_t> getCondCodeImm(const MachineOperand &MO) {
  if (MO.isImm())
    return MO.getImm();
  if (MO.isCImm())
    return MO.getCImm()->getSExtValue();
  return std::nullopt;
}

static void setCondCodeImm(MachineOperand &MO, int64_t CC) {
  if (MO.isImm())
    MO.setImm(CC);
  else
    MO.ChangeToImmediate(CC);
}

static std::optional<int64_t> invertCondCode(int64_t CC) {
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

static bool isEqNeBranch(const MachineInstr &MI) {
  if (MI.getOpcode() != Bedrock::JCC || MI.getNumOperands() < 2)
    return false;
  std::optional<int64_t> CC = getCondCodeImm(MI.getOperand(1));
  if (!CC)
    return false;
  return *CC == BedrockCC::EQ || *CC == BedrockCC::NE;
}

static unsigned getCondOperandNo(unsigned Opcode) {
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

static MachineBasicBlock::iterator firstNonDebug(MachineBasicBlock &MBB) {
  auto I = MBB.begin();
  while (I != MBB.end() && I->isDebugInstr())
    ++I;
  return I;
}

static MachineOperand *getCondOperand(MachineInstr &MI) {
  unsigned OpNo = getCondOperandNo(MI.getOpcode());
  if (OpNo >= MI.getNumOperands())
    return nullptr;
  return &MI.getOperand(OpNo);
}

static std::optional<int64_t> mapZeroCmpCondToTest(int64_t CC, bool ZeroIsLHS) {
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

static unsigned getMinMaxOpcodeForCmp(unsigned CmpOpcode, int64_t CC,
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

static bool minMaxOpcodeCanUseRegs(unsigned Opcode, Register Dst,
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

static unsigned getDJccOpcodeForDec(unsigned DecOpcode) {
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

static unsigned getIJccOpcodeForIncCmp(unsigned IncOpcode, unsigned CmpOpcode) {
  switch (IncOpcode) {
  default:
    return 0;
  case Bedrock::INC32r:
    return CmpOpcode == Bedrock::CMP32rr ? Bedrock::IJCC32r : 0;
  case Bedrock::INC64r:
    return CmpOpcode == Bedrock::CMP64rr ? Bedrock::IJCC64r : 0;
  }
}

static bool blockHasLiveInReg(const MachineBasicBlock &MBB, Register Reg,
                              const TargetRegisterInfo &TRI);

static std::optional<int64_t> getConstDefForReg(const MachineInstr &MI,
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

static MachineInstr *findLastConstDefBefore(MachineInstr &Use, Register Reg,
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

static MachineInstr *findLastConstDefBeforeAny(MachineInstr &Use, Register Reg,
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

static void removeRegLiveInsWithoutUses(MachineFunction &MF, Register Reg,
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

static bool regUnusedAfterInstrInLayout(MachineInstr &Def, Register Reg,
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

static bool replaceZeroRegCopiesAfterDef(MachineInstr &ZeroDef, Register Reg,
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

static MachineInstr *findLastDefBefore(MachineInstr &Use, Register Reg,
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

static bool onlyUsesRegAsI32Before(MachineInstr &Def, MachineInstr &Limit,
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

static bool instrOnlyUsesRegAsI32Count(const MachineInstr &MI, Register Reg,
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

static bool onlyUsedAsI32LoopCountAfter(MachineInstr &Def, Register Reg,
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

static void clearKnownZeroReg(uint32_t &Mask, Register Reg,
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

static bool maskHasKnownZeroReg(uint32_t Mask, Register Reg,
                                const TargetRegisterInfo &TRI) {
  for (unsigned Bit = 0; Bit != 16; ++Bit)
    if ((Mask & (1u << Bit)) && regsOverlap(TRI, Reg, getRegForMaskBit(Bit)))
      return true;
  return false;
}

static bool isKnownZeroCmpForReg(const MachineInstr &MI, Register Reg,
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

static bool isIntCmpOpcode(unsigned Opcode) {
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

static unsigned getTestOpcodeForCmp(unsigned Opcode) {
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

static Register preferredKnownZeroReg(uint32_t KnownZero,
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

static void transferKnownZero(const MachineInstr &MI, uint32_t &Mask,
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

static DenseMap<MachineBasicBlock *, uint32_t>
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

static bool isIdentityMove(const MachineInstr &MI,
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

bool BedrockPushPopMerge::foldIdentityMoves(MachineBasicBlock &MBB,
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

namespace {
struct TruncAnd32Arm {
  MachineInstr *Trunc = nullptr;
  MachineInstr *And = nullptr;
  Register Src;
  Register Tmp;
  int64_t Imm = 0;
};
} // end anonymous namespace

static bool matchTruncAnd32(MachineInstr &Trunc, MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldArgTruncBitOps(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldCompactUnary(MachineBasicBlock &MBB,
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
    case Bedrock::MOV8ri:
    case Bedrock::MOV16ri:
    case Bedrock::MOV32ri:
    case Bedrock::MOV64ri:
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
          uint64_t(MI.getOperand(2).getImm()) == 0xffffffffULL &&
          regsOverlap(TRI, MI.getOperand(0).getReg(),
                      MI.getOperand(1).getReg())) {
        Register Dst = MI.getOperand(0).getReg();
        Register Src = MI.getOperand(1).getReg();
        BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(),
                TII.get(Bedrock::EXTZQ32rr), Dst)
            .addReg(Src, getKillRegState(operandIsKill(MI, Src, TRI)));
        MI.eraseFromParent();
        I = MBB.begin();
        Changed = true;
        continue;
      }
      break;
    }

    if (MI.getNumOperands() >= 3 && MI.getOperand(0).isReg() &&
        MI.getOperand(1).isReg() && MI.getOperand(2).isImm()) {
      Register Dst = MI.getOperand(0).getReg();
      Register Src = MI.getOperand(1).getReg();
      int64_t Imm = MI.getOperand(2).getImm();
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
          uint64_t(Imm) == 0xffffffff00000001ULL &&
          regsOverlap(TRI, Dst, Src)) {
        auto ExtI = nextNonDebug(MI.getIterator(), MBB);
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

bool BedrockPushPopMerge::foldDecCmpBranch(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldCountedBranchShortcuts(
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

bool BedrockPushPopMerge::foldTopTestIncLoopToIJcc(MachineFunction &MF) const {
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

static bool matchEqImmCompareBlock(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldSequentialEqImmCompareChain(
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

bool BedrockPushPopMerge::foldKnownZeroCmp(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldEqNeZeroCmpToTest(MachineBasicBlock &MBB,
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
    if (!isDReg(Tested)) {
      transferKnownZero(MI, KnownZero, TRI);
      ++I;
      continue;
    }
    BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(), TII.get(TestOpcode))
        .addReg(Tested)
        .addReg(Tested);
    MI.eraseFromParent();
    I = MBB.begin();
    KnownZero = KnownZeroIn;
    Changed = true;
  }

  return Changed;
}

bool BedrockPushPopMerge::foldClrZeroCmpToTest(MachineBasicBlock &MBB,
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
    if (!isDReg(Tested)) {
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

    BuildMI(MBB, CmpI->getIterator(), CmpI->getDebugLoc(), TII.get(TestOpcode))
        .addReg(Tested)
        .addReg(Tested);
    setCondCodeImm(*CondOp, *NewCC);
    CmpI->eraseFromParent();
    Clr.eraseFromParent();
    I = MBB.begin();
    Changed = true;
  }

  return Changed;
}

static unsigned getMemImmCmpOpcodeForMemRegCmp(unsigned Opcode) {
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

bool BedrockPushPopMerge::foldAndTestToImmTest(MachineBasicBlock &MBB,
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
    if (Mask <= 0 || Mask >= 64 || !regsOverlap(TRI, AndDst, AndSrc)) {
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

bool BedrockPushPopMerge::foldDeadClrs(MachineFunction &MF) const {
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

static bool isReturnValueReg(Register Reg, const TargetRegisterInfo &TRI) {
  return regsOverlap(TRI, Reg, Bedrock::D0) ||
         regsOverlap(TRI, Reg, Bedrock::D1) ||
         regsOverlap(TRI, Reg, Bedrock::A0) ||
         regsOverlap(TRI, Reg, Bedrock::F0);
}

static bool isPlainDeadDefCandidate(const MachineInstr &MI, Register &Reg) {
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

bool BedrockPushPopMerge::foldDeadPlainDefs(MachineFunction &MF) const {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  bool HasReturnValue = !MF.getFunction().getReturnType()->isVoidTy();

  for (MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(); I != MBB.end();) {
      MachineInstr &MI = *I++;
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

bool BedrockPushPopMerge::foldZeroRegCopies(MachineFunction &MF) const {
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

static bool findCountedLoopHeaderInfo(MachineBasicBlock &Header,
                                      uint32_t HeaderKnownZero,
                                      Register &CountReg,
                                      const TargetRegisterInfo &TRI);
static bool isUnaryRegOp(const MachineInstr &MI, unsigned Opcode, Register Reg,
                         const TargetRegisterInfo &TRI);

bool BedrockPushPopMerge::foldTopTestCountedLoop(
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

bool BedrockPushPopMerge::foldPositiveCountedLoopPretest(
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

namespace {
struct ExitCopyInfo {
  MachineBasicBlock *MBB = nullptr;
  MachineInstr *Branch = nullptr;
  MachineInstr *Copy = nullptr;
  Register Source;
};
} // end anonymous namespace

static MachineInstr *findJccTo(MachineBasicBlock &MBB,
                               MachineBasicBlock &Target) {
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end() || I->getOpcode() != Bedrock::JCC ||
      I->getNumOperands() < 2 || !I->getOperand(0).isMBB() ||
      I->getOperand(0).getMBB() != &Target)
    return nullptr;
  return &*I;
}

static MachineInstr *findCopyToBefore(MachineInstr &Before, Register Dst,
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

static bool findCountedLoopHeaderInfo(MachineBasicBlock &Header,
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

static bool isUnaryRegOp(const MachineInstr &MI, unsigned Opcode, Register Reg,
                         const TargetRegisterInfo &TRI) {
  return MI.getOpcode() == Opcode && MI.getNumOperands() >= 2 &&
         MI.getOperand(0).isReg() && MI.getOperand(1).isReg() &&
         regsOverlap(TRI, MI.getOperand(0).getReg(), Reg) &&
         regsOverlap(TRI, MI.getOperand(1).getReg(), Reg);
}

static bool flagsDeadOrUsedByEqNeBranch(MachineInstr &MI,
                                        const TargetRegisterInfo &TRI) {
  MachineBasicBlock &MBB = *MI.getParent();
  auto Next = nextNonDebug(MI.getIterator(), MBB);
  if (regDeadAfterInCFG(Next, MBB, Bedrock::FLAGS, TRI))
    return true;
  return Next != MBB.end() && isEqNeBranch(*Next);
}

static std::optional<unsigned>
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

static bool isRepeatCountUse(const MachineInstr &MI, Register Count,
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

static bool blockHasLiveInReg(const MachineBasicBlock &MBB, Register Reg,
                              const TargetRegisterInfo &TRI) {
  for (const MachineBasicBlock::RegisterMaskPair &LiveIn : MBB.liveins())
    if (regsOverlap(TRI, Register(LiveIn.PhysReg), Reg))
      return true;
  return false;
}

static bool
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

static bool
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

static bool loopOnlyTouchesCountAt(ArrayRef<MachineBasicBlock *> Blocks,
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

bool BedrockPushPopMerge::foldPositiveCountedLoopHeaderPretest(
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

bool BedrockPushPopMerge::foldSMaxCountdownPretest(
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

bool BedrockPushPopMerge::foldSMaxPretestZeroReturn(MachineFunction &MF) const {
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

bool BedrockPushPopMerge::foldSMaxIndexLoopBound(MachineFunction &MF) const {
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

static bool findCountedLoopLatch(MachineBasicBlock &Header, Register CountReg,
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

static bool findCountedLoopJccLatch(MachineBasicBlock &Header,
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

static bool validateCountAndIndexDefs(MachineFunction &MF, Register CountReg,
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

bool BedrockPushPopMerge::foldCountedLoopIndexResult(
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

bool BedrockPushPopMerge::foldNarrowLoopCountCopies(MachineFunction &MF) const {
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

bool BedrockPushPopMerge::foldSelfZextI32LoopCounts(MachineFunction &MF) const {
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

bool BedrockPushPopMerge::foldFallthroughJumps(MachineBasicBlock &MBB,
                                               MachineFunction &MF) const {
  MachineBasicBlock::iterator JmpI = MBB.getLastNonDebugInstr();
  if (JmpI == MBB.end() || JmpI->getOpcode() != Bedrock::JMP ||
      JmpI->getNumOperands() == 0 || !JmpI->getOperand(0).isMBB())
    return false;

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
    SmallVector<MachineBasicBlock *, 4> Succs(MBB.successors());
    for (MachineBasicBlock *Succ : Succs)
      MBB.removeSuccessor(Succ);
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
  return true;
}

bool BedrockPushPopMerge::foldCondZextAddToInc(MachineFunction &MF) const {
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

bool BedrockPushPopMerge::foldMinSizeLoopCounter32(MachineFunction &MF) const {
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

bool BedrockPushPopMerge::foldPrologue(MachineFunction &MF) const {
  if (MF.empty())
    return false;

  MachineBasicBlock &MBB = MF.front();
  auto I = MBB.begin();
  while (I != MBB.end() && I->isDebugInstr())
    ++I;
  if (I == MBB.end())
    return false;

  int64_t Total = 0;
  if (!isStackAdjust(*I, Bedrock::SUB64ri, Total))
    return false;
  MachineInstr &Sub = *I;

  SmallVector<MachineInstr *, 8> Stores;
  uint16_t Mask = 0;
  for (++I; I != MBB.end(); ++I) {
    if (I->isDebugInstr())
      continue;
    Register Reg;
    int64_t Offset = 0;
    if (!isCalleeSaveStore(*I, Reg, Offset))
      break;
    std::optional<unsigned> Bit = getMaskBit(Reg);
    if (!Bit || (Mask & (uint16_t(1) << *Bit)) != 0)
      return false;
    Mask |= uint16_t(1) << *Bit;
    Stores.push_back(&*I);
  }

  unsigned MinSavedRegs = MF.getFunction().hasMinSize() ? 1 : 2;
  if (Stores.size() < MinSavedRegs || int64_t(Stores.size()) * 8 > Total)
    return false;

  auto RecomputeValidMask = [&]() -> std::optional<uint16_t> {
    uint16_t CandidateMask = 0;
    for (MachineInstr *Store : Stores) {
      Register Reg;
      int64_t Offset = 0;
      if (!isCalleeSaveStore(*Store, Reg, Offset))
        return std::nullopt;
      std::optional<unsigned> Bit = getMaskBit(Reg);
      if (!Bit || (CandidateMask & (uint16_t(1) << *Bit)) != 0)
        return std::nullopt;
      CandidateMask |= uint16_t(1) << *Bit;
    }
    if (Stores.size() < MinSavedRegs || int64_t(Stores.size()) * 8 > Total)
      return std::nullopt;
    for (MachineInstr *Store : Stores) {
      Register Reg;
      int64_t Offset = 0;
      if (!isCalleeSaveStore(*Store, Reg, Offset) ||
          !expectedStoreOffset(CandidateMask, Reg, Total, Offset))
        return std::nullopt;
    }
    return CandidateMask;
  };

  while (Stores.size() >= MinSavedRegs) {
    if (std::optional<uint16_t> CandidateMask = RecomputeValidMask()) {
      Mask = *CandidateMask;
      break;
    }
    Stores.pop_back();
  }
  if (Stores.size() < MinSavedRegs)
    return false;

  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  DebugLoc DL = Sub.getDebugLoc();
  MachineInstrBuilder Push =
      buildPushForMask(MBB, Sub.getIterator(), DL, TII, Mask);
  Push.setMIFlag(MachineInstr::FrameSetup);

  int64_t Residual = Total - int64_t(Stores.size()) * 8;
  if (Residual == 0) {
    Sub.eraseFromParent();
  } else {
    Sub.getOperand(2).setImm(Residual);
  }
  for (MachineInstr *Store : Stores)
    Store->eraseFromParent();
  return true;
}

bool BedrockPushPopMerge::foldEpilogue(MachineBasicBlock &MBB,
                                       MachineFunction &MF) const {
  MachineBasicBlock::iterator RetI = MBB.getLastNonDebugInstr();
  if (RetI == MBB.end() || RetI->getOpcode() != Bedrock::RET)
    return false;
  MachineInstr *Ret = &*RetI;

  MachineInstr *Add = nullptr;
  for (auto I = Ret->getIterator(); I != MBB.begin();) {
    --I;
    if (I->isDebugInstr())
      continue;
    Add = &*I;
    break;
  }
  if (!Add)
    return false;

  int64_t Total = 0;
  if (!isStackAdjust(*Add, Bedrock::ADD64ri, Total))
    return false;

  SmallVector<MachineInstr *, 8> Loads;
  uint16_t Mask = 0;
  for (auto I = Add->getIterator(); I != MBB.begin();) {
    --I;
    if (I->isDebugInstr())
      continue;
    Register Reg;
    int64_t Offset = 0;
    if (!isCalleeSaveLoad(*I, Reg, Offset))
      break;
    std::optional<unsigned> Bit = getMaskBit(Reg);
    if (!Bit || (Mask & (uint16_t(1) << *Bit)) != 0)
      return false;
    Mask |= uint16_t(1) << *Bit;
    Loads.push_back(&*I);
  }

  unsigned MinSavedRegs = MF.getFunction().hasMinSize() ? 1 : 2;
  if (Loads.size() < MinSavedRegs || int64_t(Loads.size()) * 8 > Total)
    return false;
  for (MachineInstr *Load : Loads) {
    Register Reg;
    int64_t Offset = 0;
    if (!isCalleeSaveLoad(*Load, Reg, Offset) ||
        !expectedStoreOffset(Mask, Reg, Total, Offset))
      return false;
  }

  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());
  DebugLoc DL = Add->getDebugLoc();
  int64_t Residual = Total - int64_t(Loads.size()) * 8;
  if (Residual == 0) {
    MachineInstrBuilder Pop =
        buildPopForMask(MBB, Add->getIterator(), DL, TII, Mask);
    Pop.setMIFlag(MachineInstr::FrameDestroy);
    Add->eraseFromParent();
  } else {
    Add->getOperand(2).setImm(Residual);
    MachineInstrBuilder Pop =
        buildPopForMask(MBB, std::next(Add->getIterator()), DL, TII, Mask);
    Pop.setMIFlag(MachineInstr::FrameDestroy);
  }

  for (MachineInstr *Load : Loads)
    Load->eraseFromParent();
  return true;
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
      if (EncImm < 0 || EncImm >= 64) {
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
      if ((IsCmpZero || IsCmpKnownZero || IsTestSelf) && Mask >= 0 &&
          Mask < 64 && regsOverlap(TRI, AndDst, AndLHS) &&
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

bool BedrockPushPopMerge::foldImmCmp(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldCmpOneBranch(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldImmMul(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldEntryLiveInStores(MachineFunction &MF) const {
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

bool BedrockPushPopMerge::foldStackStoreLoadForward(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldStackPointerCopyMemBase(
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

bool BedrockPushPopMerge::foldA6BaseCopyStackSpill(MachineFunction &MF) const {
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

bool BedrockPushPopMerge::foldStackSlotsToARegs(MachineFunction &MF) const {
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

bool BedrockPushPopMerge::foldStackReloadFromZextCount(
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

bool BedrockPushPopMerge::foldStackConstLoads(MachineFunction &MF) const {
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

static unsigned getTestOpcodeForZeroStackCmp(unsigned Opcode) {
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

static bool isZeroStackCmpUse(const MachineInstr &MI,
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

static bool hasEqNeBranchBeforeFlagsClobber(MachineInstr &FlagDef,
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

static bool isZeroStackLoadCmpUse(MachineInstr &Load,
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

bool BedrockPushPopMerge::foldStackZeroCmp(MachineFunction &MF) const {
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

bool BedrockPushPopMerge::foldSmallConstMultiply(MachineFunction &MF) const {
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

bool BedrockPushPopMerge::foldMinSizeDivmodConstAccumulate(
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

bool BedrockPushPopMerge::foldDeadFrameTopPadding(MachineFunction &MF) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  if (MFI.hasVarSizedObjects() || MFI.hasOpaqueSPAdjustment())
    return false;

  MachineInstr *Sub = nullptr;
  SmallVector<MachineInstr *, 4> Adds;
  int64_t Amount = 0;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;

      int64_t Adjust = 0;
      if (isStackAdjust(MI, Bedrock::SUB64ri, Adjust)) {
        if (!MI.getFlag(MachineInstr::FrameSetup) || Sub ||
            MI.getParent() != &MF.front())
          return false;
        Sub = &MI;
        Amount = Adjust;
        continue;
      }
      if (isStackAdjust(MI, Bedrock::ADD64ri, Adjust)) {
        if (!MI.getFlag(MachineInstr::FrameDestroy))
          return false;
        Adds.push_back(&MI);
        continue;
      }
    }
  }

  if (!Sub || Adds.empty() || Amount <= 0)
    return false;

  SmallPtrSet<MachineInstr *, 8> Ignored;
  Ignored.insert(Sub);
  for (MachineInstr *Add : Adds) {
    int64_t AddAmount = 0;
    if (!isStackAdjust(*Add, Bedrock::ADD64ri, AddAmount) ||
        AddAmount != Amount)
      return false;
    Ignored.insert(Add);
  }

  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  uint64_t MaxSPMemEnd = 0;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || Ignored.contains(&MI))
        continue;

      unsigned AccessSize = memSizeForOpcode(MI.getOpcode());
      for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
        const MachineOperand &MO = MI.getOperand(I);
        if (!MO.isReg() || !regsOverlap(TRI, MO.getReg(), Bedrock::SP))
          continue;
        if (MO.isImplicit())
          continue;
        if (!isMemoryBaseOperand(MI, I, Bedrock::SP, TRI))
          return false;
        if (AccessSize == 0)
          return false;
        int64_t Offset = MI.getOperand(I + 1).getImm();
        if (Offset < 0)
          return false;
        MaxSPMemEnd =
            std::max(MaxSPMemEnd, uint64_t(Offset) + uint64_t(AccessSize));
      }
    }
  }

  uint64_t Needed = std::max<uint64_t>(MaxSPMemEnd, MFI.getMaxCallFrameSize());
  Align StackAlign = Align(16);
  if (const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering())
    StackAlign = TFI->getStackAlign();
  if (MFI.hasCalls())
    Needed = alignTo(Needed + 8, StackAlign) - 8;
  else
    Needed = alignTo(Needed, StackAlign);

  if (Needed >= uint64_t(Amount))
    return false;

  if (Needed == 0) {
    Sub->eraseFromParent();
    for (MachineInstr *Add : Adds)
      Add->eraseFromParent();
    return true;
  }

  Sub->getOperand(2).setImm(Needed);
  for (MachineInstr *Add : Adds)
    Add->getOperand(2).setImm(Needed);
  return true;
}

bool BedrockPushPopMerge::foldDeadStackAdjust(MachineFunction &MF) const {
  SmallVector<MachineInstr *, 2> Subs;
  SmallVector<MachineInstr *, 4> Adds;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;

      int64_t Amount = 0;
      if (isStackAdjust(MI, Bedrock::SUB64ri, Amount) &&
          MI.getFlag(MachineInstr::FrameSetup)) {
        Subs.push_back(&MI);
        continue;
      }
      if (isStackAdjust(MI, Bedrock::ADD64ri, Amount) &&
          MI.getFlag(MachineInstr::FrameDestroy)) {
        Adds.push_back(&MI);
        continue;
      }
    }
  }

  if (Subs.size() != 1 || Adds.empty() || Subs[0]->getParent() != &MF.front())
    return false;

  int64_t Amount = 0;
  if (!isStackAdjust(*Subs[0], Bedrock::SUB64ri, Amount) || Amount == 0)
    return false;

  SmallPtrSet<MachineInstr *, 8> Ignored;
  Ignored.insert(Subs[0]);
  for (MachineInstr *Add : Adds) {
    int64_t AddAmount = 0;
    if (!isStackAdjust(*Add, Bedrock::ADD64ri, AddAmount) ||
        AddAmount != Amount)
      return false;
    Ignored.insert(Add);
  }

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || Ignored.contains(&MI))
        continue;

      for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
        const MachineOperand &MO = MI.getOperand(I);
        if (!MO.isReg() || MO.isImplicit() ||
            !regsOverlap(TRI, MO.getReg(), Bedrock::SP))
          continue;
        return false;
      }
    }
  }

  for (MachineInstr *Add : Adds)
    Add->eraseFromParent();
  Subs[0]->eraseFromParent();
  return true;
}

bool BedrockPushPopMerge::foldTailCallReturn(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldLoopCarriedLoadUpdateCopies(
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

bool BedrockPushPopMerge::foldLoopCarriedLoadAddCopies(
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

static bool isCopyRegToReg(const MachineInstr &MI, Register Dst, Register Src,
                           const TargetRegisterInfo &TRI) {
  if ((MI.getOpcode() != Bedrock::MOV32rr &&
       MI.getOpcode() != Bedrock::MOV64rr) ||
      MI.getNumOperands() < 2 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg())
    return false;
  return regsOverlap(TRI, MI.getOperand(0).getReg(), Dst) &&
         regsOverlap(TRI, MI.getOperand(1).getReg(), Src);
}

static bool canRewriteProductAccumInstr(const MachineInstr &MI,
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

static void replaceRegOperandUsesAndDefs(MachineInstr &MI, Register OldReg,
                                         Register NewReg,
                                         const TargetRegisterInfo &TRI) {
  for (MachineOperand &MO : MI.operands())
    if (MO.isReg() && regsOverlap(TRI, MO.getReg(), OldReg))
      MO.setReg(NewReg);
}

bool BedrockPushPopMerge::foldRegMAddAccumulator(MachineBasicBlock &MBB,
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

static bool canRewriteExplicitRegTouches(const MachineInstr &MI, Register Reg,
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

bool BedrockPushPopMerge::foldAccumulatorLoopUseInputCount(
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

static bool matchMAddProductTerm(MachineBasicBlock::iterator LoadI,
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

static Register findUnusedCallerDReg(const MachineFunction &MF,
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

bool BedrockPushPopMerge::foldProductChainMAdd(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldShiftedBSet(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::shrinkUnusedPushPopMask(MachineFunction &MF) const {
  MachineInstr *PushM = nullptr;
  SmallVector<MachineInstr *, 4> PopMs;
  uint16_t Mask = 0;
  if (!collectConsistentPushPopMask(MF, PushM, PopMs, Mask))
    return false;

  SmallPtrSet<const MachineInstr *, 8> Ignored;
  Ignored.insert(PushM);
  for (MachineInstr *PopM : PopMs)
    Ignored.insert(PopM);

  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  uint16_t NewMask = Mask;
  for (unsigned Bit = 0; Bit != 16; ++Bit) {
    uint16_t BitMask = uint16_t(1) << Bit;
    if ((Mask & BitMask) == 0)
      continue;
    if (!regTouchedOutsideInstrs(MF, getRegForMaskBit(Bit), Ignored, TRI))
      NewMask &= ~BitMask;
  }

  if (NewMask == Mask)
    return false;

  if (NewMask == 0) {
    for (MachineInstr *PopM : PopMs)
      PopM->eraseFromParent();
    PushM->eraseFromParent();
    return true;
  }

  uint16_t RemovedMask = Mask & ~NewMask;
  replaceWithPushPopMask(MF, *PushM, PopMs, NewMask);
  for (unsigned Bit = 0; Bit != 16; ++Bit)
    if ((RemovedMask & (uint16_t(1) << Bit)) != 0)
      removeRegLiveInsWithoutUses(MF, getRegForMaskBit(Bit), TRI);
  return true;
}

bool BedrockPushPopMerge::foldDivMod(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldStackBaseLeaOffsets(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldStackBaseBiasOriginalUses(
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

bool BedrockPushPopMerge::foldRepeatedStackAddressLeas(
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

bool BedrockPushPopMerge::foldRepeatedStackAddressLeasWithBorrowedBase(
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

bool BedrockPushPopMerge::foldRepeatedStackAddressLeasWithScopedBase(
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

bool BedrockPushPopMerge::foldSum(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldSumReturnCopy(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldSignedClampReturn(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldMinMaxBranchDiamond(MachineFunction &MF) const {
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

bool BedrockPushPopMerge::foldMinSizeA32ToCalleeSavedDRegs(
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

static MachineBasicBlock *findSingleNonSelfPredecessor(MachineBasicBlock &MBB) {
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

static bool isPositiveCountPretest(MachineBasicBlock &Header,
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

static bool isZeroExitCountPretest(MachineBasicBlock &Header,
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

static bool findPositiveConstDefInBlockBefore(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::foldPositiveConstRepPretests(
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

bool BedrockPushPopMerge::foldMinSizeMAddWindowBaseBias(
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

static bool buildRepMovMM(MachineBasicBlock &MBB, MachineInstr &BodyMI,
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

static bool buildRepPostMemStore(MachineBasicBlock &MBB, MachineInstr &BodyMI,
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

static bool buildRepPostMemSource(MachineBasicBlock &MBB, MachineInstr &BodyMI,
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

static MachineInstr *findLeaDefBefore(MachineInstr &Use, Register Reg,
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

static Register findScratchARegForLoop(MachineBasicBlock &Header,
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

static Register findScratchDRegForLoop(MachineBasicBlock &Header,
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

static MachineInstr *findConstStackStoreForLoad(MachineInstr &Load,
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

static bool findConstOrStackLoadBefore(MachineInstr &Use, Register Reg,
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

bool BedrockPushPopMerge::foldAscendingStoreProgressionLoops(
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

bool BedrockPushPopMerge::foldAscendingAddressStoreLoops(
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

bool BedrockPushPopMerge::foldAscendingConstStoreLoops(
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

bool BedrockPushPopMerge::foldAscendingMultiStoreLoops(
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

bool BedrockPushPopMerge::foldMixedZeroStoreLoops(MachineFunction &MF) const {
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

bool BedrockPushPopMerge::foldByteZeroOffsetStoreLoops(
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

bool BedrockPushPopMerge::foldByteOffsetStoreLoops(MachineFunction &MF) const {
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

bool BedrockPushPopMerge::foldSingleInstructionRepLoops(
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

bool BedrockPushPopMerge::foldWideZeroFillDjtLoops(MachineFunction &MF) const {
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

bool BedrockPushPopMerge::foldZeroExitRepgTailLoops(MachineFunction &MF) const {
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

bool BedrockPushPopMerge::foldAscendingLoadProgressionLoops(
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

static void removeAllSuccessors(MachineBasicBlock &MBB) {
  SmallVector<MachineBasicBlock *, 4> Succs(MBB.successors());
  for (MachineBasicBlock *Succ : Succs)
    MBB.removeSuccessor(Succ);
}

static void eraseAllNonDebugInstrs(MachineBasicBlock &MBB) {
  for (auto I = MBB.begin(); I != MBB.end();) {
    if (I->isDebugInstr()) {
      ++I;
      continue;
    }
    MachineInstr *MI = &*I++;
    MI->eraseFromParent();
  }
}

static MachineInstr *findLeadingZeroRegDef(MachineBasicBlock &MBB, Register Reg,
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

static MachineBasicBlock *
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

bool BedrockPushPopMerge::foldByteIndexedMemUtilityLoops(
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

bool BedrockPushPopMerge::foldScanUntilZeroRepne(MachineFunction &MF) const {
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

bool BedrockPushPopMerge::normalizeA32Arithmetic(MachineBasicBlock &MBB,
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

bool BedrockPushPopMerge::legalizeLargeStackAdjustments(
    MachineFunction &MF) const {
  bool Changed = false;
  const BedrockInstrInfo &TII =
      *static_cast<const BedrockInstrInfo *>(MF.getSubtarget().getInstrInfo());

  auto EmitChunked = [&](MachineBasicBlock &MBB, MachineInstr &MI,
                         unsigned Opcode, uint64_t Amount) {
    DebugLoc DL = MI.getDebugLoc();
    unsigned Flags = MI.getFlags();
    MachineBasicBlock::iterator Insert = MI.getIterator();
    while (Amount != 0) {
      uint64_t Chunk = std::min<uint64_t>(Amount, 63);
      BuildMI(MBB, Insert, DL, TII.get(Opcode), Bedrock::SP)
          .addReg(Bedrock::SP)
          .addImm(Chunk)
          .setMIFlags(Flags);
      Amount -= Chunk;
    }
    MI.eraseFromParent();
  };

  auto EmitScratch = [&](MachineBasicBlock &MBB, MachineInstr &MI,
                         unsigned RegOpcode, Register Scratch,
                         uint64_t Amount) {
    DebugLoc DL = MI.getDebugLoc();
    unsigned Flags = MI.getFlags();
    MachineBasicBlock::iterator Insert = MI.getIterator();
    BuildMI(MBB, Insert, DL, TII.get(Bedrock::MOV32ri), Scratch)
        .addImm(Amount)
        .setMIFlags(Flags);
    BuildMI(MBB, Insert, DL, TII.get(RegOpcode), Bedrock::SP)
        .addReg(Bedrock::SP)
        .addReg(Scratch)
        .setMIFlags(Flags);
    MI.eraseFromParent();
  };

  for (MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(); I != MBB.end();) {
      MachineInstr &MI = *I++;
      int64_t Amount = 0;
      bool IsSub = isStackAdjust(MI, Bedrock::SUB64ri, Amount);
      bool IsAdd = !IsSub && isStackAdjust(MI, Bedrock::ADD64ri, Amount);
      if ((!IsSub && !IsAdd) || fitsImm6(Amount))
        continue;

      std::optional<Register> Scratch;
      if (IsSub) {
        MachineBasicBlock::iterator Prev = prevNonDebug(MI.getIterator(), MBB);
        if (Prev != MBB.end() && isPushOpcode(Prev->getOpcode()))
          Scratch = getSavedDRegFromPushPopInstr(*Prev);
      } else {
        MachineBasicBlock::iterator Next = nextNonDebug(MI.getIterator(), MBB);
        if (Next != MBB.end() && isPopOpcode(Next->getOpcode()))
          Scratch = getSavedDRegFromPushPopInstr(*Next);
      }

      if (Scratch && Amount <= std::numeric_limits<int32_t>::max()) {
        EmitScratch(MBB, MI, IsSub ? Bedrock::SUB64rr : Bedrock::ADD64rr,
                    *Scratch, Amount);
      } else {
        EmitChunked(MBB, MI, IsSub ? Bedrock::SUB64ri : Bedrock::ADD64ri,
                    Amount);
      }
      Changed = true;
    }
  }

  return Changed;
}

bool BedrockPushPopMerge::runOnMachineFunction(MachineFunction &MF) {
  bool OptNone = MF.getFunction().hasOptNone();
  bool EnableO1 =
      !OptNone && profileAtLeast(Profile, BedrockPeepholeProfile::O1);
  bool EnableO2 =
      !OptNone && profileAtLeast(Profile, BedrockPeepholeProfile::O2);
  bool EnableO2OrSize = EnableO2 || (!OptNone && MF.getFunction().hasMinSize());
  bool Changed = false;
  if (EnableO1)
    Changed |= foldPrologue(MF);
  Changed |= foldEntryLiveInStores(MF);
  SmallVector<MachineBasicBlock *, 8> Blocks;
  for (MachineBasicBlock &MBB : MF)
    Blocks.push_back(&MBB);
  if (EnableO2OrSize)
    Changed |= foldStackZeroCmp(MF);
  bool EnableSizeSum =
      !OptNone && (profileAtLeast(Profile, BedrockPeepholeProfile::O3) ||
                   MF.getFunction().hasMinSize());
  for (MachineBasicBlock *MBB : Blocks) {
    Changed |= foldIdentityMoves(*MBB, MF);
    if (EnableO1)
      Changed |= foldArgTruncBitOps(*MBB, MF);
    Changed |= foldMemCopy(*MBB, MF);
    Changed |= foldStackPointerCopyMemBase(*MBB, MF);
    if (EnableO1)
      Changed |= foldLoadOp(*MBB, MF);
    Changed |= foldMemoryBinStore(*MBB, MF);
    Changed |= foldMemoryBinStoreAcrossDef(*MBB, MF);
    Changed |= foldMemoryBinStoreWithLoadedSource(*MBB, MF);
    Changed |= foldMemoryImmBinStore(*MBB, MF);
    if (EnableO1)
      Changed |= foldByteLoadTestZeroBranch(*MBB, MF);
    Changed |= foldMemoryImmFlagOp(*MBB, MF);
    if (EnableO1)
      Changed |= foldAndTestToImmTest(*MBB, MF);
    Changed |= foldImmCmp(*MBB, MF);
    if (EnableO1)
      Changed |= foldCmpOneBranch(*MBB, MF);
    Changed |= foldImmMul(*MBB, MF);
    Changed |= foldImmStore(*MBB, MF);
    Changed |= foldPostInc(*MBB, MF);
    Changed |= foldCompactUnary(*MBB, MF);
    if (!OptNone && MF.getFunction().hasMinSize())
      Changed |= foldAImmCopyToDImm(*MBB, MF);
    Changed |= foldClrStore(*MBB, MF);
  }

  DenseMap<MachineBasicBlock *, uint32_t> KnownZeroIns;
  if (EnableO1) {
    const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
    KnownZeroIns = computeKnownZeroIns(MF, TRI);
  }

  for (MachineBasicBlock *MBB : Blocks) {
    if (EnableO1) {
      Changed |= foldKnownZeroCmp(*MBB, MF, KnownZeroIns.lookup(MBB));
      Changed |= foldDecCmpBranch(*MBB, MF, KnownZeroIns.lookup(MBB));
      if (!EnableO2OrSize)
        Changed |= foldCountedBranchShortcuts(*MBB, MF);
      Changed |= foldTopTestCountedLoop(*MBB, MF, KnownZeroIns);
    }
    Changed |= foldLea(*MBB, MF);
    if (EnableO2OrSize)
      Changed |= foldStackBaseLeaOffsets(*MBB, MF);
    Changed |= foldIndexedMem(*MBB, MF);
    Changed |= foldIndexedMemFromDataBase(*MBB, MF);
    if (EnableO2OrSize)
      Changed |= foldIndexedZeroStore(*MBB, MF);
    if (EnableO1)
      Changed |= foldIndexedAddFromAbsBase(*MBB, MF);
    Changed |= foldStackStoreLoadForward(*MBB, MF);
    if (EnableO2OrSize) {
      Changed |= foldLoadMAdd(*MBB, MF);
      Changed |= foldMemoryBitOps(*MBB, MF);
    }
    if (EnableO2OrSize)
      Changed |= foldProductChainMAdd(*MBB, MF);
    if (EnableO2OrSize)
      Changed |= foldShiftedBSet(*MBB, MF);
    if (EnableO1)
      Changed |= foldDivMod(*MBB, MF);
    if (EnableO2OrSize)
      Changed |= foldRegMAddAccumulator(*MBB, MF);
    if (EnableSizeSum)
      Changed |= foldSum(*MBB, MF);
    if (EnableSizeSum)
      Changed |= foldSumReturnCopy(*MBB, MF);
    if (!OptNone && MF.getFunction().hasMinSize() && MBB->getParent() == &MF)
      Changed |= foldSignedClampReturn(*MBB, MF);
    if (EnableO1)
      Changed |= foldEpilogue(*MBB, MF);
    Changed |= foldFallthroughJumps(*MBB, MF);
  }
  if (!OptNone && MF.getFunction().hasMinSize())
    Changed |= foldCondZextAddToInc(MF);
  if (!OptNone && MF.getFunction().hasMinSize())
    Changed |= foldMinSizeLoopCounter32(MF);
  if (EnableO1) {
    Changed |= foldPositiveCountedLoopPretest(MF, KnownZeroIns);
    Changed |= foldCountedLoopIndexResult(MF, KnownZeroIns);
    Changed |= foldPositiveCountedLoopHeaderPretest(MF, KnownZeroIns);
    Changed |= foldSMaxCountdownPretest(MF, KnownZeroIns);
    Changed |= foldSMaxIndexLoopBound(MF);
    Changed |= foldNarrowLoopCountCopies(MF);
    if (!OptNone && MF.getFunction().hasMinSize())
      Changed |= foldAccumulatorLoopUseInputCount(MF);
    for (MachineBasicBlock *MBB : Blocks)
      Changed |= foldCrossBlockPostInc(*MBB, MF);
    Changed |= foldZeroRegCopies(MF);
    Changed |= foldDeadPlainDefs(MF);
    Changed |= foldDeadClrs(MF);
  }
  if (EnableO2OrSize) {
    Changed |= foldSmallConstMultiply(MF);
    Changed |= foldStackZeroCmp(MF);
    Changed |= foldStackConstLoads(MF);
    Changed |= foldStackReloadFromZextCount(MF);
    if (MF.getFunction().hasMinSize())
      Changed |= foldMinSizeDivmodConstAccumulate(MF);
    for (MachineBasicBlock *MBB : Blocks)
      Changed |= foldByteLoadKnownZeroCmpBranch(
          *MBB, MF, EnableO1 ? KnownZeroIns.lookup(MBB) : 0);
    for (MachineBasicBlock *MBB : Blocks)
      Changed |= foldEqNeZeroCmpToTest(*MBB, MF,
                                       EnableO1 ? KnownZeroIns.lookup(MBB) : 0);
    for (MachineBasicBlock *MBB : Blocks)
      Changed |= foldClrZeroCmpToTest(*MBB, MF);
    for (MachineBasicBlock *MBB : Blocks)
      Changed |= foldClrZeroMemCmp(*MBB, MF);
    for (MachineBasicBlock *MBB : Blocks)
      Changed |= foldMemoryImmFlagOp(*MBB, MF);
    for (MachineBasicBlock *MBB : Blocks)
      Changed |= foldMemoryRegFlagOp(*MBB, MF);
    for (MachineBasicBlock *MBB : Blocks)
      if (MBB->getParent() == &MF)
        Changed |= foldKnownZeroByteStoreToBSet(*MBB, MF);
    for (MachineBasicBlock *MBB : Blocks)
      if (MBB->getParent() == &MF)
        Changed |= foldIndexedZeroStore(*MBB, MF);
    Changed |= foldStackSlotsToARegs(MF);
    Changed |= foldDeadFrameTopPadding(MF);
    Changed |= foldDeadStackAdjust(MF);
    for (MachineBasicBlock *MBB : Blocks)
      Changed |= foldTailCallReturn(*MBB, MF);
    for (MachineBasicBlock *MBB : Blocks)
      Changed |= foldRegMAddAccumulator(*MBB, MF);
    if (MF.getFunction().hasMinSize())
      for (MachineBasicBlock *MBB : Blocks) {
        Changed |= foldLoopCarriedLoadAddCopies(*MBB, MF);
        Changed |= foldLoopCarriedLoadUpdateCopies(*MBB, MF);
      }
    for (MachineBasicBlock *MBB : Blocks)
      Changed |= foldProductChainMAdd(*MBB, MF);
    for (MachineBasicBlock *MBB : Blocks)
      Changed |= foldShiftedBSet(*MBB, MF);
    if (MF.getFunction().hasMinSize())
      for (MachineBasicBlock *MBB : Blocks)
        if (MBB->getParent() == &MF)
          Changed |= foldSignedClampReturn(*MBB, MF);
    Changed |= foldZeroRegCopies(MF);
    Changed |= foldDeadPlainDefs(MF);
    Changed |= foldDeadClrs(MF);
    Changed |= shrinkUnusedPushPopMask(MF);
    for (MachineBasicBlock *MBB : Blocks)
      Changed |= foldSumReturnCopy(*MBB, MF);
  }
  if (!OptNone && MF.getFunction().hasMinSize())
    Changed |= foldMinSizeA32ToCalleeSavedDRegs(MF);
  if (!OptNone && MF.getFunction().hasMinSize())
    Changed |= foldA6BaseCopyStackSpill(MF);
  if (!OptNone && MF.getFunction().hasMinSize())
    for (MachineBasicBlock *MBB : Blocks)
      if (MBB->getParent() == &MF)
        Changed |= foldAImmCopyToDImm(*MBB, MF);
  for (MachineBasicBlock *MBB : Blocks) {
    Changed |= foldPostInc(*MBB, MF);
    Changed |= normalizeA32Arithmetic(*MBB, MF);
  }
  if (!OptNone && MF.getFunction().hasMinSize())
    for (MachineBasicBlock *MBB : Blocks)
      if (MBB->getParent() == &MF)
        Changed |= foldIndexedMem(*MBB, MF);
  if (!OptNone && MF.getFunction().hasMinSize())
    Changed |= foldMinSizeMAddWindowBaseBias(MF);
  if (!OptNone && MF.getFunction().hasMinSize())
    for (MachineBasicBlock *MBB : Blocks) {
      if (MBB->getParent() != &MF)
        continue;
      Changed |= foldLea(*MBB, MF);
      Changed |= foldStackBaseLeaOffsets(*MBB, MF);
      Changed |= foldIndexedMem(*MBB, MF);
      Changed |= foldIndexedZeroStore(*MBB, MF);
      Changed |= foldLoadOp(*MBB, MF);
      Changed |= foldMemoryRegFlagOp(*MBB, MF);
      Changed |= foldFallthroughJumps(*MBB, MF);
    }
  if (!OptNone && MF.getFunction().hasMinSize())
    Changed |= foldSMaxPretestZeroReturn(MF);
  if (!OptNone && MF.getFunction().hasMinSize()) {
    Changed |= foldAscendingLoadProgressionLoops(MF);
    Changed |= foldAscendingConstStoreLoops(MF);
    Changed |= foldAscendingMultiStoreLoops(MF);
    Changed |= foldAscendingStoreProgressionLoops(MF);
    Changed |= foldAscendingAddressStoreLoops(MF);
    Changed |= foldByteZeroOffsetStoreLoops(MF);
    Changed |= foldByteOffsetStoreLoops(MF);
    Changed |= foldMixedZeroStoreLoops(MF);
    Changed |= foldWideZeroFillDjtLoops(MF);
    Changed |= foldZeroExitRepgTailLoops(MF);
    Changed |= foldDeadPlainDefs(MF);
    Changed |= foldDeadClrs(MF);
  }
  if (EnableO2)
    Changed |= foldScanUntilZeroRepne(MF);
  if (EnableO2)
    Changed |= foldSingleInstructionRepLoops(MF);
  if (EnableO2OrSize)
    Changed |= foldNarrowLoopCountCopies(MF);
  if (EnableO2OrSize)
    Changed |= shrinkUnusedPushPopMask(MF);
  if (EnableO2OrSize)
    for (MachineBasicBlock *MBB : Blocks)
      Changed |= foldTailCallReturn(*MBB, MF);
  if (EnableO1)
    for (MachineBasicBlock *MBB : Blocks)
      if (MBB->getParent() == &MF)
        Changed |= foldCountedBranchShortcuts(*MBB, MF);
  if (EnableO2OrSize)
    Changed |= foldTopTestIncLoopToIJcc(MF);
  if (!OptNone && MF.getFunction().hasMinSize())
    Changed |= foldSequentialEqImmCompareChain(MF);
  if (EnableO2OrSize)
    Changed |= foldStackBaseBiasOriginalUses(MF);
  if (EnableO2OrSize)
    Changed |= foldRepeatedStackAddressLeasWithBorrowedBase(MF);
  if (EnableO2OrSize)
    Changed |= foldRepeatedStackAddressLeasWithScopedBase(MF);
  if (EnableO2OrSize)
    Changed |= foldRepeatedStackAddressLeas(MF);
  if (EnableO2OrSize)
    for (MachineBasicBlock *MBB : Blocks)
      if (MBB->getParent() == &MF)
        Changed |= foldShortLeaAliasCopies(*MBB, MF);
  if (EnableO2OrSize)
    for (MachineBasicBlock *MBB : Blocks)
      if (MBB->getParent() == &MF)
        Changed |= foldARegAliasCopies(*MBB, MF);
  if (EnableO2OrSize)
    for (MachineBasicBlock *MBB : Blocks)
      if (MBB->getParent() == &MF)
        Changed |= foldAliasBackCopies(*MBB, MF);
  if (EnableO2OrSize)
    for (MachineBasicBlock *MBB : Blocks)
      if (MBB->getParent() == &MF)
        Changed |= foldDRegCopyCoalescing(*MBB, MF);
  if (EnableO2OrSize)
    for (MachineBasicBlock *MBB : Blocks)
      if (MBB->getParent() == &MF)
        Changed |= foldAndTestToImmTest(*MBB, MF);
  if (EnableO2OrSize) {
    Changed |= foldPositiveConstRepPretests(MF);
    if (!OptNone && MF.getFunction().hasMinSize()) {
      Changed |= foldAscendingConstStoreLoops(MF);
      Changed |= foldAscendingAddressStoreLoops(MF);
      Changed |= foldSingleInstructionRepLoops(MF);
      Changed |= foldWideZeroFillDjtLoops(MF);
      Changed |= foldDeadPlainDefs(MF);
      Changed |= foldDeadClrs(MF);
    }
  }
  if (EnableO2OrSize)
    Changed |= foldSelfZextI32LoopCounts(MF);
  Changed |= legalizeLargeStackAdjustments(MF);
  if (!OptNone && MF.getFunction().hasMinSize())
    for (MachineBasicBlock *MBB : Blocks)
      if (MBB->getParent() == &MF)
        Changed |= foldSmallMov64Imm(*MBB, MF);
  if (!OptNone && MF.getFunction().hasMinSize())
    for (MachineBasicBlock *MBB : Blocks)
      if (MBB->getParent() == &MF)
        Changed |= foldIndexedZeroStore(*MBB, MF);
  if (!OptNone && MF.getFunction().hasMinSize()) {
    for (MachineBasicBlock *MBB : Blocks)
      if (MBB->getParent() == &MF)
        Changed |= foldClrStore(*MBB, MF);
    if (EnableO2OrSize)
      Changed |= shrinkUnusedPushPopMask(MF);
  }
  if (EnableO2OrSize)
    Changed |= foldByteIndexedMemUtilityLoops(MF);
  if (EnableO1)
    Changed |= foldMinMaxBranchDiamond(MF);
  return Changed;
}

FunctionPass *
llvm::createBedrockPushPopMergePass(BedrockPeepholeProfile Profile) {
  return new BedrockPushPopMerge(Profile);
}
