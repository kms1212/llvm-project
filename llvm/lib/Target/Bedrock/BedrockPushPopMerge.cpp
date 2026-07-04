//===-- BedrockPushPopMerge.cpp - Bedrock post-RA peepholes ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockPushPopMerge.h"
#include "llvm/InitializePasses.h"

#define DEBUG_TYPE "bedrock-push-pop-merge"

char BedrockPushPopMerge::ID = 0;

INITIALIZE_PASS(BedrockPushPopMerge, DEBUG_TYPE, PASS_NAME, false, false)

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
