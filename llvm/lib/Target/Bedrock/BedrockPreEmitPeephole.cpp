//===-- BedrockPreEmitPeephole.cpp - Late peepholes for Bedrock -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Bedrock.h"
#include "BedrockInstrInfo.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include <iterator>
#include <limits>

using namespace llvm;

#define DEBUG_TYPE "bedrock-pre-emit-peephole"
#define BEDROCK_PRE_EMIT_PEEPHOLE_NAME "Bedrock Pre-Emit Peephole"

STATISTIC(NumZeroCopiesFolded, "Number of zero materialization copies folded");
STATISTIC(NumZeroReusesFolded,
          "Number of duplicate zero materializations folded by reuse");
STATISTIC(NumZeroStoresFolded,
          "Number of zero materialization stores folded to immediates");
STATISTIC(NumZeroReturnCalleeSavedPhisFolded,
          "Number of zero-return callee-saved register phis folded");
STATISTIC(NumZeroComparesFolded,
          "Number of zero materialization compares folded to tests");
STATISTIC(NumPositiveOneComparesFolded,
          "Number of positive-one compares folded to zero tests");
STATISTIC(NumPositiveOneImmComparesFolded,
          "Number of positive-one immediate compares folded to zero tests");
STATISTIC(
    NumPositiveOneZeroResultComparesFolded,
    "Number of positive-one compares with zero result folded to zero tests");
STATISTIC(NumMaterializedConstantComparesFolded,
          "Number of materialized constant compares folded to immediates");
STATISTIC(NumRedundantFrameStoresFolded,
          "Number of redundant frame load-store pairs folded");
STATISTIC(NumShiftOneFolded, "Number of shift-left-by-one instructions folded");
STATISTIC(NumShiftOrOneFolded,
          "Number of shifted or-one instructions folded to increment");
STATISTIC(NumShiftMaskByteExtendsFolded,
          "Number of shifted byte masks folded to byte extends");
STATISTIC(NumMulByThreeFolded,
          "Number of multiply-by-three instructions folded to additions");
STATISTIC(NumMaterializedMulImmediatesFolded,
          "Number of materialized multiply immediates folded");
STATISTIC(NumPowerPlusOneMulFolded,
          "Number of multiply-by-power-plus-one instructions folded");
STATISTIC(NumSymbolOffsetLeasFolded,
          "Number of symbol-plus-offset materializations folded to LEA");
STATISTIC(NumSymbolIndexLeasFolded,
          "Number of symbol-plus-index materializations folded to LEA");
STATISTIC(NumByteRotatesFolded,
          "Number of byte rotate shift/or sequences folded");
STATISTIC(NumDeadCopiesBeforeBranchesFolded,
          "Number of dead copies before branches folded");
STATISTIC(NumRedundantSelfLogicFolded,
          "Number of redundant self logic instructions folded");
STATISTIC(NumTwoStageExtendsFolded,
          "Number of two-stage integer extends folded");
STATISTIC(NumAndExtendsFolded,
          "Number of 32-bit and plus zero-extend instructions folded");
STATISTIC(NumKnownZeroExtendsFolded,
          "Number of redundant zero-extends folded by known high bits");
STATISTIC(NumKnownZeroCopyExtendsFolded,
          "Number of zero-extends to copy registers folded by known high bits");
STATISTIC(NumKnownZeroAndExtendsFolded,
          "Number of 32-bit and plus zero-extend instructions folded by known "
          "high bits");
STATISTIC(NumSignExtendedSMaxZeroExtendsFolded,
          "Number of sign-extended signed max-zero plus zero-extend "
          "instructions folded");
STATISTIC(NumZeroMinMaxesFolded,
          "Number of zero min/max instructions folded to register forms");
STATISTIC(NumCommutativeCopiesFolded,
          "Number of commutative binary operation copies folded");
STATISTIC(NumCalleeSavedPairsFolded,
          "Number of callee-saved register pairs folded to pushp/popp");
STATISTIC(NumCalleeSavedSinglesFolded,
          "Number of callee-saved registers folded to push/pop");
STATISTIC(NumCalleeSavedPaddingPairsFolded,
          "Number of padded callee-saved singles folded to pushp/popp");
STATISTIC(NumCalleeSavedFramePaddingPairsFolded,
          "Number of frame-padded callee-saved singles folded to pushp/popp");
STATISTIC(NumSelectFalseOpsSunk,
          "Number of precomputed select false operations sunk");
STATISTIC(NumHexDigitSelectsFolded,
          "Number of hex digit select sequences folded");
STATISTIC(NumHexDigitPostExtendsFolded,
          "Number of hex digit select plus post-extend sequences folded");
STATISTIC(NumShiftedByteStoresFolded,
          "Number of shifted loads before byte stores folded");
STATISTIC(NumRetainedLoadMemIncDecsFolded,
          "Number of retained loads plus memory inc/dec stores folded");
STATISTIC(NumReloadedMemIncDecsFolded,
          "Number of memory inc/dec stores folded with reloaded results");
STATISTIC(NumDeadMemIncDecsFolded,
          "Number of memory inc/dec stores folded with dead results");
STATISTIC(NumDivRemDecompositionsFolded,
          "Number of division remainder decompositions folded");
STATISTIC(NumTailCallsFolded, "Number of call-return pairs folded to jumps");
STATISTIC(NumAffineMulLoopsFolded,
          "Number of loop-carried affine multiply idioms folded");
STATISTIC(NumCalleeSavedAffineMulLoopsFolded,
          "Number of callee-saved affine multiply loop idioms folded");
STATISTIC(NumSelfLoopIVCopiesFolded,
          "Number of self-loop induction variable copies folded");
STATISTIC(NumDeferredLeaCopiesFolded,
          "Number of deferred LEA copies folded to in-place LEAs");
STATISTIC(NumGlobalBaseAbsAccessesFolded,
          "Number of absolute global memory accesses folded to base offsets");
STATISTIC(NumIntroducedGlobalBaseAbsAccessesFolded,
          "Number of absolute global memory accesses folded with a new base");
STATISTIC(NumCalleeSavedConstantReusesFolded,
          "Number of callee-saved constant materializations folded by reuse");
STATISTIC(NumRepgCounterScratchRegsFolded,
          "Number of REPG counters reused as loop scratch registers");

namespace {
class BedrockPreEmitPeephole : public MachineFunctionPass {
public:
  static char ID;

  BedrockPreEmitPeephole() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return BEDROCK_PRE_EMIT_PEEPHOLE_NAME;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  static bool getZeroMaterializationReg(const MachineInstr &MI, Register &Reg);
  static bool isZeroMaterializationFor(const MachineInstr &MI, Register Reg);
  static bool getFrameLoadStoreInfo(const MachineInstr &MI, bool IsStore,
                                    Register &Reg, int64_t &Base,
                                    int64_t &Offset, unsigned &Size);
  static bool isLoadOpcode(unsigned Opcode);
  static bool definesReg(const MachineInstr &MI, Register Reg);
  static bool readsReg(const MachineInstr &MI, Register Reg);
  static bool usesReg(const MachineInstr &MI, Register Reg);
  static bool mayReadFlags(const MachineInstr &MI);
  static bool writesFlags(const MachineInstr &MI);
  static bool flagsAreDeadAfter(MachineBasicBlock::iterator I,
                                MachineBasicBlock &MBB);
  static bool foldPositiveOneCompare(MachineBasicBlock::iterator &I,
                                     MachineBasicBlock &MBB,
                                     const TargetInstrInfo &TII);
  static bool foldPositiveOneImmCompare(MachineBasicBlock::iterator I,
                                        MachineBasicBlock &MBB,
                                        const TargetInstrInfo &TII);
  static bool foldMaterializedConstantCompare(
      MachineBasicBlock::iterator &I, MachineBasicBlock &MBB,
      const TargetInstrInfo &TII);
  static bool foldZeroCompare(MachineBasicBlock::iterator &I,
                              MachineBasicBlock &MBB,
                              const TargetInstrInfo &TII);
  static bool foldRedundantFrameStore(MachineBasicBlock::iterator I,
                                      MachineBasicBlock &MBB);
  static bool foldShiftLeftOne(MachineBasicBlock::iterator I,
                               MachineBasicBlock &MBB,
                               const TargetInstrInfo &TII);
  static bool foldShiftOrOne(MachineBasicBlock::iterator I,
                             MachineBasicBlock &MBB,
                             const TargetInstrInfo &TII);
  static bool foldShiftMaskByteExtend(MachineBasicBlock::iterator I,
                                      MachineBasicBlock &MBB,
                                      const TargetInstrInfo &TII);
  static bool foldSingleUseConstThreeMul(MachineFunction &MF,
                                         const TargetInstrInfo &TII);
  static bool foldSingleUseMaterializedMulImmediate(
      MachineFunction &MF, const TargetInstrInfo &TII);
  static bool foldSingleUseConstPowerPlusOneMul(MachineFunction &MF,
                                                const TargetInstrInfo &TII);
  static bool foldAffineMulLoop(MachineFunction &MF,
                                const TargetInstrInfo &TII);
  static bool foldCalleeSavedAffineMulLoop(MachineFunction &MF,
                                           const TargetInstrInfo &TII);
  static bool foldSelfLoopIVCopy(MachineFunction &MF,
                                 const TargetInstrInfo &TII);
  static bool foldDeferredLeaCopy(MachineFunction &MF,
                                  const TargetInstrInfo &TII);
  static bool foldGlobalBaseAbsAccesses(MachineFunction &MF,
                                        const TargetInstrInfo &TII);
  static bool foldIntroducedGlobalBaseAbsAccesses(MachineFunction &MF,
                                                  const TargetInstrInfo &TII);
  static bool foldCalleeSavedConstantReuse(MachineFunction &MF,
                                           const TargetInstrInfo &TII);
  static bool foldMulByThree(MachineBasicBlock::iterator &I,
                             MachineBasicBlock &MBB,
                             const TargetInstrInfo &TII);
  static bool foldAdjacentMaterializedMulImmediate(
      MachineBasicBlock::iterator &I, MachineBasicBlock &MBB,
      const TargetInstrInfo &TII);
  static bool foldSymbolOffsetLea(MachineBasicBlock::iterator I,
                                  MachineBasicBlock &MBB,
                                  const TargetInstrInfo &TII);
  static bool foldSymbolIndexLea(MachineBasicBlock::iterator &I,
                                 MachineBasicBlock &MBB,
                                 const TargetInstrInfo &TII);
  static bool foldByteRotateIdiom(MachineBasicBlock::iterator &I,
                                  MachineBasicBlock &MBB,
                                  const TargetInstrInfo &TII);
  static bool foldDeadCopyBeforeBranch(MachineBasicBlock::iterator &I,
                                       MachineBasicBlock &MBB);
  static bool foldShiftedByteStore(MachineBasicBlock::iterator I,
                                   MachineBasicBlock &MBB,
                                   const TargetInstrInfo &TII);
  static bool foldRetainedLoadMemIncDec(MachineBasicBlock::iterator I,
                                        MachineBasicBlock &MBB,
                                        const TargetInstrInfo &TII);
  static bool foldReloadedMemIncDec(MachineBasicBlock::iterator &I,
                                    MachineBasicBlock &MBB,
                                    const TargetInstrInfo &TII);
  static bool foldDeadMemIncDec(MachineBasicBlock::iterator &I,
                                MachineBasicBlock &MBB,
                                const TargetInstrInfo &TII);
  static bool foldDivRemDecomposition(MachineBasicBlock::iterator I,
                                      MachineBasicBlock &MBB,
                                      const TargetInstrInfo &TII);
  static bool foldRedundantSelfLogic(MachineBasicBlock::iterator I,
                                     MachineBasicBlock &MBB);
  static bool foldTwoStageExtend(MachineBasicBlock::iterator I,
                                 MachineBasicBlock &MBB,
                                 const TargetInstrInfo &TII);
  static bool foldAndExtend(MachineBasicBlock::iterator I,
                            MachineBasicBlock &MBB,
                            const TargetInstrInfo &TII);
  static bool foldKnownZeroHighExtends(
      MachineBasicBlock &MBB, const TargetInstrInfo &TII,
      const DenseMap<Register, MachineInstr *> &EntryPromotableZeroExts,
      const DenseMap<const MachineBasicBlock *, SmallSet<Register, 16>>
          &KnownZeroHighBlockInputs);
  static bool foldSignExtendedSMaxZeroExtends(MachineBasicBlock &MBB,
                                              const TargetInstrInfo &TII);
  static bool foldZeroMinMaxWithKnownZero(MachineBasicBlock::iterator I,
                                          MachineBasicBlock &MBB,
                                          const TargetInstrInfo &TII);
  static bool foldCommutativeCopy(MachineBasicBlock::iterator I,
                                  MachineBasicBlock &MBB,
                                  const MachineFunction &MF);
  static bool foldCalleeSavedPushPairs(MachineBasicBlock::iterator I,
                                       MachineBasicBlock &MBB,
                                       const TargetInstrInfo &TII);
  static bool foldCalleeSavedPopPairs(MachineBasicBlock::iterator I,
                                      MachineBasicBlock &MBB,
                                      const TargetInstrInfo &TII);
  static bool foldCalleeSavedFramePaddingPair(MachineFunction &MF,
                                              const TargetInstrInfo &TII);
  static bool foldCalleeSavedPaddingPair(MachineFunction &MF,
                                         const TargetInstrInfo &TII);
  static bool foldDeadCalleeSavedPairs(MachineFunction &MF);
  static bool foldRepgCounterScratch(MachineFunction &MF,
                                     const TargetInstrInfo &TII);
  static bool foldSelectFalseImmediateOp(MachineFunction &MF,
                                         const TargetInstrInfo &TII);
  static bool foldHexDigitSelect(MachineFunction &MF,
                                 const TargetInstrInfo &TII);
  static bool foldHexDigitPostExtendPair(MachineFunction &MF,
                                         const TargetInstrInfo &TII);
  static bool isTailCallEpilogueInstr(const MachineInstr &MI);
  static bool foldTailCall(MachineBasicBlock::iterator I,
                           MachineBasicBlock &MBB,
                           const TargetInstrInfo &TII);
  static bool foldZeroReuse(MachineBasicBlock::iterator I,
                            MachineBasicBlock &MBB);
  static bool foldZeroCopy(MachineBasicBlock::iterator I,
                           MachineBasicBlock &MBB);
  static bool foldZeroReturnCalleeSavedPhi(MachineFunction &MF);
  static bool foldZeroStores(MachineBasicBlock::iterator &I,
                             MachineBasicBlock &MBB,
                             const TargetInstrInfo &TII);
};
} // namespace

static unsigned getSignedAutoImmSize(int64_t Value) {
  if (Value >= -128 && Value <= 127)
    return 1;
  if (Value >= -32768 && Value <= 32767)
    return 2;
  if (Value >= -(1LL << 31) && Value <= ((1LL << 31) - 1))
    return 4;
  return 8;
}

static unsigned getConstMaterializationSize(const MachineInstr &MI) {
  int64_t Imm = MI.getOperand(1).getImm();
  if (Imm == 0)
    return 1;
  if (Imm == 1)
    return 2;
  if (Imm == -1)
    return 3;
  return 3 + getSignedAutoImmSize(Imm);
}

static bool isSymbolicConstOperand(const MachineOperand &MO) {
  switch (MO.getType()) {
  case MachineOperand::MO_GlobalAddress:
  case MachineOperand::MO_ExternalSymbol:
  case MachineOperand::MO_BlockAddress:
  case MachineOperand::MO_ConstantPoolIndex:
  case MachineOperand::MO_JumpTableIndex:
    return true;
  default:
    return false;
  }
}

bool BedrockPreEmitPeephole::getZeroMaterializationReg(const MachineInstr &MI,
                                                       Register &Reg) {
  if (MI.getNumOperands() == 0 || !MI.getOperand(0).isReg())
    return false;

  Reg = MI.getOperand(0).getReg();
  return isZeroMaterializationFor(MI, Reg);
}

bool BedrockPreEmitPeephole::isZeroMaterializationFor(const MachineInstr &MI,
                                                      Register Reg) {
  if (MI.getNumOperands() == 0 || !MI.getOperand(0).isReg() ||
      MI.getOperand(0).getReg() != Reg)
    return false;

  switch (MI.getOpcode()) {
  case Bedrock::CLRQr:
    return true;
  case Bedrock::CONST32:
  case Bedrock::CONST64:
    return MI.getOperand(1).isImm() && MI.getOperand(1).getImm() == 0;
  default:
    return false;
  }
}

bool BedrockPreEmitPeephole::getFrameLoadStoreInfo(const MachineInstr &MI,
                                                   bool IsStore, Register &Reg,
                                                   int64_t &Base,
                                                   int64_t &Offset,
                                                   unsigned &Size) {
  if (IsStore) {
    switch (MI.getOpcode()) {
    case Bedrock::STOREBfi:
      Size = 1;
      break;
    case Bedrock::STOREWfi:
      Size = 2;
      break;
    case Bedrock::STORELfi:
      Size = 4;
      break;
    case Bedrock::STOREQfi:
      Size = 8;
      break;
    default:
      return false;
    }
  } else {
    switch (MI.getOpcode()) {
    case Bedrock::LOADB_Zfi:
    case Bedrock::LOADB_Sfi:
      Size = 1;
      break;
    case Bedrock::LOADW_Zfi:
    case Bedrock::LOADW_Sfi:
      Size = 2;
      break;
    case Bedrock::LOADL_Zfi:
    case Bedrock::LOADL_Sfi:
    case Bedrock::LOADLfi:
      Size = 4;
      break;
    case Bedrock::LOADQfi:
      Size = 8;
      break;
    default:
      return false;
    }
  }

  if (!MI.getOperand(0).isReg() || !MI.getOperand(1).isImm() ||
      !MI.getOperand(2).isImm())
    return false;

  Reg = MI.getOperand(0).getReg();
  Base = MI.getOperand(1).getImm();
  Offset = MI.getOperand(2).getImm();
  return true;
}

bool BedrockPreEmitPeephole::isLoadOpcode(unsigned Opcode) {
  switch (Opcode) {
  case Bedrock::LOADB_Zrr:
  case Bedrock::LOADW_Zrr:
  case Bedrock::LOADL_Zrr:
  case Bedrock::LOADB_Srr:
  case Bedrock::LOADW_Srr:
  case Bedrock::LOADL_Srr:
  case Bedrock::LOADLrr:
  case Bedrock::LOADQrr:
  case Bedrock::LOADB_Zro:
  case Bedrock::LOADW_Zro:
  case Bedrock::LOADL_Zro:
  case Bedrock::LOADB_Sro:
  case Bedrock::LOADW_Sro:
  case Bedrock::LOADL_Sro:
  case Bedrock::LOADLro:
  case Bedrock::LOADQro:
  case Bedrock::LOADB_Zabs:
  case Bedrock::LOADW_Zabs:
  case Bedrock::LOADL_Zabs:
  case Bedrock::LOADB_Sabs:
  case Bedrock::LOADW_Sabs:
  case Bedrock::LOADL_Sabs:
  case Bedrock::LOADLabs:
  case Bedrock::LOADQabs:
  case Bedrock::LOADB_Zfi:
  case Bedrock::LOADW_Zfi:
  case Bedrock::LOADL_Zfi:
  case Bedrock::LOADB_Sfi:
  case Bedrock::LOADW_Sfi:
  case Bedrock::LOADL_Sfi:
  case Bedrock::LOADLfi:
  case Bedrock::LOADQfi:
    return true;
  default:
    return false;
  }
}

bool BedrockPreEmitPeephole::definesReg(const MachineInstr &MI, Register Reg) {
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.isDef() && MO.getReg() == Reg)
      return true;
  }
  return false;
}

bool BedrockPreEmitPeephole::readsReg(const MachineInstr &MI, Register Reg) {
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && !MO.isDef() && MO.getReg() == Reg)
      return true;
  }
  return false;
}

bool BedrockPreEmitPeephole::usesReg(const MachineInstr &MI, Register Reg) {
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.getReg() == Reg)
      return true;
  }
  return false;
}

bool BedrockPreEmitPeephole::mayReadFlags(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case Bedrock::BRCC:
  case Bedrock::SETCC:
    return true;
  default:
    return false;
  }
}

bool BedrockPreEmitPeephole::writesFlags(const MachineInstr &MI) {
  if (MI.getDesc().isCompare())
    return true;

  switch (MI.getOpcode()) {
  case Bedrock::TESTLrr:
  case Bedrock::TESTQrr:
  case Bedrock::INCL3r:
  case Bedrock::INCQ3r:
  case Bedrock::DECL3r:
  case Bedrock::DECQ3r:
  case Bedrock::NEGL3r:
  case Bedrock::NEGQ3r:
  case Bedrock::ABSL3r:
  case Bedrock::ABSQ3r:
  case Bedrock::NOTL3r:
  case Bedrock::NOTQ3r:
  case Bedrock::ADDL3rr:
  case Bedrock::ADDQ3rr:
  case Bedrock::SUBL3rr:
  case Bedrock::SUBQ3rr:
  case Bedrock::ANDL3rr:
  case Bedrock::ANDQ3rr:
  case Bedrock::ORL3rr:
  case Bedrock::ORQ3rr:
  case Bedrock::XORL3rr:
  case Bedrock::XORQ3rr:
  case Bedrock::SHLL3rr:
  case Bedrock::SHLQ3rr:
  case Bedrock::SHLL3ri:
  case Bedrock::SHLQ3ri:
  case Bedrock::SHRL3rr:
  case Bedrock::SHRQ3rr:
  case Bedrock::SHRL3ri:
  case Bedrock::SHRQ3ri:
  case Bedrock::SARL3rr:
  case Bedrock::SARQ3rr:
  case Bedrock::SARL3ri:
  case Bedrock::SARQ3ri:
  case Bedrock::ROLL3rr:
  case Bedrock::ROLQ3rr:
  case Bedrock::ROLL3ri:
  case Bedrock::ROLQ3ri:
  case Bedrock::RORL3rr:
  case Bedrock::RORQ3rr:
  case Bedrock::RORL3ri:
  case Bedrock::RORQ3ri:
  case Bedrock::MINUL3rr:
  case Bedrock::MINUQ3rr:
  case Bedrock::MINSL3rr:
  case Bedrock::MINSQ3rr:
  case Bedrock::MAXUL3rr:
  case Bedrock::MAXUQ3rr:
  case Bedrock::MAXSL3rr:
  case Bedrock::MAXSQ3rr:
  case Bedrock::MULL3rr:
  case Bedrock::MULQ3rr:
  case Bedrock::MULL3ri:
  case Bedrock::MULQ3ri:
  case Bedrock::DIVUL3rr:
  case Bedrock::DIVUQ3rr:
  case Bedrock::DIVSL3rr:
  case Bedrock::DIVSQ3rr:
  case Bedrock::DIVSL3ri:
  case Bedrock::DIVSQ3ri:
  case Bedrock::MODUL3rr:
  case Bedrock::MODUQ3rr:
  case Bedrock::MODSL3rr:
  case Bedrock::MODSQ3rr:
  case Bedrock::ADDL3ri:
  case Bedrock::ADDQ3ri:
  case Bedrock::SUBL3ri:
  case Bedrock::SUBQ3ri:
  case Bedrock::ANDL3ri:
  case Bedrock::ANDQ3ri:
  case Bedrock::ORL3ri:
  case Bedrock::ORQ3ri:
  case Bedrock::XORL3ri:
  case Bedrock::XORQ3ri:
    return true;
  default:
    return false;
  }
}

bool BedrockPreEmitPeephole::flagsAreDeadAfter(MachineBasicBlock::iterator I,
                                               MachineBasicBlock &MBB) {
  MachineBasicBlock *ScanMBB = &MBB;
  auto ScanI = std::next(I);

  for (;;) {
    while (ScanI != ScanMBB->end() && ScanI->isDebugInstr())
      ++ScanI;

    if (ScanI == ScanMBB->end()) {
      if (ScanMBB->succ_empty())
        return true;
      if (ScanMBB->succ_size() != 1)
        return false;
      MachineBasicBlock *Succ = *ScanMBB->succ_begin();
      if (Succ == ScanMBB)
        return false;
      ScanMBB = Succ;
      ScanI = ScanMBB->begin();
      continue;
    }

    if (mayReadFlags(*ScanI))
      return false;
    if (writesFlags(*ScanI))
      return true;
    if (ScanI->isCall() || ScanI->isTerminator())
      return true;
    ++ScanI;
  }
}

bool BedrockPreEmitPeephole::foldPositiveOneCompare(
    MachineBasicBlock::iterator &I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &ConstMI = *I;
  if (ConstMI.getOpcode() != Bedrock::CONST32 &&
      ConstMI.getOpcode() != Bedrock::CONST64)
    return false;
  if (!ConstMI.getOperand(1).isImm() || ConstMI.getOperand(1).getImm() != 1)
    return false;

  auto CmpI = std::next(I);
  if (CmpI == MBB.end())
    return false;
  auto NextI = std::next(CmpI);
  if (NextI == MBB.end())
    return false;

  unsigned NewTestOpcode;
  if (ConstMI.getOpcode() == Bedrock::CONST32 &&
      CmpI->getOpcode() == Bedrock::CMPLrr) {
    NewTestOpcode = Bedrock::TESTLrr;
  } else if (ConstMI.getOpcode() == Bedrock::CONST64 &&
             CmpI->getOpcode() == Bedrock::CMPQrr) {
    NewTestOpcode = Bedrock::TESTQrr;
  } else {
    return false;
  }

  Register ConstReg = ConstMI.getOperand(0).getReg();
  if (CmpI->getOperand(0).getReg() != ConstReg || !CmpI->getOperand(0).isKill())
    return false;

  MachineBasicBlock::iterator BranchI = NextI;
  MachineBasicBlock::iterator ZeroI = MBB.end();
  bool HasZeroResult = false;
  if (NextI->getOpcode() != Bedrock::BRCC) {
    auto MaybeBranchI = std::next(NextI);
    if (MaybeBranchI == MBB.end() ||
        MaybeBranchI->getOpcode() != Bedrock::BRCC ||
        !isZeroMaterializationFor(*NextI, ConstReg))
      return false;
    ZeroI = NextI;
    BranchI = MaybeBranchI;
    HasZeroResult = true;
  }

  if (BranchI->getOperand(1).getImm() != 0xc)
    return false;

  Register BoundReg = CmpI->getOperand(1).getReg();
  if (HasZeroResult)
    ConstMI.getOperand(1).setImm(0);
  CmpI->setDesc(TII.get(NewTestOpcode));
  CmpI->getOperand(0).setReg(BoundReg);
  CmpI->getOperand(0).setIsKill(false);
  CmpI->getOperand(1).setReg(BoundReg);
  CmpI->getOperand(1).setIsKill(false);
  BranchI->getOperand(1).setImm(0xe);

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding positive-one "
                       "compare: ";
             ConstMI.dump(); CmpI->dump(); BranchI->dump());

  I = CmpI;
  if (HasZeroResult) {
    ZeroI->eraseFromParent();
    ++NumPositiveOneZeroResultComparesFolded;
  } else {
    ConstMI.eraseFromParent();
    ++NumPositiveOneComparesFolded;
  }
  return true;
}

bool BedrockPreEmitPeephole::foldPositiveOneImmCompare(
    MachineBasicBlock::iterator I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &CmpMI = *I;
  unsigned NewTestOpcode;
  switch (CmpMI.getOpcode()) {
  case Bedrock::CMPLri:
    NewTestOpcode = Bedrock::TESTLrr;
    break;
  case Bedrock::CMPQri:
    NewTestOpcode = Bedrock::TESTQrr;
    break;
  default:
    return false;
  }

  if (!CmpMI.getOperand(0).isReg() || !CmpMI.getOperand(1).isImm() ||
      CmpMI.getOperand(1).getImm() != 1)
    return false;

  auto UseI = std::next(I);
  while (UseI != MBB.end()) {
    while (UseI != MBB.end() && UseI->isDebugInstr())
      ++UseI;
    if (UseI == MBB.end())
      return false;

    if (mayReadFlags(*UseI))
      break;
    if (writesFlags(*UseI) || UseI->isCall() || UseI->isTerminator())
      return false;
    ++UseI;
  }
  if (UseI == MBB.end())
    return false;

  unsigned CondOperand;
  if (UseI->getOpcode() == Bedrock::BRCC) {
    CondOperand = 1;
  } else if (UseI->getOpcode() == Bedrock::SETCC) {
    CondOperand = 1;
  } else {
    return false;
  }

  int64_t Cond = UseI->getOperand(CondOperand).getImm();
  int64_t NewCond;
  switch (Cond) {
  case 0xc: // x < 1  == x <= 0
    NewCond = 0xe;
    break;
  case 0xd: // x >= 1 == x > 0
    NewCond = 0xf;
    break;
  default:
    return false;
  }

  Register Reg = CmpMI.getOperand(0).getReg();
  CmpMI.setDesc(TII.get(NewTestOpcode));
  CmpMI.removeOperand(1);
  CmpMI.getOperand(0).setIsKill(false);
  CmpMI.addOperand(MachineOperand::CreateReg(Reg, /*isDef=*/false));
  UseI->getOperand(CondOperand).setImm(NewCond);

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding positive-one "
                       "immediate compare: ";
             CmpMI.dump(); UseI->dump());

  ++NumPositiveOneImmComparesFolded;
  return true;
}

bool BedrockPreEmitPeephole::foldMaterializedConstantCompare(
    MachineBasicBlock::iterator &I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &ConstMI = *I;
  if (ConstMI.getOpcode() != Bedrock::CONST32 &&
      ConstMI.getOpcode() != Bedrock::CONST64)
    return false;
  if (!ConstMI.getOperand(0).isReg())
    return false;

  const MachineOperand &ConstOp = ConstMI.getOperand(1);
  bool IsSymbolic = isSymbolicConstOperand(ConstOp);
  if (!ConstOp.isImm() && !IsSymbolic)
    return false;

  auto CmpI = std::next(I);
  while (CmpI != MBB.end() && CmpI->isDebugInstr())
    ++CmpI;
  if (CmpI == MBB.end())
    return false;

  unsigned NewCmpOpcode;
  if (ConstMI.getOpcode() == Bedrock::CONST32 &&
      CmpI->getOpcode() == Bedrock::CMPLrr) {
    NewCmpOpcode = Bedrock::CMPLri;
  } else if (ConstMI.getOpcode() == Bedrock::CONST64 &&
             CmpI->getOpcode() == Bedrock::CMPQrr) {
    NewCmpOpcode = Bedrock::CMPQri;
  } else {
    return false;
  }

  Register ConstReg = ConstMI.getOperand(0).getReg();
  if (!ConstReg.isPhysical() || !CmpI->getOperand(0).isReg() ||
      CmpI->getOperand(0).getReg() != ConstReg ||
      !CmpI->getOperand(0).isKill() || !CmpI->getOperand(1).isReg())
    return false;

  Register LHSReg = CmpI->getOperand(1).getReg();
  bool LHSKill = CmpI->getOperand(1).isKill();

  if (ConstOp.isImm()) {
    int64_t Imm = ConstOp.getImm();
    unsigned ConstAndCmpSize = getConstMaterializationSize(ConstMI) + 2;
    unsigned ImmCmpSize = 3 + getSignedAutoImmSize(Imm);
    if (ImmCmpSize >= ConstAndCmpSize)
      return false;

    CmpI->setDesc(TII.get(NewCmpOpcode));
    CmpI->getOperand(0).setReg(LHSReg);
    CmpI->getOperand(0).setIsKill(LHSKill);
    CmpI->getOperand(1).ChangeToImmediate(Imm);

    LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding materialized "
                         "constant compare: ";
               ConstMI.dump(); CmpI->dump());

    I = CmpI;
    ConstMI.eraseFromParent();
    ++NumMaterializedConstantComparesFolded;
    return true;
  }

  auto NewCmpI =
      BuildMI(MBB, CmpI, CmpI->getDebugLoc(), TII.get(NewCmpOpcode))
          .addReg(LHSReg, getKillRegState(LHSKill))
          .add(ConstOp)
          .getInstr()
          ->getIterator();

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding materialized "
                       "symbol compare: ";
             ConstMI.dump(); NewCmpI->dump());

  CmpI->eraseFromParent();
  I = NewCmpI;
  ConstMI.eraseFromParent();
  ++NumMaterializedConstantComparesFolded;
  return true;
}

bool BedrockPreEmitPeephole::foldZeroCompare(MachineBasicBlock::iterator &I,
                                             MachineBasicBlock &MBB,
                                             const TargetInstrInfo &TII) {
  MachineInstr &ZeroMI = *I;
  if (ZeroMI.getNumOperands() < 1 || !ZeroMI.getOperand(0).isReg())
    return false;

  Register ZeroReg = ZeroMI.getOperand(0).getReg();
  if (!isZeroMaterializationFor(ZeroMI, ZeroReg))
    return false;

  auto CmpI = std::next(I);
  while (CmpI != MBB.end()) {
    while (CmpI != MBB.end() && CmpI->isDebugInstr())
      ++CmpI;
    if (CmpI == MBB.end())
      return false;

    if (CmpI->getOpcode() == Bedrock::CMPLrr ||
        CmpI->getOpcode() == Bedrock::CMPQrr)
      break;

    if (usesReg(*CmpI, ZeroReg) || mayReadFlags(*CmpI) ||
        writesFlags(*CmpI) || CmpI->isCall() || CmpI->isTerminator())
      return false;
    ++CmpI;
  }
  if (CmpI == MBB.end())
    return false;

  bool IsLong;
  unsigned TestOpcode;
  switch (CmpI->getOpcode()) {
  case Bedrock::CMPLrr:
    IsLong = true;
    TestOpcode = Bedrock::TESTLrr;
    break;
  case Bedrock::CMPQrr:
    IsLong = false;
    TestOpcode = Bedrock::TESTQrr;
    break;
  default:
    return false;
  }
  (void)IsLong;

  Register OtherReg;
  bool ZeroKilledByCmp;
  if (CmpI->getOperand(0).isReg() && CmpI->getOperand(0).getReg() == ZeroReg &&
      CmpI->getOperand(1).isReg()) {
    OtherReg = CmpI->getOperand(1).getReg();
    ZeroKilledByCmp = CmpI->getOperand(0).isKill();
  } else if (CmpI->getOperand(1).isReg() &&
             CmpI->getOperand(1).getReg() == ZeroReg &&
             CmpI->getOperand(0).isReg()) {
    OtherReg = CmpI->getOperand(0).getReg();
    ZeroKilledByCmp = CmpI->getOperand(1).isKill();
  } else {
    return false;
  }

  bool ZeroRedefinedBeforeFlagsUse = false;
  auto UseI = std::next(CmpI);
  while (UseI != MBB.end()) {
    while (UseI != MBB.end() && UseI->isDebugInstr())
      ++UseI;
    if (UseI == MBB.end())
      return false;

    if (mayReadFlags(*UseI))
      break;
    if ((!ZeroRedefinedBeforeFlagsUse && readsReg(*UseI, ZeroReg)) ||
        writesFlags(*UseI) || UseI->isCall() ||
        UseI->isTerminator())
      return false;
    if (!ZeroRedefinedBeforeFlagsUse && definesReg(*UseI, ZeroReg))
      ZeroRedefinedBeforeFlagsUse = true;
    ++UseI;
  }
  if (UseI == MBB.end())
    return false;
  if (!ZeroKilledByCmp && !ZeroRedefinedBeforeFlagsUse)
    return false;

  if (UseI->getOpcode() != Bedrock::BRCC && UseI->getOpcode() != Bedrock::SETCC)
    return false;
  unsigned CondOp = 1;
  int64_t Cond = UseI->getOperand(CondOp).getImm();
  if (Cond != 0x2 && Cond != 0x3)
    return false;

  CmpI->setDesc(TII.get(TestOpcode));
  CmpI->getOperand(0).setReg(OtherReg);
  CmpI->getOperand(0).setIsKill(false);
  CmpI->getOperand(1).setReg(OtherReg);
  CmpI->getOperand(1).setIsKill(false);

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding zero compare: ";
             ZeroMI.dump(); CmpI->dump(); UseI->dump());

  I = CmpI;
  ZeroMI.eraseFromParent();
  ++NumZeroComparesFolded;
  return true;
}

bool BedrockPreEmitPeephole::foldRedundantFrameStore(
    MachineBasicBlock::iterator I, MachineBasicBlock &MBB) {
  MachineInstr &LoadMI = *I;
  Register LoadReg;
  int64_t LoadBase;
  int64_t LoadOffset;
  unsigned LoadSize;
  if (!getFrameLoadStoreInfo(LoadMI, /*IsStore=*/false, LoadReg, LoadBase,
                             LoadOffset, LoadSize))
    return false;

  auto NextI = std::next(I);
  if (NextI == MBB.end())
    return false;

  MachineInstr &StoreMI = *NextI;
  Register StoreReg;
  int64_t StoreBase;
  int64_t StoreOffset;
  unsigned StoreSize;
  if (!getFrameLoadStoreInfo(StoreMI, /*IsStore=*/true, StoreReg, StoreBase,
                             StoreOffset, StoreSize))
    return false;

  if (StoreReg != LoadReg || StoreBase != LoadBase ||
      StoreOffset != LoadOffset || StoreSize != LoadSize ||
      StoreMI.getOperand(0).isKill())
    return false;

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding redundant frame "
                       "store: ";
             LoadMI.dump(); StoreMI.dump());

  StoreMI.eraseFromParent();
  ++NumRedundantFrameStoresFolded;
  return true;
}

bool BedrockPreEmitPeephole::foldShiftLeftOne(MachineBasicBlock::iterator I,
                                              MachineBasicBlock &MBB,
                                              const TargetInstrInfo &TII) {
  MachineInstr &ShiftMI = *I;
  unsigned NewOpcode;
  switch (ShiftMI.getOpcode()) {
  case Bedrock::SHLL3ri:
    NewOpcode = Bedrock::ADDL3rr;
    break;
  case Bedrock::SHLQ3ri:
    NewOpcode = Bedrock::ADDQ3rr;
    break;
  default:
    return false;
  }

  if (!ShiftMI.getOperand(2).isImm() || ShiftMI.getOperand(2).getImm() != 1)
    return false;
  if (!flagsAreDeadAfter(I, MBB))
    return false;

  Register SrcReg = ShiftMI.getOperand(1).getReg();
  bool SrcKill = ShiftMI.getOperand(1).isKill();
  ShiftMI.setDesc(TII.get(NewOpcode));
  ShiftMI.removeOperand(2);
  ShiftMI.getOperand(1).setIsKill(false);
  ShiftMI.addOperand(MachineOperand::CreateReg(SrcReg, /*isDef=*/false,
                                               /*isImp=*/false, SrcKill));

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding shl-by-one: ";
             ShiftMI.dump());

  ++NumShiftOneFolded;
  return true;
}

bool BedrockPreEmitPeephole::foldShiftOrOne(MachineBasicBlock::iterator I,
                                            MachineBasicBlock &MBB,
                                            const TargetInstrInfo &TII) {
  MachineInstr &OrMI = *I;
  unsigned ShiftOpcode;
  unsigned IncOpcode;
  switch (OrMI.getOpcode()) {
  case Bedrock::ORL3ri:
    ShiftOpcode = Bedrock::SHLL3ri;
    IncOpcode = Bedrock::INCL3r;
    break;
  case Bedrock::ORQ3ri:
    ShiftOpcode = Bedrock::SHLQ3ri;
    IncOpcode = Bedrock::INCQ3r;
    break;
  default:
    return false;
  }

  if (!OrMI.getOperand(2).isImm() || OrMI.getOperand(2).getImm() != 1)
    return false;
  if (I == MBB.begin())
    return false;

  auto ShiftI = std::prev(I);
  while (ShiftI != MBB.begin() && ShiftI->isDebugInstr())
    --ShiftI;
  if (ShiftI->isDebugInstr() || ShiftI->getOpcode() != ShiftOpcode)
    return false;
  if (!ShiftI->getOperand(2).isImm() || ShiftI->getOperand(2).getImm() < 1)
    return false;

  Register ShiftDst = ShiftI->getOperand(0).getReg();
  Register OrSrc = OrMI.getOperand(1).getReg();
  if (ShiftDst != OrSrc)
    return false;
  if (!flagsAreDeadAfter(I, MBB))
    return false;

  OrMI.setDesc(TII.get(IncOpcode));
  OrMI.removeOperand(2);

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding shifted or-one: ";
             OrMI.dump());

  ++NumShiftOrOneFolded;
  return true;
}

bool BedrockPreEmitPeephole::foldShiftMaskByteExtend(
    MachineBasicBlock::iterator I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &ShiftMI = *I;
  if (ShiftMI.getOpcode() != Bedrock::SHLL3ri ||
      ShiftMI.getNumExplicitOperands() != 3 || !ShiftMI.getOperand(0).isReg() ||
      !ShiftMI.getOperand(1).isReg() || !ShiftMI.getOperand(2).isImm())
    return false;

  Register Reg = ShiftMI.getOperand(0).getReg();
  if (ShiftMI.getOperand(1).getReg() != Reg || !ShiftMI.getOperand(1).isKill())
    return false;

  MachineBasicBlock::iterator CopyI = MBB.end();
  Register SrcReg = Reg;
  bool SrcKill = ShiftMI.getOperand(1).isKill();
  if (I != MBB.begin()) {
    auto PrevI = std::prev(I);
    while (PrevI != MBB.begin() && PrevI->isDebugInstr())
      --PrevI;
    if (!PrevI->isDebugInstr() &&
        (PrevI->getOpcode() == Bedrock::MOVQrr ||
         PrevI->getOpcode() == Bedrock::MOVLrr) &&
        PrevI->getNumExplicitOperands() >= 2 && PrevI->getOperand(0).isReg() &&
        PrevI->getOperand(1).isReg() && PrevI->getOperand(0).getReg() == Reg) {
      CopyI = PrevI;
      SrcReg = PrevI->getOperand(1).getReg();
      SrcKill = PrevI->getOperand(1).isKill();
    }
  }

  int64_t Shift = ShiftMI.getOperand(2).getImm();
  if (Shift <= 0 || Shift > 24)
    return false;

  auto AndI = std::next(I);
  while (AndI != MBB.end() && AndI->isDebugInstr())
    ++AndI;
  if (AndI == MBB.end() ||
      (AndI->getOpcode() != Bedrock::ANDL3ri &&
       AndI->getOpcode() != Bedrock::ANDQ3ri) ||
      AndI->getNumExplicitOperands() != 3 || !AndI->getOperand(0).isReg() ||
      !AndI->getOperand(1).isReg() || !AndI->getOperand(2).isImm() ||
      AndI->getOperand(0).getReg() != Reg || AndI->getOperand(1).getReg() != Reg ||
      !AndI->getOperand(1).isKill() || !flagsAreDeadAfter(AndI, MBB))
    return false;

  uint32_t Mask = static_cast<uint32_t>(AndI->getOperand(2).getImm());
  if (Mask != (0xffu << Shift))
    return false;

  if (ShiftMI.getOperand(0).isTied())
    ShiftMI.untieRegOperand(0);
  ShiftMI.setDesc(TII.get(Bedrock::EXTZQBrr));
  ShiftMI.setFlags(0);
  ShiftMI.removeOperand(2);
  ShiftMI.getOperand(1).setReg(SrcReg);
  ShiftMI.getOperand(1).setIsKill(SrcKill);

  AndI->setDesc(TII.get(Bedrock::SHLL3ri));
  AndI->getOperand(2).setImm(Shift);

  if (CopyI != MBB.end())
    CopyI->eraseFromParent();

  ++NumShiftMaskByteExtendsFolded;
  return true;
}

static bool isConstThreeDef(const MachineInstr &MI, Register Reg) {
  return MI.getOpcode() == Bedrock::CONST32 && MI.getNumExplicitOperands() >= 2 &&
         MI.getOperand(0).isReg() && MI.getOperand(0).getReg() == Reg &&
         MI.getOperand(1).isImm() && MI.getOperand(1).getImm() == 3;
}

static MachineInstr *findConstThreeDefBeforeMul(MachineBasicBlock::iterator MulI,
                                                MachineBasicBlock &MBB,
                                                Register Reg) {
  if (MulI == MBB.begin())
    return nullptr;

  auto I = MulI;
  do {
    --I;
    if (I->isDebugInstr())
      continue;
    return isConstThreeDef(*I, Reg) ? &*I : nullptr;
  } while (I != MBB.begin());

  return nullptr;
}

static MachineBasicBlock::iterator
findCopyIntoRegBeforeMul(MachineBasicBlock::iterator MulI,
                         MachineBasicBlock &MBB, Register DstReg,
                         const MachineInstr *ConstDef) {
  if (MulI == MBB.begin())
    return MBB.end();

  auto I = MulI;
  do {
    --I;
    if (I->isDebugInstr())
      continue;
    if (&*I == ConstDef)
      continue;
    if ((I->getOpcode() == Bedrock::MOVQrr ||
         I->getOpcode() == Bedrock::MOVLrr) &&
        I->getNumExplicitOperands() >= 2 && I->getOperand(0).isReg() &&
        I->getOperand(1).isReg() && I->getOperand(0).getReg() == DstReg)
      return I;
    return MBB.end();
  } while (I != MBB.begin());

  return MBB.end();
}

static bool canDefinitionReachUse(const MachineInstr &DefMI,
                                  const MachineInstr &UseMI);
static bool canReachBlock(const MachineBasicBlock *From,
                          const MachineBasicBlock *To);

bool BedrockPreEmitPeephole::foldSingleUseConstThreeMul(
    MachineFunction &MF, const TargetInstrInfo &TII) {
  if (MF.empty())
    return false;

  for (MachineBasicBlock &ConstMBB : MF) {
    for (auto I = ConstMBB.begin(), E = ConstMBB.end(); I != E; ++I) {
      if (I->isDebugInstr())
        continue;
      if (I->isTerminator())
        break;

      if (I->getOpcode() != Bedrock::CONST32 ||
          I->getNumExplicitOperands() < 2 || !I->getOperand(0).isReg() ||
          !I->getOperand(1).isImm() || I->getOperand(1).getImm() != 3)
        continue;

      Register ConstReg = I->getOperand(0).getReg();
      if (!ConstReg.isPhysical() || !Bedrock::GPR64RegClass.contains(ConstReg))
        continue;

      MachineInstr *MulMI = nullptr;
      MachineBasicBlock *MulMBB = nullptr;
      SmallVector<MachineInstr *, 4> OtherDefs;
      Register ValueReg;
      bool Bad = false;
      unsigned Uses = 0;

      for (MachineBasicBlock &MBB : MF) {
        if (&MBB != &ConstMBB && !canReachBlock(&ConstMBB, &MBB))
          continue;

        bool SawConstDef = &MBB != &ConstMBB;
        bool ShadowedByOtherDef = false;
        for (MachineInstr &MI : MBB) {
          if (&MBB == &ConstMBB) {
            if (&MI == &*I) {
              SawConstDef = true;
              ShadowedByOtherDef = false;
              continue;
            }
            if (!SawConstDef)
              continue;
          }

          if (MI.isDebugInstr())
            continue;

          if (definesReg(MI, ConstReg)) {
            OtherDefs.push_back(&MI);
            ShadowedByOtherDef = true;
            continue;
          }

          if (ShadowedByOtherDef)
            continue;

          if (!readsReg(MI, ConstReg))
            continue;

          ++Uses;
          if (Uses != 1 || MI.getOpcode() != Bedrock::MULL3rr ||
              MI.getNumExplicitOperands() < 3 || !MI.getOperand(0).isReg() ||
              !MI.getOperand(1).isReg() || !MI.getOperand(2).isReg()) {
            Bad = true;
            break;
          }

          Register DstReg = MI.getOperand(0).getReg();
          Register LHSReg = MI.getOperand(1).getReg();
          Register RHSReg = MI.getOperand(2).getReg();
          if (LHSReg == DstReg && RHSReg == ConstReg) {
            ValueReg = LHSReg;
          } else if (RHSReg == DstReg && LHSReg == ConstReg) {
            ValueReg = RHSReg;
          } else {
            Bad = true;
            break;
          }

          if (!ValueReg.isPhysical() || ValueReg == ConstReg) {
            Bad = true;
            break;
          }

          MulMI = &MI;
          MulMBB = &MBB;
        }

        if (Bad)
          break;
      }

      if (Bad || Uses != 1 || !MulMI || !MulMBB)
        continue;

      for (MachineInstr *DefMI : OtherDefs) {
        if (canDefinitionReachUse(*DefMI, *MulMI)) {
          Bad = true;
          break;
        }
      }
      if (Bad)
        continue;

      auto MulI = MulMI->getIterator();
      if (!flagsAreDeadAfter(MulI, *MulMBB))
        continue;

      Register DstReg = MulMI->getOperand(0).getReg();
      DebugLoc DL = MulMI->getDebugLoc();
      BuildMI(*MulMBB, MulI, DL, TII.get(Bedrock::MOVQrr), ConstReg)
          .addReg(ValueReg);
      BuildMI(*MulMBB, MulI, DL, TII.get(Bedrock::ADDL3rr), DstReg)
          .addReg(ValueReg)
          .addReg(ValueReg)
          .setMIFlags(MulMI->getFlags());
      BuildMI(*MulMBB, MulI, DL, TII.get(Bedrock::ADDL3rr), DstReg)
          .addReg(DstReg)
          .addReg(ConstReg, RegState::Kill)
          .setMIFlags(MulMI->getFlags());

      LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding single-use "
                           "constant-three multiply: ";
                 I->dump(); MulMI->dump());

      MulMI->eraseFromParent();
      I->eraseFromParent();
      for (MachineBasicBlock &MBB : MF)
        MBB.removeLiveIn(ConstReg.asMCReg());

      ++NumMulByThreeFolded;
      return true;
    }
  }

  return false;
}

static bool getMulImmOpcode(unsigned RegOpcode, unsigned &ImmOpcode) {
  switch (RegOpcode) {
  case Bedrock::MULL3rr:
    ImmOpcode = Bedrock::MULL3ri;
    return true;
  case Bedrock::MULQ3rr:
    ImmOpcode = Bedrock::MULQ3ri;
    return true;
  default:
    return false;
  }
}

static bool getPowerPlusOneShift(int64_t Imm, unsigned &Shift);

static bool shouldFoldMaterializedMulImmediate(const MachineInstr &ConstMI,
                                               int64_t Imm) {
  if (Imm == 0 || Imm == 1 || Imm == 3 || Imm == -1)
    return false;

  unsigned Shift;
  if (getPowerPlusOneShift(Imm, Shift) && getSignedAutoImmSize(Imm) < 2)
    return false;

  return getConstMaterializationSize(ConstMI) > getSignedAutoImmSize(Imm);
}

struct MaterializedMulImmediateUse {
  MachineInstr *MulMI = nullptr;
  MachineBasicBlock *MulMBB = nullptr;
  Register ValueReg;
  bool ValueRegKilled = false;
  unsigned NewMulOpcode = 0;
};

bool BedrockPreEmitPeephole::foldAdjacentMaterializedMulImmediate(
    MachineBasicBlock::iterator &I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &ConstMI = *I;
  if ((ConstMI.getOpcode() != Bedrock::CONST32 &&
       ConstMI.getOpcode() != Bedrock::CONST64) ||
      ConstMI.getNumExplicitOperands() < 2 || !ConstMI.getOperand(0).isReg() ||
      !ConstMI.getOperand(1).isImm())
    return false;

  int64_t Imm = ConstMI.getOperand(1).getImm();
  if (!shouldFoldMaterializedMulImmediate(ConstMI, Imm))
    return false;

  Register ConstReg = ConstMI.getOperand(0).getReg();
  if (!ConstReg.isPhysical() || !Bedrock::GPR64RegClass.contains(ConstReg))
    return false;

  auto MulI = std::next(I);
  while (MulI != MBB.end() && MulI->isDebugInstr())
    ++MulI;
  if (MulI == MBB.end())
    return false;

  unsigned NewMulOpcode;
  if (!getMulImmOpcode(MulI->getOpcode(), NewMulOpcode) ||
      MulI->getNumExplicitOperands() < 3 || !MulI->getOperand(0).isReg() ||
      !MulI->getOperand(1).isReg() || !MulI->getOperand(2).isReg())
    return false;

  Register ValueReg;
  bool ValueRegKilled = false;
  if (MulI->getOperand(1).getReg() == ConstReg) {
    if (!MulI->getOperand(1).isKill())
      return false;
    ValueReg = MulI->getOperand(2).getReg();
    ValueRegKilled = MulI->getOperand(2).isKill();
  } else if (MulI->getOperand(2).getReg() == ConstReg) {
    if (!MulI->getOperand(2).isKill())
      return false;
    ValueReg = MulI->getOperand(1).getReg();
    ValueRegKilled = MulI->getOperand(1).isKill();
  } else {
    return false;
  }

  if (!ValueReg.isPhysical() || ValueReg == ConstReg)
    return false;

  auto NextI = std::next(MulI);
  DebugLoc DL = MulI->getDebugLoc();
  Register DstReg = MulI->getOperand(0).getReg();
  BuildMI(MBB, MulI, DL, TII.get(NewMulOpcode), DstReg)
      .addReg(ValueReg, getKillRegState(ValueRegKilled))
      .addImm(Imm)
      .setMIFlags(MulI->getFlags());

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding adjacent "
                       "materialized multiply immediate: ";
             ConstMI.dump(); MulI->dump());

  MulI->eraseFromParent();
  ConstMI.eraseFromParent();
  I = NextI;
  ++NumMaterializedMulImmediatesFolded;
  return true;
}

static bool getPowerPlusOneShift(int64_t Imm, unsigned &Shift) {
  if (Imm <= 4)
    return false;

  for (unsigned Candidate = 2; Candidate <= 30; ++Candidate) {
    if ((1LL << Candidate) + 1 == Imm) {
      Shift = Candidate;
      return true;
    }
  }

  return false;
}

bool BedrockPreEmitPeephole::foldSingleUseMaterializedMulImmediate(
    MachineFunction &MF, const TargetInstrInfo &TII) {
  if (MF.empty())
    return false;

  for (MachineBasicBlock &ConstMBB : MF) {
    for (auto I = ConstMBB.begin(), E = ConstMBB.end(); I != E; ++I) {
      if (I->isDebugInstr())
        continue;
      if (I->isTerminator())
        break;

      if ((I->getOpcode() != Bedrock::CONST32 &&
           I->getOpcode() != Bedrock::CONST64) ||
          I->getNumExplicitOperands() < 2 || !I->getOperand(0).isReg() ||
          !I->getOperand(1).isImm())
        continue;

      int64_t Imm = I->getOperand(1).getImm();
      if (!shouldFoldMaterializedMulImmediate(*I, Imm))
        continue;

      Register ConstReg = I->getOperand(0).getReg();
      if (!ConstReg.isPhysical() || !Bedrock::GPR64RegClass.contains(ConstReg))
        continue;

      SmallVector<MaterializedMulImmediateUse, 4> MulUses;
      SmallVector<MachineInstr *, 4> OtherDefs;
      bool Bad = false;
      bool SawUse = false;

      for (MachineBasicBlock &MBB : MF) {
        if (&MBB != &ConstMBB && !canReachBlock(&ConstMBB, &MBB))
          continue;

        bool SawConstDef = &MBB != &ConstMBB;
        bool ShadowedByOtherDef = false;
        for (MachineInstr &MI : MBB) {
          if (&MBB == &ConstMBB) {
            if (&MI == &*I) {
              SawConstDef = true;
              ShadowedByOtherDef = false;
              continue;
            }
            if (!SawConstDef)
              continue;
          }

          if (MI.isDebugInstr())
            continue;

          if (definesReg(MI, ConstReg)) {
            if (!SawUse)
              OtherDefs.push_back(&MI);
            ShadowedByOtherDef = true;
            continue;
          }

          if (ShadowedByOtherDef)
            continue;

          if (!readsReg(MI, ConstReg))
            continue;

          unsigned NewMulOpcode = 0;
          if (!getMulImmOpcode(MI.getOpcode(), NewMulOpcode) ||
              MI.getNumExplicitOperands() < 3 || !MI.getOperand(0).isReg() ||
              !MI.getOperand(1).isReg() || !MI.getOperand(2).isReg()) {
            Bad = true;
            break;
          }

          Register LHSReg = MI.getOperand(1).getReg();
          Register RHSReg = MI.getOperand(2).getReg();
          Register ValueReg;
          bool ValueRegKilled = false;
          if (LHSReg == ConstReg) {
            ValueReg = RHSReg;
            ValueRegKilled = MI.getOperand(2).isKill();
          } else if (RHSReg == ConstReg) {
            ValueReg = LHSReg;
            ValueRegKilled = MI.getOperand(1).isKill();
          } else {
            Bad = true;
            break;
          }

          if (!ValueReg.isPhysical() || ValueReg == ConstReg) {
            Bad = true;
            break;
          }

          MulUses.push_back(
              {&MI, &MBB, ValueReg, ValueRegKilled, NewMulOpcode});
          SawUse = true;
        }

        if (Bad)
          break;
      }

      if (Bad || MulUses.empty())
        continue;
      if (getConstMaterializationSize(*I) <=
          MulUses.size() * getSignedAutoImmSize(Imm))
        continue;

      for (MachineInstr *DefMI : OtherDefs) {
        for (const MaterializedMulImmediateUse &Use : MulUses) {
          if (canDefinitionReachUse(*DefMI, *Use.MulMI)) {
            Bad = true;
            break;
          }
        }
        if (Bad)
          break;
      }
      if (Bad)
        continue;

      LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding materialized "
                           "multiply immediates: ";
                 I->dump());

      for (const MaterializedMulImmediateUse &Use : MulUses) {
        auto MulI = Use.MulMI->getIterator();
        DebugLoc DL = Use.MulMI->getDebugLoc();
        Register DstReg = Use.MulMI->getOperand(0).getReg();
        BuildMI(*Use.MulMBB, MulI, DL, TII.get(Use.NewMulOpcode), DstReg)
            .addReg(Use.ValueReg, getKillRegState(Use.ValueRegKilled))
            .addImm(Imm)
            .setMIFlags(Use.MulMI->getFlags());
        Use.MulMI->eraseFromParent();
      }
      I->eraseFromParent();
      for (MachineBasicBlock &MBB : MF)
        MBB.removeLiveIn(ConstReg.asMCReg());
      while (foldDeadCalleeSavedPairs(MF)) {
      }

      NumMaterializedMulImmediatesFolded += MulUses.size();
      return true;
    }
  }

  return false;
}

bool BedrockPreEmitPeephole::foldSingleUseConstPowerPlusOneMul(
    MachineFunction &MF, const TargetInstrInfo &TII) {
  if (MF.empty())
    return false;

  MachineBasicBlock &EntryMBB = MF.front();
  for (auto I = EntryMBB.begin(), E = EntryMBB.end(); I != E; ++I) {
    if (I->isDebugInstr())
      continue;
    if (I->isTerminator())
      break;

    if ((I->getOpcode() != Bedrock::CONST32 &&
         I->getOpcode() != Bedrock::CONST64) ||
        I->getNumExplicitOperands() < 2 || !I->getOperand(0).isReg() ||
        !I->getOperand(1).isImm())
      continue;

    int64_t Imm = I->getOperand(1).getImm();
    unsigned Shift;
    if (!getPowerPlusOneShift(Imm, Shift))
      continue;
    if (getSignedAutoImmSize(Imm) >= 2)
      continue;

    Register ConstReg = I->getOperand(0).getReg();
    if (!ConstReg.isPhysical() || !Bedrock::GPR64RegClass.contains(ConstReg))
      continue;

    MachineInstr *MulMI = nullptr;
    MachineBasicBlock *MulMBB = nullptr;
    SmallVector<MachineInstr *, 4> OtherDefs;
    Register ValueReg;
    bool ValueRegKilled = false;
    bool Is64 = false;
    bool Bad = false;
    unsigned Uses = 0;

    for (MachineBasicBlock &MBB : MF) {
      bool SawConstDef = &MBB != &EntryMBB;
      bool ShadowedByOtherDef = false;
      for (MachineInstr &MI : MBB) {
        if (&MBB == &EntryMBB) {
          if (&MI == &*I) {
            SawConstDef = true;
            ShadowedByOtherDef = false;
            continue;
          }
          if (!SawConstDef)
            continue;
        }

        if (MI.isDebugInstr())
          continue;

        if (definesReg(MI, ConstReg)) {
          OtherDefs.push_back(&MI);
          ShadowedByOtherDef = true;
          continue;
        }

        if (ShadowedByOtherDef)
          continue;

        if (!readsReg(MI, ConstReg))
          continue;

        ++Uses;
        if (Uses != 1 ||
            (MI.getOpcode() != Bedrock::MULL3rr &&
             MI.getOpcode() != Bedrock::MULQ3rr) ||
            MI.getNumExplicitOperands() < 3 || !MI.getOperand(0).isReg() ||
            !MI.getOperand(1).isReg() || !MI.getOperand(2).isReg()) {
          Bad = true;
          break;
        }

        Register DstReg = MI.getOperand(0).getReg();
        Register LHSReg = MI.getOperand(1).getReg();
        Register RHSReg = MI.getOperand(2).getReg();
        if (LHSReg == ConstReg && RHSReg == DstReg) {
          ValueReg = RHSReg;
          ValueRegKilled = MI.getOperand(2).isKill();
        } else if (RHSReg == ConstReg && LHSReg == DstReg) {
          ValueReg = LHSReg;
          ValueRegKilled = MI.getOperand(1).isKill();
        } else if (LHSReg == ConstReg) {
          ValueReg = RHSReg;
          ValueRegKilled = MI.getOperand(2).isKill();
        } else if (RHSReg == ConstReg) {
          ValueReg = LHSReg;
          ValueRegKilled = MI.getOperand(1).isKill();
        } else {
          Bad = true;
          break;
        }

        if (!ValueReg.isPhysical() || ValueReg == ConstReg) {
          Bad = true;
          break;
        }

        Is64 = MI.getOpcode() == Bedrock::MULQ3rr;
        MulMI = &MI;
        MulMBB = &MBB;
      }

      if (Bad)
        break;
    }

    if (Bad || Uses != 1 || !MulMI || !MulMBB)
      continue;

    for (MachineInstr *DefMI : OtherDefs) {
      if (canDefinitionReachUse(*DefMI, *MulMI)) {
        Bad = true;
        break;
      }
    }
    if (Bad)
      continue;

    auto MulI = MulMI->getIterator();
    if (!flagsAreDeadAfter(MulI, *MulMBB))
      continue;

    Register DstReg = MulMI->getOperand(0).getReg();
    DebugLoc DL = MulMI->getDebugLoc();
    BuildMI(*MulMBB, MulI, DL,
            TII.get(Is64 ? Bedrock::MULQ3ri : Bedrock::MULL3ri), DstReg)
        .addReg(ValueReg, getKillRegState(ValueRegKilled))
        .addImm(Imm)
        .setMIFlags(MulMI->getFlags());

    LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding single-use "
                         "power-plus-one multiply to immediate: ";
               I->dump(); MulMI->dump());

    MulMI->eraseFromParent();
    I->eraseFromParent();
    for (MachineBasicBlock &MBB : MF)
      MBB.removeLiveIn(ConstReg.asMCReg());
    while (foldDeadCalleeSavedPairs(MF)) {
    }

    ++NumPowerPlusOneMulFolded;
    return true;
  }

  return false;
}

static MachineBasicBlock::iterator
nextNonDebugInBlock(MachineBasicBlock::iterator I, MachineBasicBlock &MBB) {
  ++I;
  while (I != MBB.end() && I->isDebugInstr())
    ++I;
  return I;
}

static bool canReachBlock(const MachineBasicBlock *From,
                          const MachineBasicBlock *To) {
  SmallPtrSet<const MachineBasicBlock *, 8> Seen;
  SmallVector<const MachineBasicBlock *, 8> Worklist;
  Worklist.push_back(From);
  Seen.insert(From);

  while (!Worklist.empty()) {
    const MachineBasicBlock *MBB = Worklist.pop_back_val();
    for (const MachineBasicBlock *Succ : MBB->successors()) {
      if (Succ == To)
        return true;
      if (Seen.insert(Succ).second)
        Worklist.push_back(Succ);
    }
  }

  return false;
}

static bool canDefinitionReachUse(const MachineInstr &DefMI,
                                  const MachineInstr &UseMI) {
  const MachineBasicBlock *DefMBB = DefMI.getParent();
  const MachineBasicBlock *UseMBB = UseMI.getParent();
  if (DefMBB == UseMBB)
    return true;
  return canReachBlock(DefMBB, UseMBB);
}

static bool getConstImmDef(const MachineInstr &MI, Register Reg,
                           int64_t &Imm) {
  if (MI.getNumExplicitOperands() < 2 || !MI.getOperand(0).isReg() ||
      MI.getOperand(0).getReg() != Reg || !MI.getOperand(1).isImm())
    return false;

  switch (MI.getOpcode()) {
  case Bedrock::CONST32:
  case Bedrock::CONST64:
    Imm = MI.getOperand(1).getImm();
    return true;
  default:
    return false;
  }
}

static MachineInstr *findConstDefBeforeTerminator(MachineBasicBlock &MBB,
                                                  Register Reg,
                                                  int64_t &Imm) {
  MachineInstr *DefMI = nullptr;
  for (MachineInstr &MI : MBB) {
    if (MI.isDebugInstr())
      continue;
    if (MI.isTerminator())
      break;
    if (!MI.getNumExplicitOperands() || !MI.getOperand(0).isReg() ||
        !MI.getOperand(0).isDef() ||
        MI.getOperand(0).getReg() != Reg)
      continue;
    if (!getConstImmDef(MI, Reg, Imm))
      return nullptr;
    DefMI = &MI;
  }
  return DefMI;
}

static bool hasCall(const MachineBasicBlock &MBB) {
  for (const MachineInstr &MI : MBB) {
    if (MI.isCall())
      return true;
  }
  return false;
}

static bool isLoopAccumulatorReg(Register Reg) {
  switch (Reg.id()) {
  case Bedrock::R8:
  case Bedrock::R9:
  case Bedrock::R10:
  case Bedrock::R11:
  case Bedrock::R12:
  case Bedrock::R13:
  case Bedrock::R14:
    return true;
  default:
    return false;
  }
}

static bool isIncrementOfReg(const MachineInstr &MI, Register Reg) {
  switch (MI.getOpcode()) {
  case Bedrock::INCL3r:
  case Bedrock::INCQ3r:
    return MI.getNumExplicitOperands() >= 2 && MI.getOperand(0).isReg() &&
           MI.getOperand(1).isReg() && MI.getOperand(0).getReg() == Reg &&
           MI.getOperand(1).getReg() == Reg;
  default:
    return false;
  }
}

bool BedrockPreEmitPeephole::foldAffineMulLoop(MachineFunction &MF,
                                               const TargetInstrInfo &TII) {
  for (MachineBasicBlock &Header : MF) {
    if (Header.succ_size() != 2)
      continue;

    MachineBasicBlock *Body = nullptr;
    for (MachineBasicBlock *Succ : Header.successors()) {
      if (Succ != &Header && Succ->succ_size() == 1 &&
          *Succ->succ_begin() == &Header) {
        Body = Succ;
        break;
      }
    }
    if (!Body || hasCall(*Body))
      continue;

    MachineBasicBlock *Preheader = nullptr;
    bool BadPred = false;
    for (MachineBasicBlock *Pred : Header.predecessors()) {
      if (Pred == Body)
        continue;
      if (Preheader) {
        BadPred = true;
        break;
      }
      Preheader = Pred;
    }
    if (BadPred || !Preheader)
      continue;

    MachineInstr *MovMI = nullptr;
    MachineInstr *MulMI = nullptr;
    MachineInstr *AddOffsetMI = nullptr;
    MachineInstr *UpdateMI = nullptr;
    Register TmpReg;
    Register IVReg;
    Register ScaleReg;
    int64_t Offset = 0;

    for (auto I = Body->begin(), E = Body->end(); I != E; ++I) {
      if (I->isDebugInstr())
        continue;

      if (!MovMI && I->getOpcode() == Bedrock::MOVQrr &&
          I->getNumExplicitOperands() >= 2 && I->getOperand(0).isReg() &&
          I->getOperand(1).isReg()) {
        auto MulI = nextNonDebugInBlock(I, *Body);
        if (MulI == Body->end() || MulI->getOpcode() != Bedrock::MULL3rr ||
            MulI->getNumExplicitOperands() < 3 ||
            !MulI->getOperand(0).isReg() || !MulI->getOperand(1).isReg() ||
            !MulI->getOperand(2).isReg())
          continue;

        Register CandidateTmp = I->getOperand(0).getReg();
        Register CandidateIV = I->getOperand(1).getReg();
        if (MulI->getOperand(0).getReg() != CandidateTmp ||
            MulI->getOperand(1).getReg() != CandidateTmp)
          continue;

        TmpReg = CandidateTmp;
        IVReg = CandidateIV;
        ScaleReg = MulI->getOperand(2).getReg();
        if (!TmpReg.isPhysical() || !IVReg.isPhysical() ||
            !ScaleReg.isPhysical() ||
            !Bedrock::GPR64RegClass.contains(TmpReg) ||
            !Bedrock::GPR64RegClass.contains(IVReg) ||
            !Bedrock::GPR64RegClass.contains(ScaleReg) ||
            TmpReg == IVReg || TmpReg == ScaleReg || IVReg == ScaleReg)
          continue;

        MovMI = &*I;
        MulMI = &*MulI;
        continue;
      }

      if (MovMI && !AddOffsetMI && I->getOpcode() == Bedrock::ADDL3ri &&
          I->getNumExplicitOperands() >= 3 && I->getOperand(0).isReg() &&
          I->getOperand(1).isReg() && I->getOperand(2).isImm() &&
          I->getOperand(0).getReg() == TmpReg &&
          I->getOperand(1).getReg() == TmpReg) {
        AddOffsetMI = &*I;
        Offset = I->getOperand(2).getImm();
        continue;
      }

      if (MovMI && isIncrementOfReg(*I, IVReg)) {
        UpdateMI = &*I;
        continue;
      }
    }

    if (!MovMI || !MulMI || !AddOffsetMI || !UpdateMI)
      continue;

    int64_t IVInit = 0;
    MachineInstr *IVInitMI =
        findConstDefBeforeTerminator(*Preheader, IVReg, IVInit);
    int64_t Scale = 0;
    MachineInstr *ScaleMI =
        findConstDefBeforeTerminator(*Preheader, ScaleReg, Scale);
    if (!IVInitMI || IVInit != 0 || !ScaleMI)
      continue;

    if (Header.isLiveIn(TmpReg.asMCReg()) || Body->isLiveIn(TmpReg.asMCReg()))
      continue;

    bool SawMul = false;
    bool Unsafe = false;
    for (MachineInstr &MI : *Body) {
      if (&MI == MovMI || &MI == MulMI || &MI == AddOffsetMI ||
          MI.isDebugInstr())
        continue;
      if (definesReg(MI, TmpReg)) {
        Unsafe = true;
        break;
      }
      if (&MI == UpdateMI)
        break;
      if (definesReg(MI, IVReg) && &MI != UpdateMI) {
        Unsafe = true;
        break;
      }
      if (readsReg(MI, ScaleReg)) {
        Unsafe = true;
        break;
      }
      if (&MI == MulMI)
        SawMul = true;
    }
    if (Unsafe)
      continue;

    unsigned ScaleReads = 0;
    bool ScaleRedefined = false;
    for (MachineInstr &MI : *Body) {
      if (MI.isDebugInstr())
        continue;
      if (&MI == MulMI)
        SawMul = true;
      if (readsReg(MI, ScaleReg))
        ++ScaleReads;
      if (&MI != MulMI && definesReg(MI, ScaleReg)) {
        ScaleRedefined = true;
        break;
      }
    }
    if (!SawMul || ScaleReads != 1 || ScaleRedefined)
      continue;

    DebugLoc DL = AddOffsetMI->getDebugLoc();
    BuildMI(*Preheader, Preheader->getFirstTerminator(), DL,
            TII.get(Bedrock::CONST32), TmpReg)
        .addImm(Offset);
    Header.addLiveIn(TmpReg.asMCReg());
    Body->addLiveIn(TmpReg.asMCReg());

    for (MachineInstr &MI : *Body) {
      for (MachineOperand &MO : MI.operands()) {
        if (MO.isReg() && MO.getReg() == TmpReg && MO.isKill())
          MO.setIsKill(false);
      }
    }

    BuildMI(*Body, UpdateMI->getIterator(), UpdateMI->getDebugLoc(),
            TII.get(Bedrock::ADDL3ri), TmpReg)
        .addReg(TmpReg, RegState::Kill)
        .addImm(Scale);

    LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding affine multiply "
                         "loop in ";
               MF.getFunction().printAsOperand(dbgs(), false); dbgs() << "\n");

    MovMI->eraseFromParent();
    MulMI->eraseFromParent();
    AddOffsetMI->eraseFromParent();
    ScaleMI->eraseFromParent();
    Header.removeLiveIn(ScaleReg.asMCReg());
    Body->removeLiveIn(ScaleReg.asMCReg());

    ++NumAffineMulLoopsFolded;
    return true;
  }

  return false;
}

bool BedrockPreEmitPeephole::foldCalleeSavedAffineMulLoop(
    MachineFunction &MF, const TargetInstrInfo &TII) {
  for (MachineBasicBlock &Header : MF) {
    if (Header.succ_size() != 2)
      continue;

    MachineBasicBlock *Body = nullptr;
    MachineBasicBlock *Exit = nullptr;
    bool IsSelfLoop = false;
    for (MachineBasicBlock *Succ : Header.successors()) {
      if (Succ == &Header) {
        Body = &Header;
        IsSelfLoop = true;
      } else if (Succ->succ_size() == 1 && *Succ->succ_begin() == &Header) {
        Body = Succ;
      } else {
        Exit = Succ;
      }
    }
    if (!Body || !Exit)
      continue;

    MachineBasicBlock *Preheader = nullptr;
    bool BadPred = false;
    for (MachineBasicBlock *Pred : Header.predecessors()) {
      if (Pred == Body)
        continue;
      if (Preheader) {
        BadPred = true;
        break;
      }
      Preheader = Pred;
    }
    if (BadPred || !Preheader)
      continue;

    MachineInstr *MovMI = nullptr;
    MachineInstr *MulMI = nullptr;
    MachineInstr *UpdateMI = nullptr;
    Register TmpReg;
    Register IVReg;
    Register ScaleReg;

    for (auto I = Body->begin(), E = Body->end(); I != E; ++I) {
      if (I->isDebugInstr())
        continue;

      if (!MovMI && I->getOpcode() == Bedrock::MOVQrr &&
          I->getNumExplicitOperands() >= 2 && I->getOperand(0).isReg() &&
          I->getOperand(1).isReg()) {
        auto MulI = nextNonDebugInBlock(I, *Body);
        if (MulI == Body->end() || MulI->getOpcode() != Bedrock::MULL3rr ||
            MulI->getNumExplicitOperands() < 3 ||
            !MulI->getOperand(0).isReg() || !MulI->getOperand(1).isReg() ||
            !MulI->getOperand(2).isReg())
          continue;

        Register CandidateTmp = I->getOperand(0).getReg();
        Register CandidateIV = I->getOperand(1).getReg();
        Register CandidateScale = MulI->getOperand(2).getReg();
        if (MulI->getOperand(0).getReg() != CandidateTmp ||
            MulI->getOperand(1).getReg() != CandidateTmp)
          continue;

        if (!CandidateTmp.isPhysical() || !CandidateIV.isPhysical() ||
            !CandidateScale.isPhysical() ||
            !Bedrock::GPR64RegClass.contains(CandidateTmp) ||
            !Bedrock::GPR64RegClass.contains(CandidateIV) ||
            !Bedrock::GPR64RegClass.contains(CandidateScale) ||
            CandidateTmp == CandidateIV || CandidateTmp == CandidateScale ||
            CandidateIV == CandidateScale ||
            !isLoopAccumulatorReg(CandidateScale))
          continue;

        MovMI = &*I;
        MulMI = &*MulI;
        TmpReg = CandidateTmp;
        IVReg = CandidateIV;
        ScaleReg = CandidateScale;
        continue;
      }

      if (MovMI && isIncrementOfReg(*I, IVReg)) {
        UpdateMI = &*I;
        continue;
      }
    }

    if (!MovMI || !MulMI || !UpdateMI)
      continue;

    if (Exit->isLiveIn(ScaleReg.asMCReg()))
      continue;

    int64_t IVInit = 0;
    MachineInstr *IVInitMI =
        findConstDefBeforeTerminator(*Preheader, IVReg, IVInit);
    int64_t Scale = 0;
    MachineInstr *ScaleMI =
        findConstDefBeforeTerminator(*Preheader, ScaleReg, Scale);
    if (!IVInitMI || IVInit != 0 || !ScaleMI || Scale == 0)
      continue;

    if (!IsSelfLoop) {
      bool HeaderTouchesScale = false;
      for (MachineInstr &MI : Header) {
        if (MI.isDebugInstr())
          continue;
        if (readsReg(MI, ScaleReg) || definesReg(MI, ScaleReg)) {
          HeaderTouchesScale = true;
          break;
        }
      }
      if (HeaderTouchesScale)
        continue;
    }

    unsigned ScaleReads = 0;
    bool Unsafe = false;
    for (MachineInstr &MI : *Body) {
      if (MI.isDebugInstr())
        continue;
      if (readsReg(MI, ScaleReg))
        ++ScaleReads;
      if (&MI != MulMI && definesReg(MI, ScaleReg)) {
        Unsafe = true;
        break;
      }
    }
    if (Unsafe || ScaleReads != 1)
      continue;

    MachineInstr *LastUseMI = nullptr;
    for (auto I = nextNonDebugInBlock(MulMI->getIterator(), *Body),
              E = Body->end();
         I != E; ++I) {
      if (I->isDebugInstr())
        continue;

      bool DefinesTmp = definesReg(*I, TmpReg);
      if (DefinesTmp && readsReg(*I, TmpReg)) {
        BuildMI(*Body, I, I->getDebugLoc(), TII.get(Bedrock::MOVQrr), TmpReg)
            .addReg(ScaleReg);
        LastUseMI = &*I;
        break;
      }

      bool Replaced = false;
      for (MachineOperand &MO : I->operands()) {
        if (MO.isReg() && !MO.isDef() && MO.getReg() == TmpReg) {
          MO.setReg(ScaleReg);
          MO.setIsKill(false);
          Replaced = true;
        }
      }
      if (Replaced)
        LastUseMI = &*I;
      if (DefinesTmp)
        break;
    }
    if (!LastUseMI)
      continue;

    auto LastUseI = LastUseMI->getIterator();
    if (!flagsAreDeadAfter(LastUseI, *Body))
      continue;

    ScaleMI->getOperand(1).setImm(0);
    BuildMI(*Body, std::next(LastUseI), LastUseMI->getDebugLoc(),
            TII.get(Bedrock::ADDL3ri), ScaleReg)
        .addReg(ScaleReg, RegState::Kill)
        .addImm(Scale);

    LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding callee-saved "
                         "affine multiply loop in ";
               MF.getFunction().printAsOperand(dbgs(), false); dbgs() << "\n");

    MovMI->eraseFromParent();
    MulMI->eraseFromParent();

    ++NumCalleeSavedAffineMulLoopsFolded;
    return true;
  }

  return false;
}

static bool isGPR(Register Reg);
static bool isCalleeSavedGPR(Register Reg);
static bool hasKnownOrderedMemoryRef(const MachineInstr &MI);
static bool hasUseOrLiveOutOfRegBeforeDef(MachineBasicBlock::iterator I,
                                          MachineBasicBlock &MBB,
                                          Register Reg);

static MachineBasicBlock::iterator
prevNonDebugInBlock(MachineBasicBlock::iterator I, MachineBasicBlock &MBB) {
  while (I != MBB.begin()) {
    --I;
    if (!I->isDebugInstr())
      return I;
  }
  return MBB.end();
}

static bool isRegImmCompare(const MachineInstr &MI, Register Reg,
                            int64_t &Imm) {
  switch (MI.getOpcode()) {
  case Bedrock::CMPLri:
  case Bedrock::CMPQri:
    break;
  default:
    return false;
  }

  if (MI.getNumExplicitOperands() < 2 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isImm() || MI.getOperand(0).getReg() != Reg)
    return false;
  Imm = MI.getOperand(1).getImm();
  return true;
}

bool BedrockPreEmitPeephole::foldSelfLoopIVCopy(
    MachineFunction &MF, const TargetInstrInfo &TII) {
  (void)TII;
  for (MachineBasicBlock &MBB : MF) {
    if (MBB.succ_size() != 2)
      continue;

    bool HasSelfSucc = false;
    for (MachineBasicBlock *Succ : MBB.successors())
      HasSelfSucc |= Succ == &MBB;
    if (!HasSelfSucc)
      continue;

    MachineBasicBlock *Preheader = nullptr;
    bool BadPred = false;
    for (MachineBasicBlock *Pred : MBB.predecessors()) {
      if (Pred == &MBB)
        continue;
      if (Preheader) {
        BadPred = true;
        break;
      }
      Preheader = Pred;
    }
    if (BadPred || !Preheader)
      continue;

    MachineBasicBlock::iterator BranchI = MBB.end();
    for (auto I = MBB.begin(), E = MBB.end(); I != E; ++I) {
      if (I->isDebugInstr())
        continue;
      if (I->getOpcode() == Bedrock::BRCC &&
          I->getNumExplicitOperands() >= 2 && I->getOperand(0).isMBB() &&
          I->getOperand(0).getMBB() == &MBB &&
          I->getOperand(1).isImm() && I->getOperand(1).getImm() == 4) {
        BranchI = I;
        break;
      }
    }
    if (BranchI == MBB.end())
      continue;

    auto CopyI = prevNonDebugInBlock(BranchI, MBB);
    if (CopyI == MBB.end() || CopyI->getOpcode() != Bedrock::MOVQrr ||
        CopyI->getNumExplicitOperands() < 2 || !CopyI->getOperand(0).isReg() ||
        !CopyI->getOperand(1).isReg())
      continue;

    Register OldReg = CopyI->getOperand(0).getReg();
    Register NextReg = CopyI->getOperand(1).getReg();
    if (OldReg == NextReg || !OldReg.isPhysical() || !NextReg.isPhysical() ||
        !isGPR(OldReg) || !isGPR(NextReg))
      continue;

    MachineBasicBlock::iterator UpdateI = MBB.end();
    for (auto I = CopyI; I != MBB.begin();) {
      --I;
      if (I->isDebugInstr())
        continue;
      if (isIncrementOfReg(*I, NextReg)) {
        UpdateI = I;
        break;
      }
    }
    if (UpdateI == MBB.end())
      continue;

    int64_t OldInit = 0;
    int64_t NextInit = 0;
    MachineInstr *OldInitMI =
        findConstDefBeforeTerminator(*Preheader, OldReg, OldInit);
    MachineInstr *NextInitMI =
        findConstDefBeforeTerminator(*Preheader, NextReg, NextInit);
    if (!OldInitMI || !NextInitMI || OldInit != NextInit)
      continue;

    if (hasCall(MBB) && !isCalleeSavedGPR(NextReg))
      continue;

    SmallVector<MachineInstr *, 8> RewriteBeforeUpdate;
    bool Unsafe = false;
    for (auto I = MBB.begin(); I != UpdateI; ++I) {
      if (I->isDebugInstr())
        continue;
      if (definesReg(*I, OldReg) || definesReg(*I, NextReg)) {
        Unsafe = true;
        break;
      }
      if (readsReg(*I, OldReg))
        RewriteBeforeUpdate.push_back(&*I);
    }
    if (Unsafe || RewriteBeforeUpdate.empty())
      continue;

    MachineInstr *CmpMI = nullptr;
    int64_t CmpImm = 0;
    for (auto I = std::next(UpdateI); I != CopyI; ++I) {
      if (I->isDebugInstr())
        continue;
      if (definesReg(*I, OldReg) || definesReg(*I, NextReg)) {
        Unsafe = true;
        break;
      }
      if (readsReg(*I, OldReg)) {
        if (CmpMI || !isRegImmCompare(*I, OldReg, CmpImm)) {
          Unsafe = true;
          break;
        }
        CmpMI = &*I;
        continue;
      }
      if (CmpMI && writesFlags(*I)) {
        Unsafe = true;
        break;
      }
    }
    if (Unsafe || !CmpMI || CmpImm == std::numeric_limits<int64_t>::max())
      continue;

    LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding self-loop IV "
                         "copy in ";
               MF.getFunction().printAsOperand(dbgs(), false); dbgs() << "\n");

    for (MachineInstr *RewriteMI : RewriteBeforeUpdate) {
      for (MachineOperand &MO : RewriteMI->operands()) {
        if (MO.isReg() && !MO.isDef() && MO.getReg() == OldReg) {
          MO.setReg(NextReg);
          MO.setIsKill(false);
        }
      }
    }

    CmpMI->getOperand(0).setReg(NextReg);
    CmpMI->getOperand(0).setIsKill(false);
    CmpMI->getOperand(1).setImm(CmpImm + 1);

    MBB.removeLiveIn(OldReg.asMCReg());
    CopyI->eraseFromParent();
    if (!hasUseOrLiveOutOfRegBeforeDef(std::next(OldInitMI->getIterator()),
                                       *Preheader, OldReg))
      OldInitMI->eraseFromParent();

    ++NumSelfLoopIVCopiesFolded;
    return true;
  }

  return false;
}

static unsigned getDeferredLeaCopyMemCmpOpcode(unsigned LoadOpcode,
                                               unsigned CmpOpcode) {
  if (CmpOpcode == Bedrock::CMPLrr) {
    switch (LoadOpcode) {
    case Bedrock::LOADL_Zrr:
    case Bedrock::LOADL_Srr:
    case Bedrock::LOADLrr:
      return Bedrock::CMPLrm;
    default:
      return 0;
    }
  }

  if (CmpOpcode == Bedrock::CMPQrr && LoadOpcode == Bedrock::LOADQrr)
    return Bedrock::CMPQrm;

  return 0;
}

bool BedrockPreEmitPeephole::foldDeferredLeaCopy(
    MachineFunction &MF, const TargetInstrInfo &TII) {
  for (MachineBasicBlock &Pred : MF) {
    if (Pred.succ_empty())
      continue;

    for (auto LeaI = Pred.begin(), LeaE = Pred.end(); LeaI != LeaE; ++LeaI) {
      if (LeaI->isDebugInstr() || LeaI->getOpcode() != Bedrock::LEAro ||
          LeaI->getNumExplicitOperands() < 3 || !LeaI->getOperand(0).isReg() ||
          !LeaI->getOperand(1).isReg() || !LeaI->getOperand(2).isImm())
        continue;

      Register TmpReg = LeaI->getOperand(0).getReg();
      Register BaseReg = LeaI->getOperand(1).getReg();
      if (TmpReg == BaseReg || !TmpReg.isPhysical() ||
          !BaseReg.isPhysical() || !isGPR(TmpReg) || !isGPR(BaseReg))
        continue;

      bool UnsafePredTail = false;
      for (auto ScanI = std::next(LeaI); ScanI != Pred.end(); ++ScanI) {
        if (ScanI->isDebugInstr())
          continue;
        if (ScanI->isCall() || usesReg(*ScanI, TmpReg) ||
            definesReg(*ScanI, TmpReg) || definesReg(*ScanI, BaseReg)) {
          UnsafePredTail = true;
          break;
        }
      }
      if (UnsafePredTail)
        continue;

      for (MachineBasicBlock *Body : Pred.successors()) {
        if (Body->pred_size() != 1 || !Body->isLiveIn(TmpReg.asMCReg()))
          continue;

        bool OtherSuccNeedsTmp = false;
        for (MachineBasicBlock *Succ : Pred.successors()) {
          if (Succ != Body && Succ->isLiveIn(TmpReg.asMCReg())) {
            OtherSuccNeedsTmp = true;
            break;
          }
        }
        if (OtherSuccNeedsTmp)
          continue;

        auto LoadI = Body->begin();
        while (LoadI != Body->end() && LoadI->isDebugInstr())
          ++LoadI;
        if (LoadI == Body->end() || hasKnownOrderedMemoryRef(*LoadI) ||
            LoadI->getNumExplicitOperands() < 2 ||
            !LoadI->getOperand(0).isReg() || !LoadI->getOperand(1).isReg() ||
            LoadI->getOperand(0).getReg() != BaseReg ||
            LoadI->getOperand(1).getReg() != BaseReg)
          continue;

        auto CmpI = std::next(LoadI);
        while (CmpI != Body->end() && CmpI->isDebugInstr())
          ++CmpI;
        if (CmpI == Body->end() || CmpI->getNumExplicitOperands() < 2 ||
            !CmpI->getOperand(0).isReg() || !CmpI->getOperand(1).isReg() ||
            CmpI->getOperand(1).getReg() != BaseReg ||
            !CmpI->getOperand(1).isKill())
          continue;

        Register CmpReg = CmpI->getOperand(0).getReg();
        unsigned MemCmpOpcode =
            getDeferredLeaCopyMemCmpOpcode(LoadI->getOpcode(),
                                           CmpI->getOpcode());
        if (!MemCmpOpcode || CmpReg == BaseReg || CmpReg == TmpReg ||
            !CmpReg.isPhysical() || !isGPR(CmpReg))
          continue;

        auto CopyI = std::next(CmpI);
        while (CopyI != Body->end() && CopyI->isDebugInstr())
          ++CopyI;
        if (CopyI == Body->end() || CopyI->getOpcode() != Bedrock::MOVQrr ||
            CopyI->getNumExplicitOperands() < 2 ||
            !CopyI->getOperand(0).isReg() || !CopyI->getOperand(1).isReg() ||
            CopyI->getOperand(0).getReg() != BaseReg ||
            CopyI->getOperand(1).getReg() != TmpReg ||
            hasUseOrLiveOutOfRegBeforeDef(std::next(CopyI), *Body, TmpReg))
          continue;

        DebugLoc CmpDL = CmpI->getDebugLoc();
        bool CmpRegKilled = CmpI->getOperand(0).isKill();
        int64_t Offset = LeaI->getOperand(2).getImm();

        MachineInstrBuilder CmpMIB =
            BuildMI(*Body, LoadI, CmpDL, TII.get(MemCmpOpcode))
                .setMIFlags(CmpI->getFlags());
        CmpMIB.addReg(CmpReg, getKillRegState(CmpRegKilled)).addReg(BaseReg);
        CmpMIB.cloneMemRefs(*LoadI);

        MachineInstrBuilder LeaMIB =
            BuildMI(*Body, CopyI, CopyI->getDebugLoc(), TII.get(Bedrock::LEAro),
                    BaseReg)
                .addReg(BaseReg, RegState::Kill)
                .addImm(Offset)
                .setMIFlags(LeaI->getFlags());

        LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding deferred "
                             "LEA copy in ";
                   MF.getFunction().printAsOperand(dbgs(), false);
                   dbgs() << "\n";
                   LeaI->dump(); LoadI->dump(); CmpI->dump(); CopyI->dump();
                   CmpMIB.getInstr()->dump(); LeaMIB.getInstr()->dump());

        CopyI->eraseFromParent();
        CmpI->eraseFromParent();
        LoadI->eraseFromParent();
        LeaI->eraseFromParent();
        Body->removeLiveIn(TmpReg.asMCReg());

        ++NumDeferredLeaCopiesFolded;
        return true;
      }
    }
  }

  return false;
}

bool BedrockPreEmitPeephole::foldMulByThree(
    MachineBasicBlock::iterator &I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &MulMI = *I;
  if (MulMI.getOpcode() != Bedrock::MULL3rr ||
      MulMI.getNumExplicitOperands() < 3 || !MulMI.getOperand(0).isReg() ||
      !MulMI.getOperand(1).isReg() || !MulMI.getOperand(2).isReg())
    return false;

  Register DstReg = MulMI.getOperand(0).getReg();
  Register LHSReg = MulMI.getOperand(1).getReg();
  Register RHSReg = MulMI.getOperand(2).getReg();

  Register ConstReg;
  Register ValueReg;
  if (LHSReg == DstReg) {
    ConstReg = RHSReg;
    ValueReg = LHSReg;
  } else if (RHSReg == DstReg) {
    ConstReg = LHSReg;
    ValueReg = RHSReg;
  } else {
    return false;
  }
  if (!ConstReg.isPhysical() || !ValueReg.isPhysical() || ConstReg == ValueReg)
    return false;

  MachineInstr *ConstDef = findConstThreeDefBeforeMul(I, MBB, ConstReg);
  if (!ConstDef)
    return false;

  auto CopyI = findCopyIntoRegBeforeMul(I, MBB, DstReg, ConstDef);
  if (CopyI == MBB.end())
    return false;
  Register OriginalReg = CopyI->getOperand(1).getReg();
  if (OriginalReg == DstReg || OriginalReg == ConstReg)
    return false;
  if (!flagsAreDeadAfter(I, MBB))
    return false;

  CopyI->getOperand(1).setIsKill(false);
  DebugLoc DL = MulMI.getDebugLoc();
  auto NextI = std::next(I);
  BuildMI(MBB, I, DL, TII.get(Bedrock::ADDL3rr), DstReg)
      .addReg(DstReg)
      .addReg(OriginalReg);
  BuildMI(MBB, I, DL, TII.get(Bedrock::ADDL3rr), DstReg)
      .addReg(DstReg)
      .addReg(OriginalReg);

  ConstDef->eraseFromParent();
  MulMI.eraseFromParent();
  I = NextI;
  ++NumMulByThreeFolded;
  return true;
}

static bool getSameGlobalOffsetDelta(const MachineOperand &BaseMO,
                                     const MachineOperand &OffsetMO,
                                     int64_t &Delta) {
  if (!BaseMO.isGlobal() || !OffsetMO.isGlobal() ||
      BaseMO.getGlobal() != OffsetMO.getGlobal())
    return false;

  Delta = OffsetMO.getOffset() - BaseMO.getOffset();
  return true;
}

static bool isRegBaseLoadOpcode(unsigned Opcode) {
  switch (Opcode) {
  case Bedrock::LOADB_Zrr:
  case Bedrock::LOADW_Zrr:
  case Bedrock::LOADL_Zrr:
  case Bedrock::LOADB_Srr:
  case Bedrock::LOADW_Srr:
  case Bedrock::LOADL_Srr:
  case Bedrock::LOADLrr:
  case Bedrock::LOADQrr:
    return true;
  default:
    return false;
  }
}

bool BedrockPreEmitPeephole::foldSymbolOffsetLea(
    MachineBasicBlock::iterator I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &BaseMI = *I;
  if (BaseMI.getOpcode() != Bedrock::CONST64 ||
      BaseMI.getNumExplicitOperands() < 2 || !BaseMI.getOperand(0).isReg())
    return false;

  auto OffsetI = std::next(I);
  while (OffsetI != MBB.end() && OffsetI->isDebugInstr())
    ++OffsetI;
  if (OffsetI == MBB.end() || OffsetI->getOpcode() != Bedrock::CONST64 ||
      OffsetI->getNumExplicitOperands() < 2 || !OffsetI->getOperand(0).isReg())
    return false;

  int64_t Delta = 0;
  if (!getSameGlobalOffsetDelta(BaseMI.getOperand(1), OffsetI->getOperand(1),
                                Delta) ||
      Delta == 0 || getSignedAutoImmSize(Delta) >= 4)
    return false;

  Register BaseReg = BaseMI.getOperand(0).getReg();
  OffsetI->setDesc(TII.get(Bedrock::LEAro));
  OffsetI->removeOperand(1);
  OffsetI->addOperand(MachineOperand::CreateReg(BaseReg, /*isDef=*/false));
  OffsetI->addOperand(MachineOperand::CreateImm(Delta));
  BaseMI.getOperand(0).setIsDead(false);
  ++NumSymbolOffsetLeasFolded;
  return true;
}

static bool mayFoldAsScaledIndexMemory(MachineBasicBlock::iterator BaseI,
                                       MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator AddI,
                                       Register DstReg, Register IndexReg) {
  auto ShiftI = prevNonDebugInBlock(BaseI, MBB);
  if (ShiftI == MBB.end() || ShiftI->getOpcode() != Bedrock::SHLQ3ri ||
      ShiftI->getNumExplicitOperands() != 3 || !ShiftI->getOperand(0).isReg() ||
      !ShiftI->getOperand(1).isReg() || !ShiftI->getOperand(2).isImm() ||
      ShiftI->getOperand(0).getReg() != IndexReg ||
      ShiftI->getOperand(1).getReg() != IndexReg)
    return false;

  int64_t ShiftAmount = ShiftI->getOperand(2).getImm();
  if (ShiftAmount < 0 || ShiftAmount > 3)
    return false;

  auto LoadI = std::next(AddI);
  while (LoadI != MBB.end() && LoadI->isDebugInstr())
    ++LoadI;
  return LoadI != MBB.end() && isRegBaseLoadOpcode(LoadI->getOpcode()) &&
         LoadI->getNumExplicitOperands() >= 2 && LoadI->getOperand(1).isReg() &&
         LoadI->getOperand(1).getReg() == DstReg &&
         LoadI->getOperand(1).isKill();
}

bool BedrockPreEmitPeephole::foldSymbolIndexLea(
    MachineBasicBlock::iterator &I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &BaseMI = *I;
  if (BaseMI.getOpcode() != Bedrock::CONST64 ||
      BaseMI.getNumExplicitOperands() < 2 || !BaseMI.getOperand(0).isReg() ||
      !isSymbolicConstOperand(BaseMI.getOperand(1)))
    return false;

  Register SymbolReg = BaseMI.getOperand(0).getReg();
  if (!SymbolReg.isPhysical() || !isGPR(SymbolReg))
    return false;

  auto AddI = std::next(I);
  while (AddI != MBB.end() && AddI->isDebugInstr())
    ++AddI;
  if (AddI == MBB.end() || AddI->getOpcode() != Bedrock::ADDQ3rr ||
      AddI->getNumExplicitOperands() != 3 || !AddI->getOperand(0).isReg() ||
      !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg())
    return false;

  unsigned IndexOpIdx;
  if (AddI->getOperand(1).getReg() == SymbolReg &&
      AddI->getOperand(1).isKill()) {
    IndexOpIdx = 2;
  } else if (AddI->getOperand(2).getReg() == SymbolReg &&
             AddI->getOperand(2).isKill()) {
    IndexOpIdx = 1;
  } else {
    return false;
  }

  Register DstReg = AddI->getOperand(0).getReg();
  Register IndexReg = AddI->getOperand(IndexOpIdx).getReg();
  if (!DstReg.isPhysical() || !IndexReg.isPhysical() || !isGPR(DstReg) ||
      !isGPR(IndexReg) || IndexReg == SymbolReg)
    return false;
  if (mayFoldAsScaledIndexMemory(I, MBB, AddI, DstReg, IndexReg))
    return false;

  bool IndexRegKilled = AddI->getOperand(IndexOpIdx).isKill();
  auto NewI =
      BuildMI(MBB, AddI, AddI->getDebugLoc(), TII.get(Bedrock::LEAro), DstReg)
          .addReg(IndexReg, getKillRegState(IndexRegKilled))
          .add(BaseMI.getOperand(1))
          .setMIFlags(AddI->getFlags())
          .getInstr()
          ->getIterator();

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding symbol-index "
                       "address materialization: ";
             BaseMI.dump(); AddI->dump(); NewI->dump());

  AddI->eraseFromParent();
  BaseMI.eraseFromParent();
  I = NewI;
  ++NumSymbolIndexLeasFolded;
  return true;
}

struct GlobalBaseAbsAccessInfo {
  unsigned NewOpcode;
  unsigned AddrOpIdx;
};

static bool getGlobalBaseAbsAccessInfo(unsigned Opcode,
                                       GlobalBaseAbsAccessInfo &Info) {
  Info.AddrOpIdx = 1;
  switch (Opcode) {
  case Bedrock::LOADB_Zabs:
    Info.NewOpcode = Bedrock::LOADB_Zro;
    return true;
  case Bedrock::LOADW_Zabs:
    Info.NewOpcode = Bedrock::LOADW_Zro;
    return true;
  case Bedrock::LOADL_Zabs:
    Info.NewOpcode = Bedrock::LOADL_Zro;
    return true;
  case Bedrock::LOADB_Sabs:
    Info.NewOpcode = Bedrock::LOADB_Sro;
    return true;
  case Bedrock::LOADW_Sabs:
    Info.NewOpcode = Bedrock::LOADW_Sro;
    return true;
  case Bedrock::LOADL_Sabs:
    Info.NewOpcode = Bedrock::LOADL_Sro;
    return true;
  case Bedrock::LOADLabs:
    Info.NewOpcode = Bedrock::LOADLro;
    return true;
  case Bedrock::LOADQabs:
    Info.NewOpcode = Bedrock::LOADQro;
    return true;
  case Bedrock::STOREBabs:
    Info.NewOpcode = Bedrock::STOREBro;
    return true;
  case Bedrock::STOREWabs:
    Info.NewOpcode = Bedrock::STOREWro;
    return true;
  case Bedrock::STORELabs:
    Info.NewOpcode = Bedrock::STORELro;
    return true;
  case Bedrock::STOREQabs:
    Info.NewOpcode = Bedrock::STOREQro;
    return true;
  case Bedrock::STOREB_Immabs:
    Info.NewOpcode = Bedrock::STOREB_Immro;
    return true;
  case Bedrock::STOREW_Immabs:
    Info.NewOpcode = Bedrock::STOREW_Immro;
    return true;
  case Bedrock::STOREL_Immabs:
    Info.NewOpcode = Bedrock::STOREL_Immro;
    return true;
  case Bedrock::STOREQ_Immabs:
    Info.NewOpcode = Bedrock::STOREQ_Immro;
    return true;
  case Bedrock::INCLabs:
    Info = {Bedrock::INCLmo, 0};
    return true;
  case Bedrock::INCQabs:
    Info = {Bedrock::INCQmo, 0};
    return true;
  case Bedrock::DECLabs:
    Info = {Bedrock::DECLmo, 0};
    return true;
  case Bedrock::DECQabs:
    Info = {Bedrock::DECQmo, 0};
    return true;
  default:
    return false;
  }
}

static bool instrExplicitlyTouchesReg(const MachineInstr &MI, Register Reg) {
  return any_of(MI.operands(), [Reg](const MachineOperand &MO) {
    return MO.isReg() && MO.getReg() == Reg;
  });
}

static bool instrExplicitlyDefinesReg(const MachineInstr &MI, Register Reg) {
  return any_of(MI.operands(), [Reg](const MachineOperand &MO) {
    return MO.isReg() && MO.isDef() && MO.getReg() == Reg;
  });
}

static bool hasPushPairBetween(MachineBasicBlock::iterator Begin,
                               MachineBasicBlock::iterator End) {
  for (auto I = Begin; I != End; ++I) {
    if (!I->isDebugInstr() && I->getOpcode() == Bedrock::PUSHPi)
      return true;
  }
  return false;
}

bool BedrockPreEmitPeephole::foldGlobalBaseAbsAccesses(
    MachineFunction &MF, const TargetInstrInfo &TII) {
  for (MachineBasicBlock &MBB : MF) {
    for (auto BaseI = MBB.begin(), E = MBB.end(); BaseI != E; ++BaseI) {
      if (BaseI->isDebugInstr() || BaseI->getOpcode() != Bedrock::CONST64 ||
          BaseI->getNumExplicitOperands() < 2 || !BaseI->getOperand(0).isReg())
        continue;

      Register BaseReg = BaseI->getOperand(0).getReg();
      if (!BaseReg.isPhysical() || !Bedrock::GPR64RegClass.contains(BaseReg) ||
          !isLoopAccumulatorReg(BaseReg) || !BaseI->getOperand(1).isGlobal())
        continue;

      bool TouchedBeforeBase = false;
      for (auto I = MBB.begin(); I != BaseI; ++I) {
        if (!I->isDebugInstr() && instrExplicitlyTouchesReg(*I, BaseReg)) {
          TouchedBeforeBase = true;
          break;
        }
      }
      if (TouchedBeforeBase)
        continue;

      SmallVector<MachineInstr *, 16> Rewrites;
      SmallVector<int64_t, 16> Deltas;
      MachineInstr *FirstRewrite = nullptr;
      bool FirstRewriteBeforeBase = false;
      bool SawBase = false;
      for (MachineInstr &MI : MBB) {
        if (MI.isDebugInstr())
          continue;
        if (&MI == &*BaseI) {
          SawBase = true;
          continue;
        }
        if (SawBase && instrExplicitlyDefinesReg(MI, BaseReg))
          break;

        GlobalBaseAbsAccessInfo Info;
        if (!getGlobalBaseAbsAccessInfo(MI.getOpcode(), Info) ||
            MI.getNumExplicitOperands() <= Info.AddrOpIdx ||
            !MI.getOperand(Info.AddrOpIdx).isGlobal())
          continue;

        int64_t Delta = 0;
        if (!getSameGlobalOffsetDelta(BaseI->getOperand(1),
                                      MI.getOperand(Info.AddrOpIdx), Delta) ||
            getSignedAutoImmSize(Delta) >= 4)
          continue;

        if (!FirstRewrite) {
          FirstRewrite = &MI;
          FirstRewriteBeforeBase = !SawBase;
        }
        Rewrites.push_back(&MI);
        Deltas.push_back(Delta);
      }

      if (Rewrites.empty())
        continue;

      MachineBasicBlock::iterator FirstRewriteI = FirstRewrite->getIterator();
      if (FirstRewriteBeforeBase && MBB.isLiveIn(BaseReg.asMCReg()) &&
          hasPushPairBetween(FirstRewriteI, BaseI))
        continue;

      if (FirstRewriteBeforeBase)
        MBB.splice(FirstRewriteI, &MBB, BaseI);

      BaseI->getOperand(0).setIsDead(false);
      bool BaseIsLive = false;
      for (MachineInstr &MI : MBB) {
        if (&MI == &*BaseI) {
          BaseIsLive = true;
          continue;
        }
        if (!BaseIsLive)
          continue;
        if (instrExplicitlyDefinesReg(MI, BaseReg))
          break;
        for (MachineOperand &MO : MI.operands()) {
          if (MO.isReg() && MO.getReg() == BaseReg && MO.isKill())
            MO.setIsKill(false);
        }
      }

      for (unsigned Idx = 0, End = Rewrites.size(); Idx != End; ++Idx) {
        MachineInstr *MI = Rewrites[Idx];
        int64_t Delta = Deltas[Idx];
        GlobalBaseAbsAccessInfo Info;
        if (!getGlobalBaseAbsAccessInfo(MI->getOpcode(), Info))
          continue;
        MI->setDesc(TII.get(Info.NewOpcode));
        MI->removeOperand(Info.AddrOpIdx);
        MI->addOperand(MachineOperand::CreateReg(BaseReg, /*isDef=*/false));
        MI->addOperand(MachineOperand::CreateImm(Delta));
        ++NumGlobalBaseAbsAccessesFolded;
      }

      return true;
    }
  }

  return false;
}

static bool successorHasLiveIn(const MachineBasicBlock &MBB, Register Reg) {
  for (const MachineBasicBlock *Succ : MBB.successors()) {
    if (Succ->isLiveIn(Reg.asMCReg()))
      return true;
  }
  return false;
}

static Register
getUnusedCallerScratchReg(const MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator Begin,
                          MachineBasicBlock::iterator End) {
  static const unsigned CandidateRegs[] = {
      Bedrock::R6, Bedrock::R7, Bedrock::R5, Bedrock::R4,
      Bedrock::R3, Bedrock::R2, Bedrock::R1, Bedrock::R0};

  for (unsigned RegNo : CandidateRegs) {
    Register Reg = RegNo;
    if (MBB.isLiveIn(Reg.asMCReg()) || successorHasLiveIn(MBB, Reg))
      continue;

    bool Touched = false;
    for (auto I = Begin; I != End; ++I) {
      if (!I->isDebugInstr() && instrExplicitlyTouchesReg(*I, Reg)) {
        Touched = true;
        break;
      }
    }
    if (!Touched)
      return Reg;
  }

  return Register();
}

struct IntroducedGlobalBaseCandidate {
  const GlobalValue *GV = nullptr;
  int64_t BaseOffset = 0;
  unsigned TargetFlags = 0;
  SmallVector<MachineInstr *, 8> Rewrites;
  SmallVector<int64_t, 8> Deltas;
};

static int estimateIntroducedGlobalBaseBenefit(
    const IntroducedGlobalBaseCandidate &Candidate) {
  int Benefit = -7;
  for (int64_t Delta : Candidate.Deltas) {
    unsigned OffsetSize = Delta == 0 ? 0 : getSignedAutoImmSize(Delta);
    unsigned BaseOffsetAccessSize = 3 + OffsetSize;
    Benefit += 7 - BaseOffsetAccessSize;
  }
  return Benefit;
}

bool BedrockPreEmitPeephole::foldIntroducedGlobalBaseAbsAccesses(
    MachineFunction &MF, const TargetInstrInfo &TII) {
  for (MachineBasicBlock &MBB : MF) {
    for (auto SegBegin = MBB.begin(), E = MBB.end(); SegBegin != E;) {
      while (SegBegin != E && SegBegin->isDebugInstr())
        ++SegBegin;
      if (SegBegin == E)
        break;

      auto SegEnd = SegBegin;
      while (SegEnd != E) {
        if (!SegEnd->isDebugInstr() &&
            (SegEnd->isCall() || SegEnd->isTerminator()))
          break;
        ++SegEnd;
      }

      Register ScratchReg = getUnusedCallerScratchReg(MBB, SegBegin, SegEnd);
      if (ScratchReg) {
        SmallVector<IntroducedGlobalBaseCandidate, 4> Candidates;
        for (auto I = SegBegin; I != SegEnd; ++I) {
          if (I->isDebugInstr())
            continue;

          GlobalBaseAbsAccessInfo Info;
          if (!getGlobalBaseAbsAccessInfo(I->getOpcode(), Info) ||
              I->getNumExplicitOperands() <= Info.AddrOpIdx ||
              !I->getOperand(Info.AddrOpIdx).isGlobal())
            continue;

          MachineOperand &AddrMO = I->getOperand(Info.AddrOpIdx);
          IntroducedGlobalBaseCandidate *Candidate = nullptr;
          for (IntroducedGlobalBaseCandidate &Existing : Candidates) {
            if (Existing.GV == AddrMO.getGlobal() &&
                Existing.TargetFlags == AddrMO.getTargetFlags()) {
              Candidate = &Existing;
              break;
            }
          }
          if (!Candidate) {
            Candidates.emplace_back();
            Candidate = &Candidates.back();
            Candidate->GV = AddrMO.getGlobal();
            Candidate->BaseOffset = AddrMO.getOffset();
            Candidate->TargetFlags = AddrMO.getTargetFlags();
          }

          int64_t Delta = AddrMO.getOffset() - Candidate->BaseOffset;
          if (getSignedAutoImmSize(Delta) >= 4)
            continue;

          Candidate->Rewrites.push_back(&*I);
          Candidate->Deltas.push_back(Delta);
        }

        IntroducedGlobalBaseCandidate *Best = nullptr;
        int BestBenefit = 0;
        for (IntroducedGlobalBaseCandidate &Candidate : Candidates) {
          int Benefit = estimateIntroducedGlobalBaseBenefit(Candidate);
          if (Benefit <= 0)
            continue;
          if (!Best || Benefit > BestBenefit) {
            Best = &Candidate;
            BestBenefit = Benefit;
          }
        }
        if (Best) {
          BuildMI(MBB, Best->Rewrites.front()->getIterator(),
                  Best->Rewrites.front()->getDebugLoc(),
                  TII.get(Bedrock::CONST64), ScratchReg)
              .addGlobalAddress(Best->GV, Best->BaseOffset, Best->TargetFlags);

          for (unsigned Idx = 0, End = Best->Rewrites.size(); Idx != End;
               ++Idx) {
            MachineInstr *MI = Best->Rewrites[Idx];
            int64_t Delta = Best->Deltas[Idx];
            GlobalBaseAbsAccessInfo Info;
            if (!getGlobalBaseAbsAccessInfo(MI->getOpcode(), Info))
              continue;
            MI->setDesc(TII.get(Info.NewOpcode));
            MI->removeOperand(Info.AddrOpIdx);
            MI->addOperand(
                MachineOperand::CreateReg(ScratchReg, /*isDef=*/false));
            MI->addOperand(MachineOperand::CreateImm(Delta));
            ++NumIntroducedGlobalBaseAbsAccessesFolded;
          }

          return true;
        }
      }

      if (SegEnd == E)
        break;
      SegBegin = std::next(SegEnd);
    }
  }

  return false;
}

static bool isSelfByteZeroExtend(const MachineInstr &MI, Register Reg) {
  if (MI.getOpcode() != Bedrock::EXTZLBrr &&
      MI.getOpcode() != Bedrock::EXTZQBrr)
    return false;
  return MI.getNumExplicitOperands() >= 2 && MI.getOperand(0).isReg() &&
         MI.getOperand(1).isReg() && MI.getOperand(0).getReg() == Reg &&
         MI.getOperand(1).getReg() == Reg;
}

static bool instrDefinesReg(const MachineInstr &MI, Register Reg) {
  return any_of(MI.operands(), [Reg](const MachineOperand &MO) {
    return MO.isReg() && MO.isDef() && MO.getReg() == Reg;
  });
}

static bool hasDominatingByteZeroExtend(
    const MachineBasicBlock &MBB, MachineBasicBlock::const_iterator Pos,
    Register Reg, SmallPtrSetImpl<const MachineBasicBlock *> &Visiting) {
  for (auto I = Pos; I != MBB.begin();) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (instrDefinesReg(*I, Reg))
      return isSelfByteZeroExtend(*I, Reg);
  }

  if (!Visiting.insert(&MBB).second)
    return false;
  if (MBB.pred_empty()) {
    Visiting.erase(&MBB);
    return false;
  }

  for (const MachineBasicBlock *PredMBB : MBB.predecessors()) {
    if (!hasDominatingByteZeroExtend(*PredMBB, PredMBB->end(), Reg,
                                     Visiting)) {
      Visiting.erase(&MBB);
      return false;
    }
  }

  Visiting.erase(&MBB);
  return true;
}

static bool hasDominatingByteZeroExtend(const MachineBasicBlock &MBB,
                                        MachineBasicBlock::const_iterator Pos,
                                        Register Reg) {
  SmallPtrSet<const MachineBasicBlock *, 8> Visiting;
  return hasDominatingByteZeroExtend(MBB, Pos, Reg, Visiting);
}

static bool getByteRotateLeftAmount(const MachineInstr &MI, Register Reg,
                                    unsigned &Amount) {
  if (MI.getOpcode() == Bedrock::SHLL3ri &&
      MI.getNumExplicitOperands() >= 3 && MI.getOperand(0).isReg() &&
      MI.getOperand(1).isReg() && MI.getOperand(2).isImm() &&
      MI.getOperand(0).getReg() == Reg && MI.getOperand(1).getReg() == Reg) {
    int64_t Imm = MI.getOperand(2).getImm();
    if (Imm < 1 || Imm > 7)
      return false;
    Amount = static_cast<unsigned>(Imm);
    return true;
  }

  if (MI.getOpcode() == Bedrock::ADDL3rr &&
      MI.getNumExplicitOperands() >= 3 && MI.getOperand(0).isReg() &&
      MI.getOperand(1).isReg() && MI.getOperand(2).isReg() &&
      MI.getOperand(0).getReg() == Reg && MI.getOperand(1).getReg() == Reg &&
      MI.getOperand(2).getReg() == Reg) {
    Amount = 1;
    return true;
  }

  return false;
}

static MachineBasicBlock::const_iterator
firstNonDebugInstr(const MachineBasicBlock &MBB) {
  auto I = MBB.begin();
  while (I != MBB.end() && I->isDebugInstr())
    ++I;
  return I;
}

static bool isByteRotateJoinOR(const MachineBasicBlock *JoinMBB,
                               Register ValueReg, Register TmpReg) {
  if (!JoinMBB)
    return false;

  auto JoinI = firstNonDebugInstr(*JoinMBB);
  if (JoinI == JoinMBB->end() || JoinI->getOpcode() != Bedrock::ORL3rr ||
      JoinI->getNumExplicitOperands() < 3 || !JoinI->getOperand(0).isReg() ||
      !JoinI->getOperand(1).isReg() || !JoinI->getOperand(2).isReg() ||
      JoinI->getOperand(0).getReg() != ValueReg)
    return false;

  Register LHS = JoinI->getOperand(1).getReg();
  Register RHS = JoinI->getOperand(2).getReg();
  return (LHS == ValueReg && RHS == TmpReg) ||
         (LHS == TmpReg && RHS == ValueReg);
}

bool BedrockPreEmitPeephole::foldByteRotateIdiom(
    MachineBasicBlock::iterator &I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &ShrMI = *I;
  if (ShrMI.getOpcode() != Bedrock::SHRL3ri ||
      ShrMI.getNumExplicitOperands() < 3 || !ShrMI.getOperand(0).isReg() ||
      !ShrMI.getOperand(1).isReg() || !ShrMI.getOperand(2).isImm())
    return false;

  Register TmpReg = ShrMI.getOperand(0).getReg();
  if (ShrMI.getOperand(1).getReg() != TmpReg)
    return false;

  auto ShlI = std::next(I);
  while (ShlI != MBB.end() && ShlI->isDebugInstr())
    ++ShlI;
  if (ShlI == MBB.end() || ShlI->getNumExplicitOperands() < 1 ||
      !ShlI->getOperand(0).isReg())
    return false;

  Register ValueReg = ShlI->getOperand(0).getReg();
  unsigned Rotate;
  if (!getByteRotateLeftAmount(*ShlI, ValueReg, Rotate))
    return false;

  if (ShrMI.getOperand(2).getImm() != static_cast<int64_t>(8 - Rotate))
    return false;

  auto AndI = std::next(ShlI);
  while (AndI != MBB.end() && AndI->isDebugInstr())
    ++AndI;
  if (AndI == MBB.end() || AndI->getOpcode() != Bedrock::ANDL3ri ||
      AndI->getNumExplicitOperands() < 3 || !AndI->getOperand(0).isReg() ||
      !AndI->getOperand(1).isReg() || !AndI->getOperand(2).isImm() ||
      AndI->getOperand(0).getReg() != ValueReg ||
      AndI->getOperand(1).getReg() != ValueReg)
    return false;

  uint32_t ExpectedMask = (0xffu << Rotate) & 0xffu;
  if (static_cast<uint32_t>(AndI->getOperand(2).getImm()) != ExpectedMask)
    return false;

  auto NextI = std::next(AndI);
  while (NextI != MBB.end() && NextI->isDebugInstr())
    ++NextI;

  const MachineBasicBlock *JoinMBB = nullptr;
  if (NextI != MBB.end()) {
    if (NextI->getOpcode() != Bedrock::BR ||
        !NextI->getOperand(0).isMBB())
      return false;
    JoinMBB = NextI->getOperand(0).getMBB();
  } else {
    JoinMBB = MBB.getNextNode();
  }

  if (!isByteRotateJoinOR(JoinMBB, ValueReg, TmpReg))
    return false;

  if (!hasDominatingByteZeroExtend(MBB, I, ValueReg))
    return false;

  DebugLoc DL = ShrMI.getDebugLoc();
  BuildMI(MBB, I, DL, TII.get(Bedrock::ROLB3ri), ValueReg)
      .addReg(ValueReg, RegState::Kill)
      .addImm(Rotate);
  BuildMI(MBB, I, DL, TII.get(Bedrock::CONST64), TmpReg).addImm(0);

  AndI->eraseFromParent();
  ShlI->eraseFromParent();
  I->eraseFromParent();
  I = NextI;

  ++NumByteRotatesFolded;
  return true;
}

static bool instrUsesReg(const MachineInstr &MI, Register Reg) {
  return any_of(MI.operands(), [Reg](const MachineOperand &MO) {
    return MO.isReg() && !MO.isDef() && MO.getReg() == Reg;
  });
}

static bool blockDefinesRegBeforeUse(const MachineBasicBlock &MBB,
                                     Register Reg) {
  for (const MachineInstr &MI : MBB) {
    if (MI.isDebugInstr())
      continue;
    if (instrUsesReg(MI, Reg))
      return false;
    if (instrDefinesReg(MI, Reg))
      return true;
    if (MI.isTerminator())
      return false;
  }
  return false;
}

static bool blockStartsWithByteRotateClearing(const MachineBasicBlock &MBB,
                                              Register Reg) {
  auto I = MBB.begin();
  while (I != MBB.end() && I->isDebugInstr())
    ++I;
  if (I == MBB.end() || I->getOpcode() != Bedrock::ROLB3ri)
    return false;

  ++I;
  while (I != MBB.end() && I->isDebugInstr())
    ++I;
  return I != MBB.end() && I->getOpcode() == Bedrock::CONST64 &&
         I->getNumExplicitOperands() >= 2 && I->getOperand(0).isReg() &&
         I->getOperand(1).isImm() && I->getOperand(0).getReg() == Reg &&
         I->getOperand(1).getImm() == 0;
}

bool BedrockPreEmitPeephole::foldDeadCopyBeforeBranch(
    MachineBasicBlock::iterator &I, MachineBasicBlock &MBB) {
  MachineInstr &CopyMI = *I;
  if (CopyMI.getOpcode() != Bedrock::MOVQrr ||
      CopyMI.getNumExplicitOperands() < 2 || !CopyMI.getOperand(0).isReg() ||
      !CopyMI.getOperand(1).isReg())
    return false;

  Register DstReg = CopyMI.getOperand(0).getReg();
  Register SrcReg = CopyMI.getOperand(1).getReg();
  if (DstReg == SrcReg)
    return false;

  auto ScanI = std::next(I);
  while (ScanI != MBB.end() && ScanI->isDebugInstr())
    ++ScanI;
  if (ScanI != MBB.end() && ScanI->getOpcode() != Bedrock::BR &&
      ScanI->getOpcode() != Bedrock::BRCC) {
    for (auto J = ScanI; J != MBB.end(); ++J) {
      if (J->isDebugInstr())
        continue;
      if (instrUsesReg(*J, DstReg))
        return false;
      if (instrDefinesReg(*J, DstReg)) {
        CopyMI.eraseFromParent();
        I = J;
        ++NumDeadCopiesBeforeBranchesFolded;
        return true;
      }
      if (J->getOpcode() == Bedrock::BR || J->getOpcode() == Bedrock::BRCC)
        return false;
    }
    return false;
  }

  auto BranchI = std::next(I);
  while (BranchI != MBB.end() && BranchI->isDebugInstr())
    ++BranchI;
  if (BranchI == MBB.end() || BranchI->getOpcode() != Bedrock::BRCC ||
      !BranchI->getOperand(0).isMBB())
    return false;

  const MachineBasicBlock *TargetMBB = BranchI->getOperand(0).getMBB();
  const MachineBasicBlock *FallthroughMBB = MBB.getNextNode();
  if (!FallthroughMBB)
    return false;

  if ((!blockDefinesRegBeforeUse(*TargetMBB, DstReg) &&
       !blockStartsWithByteRotateClearing(*TargetMBB, DstReg)) ||
      (!blockDefinesRegBeforeUse(*FallthroughMBB, DstReg) &&
       !blockStartsWithByteRotateClearing(*FallthroughMBB, DstReg)))
    return false;

  CopyMI.eraseFromParent();
  I = BranchI;
  ++NumDeadCopiesBeforeBranchesFolded;
  return true;
}

static unsigned getByteLoadOpcodeForShiftedStore(unsigned Opcode,
                                                 unsigned &Width) {
  switch (Opcode) {
  case Bedrock::LOADQro:
    Width = 8;
    return Bedrock::LOADB_Zro;
  case Bedrock::LOADLro:
  case Bedrock::LOADL_Zro:
    Width = 4;
    return Bedrock::LOADB_Zro;
  case Bedrock::LOADQfi:
    Width = 8;
    return Bedrock::LOADB_Zfi;
  case Bedrock::LOADLfi:
  case Bedrock::LOADL_Zfi:
    Width = 4;
    return Bedrock::LOADB_Zfi;
  default:
    return 0;
  }
}

static bool isByteStoreFromReg(const MachineInstr &MI, Register Reg) {
  switch (MI.getOpcode()) {
  case Bedrock::STOREBrr:
  case Bedrock::STOREBro:
  case Bedrock::STOREBabs:
  case Bedrock::STOREBfi:
    return MI.getNumExplicitOperands() >= 1 && MI.getOperand(0).isReg() &&
           MI.getOperand(0).getReg() == Reg && MI.getOperand(0).isKill();
  default:
    return false;
  }
}

static bool hasKnownOrderedMemoryRef(const MachineInstr &MI) {
  return !MI.memoperands_empty() && MI.hasOrderedMemoryRef();
}

bool BedrockPreEmitPeephole::foldShiftedByteStore(
    MachineBasicBlock::iterator I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &LoadMI = *I;
  unsigned LoadWidth = 0;
  unsigned ByteLoadOpcode =
      getByteLoadOpcodeForShiftedStore(LoadMI.getOpcode(), LoadWidth);
  if (!ByteLoadOpcode || hasKnownOrderedMemoryRef(LoadMI) ||
      LoadMI.getNumExplicitOperands() < 3 || !LoadMI.getOperand(0).isReg())
    return false;

  auto ShiftI = std::next(I);
  while (ShiftI != MBB.end() && ShiftI->isDebugInstr())
    ++ShiftI;
  if (ShiftI == MBB.end() ||
      (ShiftI->getOpcode() != Bedrock::SHRQ3ri &&
       ShiftI->getOpcode() != Bedrock::SHRL3ri) ||
      ShiftI->getNumExplicitOperands() != 3 || !ShiftI->getOperand(0).isReg() ||
      !ShiftI->getOperand(1).isReg() || !ShiftI->getOperand(2).isImm())
    return false;

  Register Reg = LoadMI.getOperand(0).getReg();
  if (ShiftI->getOperand(0).getReg() != Reg ||
      ShiftI->getOperand(1).getReg() != Reg || !ShiftI->getOperand(1).isKill())
    return false;

  int64_t Shift = ShiftI->getOperand(2).getImm();
  if (Shift <= 0 || Shift % 8 != 0 || Shift / 8 >= LoadWidth)
    return false;

  auto StoreI = std::next(ShiftI);
  while (StoreI != MBB.end() && StoreI->isDebugInstr())
    ++StoreI;
  if (StoreI == MBB.end() || hasKnownOrderedMemoryRef(*StoreI) ||
      !isByteStoreFromReg(*StoreI, Reg) || !flagsAreDeadAfter(ShiftI, MBB))
    return false;

  unsigned OffsetOp;
  switch (LoadMI.getOpcode()) {
  case Bedrock::LOADQro:
  case Bedrock::LOADLro:
  case Bedrock::LOADL_Zro:
  case Bedrock::LOADQfi:
  case Bedrock::LOADLfi:
  case Bedrock::LOADL_Zfi:
    OffsetOp = 2;
    break;
  default:
    return false;
  }
  if (!LoadMI.getOperand(OffsetOp).isImm())
    return false;

  LoadMI.setDesc(TII.get(ByteLoadOpcode));
  LoadMI.getOperand(OffsetOp).setImm(LoadMI.getOperand(OffsetOp).getImm() +
                                     Shift / 8);
  ShiftI->eraseFromParent();
  ++NumShiftedByteStoresFolded;
  return true;
}

enum class MemIncDecAddrKind {
  Reg,
  RegOffset,
  Frame,
  Abs,
};

struct RetainedLoadMemIncDecInfo {
  unsigned StoreOpcode;
  unsigned IncOpcode;
  unsigned DecOpcode;
  MemIncDecAddrKind AddrKind;
  bool Is64;
};

static bool getRetainedLoadMemIncDecInfo(
    unsigned LoadOpcode, RetainedLoadMemIncDecInfo &Info) {
  switch (LoadOpcode) {
  case Bedrock::LOADL_Zrr:
  case Bedrock::LOADL_Srr:
  case Bedrock::LOADLrr:
    Info = {Bedrock::STORELrr, Bedrock::INCLm, Bedrock::DECLm,
            MemIncDecAddrKind::Reg, false};
    return true;
  case Bedrock::LOADQrr:
    Info = {Bedrock::STOREQrr, Bedrock::INCQm, Bedrock::DECQm,
            MemIncDecAddrKind::Reg, true};
    return true;
  case Bedrock::LOADL_Zro:
  case Bedrock::LOADL_Sro:
  case Bedrock::LOADLro:
    Info = {Bedrock::STORELro, Bedrock::INCLmo, Bedrock::DECLmo,
            MemIncDecAddrKind::RegOffset, false};
    return true;
  case Bedrock::LOADQro:
    Info = {Bedrock::STOREQro, Bedrock::INCQmo, Bedrock::DECQmo,
            MemIncDecAddrKind::RegOffset, true};
    return true;
  case Bedrock::LOADL_Zfi:
  case Bedrock::LOADL_Sfi:
  case Bedrock::LOADLfi:
    Info = {Bedrock::STORELfi, Bedrock::INCLfi, Bedrock::DECLfi,
            MemIncDecAddrKind::Frame, false};
    return true;
  case Bedrock::LOADQfi:
    Info = {Bedrock::STOREQfi, Bedrock::INCQfi, Bedrock::DECQfi,
            MemIncDecAddrKind::Frame, true};
    return true;
  case Bedrock::LOADL_Zabs:
  case Bedrock::LOADL_Sabs:
  case Bedrock::LOADLabs:
    Info = {Bedrock::STORELabs, Bedrock::INCLabs, Bedrock::DECLabs,
            MemIncDecAddrKind::Abs, false};
    return true;
  case Bedrock::LOADQabs:
    Info = {Bedrock::STOREQabs, Bedrock::INCQabs, Bedrock::DECQabs,
            MemIncDecAddrKind::Abs, true};
    return true;
  default:
    return false;
  }
}

static unsigned getRetainedLoadMemIncDecOpcode(unsigned UnaryOpcode,
                                               const RetainedLoadMemIncDecInfo &Info) {
  switch (UnaryOpcode) {
  case Bedrock::INCL3r:
    return Info.Is64 ? 0 : Info.IncOpcode;
  case Bedrock::INCQ3r:
    return Info.Is64 ? Info.IncOpcode : 0;
  case Bedrock::DECL3r:
    return Info.Is64 ? 0 : Info.DecOpcode;
  case Bedrock::DECQ3r:
    return Info.Is64 ? Info.DecOpcode : 0;
  default:
    return 0;
  }
}

static bool retainedLoadMemIncDecAddressMatches(
    const MachineInstr &LoadMI, const MachineInstr &StoreMI,
    MemIncDecAddrKind AddrKind) {
  switch (AddrKind) {
  case MemIncDecAddrKind::Reg:
    return LoadMI.getOperand(1).isIdenticalTo(StoreMI.getOperand(1));
  case MemIncDecAddrKind::RegOffset:
  case MemIncDecAddrKind::Frame:
    return LoadMI.getOperand(1).isIdenticalTo(StoreMI.getOperand(1)) &&
           LoadMI.getOperand(2).isIdenticalTo(StoreMI.getOperand(2));
  case MemIncDecAddrKind::Abs:
    return LoadMI.getOperand(1).isIdenticalTo(StoreMI.getOperand(1));
  }
  llvm_unreachable("unknown Bedrock memory inc/dec address kind");
}

static bool retainedLoadMemIncDecAddressUsesReg(const MachineInstr &LoadMI,
                                                MemIncDecAddrKind AddrKind,
                                                Register Reg) {
  switch (AddrKind) {
  case MemIncDecAddrKind::Reg:
  case MemIncDecAddrKind::RegOffset:
    return LoadMI.getOperand(1).getReg() == Reg;
  case MemIncDecAddrKind::Frame:
  case MemIncDecAddrKind::Abs:
    return false;
  }
  llvm_unreachable("unknown Bedrock memory inc/dec address kind");
}

static void addRetainedLoadMemIncDecAddressOperands(
    MachineInstrBuilder &MIB, const MachineInstr &LoadMI,
    MemIncDecAddrKind AddrKind) {
  switch (AddrKind) {
  case MemIncDecAddrKind::Reg:
  case MemIncDecAddrKind::Abs:
    MIB.add(LoadMI.getOperand(1));
    return;
  case MemIncDecAddrKind::RegOffset:
  case MemIncDecAddrKind::Frame:
    MIB.add(LoadMI.getOperand(1)).add(LoadMI.getOperand(2));
    return;
  }
  llvm_unreachable("unknown Bedrock memory inc/dec address kind");
}

static bool hasUseOfRegBeforeDef(MachineBasicBlock::iterator I,
                                 MachineBasicBlock &MBB, Register Reg) {
  for (; I != MBB.end(); ++I) {
    if (I->isDebugInstr())
      continue;

    bool HasDef = false;
    for (const MachineOperand &MO : I->operands()) {
      if (!MO.isReg() || MO.getReg() != Reg)
        continue;
      if (MO.isUse())
        return true;
      if (MO.isDef())
        HasDef = true;
    }
    if (HasDef)
      return false;
  }
  return false;
}

static bool isLiveOutToSuccessor(const MachineBasicBlock &MBB, Register Reg);

static bool hasUseOrLiveOutOfRegBeforeDef(MachineBasicBlock::iterator I,
                                          MachineBasicBlock &MBB,
                                          Register Reg) {
  for (; I != MBB.end(); ++I) {
    if (I->isDebugInstr())
      continue;

    bool HasDef = false;
    for (const MachineOperand &MO : I->operands()) {
      if (!MO.isReg() || MO.getReg() != Reg)
        continue;
      if (MO.isUse())
        return true;
      if (MO.isDef())
        HasDef = true;
    }
    if (HasDef)
      return false;
  }
  return isLiveOutToSuccessor(MBB, Reg);
}

static bool isLiveOutToSuccessor(const MachineBasicBlock &MBB, Register Reg) {
  if (!Reg.isPhysical())
    return true;
  for (const MachineBasicBlock *Succ : MBB.successors()) {
    if (Succ->isLiveIn(Reg.asMCReg()))
      return true;
  }
  return false;
}

bool BedrockPreEmitPeephole::foldRetainedLoadMemIncDec(
    MachineBasicBlock::iterator I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &LoadMI = *I;
  RetainedLoadMemIncDecInfo Info;
  if (!getRetainedLoadMemIncDecInfo(LoadMI.getOpcode(), Info) ||
      hasKnownOrderedMemoryRef(LoadMI))
    return false;

  auto CopyI = std::next(I);
  while (CopyI != MBB.end() && CopyI->isDebugInstr())
    ++CopyI;
  if (CopyI == MBB.end() ||
      (CopyI->getOpcode() != Bedrock::MOVQrr &&
       CopyI->getOpcode() != Bedrock::MOVLrr) ||
      CopyI->getNumExplicitOperands() != 2 || !CopyI->getOperand(0).isReg() ||
      !CopyI->getOperand(1).isReg())
    return false;

  auto UnaryI = std::next(CopyI);
  while (UnaryI != MBB.end() && UnaryI->isDebugInstr())
    ++UnaryI;
  if (UnaryI == MBB.end() || UnaryI->getNumExplicitOperands() != 2 ||
      !UnaryI->getOperand(0).isReg() || !UnaryI->getOperand(1).isReg())
    return false;

  unsigned MemOpcode = getRetainedLoadMemIncDecOpcode(UnaryI->getOpcode(), Info);
  if (!MemOpcode)
    return false;

  auto StoreI = std::next(UnaryI);
  while (StoreI != MBB.end() && StoreI->isDebugInstr())
    ++StoreI;
  if (StoreI == MBB.end() || StoreI->getOpcode() != Info.StoreOpcode ||
      hasKnownOrderedMemoryRef(*StoreI) ||
      StoreI->getNumExplicitOperands() < 2 || !StoreI->getOperand(0).isReg())
    return false;

  Register LoadReg = LoadMI.getOperand(0).getReg();
  Register UpdateReg = CopyI->getOperand(0).getReg();
  if (UpdateReg == LoadReg || CopyI->getOperand(1).getReg() != LoadReg ||
      UnaryI->getOperand(0).getReg() != UpdateReg ||
      UnaryI->getOperand(1).getReg() != UpdateReg ||
      StoreI->getOperand(0).getReg() != UpdateReg ||
      !StoreI->getOperand(0).isKill() ||
      !retainedLoadMemIncDecAddressMatches(LoadMI, *StoreI, Info.AddrKind) ||
      retainedLoadMemIncDecAddressUsesReg(LoadMI, Info.AddrKind, LoadReg) ||
      retainedLoadMemIncDecAddressUsesReg(LoadMI, Info.AddrKind, UpdateReg) ||
      !hasUseOfRegBeforeDef(std::next(StoreI), MBB, LoadReg))
    return false;

  MachineInstrBuilder MIB =
      BuildMI(MBB, CopyI, UnaryI->getDebugLoc(), TII.get(MemOpcode))
          .setMIFlags(UnaryI->getFlags());
  addRetainedLoadMemIncDecAddressOperands(MIB, LoadMI, Info.AddrKind);
  MIB.cloneMemRefs(LoadMI);
  MIB.cloneMemRefs(*StoreI);

  StoreI->eraseFromParent();
  UnaryI->eraseFromParent();
  CopyI->eraseFromParent();
  ++NumRetainedLoadMemIncDecsFolded;
  return true;
}

bool BedrockPreEmitPeephole::foldReloadedMemIncDec(
    MachineBasicBlock::iterator &I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &LoadMI = *I;
  RetainedLoadMemIncDecInfo Info;
  if (!getRetainedLoadMemIncDecInfo(LoadMI.getOpcode(), Info) ||
      hasKnownOrderedMemoryRef(LoadMI))
    return false;

  auto UnaryI = std::next(I);
  while (UnaryI != MBB.end() && UnaryI->isDebugInstr())
    ++UnaryI;
  if (UnaryI == MBB.end() || UnaryI->getNumExplicitOperands() != 2 ||
      !UnaryI->getOperand(0).isReg() || !UnaryI->getOperand(1).isReg())
    return false;

  unsigned MemOpcode = getRetainedLoadMemIncDecOpcode(UnaryI->getOpcode(), Info);
  if (!MemOpcode)
    return false;

  auto StoreI = std::next(UnaryI);
  while (StoreI != MBB.end() && StoreI->isDebugInstr())
    ++StoreI;
  if (StoreI == MBB.end() || StoreI->getOpcode() != Info.StoreOpcode ||
      hasKnownOrderedMemoryRef(*StoreI) ||
      StoreI->getNumExplicitOperands() < 2 || !StoreI->getOperand(0).isReg())
    return false;

  Register Reg = LoadMI.getOperand(0).getReg();
  bool NeedsReload = hasUseOrLiveOutOfRegBeforeDef(std::next(StoreI), MBB, Reg);
  if (UnaryI->getOperand(0).getReg() != Reg ||
      UnaryI->getOperand(1).getReg() != Reg ||
      StoreI->getOperand(0).getReg() != Reg ||
      !retainedLoadMemIncDecAddressMatches(LoadMI, *StoreI, Info.AddrKind) ||
      retainedLoadMemIncDecAddressUsesReg(LoadMI, Info.AddrKind, Reg) ||
      !NeedsReload)
    return false;

  auto NextI = std::next(StoreI);
  MachineInstrBuilder MemMIB =
      BuildMI(MBB, LoadMI, UnaryI->getDebugLoc(), TII.get(MemOpcode))
          .setMIFlags(UnaryI->getFlags());
  addRetainedLoadMemIncDecAddressOperands(MemMIB, LoadMI, Info.AddrKind);
  MemMIB.cloneMemRefs(LoadMI);
  MemMIB.cloneMemRefs(*StoreI);

  MachineInstrBuilder ReloadMIB =
      BuildMI(MBB, NextI, LoadMI.getDebugLoc(), TII.get(LoadMI.getOpcode()), Reg);
  addRetainedLoadMemIncDecAddressOperands(ReloadMIB, LoadMI, Info.AddrKind);
  ReloadMIB.cloneMemRefs(LoadMI);

  I = MemMIB.getInstr()->getIterator();
  StoreI->eraseFromParent();
  UnaryI->eraseFromParent();
  LoadMI.eraseFromParent();
  ++NumReloadedMemIncDecsFolded;
  return true;
}

bool BedrockPreEmitPeephole::foldDeadMemIncDec(
    MachineBasicBlock::iterator &I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &LoadMI = *I;
  RetainedLoadMemIncDecInfo Info;
  if (!getRetainedLoadMemIncDecInfo(LoadMI.getOpcode(), Info) ||
      hasKnownOrderedMemoryRef(LoadMI))
    return false;

  auto UnaryI = std::next(I);
  while (UnaryI != MBB.end() && UnaryI->isDebugInstr())
    ++UnaryI;
  if (UnaryI == MBB.end() || UnaryI->getNumExplicitOperands() != 2 ||
      !UnaryI->getOperand(0).isReg() || !UnaryI->getOperand(1).isReg())
    return false;

  unsigned MemOpcode = getRetainedLoadMemIncDecOpcode(UnaryI->getOpcode(), Info);
  if (!MemOpcode)
    return false;

  auto StoreI = std::next(UnaryI);
  while (StoreI != MBB.end() && StoreI->isDebugInstr())
    ++StoreI;
  if (StoreI == MBB.end() || StoreI->getOpcode() != Info.StoreOpcode ||
      hasKnownOrderedMemoryRef(*StoreI) ||
      StoreI->getNumExplicitOperands() < 2 || !StoreI->getOperand(0).isReg())
    return false;

  Register Reg = LoadMI.getOperand(0).getReg();
  if (UnaryI->getOperand(0).getReg() != Reg ||
      UnaryI->getOperand(1).getReg() != Reg ||
      StoreI->getOperand(0).getReg() != Reg ||
      !retainedLoadMemIncDecAddressMatches(LoadMI, *StoreI, Info.AddrKind) ||
      retainedLoadMemIncDecAddressUsesReg(LoadMI, Info.AddrKind, Reg) ||
      hasUseOfRegBeforeDef(std::next(StoreI), MBB, Reg) ||
      isLiveOutToSuccessor(MBB, Reg))
    return false;

  MachineInstrBuilder MemMIB =
      BuildMI(MBB, LoadMI, UnaryI->getDebugLoc(), TII.get(MemOpcode))
          .setMIFlags(UnaryI->getFlags());
  addRetainedLoadMemIncDecAddressOperands(MemMIB, LoadMI, Info.AddrKind);
  MemMIB.cloneMemRefs(LoadMI);
  MemMIB.cloneMemRefs(*StoreI);

  I = MemMIB.getInstr()->getIterator();
  StoreI->eraseFromParent();
  UnaryI->eraseFromParent();
  LoadMI.eraseFromParent();
  ++NumDeadMemIncDecsFolded;
  return true;
}

static unsigned getModOpcodeForDivRemDecomposition(unsigned DivOpcode,
                                                   unsigned &MulOpcode,
                                                   unsigned &SubOpcode) {
  switch (DivOpcode) {
  case Bedrock::DIVUL3rr:
    MulOpcode = Bedrock::MULL3rr;
    SubOpcode = Bedrock::SUBL3rr;
    return Bedrock::MODUL3rr;
  case Bedrock::DIVUQ3rr:
    MulOpcode = Bedrock::MULQ3rr;
    SubOpcode = Bedrock::SUBQ3rr;
    return Bedrock::MODUQ3rr;
  case Bedrock::DIVSL3rr:
    MulOpcode = Bedrock::MULL3rr;
    SubOpcode = Bedrock::SUBL3rr;
    return Bedrock::MODSL3rr;
  case Bedrock::DIVSQ3rr:
    MulOpcode = Bedrock::MULQ3rr;
    SubOpcode = Bedrock::SUBQ3rr;
    return Bedrock::MODSQ3rr;
  default:
    return 0;
  }
}

bool BedrockPreEmitPeephole::foldDivRemDecomposition(
    MachineBasicBlock::iterator I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &QuotCopyMI = *I;
  if (QuotCopyMI.getOpcode() != Bedrock::MOVQrr ||
      QuotCopyMI.getNumExplicitOperands() != 2 ||
      !QuotCopyMI.getOperand(0).isReg() || !QuotCopyMI.getOperand(1).isReg())
    return false;

  Register QuotReg = QuotCopyMI.getOperand(0).getReg();
  Register RemReg = QuotCopyMI.getOperand(1).getReg();

  auto DivI = std::next(I);
  while (DivI != MBB.end() && DivI->isDebugInstr())
    ++DivI;
  if (DivI == MBB.end() || DivI->getNumExplicitOperands() != 3 ||
      !DivI->getOperand(0).isReg() || !DivI->getOperand(1).isReg() ||
      !DivI->getOperand(2).isReg())
    return false;

  unsigned MulOpcode;
  unsigned SubOpcode;
  unsigned ModOpcode =
      getModOpcodeForDivRemDecomposition(DivI->getOpcode(), MulOpcode,
                                         SubOpcode);
  if (!ModOpcode || DivI->getOperand(0).getReg() != QuotReg ||
      DivI->getOperand(1).getReg() != QuotReg ||
      !DivI->getOperand(1).isKill())
    return false;
  Register DivisorReg = DivI->getOperand(2).getReg();

  auto ProductCopyI = std::next(DivI);
  while (ProductCopyI != MBB.end() && ProductCopyI->isDebugInstr())
    ++ProductCopyI;
  if (ProductCopyI == MBB.end() ||
      ProductCopyI->getOpcode() != Bedrock::MOVQrr ||
      ProductCopyI->getNumExplicitOperands() != 2 ||
      !ProductCopyI->getOperand(0).isReg() ||
      !ProductCopyI->getOperand(1).isReg() ||
      ProductCopyI->getOperand(1).getReg() != QuotReg ||
      ProductCopyI->getOperand(1).isKill())
    return false;
  Register ProductReg = ProductCopyI->getOperand(0).getReg();

  auto MulI = std::next(ProductCopyI);
  while (MulI != MBB.end() && MulI->isDebugInstr())
    ++MulI;
  if (MulI == MBB.end() || MulI->getOpcode() != MulOpcode ||
      MulI->getNumExplicitOperands() != 3 || !MulI->getOperand(0).isReg() ||
      !MulI->getOperand(1).isReg() || !MulI->getOperand(2).isReg() ||
      MulI->getOperand(0).getReg() != ProductReg ||
      MulI->getOperand(1).getReg() != ProductReg ||
      !MulI->getOperand(1).isKill() ||
      MulI->getOperand(2).getReg() != DivisorReg)
    return false;

  auto SubI = std::next(MulI);
  while (SubI != MBB.end() && SubI->isDebugInstr())
    ++SubI;
  if (SubI == MBB.end() || SubI->getOpcode() != SubOpcode ||
      SubI->getNumExplicitOperands() != 3 || !SubI->getOperand(0).isReg() ||
      !SubI->getOperand(1).isReg() || !SubI->getOperand(2).isReg() ||
      SubI->getOperand(0).getReg() != RemReg ||
      SubI->getOperand(1).getReg() != RemReg ||
      !SubI->getOperand(1).isKill() ||
      SubI->getOperand(2).getReg() != ProductReg ||
      !SubI->getOperand(2).isKill() || !flagsAreDeadAfter(SubI, MBB))
    return false;

  bool DivisorKill = MulI->getOperand(2).isKill();
  SubI->setDesc(TII.get(ModOpcode));
  SubI->setFlags(0);
  SubI->getOperand(2).setReg(DivisorReg);
  SubI->getOperand(2).setIsKill(DivisorKill);
  MulI->eraseFromParent();
  ProductCopyI->eraseFromParent();

  ++NumDivRemDecompositionsFolded;
  return true;
}

bool BedrockPreEmitPeephole::foldRedundantSelfLogic(
    MachineBasicBlock::iterator I, MachineBasicBlock &MBB) {
  MachineInstr &MI = *I;
  switch (MI.getOpcode()) {
  case Bedrock::ANDL3rr:
  case Bedrock::ANDQ3rr:
  case Bedrock::ORL3rr:
  case Bedrock::ORQ3rr:
    break;
  default:
    return false;
  }

  if (MI.getNumExplicitOperands() != 3 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg() || !MI.getOperand(2).isReg())
    return false;

  Register Reg = MI.getOperand(0).getReg();
  if (MI.getOperand(1).getReg() != Reg || MI.getOperand(2).getReg() != Reg)
    return false;
  if (!flagsAreDeadAfter(I, MBB))
    return false;

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding redundant self "
                       "logic: ";
             MI.dump());

  MI.eraseFromParent();
  ++NumRedundantSelfLogicFolded;
  return true;
}

static unsigned getFoldedExtendOpcode(unsigned NarrowOpcode,
                                      unsigned WideOpcode) {
  switch (NarrowOpcode) {
  case Bedrock::EXTZLBrr:
    return WideOpcode == Bedrock::EXTZQLrr ? Bedrock::EXTZQBrr : 0;
  case Bedrock::EXTZLWrr:
    return WideOpcode == Bedrock::EXTZQLrr ? Bedrock::EXTZQWrr : 0;
  case Bedrock::EXTSLBrr:
    return WideOpcode == Bedrock::EXTSQLrr ? Bedrock::EXTSQBrr : 0;
  case Bedrock::EXTSLWrr:
    return WideOpcode == Bedrock::EXTSQLrr ? Bedrock::EXTSQWrr : 0;
  default:
    return 0;
  }
}

bool BedrockPreEmitPeephole::foldTwoStageExtend(
    MachineBasicBlock::iterator I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &NarrowMI = *I;
  if (NarrowMI.getNumExplicitOperands() != 2 || !NarrowMI.getOperand(0).isReg() ||
      !NarrowMI.getOperand(1).isReg())
    return false;

  auto WideI = std::next(I);
  while (WideI != MBB.end() && WideI->isDebugInstr())
    ++WideI;
  if (WideI == MBB.end() || WideI->getNumExplicitOperands() != 2 ||
      !WideI->getOperand(0).isReg() || !WideI->getOperand(1).isReg())
    return false;

  unsigned FoldedOpcode =
      getFoldedExtendOpcode(NarrowMI.getOpcode(), WideI->getOpcode());
  if (!FoldedOpcode)
    return false;

  Register NarrowDst = NarrowMI.getOperand(0).getReg();
  Register NarrowSrc = NarrowMI.getOperand(1).getReg();
  Register WideDst = WideI->getOperand(0).getReg();
  MachineOperand &WideSrcOp = WideI->getOperand(1);
  if (WideSrcOp.getReg() != NarrowDst || !WideSrcOp.isKill())
    return false;

  bool SrcKill = NarrowMI.getOperand(1).isKill();
  if (NarrowSrc == NarrowDst)
    SrcKill |= WideSrcOp.isKill();
  bool DstDead = WideI->getOperand(0).isDead();

  NarrowMI.setDesc(TII.get(FoldedOpcode));
  NarrowMI.getOperand(0).setReg(WideDst);
  NarrowMI.getOperand(0).setIsDead(DstDead);
  NarrowMI.getOperand(1).setReg(NarrowSrc);
  NarrowMI.getOperand(1).setIsKill(SrcKill);
  WideI->eraseFromParent();

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding two-stage extend: ";
             NarrowMI.dump());

  ++NumTwoStageExtendsFolded;
  return true;
}

bool BedrockPreEmitPeephole::foldAndExtend(MachineBasicBlock::iterator I,
                                           MachineBasicBlock &MBB,
                                           const TargetInstrInfo &TII) {
  MachineInstr &AndMI = *I;
  if (AndMI.getOpcode() != Bedrock::ANDL3ri ||
      AndMI.getNumExplicitOperands() != 3 || !AndMI.getOperand(0).isReg() ||
      !AndMI.getOperand(1).isReg() || !AndMI.getOperand(2).isImm())
    return false;

  int64_t Imm = AndMI.getOperand(2).getImm();
  if (Imm < 0 || Imm > 0xffffffffLL)
    return false;

  auto ExtI = std::next(I);
  while (ExtI != MBB.end() && ExtI->isDebugInstr())
    ++ExtI;
  if (ExtI == MBB.end() || ExtI->getOpcode() != Bedrock::EXTZQLrr ||
      ExtI->getNumExplicitOperands() != 2 || !ExtI->getOperand(0).isReg() ||
      !ExtI->getOperand(1).isReg())
    return false;

  Register AndDst = AndMI.getOperand(0).getReg();
  if (ExtI->getOperand(0).getReg() != AndDst ||
      ExtI->getOperand(1).getReg() != AndDst || !ExtI->getOperand(1).isKill())
    return false;
  if (!flagsAreDeadAfter(I, MBB))
    return false;

  AndMI.setDesc(TII.get(Bedrock::ANDQ3ri));
  ExtI->eraseFromParent();

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding and+extend: ";
             AndMI.dump());

  ++NumAndExtendsFolded;
  return true;
}

static bool isGPR(Register Reg) {
  switch (Reg) {
  case Bedrock::R0:
  case Bedrock::R1:
  case Bedrock::R2:
  case Bedrock::R3:
  case Bedrock::R4:
  case Bedrock::R5:
  case Bedrock::R6:
  case Bedrock::R7:
  case Bedrock::R8:
  case Bedrock::R9:
  case Bedrock::R10:
  case Bedrock::R11:
  case Bedrock::R12:
  case Bedrock::R13:
  case Bedrock::R14:
  case Bedrock::R15:
    return true;
  default:
    return false;
  }
}

static bool isKnownZeroHighLoad(unsigned Opcode) {
  switch (Opcode) {
  case Bedrock::LOADB_Zrr:
  case Bedrock::LOADW_Zrr:
  case Bedrock::LOADL_Zrr:
  case Bedrock::LOADB_Zro:
  case Bedrock::LOADW_Zro:
  case Bedrock::LOADL_Zro:
  case Bedrock::LOADB_Zabs:
  case Bedrock::LOADW_Zabs:
  case Bedrock::LOADL_Zabs:
  case Bedrock::LOADB_Zfi:
  case Bedrock::LOADW_Zfi:
  case Bedrock::LOADL_Zfi:
    return true;
  default:
    return false;
  }
}

static bool isHighPreservingLongOp(unsigned Opcode) {
  switch (Opcode) {
  case Bedrock::EXTZLBrr:
  case Bedrock::EXTZLWrr:
  case Bedrock::EXTSLBrr:
  case Bedrock::EXTSLWrr:
  case Bedrock::MOVLrr:
  case Bedrock::ADDL3rr:
  case Bedrock::ADDL3ri:
  case Bedrock::SUBL3rr:
  case Bedrock::SUBL3ri:
  case Bedrock::ANDL3rr:
  case Bedrock::ANDL3ri:
  case Bedrock::ORL3rr:
  case Bedrock::ORL3ri:
  case Bedrock::XORL3rr:
  case Bedrock::XORL3ri:
  case Bedrock::SHLL3rr:
  case Bedrock::SHLL3ri:
  case Bedrock::SHRL3rr:
  case Bedrock::SHRL3ri:
  case Bedrock::SARL3rr:
  case Bedrock::SARL3ri:
  case Bedrock::ROLL3rr:
  case Bedrock::ROLL3ri:
  case Bedrock::RORL3rr:
  case Bedrock::RORL3ri:
  case Bedrock::INCL3r:
  case Bedrock::DECL3r:
  case Bedrock::NEGL3r:
  case Bedrock::ABSL3r:
  case Bedrock::NOTL3r:
  case Bedrock::MINUL3rr:
  case Bedrock::MINUL3ri:
  case Bedrock::MINSL3rr:
  case Bedrock::MINSL3ri:
  case Bedrock::MAXUL3rr:
  case Bedrock::MAXUL3ri:
  case Bedrock::MAXSL3rr:
  case Bedrock::MAXSL3ri:
  case Bedrock::MULL3rr:
  case Bedrock::DIVUL3rr:
  case Bedrock::DIVSL3rr:
  case Bedrock::DIVSL3ri:
  case Bedrock::MODUL3rr:
  case Bedrock::MODSL3rr:
    return true;
  default:
    return false;
  }
}

static bool isNonNegativeU32Imm(const MachineOperand &MO) {
  return MO.isImm() && MO.getImm() >= 0 && MO.getImm() <= 0xffffffffLL;
}

static unsigned getPromotedExtQOpcode(unsigned Opcode) {
  switch (Opcode) {
  case Bedrock::EXTZLBrr:
    return Bedrock::EXTZQBrr;
  case Bedrock::EXTZLWrr:
    return Bedrock::EXTZQWrr;
  default:
    return 0;
  }
}

static bool definesKnownZeroHigh(
    const MachineInstr &MI, Register DefReg,
    const SmallSet<Register, 16> &KnownZeroHighBefore) {
  if (MI.getNumExplicitOperands() == 0 || !MI.getOperand(0).isReg() ||
      MI.getOperand(0).getReg() != DefReg)
    return false;

  switch (MI.getOpcode()) {
  case Bedrock::CLRQr:
  case Bedrock::EXTZQBrr:
  case Bedrock::EXTZQWrr:
  case Bedrock::EXTZQLrr:
    return true;
  case Bedrock::CONST32:
  case Bedrock::CONST64:
    return MI.getNumExplicitOperands() >= 2 &&
           isNonNegativeU32Imm(MI.getOperand(1));
  case Bedrock::MOVQrr:
    return MI.getNumExplicitOperands() >= 2 && MI.getOperand(1).isReg() &&
           KnownZeroHighBefore.contains(MI.getOperand(1).getReg());
  case Bedrock::ANDQ3ri:
    return MI.getNumExplicitOperands() >= 3 &&
           isNonNegativeU32Imm(MI.getOperand(2));
  default:
    if (isKnownZeroHighLoad(MI.getOpcode()))
      return true;
    if (isHighPreservingLongOp(MI.getOpcode()))
      return KnownZeroHighBefore.contains(DefReg);
    return false;
  }
}

static void addKnownZeroHighLiveIns(const MachineFunction &MF,
                                    const MachineBasicBlock &MBB,
                                    SmallSet<Register, 16> &KnownZeroHigh) {
  if (&MBB != &MF.front())
    return;

  static constexpr Register ArgRegs[] = {
      Bedrock::R0, Bedrock::R1, Bedrock::R2, Bedrock::R3,
      Bedrock::R4, Bedrock::R5, Bedrock::R6, Bedrock::R7};
  unsigned RegIdx = 0;
  for (const Argument &Arg : MF.getFunction().args()) {
    if (Arg.getType()->isFloatingPointTy())
      continue;
    if (!Arg.getType()->isIntegerTy() && !Arg.getType()->isPointerTy())
      continue;
    if (RegIdx >= std::size(ArgRegs))
      return;

    if (Arg.getType()->isIntegerTy() &&
        Arg.getType()->getIntegerBitWidth() <= 32 && Arg.hasZExtAttr())
      KnownZeroHigh.insert(ArgRegs[RegIdx]);
    ++RegIdx;
  }
}

static void addAllGPRs(SmallSet<Register, 16> &Regs) {
  for (Register Reg : {Bedrock::R0, Bedrock::R1, Bedrock::R2, Bedrock::R3,
                       Bedrock::R4, Bedrock::R5, Bedrock::R6, Bedrock::R7,
                       Bedrock::R8, Bedrock::R9, Bedrock::R10, Bedrock::R11,
                       Bedrock::R12, Bedrock::R13, Bedrock::R14, Bedrock::R15})
    Regs.insert(Reg);
}

static bool knownZeroHighSetsEqual(const SmallSet<Register, 16> &LHS,
                                   const SmallSet<Register, 16> &RHS) {
  if (LHS.size() != RHS.size())
    return false;
  for (Register Reg : LHS) {
    if (!RHS.contains(Reg))
      return false;
  }
  return true;
}

static void transferKnownZeroHighBlock(const MachineBasicBlock &MBB,
                                       SmallSet<Register, 16> &KnownZeroHigh) {
  for (const MachineInstr &MI : MBB) {
    if (MI.isDebugInstr())
      continue;

    SmallSet<Register, 16> KnownBefore = KnownZeroHigh;
    SmallVector<Register, 4> ExplicitGPRDefs;
    for (const MachineOperand &MO : MI.explicit_operands()) {
      if (MO.isReg() && MO.isDef() && isGPR(MO.getReg()))
        ExplicitGPRDefs.push_back(MO.getReg());
    }

    for (Register Reg : ExplicitGPRDefs)
      KnownZeroHigh.erase(Reg);
    for (Register Reg : ExplicitGPRDefs) {
      if (definesKnownZeroHigh(MI, Reg, KnownBefore))
        KnownZeroHigh.insert(Reg);
    }

    if (MI.isCall()) {
      for (Register Reg : {Bedrock::R0, Bedrock::R1, Bedrock::R2,
                           Bedrock::R3, Bedrock::R4, Bedrock::R5,
                           Bedrock::R6, Bedrock::R7})
        KnownZeroHigh.erase(Reg);
    }
  }
}

static DenseMap<const MachineBasicBlock *, SmallSet<Register, 16>>
computeKnownZeroHighBlockInputs(MachineFunction &MF) {
  DenseMap<const MachineBasicBlock *, SmallSet<Register, 16>> Inputs;
  DenseMap<const MachineBasicBlock *, SmallSet<Register, 16>> Outputs;
  if (MF.empty())
    return Inputs;

  SmallSet<Register, 16> Top;
  addAllGPRs(Top);
  for (const MachineBasicBlock &MBB : MF) {
    if (&MBB == &MF.front()) {
      addKnownZeroHighLiveIns(MF, MBB, Inputs[&MBB]);
      Outputs[&MBB] = Inputs[&MBB];
    } else if (MBB.pred_empty()) {
      Inputs[&MBB] = SmallSet<Register, 16>();
      Outputs[&MBB] = SmallSet<Register, 16>();
    } else {
      Inputs[&MBB] = Top;
      Outputs[&MBB] = Top;
    }
  }

  bool Changed;
  do {
    Changed = false;
    for (const MachineBasicBlock &MBB : MF) {
      SmallSet<Register, 16> NewInput;
      if (&MBB == &MF.front()) {
        addKnownZeroHighLiveIns(MF, MBB, NewInput);
      } else if (!MBB.pred_empty()) {
        bool FirstPred = true;
        for (const MachineBasicBlock *Pred : MBB.predecessors()) {
          auto OutIt = Outputs.find(Pred);
          if (OutIt == Outputs.end())
            continue;
          if (FirstPred) {
            NewInput = OutIt->second;
            FirstPred = false;
            continue;
          }

          SmallVector<Register, 16> ToErase;
          for (Register Reg : NewInput) {
            if (!OutIt->second.contains(Reg))
              ToErase.push_back(Reg);
          }
          for (Register Reg : ToErase)
            NewInput.erase(Reg);
        }
      }

      SmallSet<Register, 16> NewOutput = NewInput;
      transferKnownZeroHighBlock(MBB, NewOutput);
      if (!knownZeroHighSetsEqual(Inputs[&MBB], NewInput)) {
        Inputs[&MBB] = NewInput;
        Changed = true;
      }
      if (!knownZeroHighSetsEqual(Outputs[&MBB], NewOutput)) {
        Outputs[&MBB] = NewOutput;
        Changed = true;
      }
    }
  } while (Changed);

  return Inputs;
}

static bool isCallerSavedGPR(Register Reg);

static bool foldKnownZeroHighCopyExtend(MachineBasicBlock::iterator I,
                                        MachineBasicBlock &MBB) {
  MachineInstr &ExtMI = *I;
  if (ExtMI.getOpcode() != Bedrock::EXTZQLrr ||
      ExtMI.getNumExplicitOperands() != 2 || !ExtMI.getOperand(0).isReg() ||
      !ExtMI.getOperand(1).isReg())
    return false;

  Register DstReg = ExtMI.getOperand(0).getReg();
  Register SrcReg = ExtMI.getOperand(1).getReg();
  if (DstReg == SrcReg || !DstReg.isPhysical() || !SrcReg.isPhysical() ||
      !isGPR(DstReg) || !isGPR(SrcReg) || successorHasLiveIn(MBB, DstReg))
    return false;

  SmallVector<MachineInstr *, 8> RewriteMIs;
  bool SawCall = false;
  for (auto ScanI = std::next(I); ScanI != MBB.end(); ++ScanI) {
    if (ScanI->isDebugInstr())
      continue;

    if (instrDefinesReg(*ScanI, SrcReg))
      return false;

    if (instrUsesReg(*ScanI, DstReg)) {
      if (SawCall && isCallerSavedGPR(SrcReg))
        return false;
      for (const MachineOperand &MO : ScanI->operands()) {
        if (MO.isReg() && MO.getReg() == DstReg &&
            (MO.isDef() || MO.isImplicit()))
          return false;
      }
      RewriteMIs.push_back(&*ScanI);
    }

    if (instrDefinesReg(*ScanI, DstReg))
      break;

    if (ScanI->isCall())
      SawCall = true;
    if (ScanI->isTerminator())
      break;
  }

  if (RewriteMIs.empty() && !ExtMI.getOperand(0).isDead())
    return false;

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding known-zero copy "
                       "extend: ";
             ExtMI.dump());

  ExtMI.getOperand(1).setIsKill(false);
  for (MachineInstr *RewriteMI : RewriteMIs) {
    for (MachineOperand &MO : RewriteMI->operands()) {
      if (MO.isReg() && !MO.isDef() && MO.getReg() == DstReg) {
        MO.setReg(SrcReg);
        MO.setIsKill(false);
      }
    }
  }

  ExtMI.eraseFromParent();
  ++NumKnownZeroCopyExtendsFolded;
  return true;
}

static DenseMap<Register, MachineInstr *>
collectEntryPromotableZeroExts(MachineFunction &MF) {
  DenseMap<Register, MachineInstr *> UniqueDefs;
  SmallSet<Register, 16> MultipleDefs;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;

      for (MachineOperand &MO : MI.operands()) {
        if (!MO.isReg() || !MO.isDef() || !isGPR(MO.getReg()))
          continue;

        Register Reg = MO.getReg();
        if (MultipleDefs.contains(Reg))
          continue;

        if (UniqueDefs.contains(Reg)) {
          UniqueDefs.erase(Reg);
          MultipleDefs.insert(Reg);
          continue;
        }

        UniqueDefs[Reg] = &MI;
      }
    }
  }

  SmallVector<Register, 8> ToErase;
  for (const auto &KV : UniqueDefs) {
    MachineInstr *MI = KV.second;
    if (MI->getParent() != &MF.front() ||
        !getPromotedExtQOpcode(MI->getOpcode()) ||
        MI->getNumExplicitOperands() != 2 || !MI->getOperand(0).isReg() ||
        MI->getOperand(0).getReg() != KV.first)
      ToErase.push_back(KV.first);
  }
  for (Register Reg : ToErase)
    UniqueDefs.erase(Reg);

  return UniqueDefs;
}

bool BedrockPreEmitPeephole::foldKnownZeroHighExtends(
    MachineBasicBlock &MBB, const TargetInstrInfo &TII,
    const DenseMap<Register, MachineInstr *> &EntryPromotableZeroExts,
    const DenseMap<const MachineBasicBlock *, SmallSet<Register, 16>>
        &KnownZeroHighBlockInputs) {
  bool Changed = false;
  SmallSet<Register, 16> KnownZeroHigh;
  DenseMap<Register, MachineInstr *> PromotableZeroExtToLong;
  auto InputIt = KnownZeroHighBlockInputs.find(&MBB);
  if (InputIt != KnownZeroHighBlockInputs.end())
    KnownZeroHigh = InputIt->second;
  else
    addKnownZeroHighLiveIns(*MBB.getParent(), MBB, KnownZeroHigh);
  if (&MBB != &MBB.getParent()->front())
    PromotableZeroExtToLong = EntryPromotableZeroExts;

  for (auto I = MBB.begin(); I != MBB.end();) {
    if (I->isDebugInstr()) {
      ++I;
      continue;
    }

    if (I->getOpcode() == Bedrock::EXTZQLrr &&
        I->getNumExplicitOperands() == 2 && I->getOperand(0).isReg() &&
        I->getOperand(1).isReg() &&
        I->getOperand(0).getReg() == I->getOperand(1).getReg() &&
        KnownZeroHigh.contains(I->getOperand(0).getReg())) {
      LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding known-zero "
                           "extend: ";
                 I->dump());
      auto NextI = std::next(I);
      I->eraseFromParent();
      I = NextI;
      ++NumKnownZeroExtendsFolded;
      Changed = true;
      continue;
    }

    if (I->getOpcode() == Bedrock::EXTZQLrr &&
        I->getNumExplicitOperands() == 2 && I->getOperand(0).isReg() &&
        I->getOperand(1).isReg() &&
        I->getOperand(0).getReg() != I->getOperand(1).getReg() &&
        KnownZeroHigh.contains(I->getOperand(1).getReg())) {
      auto NextI = std::next(I);
      if (foldKnownZeroHighCopyExtend(I, MBB)) {
        I = NextI;
        Changed = true;
        continue;
      }
    }

    SmallSet<Register, 16> KnownBefore = KnownZeroHigh;

    if (I->getOpcode() == Bedrock::ANDL3rr &&
        I->getNumExplicitOperands() == 3 && I->getOperand(0).isReg() &&
        I->getOperand(1).isReg() && I->getOperand(2).isReg()) {
      MachineInstr *PromoteExt = nullptr;
      if (!KnownBefore.contains(I->getOperand(1).getReg()) &&
          !KnownBefore.contains(I->getOperand(2).getReg())) {
        PromoteExt = PromotableZeroExtToLong.lookup(I->getOperand(1).getReg());
        if (!PromoteExt)
          PromoteExt =
              PromotableZeroExtToLong.lookup(I->getOperand(2).getReg());
      }

      Register AndDst = I->getOperand(0).getReg();
      auto ExtI = std::next(I);
      bool Blocked = false;
      while (ExtI != MBB.end()) {
        if (ExtI->isDebugInstr()) {
          ++ExtI;
          continue;
        }
        if (ExtI->getOpcode() == Bedrock::EXTZQLrr &&
            ExtI->getNumExplicitOperands() == 2 &&
            ExtI->getOperand(0).isReg() && ExtI->getOperand(1).isReg() &&
            ExtI->getOperand(0).getReg() == AndDst &&
            ExtI->getOperand(1).getReg() == AndDst)
          break;
        if (usesReg(*ExtI, AndDst) || ExtI->isCall() || ExtI->isTerminator()) {
          Blocked = true;
          break;
        }
        ++ExtI;
      }
      if ((KnownBefore.contains(I->getOperand(1).getReg()) ||
           KnownBefore.contains(I->getOperand(2).getReg()) || PromoteExt) &&
          !Blocked && ExtI != MBB.end() &&
          ExtI->getOpcode() == Bedrock::EXTZQLrr &&
          ExtI->getNumExplicitOperands() == 2 &&
          ExtI->getOperand(0).isReg() && ExtI->getOperand(1).isReg() &&
          ExtI->getOperand(0).getReg() == AndDst &&
          ExtI->getOperand(1).getReg() == AndDst &&
          ExtI->getOperand(1).isKill() && flagsAreDeadAfter(I, MBB)) {
        LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding known-zero "
                             "and+extend: ";
                   I->dump(); ExtI->dump());
        if (PromoteExt) {
          unsigned PromotedOpcode = getPromotedExtQOpcode(PromoteExt->getOpcode());
          if (PromotedOpcode)
            PromoteExt->setDesc(TII.get(PromotedOpcode));
        }
        I->setDesc(TII.get(Bedrock::ANDQ3rr));
        ExtI->eraseFromParent();
        ++NumKnownZeroAndExtendsFolded;
        return true;
      }
    }

    SmallVector<Register, 4> ExplicitGPRDefs;
    for (const MachineOperand &MO : I->explicit_operands()) {
      if (MO.isReg() && MO.isDef() && isGPR(MO.getReg()))
        ExplicitGPRDefs.push_back(MO.getReg());
    }

    for (Register Reg : ExplicitGPRDefs)
      KnownZeroHigh.erase(Reg);
    for (Register Reg : ExplicitGPRDefs) {
      if (definesKnownZeroHigh(*I, Reg, KnownBefore))
        KnownZeroHigh.insert(Reg);
    }

    if (I->isCall()) {
      for (Register Reg : {Bedrock::R0, Bedrock::R1, Bedrock::R2, Bedrock::R3,
                           Bedrock::R4, Bedrock::R5, Bedrock::R6, Bedrock::R7})
        KnownZeroHigh.erase(Reg);
      PromotableZeroExtToLong.clear();
    }

    for (const MachineOperand &MO : I->explicit_operands()) {
      if (!MO.isReg() || !isGPR(MO.getReg()))
        continue;
      PromotableZeroExtToLong.erase(MO.getReg());
    }

    if (getPromotedExtQOpcode(I->getOpcode()) &&
        I->getNumExplicitOperands() == 2 && I->getOperand(0).isReg() &&
        I->getOperand(1).isReg()) {
      Register DefReg = I->getOperand(0).getReg();
      if (isGPR(DefReg))
        PromotableZeroExtToLong[DefReg] = &*I;
    }

    ++I;
  }

  return Changed;
}

static void addSignExtendedLiveIns(const MachineFunction &MF,
                                   const MachineBasicBlock &MBB,
                                   SmallSet<Register, 16> &SignExtended) {
  if (&MBB != &MF.front())
    return;

  static constexpr Register ArgRegs[] = {
      Bedrock::R0, Bedrock::R1, Bedrock::R2, Bedrock::R3,
      Bedrock::R4, Bedrock::R5, Bedrock::R6, Bedrock::R7};
  unsigned RegIdx = 0;
  for (const Argument &Arg : MF.getFunction().args()) {
    if (Arg.getType()->isFloatingPointTy())
      continue;
    if (!Arg.getType()->isIntegerTy() && !Arg.getType()->isPointerTy())
      continue;
    if (RegIdx >= std::size(ArgRegs))
      return;

    if (Arg.getType()->isIntegerTy() &&
        Arg.getType()->getIntegerBitWidth() <= 32 && Arg.hasSExtAttr())
      SignExtended.insert(ArgRegs[RegIdx]);
    ++RegIdx;
  }
}

static bool isSignedI32Imm(const MachineOperand &MO) {
  return MO.isImm() && MO.getImm() >= std::numeric_limits<int32_t>::min() &&
         MO.getImm() <= std::numeric_limits<int32_t>::max();
}

static bool definesSignExtendedI32High(
    const MachineInstr &MI, Register DefReg,
    const SmallSet<Register, 16> &SignExtendedBefore) {
  if (MI.getNumExplicitOperands() == 0 || !MI.getOperand(0).isReg() ||
      MI.getOperand(0).getReg() != DefReg)
    return false;

  switch (MI.getOpcode()) {
  case Bedrock::EXTSQLrr:
    return true;
  case Bedrock::CONST32:
  case Bedrock::CONST64:
    return MI.getNumExplicitOperands() >= 2 && isSignedI32Imm(MI.getOperand(1));
  case Bedrock::MOVQrr:
    return MI.getNumExplicitOperands() >= 2 && MI.getOperand(1).isReg() &&
           SignExtendedBefore.contains(MI.getOperand(1).getReg());
  case Bedrock::SMAX_ZERO_Q:
    return MI.getNumExplicitOperands() >= 2 && MI.getOperand(1).isReg() &&
           SignExtendedBefore.contains(MI.getOperand(1).getReg());
  default:
    return false;
  }
}

bool BedrockPreEmitPeephole::foldSignExtendedSMaxZeroExtends(
    MachineBasicBlock &MBB, const TargetInstrInfo &TII) {
  bool Changed = false;
  SmallSet<Register, 16> SignExtended;
  addSignExtendedLiveIns(*MBB.getParent(), MBB, SignExtended);

  for (auto I = MBB.begin(); I != MBB.end();) {
    if (I->isDebugInstr()) {
      ++I;
      continue;
    }

    SmallSet<Register, 16> SignExtendedBefore = SignExtended;

    if (I->getOpcode() == Bedrock::SMAX_ZERO_L &&
        I->getNumExplicitOperands() == 2 && I->getOperand(0).isReg() &&
        I->getOperand(1).isReg() &&
        I->getOperand(0).getReg() == I->getOperand(1).getReg() &&
        SignExtendedBefore.contains(I->getOperand(1).getReg())) {
      auto ExtI = std::next(I);
      while (ExtI != MBB.end() && ExtI->isDebugInstr())
        ++ExtI;
      if (ExtI != MBB.end() && ExtI->getOpcode() == Bedrock::EXTZQLrr &&
          ExtI->getNumExplicitOperands() == 2 &&
          ExtI->getOperand(0).isReg() && ExtI->getOperand(1).isReg() &&
          ExtI->getOperand(1).getReg() == I->getOperand(0).getReg()) {
        Register Reg = I->getOperand(0).getReg();
        Register ExtReg = ExtI->getOperand(0).getReg();
        bool CanFold = false;
        bool ReplaceExtUses = false;

        if (ExtReg == Reg && ExtI->getOperand(1).isKill()) {
          CanFold = true;
        } else if (&MBB == &MBB.getParent()->front() && ExtReg != Reg &&
                   ExtReg.isPhysical()) {
          MachineFunction &MF = *MBB.getParent();
          bool SeenExt = false;
          bool SawExtUse = false;
          CanFold = true;
          for (MachineBasicBlock &ScanMBB : MF) {
            for (MachineInstr &ScanMI : ScanMBB) {
              if (ScanMI.isDebugInstr())
                continue;
              if (&ScanMI == &*ExtI) {
                SeenExt = true;
                continue;
              }
              if (!SeenExt)
                continue;

              if (definesReg(ScanMI, Reg)) {
                CanFold = false;
                break;
              }
              if (!usesReg(ScanMI, ExtReg))
                continue;
              if (definesReg(ScanMI, ExtReg)) {
                CanFold = false;
                break;
              }
              for (const MachineOperand &MO : ScanMI.operands()) {
                if (!MO.isReg() || MO.getReg() != ExtReg)
                  continue;
                if (MO.isDef() || MO.isImplicit()) {
                  CanFold = false;
                  break;
                }
                SawExtUse = true;
              }
              if (!CanFold)
                break;
            }
            if (!CanFold)
              break;
          }
          ReplaceExtUses = CanFold && SawExtUse;
          CanFold = ReplaceExtUses;
        }

        if (CanFold) {
          LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding "
                               "sign-extended smax-zero extend: ";
                     I->dump(); ExtI->dump());

          if (ReplaceExtUses) {
            MachineFunction &MF = *MBB.getParent();
            bool SeenExt = false;
            for (MachineBasicBlock &RewriteMBB : MF) {
              for (MachineInstr &RewriteMI : RewriteMBB) {
                if (RewriteMI.isDebugInstr())
                  continue;
                if (&RewriteMI == &*ExtI) {
                  SeenExt = true;
                  continue;
                }
                if (!SeenExt)
                  continue;
                for (MachineOperand &MO : RewriteMI.operands()) {
                  if (MO.isReg() && !MO.isDef() && MO.getReg() == ExtReg) {
                    MO.setReg(Reg);
                    MO.setIsKill(false);
                  }
                }
              }

              if (RewriteMBB.isLiveIn(ExtReg.asMCReg())) {
                RewriteMBB.removeLiveIn(ExtReg.asMCReg());
                if (!RewriteMBB.isLiveIn(Reg.asMCReg()))
                  RewriteMBB.addLiveIn(Reg.asMCReg());
              }
            }
          }

          I->setDesc(TII.get(Bedrock::SMAX_ZERO_Q));
          I->getOperand(0).setIsDead(ExtI->getOperand(0).isDead() &&
                                     !ReplaceExtUses);
          auto NextI = std::next(ExtI);
          ExtI->eraseFromParent();
          SignExtended.erase(Reg);
          SignExtended.insert(Reg);
          I = NextI;
          ++NumSignExtendedSMaxZeroExtendsFolded;
          Changed = true;
          continue;
        }
      }
    }

    SmallVector<Register, 4> ExplicitGPRDefs;
    for (const MachineOperand &MO : I->explicit_operands()) {
      if (MO.isReg() && MO.isDef() && isGPR(MO.getReg()))
        ExplicitGPRDefs.push_back(MO.getReg());
    }

    for (Register Reg : ExplicitGPRDefs)
      SignExtended.erase(Reg);
    for (Register Reg : ExplicitGPRDefs) {
      if (definesSignExtendedI32High(*I, Reg, SignExtendedBefore))
        SignExtended.insert(Reg);
    }

    if (I->isCall()) {
      for (Register Reg : {Bedrock::R0, Bedrock::R1, Bedrock::R2, Bedrock::R3,
                           Bedrock::R4, Bedrock::R5, Bedrock::R6, Bedrock::R7})
        SignExtended.erase(Reg);
    }

    ++I;
  }

  return Changed;
}

static void clearRegKillsBetween(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator Begin,
                                 MachineBasicBlock::iterator End,
                                 Register Reg);

bool BedrockPreEmitPeephole::foldZeroMinMaxWithKnownZero(
    MachineBasicBlock::iterator I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &MI = *I;
  unsigned NewOpcode;
  switch (MI.getOpcode()) {
  case Bedrock::SMAX_ZERO_L:
    NewOpcode = Bedrock::MAXSL3rr;
    break;
  case Bedrock::SMAX_ZERO_Q:
    NewOpcode = Bedrock::MAXSQ3rr;
    break;
  case Bedrock::SMIN_ZERO_L:
    NewOpcode = Bedrock::MINSL3rr;
    break;
  case Bedrock::SMIN_ZERO_Q:
    NewOpcode = Bedrock::MINSQ3rr;
    break;
  default:
    return false;
  }

  if (MI.getNumExplicitOperands() != 2 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg())
    return false;

  Register DstReg = MI.getOperand(0).getReg();
  Register SrcReg = MI.getOperand(1).getReg();
  if (DstReg != SrcReg || !DstReg.isPhysical() || !isGPR(DstReg))
    return false;

  SmallSet<Register, 16> BlockedRegs;
  MachineBasicBlock::iterator ZeroI = MBB.end();
  Register ZeroReg;
  for (auto ScanI = I; ScanI != MBB.begin();) {
    --ScanI;
    if (ScanI->isDebugInstr())
      continue;
    if (ScanI->isCall() || ScanI->isTerminator())
      break;

    Register CandidateReg;
    if (getZeroMaterializationReg(*ScanI, CandidateReg) &&
        CandidateReg != DstReg && CandidateReg.isPhysical() &&
        isGPR(CandidateReg) && !BlockedRegs.contains(CandidateReg)) {
      ZeroI = ScanI;
      ZeroReg = CandidateReg;
      break;
    }

    for (const MachineOperand &MO : ScanI->operands()) {
      if (MO.isReg() && MO.isDef() && MO.getReg().isPhysical() &&
          isGPR(MO.getReg()))
        BlockedRegs.insert(MO.getReg());
      else if (MO.isRegMask())
        break;
    }
  }
  if (ZeroI == MBB.end())
    return false;

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding zero min/max: ";
             ZeroI->dump(); MI.dump());

  ZeroI->getOperand(0).setIsDead(false);
  clearRegKillsBetween(MBB, std::next(ZeroI), I, ZeroReg);
  MI.setDesc(TII.get(NewOpcode));
  MI.addOperand(MachineOperand::CreateReg(ZeroReg, /*isDef=*/false));
  ++NumZeroMinMaxesFolded;
  return true;
}

static bool isCoreCommutativeBinaryRR(unsigned Opcode, bool &Is64) {
  switch (Opcode) {
  case Bedrock::ADDL3rr:
  case Bedrock::ANDL3rr:
  case Bedrock::ORL3rr:
  case Bedrock::XORL3rr:
    Is64 = false;
    return true;
  case Bedrock::ADDQ3rr:
  case Bedrock::ANDQ3rr:
  case Bedrock::ORQ3rr:
  case Bedrock::XORQ3rr:
    Is64 = true;
    return true;
  default:
    return false;
  }
}

static bool isI32ReturnValueCopy(const MachineFunction &MF,
                                 MachineBasicBlock::iterator CopyI,
                                 MachineBasicBlock &MBB, Register Reg) {
  if (!MF.getFunction().getReturnType()->isIntegerTy(32))
    return false;

  auto RetI = std::next(CopyI);
  while (RetI != MBB.end() && RetI->isDebugInstr())
    ++RetI;
  if (RetI == MBB.end() || RetI->getOpcode() != Bedrock::RET)
    return false;

  for (const MachineOperand &MO : RetI->operands()) {
    if (MO.isReg() && MO.getReg() == Reg && MO.isImplicit() && !MO.isDef())
      return true;
  }
  return false;
}

bool BedrockPreEmitPeephole::foldCommutativeCopy(MachineBasicBlock::iterator I,
                                                 MachineBasicBlock &MBB,
                                                 const MachineFunction &MF) {
  MachineInstr &BinMI = *I;
  bool Is64;
  if (!isCoreCommutativeBinaryRR(BinMI.getOpcode(), Is64))
    return false;
  if (BinMI.getNumExplicitOperands() != 3 || !BinMI.getOperand(0).isReg() ||
      !BinMI.getOperand(1).isReg() || !BinMI.getOperand(2).isReg())
    return false;

  auto CopyI = std::next(I);
  while (CopyI != MBB.end() && CopyI->isDebugInstr())
    ++CopyI;
  if (CopyI == MBB.end())
    return false;

  MachineInstr &CopyMI = *CopyI;
  unsigned CopyOpcode = CopyMI.getOpcode();
  if (CopyOpcode != Bedrock::MOVQrr && CopyOpcode != Bedrock::MOVLrr)
    return false;
  if (!CopyMI.getOperand(0).isReg() || !CopyMI.getOperand(1).isReg() ||
      !CopyMI.getOperand(1).isKill())
    return false;

  Register BinDst = BinMI.getOperand(0).getReg();
  Register LHSReg = BinMI.getOperand(1).getReg();
  Register RHSReg = BinMI.getOperand(2).getReg();
  bool LHSKill = BinMI.getOperand(1).isKill();
  bool RHSKill = BinMI.getOperand(2).isKill();
  Register CopyDst = CopyMI.getOperand(0).getReg();
  Register CopySrc = CopyMI.getOperand(1).getReg();
  if (!CopyDst.isPhysical() || !CopySrc.isPhysical() || CopyDst == CopySrc ||
      CopySrc != BinDst)
    return false;

  if (Is64) {
    if (CopyOpcode != Bedrock::MOVQrr)
      return false;
  } else if (CopyOpcode == Bedrock::MOVQrr) {
    if (!isI32ReturnValueCopy(MF, CopyI, MBB, CopyDst))
      return false;
  }

  Register OtherReg;
  bool OtherKill;
  if (CopyDst == LHSReg) {
    OtherReg = RHSReg;
    OtherKill = RHSKill;
  } else if (CopyDst == RHSReg) {
    OtherReg = LHSReg;
    OtherKill = LHSKill;
  } else {
    return false;
  }

  if (I != MBB.begin()) {
    auto DefI = std::prev(I);
    while (DefI != MBB.begin() && DefI->isDebugInstr())
      --DefI;
    if (!DefI->isDebugInstr() && isLoadOpcode(DefI->getOpcode()) &&
        DefI->getOperand(0).isReg() && DefI->getOperand(0).getReg() == CopyDst)
      return false;
  }

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding commutative copy: ";
             BinMI.dump(); CopyMI.dump());

  BinMI.getOperand(0).setReg(CopyDst);
  BinMI.getOperand(1).setReg(CopyDst);
  BinMI.getOperand(1).setIsKill(false);
  BinMI.getOperand(2).setReg(OtherReg);
  BinMI.getOperand(2).setIsKill(OtherKill);
  CopyMI.eraseFromParent();
  ++NumCommutativeCopiesFolded;
  return true;
}

static bool isSinkableImmediateOp(unsigned Opcode) {
  switch (Opcode) {
  case Bedrock::ADDL3ri:
  case Bedrock::ADDQ3ri:
  case Bedrock::SUBL3ri:
  case Bedrock::SUBQ3ri:
  case Bedrock::ANDL3ri:
  case Bedrock::ANDQ3ri:
  case Bedrock::ORL3ri:
  case Bedrock::ORQ3ri:
  case Bedrock::XORL3ri:
  case Bedrock::XORQ3ri:
  case Bedrock::SHLL3ri:
  case Bedrock::SHLQ3ri:
  case Bedrock::SHRL3ri:
  case Bedrock::SHRQ3ri:
  case Bedrock::SARL3ri:
  case Bedrock::SARQ3ri:
  case Bedrock::MINUL3ri:
  case Bedrock::MINUQ3ri:
  case Bedrock::MINSL3ri:
  case Bedrock::MINSQ3ri:
  case Bedrock::MAXUL3ri:
  case Bedrock::MAXUQ3ri:
  case Bedrock::MAXSL3ri:
  case Bedrock::MAXSQ3ri:
    return true;
  default:
    return false;
  }
}

bool BedrockPreEmitPeephole::foldSelectFalseImmediateOp(
    MachineFunction &MF, const TargetInstrInfo &TII) {
  for (MachineBasicBlock &MBB : MF) {
    if (MBB.empty())
      continue;

    auto BrI = MBB.getLastNonDebugInstr();
    if (BrI == MBB.end() || BrI->getOpcode() != Bedrock::BRCC)
      continue;
    MachineBasicBlock *SinkMBB = BrI->getOperand(0).getMBB();
    MachineBasicBlock *FalseMBB = nullptr;
    auto LayoutNext = std::next(MBB.getIterator());
    if (LayoutNext != MF.end() && &*LayoutNext != SinkMBB)
      FalseMBB = &*LayoutNext;
    if (!FalseMBB || FalseMBB->pred_size() != 1 || FalseMBB->succ_size() != 1 ||
        *FalseMBB->succ_begin() != SinkMBB)
      continue;

    auto FalseMoveI = FalseMBB->begin();
    while (FalseMoveI != FalseMBB->end() && FalseMoveI->isDebugInstr())
      ++FalseMoveI;
    if (FalseMoveI == FalseMBB->end() ||
        (FalseMoveI->getOpcode() != Bedrock::MOVQrr &&
         FalseMoveI->getOpcode() != Bedrock::MOVLrr) ||
        FalseMoveI->getNumExplicitOperands() != 2 ||
        !FalseMoveI->getOperand(0).isReg() || !FalseMoveI->getOperand(1).isReg())
      continue;
    auto AfterMoveI = std::next(FalseMoveI);
    while (AfterMoveI != FalseMBB->end() && AfterMoveI->isDebugInstr())
      ++AfterMoveI;
    if (AfterMoveI != FalseMBB->end())
      continue;

    Register DstReg = FalseMoveI->getOperand(0).getReg();
    Register FalseReg = FalseMoveI->getOperand(1).getReg();
    if (!FalseReg.isPhysical() || !DstReg.isPhysical() ||
        !FalseMoveI->getOperand(1).isKill())
      continue;

    auto CmpI = BrI;
    bool MissingCmp = false;
    do {
      if (CmpI == MBB.begin()) {
        MissingCmp = true;
        break;
      }
      --CmpI;
    } while (CmpI->isDebugInstr());
    if (MissingCmp)
      continue;
    if (!CmpI->getDesc().isCompare())
      continue;

    MachineBasicBlock::iterator FalseOpI = MBB.end();
    for (auto ScanI = CmpI; ScanI != MBB.begin();) {
      --ScanI;
      if (ScanI->isDebugInstr())
        continue;
      if (definesReg(*ScanI, FalseReg)) {
        FalseOpI = ScanI;
        break;
      }
      if (usesReg(*ScanI, FalseReg) || ScanI->isCall() ||
          ScanI->isTerminator())
        break;
    }
    if (FalseOpI == MBB.end() ||
        !isSinkableImmediateOp(FalseOpI->getOpcode()) ||
        FalseOpI->getNumExplicitOperands() != 3 ||
        !FalseOpI->getOperand(0).isReg() || !FalseOpI->getOperand(1).isReg() ||
        !FalseOpI->getOperand(2).isImm() ||
        FalseOpI->getOperand(0).getReg() != FalseReg ||
        FalseOpI->getOperand(1).getReg() != FalseReg)
      continue;

    auto FalseCopyI = FalseOpI;
    bool MissingFalseCopy = false;
    do {
      if (FalseCopyI == MBB.begin()) {
        MissingFalseCopy = true;
        break;
      }
      --FalseCopyI;
    } while (FalseCopyI->isDebugInstr());
    if (MissingFalseCopy)
      continue;
    if ((FalseCopyI->getOpcode() != Bedrock::MOVQrr &&
         FalseCopyI->getOpcode() != Bedrock::MOVLrr) ||
        FalseCopyI->getNumExplicitOperands() != 2 ||
        !FalseCopyI->getOperand(0).isReg() ||
        !FalseCopyI->getOperand(1).isReg() ||
        FalseCopyI->getOperand(0).getReg() != FalseReg)
      continue;

    Register BaseReg = FalseCopyI->getOperand(1).getReg();
    if (!BaseReg.isPhysical())
      continue;

    bool CmpUsesBase = false;
    for (MachineOperand &MO : CmpI->explicit_operands()) {
      if (MO.isReg() && !MO.isDef() && MO.getReg() == BaseReg) {
        MO.setIsKill(false);
        CmpUsesBase = true;
      }
    }
    if (!CmpUsesBase)
      continue;

    bool HasInterference = false;
    for (auto ScanI = std::next(FalseOpI); ScanI != CmpI; ++ScanI) {
      if (ScanI->isDebugInstr())
        continue;
      if (usesReg(*ScanI, FalseReg) || definesReg(*ScanI, BaseReg)) {
        HasInterference = true;
        break;
      }
    }
    if (HasInterference)
      continue;

    DebugLoc DL = FalseOpI->getDebugLoc();
    BuildMI(*FalseMBB, FalseMoveI, DL, TII.get(FalseMoveI->getOpcode()), DstReg)
        .addReg(BaseReg);
    BuildMI(*FalseMBB, FalseMoveI, DL, TII.get(FalseOpI->getOpcode()), DstReg)
        .addReg(DstReg)
        .addImm(FalseOpI->getOperand(2).getImm());

    FalseMBB->removeLiveIn(FalseReg.asMCReg());
    FalseMBB->addLiveIn(BaseReg.asMCReg());
    FalseMoveI->eraseFromParent();
    FalseOpI->eraseFromParent();
    FalseCopyI->eraseFromParent();
    ++NumSelectFalseOpsSunk;
    return true;
  }

  return false;
}

bool BedrockPreEmitPeephole::foldHexDigitSelect(MachineFunction &MF,
                                                const TargetInstrInfo &TII) {
  for (MachineBasicBlock &MBB : MF) {
    if (MBB.empty())
      continue;

    auto BrI = MBB.getLastNonDebugInstr();
    if (BrI == MBB.end() || BrI->getOpcode() != Bedrock::BRCC ||
        BrI->getOperand(1).getImm() != 4)
      continue;
    MachineBasicBlock *MergeMBB = BrI->getOperand(0).getMBB();
    MachineBasicBlock *FalseMBB = nullptr;
    auto LayoutNext = std::next(MBB.getIterator());
    if (LayoutNext != MF.end() && &*LayoutNext != MergeMBB)
      FalseMBB = &*LayoutNext;
    if (!FalseMBB || FalseMBB->pred_size() != 1 ||
        FalseMBB->succ_size() != 1 || *FalseMBB->succ_begin() != MergeMBB)
      continue;

    auto CmpI = BrI;
    bool Missing = false;
    do {
      if (CmpI == MBB.begin()) {
        Missing = true;
        break;
      }
      --CmpI;
    } while (CmpI->isDebugInstr());
    if (Missing || CmpI->getOpcode() != Bedrock::CMPLri ||
        CmpI->getNumExplicitOperands() != 2 || !CmpI->getOperand(0).isReg() ||
        !CmpI->getOperand(1).isImm() || CmpI->getOperand(1).getImm() != 10)
      continue;

    auto OrI = CmpI;
    do {
      if (OrI == MBB.begin()) {
        Missing = true;
        break;
      }
      --OrI;
    } while (OrI->isDebugInstr());
    if (Missing || OrI->getOpcode() != Bedrock::ORL3ri ||
        OrI->getNumExplicitOperands() != 3 || !OrI->getOperand(0).isReg() ||
        !OrI->getOperand(1).isReg() || !OrI->getOperand(2).isImm() ||
        OrI->getOperand(2).getImm() != 48)
      continue;

    auto ResultCopyI = OrI;
    do {
      if (ResultCopyI == MBB.begin()) {
        Missing = true;
        break;
      }
      --ResultCopyI;
    } while (ResultCopyI->isDebugInstr());
    if (Missing || ResultCopyI->getOpcode() != Bedrock::MOVQrr ||
        ResultCopyI->getNumExplicitOperands() != 2 ||
        !ResultCopyI->getOperand(0).isReg() ||
        !ResultCopyI->getOperand(1).isReg())
      continue;

    auto AndI = ResultCopyI;
    do {
      if (AndI == MBB.begin()) {
        Missing = true;
        break;
      }
      --AndI;
    } while (AndI->isDebugInstr());
    if (Missing || AndI->getOpcode() != Bedrock::ANDL3ri ||
        AndI->getNumExplicitOperands() != 3 || !AndI->getOperand(0).isReg() ||
        !AndI->getOperand(1).isReg() || !AndI->getOperand(2).isImm() ||
        AndI->getOperand(2).getImm() != 15)
      continue;

    auto InputCopyI = AndI;
    do {
      if (InputCopyI == MBB.begin()) {
        Missing = true;
        break;
      }
      --InputCopyI;
    } while (InputCopyI->isDebugInstr());
    if (Missing || InputCopyI->getOpcode() != Bedrock::MOVQrr ||
        InputCopyI->getNumExplicitOperands() != 2 ||
        !InputCopyI->getOperand(0).isReg() ||
        !InputCopyI->getOperand(1).isReg())
      continue;

    Register ValueReg = AndI->getOperand(0).getReg();
    Register InputReg = InputCopyI->getOperand(1).getReg();
    Register ResultReg = OrI->getOperand(0).getReg();
    if (InputCopyI->getOperand(0).getReg() != ValueReg ||
        AndI->getOperand(1).getReg() != ValueReg ||
        ResultCopyI->getOperand(0).getReg() != ResultReg ||
        ResultCopyI->getOperand(1).getReg() != ValueReg ||
        OrI->getOperand(1).getReg() != ResultReg ||
        CmpI->getOperand(0).getReg() != ValueReg || ResultReg != InputReg)
      continue;

    auto FalseMoveI = FalseMBB->begin();
    while (FalseMoveI != FalseMBB->end() && FalseMoveI->isDebugInstr())
      ++FalseMoveI;
    if (FalseMoveI == FalseMBB->end() ||
        FalseMoveI->getOpcode() != Bedrock::MOVQrr ||
        FalseMoveI->getNumExplicitOperands() != 2 ||
        !FalseMoveI->getOperand(0).isReg() ||
        !FalseMoveI->getOperand(1).isReg() ||
        FalseMoveI->getOperand(0).getReg() != ResultReg ||
        FalseMoveI->getOperand(1).getReg() != ValueReg)
      continue;

    auto FalseAddI = std::next(FalseMoveI);
    while (FalseAddI != FalseMBB->end() && FalseAddI->isDebugInstr())
      ++FalseAddI;
    if (FalseAddI == FalseMBB->end() ||
        FalseAddI->getOpcode() != Bedrock::ADDL3ri ||
        FalseAddI->getNumExplicitOperands() != 3 ||
        !FalseAddI->getOperand(0).isReg() ||
        !FalseAddI->getOperand(1).isReg() ||
        !FalseAddI->getOperand(2).isImm() ||
        FalseAddI->getOperand(0).getReg() != ResultReg ||
        FalseAddI->getOperand(1).getReg() != ResultReg ||
        FalseAddI->getOperand(2).getImm() != 55)
      continue;
    auto FalseEndI = std::next(FalseAddI);
    while (FalseEndI != FalseMBB->end() && FalseEndI->isDebugInstr())
      ++FalseEndI;
    if (FalseEndI != FalseMBB->end())
      continue;

    InputCopyI->setDesc(TII.get(Bedrock::ANDL3ri));
    InputCopyI->getOperand(0).setReg(InputReg);
    InputCopyI->getOperand(1).setReg(InputReg);
    InputCopyI->getOperand(1).setIsKill(true);
    InputCopyI->addOperand(MachineOperand::CreateImm(15));
    InputCopyI->tieOperands(0, 1);

    OrI->getOperand(1).setIsKill(true);
    CmpI->getOperand(0).setReg(ResultReg);
    CmpI->getOperand(0).setIsKill(false);
    CmpI->getOperand(1).setImm(58);
    FalseAddI->getOperand(2).setImm(7);

    FalseMBB->removeLiveIn(ValueReg.asMCReg());
    FalseMBB->addLiveIn(ResultReg.asMCReg());
    AndI->eraseFromParent();
    ResultCopyI->eraseFromParent();
    FalseMoveI->eraseFromParent();
    ++NumHexDigitSelectsFolded;
    return true;
  }

  return false;
}

struct HexDigitPostExtendSelect {
  MachineBasicBlock *MBB = nullptr;
  MachineBasicBlock *FalseMBB = nullptr;
  MachineBasicBlock *MergeMBB = nullptr;
  MachineBasicBlock::iterator AndI;
  MachineBasicBlock::iterator ResultCopyI;
  MachineBasicBlock::iterator OrI;
  MachineBasicBlock::iterator ConstI;
  MachineBasicBlock::iterator CmpI;
  MachineBasicBlock::iterator BrI;
  MachineBasicBlock::iterator FalseMoveI;
  MachineBasicBlock::iterator FalseAddI;
  MachineBasicBlock::iterator ExtendI;
  Register ValueReg;
  Register ResultReg;
  Register ThresholdReg;
  bool HasConst = false;
};

static MachineBasicBlock::iterator
prevNonDebug(MachineBasicBlock::iterator I, MachineBasicBlock &MBB,
             bool &Missing) {
  do {
    if (I == MBB.begin()) {
      Missing = true;
      return I;
    }
    --I;
  } while (I->isDebugInstr());
  return I;
}

static MachineBasicBlock::iterator
nextNonDebug(MachineBasicBlock::iterator I, MachineBasicBlock &MBB) {
  while (I != MBB.end() && I->isDebugInstr())
    ++I;
  return I;
}

static bool isConstTenDef(const MachineInstr &MI, Register Reg) {
  return (MI.getOpcode() == Bedrock::CONST32 ||
          MI.getOpcode() == Bedrock::CONST64) &&
         MI.getNumExplicitOperands() == 2 && MI.getOperand(0).isReg() &&
         MI.getOperand(0).getReg() == Reg && MI.getOperand(1).isImm() &&
         MI.getOperand(1).getImm() == 10;
}

static bool hasRegAccessOtherThan(MachineBasicBlock::iterator Begin,
                                  MachineBasicBlock::iterator End,
                                  Register Reg,
                                  const MachineInstr *AllowedA,
                                  const MachineInstr *AllowedB = nullptr) {
  for (auto I = Begin; I != End; ++I) {
    if (I->isDebugInstr())
      continue;
    for (const MachineOperand &MO : I->operands()) {
      if (!MO.isReg() || MO.getReg() != Reg)
        continue;
      if (&*I == AllowedA || &*I == AllowedB)
        continue;
      return true;
    }
  }
  return false;
}

static bool matchHexDigitPostExtendSelect(MachineBasicBlock &MBB,
                                          HexDigitPostExtendSelect &Sel) {
  if (MBB.empty())
    return false;

  auto BrI = MBB.getLastNonDebugInstr();
  if (BrI == MBB.end() || BrI->getOpcode() != Bedrock::BRCC ||
      BrI->getOperand(1).getImm() != 4)
    return false;

  MachineFunction *MF = MBB.getParent();
  MachineBasicBlock *MergeMBB = BrI->getOperand(0).getMBB();
  MachineBasicBlock *FalseMBB = nullptr;
  auto LayoutNext = std::next(MBB.getIterator());
  if (LayoutNext != MF->end() && &*LayoutNext != MergeMBB)
    FalseMBB = &*LayoutNext;
  if (!FalseMBB || FalseMBB->pred_size() != 1 ||
      FalseMBB->succ_size() != 1 || *FalseMBB->succ_begin() != MergeMBB)
    return false;

  bool Missing = false;
  auto CmpI = prevNonDebug(BrI, MBB, Missing);
  if (Missing || CmpI->getOpcode() != Bedrock::CMPLrr ||
      CmpI->getNumExplicitOperands() != 2 || !CmpI->getOperand(0).isReg() ||
      !CmpI->getOperand(1).isReg())
    return false;

  Register ThresholdReg = CmpI->getOperand(0).getReg();
  Register ValueReg = CmpI->getOperand(1).getReg();

  auto OrI = prevNonDebug(CmpI, MBB, Missing);
  if (Missing)
    return false;

  auto ConstI = OrI;
  bool HasConst = false;
  if (isConstTenDef(*ConstI, ThresholdReg)) {
    HasConst = true;
    OrI = prevNonDebug(ConstI, MBB, Missing);
    if (Missing)
      return false;
  }

  if (OrI->getOpcode() != Bedrock::ORL3ri ||
      OrI->getNumExplicitOperands() != 3 || !OrI->getOperand(0).isReg() ||
      !OrI->getOperand(1).isReg() || !OrI->getOperand(2).isImm() ||
      OrI->getOperand(2).getImm() != 48)
    return false;
  Register ResultReg = OrI->getOperand(0).getReg();
  if (OrI->getOperand(1).getReg() != ResultReg || ResultReg == ValueReg)
    return false;

  auto ResultCopyI = prevNonDebug(OrI, MBB, Missing);
  if (Missing || ResultCopyI->getOpcode() != Bedrock::MOVQrr ||
      ResultCopyI->getNumExplicitOperands() != 2 ||
      !ResultCopyI->getOperand(0).isReg() ||
      !ResultCopyI->getOperand(1).isReg() ||
      ResultCopyI->getOperand(0).getReg() != ResultReg ||
      ResultCopyI->getOperand(1).getReg() != ValueReg)
    return false;

  auto AndI = prevNonDebug(ResultCopyI, MBB, Missing);
  if (Missing || AndI->getOpcode() != Bedrock::ANDL3ri ||
      AndI->getNumExplicitOperands() != 3 || !AndI->getOperand(0).isReg() ||
      !AndI->getOperand(1).isReg() || !AndI->getOperand(2).isImm() ||
      AndI->getOperand(0).getReg() != ValueReg ||
      AndI->getOperand(1).getReg() != ValueReg ||
      AndI->getOperand(2).getImm() != 15)
    return false;

  auto FalseMoveI = nextNonDebug(FalseMBB->begin(), *FalseMBB);
  if (FalseMoveI == FalseMBB->end() ||
      FalseMoveI->getOpcode() != Bedrock::MOVQrr ||
      FalseMoveI->getNumExplicitOperands() != 2 ||
      !FalseMoveI->getOperand(0).isReg() ||
      !FalseMoveI->getOperand(1).isReg() ||
      FalseMoveI->getOperand(0).getReg() != ResultReg ||
      FalseMoveI->getOperand(1).getReg() != ValueReg)
    return false;

  auto FalseAddI = nextNonDebug(std::next(FalseMoveI), *FalseMBB);
  if (FalseAddI == FalseMBB->end() ||
      FalseAddI->getOpcode() != Bedrock::ADDL3ri ||
      FalseAddI->getNumExplicitOperands() != 3 ||
      !FalseAddI->getOperand(0).isReg() ||
      !FalseAddI->getOperand(1).isReg() ||
      !FalseAddI->getOperand(2).isImm() ||
      FalseAddI->getOperand(0).getReg() != ResultReg ||
      FalseAddI->getOperand(1).getReg() != ResultReg ||
      FalseAddI->getOperand(2).getImm() != 55)
    return false;

  auto FalseEndI = nextNonDebug(std::next(FalseAddI), *FalseMBB);
  if (FalseEndI != FalseMBB->end())
    return false;

  auto ExtendI = nextNonDebug(MergeMBB->begin(), *MergeMBB);
  if (ExtendI == MergeMBB->end() ||
      ExtendI->getOpcode() != Bedrock::EXTZQLrr ||
      ExtendI->getNumExplicitOperands() != 2 ||
      !ExtendI->getOperand(0).isReg() || !ExtendI->getOperand(1).isReg() ||
      ExtendI->getOperand(1).getReg() != ResultReg)
    return false;

  Sel.MBB = &MBB;
  Sel.FalseMBB = FalseMBB;
  Sel.MergeMBB = MergeMBB;
  Sel.AndI = AndI;
  Sel.ResultCopyI = ResultCopyI;
  Sel.OrI = OrI;
  Sel.ConstI = ConstI;
  Sel.CmpI = CmpI;
  Sel.BrI = BrI;
  Sel.FalseMoveI = FalseMoveI;
  Sel.FalseAddI = FalseAddI;
  Sel.ExtendI = ExtendI;
  Sel.ValueReg = ValueReg;
  Sel.ResultReg = ResultReg;
  Sel.ThresholdReg = ThresholdReg;
  Sel.HasConst = HasConst;
  return true;
}

static void rewriteHexDigitPostExtendSelect(HexDigitPostExtendSelect &Sel) {
  Sel.OrI->getOperand(0).setReg(Sel.ValueReg);
  Sel.OrI->getOperand(1).setReg(Sel.ValueReg);
  Sel.OrI->getOperand(1).setIsKill(true);

  Sel.CmpI->getOperand(1).setReg(Sel.ValueReg);
  Sel.CmpI->getOperand(1).setIsKill(false);

  Sel.FalseAddI->getOperand(0).setReg(Sel.ValueReg);
  Sel.FalseAddI->getOperand(1).setReg(Sel.ValueReg);
  Sel.FalseAddI->getOperand(2).setImm(7);

  Sel.ExtendI->getOperand(1).setReg(Sel.ValueReg);
  Sel.ExtendI->getOperand(1).setIsKill(true);

  Sel.MergeMBB->removeLiveIn(Sel.ResultReg.asMCReg());
  Sel.MergeMBB->addLiveIn(Sel.ValueReg.asMCReg());
  Sel.FalseMoveI->eraseFromParent();
  Sel.ResultCopyI->eraseFromParent();
}

bool BedrockPreEmitPeephole::foldHexDigitPostExtendPair(
    MachineFunction &MF, const TargetInstrInfo &TII) {
  (void)TII;
  for (MachineBasicBlock &MBB : MF) {
    HexDigitPostExtendSelect First;
    if (!matchHexDigitPostExtendSelect(MBB, First) || !First.HasConst)
      continue;

    HexDigitPostExtendSelect Second;
    if (!matchHexDigitPostExtendSelect(*First.MergeMBB, Second) ||
        Second.HasConst || Second.ThresholdReg != First.ThresholdReg ||
        !Second.CmpI->getOperand(0).isKill())
      continue;

    auto AfterConstI = std::next(First.ConstI);
    if (hasRegAccessOtherThan(AfterConstI, First.MBB->end(),
                              First.ThresholdReg, &*First.CmpI) ||
        hasRegAccessOtherThan(First.FalseMBB->begin(), First.FalseMBB->end(),
                              First.ThresholdReg, nullptr) ||
        hasRegAccessOtherThan(First.MergeMBB->begin(), std::next(Second.CmpI),
                              First.ThresholdReg, &*Second.CmpI) ||
        hasRegAccessOtherThan(Second.FalseMBB->begin(), Second.FalseMBB->end(),
                              First.ThresholdReg, nullptr))
      continue;

    First.ConstI->getOperand(1).setImm(58);
    rewriteHexDigitPostExtendSelect(First);
    rewriteHexDigitPostExtendSelect(Second);
    ++NumHexDigitPostExtendsFolded;
    return true;
  }

  return false;
}

static int getPushPairIndex(Register FirstReg, Register SecondReg) {
  if (FirstReg == Bedrock::R8 && SecondReg == Bedrock::R9)
    return 3;
  if (FirstReg == Bedrock::R10 && SecondReg == Bedrock::R11)
    return 2;
  if (FirstReg == Bedrock::R12 && SecondReg == Bedrock::R13)
    return 1;
  return -1;
}

static int getPopPairIndex(Register FirstReg, Register SecondReg) {
  if (FirstReg == Bedrock::R13 && SecondReg == Bedrock::R12)
    return 1;
  if (FirstReg == Bedrock::R11 && SecondReg == Bedrock::R10)
    return 2;
  if (FirstReg == Bedrock::R9 && SecondReg == Bedrock::R8)
    return 3;
  return -1;
}

static int getPaddingPairIndex(Register Reg) {
  if (Reg == Bedrock::R14)
    return 0;
  if (Reg == Bedrock::R8)
    return 3;
  if (Reg == Bedrock::R10)
    return 2;
  if (Reg == Bedrock::R12)
    return 1;
  return -1;
}

static Register getPaddingPairMate(Register Reg) {
  if (Reg == Bedrock::R14)
    return Bedrock::R15;
  if (Reg == Bedrock::R8)
    return Bedrock::R9;
  if (Reg == Bedrock::R10)
    return Bedrock::R11;
  if (Reg == Bedrock::R12)
    return Bedrock::R13;
  return Register();
}

static bool getCalleeSavedPairRegs(unsigned PairIndex, Register &First,
                                   Register &Second) {
  switch (PairIndex) {
  case 0:
    First = Bedrock::R14;
    Second = Bedrock::R15;
    return true;
  case 1:
    First = Bedrock::R12;
    Second = Bedrock::R13;
    return true;
  case 2:
    First = Bedrock::R10;
    Second = Bedrock::R11;
    return true;
  case 3:
    First = Bedrock::R8;
    Second = Bedrock::R9;
    return true;
  default:
    return false;
  }
}

static bool hasExplicitTouchOfAnyReg(const MachineFunction &MF, Register Reg0,
                                     Register Reg1,
                                     unsigned IgnoredPairIndex) {
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      if ((MI.getOpcode() == Bedrock::PUSHPi ||
           MI.getOpcode() == Bedrock::POPPi) &&
          MI.getNumExplicitOperands() == 1 && MI.getOperand(0).isImm() &&
          MI.getOperand(0).getImm() == static_cast<int64_t>(IgnoredPairIndex))
        continue;
      if (any_of(MI.explicit_operands(), [Reg0, Reg1](const MachineOperand &MO) {
            return MO.isReg() && (MO.getReg() == Reg0 || MO.getReg() == Reg1);
          }))
        return true;
    }
  }
  return false;
}

static bool isIgnoredSingleSaveRestore(const MachineInstr &MI, Register Reg) {
  return (MI.getOpcode() == Bedrock::PUSHr ||
          MI.getOpcode() == Bedrock::POPr) &&
         MI.getNumExplicitOperands() == 1 && MI.getOperand(0).isReg() &&
         MI.getOperand(0).getReg() == Reg;
}

bool BedrockPreEmitPeephole::foldDeadCalleeSavedPairs(MachineFunction &MF) {
  for (unsigned PairIndex = 0; PairIndex != 4; ++PairIndex) {
    Register First;
    Register Second;
    if (!getCalleeSavedPairRegs(PairIndex, First, Second) ||
        hasExplicitTouchOfAnyReg(MF, First, Second, PairIndex))
      continue;

    bool Removed = false;
    for (MachineBasicBlock &MBB : MF) {
      for (auto I = MBB.begin(); I != MBB.end();) {
        if ((I->getOpcode() == Bedrock::PUSHPi ||
             I->getOpcode() == Bedrock::POPPi) &&
            I->getNumExplicitOperands() == 1 && I->getOperand(0).isImm() &&
            I->getOperand(0).getImm() == static_cast<int64_t>(PairIndex)) {
          auto NextI = std::next(I);
          I->eraseFromParent();
          I = NextI;
          Removed = true;
          continue;
        }
        ++I;
      }
      MBB.removeLiveIn(First.asMCReg());
      MBB.removeLiveIn(Second.asMCReg());
    }

    if (Removed)
      return true;
  }

  for (Register Reg : {Bedrock::R8, Bedrock::R9, Bedrock::R10, Bedrock::R11,
                       Bedrock::R12, Bedrock::R13, Bedrock::R14}) {
    bool HasExplicitTouch = false;
    for (const MachineBasicBlock &MBB : MF) {
      for (const MachineInstr &MI : MBB) {
        if (MI.isDebugInstr() || isIgnoredSingleSaveRestore(MI, Reg))
          continue;
        if (any_of(MI.explicit_operands(), [Reg](const MachineOperand &MO) {
              return MO.isReg() && MO.getReg() == Reg;
            })) {
          HasExplicitTouch = true;
          break;
        }
      }
      if (HasExplicitTouch)
        break;
    }
    if (HasExplicitTouch)
      continue;

    bool Removed = false;
    for (MachineBasicBlock &MBB : MF) {
      for (auto I = MBB.begin(); I != MBB.end();) {
        if (isIgnoredSingleSaveRestore(*I, Reg)) {
          auto NextI = std::next(I);
          I->eraseFromParent();
          I = NextI;
          Removed = true;
          continue;
        }
        ++I;
      }
      MBB.removeLiveIn(Reg.asMCReg());
    }
    if (Removed)
      return true;
  }

  return false;
}

static bool hasSavedPair(const MachineFunction &MF, unsigned PairIndex) {
  bool HasPush = false;
  bool HasPop = false;
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (MI.getNumExplicitOperands() != 1 || !MI.getOperand(0).isImm() ||
          MI.getOperand(0).getImm() != static_cast<int64_t>(PairIndex))
        continue;
      HasPush |= MI.getOpcode() == Bedrock::PUSHPi;
      HasPop |= MI.getOpcode() == Bedrock::POPPi;
    }
  }
  return HasPush && HasPop;
}

static bool hasSavedReg(const MachineFunction &MF, Register Reg) {
  bool HasPush = false;
  bool HasPop = false;
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (!isIgnoredSingleSaveRestore(MI, Reg))
        continue;
      HasPush |= MI.getOpcode() == Bedrock::PUSHr;
      HasPop |= MI.getOpcode() == Bedrock::POPr;
    }
  }
  return HasPush && HasPop;
}

static MachineBasicBlock::iterator firstNonDebugMI(MachineBasicBlock &MBB) {
  auto I = MBB.begin();
  while (I != MBB.end() && I->isDebugInstr())
    ++I;
  return I;
}

static MachineBasicBlock::iterator
previousNonDebugMI(MachineBasicBlock::iterator I, MachineBasicBlock &MBB) {
  while (I != MBB.begin()) {
    --I;
    if (!I->isDebugInstr())
      return I;
  }
  return MBB.end();
}

static const MachineBasicBlock *layoutSuccessor(const MachineBasicBlock &MBB) {
  const MachineFunction &MF = *MBB.getParent();
  auto I = std::next(MBB.getIterator());
  return I == MF.end() ? nullptr : &*I;
}

static bool isCounterDecrement(const MachineInstr &MI, Register CounterReg) {
  return MI.getOpcode() == Bedrock::DECQ3r &&
         MI.getNumExplicitOperands() >= 2 && MI.getOperand(0).isReg() &&
         MI.getOperand(1).isReg() && MI.getOperand(0).getReg() == CounterReg &&
         MI.getOperand(1).getReg() == CounterReg;
}

static bool instructionTouchesReg(const MachineInstr &MI, Register Reg) {
  return any_of(MI.operands(), [Reg](const MachineOperand &MO) {
    return MO.isReg() && MO.getReg() == Reg;
  });
}

static bool isIgnoredPairSaveRestore(const MachineInstr &MI,
                                     unsigned PairIndex) {
  return (MI.getOpcode() == Bedrock::PUSHPi ||
          MI.getOpcode() == Bedrock::POPPi) &&
         MI.getNumExplicitOperands() == 1 && MI.getOperand(0).isImm() &&
         MI.getOperand(0).getImm() == static_cast<int64_t>(PairIndex);
}

static bool tryReuseRepgCounterInBody(MachineFunction &MF,
                                      MachineBasicBlock &Body,
                                      MachineInstr &BodyStart,
                                      MachineInstr &DecMI, Register CounterReg,
                                      const TargetInstrInfo &TII) {
  SmallPtrSet<const MachineInstr *, 32> BodyRange;
  bool ReachedDec = false;
  for (auto I = BodyStart.getIterator(); I != Body.end(); ++I) {
    if (&*I == &DecMI) {
      ReachedDec = true;
      break;
    }
    if (I->isDebugInstr())
      continue;
    if (I->isCall() || I->isTerminator() || I->isBranch() || I->isReturn() ||
        I->isMetaInstruction() || instructionTouchesReg(*I, CounterReg))
      return false;
    BodyRange.insert(&*I);
  }
  if (!ReachedDec || BodyRange.empty())
    return false;

  for (unsigned PairIndex = 0; PairIndex != 4; ++PairIndex) {
    Register First;
    Register Second;
    if (!getCalleeSavedPairRegs(PairIndex, First, Second))
      continue;

    bool SavedPair = hasSavedPair(MF, PairIndex);

    for (Register ScratchReg : {First, Second}) {
      Register MateReg = ScratchReg == First ? Second : First;
      bool SavedSingle = hasSavedReg(MF, ScratchReg);
      if ((!SavedPair && !SavedSingle) ||
          Body.isLiveIn(ScratchReg.asMCReg()) ||
          (SavedPair &&
           hasExplicitTouchOfAnyReg(MF, MateReg, MateReg, PairIndex)))
        continue;

      bool TouchedOutside = false;
      for (const MachineBasicBlock &MBB : MF) {
        for (const MachineInstr &MI : MBB) {
          if (MI.isDebugInstr() || BodyRange.contains(&MI) ||
              (SavedPair && isIgnoredPairSaveRestore(MI, PairIndex)) ||
              (SavedSingle &&
               isIgnoredSingleSaveRestore(MI, ScratchReg)))
            continue;
          if (instructionTouchesReg(MI, ScratchReg)) {
            TouchedOutside = true;
            break;
          }
        }
        if (TouchedOutside)
          break;
      }
      if (TouchedOutside)
        continue;

      bool SawScratch = false;
      bool Defined = false;
      bool Invalid = false;
      for (auto I = BodyStart.getIterator(); &*I != &DecMI; ++I) {
        if (I->isDebugInstr())
          continue;
        bool Reads =
            any_of(I->operands(), [ScratchReg](const MachineOperand &MO) {
              return MO.isReg() && !MO.isDef() && MO.getReg() == ScratchReg;
            });
        bool Defines =
            any_of(I->operands(), [ScratchReg](const MachineOperand &MO) {
              return MO.isReg() && MO.isDef() && MO.getReg() == ScratchReg;
            });
        if (Reads && !Defined) {
          Invalid = true;
          break;
        }
        SawScratch |= Reads || Defines;
        Defined |= Defines;
      }
      if (Invalid || !SawScratch || !Defined)
        continue;

      BuildMI(Body, BodyStart.getIterator(), BodyStart.getDebugLoc(),
              TII.get(Bedrock::REPG_SCRATCH))
          .addReg(CounterReg);
      for (auto I = BodyStart.getIterator(); &*I != &DecMI; ++I) {
        for (MachineOperand &MO : I->operands()) {
          if (MO.isReg() && MO.getReg() == ScratchReg)
            MO.setReg(CounterReg);
        }
      }
      // The repeat engine restores the captured next-count value at the group
      // boundary.  Keeping the physical register live through the removed
      // scalar decrement models that boundary for the machine verifier.
      MF.getRegInfo().clearKillFlags(CounterReg);

      ++NumRepgCounterScratchRegsFolded;
      return true;
    }
  }

  return false;
}

bool BedrockPreEmitPeephole::foldRepgCounterScratch(
    MachineFunction &MF, const TargetInstrInfo &TII) {
  // Header-loop form: TEST counter; JE exit; body; DEC counter; JMP header.
  for (MachineBasicBlock &Header : MF) {
    auto TestI = firstNonDebugMI(Header);
    if (TestI == Header.end() || TestI->getOpcode() != Bedrock::TESTQrr ||
        TestI->getNumExplicitOperands() < 2 || !TestI->getOperand(0).isReg() ||
        !TestI->getOperand(1).isReg() ||
        TestI->getOperand(0).getReg() != TestI->getOperand(1).getReg())
      continue;
    Register CounterReg = TestI->getOperand(0).getReg();

    auto BranchI = std::next(TestI);
    while (BranchI != Header.end() && BranchI->isDebugInstr())
      ++BranchI;
    if (BranchI == Header.end() || BranchI->getOpcode() != Bedrock::BRCC ||
        BranchI->getOperand(1).getImm() != 0x2)
      continue;
    auto AfterBranch = std::next(BranchI);
    while (AfterBranch != Header.end() && AfterBranch->isDebugInstr())
      ++AfterBranch;
    if (AfterBranch != Header.end())
      continue;

    MachineBasicBlock *Body = Header.getNextNode();
    MachineBasicBlock *Exit = BranchI->getOperand(0).getMBB();
    if (!Body || Body == Exit || !Header.isSuccessor(Body) ||
        !Header.isSuccessor(Exit) || layoutSuccessor(*Body) != Exit ||
        Body->pred_size() != 1 || *Body->pred_begin() != &Header)
      continue;
    auto BodyBrI = Body->getLastNonDebugInstr();
    if (BodyBrI == Body->end() || BodyBrI->getOpcode() != Bedrock::BR ||
        BodyBrI->getOperand(0).getMBB() != &Header)
      continue;

    auto BodyStartI = firstNonDebugMI(*Body);
    for (auto I = BodyStartI; I != BodyBrI; ++I) {
      if (I->isDebugInstr())
        continue;
      if (isCounterDecrement(*I, CounterReg) &&
          tryReuseRepgCounterInBody(MF, *Body, *BodyStartI, *I, CounterReg,
                                    TII)) {
        while (foldDeadCalleeSavedPairs(MF)) {
        }
        return true;
      }
      if (instructionTouchesReg(*I, CounterReg))
        break;
    }
  }

  // Guarded self-loop form.  Requiring the canonical guard and one-instruction
  // preheader keeps the transformation coupled to the assembly printer's REPG
  // recognizer; if that recognizer cannot remove the scalar loop, this form is
  // not changed.
  for (MachineBasicBlock &Body : MF) {
    auto BranchI = Body.getLastNonDebugInstr();
    if (BranchI == Body.end() || BranchI->getOpcode() != Bedrock::BRCC ||
        BranchI->getOperand(0).getMBB() != &Body ||
        BranchI->getOperand(1).getImm() != 0x3)
      continue;
    const MachineBasicBlock *Exit = layoutSuccessor(Body);
    if (!Exit || !Body.isSuccessor(Exit))
      continue;
    auto TestI = previousNonDebugMI(BranchI, Body);
    auto DecI =
        TestI == Body.end() ? Body.end() : previousNonDebugMI(TestI, Body);
    if (TestI == Body.end() || DecI == Body.end() ||
        TestI->getOpcode() != Bedrock::TESTQrr ||
        TestI->getNumExplicitOperands() < 2 || !TestI->getOperand(0).isReg() ||
        !TestI->getOperand(1).isReg() ||
        TestI->getOperand(0).getReg() != TestI->getOperand(1).getReg())
      continue;
    Register CounterReg = TestI->getOperand(0).getReg();
    if (!isCounterDecrement(*DecI, CounterReg))
      continue;

    MachineBasicBlock *Preheader = nullptr;
    for (MachineBasicBlock *Pred : Body.predecessors()) {
      if (Pred == &Body)
        continue;
      if (Preheader) {
        Preheader = nullptr;
        break;
      }
      Preheader = Pred;
    }
    if (!Preheader || Preheader->succ_size() != 1 ||
        !Preheader->isSuccessor(&Body))
      continue;
    auto CounterDefI = firstNonDebugMI(*Preheader);
    if (CounterDefI == Preheader->end() ||
        (CounterDefI->getOpcode() != Bedrock::EXTZQLrr &&
         CounterDefI->getOpcode() != Bedrock::MOVQrr) ||
        CounterDefI->getNumExplicitOperands() < 2 ||
        !CounterDefI->getOperand(0).isReg() ||
        !CounterDefI->getOperand(1).isReg() ||
        CounterDefI->getOperand(0).getReg() != CounterReg)
      continue;
    auto AfterCounterDef = std::next(CounterDefI);
    while (AfterCounterDef != Preheader->end() &&
           AfterCounterDef->isDebugInstr())
      ++AfterCounterDef;
    if (AfterCounterDef != Preheader->end())
      continue;

    Register SourceReg = CounterDefI->getOperand(1).getReg();
    if (Preheader->pred_size() != 1)
      continue;
    MachineBasicBlock *Guard = *Preheader->pred_begin();
    if (layoutSuccessor(*Guard) != Preheader)
      continue;
    auto GuardBrI = Guard->getLastNonDebugInstr();
    auto GuardTestI = GuardBrI == Guard->end()
                          ? Guard->end()
                          : previousNonDebugMI(GuardBrI, *Guard);
    if (GuardBrI == Guard->end() || GuardTestI == Guard->end() ||
        GuardBrI->getOpcode() != Bedrock::BRCC ||
        GuardBrI->getOperand(1).getImm() != 0xe ||
        GuardBrI->getOperand(0).getMBB() == Preheader ||
        GuardBrI->getOperand(0).getMBB() == &Body ||
        !Guard->isSuccessor(Preheader) ||
        !Guard->isSuccessor(GuardBrI->getOperand(0).getMBB()) ||
        (GuardTestI->getOpcode() != Bedrock::TESTLrr &&
         GuardTestI->getOpcode() != Bedrock::TESTQrr) ||
        GuardTestI->getNumExplicitOperands() < 2 ||
        !GuardTestI->getOperand(0).isReg() ||
        !GuardTestI->getOperand(1).isReg() ||
        GuardTestI->getOperand(0).getReg() != SourceReg ||
        GuardTestI->getOperand(1).getReg() != SourceReg)
      continue;

    auto BodyStartI = firstNonDebugMI(Body);
    if (BodyStartI != Body.end() &&
        tryReuseRepgCounterInBody(MF, Body, *BodyStartI, *DecI, CounterReg,
                                  TII)) {
      while (foldDeadCalleeSavedPairs(MF)) {
      }
      return true;
    }
  }

  return false;
}

static bool isFrameAccessOpcode(unsigned Opcode) {
  switch (Opcode) {
  case Bedrock::ADDL3rmfi:
  case Bedrock::ADDQ3rmfi:
  case Bedrock::SUBL3rmfi:
  case Bedrock::SUBQ3rmfi:
  case Bedrock::ANDL3rmfi:
  case Bedrock::ANDQ3rmfi:
  case Bedrock::ORL3rmfi:
  case Bedrock::ORQ3rmfi:
  case Bedrock::XORL3rmfi:
  case Bedrock::XORQ3rmfi:
  case Bedrock::MULL3rmfi:
  case Bedrock::MULQ3rmfi:
  case Bedrock::MINUL3rmfi:
  case Bedrock::MINUQ3rmfi:
  case Bedrock::MINSL3rmfi:
  case Bedrock::MINSQ3rmfi:
  case Bedrock::MAXUL3rmfi:
  case Bedrock::MAXUQ3rmfi:
  case Bedrock::MAXSL3rmfi:
  case Bedrock::MAXSQ3rmfi:
  case Bedrock::DIVUL3rmfi:
  case Bedrock::DIVUQ3rmfi:
  case Bedrock::DIVSL3rmfi:
  case Bedrock::DIVSQ3rmfi:
  case Bedrock::MODUL3rmfi:
  case Bedrock::MODUQ3rmfi:
  case Bedrock::MODSL3rmfi:
  case Bedrock::MODSQ3rmfi:
  case Bedrock::ADDL3mfi:
  case Bedrock::ADDQ3mfi:
  case Bedrock::SUBL3mfi:
  case Bedrock::SUBQ3mfi:
  case Bedrock::ANDL3mfi:
  case Bedrock::ANDQ3mfi:
  case Bedrock::ORL3mfi:
  case Bedrock::ORQ3mfi:
  case Bedrock::XORL3mfi:
  case Bedrock::XORQ3mfi:
  case Bedrock::MINUL3mfi:
  case Bedrock::MINUQ3mfi:
  case Bedrock::MINSL3mfi:
  case Bedrock::MINSQ3mfi:
  case Bedrock::MAXUL3mfi:
  case Bedrock::MAXUQ3mfi:
  case Bedrock::MAXSL3mfi:
  case Bedrock::MAXSQ3mfi:
  case Bedrock::CMPLmfir:
  case Bedrock::CMPQmfir:
  case Bedrock::CMPLrmfi:
  case Bedrock::CMPQrmfi:
  case Bedrock::CMPLmfmf:
  case Bedrock::CMPQmfmf:
  case Bedrock::LOADB_Zfi:
  case Bedrock::LOADW_Zfi:
  case Bedrock::LOADL_Zfi:
  case Bedrock::LOADB_Sfi:
  case Bedrock::LOADW_Sfi:
  case Bedrock::LOADL_Sfi:
  case Bedrock::LOADLfi:
  case Bedrock::LOADQfi:
  case Bedrock::FLOADDfi:
  case Bedrock::LEAfi:
  case Bedrock::INCLfi:
  case Bedrock::INCQfi:
  case Bedrock::DECLfi:
  case Bedrock::DECQfi:
  case Bedrock::STOREBfi:
  case Bedrock::STOREWfi:
  case Bedrock::STORELfi:
  case Bedrock::STOREQfi:
  case Bedrock::FSTOREDfi:
  case Bedrock::STOREB_Immfi:
  case Bedrock::STOREW_Immfi:
  case Bedrock::STOREL_Immfi:
  case Bedrock::STOREQ_Immfi:
    return true;
  default:
    return false;
  }
}

static bool getFrameBaseOperandIndices(unsigned Opcode,
                                       SmallVectorImpl<unsigned> &BaseOps) {
  switch (Opcode) {
  case Bedrock::ADDL3rmfi:
  case Bedrock::ADDQ3rmfi:
  case Bedrock::SUBL3rmfi:
  case Bedrock::SUBQ3rmfi:
  case Bedrock::ANDL3rmfi:
  case Bedrock::ANDQ3rmfi:
  case Bedrock::ORL3rmfi:
  case Bedrock::ORQ3rmfi:
  case Bedrock::XORL3rmfi:
  case Bedrock::XORQ3rmfi:
  case Bedrock::MULL3rmfi:
  case Bedrock::MULQ3rmfi:
  case Bedrock::MINUL3rmfi:
  case Bedrock::MINUQ3rmfi:
  case Bedrock::MINSL3rmfi:
  case Bedrock::MINSQ3rmfi:
  case Bedrock::MAXUL3rmfi:
  case Bedrock::MAXUQ3rmfi:
  case Bedrock::MAXSL3rmfi:
  case Bedrock::MAXSQ3rmfi:
  case Bedrock::DIVUL3rmfi:
  case Bedrock::DIVUQ3rmfi:
  case Bedrock::DIVSL3rmfi:
  case Bedrock::DIVSQ3rmfi:
  case Bedrock::MODUL3rmfi:
  case Bedrock::MODUQ3rmfi:
  case Bedrock::MODSL3rmfi:
  case Bedrock::MODSQ3rmfi:
    BaseOps.push_back(2);
    return true;
  case Bedrock::ADDL3mfi:
  case Bedrock::ADDQ3mfi:
  case Bedrock::SUBL3mfi:
  case Bedrock::SUBQ3mfi:
  case Bedrock::ANDL3mfi:
  case Bedrock::ANDQ3mfi:
  case Bedrock::ORL3mfi:
  case Bedrock::ORQ3mfi:
  case Bedrock::XORL3mfi:
  case Bedrock::XORQ3mfi:
  case Bedrock::MINUL3mfi:
  case Bedrock::MINUQ3mfi:
  case Bedrock::MINSL3mfi:
  case Bedrock::MINSQ3mfi:
  case Bedrock::MAXUL3mfi:
  case Bedrock::MAXUQ3mfi:
  case Bedrock::MAXSL3mfi:
  case Bedrock::MAXSQ3mfi:
  case Bedrock::CMPLrmfi:
  case Bedrock::CMPQrmfi:
  case Bedrock::LOADB_Zfi:
  case Bedrock::LOADW_Zfi:
  case Bedrock::LOADL_Zfi:
  case Bedrock::LOADB_Sfi:
  case Bedrock::LOADW_Sfi:
  case Bedrock::LOADL_Sfi:
  case Bedrock::LOADLfi:
  case Bedrock::LOADQfi:
  case Bedrock::FLOADDfi:
  case Bedrock::LEAfi:
  case Bedrock::STOREBfi:
  case Bedrock::STOREWfi:
  case Bedrock::STORELfi:
  case Bedrock::STOREQfi:
  case Bedrock::FSTOREDfi:
  case Bedrock::STOREB_Immfi:
  case Bedrock::STOREW_Immfi:
  case Bedrock::STOREL_Immfi:
  case Bedrock::STOREQ_Immfi:
    BaseOps.push_back(1);
    return true;
  case Bedrock::CMPLmfir:
  case Bedrock::CMPQmfir:
  case Bedrock::INCLfi:
  case Bedrock::INCQfi:
  case Bedrock::DECLfi:
  case Bedrock::DECQfi:
    BaseOps.push_back(0);
    return true;
  case Bedrock::CMPLmfmf:
  case Bedrock::CMPQmfmf:
    BaseOps.push_back(0);
    BaseOps.push_back(2);
    return true;
  default:
    return false;
  }
}

static bool isCalleeSavedGPR(Register Reg) {
  return Reg == Bedrock::R8 || Reg == Bedrock::R9 || Reg == Bedrock::R10 ||
         Reg == Bedrock::R11 || Reg == Bedrock::R12 || Reg == Bedrock::R13 ||
         Reg == Bedrock::R14;
}

static bool isCallerSavedGPR(Register Reg) {
  return Reg == Bedrock::R0 || Reg == Bedrock::R1 || Reg == Bedrock::R2 ||
         Reg == Bedrock::R3 || Reg == Bedrock::R4 || Reg == Bedrock::R5 ||
         Reg == Bedrock::R6 || Reg == Bedrock::R7;
}

static bool getReusableIntegerConstant(const MachineInstr &MI, Register &Reg,
                                       int64_t &Imm) {
  if ((MI.getOpcode() != Bedrock::CONST32 &&
       MI.getOpcode() != Bedrock::CONST64) ||
      MI.getNumExplicitOperands() < 2 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isImm())
    return false;

  Reg = MI.getOperand(0).getReg();
  Imm = MI.getOperand(1).getImm();
  return Reg.isPhysical() && isGPR(Reg) && Imm >= 0 && Imm <= 0xffffffffLL;
}

static void clearRegKillsBetween(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator Begin,
                                 MachineBasicBlock::iterator End,
                                 Register Reg) {
  for (auto I = Begin; I != End; ++I) {
    for (MachineOperand &MO : I->operands()) {
      if (MO.isReg() && MO.getReg() == Reg && MO.isKill())
        MO.setIsKill(false);
    }
  }
}

bool BedrockPreEmitPeephole::foldCalleeSavedConstantReuse(
    MachineFunction &MF, const TargetInstrInfo &TII) {
  struct TrackedConstant {
    int64_t Imm = 0;
    MachineInstr *DefMI = nullptr;
  };

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    DenseMap<Register, TrackedConstant> Constants;
    for (auto I = MBB.begin(); I != MBB.end();) {
      MachineInstr &MI = *I;
      if (MI.isDebugInstr()) {
        ++I;
        continue;
      }

      Register DstReg;
      int64_t Imm;
      if (getReusableIntegerConstant(MI, DstReg, Imm) &&
          getConstMaterializationSize(MI) > 2) {
        Register ReuseReg;
        MachineInstr *DefMI = nullptr;
        for (const auto &Entry : Constants) {
          if (Entry.first == DstReg || Entry.second.Imm != Imm)
            continue;
          ReuseReg = Entry.first;
          DefMI = Entry.second.DefMI;
          break;
        }

        if (ReuseReg && DefMI) {
          clearRegKillsBetween(MBB, std::next(DefMI->getIterator()), I,
                               ReuseReg);
          BuildMI(MBB, I, MI.getDebugLoc(), TII.get(Bedrock::MOVQrr), DstReg)
              .addReg(ReuseReg);
          auto NextI = std::next(I);
          MI.eraseFromParent();
          I = NextI;
          ++NumCalleeSavedConstantReusesFolded;
          Changed = true;
          continue;
        }
      }

      SmallVector<Register, 4> Clobbered;
      for (const auto &Entry : Constants) {
        Register Reg = Entry.first;
        bool RegClobbered = false;
        for (const MachineOperand &MO : MI.operands()) {
          if (MO.isReg()) {
            if (MO.isDef() && MO.getReg() == Reg)
              RegClobbered = true;
          } else if (MO.isRegMask() && MO.clobbersPhysReg(Reg)) {
            RegClobbered = true;
          }
        }
        if (RegClobbered)
          Clobbered.push_back(Reg);
      }
      for (Register Reg : Clobbered)
        Constants.erase(Reg);

      if (getReusableIntegerConstant(MI, DstReg, Imm) &&
          isCalleeSavedGPR(DstReg) && !MI.getOperand(0).isDead())
        Constants[DstReg] = {Imm, &MI};

      ++I;
    }
  }

  return Changed;
}

bool BedrockPreEmitPeephole::foldCalleeSavedPushPairs(
    MachineBasicBlock::iterator I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &AdjMI = *I;
  if (AdjMI.getOpcode() != Bedrock::ADJSP_DOWN || !AdjMI.getOperand(0).isImm())
    return false;

  int64_t Amount = AdjMI.getOperand(0).getImm();
  if (Amount < 8)
    return false;

  struct StoreInfo {
    MachineBasicBlock::iterator I;
    Register Reg;
    bool IsKill;
  };
  SmallVector<StoreInfo, 6> Stores;
  auto StoreI = std::next(I);
  while (StoreI != MBB.end() && StoreI->getOpcode() == Bedrock::STOREQfi &&
         StoreI->getOperand(0).isReg() && StoreI->getOperand(1).isImm() &&
         StoreI->getOperand(2).isImm() && StoreI->getOperand(2).getImm() == 0) {
    Register Reg = StoreI->getOperand(0).getReg();
    int64_t ExpectedOffset = Amount - 8 * (Stores.size() + 1);
    if (!isCalleeSavedGPR(Reg) ||
        StoreI->getOperand(1).getImm() != ExpectedOffset)
      break;
    Stores.push_back({StoreI, Reg, StoreI->getOperand(0).isKill()});
    ++StoreI;
  }

  if (Stores.empty())
    return false;

  unsigned FoldedPairs = 0;
  unsigned FoldedSingles = 0;
  unsigned FoldedBytes = 0;
  for (unsigned Idx = 0; Idx < Stores.size();) {
    int PairIndex = -1;
    if (Idx + 1 < Stores.size())
      PairIndex = getPushPairIndex(Stores[Idx].Reg, Stores[Idx + 1].Reg);

    if (PairIndex >= 0) {
      DebugLoc DL = Stores[Idx].I->getDebugLoc();
      BuildMI(MBB, I, DL, TII.get(Bedrock::PUSHPi)).addImm(PairIndex);
      Stores[Idx].I->eraseFromParent();
      Stores[Idx + 1].I->eraseFromParent();
      ++FoldedPairs;
      FoldedBytes += 16;
      Idx += 2;
      continue;
    }

    DebugLoc DL = Stores[Idx].I->getDebugLoc();
    BuildMI(MBB, I, DL, TII.get(Bedrock::PUSHr))
        .addReg(Stores[Idx].Reg, getKillRegState(Stores[Idx].IsKill));
    Stores[Idx].I->eraseFromParent();
    ++FoldedSingles;
    FoldedBytes += 8;
    ++Idx;
  }

  if (FoldedBytes == 0)
    return false;

  AdjMI.getOperand(0).setImm(Amount - FoldedBytes);
  NumCalleeSavedPairsFolded += FoldedPairs;
  NumCalleeSavedSinglesFolded += FoldedSingles;
  return true;
}

bool BedrockPreEmitPeephole::foldCalleeSavedFramePaddingPair(
    MachineFunction &MF, const TargetInstrInfo &TII) {
  MachineBasicBlock::iterator PushI;
  MachineBasicBlock::iterator DownI;
  MachineBasicBlock *PushMBB = nullptr;
  Register SavedReg;
  Register MateReg;
  int PairIndex = -1;
  int64_t Amount = 0;
  SmallVector<std::pair<MachineInstr *, unsigned>, 16> FrameBaseOps;

  for (MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(); I != MBB.end(); ++I) {
      if (I->isDebugInstr())
        continue;

      SmallVector<unsigned, 2> BaseOps;
      if (getFrameBaseOperandIndices(I->getOpcode(), BaseOps)) {
        for (unsigned BaseOp : BaseOps) {
          if (I->getNumOperands() <= BaseOp + 1 ||
              !I->getOperand(BaseOp).isImm() ||
              !I->getOperand(BaseOp + 1).isImm())
            return false;
          int64_t FrameOffset = I->getOperand(BaseOp).getImm() +
                                I->getOperand(BaseOp + 1).getImm();
          if (FrameOffset < 8)
            return false;
          FrameBaseOps.push_back({&*I, BaseOp});
        }
        continue;
      }

      if (I->getOpcode() != Bedrock::PUSHr ||
          I->getNumExplicitOperands() != 1 || !I->getOperand(0).isReg())
        continue;

      Register Reg = I->getOperand(0).getReg();
      int CandidatePairIndex = getPaddingPairIndex(Reg);
      if (CandidatePairIndex < 0)
        continue;

      auto NextI = std::next(I);
      while (NextI != MBB.end() && NextI->isDebugInstr())
        ++NextI;
      if (NextI == MBB.end() || NextI->getOpcode() != Bedrock::ADJSP_DOWN ||
          !NextI->getOperand(0).isImm() || NextI->getOperand(0).getImm() <= 8)
        continue;

      if (PushMBB)
        return false;

      PushI = I;
      DownI = NextI;
      PushMBB = &MBB;
      SavedReg = Reg;
      PairIndex = CandidatePairIndex;
      MateReg = getPaddingPairMate(Reg);
      Amount = NextI->getOperand(0).getImm();
    }
  }

  if (!PushMBB || FrameBaseOps.empty())
    return false;

  SmallVector<std::pair<MachineBasicBlock::iterator,
                        MachineBasicBlock::iterator>,
              4>
      Pops;
  for (MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(); I != MBB.end(); ++I) {
      if (I->isDebugInstr())
        continue;
      if (usesReg(*I, MateReg))
        return false;
      if (I->getOpcode() != Bedrock::POPr)
        continue;
      if (I->getNumExplicitOperands() != 1 || !I->getOperand(0).isReg() ||
          I->getOperand(0).getReg() != SavedReg)
        continue;
      if (I == MBB.begin())
        return false;

      auto PrevI = std::prev(I);
      while (PrevI != MBB.begin() && PrevI->isDebugInstr())
        --PrevI;
      if (PrevI->isDebugInstr() || PrevI->getOpcode() != Bedrock::ADJSP_UP ||
          !PrevI->getOperand(0).isImm() || PrevI->getOperand(0).getImm() != Amount)
        return false;

      Pops.push_back({PrevI, I});
    }
  }

  if (Pops.empty())
    return false;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (&MI == &*DownI)
        continue;
      bool IsMatchedPopAdjust = false;
      for (const auto &Pop : Pops) {
        if (&MI == &*Pop.first) {
          IsMatchedPopAdjust = true;
          break;
        }
      }
      if (IsMatchedPopAdjust)
        continue;
      if ((MI.getOpcode() == Bedrock::ADJSP_DOWN ||
           MI.getOpcode() == Bedrock::ADJSP_UP) &&
          MI.getOperand(0).isImm() && MI.getOperand(0).getImm() != 0)
        return false;
    }
  }

  PushI->setDesc(TII.get(Bedrock::PUSHPi));
  while (PushI->getNumOperands() != 0)
    PushI->removeOperand(PushI->getNumOperands() - 1);
  PushI->addOperand(MachineOperand::CreateImm(PairIndex));
  DownI->getOperand(0).setImm(Amount - 8);

  for (auto &Pop : Pops) {
    MachineBasicBlock::iterator UpI = Pop.first;
    MachineBasicBlock::iterator PopI = Pop.second;
    UpI->getOperand(0).setImm(Amount - 8);
    PopI->setDesc(TII.get(Bedrock::POPPi));
    while (PopI->getNumOperands() != 0)
      PopI->removeOperand(PopI->getNumOperands() - 1);
    PopI->addOperand(MachineOperand::CreateImm(PairIndex));
  }

  for (auto &FrameBase : FrameBaseOps)
    FrameBase.first->getOperand(FrameBase.second).setImm(
        FrameBase.first->getOperand(FrameBase.second).getImm() - 8);

  ++NumCalleeSavedFramePaddingPairsFolded;
  return true;
}

bool BedrockPreEmitPeephole::foldCalleeSavedPaddingPair(
    MachineFunction &MF, const TargetInstrInfo &TII) {
  MachineBasicBlock::iterator PushI;
  MachineBasicBlock::iterator DownI;
  MachineBasicBlock *PushMBB = nullptr;
  Register SavedReg;
  Register MateReg;
  int PairIndex = -1;

  for (MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(); I != MBB.end(); ++I) {
      if (I->isDebugInstr())
        continue;
      if (isFrameAccessOpcode(I->getOpcode()))
        return false;
      if (I->getOpcode() != Bedrock::PUSHr || I->getNumExplicitOperands() != 1 ||
          !I->getOperand(0).isReg())
        continue;

      Register Reg = I->getOperand(0).getReg();
      int CandidatePairIndex = getPaddingPairIndex(Reg);
      if (CandidatePairIndex < 0)
        continue;

      auto NextI = std::next(I);
      while (NextI != MBB.end() && NextI->isDebugInstr())
        ++NextI;
      if (NextI == MBB.end() || NextI->getOpcode() != Bedrock::ADJSP_DOWN ||
          !NextI->getOperand(0).isImm() || NextI->getOperand(0).getImm() != 8)
        continue;

      if (PushMBB)
        return false;

      PushI = I;
      DownI = NextI;
      PushMBB = &MBB;
      SavedReg = Reg;
      PairIndex = CandidatePairIndex;
      MateReg = getPaddingPairMate(Reg);
    }
  }

  if (!PushMBB)
    return false;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (&MI == &*PushI || &MI == &*DownI)
        continue;
      if (usesReg(MI, MateReg))
        return false;
    }
  }

  SmallVector<std::pair<MachineBasicBlock::iterator,
                        MachineBasicBlock::iterator>,
              4>
      Pops;
  for (MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(); I != MBB.end(); ++I) {
      if (I->isDebugInstr() || I->getOpcode() != Bedrock::POPr)
        continue;
      if (I->getNumExplicitOperands() != 1 || !I->getOperand(0).isReg() ||
          I->getOperand(0).getReg() != SavedReg)
        continue;
      if (I == MBB.begin())
        return false;

      auto PrevI = std::prev(I);
      while (PrevI != MBB.begin() && PrevI->isDebugInstr())
        --PrevI;
      if (PrevI->isDebugInstr() || PrevI->getOpcode() != Bedrock::ADJSP_UP ||
          !PrevI->getOperand(0).isImm() || PrevI->getOperand(0).getImm() != 8)
        return false;

      Pops.push_back({PrevI, I});
    }
  }

  if (Pops.empty())
    return false;

  PushI->setDesc(TII.get(Bedrock::PUSHPi));
  while (PushI->getNumOperands() != 0)
    PushI->removeOperand(PushI->getNumOperands() - 1);
  PushI->addOperand(MachineOperand::CreateImm(PairIndex));
  DownI->eraseFromParent();

  for (auto &Pop : Pops) {
    MachineBasicBlock::iterator UpI = Pop.first;
    MachineBasicBlock::iterator PopI = Pop.second;
    PopI->setDesc(TII.get(Bedrock::POPPi));
    while (PopI->getNumOperands() != 0)
      PopI->removeOperand(PopI->getNumOperands() - 1);
    PopI->addOperand(MachineOperand::CreateImm(PairIndex));
    UpI->eraseFromParent();
  }

  ++NumCalleeSavedPaddingPairsFolded;
  return true;
}

bool BedrockPreEmitPeephole::foldCalleeSavedPopPairs(
    MachineBasicBlock::iterator I, MachineBasicBlock &MBB,
    const TargetInstrInfo &TII) {
  MachineInstr &AdjMI = *I;
  if (AdjMI.getOpcode() != Bedrock::ADJSP_UP || !AdjMI.getOperand(0).isImm())
    return false;

  int64_t Amount = AdjMI.getOperand(0).getImm();
  if (Amount < 8 || I == MBB.begin())
    return false;

  struct LoadInfo {
    MachineBasicBlock::iterator I;
    Register Reg;
    int64_t Offset;
  };
  SmallVector<LoadInfo, 6> Loads;
  auto LoadI = I;
  while (LoadI != MBB.begin()) {
    auto PrevI = std::prev(LoadI);
    if (PrevI->getOpcode() != Bedrock::LOADQfi ||
        !PrevI->getOperand(0).isReg() || !PrevI->getOperand(1).isImm() ||
        !PrevI->getOperand(2).isImm() || PrevI->getOperand(2).getImm() != 0)
      break;

    Register Reg = PrevI->getOperand(0).getReg();
    if (!isCalleeSavedGPR(Reg))
      break;
    Loads.push_back({PrevI, Reg, PrevI->getOperand(1).getImm()});
    LoadI = PrevI;
  }

  if (Loads.empty())
    return false;
  std::reverse(Loads.begin(), Loads.end());

  for (unsigned Idx = 0, E = Loads.size(); Idx != E; ++Idx) {
    int64_t ExpectedOffset = Amount - 8 * (E - Idx);
    if (Loads[Idx].Offset != ExpectedOffset)
      return false;
  }

  unsigned FoldedPairs = 0;
  unsigned FoldedSingles = 0;
  unsigned FoldedBytes = 0;
  auto InsertI = std::next(I);
  for (unsigned Idx = 0; Idx < Loads.size();) {
    int PairIndex = -1;
    if (Idx + 1 < Loads.size())
      PairIndex = getPopPairIndex(Loads[Idx].Reg, Loads[Idx + 1].Reg);

    if (PairIndex >= 0) {
      DebugLoc DL = Loads[Idx].I->getDebugLoc();
      BuildMI(MBB, InsertI, DL, TII.get(Bedrock::POPPi)).addImm(PairIndex);
      Loads[Idx].I->eraseFromParent();
      Loads[Idx + 1].I->eraseFromParent();
      ++FoldedPairs;
      FoldedBytes += 16;
      Idx += 2;
      continue;
    }

    DebugLoc DL = Loads[Idx].I->getDebugLoc();
    BuildMI(MBB, InsertI, DL, TII.get(Bedrock::POPr), Loads[Idx].Reg);
    Loads[Idx].I->eraseFromParent();
    ++FoldedSingles;
    FoldedBytes += 8;
    ++Idx;
  }

  if (FoldedBytes == 0)
    return false;

  AdjMI.getOperand(0).setImm(Amount - FoldedBytes);
  NumCalleeSavedPairsFolded += FoldedPairs;
  NumCalleeSavedSinglesFolded += FoldedSingles;
  return true;
}

bool BedrockPreEmitPeephole::foldTailCall(MachineBasicBlock::iterator I,
                                          MachineBasicBlock &MBB,
                                          const TargetInstrInfo &TII) {
  MachineInstr &CallMI = *I;
  if (CallMI.getOpcode() != Bedrock::CALL_TAIL &&
      CallMI.getOpcode() != Bedrock::CALLr_TAIL)
    return false;

  MachineInstr *CallPadDown = nullptr;
  for (auto PrevI = I; PrevI != MBB.begin();) {
    --PrevI;
    if (PrevI->isDebugInstr())
      continue;
    if (PrevI->getOpcode() == Bedrock::ADJSP_DOWN &&
        PrevI->getOperand(0).isImm() && PrevI->getOperand(0).getImm() == 8) {
      CallPadDown = &*PrevI;
      break;
    }
    if (PrevI->isCall() || PrevI->isInlineAsm() || PrevI->isTerminator() ||
        isFrameAccessOpcode(PrevI->getOpcode()) ||
        instructionTouchesReg(*PrevI, Bedrock::R15) ||
        PrevI->getOpcode() == Bedrock::ADJSP_DOWN ||
        PrevI->getOpcode() == Bedrock::ADJSP_UP ||
        PrevI->getOpcode() == Bedrock::PUSHr ||
        PrevI->getOpcode() == Bedrock::PUSHPi ||
        PrevI->getOpcode() == Bedrock::POPr ||
        PrevI->getOpcode() == Bedrock::POPPi)
      break;
  }

  auto RetI = std::next(I);
  MachineInstr *CallPadUp = nullptr;
  auto AfterCallI = RetI;
  while (AfterCallI != MBB.end() && AfterCallI->isDebugInstr())
    ++AfterCallI;
  if (AfterCallI != MBB.end() &&
      AfterCallI->getOpcode() == Bedrock::ADJSP_UP &&
      AfterCallI->getOperand(0).isImm() &&
      AfterCallI->getOperand(0).getImm() == 8)
    CallPadUp = &*AfterCallI;
  if ((CallPadDown == nullptr) != (CallPadUp == nullptr))
    return false;

  while (RetI != MBB.end() &&
         (RetI->isDebugInstr() || isTailCallEpilogueInstr(*RetI)))
    ++RetI;
  if (RetI == MBB.end() || RetI->getOpcode() != Bedrock::RET)
    return false;

  // An indirect target may be allocated in a callee-saved register. Reject a
  // fold if restoring the caller would overwrite it before the final jump.
  if (CallMI.getOpcode() == Bedrock::CALLr_TAIL) {
    Register TargetReg = CallMI.getOperand(0).getReg();
    for (auto Scan = std::next(I); Scan != RetI; ++Scan)
      if (!Scan->isDebugInstr() && instructionTouchesReg(*Scan, TargetReg))
        return false;
  }

  DebugLoc DL = CallMI.getDebugLoc();
  unsigned TailOpcode = CallMI.getOpcode() == Bedrock::CALL_TAIL
                            ? Bedrock::TAILCALL
                            : Bedrock::BRIND;
  BuildMI(MBB, RetI, DL, TII.get(TailOpcode)).add(CallMI.getOperand(0));
  if (CallPadDown && CallPadUp) {
    CallPadDown->eraseFromParent();
    CallPadUp->eraseFromParent();
  }
  CallMI.eraseFromParent();
  RetI->eraseFromParent();
  ++NumTailCallsFolded;
  return true;
}

bool BedrockPreEmitPeephole::isTailCallEpilogueInstr(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case Bedrock::ADJSP_UP:
  case Bedrock::POPr:
  case Bedrock::POPPi:
    return true;
  default:
    return false;
  }
}

static MachineBasicBlock::iterator
skipDebugForward(MachineBasicBlock::iterator I, MachineBasicBlock &MBB) {
  while (I != MBB.end() && I->isDebugInstr())
    ++I;
  return I;
}

bool BedrockPreEmitPeephole::foldZeroReturnCalleeSavedPhi(MachineFunction &MF) {
  if (MF.empty())
    return false;

  for (MachineBasicBlock &EntryMBB : MF) {
    if (EntryMBB.succ_size() != 2)
      continue;

    MachineBasicBlock *TrueMBB = EntryMBB.getNextNode();
    if (!TrueMBB || TrueMBB->succ_size() != 1)
      continue;
    MachineBasicBlock *ReturnMBB = *TrueMBB->succ_begin();

    auto DownI = skipDebugForward(EntryMBB.begin(), EntryMBB);
    if (DownI == EntryMBB.end() || DownI->getOpcode() != Bedrock::ADJSP_DOWN ||
        !DownI->getOperand(0).isImm() || DownI->getOperand(0).getImm() != 16)
      continue;
    auto StoreI = skipDebugForward(std::next(DownI), EntryMBB);
    if (StoreI == EntryMBB.end() || StoreI->getOpcode() != Bedrock::STOREQfi ||
        StoreI->getNumExplicitOperands() < 3 || !StoreI->getOperand(0).isReg() ||
        !StoreI->getOperand(1).isImm() || !StoreI->getOperand(2).isImm() ||
        StoreI->getOperand(1).getImm() != 8 ||
        StoreI->getOperand(2).getImm() != 0)
      continue;
    Register SavedReg = StoreI->getOperand(0).getReg();
    if (!SavedReg.isPhysical() || !isCalleeSavedGPR(SavedReg))
      continue;

    auto SavedZeroI = skipDebugForward(std::next(StoreI), EntryMBB);
    if (SavedZeroI == EntryMBB.end() ||
        SavedZeroI->getOpcode() != Bedrock::CONST64 ||
        SavedZeroI->getNumExplicitOperands() < 2 ||
        !SavedZeroI->getOperand(0).isReg() ||
        !SavedZeroI->getOperand(1).isImm() ||
        SavedZeroI->getOperand(0).getReg() != SavedReg ||
        SavedZeroI->getOperand(1).getImm() != 0)
      continue;

    auto RetZeroI = skipDebugForward(std::next(SavedZeroI), EntryMBB);
    if (RetZeroI == EntryMBB.end() ||
        RetZeroI->getOpcode() != Bedrock::CONST64 ||
        RetZeroI->getNumExplicitOperands() < 2 ||
        !RetZeroI->getOperand(0).isReg() ||
        !RetZeroI->getOperand(1).isImm() ||
        RetZeroI->getOperand(0).getReg() != Bedrock::R0 ||
        RetZeroI->getOperand(1).getImm() != 0)
      continue;

    auto CallI = skipDebugForward(std::next(RetZeroI), EntryMBB);
    if (CallI == EntryMBB.end() || !CallI->isCall())
      continue;
    auto AndI = skipDebugForward(std::next(CallI), EntryMBB);
    if (AndI == EntryMBB.end() || AndI->getOpcode() != Bedrock::ANDL3ri ||
        AndI->getNumExplicitOperands() < 3 || !AndI->getOperand(0).isReg() ||
        !AndI->getOperand(1).isReg() || !AndI->getOperand(2).isImm() ||
        AndI->getOperand(0).getReg() != Bedrock::R0 ||
        AndI->getOperand(1).getReg() != Bedrock::R0 ||
        AndI->getOperand(2).getImm() != 1)
      continue;
    auto TestI = skipDebugForward(std::next(AndI), EntryMBB);
    if (TestI == EntryMBB.end() || TestI->getOpcode() != Bedrock::TESTLrr ||
        TestI->getNumExplicitOperands() < 2 || !TestI->getOperand(0).isReg() ||
        !TestI->getOperand(1).isReg() ||
        TestI->getOperand(0).getReg() != Bedrock::R0 ||
        TestI->getOperand(1).getReg() != Bedrock::R0)
      continue;
    auto BrI = skipDebugForward(std::next(TestI), EntryMBB);
    if (BrI == EntryMBB.end() || BrI->getOpcode() != Bedrock::BRCC ||
        BrI->getNumExplicitOperands() < 2 || !BrI->getOperand(0).isMBB() ||
        !BrI->getOperand(1).isImm() || BrI->getOperand(0).getMBB() != ReturnMBB ||
        BrI->getOperand(1).getImm() != 2)
      continue;

    auto TrueSetI = skipDebugForward(TrueMBB->begin(), *TrueMBB);
    if (TrueSetI == TrueMBB->end() || !definesReg(*TrueSetI, Bedrock::R0))
      continue;
    auto TrueCallI = skipDebugForward(std::next(TrueSetI), *TrueMBB);
    if (TrueCallI == TrueMBB->end() || !TrueCallI->isCall())
      continue;
    auto ExtI = skipDebugForward(std::next(TrueCallI), *TrueMBB);
    if (ExtI == TrueMBB->end() || ExtI->getOpcode() != Bedrock::EXTZQLrr ||
        ExtI->getNumExplicitOperands() < 2 || !ExtI->getOperand(0).isReg() ||
        !ExtI->getOperand(1).isReg() ||
        ExtI->getOperand(0).getReg() != SavedReg ||
        ExtI->getOperand(1).getReg() != Bedrock::R0)
      continue;

    auto RetCopyI = skipDebugForward(ReturnMBB->begin(), *ReturnMBB);
    if (RetCopyI == ReturnMBB->end() ||
        RetCopyI->getOpcode() != Bedrock::MOVQrr ||
        RetCopyI->getNumExplicitOperands() < 2 ||
        !RetCopyI->getOperand(0).isReg() ||
        !RetCopyI->getOperand(1).isReg() ||
        RetCopyI->getOperand(0).getReg() != Bedrock::R0 ||
        RetCopyI->getOperand(1).getReg() != SavedReg)
      continue;
    auto LoadI = skipDebugForward(std::next(RetCopyI), *ReturnMBB);
    if (LoadI == ReturnMBB->end() || LoadI->getOpcode() != Bedrock::LOADQfi ||
        LoadI->getNumExplicitOperands() < 3 || !LoadI->getOperand(0).isReg() ||
        !LoadI->getOperand(1).isImm() || !LoadI->getOperand(2).isImm() ||
        LoadI->getOperand(0).getReg() != SavedReg ||
        LoadI->getOperand(1).getImm() != 8 ||
        LoadI->getOperand(2).getImm() != 0)
      continue;
    auto UpI = skipDebugForward(std::next(LoadI), *ReturnMBB);
    if (UpI == ReturnMBB->end() || UpI->getOpcode() != Bedrock::ADJSP_UP ||
        !UpI->getOperand(0).isImm() || UpI->getOperand(0).getImm() != 16)
      continue;
    auto RetI = skipDebugForward(std::next(UpI), *ReturnMBB);
    if (RetI == ReturnMBB->end() || RetI->getOpcode() != Bedrock::RET)
      continue;

    SmallPtrSet<const MachineInstr *, 8> Allowed = {
        &*StoreI, &*SavedZeroI, &*ExtI, &*RetCopyI, &*LoadI};
    bool HasUnexpectedTouch = false;
    for (const MachineBasicBlock &MBB : MF) {
      for (const MachineInstr &MI : MBB) {
        if (MI.isDebugInstr() || Allowed.contains(&MI))
          continue;
        if (usesReg(MI, SavedReg) || definesReg(MI, SavedReg)) {
          HasUnexpectedTouch = true;
          break;
        }
      }
      if (HasUnexpectedTouch)
        break;
    }
    if (HasUnexpectedTouch)
      continue;

    LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding zero-return "
                         "callee-saved phi in ";
               MF.getFunction().printAsOperand(dbgs(), false); dbgs() << "\n");

    for (MachineOperand &MO : TestI->operands()) {
      if (MO.isReg() && MO.getReg() == Bedrock::R0)
        MO.setIsKill(false);
    }
    ExtI->getOperand(0).setReg(Bedrock::R0);

    for (MachineBasicBlock &MBB : MF)
      MBB.removeLiveIn(SavedReg.asMCReg());
    if (!ReturnMBB->isLiveIn(Bedrock::R0))
      ReturnMBB->addLiveIn(Bedrock::R0);

    StoreI->eraseFromParent();
    SavedZeroI->eraseFromParent();
    RetCopyI->eraseFromParent();
    LoadI->eraseFromParent();
    DownI->eraseFromParent();
    UpI->eraseFromParent();

    ++NumZeroReturnCalleeSavedPhisFolded;
    return true;
  }

  return false;
}

static unsigned getZeroImmediateStoreOpcode(const MachineInstr &MI,
                                            Register ZeroReg) {
  if (MI.getNumOperands() == 0 || !MI.getOperand(0).isReg() ||
      MI.getOperand(0).getReg() != ZeroReg)
    return 0;

  for (unsigned Idx = 1, End = MI.getNumOperands(); Idx != End; ++Idx) {
    const MachineOperand &MO = MI.getOperand(Idx);
    if (MO.isReg() && MO.getReg() == ZeroReg)
      return 0;
  }

  switch (MI.getOpcode()) {
  case Bedrock::STOREBrr:
    return Bedrock::STOREB_Immrr;
  case Bedrock::STOREWrr:
    return Bedrock::STOREW_Immrr;
  case Bedrock::STORELrr:
    return Bedrock::STOREL_Immrr;
  case Bedrock::STOREQrr:
    return Bedrock::STOREQ_Immrr;
  case Bedrock::STOREBro:
    return Bedrock::STOREB_Immro;
  case Bedrock::STOREWro:
    return Bedrock::STOREW_Immro;
  case Bedrock::STORELro:
    return Bedrock::STOREL_Immro;
  case Bedrock::STOREQro:
    return Bedrock::STOREQ_Immro;
  case Bedrock::STOREBabs:
    return Bedrock::STOREB_Immabs;
  case Bedrock::STOREWabs:
    return Bedrock::STOREW_Immabs;
  case Bedrock::STORELabs:
    return Bedrock::STOREL_Immabs;
  case Bedrock::STOREQabs:
    return Bedrock::STOREQ_Immabs;
  case Bedrock::STOREBfi:
    return Bedrock::STOREB_Immfi;
  case Bedrock::STOREWfi:
    return Bedrock::STOREW_Immfi;
  case Bedrock::STORELfi:
    return Bedrock::STOREL_Immfi;
  case Bedrock::STOREQfi:
    return Bedrock::STOREQ_Immfi;
  default:
    return 0;
  }
}

bool BedrockPreEmitPeephole::foldZeroStores(MachineBasicBlock::iterator &I,
                                            MachineBasicBlock &MBB,
                                            const TargetInstrInfo &TII) {
  MachineInstr &ZeroMI = *I;
  Register ZeroReg;
  if (!getZeroMaterializationReg(ZeroMI, ZeroReg))
    return false;
  if (!ZeroReg.isPhysical() || !Bedrock::GPR64RegClass.contains(ZeroReg))
    return false;

  SmallVector<MachineInstr *, 4> Stores;
  bool ReachesSafeEnd = false;
  for (auto J = std::next(I); J != MBB.end(); ++J) {
    if (J->isDebugInstr())
      continue;

    if (definesReg(*J, ZeroReg)) {
      ReachesSafeEnd = true;
      break;
    }

    if (getZeroImmediateStoreOpcode(*J, ZeroReg) != 0) {
      Stores.push_back(&*J);
      continue;
    }

    if (readsReg(*J, ZeroReg))
      return false;

    if (J->isCall() && isCallerSavedGPR(ZeroReg)) {
      ReachesSafeEnd = true;
      break;
    }

    if (J->isTerminator())
      break;
  }

  if (Stores.empty())
    return false;

  if (!ReachesSafeEnd && successorHasLiveIn(MBB, ZeroReg))
    return false;

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding zero stores: ";
             ZeroMI.dump());

  for (MachineInstr *StoreMI : Stores) {
    unsigned NewOpcode = getZeroImmediateStoreOpcode(*StoreMI, ZeroReg);
    StoreMI->setDesc(TII.get(NewOpcode));
    StoreMI->getOperand(0).ChangeToImmediate(0);
    ++NumZeroStoresFolded;
  }

  auto NextI = std::next(I);
  ZeroMI.eraseFromParent();
  I = NextI;
  return true;
}

bool BedrockPreEmitPeephole::foldZeroReuse(MachineBasicBlock::iterator I,
                                           MachineBasicBlock &MBB) {
  MachineInstr &ZeroMI = *I;
  Register ZeroReg;
  if (!getZeroMaterializationReg(ZeroMI, ZeroReg))
    return false;
  if (!ZeroReg.isPhysical() || !Bedrock::GPR64RegClass.contains(ZeroReg))
    return false;

  auto Begin = std::next(I);
  for (auto J = Begin; J != MBB.end(); ++J) {
    if (J->isDebugInstr())
      continue;
    if (J->isCall() || J->isTerminator())
      return false;

    Register ReuseReg;
    if (!getZeroMaterializationReg(*J, ReuseReg))
      continue;
    if (ReuseReg == ZeroReg || !ReuseReg.isPhysical() ||
        !Bedrock::GPR64RegClass.contains(ReuseReg))
      continue;

    auto RewriteEnd = J;
    for (auto K = Begin; K != J; ++K) {
      if (K->isDebugInstr())
        continue;
      if (K->isCall() || K->isTerminator())
        return false;
      if (usesReg(*K, ReuseReg))
        return false;
      if (definesReg(*K, ZeroReg)) {
        RewriteEnd = K;
        break;
      }
    }

    bool ReachesBlockEnd = true;
    for (auto K = std::next(J); K != MBB.end(); ++K) {
      if (K->isDebugInstr())
        continue;
      if (readsReg(*K, ZeroReg))
        return false;
      if (definesReg(*K, ZeroReg)) {
        ReachesBlockEnd = false;
        break;
      }
      if (K->isTerminator()) {
        if (successorHasLiveIn(MBB, ZeroReg))
          return false;
        ReachesBlockEnd = false;
        break;
      }
    }
    if (ReachesBlockEnd && successorHasLiveIn(MBB, ZeroReg))
      return false;

    LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: reusing zero: ";
               ZeroMI.dump(); J->dump());

    ZeroMI.getOperand(0).setReg(ReuseReg);
    for (auto K = Begin; K != RewriteEnd; ++K) {
      for (MachineOperand &MO : K->operands()) {
        if (MO.isReg() && !MO.isDef() && MO.getReg() == ZeroReg) {
          MO.setReg(ReuseReg);
          MO.setIsKill(false);
        }
      }
    }

    J->eraseFromParent();
    ++NumZeroReusesFolded;
    return true;
  }

  return false;
}

bool BedrockPreEmitPeephole::foldZeroCopy(MachineBasicBlock::iterator I,
                                          MachineBasicBlock &MBB) {
  MachineInstr &ZeroMI = *I;
  switch (ZeroMI.getOpcode()) {
  case Bedrock::CLRQr:
    break;
  case Bedrock::CONST32:
  case Bedrock::CONST64:
    if (!ZeroMI.getOperand(1).isImm() || ZeroMI.getOperand(1).getImm() != 0)
      return false;
    break;
  default:
    return false;
  }

  auto NextI = std::next(I);
  if (NextI == MBB.end())
    return false;

  MachineInstr &MovMI = *NextI;
  if (MovMI.getOpcode() != Bedrock::MOVQrr)
    return false;

  MachineOperand &SrcOp = MovMI.getOperand(1);
  if (!SrcOp.isReg() || !SrcOp.isKill())
    return false;

  Register SrcReg = SrcOp.getReg();
  Register ZeroReg = ZeroMI.getOperand(0).getReg();
  if (SrcReg != ZeroReg)
    return false;

  Register DstReg = MovMI.getOperand(0).getReg();
  if (!DstReg.isPhysical() || !SrcReg.isPhysical() || DstReg == SrcReg)
    return false;

  LLVM_DEBUG(dbgs() << "Bedrock pre-emit peephole: folding zero copy: ";
             ZeroMI.dump(); MovMI.dump());

  ZeroMI.getOperand(0).setReg(DstReg);
  MovMI.eraseFromParent();
  ++NumZeroCopiesFolded;
  return true;
}

bool BedrockPreEmitPeephole::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  DenseMap<Register, MachineInstr *> EntryPromotableZeroExts =
      collectEntryPromotableZeroExts(MF);

  while (foldZeroReturnCalleeSavedPhi(MF))
    Changed = true;

  DenseMap<const MachineBasicBlock *, SmallSet<Register, 16>>
      KnownZeroHighBlockInputs = computeKnownZeroHighBlockInputs(MF);

  for (MachineBasicBlock &MBB : MF) {
    bool LocalChanged;
    do {
      LocalChanged = false;
      if (foldKnownZeroHighExtends(MBB, TII, EntryPromotableZeroExts,
                                   KnownZeroHighBlockInputs)) {
        Changed = LocalChanged = true;
        continue;
      }

      if (foldSignExtendedSMaxZeroExtends(MBB, TII)) {
        Changed = LocalChanged = true;
        continue;
      }

      for (auto I = MBB.begin(); I != MBB.end();) {
        if (foldZeroMinMaxWithKnownZero(I, MBB, TII)) {
          Changed = LocalChanged = true;
          continue;
        }

        if (foldPositiveOneCompare(I, MBB, TII)) {
          Changed = LocalChanged = true;
          continue;
        }

        if (foldZeroCompare(I, MBB, TII)) {
          Changed = LocalChanged = true;
          continue;
        }

        if (foldMaterializedConstantCompare(I, MBB, TII)) {
          Changed = LocalChanged = true;
          continue;
        }

        if (foldByteRotateIdiom(I, MBB, TII)) {
          Changed = LocalChanged = true;
          continue;
        }

        if (foldDeadCopyBeforeBranch(I, MBB)) {
          Changed = LocalChanged = true;
          continue;
        }

        if (foldMulByThree(I, MBB, TII)) {
          Changed = LocalChanged = true;
          continue;
        }

        if (foldAdjacentMaterializedMulImmediate(I, MBB, TII)) {
          Changed = LocalChanged = true;
          continue;
        }

        MachineBasicBlock::iterator Cur = I++;
        if (foldPositiveOneImmCompare(Cur, MBB, TII)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        if (foldRedundantFrameStore(Cur, MBB)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        I = Cur;
        if (foldZeroStores(I, MBB, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        Cur = I++;

        if (foldZeroReuse(Cur, MBB)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        if (foldZeroCopy(Cur, MBB)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        if (foldShiftLeftOne(Cur, MBB, TII)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        if (foldShiftOrOne(Cur, MBB, TII)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        if (foldShiftMaskByteExtend(Cur, MBB, TII)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        I = Cur;
        if (foldSymbolIndexLea(I, MBB, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        Cur = I++;

        if (foldSymbolOffsetLea(Cur, MBB, TII)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        if (foldShiftedByteStore(Cur, MBB, TII)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        if (foldDeadMemIncDec(Cur, MBB, TII)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        if (foldRetainedLoadMemIncDec(Cur, MBB, TII)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        if (foldReloadedMemIncDec(Cur, MBB, TII)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        if (foldDivRemDecomposition(Cur, MBB, TII)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        auto Next = std::next(Cur);
        if (foldRedundantSelfLogic(Cur, MBB)) {
          Changed = LocalChanged = true;
          I = Next;
          continue;
        }

        if (foldTwoStageExtend(Cur, MBB, TII)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        if (foldAndExtend(Cur, MBB, TII)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        if (foldCommutativeCopy(Cur, MBB, MF)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        if (foldCalleeSavedPushPairs(Cur, MBB, TII)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        if (foldCalleeSavedPopPairs(Cur, MBB, TII)) {
          Changed = LocalChanged = true;
          I = std::next(Cur);
          continue;
        }

        if (foldTailCall(Cur, MBB, TII)) {
          Changed = LocalChanged = true;
          I = MBB.end();
          continue;
        }
      }
    } while (LocalChanged);
  }

  while (foldSingleUseConstThreeMul(MF, TII))
    Changed = true;

  while (foldAffineMulLoop(MF, TII))
    Changed = true;

  while (foldCalleeSavedAffineMulLoop(MF, TII))
    Changed = true;

  while (foldSingleUseMaterializedMulImmediate(MF, TII))
    Changed = true;

  while (foldSelfLoopIVCopy(MF, TII))
    Changed = true;

  while (foldDeferredLeaCopy(MF, TII))
    Changed = true;

  while (foldSingleUseConstPowerPlusOneMul(MF, TII))
    Changed = true;

  while (foldSelectFalseImmediateOp(MF, TII))
    Changed = true;

  while (foldHexDigitSelect(MF, TII))
    Changed = true;

  while (foldHexDigitPostExtendPair(MF, TII))
    Changed = true;

  while (foldCalleeSavedConstantReuse(MF, TII))
    Changed = true;

  if (foldCalleeSavedFramePaddingPair(MF, TII))
    Changed = true;

  if (foldCalleeSavedPaddingPair(MF, TII))
    Changed = true;

  while (foldGlobalBaseAbsAccesses(MF, TII))
    Changed = true;

  while (foldIntroducedGlobalBaseAbsAccesses(MF, TII))
    Changed = true;

  while (foldRepgCounterScratch(MF, TII))
    Changed = true;

  return Changed;
}

INITIALIZE_PASS(BedrockPreEmitPeephole, DEBUG_TYPE,
                BEDROCK_PRE_EMIT_PEEPHOLE_NAME, false, false)
char BedrockPreEmitPeephole::ID = 0;

FunctionPass *llvm::createBedrockPreEmitPeepholePass() {
  return new BedrockPreEmitPeephole();
}
