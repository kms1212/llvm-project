//===-- BedrockAsmPrinter.cpp - Bedrock assembly printer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Bedrock.h"
#include "BedrockTargetMachine.h"
#include "MCTargetDesc/BedrockFixupKinds.h"
#include "MCTargetDesc/BedrockInstPrinter.h"
#include "MCTargetDesc/BedrockMCEncoding.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "TargetInfo/BedrockTargetInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {

enum class MemAddrKind { Reg, RegIndex, RegOffset, Abs, Frame };

struct RepgStartInfo {
  Register CounterReg;
  uint16_t BodyBytes = 0;
};

struct RawExprFixup {
  unsigned Offset;
  MCFixupKind Kind;
  const MCExpr *Expr;
};

class BedrockAsmPrinter : public AsmPrinter {
public:
  static char ID;

  BedrockAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override { return "Bedrock Assembly Printer"; }
  bool runOnMachineFunction(MachineFunction &MF) override;
  void emitInstruction(const MachineInstr *MI) override;

private:
  DenseSet<const MachineInstr *> ShortBranches;
  DenseSet<const MachineInstr *> MediumBranches;
  DenseSet<const MachineInstr *> PostIncMemOps;
  DenseSet<const MachineInstr *> PostIncBinaryMemOps;
  DenseSet<const MachineInstr *> PostIncMemMoveSrcOps;
  DenseSet<const MachineInstr *> PostIncMemMoveDstOps;
  DenseMap<const MachineInstr *, const MachineInstr *> CmpTestJumpCmps;
  DenseSet<const MachineInstr *> CmpTestJumpBranches;
  DenseSet<const MachineInstr *> CmpTestJumpCompareInstrs;
  DenseSet<const MachineInstr *> CmpTestJump8Branches;
  DenseMap<const MachineInstr *, std::pair<Register, unsigned>> BitTestAnds;
  DenseSet<const MachineInstr *> BitTestSuppressedInstrs;
  DenseSet<const MachineInstr *> ZeroCopyClears;
  DenseSet<const MachineInstr *> ZeroMemStoreClears;
  DenseSet<const MachineInstr *> ZeroMemStores;
  DenseMap<const MachineInstr *, const MachineInstr *> ConstStoreStores;
  DenseSet<const MachineInstr *> ConstStoreConsts;
  DenseMap<const MachineInstr *, const MachineInstr *> MemMoveStores;
  DenseSet<const MachineInstr *> MemMoveLoads;
  DenseMap<const MachineInstr *, const MachineInstr *> DJBranches;
  DenseMap<const MachineInstr *, const MachineInstr *> DJTests;
  DenseSet<const MachineInstr *> DJCounterInstrs;
  DenseSet<const MachineInstr *> DJTestInstrs;
  DenseSet<const MachineInstr *> DJ8Branches;
  DenseSet<const MachineInstr *> DJ16Branches;
  DenseMap<const MachineInstr *, const MachineInstr *> IJBranches;
  DenseMap<const MachineInstr *, const MachineInstr *> IJCmps;
  DenseSet<const MachineInstr *> IJCounterInstrs;
  DenseSet<const MachineInstr *> IJCompareInstrs;
  DenseMap<const MachineInstr *, RepgStartInfo> RepgStarts;
  DenseSet<const MachineInstr *> RepgSuppressedInstrs;
  DenseSet<const MachineInstr *> RepgEndMarkers;
  DenseMap<const MachineInstr *, int64_t> ShortBranchDisplacements;
  DenseMap<const MachineInstr *, int64_t> MediumBranchDisplacements;
  DenseMap<const MachineInstr *, int64_t> CmpTestJumpDisplacements;
  DenseMap<const MachineInstr *, int64_t> DJDisplacements;
  MCOperand lowerOperand(const MachineOperand &MO) const;
  const MCExpr *lowerSymbolOperand(const MachineOperand &MO) const;

  void computeShortBranches(const MachineFunction &MF);
  void computeBlockOffsets(const MachineFunction &MF,
                           SmallVectorImpl<uint64_t> &BlockOffsets) const;
  unsigned getInstSizeForBranchLayout(const MachineInstr &MI) const;
  void collectCmpTestJumpBranches(const MachineFunction &MF);
  void collectZeroMemStores(const MachineFunction &MF);
  void collectConstStores(const MachineFunction &MF);
  void collectRepeatGroups(const MachineFunction &MF);
  bool tryCollectHeaderRepeatGroup(const MachineBasicBlock &HeaderMBB);
  bool tryCollectGuardedSelfRepeatGroup(const MachineBasicBlock &BodyMBB);
  bool computeRepeatGroupBodyBytes(const MachineBasicBlock &MBB,
                                   const MachineInstr *StartMI,
                                   const MachineInstr *EndMI,
                                   const MachineInstr *SkipMI,
                                   Register CounterReg, bool AllowCounterDefs,
                                   uint16_t &BodyBytes) const;

  void emitMCInst(const MCInst &Inst);
  void emitRaw(ArrayRef<uint8_t> Bytes);
  void emitRawExpr(ArrayRef<uint8_t> Bytes, ArrayRef<RawExprFixup> Fixups);
  void emitRawExpr(ArrayRef<uint8_t> Bytes, unsigned FixupOffset,
                   MCFixupKind Kind, const MCExpr *Expr);
  void emitRepgHeader(Register CounterReg, uint16_t BodyBytes);
  void emitRepMemset(const MachineInstr *MI);
  void emitRR(unsigned Opcode, Register DstReg, Register SrcReg);
  void emitConst(const MachineInstr *MI, bool Is64);
  void emitUnaryPseudo(const MachineInstr *MI, unsigned RealOpcode,
                       unsigned CopyOpcode);
  void emitLongCountPseudo(const MachineInstr *MI, StringRef Mnemonic,
                           StringRef Pattern, unsigned Size);
  void emitLongMulHighPseudo(const MachineInstr *MI, StringRef Mnemonic,
                             StringRef Pattern);
  void emitDivModPseudo(const MachineInstr *MI, bool IsSigned, unsigned Size);
  void emitExtractPseudo(const MachineInstr *MI, unsigned Size);
  void emitClearCarry();
  void emitCarryInstruction(Register DstReg, Register RHSReg,
                            StringRef Mnemonic, StringRef Pattern,
                            unsigned Size);
  void emitCarryPseudo(const MachineInstr *MI, StringRef Mnemonic,
                       StringRef Pattern, unsigned Size);
  void emitCarryStartPseudo(const MachineInstr *MI, bool IsAdd, unsigned Size);
  void emitFlagUnaryPseudo(const MachineInstr *MI, StringRef Mnemonic,
                           StringRef Pattern, unsigned Size);
  void emitExtQRegPseudo(const MachineInstr *MI, StringRef Pattern);
  void emitLongZeroMinMaxPseudo(const MachineInstr *MI, StringRef Pattern,
                                unsigned Size);
  void emitBinaryPseudo(const MachineInstr *MI, unsigned RealOpcode);
  void emitBinaryImmPseudo(const MachineInstr *MI, StringRef Pattern,
                           unsigned Size);
  void emitLongBinaryImmPseudo(const MachineInstr *MI, StringRef Pattern,
                               unsigned Size);
  void emitShiftImmPseudo(const MachineInstr *MI, StringRef Pattern,
                          unsigned Size);
  void emitBitImm(Register Reg, int64_t Imm, StringRef Mnemonic,
                  StringRef Pattern);
  void emitBTestImm(Register Reg, unsigned Bit);
  void emitBitImmPseudo(const MachineInstr *MI, StringRef Mnemonic,
                        StringRef Pattern, unsigned Size);
  void emitBSet2ImmPseudo(const MachineInstr *MI);
  void emitBinaryMemPseudo(const MachineInstr *MI, StringRef Pattern,
                           unsigned Size, bool IsLong, MemAddrKind AddrKind);
  void emitBinaryMemDestPseudo(const MachineInstr *MI, StringRef Pattern,
                               unsigned Size, bool IsLong,
                               MemAddrKind AddrKind);
  void emitLongBinaryPseudo(const MachineInstr *MI, StringRef Pattern,
                            unsigned Size);
  void emitCmpImmPseudo(const MachineInstr *MI, unsigned Size);
  void emitCmpMemPseudo(const MachineInstr *MI, unsigned Size,
                        MemAddrKind AddrKind, bool MemIsSrc);
  void emitCmpFrameFramePseudo(const MachineInstr *MI, unsigned Size);
  void emitMemMoveRegRegPseudo(const MachineInstr *MI, unsigned Size);
  void emitUnaryMemPseudo(const MachineInstr *MI, StringRef Pattern,
                          unsigned Size, MemAddrKind AddrKind);
  void emitUnaryFramePseudo(const MachineInstr *MI, StringRef Pattern,
                            unsigned Size);
  void emitUnaryAbsPseudo(const MachineInstr *MI, StringRef Pattern,
                          unsigned Size);
  void emitFrameAddress(const MachineInstr *MI);
  void emitAbsLoad(const MachineInstr *MI, unsigned Size, bool IsExt = false,
                   bool IsSigned = false);
  void emitAbsStore(const MachineInstr *MI, unsigned Size);
  void emitLoad(const MachineInstr *MI, unsigned Size, bool IsFrame,
                bool IsExt = false, bool IsSigned = false);
  void emitLoadOffset(const MachineInstr *MI, unsigned Size,
                      bool IsExt = false, bool IsSigned = false);
  void emitLoadIndex(const MachineInstr *MI, unsigned Size,
                     bool IsExt = false, bool IsSigned = false);
  void emitLoadSPIndex(const MachineInstr *MI, unsigned Size,
                       bool IsExt = false, bool IsSigned = false);
  void emitStore(const MachineInstr *MI, unsigned Size, bool IsFrame);
  void emitStoreIndex(const MachineInstr *MI, unsigned Size);
  void emitStoreSPIndex(const MachineInstr *MI, unsigned Size);
  void emitStoreOffset(const MachineInstr *MI, unsigned Size);
  void emitImmStore(const MachineInstr *MI, unsigned Size, bool IsFrame);
  void emitImmStoreOffset(const MachineInstr *MI, unsigned Size);
  void emitImmStoreAbs(const MachineInstr *MI, unsigned Size);
  void emitConstStoreFold(const MachineInstr *ConstMI,
                          const MachineInstr *StoreMI, unsigned Size,
                          MemAddrKind AddrKind);
  void emitZeroMemStore(const MachineInstr *MI, unsigned Size,
                        MemAddrKind AddrKind);
  void emitFpuMove(Register DstReg, Register SrcReg);
  void emitFpuMove(const MachineInstr *MI);
  void emitFpuClear(const MachineInstr *MI);
  void emitFpuConstant(const MachineInstr *MI);
  void emitFpuComparePseudo(const MachineInstr *MI, bool IsDouble);
  void emitFpuTestPseudo(const MachineInstr *MI, bool IsDouble);
  void emitFpuSelectPseudo(const MachineInstr *MI, bool IsDouble);
  void emitFpuSelectTestPseudo(const MachineInstr *MI, bool IsDouble);
  void emitFpuBinaryPseudo(const MachineInstr *MI, StringRef Mnemonic,
                           StringRef Pattern, bool IsDouble,
                           bool IsLong = false);
  void emitFpuCopySignPseudo(const MachineInstr *MI, bool IsDouble);
  void emitFpuUnaryPseudo(const MachineInstr *MI, StringRef Mnemonic,
                          StringRef Pattern, bool IsDouble,
                          bool IsLong = false);
  void emitFusedPseudo(const MachineInstr *MI, StringRef Mnemonic,
                       StringRef Pattern, bool IsDouble);
  void emitApproxUnaryPseudo(const MachineInstr *MI, StringRef Mnemonic,
                             StringRef Pattern, bool IsDouble);
  void emitSincosPseudo(const MachineInstr *MI, bool IsDouble);
  void emitFpuConvert(const MachineInstr *MI, StringRef Mnemonic,
                      StringRef Pattern, bool IsDouble, bool SrcIsFPR,
                      bool DstIsFPR);
  void emitFpuAbsLoad(const MachineInstr *MI, bool IsDouble);
  void emitFpuAbsStore(const MachineInstr *MI, bool IsDouble);
  void emitFpuLoad(const MachineInstr *MI, bool IsFrame, bool IsDouble);
  void emitFpuLoadOffset(const MachineInstr *MI, bool IsDouble);
  void emitFpuStore(const MachineInstr *MI, bool IsFrame, bool IsDouble);
  void emitFpuStoreOffset(const MachineInstr *MI, bool IsDouble);
  void emitStackAdjust(const MachineInstr *MI, bool IsDown);
  void emitBranch(const MachineInstr *MI, bool IsCond);
  void emitCmpTestJump(const MachineInstr *MI);
  void emitDJ(const MachineInstr *MI);
  void emitIJ(const MachineInstr *MI);
  void emitRegOffsetAddress(const MachineInstr *MI);
  void emitScaledIndexAddress(const MachineInstr *MI);
  void emitMemMoveFold(const MachineInstr *LoadMI,
                       const MachineInstr *StoreMI, unsigned Size,
                       MemAddrKind SrcKind, MemAddrKind DstKind);
  void emitSetCC(const MachineInstr *MI);
  void emitCall(const MachineInstr *MI);
  void emitTLSDescCall(const MachineInstr *MI);
  void emitTailCall(const MachineInstr *MI);
  void emitIndirectCall(const MachineInstr *MI);
  void emitIndirectJump(const MachineInstr *MI);
  void emitPublicIntrinsic(const MachineInstr *MI);
  void emitSystemIntrinsic(const MachineInstr *MI);
  void emitAtomic(const MachineInstr *MI);
};

} // namespace

static void appendLE(SmallVectorImpl<uint8_t> &Bytes, uint64_t Value,
                     unsigned Width) {
  for (unsigned I = 0; I != Width; ++I)
    Bytes.push_back((Value >> (I * 8)) & 0xff);
}

static bool appendSignedAuto(int64_t Value, SmallVectorImpl<uint8_t> &Tail,
                             unsigned &WidthCode) {
  if (isInt<8>(Value)) {
    WidthCode = 0;
    appendLE(Tail, static_cast<uint64_t>(Value), 1);
    return true;
  }
  if (isInt<16>(Value)) {
    WidthCode = 1;
    appendLE(Tail, static_cast<uint64_t>(Value), 2);
    return true;
  }
  if (isInt<32>(Value)) {
    WidthCode = 2;
    appendLE(Tail, static_cast<uint64_t>(Value), 4);
    return true;
  }

  WidthCode = 3;
  appendLE(Tail, static_cast<uint64_t>(Value), 8);
  return true;
}

static unsigned getSignedAutoSize(int64_t Value) {
  if (isInt<8>(Value))
    return 1;
  if (isInt<16>(Value))
    return 2;
  if (isInt<32>(Value))
    return 4;
  return 8;
}

static unsigned getGPRNo(Register Reg) {
  switch (Reg) {
  case Bedrock::R0:
    return 0;
  case Bedrock::R1:
    return 1;
  case Bedrock::R2:
    return 2;
  case Bedrock::R3:
    return 3;
  case Bedrock::R4:
    return 4;
  case Bedrock::R5:
    return 5;
  case Bedrock::R6:
    return 6;
  case Bedrock::R7:
    return 7;
  case Bedrock::R8:
    return 8;
  case Bedrock::R9:
    return 9;
  case Bedrock::R10:
    return 10;
  case Bedrock::R11:
    return 11;
  case Bedrock::R12:
    return 12;
  case Bedrock::R13:
    return 13;
  case Bedrock::R14:
    return 14;
  case Bedrock::R15:
    return 15;
  default:
    report_fatal_error("expected Bedrock GPR");
  }
}

static unsigned getFPRNo(Register Reg) {
  switch (Reg) {
  case Bedrock::F0:
    return 0;
  case Bedrock::F1:
    return 1;
  case Bedrock::F2:
    return 2;
  case Bedrock::F3:
    return 3;
  case Bedrock::F4:
    return 4;
  case Bedrock::F5:
    return 5;
  case Bedrock::F6:
    return 6;
  case Bedrock::F7:
    return 7;
  case Bedrock::F8:
    return 8;
  case Bedrock::F9:
    return 9;
  case Bedrock::F10:
    return 10;
  case Bedrock::F11:
    return 11;
  case Bedrock::F12:
    return 12;
  case Bedrock::F13:
    return 13;
  case Bedrock::F14:
    return 14;
  case Bedrock::F15:
    return 15;
  default:
    report_fatal_error("expected Bedrock FPR");
  }
}

static uint32_t applyPattern(StringRef Pattern, uint8_t EA, unsigned Z,
                             unsigned Reg, char RegField) {
  unsigned EBits = 0;
  unsigned ZBits = 0;
  unsigned RBits = 0;
  for (char C : Pattern) {
    if (C == 'e')
      ++EBits;
    else if (C == 'z')
      ++ZBits;
    else if (C == RegField)
      ++RBits;
  }

  uint32_t Payload = 0;
  unsigned Width = Pattern.size();
  for (unsigned I = 0; I != Width; ++I) {
    unsigned Bit = Width - I - 1;
    switch (Pattern[I]) {
    case '0':
      break;
    case '1':
      Payload |= 1u << Bit;
      break;
    case 'e':
      Payload |= ((EA >> --EBits) & 1) << Bit;
      break;
    case 'z':
      Payload |= ((Z >> --ZBits) & 1) << Bit;
      break;
    case 'd':
    case 's':
      assert(Pattern[I] == RegField && "unexpected register field");
      Payload |= ((Reg >> --RBits) & 1) << Bit;
      break;
    default:
      llvm_unreachable("unexpected Bedrock pattern field");
    }
  }
  return Payload;
}

struct PatternFieldValue {
  char Field;
  unsigned Value;
};

static uint32_t applyPatternValues(StringRef Pattern,
                                   ArrayRef<PatternFieldValue> FieldValues) {
  unsigned Counts[256] = {};
  unsigned Values[256] = {};
  bool Active[256] = {};

  for (const PatternFieldValue &Field : FieldValues) {
    Active[static_cast<unsigned char>(Field.Field)] = true;
    Values[static_cast<unsigned char>(Field.Field)] = Field.Value;
  }

  for (char C : Pattern) {
    unsigned Index = static_cast<unsigned char>(C);
    if (Active[Index])
      ++Counts[Index];
  }

  uint32_t Payload = 0;
  unsigned Width = Pattern.size();
  for (unsigned I = 0; I != Width; ++I) {
    unsigned Bit = Width - I - 1;
    char C = Pattern[I];
    if (C == '0')
      continue;
    if (C == '1') {
      Payload |= 1u << Bit;
      continue;
    }

    unsigned Index = static_cast<unsigned char>(C);
    assert(Active[Index] && "missing Bedrock pattern field");
    Payload |= ((Values[Index] >> --Counts[Index]) & 1) << Bit;
  }
  return Payload;
}

static uint64_t applyPatternValues64(
    StringRef Pattern, ArrayRef<PatternFieldValue> FieldValues) {
  unsigned Counts[256] = {};
  unsigned Values[256] = {};
  bool Active[256] = {};

  for (const PatternFieldValue &Field : FieldValues) {
    Active[static_cast<unsigned char>(Field.Field)] = true;
    Values[static_cast<unsigned char>(Field.Field)] = Field.Value;
  }
  for (char C : Pattern) {
    unsigned Index = static_cast<unsigned char>(C);
    if (Active[Index])
      ++Counts[Index];
  }

  uint64_t Payload = 0;
  unsigned Width = Pattern.size();
  for (unsigned I = 0; I != Width; ++I) {
    unsigned Bit = Width - I - 1;
    char C = Pattern[I];
    if (C == '0')
      continue;
    if (C == '1') {
      Payload |= UINT64_C(1) << Bit;
      continue;
    }
    unsigned Index = static_cast<unsigned char>(C);
    assert(Active[Index] && "missing Bedrock pattern field");
    Payload |= uint64_t((Values[Index] >> --Counts[Index]) & 1) << Bit;
  }
  return Payload;
}

static uint32_t getLeaPayload(uint8_t EA, unsigned Size, Register DstReg) {
  return applyPattern("0eeez1zdddd000eeee", EA, Size, getGPRNo(DstReg), 'd');
}

static const char *getCondSuffix(unsigned Cond) {
  switch (Cond) {
  case 0x2:
    return "eq";
  case 0x3:
    return "ne";
  case 0x4:
    return "ult";
  case 0x5:
    return "uge";
  case 0x6:
    return "mi";
  case 0x7:
    return "pl";
  case 0x8:
    return "vs";
  case 0x9:
    return "vc";
  case 0xa:
    return "ule";
  case 0xb:
    return "ugt";
  case 0xc:
    return "lt";
  case 0xd:
    return "ge";
  case 0xe:
    return "le";
  case 0xf:
    return "gt";
  default:
    report_fatal_error("invalid Bedrock condition code");
  }
}

static uint8_t getRegEA(Register Reg) { return getGPRNo(Reg); }

static bool getAtomicEncodingInfo(unsigned Opcode, unsigned &Size,
                                  StringRef &Pattern, bool &IsCmpXchg) {
  IsCmpXchg = false;
  switch (Opcode) {
#define ATOMIC_FETCH_CASES(OP, PATTERN)                                      \
  case Bedrock::ATOMIC_##OP##B:                                              \
    Size = 0;                                                               \
    Pattern = PATTERN;                                                       \
    return true;                                                            \
  case Bedrock::ATOMIC_##OP##W:                                              \
    Size = 1;                                                               \
    Pattern = PATTERN;                                                       \
    return true;                                                            \
  case Bedrock::ATOMIC_##OP##L:                                              \
    Size = 2;                                                               \
    Pattern = PATTERN;                                                       \
    return true;                                                            \
  case Bedrock::ATOMIC_##OP##Q:                                              \
    Size = 3;                                                               \
    Pattern = PATTERN;                                                       \
    return true
    ATOMIC_FETCH_CASES(FETCHADD, "111111000101zz000000ooosssseeeeeee");
    ATOMIC_FETCH_CASES(FETCHAND, "111111000101zz000001ooosssseeeeeee");
    ATOMIC_FETCH_CASES(FETCHOR, "111111000101zz000010ooosssseeeeeee");
    ATOMIC_FETCH_CASES(FETCHSUB, "111111000101zz000011ooosssseeeeeee");
    ATOMIC_FETCH_CASES(FETCHXOR, "111111000101zz000100ooosssseeeeeee");
#undef ATOMIC_FETCH_CASES
  case Bedrock::ATOMIC_CMPXCHG_B:
    Size = 0;
    break;
  case Bedrock::ATOMIC_CMPXCHG_W:
    Size = 1;
    break;
  case Bedrock::ATOMIC_CMPXCHG_L:
    Size = 2;
    break;
  case Bedrock::ATOMIC_CMPXCHG_Q:
    Size = 3;
    break;
  default:
    return false;
  }
  Pattern = "111111000101zz01xxxxoooddddeeeeeee";
  IsCmpXchg = true;
  return true;
}

static void getMemEAForReg(Register BaseReg, uint8_t &EA,
                           SmallVectorImpl<uint8_t> &Tail) {
  EA = 0x10 | getGPRNo(BaseReg);
}

static void getMemEAForRegPostInc(Register BaseReg, uint8_t &EA,
                                  SmallVectorImpl<uint8_t> &Tail) {
  EA = 0x74;
  Tail.push_back(0x84 | (getGPRNo(BaseReg) << 3));
}

static void getMemEAForRegIndex(Register BaseReg, Register IndexReg,
                                uint8_t &EA,
                                SmallVectorImpl<uint8_t> &Tail) {
  EA = 0x74;
  Tail.push_back(0x82);
  Tail.push_back((getGPRNo(BaseReg) << 4) | getGPRNo(IndexReg));
}

static void getMemEAForSPIndex(Register IndexReg, uint8_t &EA,
                               SmallVectorImpl<uint8_t> &Tail) {
  EA = 0x74;
  Tail.push_back(0x8a);
  Tail.push_back(0x20 | getGPRNo(IndexReg));
}

static void getMemEAForRegOffset(Register BaseReg, int64_t Offset, uint8_t &EA,
                                 SmallVectorImpl<uint8_t> &Tail) {
  if (Offset == 0) {
    getMemEAForReg(BaseReg, EA, Tail);
    return;
  }

  unsigned WidthCode;
  appendSignedAuto(Offset, Tail, WidthCode);
  EA = 0x20 + (WidthCode << 4) + getGPRNo(BaseReg);
}

static void getMemEAForRegSymbol(Register BaseReg, uint8_t &EA,
                                 SmallVectorImpl<uint8_t> &Tail) {
  Tail.append(4, 0);
  EA = 0x20 + (2 << 4) + getGPRNo(BaseReg);
}

static bool isSymbolicAddressOperand(const MachineOperand &MO) {
  switch (MO.getType()) {
  case MachineOperand::MO_MachineBasicBlock:
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

static void getMemEAForSP(int64_t Offset, uint8_t &EA,
                          SmallVectorImpl<uint8_t> &Tail) {
  if (Offset == 0) {
    EA = 0x69;
    return;
  }

  unsigned WidthCode;
  appendSignedAuto(Offset, Tail, WidthCode);
  EA = 0x60 + WidthCode;
}

static int64_t getFrameOffset(const MachineInstr *MI, unsigned BaseOp) {
  return MI->getOperand(BaseOp).getImm() + MI->getOperand(BaseOp + 1).getImm();
}

static void getMemEAFromOperands(const MachineInstr *MI, unsigned BaseOp,
                                 MemAddrKind AddrKind, uint8_t &EA,
                                 SmallVectorImpl<uint8_t> &Tail) {
  switch (AddrKind) {
  case MemAddrKind::Reg:
    getMemEAForReg(MI->getOperand(BaseOp).getReg(), EA, Tail);
    return;
  case MemAddrKind::RegIndex:
    getMemEAForRegIndex(MI->getOperand(BaseOp).getReg(),
                        MI->getOperand(BaseOp + 1).getReg(), EA, Tail);
    return;
  case MemAddrKind::RegOffset:
    getMemEAForRegOffset(MI->getOperand(BaseOp).getReg(),
                         MI->getOperand(BaseOp + 1).getImm(), EA, Tail);
    return;
  case MemAddrKind::Abs:
    EA = 0x6a;
    Tail.append(4, 0);
    return;
  case MemAddrKind::Frame:
    getMemEAForSP(getFrameOffset(MI, BaseOp), EA, Tail);
    return;
  }
  llvm_unreachable("unknown Bedrock memory address kind");
}

static uint32_t getMovPayload(bool IsLoad, unsigned Size, uint8_t EA,
                              Register Reg) {
  bool IsLQ = Size >= 2;
  StringRef Pattern;
  if (IsLoad)
    Pattern = IsLQ ? "000011zddddeeeeeee" : "000010zddddeeeeeee";
  else
    Pattern = IsLQ ? "000001zsssseeeeeee" : "000000zsssseeeeeee";
  return applyPattern(Pattern, EA, Size & 1, getGPRNo(Reg), IsLoad ? 'd' : 's');
}

static uint32_t getBinaryImmPayload(StringRef Pattern, unsigned Size,
                                    uint8_t EA, Register DstReg) {
  return applyPattern(Pattern, EA, Size & 1, getGPRNo(DstReg), 'd');
}

static uint32_t getExtLoadPayload(unsigned Size, bool IsSigned, uint8_t EA,
                                  Register DstReg) {
  StringRef Pattern;
  if (IsSigned) {
    switch (Size) {
    case 0:
      Pattern = "1100011ddddeeeeeee";
      break;
    case 1:
      Pattern = "1100101ddddeeeeeee";
      break;
    case 2:
      Pattern = "1100111ddddeeeeeee";
      break;
    default:
      report_fatal_error("invalid Bedrock signed-ext load size");
    }
  } else {
    switch (Size) {
    case 0:
      Pattern = "1101011ddddeeeeeee";
      break;
    case 1:
      Pattern = "1101101ddddeeeeeee";
      break;
    case 2:
      Pattern = "1101111ddddeeeeeee";
      break;
    default:
      report_fatal_error("invalid Bedrock zero-ext load size");
    }
  }

  return applyPattern(Pattern, EA, 0, getGPRNo(DstReg), 'd');
}

static char getSizeSuffix(unsigned Size);

static unsigned getImmStoreOpcodeForStore(unsigned StoreOpc) {
  switch (StoreOpc) {
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

static bool foldConstStorePair(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator &I,
                               const TargetInstrInfo &TII) {
  MachineInstr &ConstMI = *I;
  if (ConstMI.getOpcode() != Bedrock::CONST32 &&
      ConstMI.getOpcode() != Bedrock::CONST64)
    return false;
  if (!ConstMI.getOperand(1).isImm())
    return false;

  int64_t Imm = ConstMI.getOperand(1).getImm();
  if (Imm == 0)
    return false;

  Register Reg = ConstMI.getOperand(0).getReg();
  auto StoreI = std::next(I);
  while (StoreI != MBB.end() && StoreI->isDebugInstr())
    ++StoreI;
  if (StoreI == MBB.end())
    return false;

  unsigned ImmStoreOpc = getImmStoreOpcodeForStore(StoreI->getOpcode());
  if (!ImmStoreOpc || StoreI->getOperand(0).getReg() != Reg ||
      !StoreI->getOperand(0).isKill())
    return false;

  auto NextI = std::next(StoreI);
  MachineInstrBuilder MIB =
      BuildMI(MBB, StoreI, StoreI->getDebugLoc(), TII.get(ImmStoreOpc))
          .addImm(Imm);
  for (unsigned OpIdx = 1, OpEnd = StoreI->getNumOperands(); OpIdx != OpEnd;
       ++OpIdx)
    MIB.add(StoreI->getOperand(OpIdx));
  MIB.cloneMemRefs(*StoreI);

  StoreI->eraseFromParent();
  ConstMI.eraseFromParent();
  I = NextI;
  return true;
}

static unsigned getFrameStoreOpcodeForLoad(unsigned LoadOpc) {
  switch (LoadOpc) {
  case Bedrock::LOADB_Zfi:
  case Bedrock::LOADB_Sfi:
    return Bedrock::STOREBfi;
  case Bedrock::LOADW_Zfi:
  case Bedrock::LOADW_Sfi:
    return Bedrock::STOREWfi;
  case Bedrock::LOADL_Zfi:
  case Bedrock::LOADL_Sfi:
  case Bedrock::LOADLfi:
    return Bedrock::STORELfi;
  case Bedrock::LOADQfi:
    return Bedrock::STOREQfi;
  default:
    return 0;
  }
}

static bool hasVolatileMemOperand(const MachineInstr &MI) {
  return any_of(MI.memoperands(),
                [](MachineMemOperand *MMO) { return MMO->isVolatile(); });
}

static unsigned getCmpMemOpcode(unsigned CmpOpc, MemAddrKind AddrKind,
                                bool MemIsSrc);

static bool foldRedundantFrameLoadStorePair(MachineBasicBlock &MBB,
                                            MachineBasicBlock::iterator &I) {
  MachineInstr &LoadMI = *I;
  unsigned StoreOpc = getFrameStoreOpcodeForLoad(LoadMI.getOpcode());
  if (!StoreOpc || hasVolatileMemOperand(LoadMI))
    return false;

  Register Reg = LoadMI.getOperand(0).getReg();
  auto StoreI = std::next(I);
  while (StoreI != MBB.end() && StoreI->isDebugInstr())
    ++StoreI;
  if (StoreI == MBB.end() || StoreI->getOpcode() != StoreOpc ||
      hasVolatileMemOperand(*StoreI))
    return false;

  if (StoreI->getOperand(0).getReg() != Reg ||
      !StoreI->getOperand(0).isKill() ||
      !LoadMI.getOperand(1).isIdenticalTo(StoreI->getOperand(1)) ||
      !LoadMI.getOperand(2).isIdenticalTo(StoreI->getOperand(2)))
    return false;

  auto NextI = std::next(StoreI);
  StoreI->eraseFromParent();
  LoadMI.eraseFromParent();
  I = NextI;
  return true;
}

static unsigned getFrameIncDecOpcode(unsigned LoadOpc, unsigned UnaryOpc,
                                     unsigned StoreOpc) {
  bool IsLongSlot = LoadOpc == Bedrock::LOADLfi && StoreOpc == Bedrock::STORELfi;
  bool IsQuadSlot = LoadOpc == Bedrock::LOADQfi && StoreOpc == Bedrock::STOREQfi;
  if (!IsLongSlot && !IsQuadSlot)
    return 0;

  switch (UnaryOpc) {
  case Bedrock::INCL3r:
    return IsLongSlot ? Bedrock::INCLfi : 0;
  case Bedrock::INCQ3r:
    return IsQuadSlot ? Bedrock::INCQfi : 0;
  case Bedrock::DECL3r:
    return IsLongSlot ? Bedrock::DECLfi : 0;
  case Bedrock::DECQ3r:
    return IsQuadSlot ? Bedrock::DECQfi : 0;
  default:
    return 0;
  }
}

static bool foldFrameIncDecStorePair(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator &I,
                                     const TargetInstrInfo &TII) {
  MachineInstr &LoadMI = *I;
  if (hasVolatileMemOperand(LoadMI))
    return false;

  auto UnaryI = std::next(I);
  while (UnaryI != MBB.end() && UnaryI->isDebugInstr())
    ++UnaryI;
  if (UnaryI == MBB.end())
    return false;

  auto StoreI = std::next(UnaryI);
  while (StoreI != MBB.end() && StoreI->isDebugInstr())
    ++StoreI;
  if (StoreI == MBB.end() || hasVolatileMemOperand(*StoreI))
    return false;

  unsigned MemUnaryOpc =
      getFrameIncDecOpcode(LoadMI.getOpcode(), UnaryI->getOpcode(),
                           StoreI->getOpcode());
  if (!MemUnaryOpc)
    return false;

  Register Reg = LoadMI.getOperand(0).getReg();
  if (UnaryI->getOperand(0).getReg() != Reg ||
      UnaryI->getOperand(1).getReg() != Reg ||
      StoreI->getOperand(0).getReg() != Reg ||
      !LoadMI.getOperand(1).isIdenticalTo(StoreI->getOperand(1)) ||
      !LoadMI.getOperand(2).isIdenticalTo(StoreI->getOperand(2)))
    return false;

  auto buildMemUnary = [&]() {
    MachineInstrBuilder MIB =
        BuildMI(MBB, LoadMI, UnaryI->getDebugLoc(), TII.get(MemUnaryOpc))
            .add(LoadMI.getOperand(1))
            .add(LoadMI.getOperand(2));
    MIB.cloneMemRefs(LoadMI);
    MIB.cloneMemRefs(*StoreI);
  };

  if (!StoreI->getOperand(0).isKill()) {
    auto CmpI = std::next(StoreI);
    while (CmpI != MBB.end() && CmpI->isDebugInstr())
      ++CmpI;
    if (CmpI == MBB.end())
      return false;

    if (hasVolatileMemOperand(*CmpI))
      return false;

    MachineBasicBlock::iterator OtherLoadI;
    bool HasOtherLoad = false;
    if (CmpI->getOpcode() != Bedrock::CMPLrr &&
        CmpI->getOpcode() != Bedrock::CMPQrr) {
      OtherLoadI = CmpI;
      HasOtherLoad = true;
      if (hasVolatileMemOperand(*OtherLoadI))
        return false;
      CmpI = std::next(OtherLoadI);
      while (CmpI != MBB.end() && CmpI->isDebugInstr())
        ++CmpI;
      if (CmpI == MBB.end() ||
          (CmpI->getOpcode() != Bedrock::CMPLrr &&
           CmpI->getOpcode() != Bedrock::CMPQrr))
        return false;
    }

    unsigned CmpMemOpc = 0;
    Register OtherReg;
    bool MemIsSrc;
    if (CmpI->getOperand(0).getReg() == Reg &&
        CmpI->getOperand(0).isKill() && CmpI->getOperand(1).getReg() != Reg) {
      OtherReg = CmpI->getOperand(1).getReg();
      MemIsSrc = true;
    } else if (CmpI->getOperand(1).getReg() == Reg &&
               CmpI->getOperand(1).isKill() &&
               CmpI->getOperand(0).getReg() != Reg) {
      OtherReg = CmpI->getOperand(0).getReg();
      MemIsSrc = false;
    } else {
      return false;
    }

    if (HasOtherLoad) {
      switch (OtherLoadI->getOpcode()) {
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
      case Bedrock::LOADB_Zfi:
      case Bedrock::LOADW_Zfi:
      case Bedrock::LOADL_Zfi:
      case Bedrock::LOADB_Sfi:
      case Bedrock::LOADW_Sfi:
      case Bedrock::LOADL_Sfi:
      case Bedrock::LOADLfi:
      case Bedrock::LOADQfi:
        break;
      default:
        return false;
      }
      if (OtherLoadI->getOperand(0).getReg() != OtherReg)
        return false;
    }

    CmpMemOpc =
        getCmpMemOpcode(CmpI->getOpcode(), MemAddrKind::Frame, MemIsSrc);
    if (!CmpMemOpc)
      return false;

    auto NextI = std::next(CmpI);
    buildMemUnary();
    MachineInstrBuilder CmpMIB =
        BuildMI(MBB, CmpI, CmpI->getDebugLoc(), TII.get(CmpMemOpc))
            .setMIFlags(CmpI->getFlags());
    if (MemIsSrc) {
      CmpMIB.add(LoadMI.getOperand(1))
          .add(LoadMI.getOperand(2))
          .add(CmpI->getOperand(1));
    } else {
      CmpMIB.add(CmpI->getOperand(0))
          .add(LoadMI.getOperand(1))
          .add(LoadMI.getOperand(2));
    }
    CmpMIB.cloneMemRefs(LoadMI);

    CmpI->eraseFromParent();
    StoreI->eraseFromParent();
    UnaryI->eraseFromParent();
    LoadMI.eraseFromParent();
    I = HasOtherLoad ? OtherLoadI : NextI;
    return true;
  }

  auto NextI = std::next(StoreI);
  buildMemUnary();

  StoreI->eraseFromParent();
  UnaryI->eraseFromParent();
  LoadMI.eraseFromParent();
  I = NextI;
  return true;
}

enum class SimpleMemAddrKind { Reg, RegOffset, Abs };

struct MemIncDecFoldInfo {
  unsigned StoreOpc;
  unsigned RegOpc;
  unsigned RegOffsetOpc;
  unsigned AbsOpc;
  bool Is64;
  SimpleMemAddrKind AddrKind;
};

static bool getMemIncDecFoldInfo(unsigned LoadOpc, unsigned UnaryOpc,
                                 MemIncDecFoldInfo &Info) {
  bool Is64 = false;
  SimpleMemAddrKind AddrKind;
  unsigned StoreOpc = 0;
  switch (LoadOpc) {
  case Bedrock::LOADL_Zrr:
  case Bedrock::LOADL_Srr:
  case Bedrock::LOADLrr:
    StoreOpc = Bedrock::STORELrr;
    AddrKind = SimpleMemAddrKind::Reg;
    break;
  case Bedrock::LOADQrr:
    StoreOpc = Bedrock::STOREQrr;
    AddrKind = SimpleMemAddrKind::Reg;
    Is64 = true;
    break;
  case Bedrock::LOADL_Zro:
  case Bedrock::LOADL_Sro:
  case Bedrock::LOADLro:
    StoreOpc = Bedrock::STORELro;
    AddrKind = SimpleMemAddrKind::RegOffset;
    break;
  case Bedrock::LOADQro:
    StoreOpc = Bedrock::STOREQro;
    AddrKind = SimpleMemAddrKind::RegOffset;
    Is64 = true;
    break;
  case Bedrock::LOADL_Zabs:
  case Bedrock::LOADL_Sabs:
  case Bedrock::LOADLabs:
    StoreOpc = Bedrock::STORELabs;
    AddrKind = SimpleMemAddrKind::Abs;
    break;
  case Bedrock::LOADQabs:
    StoreOpc = Bedrock::STOREQabs;
    AddrKind = SimpleMemAddrKind::Abs;
    Is64 = true;
    break;
  default:
    return false;
  }

  switch (UnaryOpc) {
  case Bedrock::INCL3r:
    if (Is64)
      return false;
    Info = {StoreOpc, Bedrock::INCLm, Bedrock::INCLmo, Bedrock::INCLabs,
            Is64, AddrKind};
    return true;
  case Bedrock::INCQ3r:
    if (!Is64)
      return false;
    Info = {StoreOpc, Bedrock::INCQm, Bedrock::INCQmo, Bedrock::INCQabs,
            Is64, AddrKind};
    return true;
  case Bedrock::DECL3r:
    if (Is64)
      return false;
    Info = {StoreOpc, Bedrock::DECLm, Bedrock::DECLmo, Bedrock::DECLabs,
            Is64, AddrKind};
    return true;
  case Bedrock::DECQ3r:
    if (!Is64)
      return false;
    Info = {StoreOpc, Bedrock::DECQm, Bedrock::DECQmo, Bedrock::DECQabs,
            Is64, AddrKind};
    return true;
  default:
    return false;
  }
}

static unsigned getMemIncDecOpcode(const MemIncDecFoldInfo &Info) {
  switch (Info.AddrKind) {
  case SimpleMemAddrKind::Reg:
    return Info.RegOpc;
  case SimpleMemAddrKind::RegOffset:
    return Info.RegOffsetOpc;
  case SimpleMemAddrKind::Abs:
    return Info.AbsOpc;
  }
  llvm_unreachable("unknown Bedrock memory inc/dec address kind");
}

static bool memIncDecAddressMatches(const MachineInstr &LoadMI,
                                    const MachineInstr &StoreMI,
                                    SimpleMemAddrKind AddrKind) {
  switch (AddrKind) {
  case SimpleMemAddrKind::Reg:
    return LoadMI.getOperand(1).isIdenticalTo(StoreMI.getOperand(1));
  case SimpleMemAddrKind::RegOffset:
    return LoadMI.getOperand(1).isIdenticalTo(StoreMI.getOperand(1)) &&
           LoadMI.getOperand(2).isIdenticalTo(StoreMI.getOperand(2));
  case SimpleMemAddrKind::Abs:
    return LoadMI.getOperand(1).isIdenticalTo(StoreMI.getOperand(1));
  }
  llvm_unreachable("unknown Bedrock memory inc/dec address kind");
}

static bool memIncDecAddressUsesReg(const MachineInstr &LoadMI,
                                    SimpleMemAddrKind AddrKind, Register Reg) {
  switch (AddrKind) {
  case SimpleMemAddrKind::Reg:
  case SimpleMemAddrKind::RegOffset:
    return LoadMI.getOperand(1).getReg() == Reg;
  case SimpleMemAddrKind::Abs:
    return false;
  }
  llvm_unreachable("unknown Bedrock memory inc/dec address kind");
}

static void addMemIncDecAddressOperands(MachineInstrBuilder &MIB,
                                        const MachineInstr &LoadMI,
                                        SimpleMemAddrKind AddrKind) {
  switch (AddrKind) {
  case SimpleMemAddrKind::Reg:
    MIB.add(LoadMI.getOperand(1));
    return;
  case SimpleMemAddrKind::RegOffset:
    MIB.add(LoadMI.getOperand(1)).add(LoadMI.getOperand(2));
    return;
  case SimpleMemAddrKind::Abs:
    MIB.add(LoadMI.getOperand(1));
    return;
  }
  llvm_unreachable("unknown Bedrock memory inc/dec address kind");
}

static bool foldSimpleMemIncDecStorePair(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator &I,
                                         const TargetInstrInfo &TII) {
  MachineInstr &LoadMI = *I;
  if (hasVolatileMemOperand(LoadMI))
    return false;

  auto UnaryI = std::next(I);
  while (UnaryI != MBB.end() && UnaryI->isDebugInstr())
    ++UnaryI;
  if (UnaryI == MBB.end() || hasVolatileMemOperand(*UnaryI))
    return false;

  MemIncDecFoldInfo Info;
  if (!getMemIncDecFoldInfo(LoadMI.getOpcode(), UnaryI->getOpcode(), Info))
    return false;

  auto StoreI = std::next(UnaryI);
  while (StoreI != MBB.end() && StoreI->isDebugInstr())
    ++StoreI;
  if (StoreI == MBB.end() || StoreI->getOpcode() != Info.StoreOpc ||
      hasVolatileMemOperand(*StoreI))
    return false;

  Register Reg = LoadMI.getOperand(0).getReg();
  if (UnaryI->getOperand(0).getReg() != Reg ||
      UnaryI->getOperand(1).getReg() != Reg ||
      StoreI->getOperand(0).getReg() != Reg ||
      !StoreI->getOperand(0).isKill() ||
      !memIncDecAddressMatches(LoadMI, *StoreI, Info.AddrKind) ||
      memIncDecAddressUsesReg(LoadMI, Info.AddrKind, Reg))
    return false;

  auto NextI = std::next(StoreI);
  MachineInstrBuilder MIB =
      BuildMI(MBB, LoadMI, UnaryI->getDebugLoc(),
              TII.get(getMemIncDecOpcode(Info)));
  addMemIncDecAddressOperands(MIB, LoadMI, Info.AddrKind);
  MIB.cloneMemRefs(LoadMI);
  MIB.cloneMemRefs(*StoreI);

  StoreI->eraseFromParent();
  UnaryI->eraseFromParent();
  LoadMI.eraseFromParent();
  I = NextI;
  return true;
}

struct BinaryMemFoldInfo {
  unsigned RegOpc;
  unsigned RegOffsetOpc;
  unsigned FrameOpc;
  bool Is64;
};

static bool getBinaryMemFoldInfo(unsigned Opc, BinaryMemFoldInfo &Info) {
  switch (Opc) {
  case Bedrock::ADDL3rr:
    Info = {Bedrock::ADDL3rm, Bedrock::ADDL3rmo, Bedrock::ADDL3rmfi, false};
    return true;
  case Bedrock::ADDQ3rr:
    Info = {Bedrock::ADDQ3rm, Bedrock::ADDQ3rmo, Bedrock::ADDQ3rmfi, true};
    return true;
  case Bedrock::SUBL3rr:
    Info = {Bedrock::SUBL3rm, Bedrock::SUBL3rmo, Bedrock::SUBL3rmfi, false};
    return true;
  case Bedrock::SUBQ3rr:
    Info = {Bedrock::SUBQ3rm, Bedrock::SUBQ3rmo, Bedrock::SUBQ3rmfi, true};
    return true;
  case Bedrock::ANDL3rr:
    Info = {Bedrock::ANDL3rm, Bedrock::ANDL3rmo, Bedrock::ANDL3rmfi, false};
    return true;
  case Bedrock::ANDQ3rr:
    Info = {Bedrock::ANDQ3rm, Bedrock::ANDQ3rmo, Bedrock::ANDQ3rmfi, true};
    return true;
  case Bedrock::ORL3rr:
    Info = {Bedrock::ORL3rm, Bedrock::ORL3rmo, Bedrock::ORL3rmfi, false};
    return true;
  case Bedrock::ORQ3rr:
    Info = {Bedrock::ORQ3rm, Bedrock::ORQ3rmo, Bedrock::ORQ3rmfi, true};
    return true;
  case Bedrock::XORL3rr:
    Info = {Bedrock::XORL3rm, Bedrock::XORL3rmo, Bedrock::XORL3rmfi, false};
    return true;
  case Bedrock::XORQ3rr:
    Info = {Bedrock::XORQ3rm, Bedrock::XORQ3rmo, Bedrock::XORQ3rmfi, true};
    return true;
  case Bedrock::MULL3rr:
    Info = {Bedrock::MULL3rm, Bedrock::MULL3rmo, Bedrock::MULL3rmfi, false};
    return true;
  case Bedrock::MULQ3rr:
    Info = {Bedrock::MULQ3rm, Bedrock::MULQ3rmo, Bedrock::MULQ3rmfi, true};
    return true;
  case Bedrock::MINUL3rr:
    Info = {Bedrock::MINUL3rm, Bedrock::MINUL3rmo, Bedrock::MINUL3rmfi,
            false};
    return true;
  case Bedrock::MINUQ3rr:
    Info = {Bedrock::MINUQ3rm, Bedrock::MINUQ3rmo, Bedrock::MINUQ3rmfi, true};
    return true;
  case Bedrock::MINSL3rr:
    Info = {Bedrock::MINSL3rm, Bedrock::MINSL3rmo, Bedrock::MINSL3rmfi,
            false};
    return true;
  case Bedrock::MINSQ3rr:
    Info = {Bedrock::MINSQ3rm, Bedrock::MINSQ3rmo, Bedrock::MINSQ3rmfi, true};
    return true;
  case Bedrock::MAXUL3rr:
    Info = {Bedrock::MAXUL3rm, Bedrock::MAXUL3rmo, Bedrock::MAXUL3rmfi,
            false};
    return true;
  case Bedrock::MAXUQ3rr:
    Info = {Bedrock::MAXUQ3rm, Bedrock::MAXUQ3rmo, Bedrock::MAXUQ3rmfi, true};
    return true;
  case Bedrock::MAXSL3rr:
    Info = {Bedrock::MAXSL3rm, Bedrock::MAXSL3rmo, Bedrock::MAXSL3rmfi,
            false};
    return true;
  case Bedrock::MAXSQ3rr:
    Info = {Bedrock::MAXSQ3rm, Bedrock::MAXSQ3rmo, Bedrock::MAXSQ3rmfi, true};
    return true;
  case Bedrock::DIVUL3rr:
    Info = {Bedrock::DIVUL3rm, Bedrock::DIVUL3rmo, Bedrock::DIVUL3rmfi,
            false};
    return true;
  case Bedrock::DIVUQ3rr:
    Info = {Bedrock::DIVUQ3rm, Bedrock::DIVUQ3rmo, Bedrock::DIVUQ3rmfi, true};
    return true;
  case Bedrock::DIVSL3rr:
    Info = {Bedrock::DIVSL3rm, Bedrock::DIVSL3rmo, Bedrock::DIVSL3rmfi,
            false};
    return true;
  case Bedrock::DIVSQ3rr:
    Info = {Bedrock::DIVSQ3rm, Bedrock::DIVSQ3rmo, Bedrock::DIVSQ3rmfi, true};
    return true;
  case Bedrock::MODUL3rr:
    Info = {Bedrock::MODUL3rm, Bedrock::MODUL3rmo, Bedrock::MODUL3rmfi,
            false};
    return true;
  case Bedrock::MODUQ3rr:
    Info = {Bedrock::MODUQ3rm, Bedrock::MODUQ3rmo, Bedrock::MODUQ3rmfi, true};
    return true;
  case Bedrock::MODSL3rr:
    Info = {Bedrock::MODSL3rm, Bedrock::MODSL3rmo, Bedrock::MODSL3rmfi,
            false};
    return true;
  case Bedrock::MODSQ3rr:
    Info = {Bedrock::MODSQ3rm, Bedrock::MODSQ3rmo, Bedrock::MODSQ3rmfi, true};
    return true;
  default:
    return false;
  }
}

static bool isCommutativeBinaryMemFoldOpcode(unsigned Opc) {
  switch (Opc) {
  case Bedrock::ADDL3rr:
  case Bedrock::ADDQ3rr:
  case Bedrock::ANDL3rr:
  case Bedrock::ANDQ3rr:
  case Bedrock::ORL3rr:
  case Bedrock::ORQ3rr:
  case Bedrock::XORL3rr:
  case Bedrock::XORQ3rr:
  case Bedrock::MULL3rr:
  case Bedrock::MULQ3rr:
  case Bedrock::MINUL3rr:
  case Bedrock::MINUQ3rr:
  case Bedrock::MINSL3rr:
  case Bedrock::MINSQ3rr:
  case Bedrock::MAXUL3rr:
  case Bedrock::MAXUQ3rr:
  case Bedrock::MAXSL3rr:
  case Bedrock::MAXSQ3rr:
    return true;
  default:
    return false;
  }
}

static bool getLoadAddrKindForBinaryFold(const MachineInstr &LoadMI, bool Is64,
                                         MemAddrKind &AddrKind) {
  switch (LoadMI.getOpcode()) {
  case Bedrock::LOADL_Zrr:
  case Bedrock::LOADL_Srr:
  case Bedrock::LOADLrr:
    if (Is64)
      return false;
    AddrKind = MemAddrKind::Reg;
    return true;
  case Bedrock::LOADQrr:
    AddrKind = MemAddrKind::Reg;
    return true;
  case Bedrock::LOADL_Zrx:
  case Bedrock::LOADL_Srx:
  case Bedrock::LOADLrx:
    if (Is64)
      return false;
    AddrKind = MemAddrKind::RegIndex;
    return true;
  case Bedrock::LOADQrx:
    AddrKind = MemAddrKind::RegIndex;
    return true;
  case Bedrock::LOADL_Zro:
  case Bedrock::LOADL_Sro:
  case Bedrock::LOADLro:
    if (Is64)
      return false;
    AddrKind = MemAddrKind::RegOffset;
    return true;
  case Bedrock::LOADQro:
    AddrKind = MemAddrKind::RegOffset;
    return true;
  case Bedrock::LOADL_Zabs:
  case Bedrock::LOADL_Sabs:
  case Bedrock::LOADLabs:
    if (Is64)
      return false;
    AddrKind = MemAddrKind::Abs;
    return true;
  case Bedrock::LOADQabs:
    AddrKind = MemAddrKind::Abs;
    return true;
  case Bedrock::LOADL_Zfi:
  case Bedrock::LOADL_Sfi:
  case Bedrock::LOADLfi:
    if (Is64)
      return false;
    AddrKind = MemAddrKind::Frame;
    return true;
  case Bedrock::LOADQfi:
    AddrKind = MemAddrKind::Frame;
    return true;
  default:
    return false;
  }
}

static unsigned getBinaryMemOpcode(const BinaryMemFoldInfo &Info,
                                   MemAddrKind AddrKind) {
  switch (AddrKind) {
  case MemAddrKind::Reg:
    return Info.RegOpc;
  case MemAddrKind::RegIndex:
    return 0;
  case MemAddrKind::RegOffset:
    return Info.RegOffsetOpc;
  case MemAddrKind::Abs:
    return 0;
  case MemAddrKind::Frame:
    return Info.FrameOpc;
  }
  llvm_unreachable("unknown Bedrock memory address kind");
}

static bool loadAddressUsesReg(const MachineInstr &LoadMI,
                               MemAddrKind AddrKind, Register Reg) {
  switch (AddrKind) {
  case MemAddrKind::Reg:
    return LoadMI.getOperand(1).getReg() == Reg;
  case MemAddrKind::RegIndex:
    return LoadMI.getOperand(1).getReg() == Reg ||
           LoadMI.getOperand(2).getReg() == Reg;
  case MemAddrKind::RegOffset:
    return LoadMI.getOperand(1).getReg() == Reg;
  case MemAddrKind::Abs:
    return false;
  case MemAddrKind::Frame:
    return false;
  }
  llvm_unreachable("unknown Bedrock memory address kind");
}

static bool loadFeedsFoldableBinaryMemSource(MachineBasicBlock::iterator LoadI,
                                             MachineBasicBlock &MBB) {
  auto BinI = std::next(LoadI);
  while (BinI != MBB.end() && BinI->isDebugInstr())
    ++BinI;
  if (BinI == MBB.end())
    return false;

  BinaryMemFoldInfo Info;
  if (!getBinaryMemFoldInfo(BinI->getOpcode(), Info))
    return false;

  MemAddrKind AddrKind;
  if (!getLoadAddrKindForBinaryFold(*LoadI, Info.Is64, AddrKind))
    return false;
  if (!getBinaryMemOpcode(Info, AddrKind))
    return false;

  Register LoadReg = LoadI->getOperand(0).getReg();
  Register DstReg = BinI->getOperand(0).getReg();
  Register LHSReg = BinI->getOperand(1).getReg();
  Register RHSReg = BinI->getOperand(2).getReg();
  if (DstReg != LHSReg)
    return false;

  if (DstReg != LoadReg && RHSReg == LoadReg && BinI->getOperand(2).isKill())
    return true;

  return DstReg == LoadReg && RHSReg != LoadReg &&
         BinI->getOperand(2).isKill() &&
         isCommutativeBinaryMemFoldOpcode(BinI->getOpcode()) &&
         !loadAddressUsesReg(*LoadI, AddrKind, RHSReg);
}

static bool getCommutativeBinaryMemSourceAddrKind(unsigned Opc,
                                                  MemAddrKind &AddrKind) {
#define HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(OP)                             \
  case Bedrock::OP##rm:                                                       \
    AddrKind = MemAddrKind::Reg;                                              \
    return true;                                                              \
  case Bedrock::OP##rmx:                                                      \
    AddrKind = MemAddrKind::RegIndex;                                         \
    return true;                                                              \
  case Bedrock::OP##rmo:                                                      \
    AddrKind = MemAddrKind::RegOffset;                                        \
    return true;                                                              \
  case Bedrock::OP##rmfi:                                                     \
    AddrKind = MemAddrKind::Frame;                                            \
    return true;
  switch (Opc) {
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(ADDL3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(ADDQ3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(ANDL3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(ANDQ3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(ORL3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(ORQ3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(XORL3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(XORQ3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(MULL3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(MULQ3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(MINUL3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(MINUQ3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(MINSL3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(MINSQ3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(MAXUL3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(MAXUQ3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(MAXSL3)
    HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE(MAXSQ3)
  default:
    return false;
  }
#undef HANDLE_COMMUTATIVE_BINARY_MEM_SOURCE
}

static bool binaryMemSourceAddressUsesReg(const MachineInstr &MI,
                                          MemAddrKind AddrKind, Register Reg) {
  switch (AddrKind) {
  case MemAddrKind::Reg:
    return MI.getOperand(2).isReg() && MI.getOperand(2).getReg() == Reg;
  case MemAddrKind::RegIndex:
    return (MI.getOperand(2).isReg() && MI.getOperand(2).getReg() == Reg) ||
           (MI.getOperand(3).isReg() && MI.getOperand(3).getReg() == Reg);
  case MemAddrKind::RegOffset:
    return MI.getOperand(2).isReg() && MI.getOperand(2).getReg() == Reg;
  case MemAddrKind::Abs:
  case MemAddrKind::Frame:
    return false;
  }
  llvm_unreachable("unknown Bedrock memory address kind");
}

static bool foldBinaryMemSourcePair(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator &I,
                                    const TargetInstrInfo &TII) {
  MachineInstr &LoadMI = *I;
  if (hasVolatileMemOperand(LoadMI))
    return false;

  auto BinI = std::next(I);
  while (BinI != MBB.end() && BinI->isDebugInstr())
    ++BinI;
  if (BinI == MBB.end())
    return false;

  BinaryMemFoldInfo Info;
  if (!getBinaryMemFoldInfo(BinI->getOpcode(), Info))
    return false;

  MemAddrKind AddrKind;
  if (!getLoadAddrKindForBinaryFold(LoadMI, Info.Is64, AddrKind))
    return false;

  Register LoadReg = LoadMI.getOperand(0).getReg();
  Register DstReg = BinI->getOperand(0).getReg();
  Register LHSReg = BinI->getOperand(1).getReg();
  Register RHSReg = BinI->getOperand(2).getReg();
  if (DstReg != LHSReg)
    return false;

  auto NextI = std::next(BinI);

  if (DstReg != LoadReg && RHSReg == LoadReg && BinI->getOperand(2).isKill()) {
    unsigned FoldOpcode = getBinaryMemOpcode(Info, AddrKind);
    if (!FoldOpcode)
      return false;
    MachineInstrBuilder MIB =
        BuildMI(MBB, BinI, BinI->getDebugLoc(), TII.get(FoldOpcode), DstReg)
            .add(BinI->getOperand(1))
            .setMIFlags(BinI->getFlags());
    for (unsigned OpIdx = 1, OpEnd = LoadMI.getNumOperands(); OpIdx != OpEnd;
         ++OpIdx)
      MIB.add(LoadMI.getOperand(OpIdx));
    MIB.cloneMemRefs(LoadMI);
  } else if (DstReg == LoadReg && RHSReg != LoadReg &&
             BinI->getOperand(2).isKill() &&
             isCommutativeBinaryMemFoldOpcode(BinI->getOpcode()) &&
             !loadAddressUsesReg(LoadMI, AddrKind, RHSReg)) {
    unsigned FoldOpcode = getBinaryMemOpcode(Info, AddrKind);
    if (!FoldOpcode)
      return false;
    MachineInstrBuilder MIB =
        BuildMI(MBB, BinI, BinI->getDebugLoc(), TII.get(FoldOpcode), RHSReg)
            .addReg(RHSReg, RegState::Kill)
            .setMIFlags(BinI->getFlags());
    for (unsigned OpIdx = 1, OpEnd = LoadMI.getNumOperands(); OpIdx != OpEnd;
         ++OpIdx)
      MIB.add(LoadMI.getOperand(OpIdx));
    MIB.cloneMemRefs(LoadMI);
    BuildMI(MBB, BinI, BinI->getDebugLoc(), TII.get(Bedrock::MOVQrr), LoadReg)
        .addReg(RHSReg, RegState::Kill);
  } else {
    return false;
  }

  BinI->eraseFromParent();
  LoadMI.eraseFromParent();
  I = NextI;
  return true;
}

static bool foldCommutativeMemSourceCopyBack(MachineBasicBlock &MBB,
                                             MachineBasicBlock::iterator &I) {
  MachineInstr &MoveInMI = *I;
  if (MoveInMI.getOpcode() != Bedrock::MOVQrr ||
      MoveInMI.getNumExplicitOperands() != 2 ||
      !MoveInMI.getOperand(0).isReg() || !MoveInMI.getOperand(1).isReg())
    return false;

  auto MemI = std::next(I);
  while (MemI != MBB.end() && MemI->isDebugInstr())
    ++MemI;
  if (MemI == MBB.end())
    return false;

  MemAddrKind AddrKind;
  if (!getCommutativeBinaryMemSourceAddrKind(MemI->getOpcode(), AddrKind) ||
      MemI->getNumExplicitOperands() < 3 || !MemI->getOperand(0).isReg() ||
      !MemI->getOperand(1).isReg())
    return false;

  auto MoveOutI = std::next(MemI);
  while (MoveOutI != MBB.end() && MoveOutI->isDebugInstr())
    ++MoveOutI;
  if (MoveOutI == MBB.end() || MoveOutI->getOpcode() != Bedrock::MOVQrr ||
      MoveOutI->getNumExplicitOperands() != 2 ||
      !MoveOutI->getOperand(0).isReg() || !MoveOutI->getOperand(1).isReg() ||
      !MoveOutI->getOperand(1).isKill())
    return false;

  Register SrcReg = MoveInMI.getOperand(1).getReg();
  Register TmpReg = MoveInMI.getOperand(0).getReg();
  if (SrcReg == TmpReg || MemI->getOperand(0).getReg() != TmpReg ||
      MemI->getOperand(1).getReg() != TmpReg ||
      MoveOutI->getOperand(1).getReg() != TmpReg ||
      binaryMemSourceAddressUsesReg(*MemI, AddrKind, SrcReg))
    return false;

  MemI->getOperand(0).setReg(SrcReg);
  MemI->getOperand(1).setReg(SrcReg);
  MemI->getOperand(1).setIsKill(true);

  auto NextI = std::next(MoveOutI);
  if (MoveOutI->getOperand(0).getReg() == SrcReg) {
    MoveOutI->eraseFromParent();
  } else {
    MoveOutI->getOperand(1).setReg(SrcReg);
    MoveOutI->getOperand(1).setIsKill(true);
  }
  MoveInMI.eraseFromParent();
  I = NextI;
  return true;
}

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

  if (!Reg.isPhysical())
    return true;
  for (const MachineBasicBlock *Succ : MBB.successors()) {
    if (Succ->isLiveIn(Reg.asMCReg()))
      return true;
  }
  return false;
}

struct CommutativeMemSourceStoreFoldInfo {
  unsigned NewOpcode;
  MemAddrKind AddrKind;
  bool Is64;
};

static bool getCommutativeMemSourceStoreFoldInfo(
    unsigned Opcode, CommutativeMemSourceStoreFoldInfo &Info) {
#define HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(OP, IS64)                         \
  case Bedrock::OP##rm:                                                       \
    Info = {Bedrock::OP##mr, MemAddrKind::Reg, IS64};                         \
    return true;                                                              \
  case Bedrock::OP##rmo:                                                      \
    Info = {Bedrock::OP##mro, MemAddrKind::RegOffset, IS64};                  \
    return true;                                                              \
  case Bedrock::OP##rmfi:                                                     \
    Info = {Bedrock::OP##mfi, MemAddrKind::Frame, IS64};                      \
    return true;
  switch (Opcode) {
    HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(ADDL3, false)
    HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(ADDQ3, true)
    HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(ANDL3, false)
    HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(ANDQ3, true)
    HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(ORL3, false)
    HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(ORQ3, true)
    HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(XORL3, false)
    HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(XORQ3, true)
    HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(MINUL3, false)
    HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(MINUQ3, true)
    HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(MINSL3, false)
    HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(MINSQ3, true)
    HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(MAXUL3, false)
    HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(MAXUQ3, true)
    HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(MAXSL3, false)
    HANDLE_COMMUTATIVE_MEM_SOURCE_STORE(MAXSQ3, true)
  default:
    return false;
  }
#undef HANDLE_COMMUTATIVE_MEM_SOURCE_STORE
}

static bool getStoreAddrKindForCommutativeMemSourceStoreFold(
    const MachineInstr &StoreMI, bool Is64, MemAddrKind &AddrKind) {
  switch (StoreMI.getOpcode()) {
  case Bedrock::STORELrr:
    if (Is64)
      return false;
    AddrKind = MemAddrKind::Reg;
    return true;
  case Bedrock::STOREQrr:
    AddrKind = MemAddrKind::Reg;
    return true;
  case Bedrock::STORELro:
    if (Is64)
      return false;
    AddrKind = MemAddrKind::RegOffset;
    return true;
  case Bedrock::STOREQro:
    AddrKind = MemAddrKind::RegOffset;
    return true;
  case Bedrock::STORELfi:
    if (Is64)
      return false;
    AddrKind = MemAddrKind::Frame;
    return true;
  case Bedrock::STOREQfi:
    AddrKind = MemAddrKind::Frame;
    return true;
  default:
    return false;
  }
}

static bool memDestStoreAddressUsesReg(const MachineInstr &StoreMI,
                                       MemAddrKind AddrKind, Register Reg) {
  switch (AddrKind) {
  case MemAddrKind::Reg:
  case MemAddrKind::RegOffset:
    return StoreMI.getOperand(1).isReg() &&
           StoreMI.getOperand(1).getReg() == Reg;
  case MemAddrKind::Frame:
  case MemAddrKind::Abs:
    return false;
  case MemAddrKind::RegIndex:
    llvm_unreachable("indexed binary memory destination pseudo is not "
                     "supported");
  }
  llvm_unreachable("unknown Bedrock memory address kind");
}

static bool binaryMemSourceStoreAddressesMatch(const MachineInstr &MemMI,
                                               const MachineInstr &StoreMI,
                                               MemAddrKind AddrKind) {
  switch (AddrKind) {
  case MemAddrKind::Reg:
    return MemMI.getOperand(2).isIdenticalTo(StoreMI.getOperand(1));
  case MemAddrKind::RegOffset:
  case MemAddrKind::Frame:
    return MemMI.getOperand(2).isIdenticalTo(StoreMI.getOperand(1)) &&
           MemMI.getOperand(3).isIdenticalTo(StoreMI.getOperand(2));
  case MemAddrKind::Abs:
  case MemAddrKind::RegIndex:
    return false;
  }
  llvm_unreachable("unknown Bedrock memory address kind");
}

static void addBinaryMemSourceAddressOperands(MachineInstrBuilder &MIB,
                                              const MachineInstr &MemMI,
                                              MemAddrKind AddrKind) {
  switch (AddrKind) {
  case MemAddrKind::Reg:
    MIB.add(MemMI.getOperand(2));
    return;
  case MemAddrKind::RegOffset:
  case MemAddrKind::Frame:
    MIB.add(MemMI.getOperand(2)).add(MemMI.getOperand(3));
    return;
  case MemAddrKind::Abs:
  case MemAddrKind::RegIndex:
    llvm_unreachable("unsupported binary memory source address kind");
  }
  llvm_unreachable("unknown Bedrock memory address kind");
}

static bool memSourceStoreInstrHasRegOperand(const MachineInstr &MI,
                                             Register Reg) {
  return any_of(MI.operands(), [Reg](const MachineOperand &MO) {
    return MO.isReg() && MO.getReg() == Reg;
  });
}

static bool memSourceStoreInstrDefinesAddressReg(const MachineInstr &MI,
                                                 const MachineInstr &MemMI,
                                                 MemAddrKind AddrKind) {
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.isDef() &&
        binaryMemSourceAddressUsesReg(MemMI, AddrKind, MO.getReg()))
      return true;
  }
  return false;
}

static bool isPlainLoadForMemSourceStoreScan(unsigned Opcode) {
  switch (Opcode) {
  case Bedrock::LOADB_Zrr:
  case Bedrock::LOADW_Zrr:
  case Bedrock::LOADL_Zrr:
  case Bedrock::LOADB_Srr:
  case Bedrock::LOADW_Srr:
  case Bedrock::LOADL_Srr:
  case Bedrock::LOADLrr:
  case Bedrock::LOADQrr:
  case Bedrock::LOADB_Zrx:
  case Bedrock::LOADW_Zrx:
  case Bedrock::LOADL_Zrx:
  case Bedrock::LOADB_Srx:
  case Bedrock::LOADW_Srx:
  case Bedrock::LOADL_Srx:
  case Bedrock::LOADLrx:
  case Bedrock::LOADQrx:
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

static bool canScanAcrossForMemSourceStoreFold(const MachineInstr &MI,
                                               const MachineInstr &MemMI,
                                               MemAddrKind AddrKind,
                                               Register DstReg,
                                               Register SrcReg) {
  if (MI.isCall() || MI.isTerminator() ||
      !isPlainLoadForMemSourceStoreScan(MI.getOpcode()) ||
      hasVolatileMemOperand(MI) ||
      (!MI.memoperands_empty() && MI.hasOrderedMemoryRef()) ||
      memSourceStoreInstrHasRegOperand(MI, DstReg) ||
      memSourceStoreInstrHasRegOperand(MI, SrcReg) ||
      memSourceStoreInstrDefinesAddressReg(MI, MemMI, AddrKind))
    return false;
  return true;
}

static bool foldCommutativeMemSourceStorePair(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator &I,
    const TargetInstrInfo &TII) {
  MachineInstr &MemMI = *I;
  if (hasVolatileMemOperand(MemMI) || MemMI.getNumExplicitOperands() < 3 ||
      !MemMI.getOperand(0).isReg() || !MemMI.getOperand(1).isReg())
    return false;

  CommutativeMemSourceStoreFoldInfo Info;
  if (!getCommutativeMemSourceStoreFoldInfo(MemMI.getOpcode(), Info))
    return false;

  Register DstReg = MemMI.getOperand(0).getReg();
  if (binaryMemSourceAddressUsesReg(MemMI, Info.AddrKind, DstReg))
    return false;

  Register SrcReg = MemMI.getOperand(1).getReg();

  auto StoreI = std::next(I);
  while (StoreI != MBB.end()) {
    while (StoreI != MBB.end() && StoreI->isDebugInstr())
      ++StoreI;
    if (StoreI == MBB.end())
      return false;
    if (StoreI->mayStore())
      break;
    if (!canScanAcrossForMemSourceStoreFold(*StoreI, MemMI, Info.AddrKind,
                                            DstReg, SrcReg)) {
      return false;
    }
    ++StoreI;
  }

  if (StoreI == MBB.end() || hasVolatileMemOperand(*StoreI) ||
      StoreI->getNumExplicitOperands() < 2 || !StoreI->getOperand(0).isReg() ||
      StoreI->getOperand(0).getReg() != DstReg)
    return false;

  MemAddrKind StoreAddrKind;
  if (!getStoreAddrKindForCommutativeMemSourceStoreFold(*StoreI, Info.Is64,
                                                        StoreAddrKind) ||
      StoreAddrKind != Info.AddrKind ||
      !binaryMemSourceStoreAddressesMatch(MemMI, *StoreI, Info.AddrKind) ||
      memDestStoreAddressUsesReg(*StoreI, StoreAddrKind, DstReg) ||
      hasUseOrLiveOutOfRegBeforeDef(std::next(StoreI), MBB, DstReg))
    return false;

  auto NextI = std::next(StoreI);
  MachineInstrBuilder MIB =
      BuildMI(MBB, StoreI, MemMI.getDebugLoc(), TII.get(Info.NewOpcode))
          .add(MemMI.getOperand(1))
          .setMIFlags(MemMI.getFlags());
  addBinaryMemSourceAddressOperands(MIB, MemMI, Info.AddrKind);
  MIB.cloneMemRefs(MemMI);
  MIB.cloneMemRefs(*StoreI);

  StoreI->eraseFromParent();
  MemMI.eraseFromParent();
  I = NextI;
  return true;
}

static bool isCommutativeRRCopyForwardOpcode(unsigned Opc) {
  switch (Opc) {
  case Bedrock::ADDL3rr:
  case Bedrock::ADDQ3rr:
  case Bedrock::ANDL3rr:
  case Bedrock::ANDQ3rr:
  case Bedrock::ORL3rr:
  case Bedrock::ORQ3rr:
  case Bedrock::XORL3rr:
  case Bedrock::XORQ3rr:
  case Bedrock::MULL3rr:
  case Bedrock::MULQ3rr:
  case Bedrock::MINUL3rr:
  case Bedrock::MINUQ3rr:
  case Bedrock::MINSL3rr:
  case Bedrock::MINSQ3rr:
  case Bedrock::MAXUL3rr:
  case Bedrock::MAXUQ3rr:
  case Bedrock::MAXSL3rr:
  case Bedrock::MAXSQ3rr:
    return true;
  default:
    return false;
  }
}

static bool foldCopyIntoCommutativeRR(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator &I) {
  MachineInstr &MoveMI = *I;
  if (MoveMI.getOpcode() != Bedrock::MOVQrr ||
      MoveMI.getNumExplicitOperands() != 2 || !MoveMI.getOperand(0).isReg() ||
      !MoveMI.getOperand(1).isReg() || !MoveMI.getOperand(1).isKill())
    return false;

  auto BinI = std::next(I);
  while (BinI != MBB.end() && BinI->isDebugInstr())
    ++BinI;
  if (BinI == MBB.end() ||
      !isCommutativeRRCopyForwardOpcode(BinI->getOpcode()) ||
      BinI->getNumExplicitOperands() != 3 || !BinI->getOperand(0).isReg() ||
      !BinI->getOperand(1).isReg() || !BinI->getOperand(2).isReg())
    return false;

  Register SrcReg = MoveMI.getOperand(1).getReg();
  Register TmpReg = MoveMI.getOperand(0).getReg();
  if (SrcReg == TmpReg || BinI->getOperand(0).getReg() == TmpReg ||
      BinI->getOperand(1).getReg() == TmpReg ||
      BinI->getOperand(2).getReg() != TmpReg ||
      !BinI->getOperand(2).isKill())
    return false;

  BinI->getOperand(2).setReg(SrcReg);
  BinI->getOperand(2).setIsKill(true);
  MoveMI.eraseFromParent();
  I = std::next(BinI);
  return true;
}

static bool isSimpleDefCopyBackOpcode(unsigned Opcode) {
  switch (Opcode) {
  case Bedrock::CONST32:
  case Bedrock::CONST64:
  case Bedrock::LEAfi:
  case Bedrock::LEAro:
  case Bedrock::LEArx:
  case Bedrock::CLRQr:
  case Bedrock::EXTSQBrr:
  case Bedrock::EXTSQWrr:
  case Bedrock::EXTSQLrr:
  case Bedrock::EXTZQBrr:
  case Bedrock::EXTZQWrr:
  case Bedrock::EXTZQLrr:
  case Bedrock::SETCC:
    return true;
  default:
    return false;
  }
}

static bool hasExplicitUseOfReg(const MachineInstr &MI, Register Reg) {
  for (const MachineOperand &MO : MI.explicit_operands()) {
    if (MO.isReg() && !MO.isDef() && MO.getReg() == Reg)
      return true;
  }
  return false;
}

static bool foldSimpleDefCopyBack(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator &I) {
  MachineInstr &DefMI = *I;
  if (!isSimpleDefCopyBackOpcode(DefMI.getOpcode()) ||
      DefMI.getNumExplicitOperands() < 1 || !DefMI.getOperand(0).isReg())
    return false;

  Register DefReg = DefMI.getOperand(0).getReg();
  auto CopyI = std::next(I);
  while (CopyI != MBB.end() && CopyI->isDebugInstr())
    ++CopyI;
  if (CopyI == MBB.end() || CopyI->getOpcode() != Bedrock::MOVQrr ||
      CopyI->getNumExplicitOperands() != 2 || !CopyI->getOperand(0).isReg() ||
      !CopyI->getOperand(1).isReg() ||
      CopyI->getOperand(1).getReg() != DefReg ||
      !CopyI->getOperand(1).isKill())
    return false;

  Register CopyDst = CopyI->getOperand(0).getReg();
  if (CopyDst == DefReg || hasExplicitUseOfReg(DefMI, CopyDst))
    return false;

  DefMI.getOperand(0).setReg(CopyDst);
  DefMI.getOperand(0).setIsDead(CopyI->getOperand(0).isDead());

  auto NextI = std::next(CopyI);
  CopyI->eraseFromParent();
  I = NextI;
  return true;
}

struct IndexedMemFoldInfo {
  unsigned NewOpcode;
  unsigned ShiftAmount;
  unsigned BaseOp;
};

struct IndexedCmpFoldInfo {
  unsigned NewOpcode;
  unsigned ShiftAmount;
  unsigned BaseOp;
};

static bool getIndexedLoadFoldInfo(unsigned Opcode,
                                   IndexedMemFoldInfo &Info) {
  switch (Opcode) {
  case Bedrock::LOADB_Zrr:
    Info = {Bedrock::LOADB_Zrx, 0, 1};
    return true;
  case Bedrock::LOADB_Srr:
    Info = {Bedrock::LOADB_Srx, 0, 1};
    return true;
  case Bedrock::LOADW_Zrr:
    Info = {Bedrock::LOADW_Zrx, 1, 1};
    return true;
  case Bedrock::LOADW_Srr:
    Info = {Bedrock::LOADW_Srx, 1, 1};
    return true;
  case Bedrock::LOADL_Zrr:
    Info = {Bedrock::LOADL_Zrx, 2, 1};
    return true;
  case Bedrock::LOADL_Srr:
    Info = {Bedrock::LOADL_Srx, 2, 1};
    return true;
  case Bedrock::LOADLrr:
    Info = {Bedrock::LOADLrx, 2, 1};
    return true;
  case Bedrock::LOADQrr:
    Info = {Bedrock::LOADQrx, 3, 1};
    return true;
  default:
    return false;
  }
}

static bool getIndexedCmpFoldInfo(unsigned Opcode, IndexedCmpFoldInfo &Info) {
  switch (Opcode) {
  case Bedrock::CMPLmr:
    Info = {Bedrock::CMPLmxr, 2, 0};
    return true;
  case Bedrock::CMPQmr:
    Info = {Bedrock::CMPQmxr, 3, 0};
    return true;
  case Bedrock::CMPLrm:
    Info = {Bedrock::CMPLrmx, 2, 1};
    return true;
  case Bedrock::CMPQrm:
    Info = {Bedrock::CMPQrmx, 3, 1};
    return true;
  default:
    return false;
  }
}

static bool isScaledIndexShift(const MachineInstr &MI, Register IndexReg,
                               unsigned ShiftAmount) {
  if (MI.getOpcode() != Bedrock::SHLQ3ri ||
      MI.getNumExplicitOperands() != 3 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isReg() || !MI.getOperand(2).isImm())
    return false;

  return MI.getOperand(0).getReg() == IndexReg &&
         MI.getOperand(1).getReg() == IndexReg &&
         MI.getOperand(2).getImm() == ShiftAmount;
}

static bool isAddressBaseMaterialization(const MachineInstr &MI,
                                         Register &BaseReg) {
  switch (MI.getOpcode()) {
  case Bedrock::CONST64:
  case Bedrock::LEAfi:
    if (!MI.getOperand(0).isReg())
      return false;
    BaseReg = MI.getOperand(0).getReg();
    return true;
  default:
    return false;
  }
}

static bool foldScaledIndexMem(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator &I,
                               const TargetInstrInfo &TII) {
  MachineInstr &ShiftMI = *I;
  Register IndexReg;
  if (ShiftMI.getOpcode() != Bedrock::SHLQ3ri)
    return false;

  auto BaseI = std::next(I);
  while (BaseI != MBB.end() && BaseI->isDebugInstr())
    ++BaseI;
  if (BaseI == MBB.end())
    return false;

  Register BaseReg;
  if (!isAddressBaseMaterialization(*BaseI, BaseReg))
    return false;

  auto AddI = std::next(BaseI);
  while (AddI != MBB.end() && AddI->isDebugInstr())
    ++AddI;
  if (AddI == MBB.end() || AddI->getOpcode() != Bedrock::ADDQ3rr ||
      AddI->getNumExplicitOperands() != 3 || !AddI->getOperand(0).isReg() ||
      !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg())
    return false;

  Register AddDstReg = AddI->getOperand(0).getReg();
  Register AddLHSReg = AddI->getOperand(1).getReg();
  Register AddRHSReg = AddI->getOperand(2).getReg();
  unsigned IndexOp;
  unsigned BaseOp;
  if (AddDstReg == BaseReg && AddLHSReg == BaseReg) {
    IndexReg = AddRHSReg;
    IndexOp = 2;
    BaseOp = 1;
  } else if (AddDstReg == AddLHSReg && AddRHSReg == BaseReg) {
    IndexReg = AddLHSReg;
    IndexOp = 1;
    BaseOp = 2;
  } else {
    return false;
  }

  if (IndexReg == BaseReg || !AddI->getOperand(IndexOp).isKill())
    return false;

  auto MemI = std::next(AddI);
  while (MemI != MBB.end() && MemI->isDebugInstr())
    ++MemI;
  if (MemI == MBB.end())
    return false;

  IndexedMemFoldInfo Info;
  if (!getIndexedLoadFoldInfo(MemI->getOpcode(), Info) ||
      !isScaledIndexShift(ShiftMI, IndexReg, Info.ShiftAmount) ||
      MemI->getNumExplicitOperands() <= Info.BaseOp ||
      !MemI->getOperand(Info.BaseOp).isReg() ||
      MemI->getOperand(Info.BaseOp).getReg() != AddDstReg ||
      !MemI->getOperand(Info.BaseOp).isKill())
    return false;

  MemI->setDesc(TII.get(Info.NewOpcode));
  MemI->getOperand(Info.BaseOp).setReg(BaseReg);
  MemI->getOperand(Info.BaseOp).setIsKill(AddI->getOperand(BaseOp).isKill());
  MemI->addOperand(MachineOperand::CreateReg(IndexReg, /*isDef=*/false,
                                             /*isImp=*/false, true));

  auto NextI = std::next(MemI);
  AddI->eraseFromParent();
  ShiftMI.eraseFromParent();
  I = NextI;
  return true;
}

static bool foldAddIndexMem(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator &I,
                            const TargetInstrInfo &TII) {
  MachineInstr &AddMI = *I;
  if (AddMI.getOpcode() != Bedrock::ADDQ3rr ||
      AddMI.getNumExplicitOperands() != 3 || !AddMI.getOperand(0).isReg() ||
      !AddMI.getOperand(1).isReg() || !AddMI.getOperand(2).isReg())
    return false;

  Register AddrReg = AddMI.getOperand(0).getReg();
  Register BaseReg = AddMI.getOperand(1).getReg();
  Register IndexReg = AddMI.getOperand(2).getReg();
  if (BaseReg == IndexReg)
    return false;
  bool BaseIsKill = BaseReg == AddrReg ? true : AddMI.getOperand(1).isKill();
  bool IndexIsKill = IndexReg == AddrReg ? true : AddMI.getOperand(2).isKill();

  auto MemI = std::next(I);
  while (MemI != MBB.end() && MemI->isDebugInstr())
    ++MemI;
  if (MemI == MBB.end())
    return false;

  IndexedMemFoldInfo MemInfo;
  if (getIndexedLoadFoldInfo(MemI->getOpcode(), MemInfo) &&
      MemI->getNumExplicitOperands() > MemInfo.BaseOp &&
      MemI->getOperand(MemInfo.BaseOp).isReg() &&
      MemI->getOperand(MemInfo.BaseOp).getReg() == AddrReg &&
      MemI->getOperand(MemInfo.BaseOp).isKill()) {
    if (loadFeedsFoldableBinaryMemSource(MemI, MBB))
      return false;

    MemI->setDesc(TII.get(MemInfo.NewOpcode));
    MemI->getOperand(MemInfo.BaseOp).setReg(BaseReg);
    MemI->getOperand(MemInfo.BaseOp).setIsKill(BaseIsKill);
    MemI->addOperand(MachineOperand::CreateReg(
        IndexReg, /*isDef=*/false, /*isImp=*/false, IndexIsKill));

    auto NextI = std::next(MemI);
    AddMI.eraseFromParent();
    I = NextI;
    return true;
  }

  IndexedCmpFoldInfo CmpInfo;
  if (!getIndexedCmpFoldInfo(MemI->getOpcode(), CmpInfo) ||
      MemI->getNumExplicitOperands() <= CmpInfo.BaseOp ||
      !MemI->getOperand(CmpInfo.BaseOp).isReg() ||
      MemI->getOperand(CmpInfo.BaseOp).getReg() != AddrReg ||
      !MemI->getOperand(CmpInfo.BaseOp).isKill())
    return false;

  auto NextI = std::next(MemI);
  MachineInstrBuilder MIB =
      BuildMI(MBB, MemI, MemI->getDebugLoc(), TII.get(CmpInfo.NewOpcode))
          .setMIFlags(MemI->getFlags());
  if (CmpInfo.BaseOp == 0) {
    MIB.addReg(BaseReg, BaseIsKill ? RegState::Kill : 0)
        .addReg(IndexReg, IndexIsKill ? RegState::Kill : 0)
        .add(MemI->getOperand(1));
  } else {
    MIB.add(MemI->getOperand(0))
        .addReg(BaseReg, BaseIsKill ? RegState::Kill : 0)
        .addReg(IndexReg, IndexIsKill ? RegState::Kill : 0);
  }
  MIB.cloneMemRefs(*MemI);

  MemI->eraseFromParent();
  AddMI.eraseFromParent();
  I = NextI;
  return true;
}

static bool foldCopiedBaseIndexLoad(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator &I,
                                    const TargetInstrInfo &TII) {
  MachineInstr &MoveMI = *I;
  if (MoveMI.getOpcode() != Bedrock::MOVQrr ||
      MoveMI.getNumExplicitOperands() != 2 || !MoveMI.getOperand(0).isReg() ||
      !MoveMI.getOperand(1).isReg())
    return false;

  Register AddrReg = MoveMI.getOperand(0).getReg();
  Register BaseReg = MoveMI.getOperand(1).getReg();
  if (AddrReg == BaseReg)
    return false;

  auto AddI = std::next(I);
  while (AddI != MBB.end() && AddI->isDebugInstr())
    ++AddI;
  if (AddI == MBB.end() || AddI->getOpcode() != Bedrock::ADDQ3rr ||
      AddI->getNumExplicitOperands() != 3 || !AddI->getOperand(0).isReg() ||
      !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg() ||
      AddI->getOperand(0).getReg() != AddrReg ||
      AddI->getOperand(1).getReg() != AddrReg)
    return false;

  Register IndexReg = AddI->getOperand(2).getReg();
  if (IndexReg == AddrReg)
    return false;

  auto LoadI = std::next(AddI);
  while (LoadI != MBB.end() && LoadI->isDebugInstr())
    ++LoadI;
  if (LoadI == MBB.end())
    return false;

  IndexedMemFoldInfo Info;
  if (!getIndexedLoadFoldInfo(LoadI->getOpcode(), Info) ||
      LoadI->getNumExplicitOperands() <= Info.BaseOp ||
      !LoadI->getOperand(Info.BaseOp).isReg() ||
      LoadI->getOperand(Info.BaseOp).getReg() != AddrReg ||
      !LoadI->getOperand(Info.BaseOp).isKill())
    return false;
  if (loadFeedsFoldableBinaryMemSource(LoadI, MBB))
    return false;

  LoadI->setDesc(TII.get(Info.NewOpcode));
  LoadI->getOperand(Info.BaseOp).setReg(BaseReg);
  LoadI->getOperand(Info.BaseOp).setIsKill(MoveMI.getOperand(1).isKill());
  LoadI->addOperand(MachineOperand::CreateReg(
      IndexReg, /*isDef=*/false, /*isImp=*/false, AddI->getOperand(2).isKill()));

  auto NextI = std::next(LoadI);
  AddI->eraseFromParent();
  MoveMI.eraseFromParent();
  I = NextI;
  return true;
}

static bool getIndexedStoreFoldInfo(unsigned Opcode,
                                    IndexedMemFoldInfo &Info) {
  switch (Opcode) {
  case Bedrock::STOREBrr:
    Info = {Bedrock::STOREBrx, 0, 1};
    return true;
  case Bedrock::STOREWrr:
    Info = {Bedrock::STOREWrx, 1, 1};
    return true;
  case Bedrock::STORELrr:
    Info = {Bedrock::STORELrx, 2, 1};
    return true;
  case Bedrock::STOREQrr:
    Info = {Bedrock::STOREQrx, 3, 1};
    return true;
  default:
    return false;
  }
}

static bool getSPIndexLoadFoldInfo(unsigned Opcode,
                                   IndexedMemFoldInfo &Info) {
  switch (Opcode) {
  case Bedrock::LOADB_Zrr:
    Info = {Bedrock::LOADB_Zspx, 0, 1};
    return true;
  case Bedrock::LOADB_Srr:
    Info = {Bedrock::LOADB_Sspx, 0, 1};
    return true;
  case Bedrock::LOADW_Zrr:
    Info = {Bedrock::LOADW_Zspx, 1, 1};
    return true;
  case Bedrock::LOADW_Srr:
    Info = {Bedrock::LOADW_Sspx, 1, 1};
    return true;
  case Bedrock::LOADL_Zrr:
    Info = {Bedrock::LOADL_Zspx, 2, 1};
    return true;
  case Bedrock::LOADL_Srr:
    Info = {Bedrock::LOADL_Sspx, 2, 1};
    return true;
  case Bedrock::LOADLrr:
    Info = {Bedrock::LOADLspx, 2, 1};
    return true;
  case Bedrock::LOADQrr:
    Info = {Bedrock::LOADQspx, 3, 1};
    return true;
  default:
    return false;
  }
}

static bool getSPIndexStoreFoldInfo(unsigned Opcode,
                                    IndexedMemFoldInfo &Info) {
  switch (Opcode) {
  case Bedrock::STOREBrr:
    Info = {Bedrock::STOREBspx, 0, 1};
    return true;
  case Bedrock::STOREWrr:
    Info = {Bedrock::STOREWspx, 1, 1};
    return true;
  case Bedrock::STORELrr:
    Info = {Bedrock::STORELspx, 2, 1};
    return true;
  case Bedrock::STOREQrr:
    Info = {Bedrock::STOREQspx, 3, 1};
    return true;
  default:
    return false;
  }
}

static bool isZeroFrameAddress(const MachineInstr &MI, Register &AddrReg) {
  if (MI.getOpcode() != Bedrock::LEAfi || MI.getNumExplicitOperands() < 3 ||
      !MI.getOperand(0).isReg() || getFrameOffset(&MI, 1) != 0)
    return false;
  AddrReg = MI.getOperand(0).getReg();
  return true;
}

static bool matchSPIndexAddress(MachineBasicBlock::iterator I,
                                MachineBasicBlock &MBB, Register &AddrReg,
                                Register &IndexReg,
                                MachineBasicBlock::iterator &AddI) {
  if (!isZeroFrameAddress(*I, AddrReg))
    return false;

  AddI = std::next(I);
  while (AddI != MBB.end() && AddI->isDebugInstr())
    ++AddI;
  if (AddI == MBB.end() || AddI->getOpcode() != Bedrock::ADDQ3rr ||
      AddI->getNumExplicitOperands() != 3 || !AddI->getOperand(0).isReg() ||
      !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg() ||
      AddI->getOperand(0).getReg() != AddrReg ||
      AddI->getOperand(1).getReg() != AddrReg)
    return false;

  IndexReg = AddI->getOperand(2).getReg();
  return IndexReg != AddrReg;
}

static bool foldSPIndexLoad(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator &I,
                            const TargetInstrInfo &TII) {
  Register AddrReg;
  Register IndexReg;
  MachineBasicBlock::iterator AddI;
  if (!matchSPIndexAddress(I, MBB, AddrReg, IndexReg, AddI))
    return false;

  auto LoadI = std::next(AddI);
  while (LoadI != MBB.end() && LoadI->isDebugInstr())
    ++LoadI;
  if (LoadI == MBB.end())
    return false;

  IndexedMemFoldInfo Info;
  if (!getSPIndexLoadFoldInfo(LoadI->getOpcode(), Info) ||
      LoadI->getNumExplicitOperands() <= Info.BaseOp ||
      !LoadI->getOperand(Info.BaseOp).isReg() ||
      LoadI->getOperand(Info.BaseOp).getReg() != AddrReg ||
      !LoadI->getOperand(Info.BaseOp).isKill())
    return false;
  if (loadFeedsFoldableBinaryMemSource(LoadI, MBB))
    return false;

  LoadI->setDesc(TII.get(Info.NewOpcode));
  LoadI->getOperand(Info.BaseOp).setReg(IndexReg);
  LoadI->getOperand(Info.BaseOp).setIsKill(AddI->getOperand(2).isKill());

  auto NextI = std::next(LoadI);
  AddI->eraseFromParent();
  I->eraseFromParent();
  I = NextI;
  return true;
}

static bool foldSPIndexStore(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator &I,
                             const TargetInstrInfo &TII) {
  Register AddrReg;
  Register IndexReg;
  MachineBasicBlock::iterator AddI;
  if (!matchSPIndexAddress(I, MBB, AddrReg, IndexReg, AddI))
    return false;

  auto StoreI = std::next(AddI);
  while (StoreI != MBB.end() && StoreI->isDebugInstr())
    ++StoreI;
  if (StoreI == MBB.end())
    return false;

  IndexedMemFoldInfo Info;
  if (!getSPIndexStoreFoldInfo(StoreI->getOpcode(), Info) ||
      StoreI->getNumExplicitOperands() <= Info.BaseOp ||
      !StoreI->getOperand(0).isReg() ||
      StoreI->getOperand(0).getReg() == AddrReg ||
      !StoreI->getOperand(Info.BaseOp).isReg() ||
      StoreI->getOperand(Info.BaseOp).getReg() != AddrReg ||
      !StoreI->getOperand(Info.BaseOp).isKill())
    return false;

  StoreI->setDesc(TII.get(Info.NewOpcode));
  StoreI->getOperand(Info.BaseOp).setReg(IndexReg);
  StoreI->getOperand(Info.BaseOp).setIsKill(AddI->getOperand(2).isKill());

  auto NextI = std::next(StoreI);
  AddI->eraseFromParent();
  I->eraseFromParent();
  I = NextI;
  return true;
}

static bool foldCopiedBaseIndexStore(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator &I,
                                     const TargetInstrInfo &TII) {
  MachineInstr &MoveMI = *I;
  if (MoveMI.getOpcode() != Bedrock::MOVQrr ||
      MoveMI.getNumExplicitOperands() != 2 || !MoveMI.getOperand(0).isReg() ||
      !MoveMI.getOperand(1).isReg())
    return false;

  Register AddrReg = MoveMI.getOperand(0).getReg();
  Register BaseReg = MoveMI.getOperand(1).getReg();
  if (AddrReg == BaseReg)
    return false;

  auto AddI = std::next(I);
  while (AddI != MBB.end() && AddI->isDebugInstr())
    ++AddI;
  if (AddI == MBB.end() || AddI->getOpcode() != Bedrock::ADDQ3rr ||
      AddI->getNumExplicitOperands() != 3 || !AddI->getOperand(0).isReg() ||
      !AddI->getOperand(1).isReg() || !AddI->getOperand(2).isReg() ||
      AddI->getOperand(0).getReg() != AddrReg ||
      AddI->getOperand(1).getReg() != AddrReg)
    return false;

  Register IndexReg = AddI->getOperand(2).getReg();
  if (IndexReg == AddrReg)
    return false;

  auto StoreI = std::next(AddI);
  while (StoreI != MBB.end() && StoreI->isDebugInstr())
    ++StoreI;
  if (StoreI == MBB.end())
    return false;

  IndexedMemFoldInfo Info;
  if (!getIndexedStoreFoldInfo(StoreI->getOpcode(), Info) ||
      StoreI->getNumExplicitOperands() <= Info.BaseOp ||
      !StoreI->getOperand(0).isReg() ||
      StoreI->getOperand(0).getReg() == AddrReg ||
      !StoreI->getOperand(Info.BaseOp).isReg() ||
      StoreI->getOperand(Info.BaseOp).getReg() != AddrReg ||
      !StoreI->getOperand(Info.BaseOp).isKill())
    return false;

  StoreI->setDesc(TII.get(Info.NewOpcode));
  StoreI->getOperand(Info.BaseOp).setReg(BaseReg);
  StoreI->getOperand(Info.BaseOp).setIsKill(MoveMI.getOperand(1).isKill());
  StoreI->addOperand(MachineOperand::CreateReg(
      IndexReg, /*isDef=*/false, /*isImp=*/false, AddI->getOperand(2).isKill()));

  auto NextI = std::next(StoreI);
  AddI->eraseFromParent();
  MoveMI.eraseFromParent();
  I = NextI;
  return true;
}

struct BinaryMemDestFoldInfo {
  unsigned RegOpc;
  unsigned RegOffsetOpc;
  unsigned FrameOpc;
  bool Is64;
};

static bool getBinaryMemDestFoldInfo(unsigned Opc,
                                     BinaryMemDestFoldInfo &Info) {
  switch (Opc) {
  case Bedrock::ADDL3rr:
    Info = {Bedrock::ADDL3mr, Bedrock::ADDL3mro, Bedrock::ADDL3mfi, false};
    return true;
  case Bedrock::ADDQ3rr:
    Info = {Bedrock::ADDQ3mr, Bedrock::ADDQ3mro, Bedrock::ADDQ3mfi, true};
    return true;
  case Bedrock::SUBL3rr:
    Info = {Bedrock::SUBL3mr, Bedrock::SUBL3mro, Bedrock::SUBL3mfi, false};
    return true;
  case Bedrock::SUBQ3rr:
    Info = {Bedrock::SUBQ3mr, Bedrock::SUBQ3mro, Bedrock::SUBQ3mfi, true};
    return true;
  case Bedrock::ANDL3rr:
    Info = {Bedrock::ANDL3mr, Bedrock::ANDL3mro, Bedrock::ANDL3mfi, false};
    return true;
  case Bedrock::ANDQ3rr:
    Info = {Bedrock::ANDQ3mr, Bedrock::ANDQ3mro, Bedrock::ANDQ3mfi, true};
    return true;
  case Bedrock::ORL3rr:
    Info = {Bedrock::ORL3mr, Bedrock::ORL3mro, Bedrock::ORL3mfi, false};
    return true;
  case Bedrock::ORQ3rr:
    Info = {Bedrock::ORQ3mr, Bedrock::ORQ3mro, Bedrock::ORQ3mfi, true};
    return true;
  case Bedrock::XORL3rr:
    Info = {Bedrock::XORL3mr, Bedrock::XORL3mro, Bedrock::XORL3mfi, false};
    return true;
  case Bedrock::XORQ3rr:
    Info = {Bedrock::XORQ3mr, Bedrock::XORQ3mro, Bedrock::XORQ3mfi, true};
    return true;
  case Bedrock::MINUL3rr:
    Info = {Bedrock::MINUL3mr, Bedrock::MINUL3mro, Bedrock::MINUL3mfi,
            false};
    return true;
  case Bedrock::MINUQ3rr:
    Info = {Bedrock::MINUQ3mr, Bedrock::MINUQ3mro, Bedrock::MINUQ3mfi, true};
    return true;
  case Bedrock::MINSL3rr:
    Info = {Bedrock::MINSL3mr, Bedrock::MINSL3mro, Bedrock::MINSL3mfi,
            false};
    return true;
  case Bedrock::MINSQ3rr:
    Info = {Bedrock::MINSQ3mr, Bedrock::MINSQ3mro, Bedrock::MINSQ3mfi, true};
    return true;
  case Bedrock::MAXUL3rr:
    Info = {Bedrock::MAXUL3mr, Bedrock::MAXUL3mro, Bedrock::MAXUL3mfi,
            false};
    return true;
  case Bedrock::MAXUQ3rr:
    Info = {Bedrock::MAXUQ3mr, Bedrock::MAXUQ3mro, Bedrock::MAXUQ3mfi, true};
    return true;
  case Bedrock::MAXSL3rr:
    Info = {Bedrock::MAXSL3mr, Bedrock::MAXSL3mro, Bedrock::MAXSL3mfi,
            false};
    return true;
  case Bedrock::MAXSQ3rr:
    Info = {Bedrock::MAXSQ3mr, Bedrock::MAXSQ3mro, Bedrock::MAXSQ3mfi, true};
    return true;
  default:
    return false;
  }
}

static unsigned getBinaryMemDestOpcode(const BinaryMemDestFoldInfo &Info,
                                       MemAddrKind AddrKind) {
  switch (AddrKind) {
  case MemAddrKind::Reg:
    return Info.RegOpc;
  case MemAddrKind::RegIndex:
    return 0;
  case MemAddrKind::RegOffset:
    return Info.RegOffsetOpc;
  case MemAddrKind::Abs:
    return 0;
  case MemAddrKind::Frame:
    return Info.FrameOpc;
  }
  llvm_unreachable("unknown Bedrock memory address kind");
}

static bool getStoreAddrKindForBinaryMemDestFold(const MachineInstr &StoreMI,
                                                 bool Is64,
                                                 MemAddrKind &AddrKind) {
  switch (StoreMI.getOpcode()) {
  case Bedrock::STORELrr:
    if (Is64)
      return false;
    AddrKind = MemAddrKind::Reg;
    return true;
  case Bedrock::STOREQrr:
    AddrKind = MemAddrKind::Reg;
    return true;
  case Bedrock::STORELro:
    if (Is64)
      return false;
    AddrKind = MemAddrKind::RegOffset;
    return true;
  case Bedrock::STOREQro:
    AddrKind = MemAddrKind::RegOffset;
    return true;
  case Bedrock::STORELabs:
  case Bedrock::STOREQabs:
    return false;
  case Bedrock::STORELfi:
    if (Is64)
      return false;
    AddrKind = MemAddrKind::Frame;
    return true;
  case Bedrock::STOREQfi:
    AddrKind = MemAddrKind::Frame;
    return true;
  default:
    return false;
  }
}

static bool binaryMemDestAddressesMatch(const MachineInstr &LoadMI,
                                        const MachineInstr &StoreMI,
                                        MemAddrKind AddrKind) {
  switch (AddrKind) {
  case MemAddrKind::Reg:
    return LoadMI.getOperand(1).getReg() == StoreMI.getOperand(1).getReg();
  case MemAddrKind::RegIndex:
    return false;
  case MemAddrKind::RegOffset:
    return LoadMI.getOperand(1).getReg() == StoreMI.getOperand(1).getReg() &&
           LoadMI.getOperand(2).getImm() == StoreMI.getOperand(2).getImm();
  case MemAddrKind::Abs:
    return false;
  case MemAddrKind::Frame:
    return LoadMI.getOperand(1).getImm() == StoreMI.getOperand(1).getImm() &&
           LoadMI.getOperand(2).getImm() == StoreMI.getOperand(2).getImm();
  }
  llvm_unreachable("unknown Bedrock memory address kind");
}

static void addBinaryMemDestAddressOperands(MachineInstrBuilder &MIB,
                                            const MachineInstr &LoadMI,
                                            MemAddrKind AddrKind) {
  switch (AddrKind) {
  case MemAddrKind::Reg:
    MIB.add(LoadMI.getOperand(1));
    return;
  case MemAddrKind::RegIndex:
    llvm_unreachable("indexed binary memory destination pseudo is not "
                     "supported");
  case MemAddrKind::RegOffset:
  case MemAddrKind::Frame:
    MIB.add(LoadMI.getOperand(1)).add(LoadMI.getOperand(2));
    return;
  case MemAddrKind::Abs:
    llvm_unreachable("absolute binary memory destination pseudo is not "
                     "supported");
  }
  llvm_unreachable("unknown Bedrock memory address kind");
}

static bool foldBinaryMemDestPair(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator &I,
                                  const TargetInstrInfo &TII) {
  MachineInstr &LoadMI = *I;
  if (hasVolatileMemOperand(LoadMI))
    return false;

  auto BinI = std::next(I);
  while (BinI != MBB.end() && BinI->isDebugInstr())
    ++BinI;
  if (BinI == MBB.end())
    return false;

  BinaryMemDestFoldInfo Info;
  if (!getBinaryMemDestFoldInfo(BinI->getOpcode(), Info))
    return false;

  MemAddrKind LoadAddrKind;
  if (!getLoadAddrKindForBinaryFold(LoadMI, Info.Is64, LoadAddrKind) ||
      LoadAddrKind == MemAddrKind::Abs)
    return false;

  Register LoadReg = LoadMI.getOperand(0).getReg();
  if (loadAddressUsesReg(LoadMI, LoadAddrKind, LoadReg))
    return false;

  Register DstReg = BinI->getOperand(0).getReg();
  Register LHSReg = BinI->getOperand(1).getReg();
  Register RHSReg = BinI->getOperand(2).getReg();
  if (DstReg != LoadReg)
    return false;

  Register SrcReg;
  if (LHSReg == LoadReg && RHSReg != LoadReg) {
    SrcReg = RHSReg;
  } else if (RHSReg == LoadReg && LHSReg != LoadReg &&
             isCommutativeBinaryMemFoldOpcode(BinI->getOpcode())) {
    SrcReg = LHSReg;
  } else {
    return false;
  }

  auto StoreI = std::next(BinI);
  while (StoreI != MBB.end() && StoreI->isDebugInstr())
    ++StoreI;
  if (StoreI == MBB.end() || hasVolatileMemOperand(*StoreI))
    return false;

  MemAddrKind StoreAddrKind;
  if (!getStoreAddrKindForBinaryMemDestFold(*StoreI, Info.Is64,
                                            StoreAddrKind) ||
      StoreAddrKind != LoadAddrKind ||
      !binaryMemDestAddressesMatch(LoadMI, *StoreI, LoadAddrKind))
    return false;

  if (StoreI->getOperand(0).getReg() != LoadReg ||
      !StoreI->getOperand(0).isKill())
    return false;

  unsigned FoldOpcode = getBinaryMemDestOpcode(Info, LoadAddrKind);
  if (!FoldOpcode)
    return false;

  auto NextI = std::next(StoreI);
  MachineInstrBuilder MIB =
      BuildMI(MBB, LoadMI, BinI->getDebugLoc(), TII.get(FoldOpcode))
          .addReg(SrcReg)
          .setMIFlags(BinI->getFlags());
  addBinaryMemDestAddressOperands(MIB, LoadMI, LoadAddrKind);
  MIB.cloneMemRefs(LoadMI);
  MIB.cloneMemRefs(*StoreI);

  StoreI->eraseFromParent();
  BinI->eraseFromParent();
  LoadMI.eraseFromParent();
  I = NextI;
  return true;
}

static bool isCommutativeLongMemDestFoldOpcode(unsigned Opc) {
  switch (Opc) {
  case Bedrock::MULL3rr:
  case Bedrock::MULQ3rr:
  case Bedrock::MINUL3rr:
  case Bedrock::MINUQ3rr:
  case Bedrock::MINSL3rr:
  case Bedrock::MINSQ3rr:
  case Bedrock::MAXUL3rr:
  case Bedrock::MAXUQ3rr:
  case Bedrock::MAXSL3rr:
  case Bedrock::MAXSQ3rr:
    return true;
  default:
    return false;
  }
}

static bool foldCommutativeLongMemDestPair(MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator &I,
                                           const TargetInstrInfo &TII) {
  MachineInstr &LoadMI = *I;
  if (hasVolatileMemOperand(LoadMI))
    return false;

  auto BinI = std::next(I);
  while (BinI != MBB.end() && BinI->isDebugInstr())
    ++BinI;
  if (BinI == MBB.end() ||
      !isCommutativeLongMemDestFoldOpcode(BinI->getOpcode()))
    return false;

  BinaryMemFoldInfo Info;
  if (!getBinaryMemFoldInfo(BinI->getOpcode(), Info))
    return false;

  MemAddrKind AddrKind;
  if (!getLoadAddrKindForBinaryFold(LoadMI, Info.Is64, AddrKind))
    return false;

  Register LoadReg = LoadMI.getOperand(0).getReg();
  Register RHSReg = BinI->getOperand(2).getReg();
  if (BinI->getOperand(0).getReg() != LoadReg ||
      BinI->getOperand(1).getReg() != LoadReg || RHSReg == LoadReg ||
      loadAddressUsesReg(LoadMI, AddrKind, LoadReg))
    return false;

  unsigned FoldOpcode = getBinaryMemOpcode(Info, AddrKind);
  if (!FoldOpcode)
    return false;

  auto NextI = std::next(BinI);
  BuildMI(MBB, BinI, BinI->getDebugLoc(),
          TII.get(Info.Is64 ? Bedrock::MOVQrr : Bedrock::MOVLrr), LoadReg)
      .add(BinI->getOperand(2));
  MachineInstrBuilder MIB =
      BuildMI(MBB, BinI, BinI->getDebugLoc(), TII.get(FoldOpcode), LoadReg)
          .addReg(LoadReg, RegState::Kill)
          .setMIFlags(BinI->getFlags());
  for (unsigned OpIdx = 1, OpEnd = LoadMI.getNumOperands(); OpIdx != OpEnd;
       ++OpIdx)
    MIB.add(LoadMI.getOperand(OpIdx));
  MIB.cloneMemRefs(LoadMI);

  BinI->eraseFromParent();
  LoadMI.eraseFromParent();
  I = NextI;
  return true;
}

static unsigned getCmpMemOpcode(unsigned CmpOpc, MemAddrKind AddrKind,
                                bool MemIsSrc) {
  bool Is64 = CmpOpc == Bedrock::CMPQrr;
  if (CmpOpc != Bedrock::CMPLrr && CmpOpc != Bedrock::CMPQrr)
    return 0;

  if (MemIsSrc) {
    switch (AddrKind) {
    case MemAddrKind::Reg:
      return Is64 ? Bedrock::CMPQmr : Bedrock::CMPLmr;
    case MemAddrKind::RegIndex:
      return Is64 ? Bedrock::CMPQmxr : Bedrock::CMPLmxr;
    case MemAddrKind::RegOffset:
      return Is64 ? Bedrock::CMPQmor : Bedrock::CMPLmor;
    case MemAddrKind::Abs:
      return Is64 ? Bedrock::CMPQmabsr : Bedrock::CMPLmabsr;
    case MemAddrKind::Frame:
      return Is64 ? Bedrock::CMPQmfir : Bedrock::CMPLmfir;
    }
  }

  switch (AddrKind) {
  case MemAddrKind::Reg:
    return Is64 ? Bedrock::CMPQrm : Bedrock::CMPLrm;
  case MemAddrKind::RegIndex:
    return Is64 ? Bedrock::CMPQrmx : Bedrock::CMPLrmx;
  case MemAddrKind::RegOffset:
    return Is64 ? Bedrock::CMPQrmo : Bedrock::CMPLrmo;
  case MemAddrKind::Abs:
    return Is64 ? Bedrock::CMPQrmabs : Bedrock::CMPLrmabs;
  case MemAddrKind::Frame:
    return Is64 ? Bedrock::CMPQrmfi : Bedrock::CMPLrmfi;
  }
  llvm_unreachable("unknown Bedrock memory address kind");
}

static bool foldCmpMemPair(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator &I,
                           const TargetInstrInfo &TII) {
  MachineInstr &LoadMI = *I;
  if (hasVolatileMemOperand(LoadMI))
    return false;

  auto CmpI = std::next(I);
  while (CmpI != MBB.end() && CmpI->isDebugInstr())
    ++CmpI;
  if (CmpI == MBB.end() ||
      (CmpI->getOpcode() != Bedrock::CMPLrr &&
       CmpI->getOpcode() != Bedrock::CMPQrr))
    return false;

  bool Is64 = CmpI->getOpcode() == Bedrock::CMPQrr;
  MemAddrKind AddrKind;
  if (!getLoadAddrKindForBinaryFold(LoadMI, Is64, AddrKind))
    return false;

  Register LoadReg = LoadMI.getOperand(0).getReg();
  bool MemIsSrc = CmpI->getOperand(0).getReg() == LoadReg &&
                  CmpI->getOperand(0).isKill() &&
                  CmpI->getOperand(1).getReg() != LoadReg;
  bool MemIsDst = CmpI->getOperand(1).getReg() == LoadReg &&
                  CmpI->getOperand(1).isKill() &&
                  CmpI->getOperand(0).getReg() != LoadReg;
  if (!MemIsSrc && !MemIsDst)
    return false;

  auto NextI = std::next(CmpI);
  MachineInstrBuilder MIB =
      BuildMI(MBB, CmpI, CmpI->getDebugLoc(),
              TII.get(getCmpMemOpcode(CmpI->getOpcode(), AddrKind, MemIsSrc)))
          .setMIFlags(CmpI->getFlags());
  if (MemIsSrc) {
    for (unsigned OpIdx = 1, OpEnd = LoadMI.getNumOperands(); OpIdx != OpEnd;
         ++OpIdx)
      MIB.add(LoadMI.getOperand(OpIdx));
    MIB.add(CmpI->getOperand(1));
  } else {
    MIB.add(CmpI->getOperand(0));
    for (unsigned OpIdx = 1, OpEnd = LoadMI.getNumOperands(); OpIdx != OpEnd;
         ++OpIdx)
      MIB.add(LoadMI.getOperand(OpIdx));
  }
  MIB.cloneMemRefs(LoadMI);

  CmpI->eraseFromParent();
  LoadMI.eraseFromParent();
  I = NextI;
  return true;
}

static bool foldLoadCmpOneMemPair(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator &I,
                                  const TargetInstrInfo &TII) {
  MachineInstr &LoadMI = *I;
  if (hasVolatileMemOperand(LoadMI))
    return false;

  auto CmpI = std::next(I);
  while (CmpI != MBB.end() && CmpI->isDebugInstr())
    ++CmpI;
  if (CmpI == MBB.end() ||
      (CmpI->getOpcode() != Bedrock::CMPLri &&
       CmpI->getOpcode() != Bedrock::CMPQri))
    return false;

  bool Is64 = CmpI->getOpcode() == Bedrock::CMPQri;
  MemAddrKind AddrKind;
  if (!getLoadAddrKindForBinaryFold(LoadMI, Is64, AddrKind))
    return false;

  Register LoadReg = LoadMI.getOperand(0).getReg();
  if (loadAddressUsesReg(LoadMI, AddrKind, LoadReg))
    return false;
  if (CmpI->getOperand(0).getReg() != LoadReg ||
      !CmpI->getOperand(0).isKill() || !CmpI->getOperand(1).isImm())
    return false;

  int64_t Imm = CmpI->getOperand(1).getImm();
  if (Imm != 1 && Imm != -1)
    return false;

  unsigned CmpMemOpc =
      getCmpMemOpcode(Is64 ? Bedrock::CMPQrr : Bedrock::CMPLrr, AddrKind,
                      /*MemIsSrc=*/false);
  if (!CmpMemOpc)
    return false;

  auto NextI = std::next(CmpI);
  BuildMI(MBB, LoadMI, LoadMI.getDebugLoc(),
          TII.get(Is64 ? Bedrock::CONST64 : Bedrock::CONST32), LoadReg)
      .addImm(Imm);

  MachineInstrBuilder MIB =
      BuildMI(MBB, CmpI, CmpI->getDebugLoc(), TII.get(CmpMemOpc))
          .setMIFlags(CmpI->getFlags());
  MIB.addReg(LoadReg, RegState::Kill);
  for (unsigned OpIdx = 1, OpEnd = LoadMI.getNumOperands(); OpIdx != OpEnd;
       ++OpIdx)
    MIB.add(LoadMI.getOperand(OpIdx));
  MIB.cloneMemRefs(LoadMI);

  CmpI->eraseFromParent();
  LoadMI.eraseFromParent();
  I = NextI;
  return true;
}

static unsigned getFrameFrameCmpOpcode(unsigned CmpOpc) {
  switch (CmpOpc) {
  case Bedrock::CMPLmfir:
  case Bedrock::CMPLrmfi:
    return Bedrock::CMPLmfmf;
  case Bedrock::CMPQmfir:
  case Bedrock::CMPQrmfi:
    return Bedrock::CMPQmfmf;
  default:
    return 0;
  }
}

static bool foldCmpFrameFramePair(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator &I,
                                  const TargetInstrInfo &TII) {
  MachineInstr &LoadMI = *I;
  if (hasVolatileMemOperand(LoadMI))
    return false;

  bool IsLongLoad = LoadMI.getOpcode() == Bedrock::LOADLfi;
  bool IsQuadLoad = LoadMI.getOpcode() == Bedrock::LOADQfi;
  if (!IsLongLoad && !IsQuadLoad)
    return false;

  auto CmpI = std::next(I);
  while (CmpI != MBB.end() && CmpI->isDebugInstr())
    ++CmpI;
  if (CmpI == MBB.end() || hasVolatileMemOperand(*CmpI))
    return false;

  unsigned CmpOpc = CmpI->getOpcode();
  unsigned FrameFrameOpc = getFrameFrameCmpOpcode(CmpOpc);
  if (!FrameFrameOpc)
    return false;
  if ((IsLongLoad && FrameFrameOpc != Bedrock::CMPLmfmf) ||
      (FrameFrameOpc == Bedrock::CMPQmfmf && !IsQuadLoad))
    return false;

  Register LoadReg = LoadMI.getOperand(0).getReg();
  auto NextI = std::next(CmpI);
  bool LoadedFrameIsSrc;
  switch (CmpOpc) {
  case Bedrock::CMPLmfir:
  case Bedrock::CMPQmfir:
    if (CmpI->getOperand(2).getReg() != LoadReg ||
        !CmpI->getOperand(2).isKill())
      return false;
    LoadedFrameIsSrc = false;
    break;
  case Bedrock::CMPLrmfi:
  case Bedrock::CMPQrmfi:
    if (CmpI->getOperand(0).getReg() != LoadReg ||
        !CmpI->getOperand(0).isKill())
      return false;
    LoadedFrameIsSrc = true;
    break;
  default:
    llvm_unreachable("unexpected Bedrock frame compare opcode");
  }

  MachineInstrBuilder MIB =
      BuildMI(MBB, CmpI, CmpI->getDebugLoc(), TII.get(FrameFrameOpc))
          .setMIFlags(CmpI->getFlags());
  if (LoadedFrameIsSrc) {
    MIB.add(LoadMI.getOperand(1))
        .add(LoadMI.getOperand(2))
        .add(CmpI->getOperand(1))
        .add(CmpI->getOperand(2));
  } else {
    MIB.add(CmpI->getOperand(0))
        .add(CmpI->getOperand(1))
        .add(LoadMI.getOperand(1))
        .add(LoadMI.getOperand(2));
  }
  MIB.cloneMemRefs(LoadMI);
  MIB.cloneMemRefs(*CmpI);

  CmpI->eraseFromParent();
  LoadMI.eraseFromParent();
  I = NextI;
  return true;
}

static bool foldRegMemMovePair(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator &I,
                               const TargetInstrInfo &TII) {
  MachineInstr &LoadMI = *I;
  unsigned MovOpc;
  unsigned StoreOpc;
  if (LoadMI.getOpcode() == Bedrock::LOADLrr) {
    MovOpc = Bedrock::MOVLmmrr;
    StoreOpc = Bedrock::STORELrr;
  } else if (LoadMI.getOpcode() == Bedrock::LOADQrr) {
    MovOpc = Bedrock::MOVQmmrr;
    StoreOpc = Bedrock::STOREQrr;
  } else {
    return false;
  }
  if (hasVolatileMemOperand(LoadMI))
    return false;

  auto StoreI = std::next(I);
  while (StoreI != MBB.end() && StoreI->isDebugInstr())
    ++StoreI;
  if (StoreI == MBB.end() || StoreI->getOpcode() != StoreOpc ||
      hasVolatileMemOperand(*StoreI))
    return false;

  Register LoadReg = LoadMI.getOperand(0).getReg();
  if (StoreI->getOperand(0).getReg() != LoadReg ||
      !StoreI->getOperand(0).isKill() ||
      StoreI->getOperand(1).getReg() == LoadReg)
    return false;

  auto NextI = std::next(StoreI);
  MachineInstrBuilder MIB =
      BuildMI(MBB, StoreI, StoreI->getDebugLoc(), TII.get(MovOpc))
          .add(LoadMI.getOperand(1))
          .add(StoreI->getOperand(1));
  MIB.cloneMemRefs(LoadMI);
  MIB.cloneMemRefs(*StoreI);

  StoreI->eraseFromParent();
  LoadMI.eraseFromParent();
  I = NextI;
  return true;
}

static bool getPostIncMemInfo(const MachineInstr &MI, unsigned &BaseOp,
                              unsigned &AccessSize, Register &DataReg) {
  switch (MI.getOpcode()) {
  case Bedrock::LOADB_Zrr:
  case Bedrock::LOADB_Srr:
    BaseOp = 1;
    AccessSize = 1;
    DataReg = MI.getOperand(0).getReg();
    return true;
  case Bedrock::LOADW_Zrr:
  case Bedrock::LOADW_Srr:
    BaseOp = 1;
    AccessSize = 2;
    DataReg = MI.getOperand(0).getReg();
    return true;
  case Bedrock::LOADL_Zrr:
  case Bedrock::LOADL_Srr:
  case Bedrock::LOADLrr:
    BaseOp = 1;
    AccessSize = 4;
    DataReg = MI.getOperand(0).getReg();
    return true;
  case Bedrock::LOADQrr:
    BaseOp = 1;
    AccessSize = 8;
    DataReg = MI.getOperand(0).getReg();
    return true;
  case Bedrock::STOREBrr:
    BaseOp = 1;
    AccessSize = 1;
    DataReg = MI.getOperand(0).getReg();
    return true;
  case Bedrock::STOREWrr:
    BaseOp = 1;
    AccessSize = 2;
    DataReg = MI.getOperand(0).getReg();
    return true;
  case Bedrock::STORELrr:
    BaseOp = 1;
    AccessSize = 4;
    DataReg = MI.getOperand(0).getReg();
    return true;
  case Bedrock::STOREQrr:
    BaseOp = 1;
    AccessSize = 8;
    DataReg = MI.getOperand(0).getReg();
    return true;
  default:
    return false;
  }
}

static bool hasRegOperand(const MachineInstr &MI, Register Reg) {
  return any_of(MI.operands(), [Reg](const MachineOperand &MO) {
    return MO.isReg() && MO.getReg() == Reg;
  });
}

static bool definesReg(const MachineInstr &MI, Register Reg) {
  return any_of(MI.operands(), [Reg](const MachineOperand &MO) {
    return MO.isReg() && MO.isDef() && MO.getReg() == Reg;
  });
}

static bool usesReg(const MachineInstr &MI, Register Reg) {
  return any_of(MI.operands(), [Reg](const MachineOperand &MO) {
    return MO.isReg() && !MO.isDef() && MO.getReg() == Reg;
  });
}

static bool canScanAcrossForPostIncAdd(const MachineInstr &MI,
                                       Register BaseReg) {
  if (MI.isCall() || MI.isTerminator() || hasRegOperand(MI, BaseReg))
    return false;
  return !MI.mayLoadOrStore() || !hasVolatileMemOperand(MI);
}

static bool mayReadFlags(const MachineInstr &MI) {
  return usesReg(MI, Bedrock::FLAGS);
}

static MachineBasicBlock::const_iterator
nextNonDebug(MachineBasicBlock::const_iterator I,
             const MachineBasicBlock &MBB) {
  while (I != MBB.end() && I->isDebugInstr())
    ++I;
  return I;
}

static MachineBasicBlock::const_iterator
firstNonDebug(const MachineBasicBlock &MBB) {
  return nextNonDebug(MBB.begin(), MBB);
}

static MachineBasicBlock::const_iterator
prevNonDebug(MachineBasicBlock::const_iterator I,
             const MachineBasicBlock &MBB, bool &Missing) {
  do {
    if (I == MBB.begin()) {
      Missing = true;
      return I;
    }
    --I;
  } while (I->isDebugInstr());
  return I;
}

static bool writesFlagsForPostIncScan(const MachineInstr &MI) {
  return definesReg(MI, Bedrock::FLAGS);
}

static bool canRemovePostIncAddFrom(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator CheckI,
                                    Register BaseReg) {
  bool FlagsRedefined = false;
  for (;; ++CheckI) {
    while (CheckI != MBB.end() && CheckI->isDebugInstr())
      ++CheckI;
    if (CheckI == MBB.end() || CheckI->getOpcode() == Bedrock::BR)
      return true;

    if (CheckI->getOpcode() == Bedrock::BRCC)
      return FlagsRedefined;

    if (CheckI->isCall())
      return false;

    if (CheckI->isTerminator())
      return !mayReadFlags(*CheckI);

    if (mayReadFlags(*CheckI) && !FlagsRedefined)
      return false;

    if (writesFlagsForPostIncScan(*CheckI))
      FlagsRedefined = true;
  }
}

static bool isPostIncAdd(const MachineInstr &MI, Register BaseReg,
                         unsigned AccessSize) {
  if (MI.getNumExplicitOperands() < 3)
    return false;
  if (!MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
      !MI.getOperand(2).isImm())
    return false;

  switch (MI.getOpcode()) {
  case Bedrock::ADDQ3ri:
  case Bedrock::LEAro:
    return MI.getOperand(0).getReg() == BaseReg &&
           MI.getOperand(1).getReg() == BaseReg &&
           MI.getOperand(2).getImm() == AccessSize;
  default:
    return false;
  }
}

static bool foldPostIncMemPair(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator &I,
                               DenseSet<const MachineInstr *> &PostIncMemOps) {
  MachineInstr &MemMI = *I;
  if (hasVolatileMemOperand(MemMI))
    return false;

  unsigned BaseOp = 0;
  unsigned AccessSize = 0;
  Register DataReg;
  if (!getPostIncMemInfo(MemMI, BaseOp, AccessSize, DataReg))
    return false;

  Register BaseReg = MemMI.getOperand(BaseOp).getReg();
  if (DataReg == BaseReg)
    return false;

  auto AddI = std::next(I);
  MachineInstr *AddToErase = nullptr;
  for (;;) {
    while (AddI != MBB.end() && AddI->isDebugInstr())
      ++AddI;
    if (AddI == MBB.end())
      return false;

    if (isPostIncAdd(*AddI, BaseReg, AccessSize)) {
      AddToErase = &*AddI;
      ++AddI;
      break;
    }

    if (!canScanAcrossForPostIncAdd(*AddI, BaseReg))
      return false;
    ++AddI;
  }

  if (!canRemovePostIncAddFrom(MBB, AddI, BaseReg))
    return false;

  PostIncMemOps.insert(&MemMI);
  AddToErase->eraseFromParent();
  I = AddI;
  return true;
}

static bool foldPostIncMemAcrossFallthrough(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator &I,
    DenseSet<const MachineInstr *> &PostIncMemOps) {
  MachineInstr &MemMI = *I;
  if (PostIncMemOps.contains(&MemMI) || hasVolatileMemOperand(MemMI))
    return false;

  unsigned BaseOp = 0;
  unsigned AccessSize = 0;
  Register DataReg;
  if (!getPostIncMemInfo(MemMI, BaseOp, AccessSize, DataReg))
    return false;

  Register BaseReg = MemMI.getOperand(BaseOp).getReg();
  if (DataReg == BaseReg)
    return false;

  auto BranchI = std::next(I);
  for (;;) {
    while (BranchI != MBB.end() && BranchI->isDebugInstr())
      ++BranchI;
    if (BranchI == MBB.end())
      return false;
    if (BranchI->getOpcode() == Bedrock::BRCC)
      break;
    if (!canScanAcrossForPostIncAdd(*BranchI, BaseReg))
      return false;
    ++BranchI;
  }

  auto MBBI = MBB.getIterator();
  auto FallthroughI = std::next(MBBI);
  if (FallthroughI == MF.end())
    return false;

  MachineBasicBlock *Fallthrough = &*FallthroughI;
  MachineBasicBlock *BranchTarget = BranchI->getOperand(0).getMBB();
  if (BranchTarget == Fallthrough || !MBB.isSuccessor(Fallthrough) ||
      !MBB.isSuccessor(BranchTarget) || Fallthrough->pred_size() != 1 ||
      BranchTarget->isLiveIn(BaseReg))
    return false;

  auto AddI = Fallthrough->begin();
  while (AddI != Fallthrough->end() && AddI->isDebugInstr())
    ++AddI;
  if (AddI == Fallthrough->end() ||
      !isPostIncAdd(*AddI, BaseReg, AccessSize))
    return false;

  auto AfterAddI = std::next(AddI);
  if (!canRemovePostIncAddFrom(*Fallthrough, AfterAddI, BaseReg))
    return false;

  PostIncMemOps.insert(&MemMI);
  AddI->eraseFromParent();
  I = std::next(I);
  return true;
}

static bool getPostIncBinaryMemInfo(const MachineInstr &MI, unsigned &BaseOp,
                                    unsigned &AccessSize) {
  switch (MI.getOpcode()) {
#define HANDLE_POSTINC_BINARY_MEM(OP, SIZE)                                  \
  case Bedrock::OP##rm:                                                      \
    BaseOp = 2;                                                              \
    AccessSize = SIZE;                                                       \
    return true;
    HANDLE_POSTINC_BINARY_MEM(ADDL3, 4)
    HANDLE_POSTINC_BINARY_MEM(ADDQ3, 8)
    HANDLE_POSTINC_BINARY_MEM(SUBL3, 4)
    HANDLE_POSTINC_BINARY_MEM(SUBQ3, 8)
    HANDLE_POSTINC_BINARY_MEM(ANDL3, 4)
    HANDLE_POSTINC_BINARY_MEM(ANDQ3, 8)
    HANDLE_POSTINC_BINARY_MEM(ORL3, 4)
    HANDLE_POSTINC_BINARY_MEM(ORQ3, 8)
    HANDLE_POSTINC_BINARY_MEM(XORL3, 4)
    HANDLE_POSTINC_BINARY_MEM(XORQ3, 8)
    HANDLE_POSTINC_BINARY_MEM(MULL3, 4)
    HANDLE_POSTINC_BINARY_MEM(MULQ3, 8)
    HANDLE_POSTINC_BINARY_MEM(MINUL3, 4)
    HANDLE_POSTINC_BINARY_MEM(MINUQ3, 8)
    HANDLE_POSTINC_BINARY_MEM(MINSL3, 4)
    HANDLE_POSTINC_BINARY_MEM(MINSQ3, 8)
    HANDLE_POSTINC_BINARY_MEM(MAXUL3, 4)
    HANDLE_POSTINC_BINARY_MEM(MAXUQ3, 8)
    HANDLE_POSTINC_BINARY_MEM(MAXSL3, 4)
    HANDLE_POSTINC_BINARY_MEM(MAXSQ3, 8)
    HANDLE_POSTINC_BINARY_MEM(DIVUL3, 4)
    HANDLE_POSTINC_BINARY_MEM(DIVUQ3, 8)
    HANDLE_POSTINC_BINARY_MEM(DIVSL3, 4)
    HANDLE_POSTINC_BINARY_MEM(DIVSQ3, 8)
    HANDLE_POSTINC_BINARY_MEM(MODUL3, 4)
    HANDLE_POSTINC_BINARY_MEM(MODUQ3, 8)
    HANDLE_POSTINC_BINARY_MEM(MODSL3, 4)
    HANDLE_POSTINC_BINARY_MEM(MODSQ3, 8)
#undef HANDLE_POSTINC_BINARY_MEM
  default:
    return false;
  }
}

static bool
foldPostIncBinaryMemPair(MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator &I,
                         DenseSet<const MachineInstr *> &PostIncBinaryMemOps) {
  MachineInstr &MemMI = *I;
  if (hasVolatileMemOperand(MemMI))
    return false;

  unsigned BaseOp = 0;
  unsigned AccessSize = 0;
  if (!getPostIncBinaryMemInfo(MemMI, BaseOp, AccessSize))
    return false;

  Register BaseReg = MemMI.getOperand(BaseOp).getReg();
  if (BaseReg == MemMI.getOperand(0).getReg() ||
      BaseReg == MemMI.getOperand(1).getReg())
    return false;

  auto AddI = std::next(I);
  MachineInstr *AddToErase = nullptr;
  for (;;) {
    while (AddI != MBB.end() && AddI->isDebugInstr())
      ++AddI;
    if (AddI == MBB.end())
      return false;

    if (isPostIncAdd(*AddI, BaseReg, AccessSize)) {
      AddToErase = &*AddI;
      ++AddI;
      break;
    }

    if (!canScanAcrossForPostIncAdd(*AddI, BaseReg))
      return false;
    ++AddI;
  }

  if (!canRemovePostIncAddFrom(MBB, AddI, BaseReg))
    return false;

  PostIncBinaryMemOps.insert(&MemMI);
  AddToErase->eraseFromParent();
  I = AddI;
  return true;
}

static bool getPostIncMemMoveInfo(const MachineInstr &MI,
                                  unsigned &AccessSize) {
  switch (MI.getOpcode()) {
  case Bedrock::MOVLmmrr:
    AccessSize = 4;
    return true;
  case Bedrock::MOVQmmrr:
    AccessSize = 8;
    return true;
  default:
    return false;
  }
}

static bool foldPostIncMemMovePair(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator &I,
    DenseSet<const MachineInstr *> &PostIncMemMoveSrcOps,
    DenseSet<const MachineInstr *> &PostIncMemMoveDstOps) {
  MachineInstr &MoveMI = *I;
  if (hasVolatileMemOperand(MoveMI))
    return false;

  unsigned AccessSize = 0;
  if (!getPostIncMemMoveInfo(MoveMI, AccessSize))
    return false;

  Register SrcBase = MoveMI.getOperand(0).getReg();
  Register DstBase = MoveMI.getOperand(1).getReg();
  if (SrcBase == DstBase)
    return false;

  bool FoldedSrc = false;
  bool FoldedDst = false;
  SmallVector<MachineInstr *, 2> AddsToErase;
  auto ScanI = std::next(I);
  for (;;) {
    while (ScanI != MBB.end() && ScanI->isDebugInstr())
      ++ScanI;
    if (ScanI == MBB.end())
      break;

    if (isPostIncAdd(*ScanI, SrcBase, AccessSize) ||
        isPostIncAdd(*ScanI, DstBase, AccessSize)) {
      bool IsSrcAdd = isPostIncAdd(*ScanI, SrcBase, AccessSize) && !FoldedSrc;
      bool IsDstAdd = isPostIncAdd(*ScanI, DstBase, AccessSize) && !FoldedDst;
      if (!IsSrcAdd && !IsDstAdd)
        break;

      if (IsSrcAdd)
        FoldedSrc = true;
      else
        FoldedDst = true;
      AddsToErase.push_back(&*ScanI);
      ++ScanI;
      continue;
    }

    if (FoldedSrc || FoldedDst)
      break;

    if (!canScanAcrossForPostIncAdd(*ScanI, SrcBase) ||
        !canScanAcrossForPostIncAdd(*ScanI, DstBase))
      return false;
    ++ScanI;
  }

  if (!FoldedSrc && !FoldedDst)
    return false;

  if (FoldedSrc && !canRemovePostIncAddFrom(MBB, ScanI, SrcBase))
    return false;
  if (FoldedDst && !canRemovePostIncAddFrom(MBB, ScanI, DstBase))
    return false;

  for (MachineInstr *MI : AddsToErase)
    MI->eraseFromParent();

  if (FoldedSrc)
    PostIncMemMoveSrcOps.insert(&MoveMI);
  if (FoldedDst)
    PostIncMemMoveDstOps.insert(&MoveMI);
  I = ScanI;
  return true;
}

static bool foldImmediateStores(
    MachineFunction &MF, DenseSet<const MachineInstr *> &PostIncMemOps,
    DenseSet<const MachineInstr *> &PostIncBinaryMemOps,
    DenseSet<const MachineInstr *> &PostIncMemMoveSrcOps,
    DenseSet<const MachineInstr *> &PostIncMemMoveDstOps) {
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    bool LocalChanged;
    do {
      LocalChanged = false;
      for (auto I = MBB.begin(); I != MBB.end();) {
        if (foldConstStorePair(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldRedundantFrameLoadStorePair(MBB, I)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldFrameIncDecStorePair(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldSimpleMemIncDecStorePair(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldBinaryMemDestPair(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldBinaryMemSourcePair(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldCommutativeMemSourceStorePair(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldSPIndexLoad(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldSPIndexStore(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldCopiedBaseIndexLoad(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldCopiedBaseIndexStore(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldAddIndexMem(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldScaledIndexMem(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldCommutativeMemSourceCopyBack(MBB, I)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldCopyIntoCommutativeRR(MBB, I)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldSimpleDefCopyBack(MBB, I)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldCommutativeLongMemDestPair(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldLoadCmpOneMemPair(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldCmpMemPair(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldCmpFrameFramePair(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldRegMemMovePair(MBB, I, TII)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldPostIncMemPair(MBB, I, PostIncMemOps)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldPostIncMemAcrossFallthrough(MF, MBB, I, PostIncMemOps)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldPostIncBinaryMemPair(MBB, I, PostIncBinaryMemOps)) {
          Changed = LocalChanged = true;
          continue;
        }
        if (foldPostIncMemMovePair(MBB, I, PostIncMemMoveSrcOps,
                                   PostIncMemMoveDstOps)) {
          Changed = LocalChanged = true;
          continue;
        }
        ++I;
      }
    } while (LocalChanged);
  }
  return Changed;
}

static unsigned getOptionalRegCopySize(const MachineInstr &MI, unsigned DstOp,
                                       unsigned SrcOp) {
  return MI.getOperand(DstOp).getReg() == MI.getOperand(SrcOp).getReg() ? 0 : 2;
}

static unsigned getOffsetTailSize(int64_t Offset) {
  return Offset == 0 ? 0 : getSignedAutoSize(Offset);
}

static unsigned getFrameTailSize(const MachineInstr &MI, unsigned BaseOp) {
  return getOffsetTailSize(getFrameOffset(&MI, BaseOp));
}

static unsigned getMemAddrTailSize(const MachineInstr &MI, unsigned BaseOp,
                                   MemAddrKind AddrKind) {
  switch (AddrKind) {
  case MemAddrKind::Reg:
    return 0;
  case MemAddrKind::RegIndex:
    return 2;
  case MemAddrKind::RegOffset:
    return getOffsetTailSize(MI.getOperand(BaseOp + 1).getImm());
  case MemAddrKind::Abs:
    return 4;
  case MemAddrKind::Frame:
    return getFrameTailSize(MI, BaseOp);
  }
  llvm_unreachable("unknown Bedrock memory address kind");
}

static unsigned getConstSize(const MachineInstr &MI) {
  const MachineOperand &ImmOp = MI.getOperand(1);
  if (!ImmOp.isImm()) {
    switch (ImmOp.getTargetFlags()) {
    case BedrockII::MO_ABS64:
    case BedrockII::MO_PCREL64:
    case BedrockII::MO_PLT64:
      return 11;
    case BedrockII::MO_GOTPCREL32:
      return 10;
    case BedrockII::MO_GOTPCREL64:
      return 14;
    case BedrockII::MO_TLS_LE32:
      return 8;
    case BedrockII::MO_TLS_LE64:
      return 12;
    default:
      return 7;
    }
  }
  int64_t Imm = ImmOp.getImm();
  if (Imm == 0)
    return 1;
  if (Imm == 1)
    return 2;
  if (Imm == -1)
    return 3;
  return 3 + getSignedAutoSize(Imm);
}

static unsigned getBinaryRegPseudoSize(const MachineInstr &MI,
                                       unsigned OpcodeSize) {
  return getOptionalRegCopySize(MI, 0, 1) + OpcodeSize;
}

static unsigned getBinaryImmPseudoSize(const MachineInstr &MI) {
  return getOptionalRegCopySize(MI, 0, 1) + 3 +
         getSignedAutoSize(MI.getOperand(2).getImm());
}

static unsigned getBinaryMemPseudoSize(const MachineInstr &MI, bool IsLong,
                                       MemAddrKind AddrKind) {
  return getOptionalRegCopySize(MI, 0, 1) + (IsLong ? 4 : 3) +
         getMemAddrTailSize(MI, 2, AddrKind);
}

static unsigned getBinaryMemDestPseudoSize(const MachineInstr &MI, bool IsLong,
                                           MemAddrKind AddrKind) {
  return (IsLong ? 4 : 3) + getMemAddrTailSize(MI, 1, AddrKind);
}

static unsigned getCmpMemPseudoSize(const MachineInstr &MI,
                                    MemAddrKind AddrKind, bool MemIsSrc) {
  return 3 + getMemAddrTailSize(MI, MemIsSrc ? 0 : 1, AddrKind);
}

static unsigned getImmStoreSize(const MachineInstr &MI, bool IsFrame) {
  return 4 + getSignedAutoSize(MI.getOperand(0).getImm()) +
         (IsFrame ? getFrameTailSize(MI, 1) : 0);
}

static unsigned getImmStoreOffsetSize(const MachineInstr &MI) {
  return 4 + getSignedAutoSize(MI.getOperand(0).getImm()) +
         getOffsetTailSize(MI.getOperand(2).getImm());
}

static unsigned getImmStoreAbsSize(const MachineInstr &MI) {
  return 8 + getSignedAutoSize(MI.getOperand(0).getImm());
}

static unsigned getConstStoreFoldSize(const MachineInstr &StoreMI,
                                      MemAddrKind AddrKind) {
  return 8 + getMemAddrTailSize(StoreMI, 1, AddrKind);
}

static bool getRegStoreInfo(const MachineInstr &MI, unsigned &Size,
                            MemAddrKind &AddrKind, Register &SrcReg) {
  switch (MI.getOpcode()) {
  case Bedrock::STOREBrr:
    Size = 0;
    AddrKind = MemAddrKind::Reg;
    break;
  case Bedrock::STOREWrr:
    Size = 1;
    AddrKind = MemAddrKind::Reg;
    break;
  case Bedrock::STORELrr:
    Size = 2;
    AddrKind = MemAddrKind::Reg;
    break;
  case Bedrock::STOREQrr:
    Size = 3;
    AddrKind = MemAddrKind::Reg;
    break;
  case Bedrock::STOREBro:
    Size = 0;
    AddrKind = MemAddrKind::RegOffset;
    break;
  case Bedrock::STOREWro:
    Size = 1;
    AddrKind = MemAddrKind::RegOffset;
    break;
  case Bedrock::STORELro:
    Size = 2;
    AddrKind = MemAddrKind::RegOffset;
    break;
  case Bedrock::STOREQro:
    Size = 3;
    AddrKind = MemAddrKind::RegOffset;
    break;
  case Bedrock::STOREBabs:
    Size = 0;
    AddrKind = MemAddrKind::Abs;
    break;
  case Bedrock::STOREWabs:
    Size = 1;
    AddrKind = MemAddrKind::Abs;
    break;
  case Bedrock::STORELabs:
    Size = 2;
    AddrKind = MemAddrKind::Abs;
    break;
  case Bedrock::STOREQabs:
    Size = 3;
    AddrKind = MemAddrKind::Abs;
    break;
  case Bedrock::STOREBfi:
    Size = 0;
    AddrKind = MemAddrKind::Frame;
    break;
  case Bedrock::STOREWfi:
    Size = 1;
    AddrKind = MemAddrKind::Frame;
    break;
  case Bedrock::STORELfi:
    Size = 2;
    AddrKind = MemAddrKind::Frame;
    break;
  case Bedrock::STOREQfi:
    Size = 3;
    AddrKind = MemAddrKind::Frame;
    break;
  default:
    return false;
  }

  if (!MI.getOperand(0).isReg())
    return false;
  SrcReg = MI.getOperand(0).getReg();
  return true;
}

static bool getImmZeroStoreInfo(const MachineInstr &MI, unsigned &Size,
                                MemAddrKind &AddrKind) {
  switch (MI.getOpcode()) {
  case Bedrock::STOREB_Immrr:
    Size = 0;
    AddrKind = MemAddrKind::Reg;
    break;
  case Bedrock::STOREW_Immrr:
    Size = 1;
    AddrKind = MemAddrKind::Reg;
    break;
  case Bedrock::STOREL_Immrr:
    Size = 2;
    AddrKind = MemAddrKind::Reg;
    break;
  case Bedrock::STOREQ_Immrr:
    Size = 3;
    AddrKind = MemAddrKind::Reg;
    break;
  case Bedrock::STOREB_Immro:
    Size = 0;
    AddrKind = MemAddrKind::RegOffset;
    break;
  case Bedrock::STOREW_Immro:
    Size = 1;
    AddrKind = MemAddrKind::RegOffset;
    break;
  case Bedrock::STOREL_Immro:
    Size = 2;
    AddrKind = MemAddrKind::RegOffset;
    break;
  case Bedrock::STOREQ_Immro:
    Size = 3;
    AddrKind = MemAddrKind::RegOffset;
    break;
  case Bedrock::STOREB_Immabs:
    Size = 0;
    AddrKind = MemAddrKind::Abs;
    break;
  case Bedrock::STOREW_Immabs:
    Size = 1;
    AddrKind = MemAddrKind::Abs;
    break;
  case Bedrock::STOREL_Immabs:
    Size = 2;
    AddrKind = MemAddrKind::Abs;
    break;
  case Bedrock::STOREQ_Immabs:
    Size = 3;
    AddrKind = MemAddrKind::Abs;
    break;
  case Bedrock::STOREB_Immfi:
    Size = 0;
    AddrKind = MemAddrKind::Frame;
    break;
  case Bedrock::STOREW_Immfi:
    Size = 1;
    AddrKind = MemAddrKind::Frame;
    break;
  case Bedrock::STOREL_Immfi:
    Size = 2;
    AddrKind = MemAddrKind::Frame;
    break;
  case Bedrock::STOREQ_Immfi:
    Size = 3;
    AddrKind = MemAddrKind::Frame;
    break;
  default:
    return false;
  }

  return MI.getOperand(0).isImm() && MI.getOperand(0).getImm() == 0;
}

static unsigned getClearMemSize(const MachineInstr &MI, MemAddrKind AddrKind,
                                bool IsPostInc) {
  return 3 + (IsPostInc ? 1 : getMemAddrTailSize(MI, 1, AddrKind));
}

static bool isLayoutBranchOpcode(unsigned Opc) {
  return Opc == Bedrock::BR || Opc == Bedrock::BRCC;
}

static bool isCmpTestJumpCompareOpcode(unsigned Opc) {
  switch (Opc) {
  case Bedrock::CMPLrr:
  case Bedrock::CMPQrr:
  case Bedrock::TESTLrr:
  case Bedrock::TESTQrr:
    return true;
  default:
    return false;
  }
}

static bool getCmpTestJumpForm(const MachineInstr &CmpMI, StringRef &Pattern8,
                               StringRef &Pattern16, StringRef &Base,
                               unsigned &Size) {
  switch (CmpMI.getOpcode()) {
  case Bedrock::CMPLrr:
    Base = "cmpj";
    Size = 2;
    Pattern8 = "111100010zzccccssss000dddd";
    Pattern16 = "111100010zzccccssss001dddd";
    return true;
  case Bedrock::CMPQrr:
    Base = "cmpj";
    Size = 3;
    Pattern8 = "111100010zzccccssss000dddd";
    Pattern16 = "111100010zzccccssss001dddd";
    return true;
  case Bedrock::TESTLrr:
    Base = "testj";
    Size = 2;
    Pattern8 = "111100010zzccccssss010dddd";
    Pattern16 = "111100010zzccccssss011dddd";
    return true;
  case Bedrock::TESTQrr:
    Base = "testj";
    Size = 3;
    Pattern8 = "111100010zzccccssss010dddd";
    Pattern16 = "111100010zzccccssss011dddd";
    return true;
  default:
    return false;
  }
}

static bool shouldPreferCmpTestJumpOverShortSplit(const MachineFunction &MF) {
  return MF.getTarget().getOptLevel() >= CodeGenOptLevel::Default &&
         !MF.getFunction().hasOptSize();
}

static bool isDJCandidate(const MachineInstr &DecMI, const MachineInstr &TestMI,
                          const MachineInstr &BranchMI) {
  if (DecMI.getOpcode() != Bedrock::DECQ3r ||
      TestMI.getOpcode() != Bedrock::TESTQrr ||
      BranchMI.getOpcode() != Bedrock::BRCC || BranchMI.getOperand(1).getImm() != 0x3)
    return false;

  Register CounterReg = DecMI.getOperand(0).getReg();
  if (DecMI.getOperand(1).getReg() != CounterReg)
    return false;
  return TestMI.getOperand(0).getReg() == CounterReg &&
         TestMI.getOperand(1).getReg() == CounterReg;
}

static bool isIJCandidate(const MachineInstr &IncMI, const MachineInstr &CmpMI,
                          const MachineInstr &BranchMI) {
  if (IncMI.getOpcode() != Bedrock::INCQ3r ||
      CmpMI.getOpcode() != Bedrock::CMPQrr ||
      BranchMI.getOpcode() != Bedrock::BRCC)
    return false;
  unsigned Cond = BranchMI.getOperand(1).getImm();
  if (Cond < 0x2 || Cond > 0xf)
    return false;

  Register IndexReg = IncMI.getOperand(0).getReg();
  return IncMI.getOperand(1).getReg() == IndexReg &&
         CmpMI.getOperand(1).getReg() == IndexReg &&
         CmpMI.getOperand(0).getReg() != IndexReg;
}

static bool isRepeatCounterDec(const MachineInstr &MI, Register CounterReg) {
  return MI.getOpcode() == Bedrock::DECQ3r &&
         MI.getNumExplicitOperands() >= 2 && MI.getOperand(0).isReg() &&
         MI.getOperand(1).isReg() && MI.getOperand(0).getReg() == CounterReg &&
         MI.getOperand(1).getReg() == CounterReg;
}

static bool getRepeatCounterDecTestBranch(const MachineBasicBlock &MBB,
                                          const MachineInstr *&DecMI,
                                          const MachineInstr *&TestMI,
                                          const MachineInstr *&BranchMI,
                                          Register &CounterReg) {
  auto BranchI = MBB.getLastNonDebugInstr();
  if (BranchI == MBB.end() || BranchI->getOpcode() != Bedrock::BRCC ||
      BranchI->getOperand(0).getMBB() != &MBB ||
      BranchI->getOperand(1).getImm() != 0x3)
    return false;

  bool Missing = false;
  auto TestI = prevNonDebug(BranchI, MBB, Missing);
  if (Missing || TestI->getOpcode() != Bedrock::TESTQrr ||
      TestI->getNumExplicitOperands() < 2 || !TestI->getOperand(0).isReg() ||
      !TestI->getOperand(1).isReg() ||
      TestI->getOperand(0).getReg() != TestI->getOperand(1).getReg())
    return false;

  CounterReg = TestI->getOperand(0).getReg();
  auto DecI = prevNonDebug(TestI, MBB, Missing);
  if (Missing || !isRepeatCounterDec(*DecI, CounterReg))
    return false;

  DecMI = &*DecI;
  TestMI = &*TestI;
  BranchMI = &*BranchI;
  return true;
}

static const MachineBasicBlock *
getLayoutNextBlock(const MachineBasicBlock &MBB) {
  const MachineFunction *MF = MBB.getParent();
  auto NextI = std::next(MBB.getIterator());
  if (NextI == MF->end())
    return nullptr;
  return &*NextI;
}

static bool getCounterSourceDef(const MachineBasicBlock &MBB,
                                Register CounterReg, Register &SourceReg) {
  const MachineInstr *CounterDef = nullptr;
  for (const MachineInstr &MI : MBB) {
    if (MI.isDebugInstr())
      continue;
    if (MI.isCall() || MI.isTerminator() ||
        MI.getOpcode() == TargetOpcode::INLINEASM ||
        MI.getOpcode() == TargetOpcode::INLINEASM_BR)
      return false;

    bool IsCounterCopy =
        (MI.getOpcode() == Bedrock::EXTZQLrr ||
         MI.getOpcode() == Bedrock::MOVQrr) &&
        MI.getNumExplicitOperands() >= 2 && MI.getOperand(0).isReg() &&
        MI.getOperand(1).isReg() && MI.getOperand(0).getReg() == CounterReg;
    if (IsCounterCopy) {
      if (CounterDef)
        return false;
      CounterDef = &MI;
      SourceReg = MI.getOperand(1).getReg();
      continue;
    }
    if (hasRegOperand(MI, CounterReg))
      return false;
  }
  return CounterDef != nullptr;
}

static bool isCounterSelfTest(const MachineInstr &MI, Register Reg) {
  if (MI.getOpcode() != Bedrock::TESTLrr &&
      MI.getOpcode() != Bedrock::TESTQrr)
    return false;
  return MI.getNumExplicitOperands() >= 2 && MI.getOperand(0).isReg() &&
         MI.getOperand(1).isReg() && MI.getOperand(0).getReg() == Reg &&
         MI.getOperand(1).getReg() == Reg;
}

static bool isPositiveCounterGuard(const MachineBasicBlock &GuardMBB,
                                   const MachineBasicBlock &PreheaderMBB,
                                   const MachineBasicBlock &BodyMBB,
                                   Register SourceReg) {
  if (getLayoutNextBlock(GuardMBB) != &PreheaderMBB)
    return false;

  auto BranchI = GuardMBB.getLastNonDebugInstr();
  if (BranchI == GuardMBB.end() || BranchI->getOpcode() != Bedrock::BRCC ||
      BranchI->getOperand(1).getImm() != 0xe)
    return false;
  const MachineBasicBlock *SkipMBB = BranchI->getOperand(0).getMBB();
  if (SkipMBB == &PreheaderMBB || SkipMBB == &BodyMBB ||
      !GuardMBB.isSuccessor(SkipMBB) || !GuardMBB.isSuccessor(&PreheaderMBB))
    return false;

  bool Missing = false;
  for (auto I = BranchI;;) {
    I = prevNonDebug(I, GuardMBB, Missing);
    if (Missing)
      return false;
    if (isCounterSelfTest(*I, SourceReg))
      return true;
    if (I->isCall() || I->isTerminator() ||
        I->getOpcode() == TargetOpcode::INLINEASM ||
        I->getOpcode() == TargetOpcode::INLINEASM_BR || mayReadFlags(*I) ||
        writesFlagsForPostIncScan(*I) || usesReg(*I, SourceReg) ||
        definesReg(*I, SourceReg))
      return false;
  }
}

static bool isRepgBodyCandidate(const MachineInstr &MI, Register CounterReg,
                                bool AllowCounterDefs) {
  if (MI.isDebugInstr())
    return true;

  if (MI.isMetaInstruction() || MI.isCall() || MI.isTerminator() ||
      MI.isBranch() || MI.isReturn() ||
      MI.getOpcode() == TargetOpcode::INLINEASM ||
      MI.getOpcode() == TargetOpcode::INLINEASM_BR ||
      MI.getOpcode() == Bedrock::ADJSP_DOWN ||
      MI.getOpcode() == Bedrock::ADJSP_UP)
    return false;

  if (MI.mayLoadOrStore() && hasVolatileMemOperand(MI))
    return false;

  return AllowCounterDefs || !definesReg(MI, CounterReg);
}

static bool getSingleBitMaskIndex(const MachineInstr &AndMI, bool IsLong,
                                  unsigned &Bit) {
  if (!AndMI.getOperand(2).isImm())
    return false;

  uint64_t Mask = IsLong ? static_cast<uint32_t>(AndMI.getOperand(2).getImm())
                         : static_cast<uint64_t>(AndMI.getOperand(2).getImm());
  if (Mask == 0 || !isPowerOf2_64(Mask))
    return false;

  Bit = Log2_64(Mask);
  return !IsLong || Bit < 32;
}

static bool getBitTestFold(const MachineBasicBlock &MBB,
                           MachineBasicBlock::const_iterator AndI,
                           const MachineInstr &TestMI,
                           const MachineInstr &BranchMI,
                           const MachineInstr *&CopyMI, Register &Reg,
                           unsigned &Bit) {
  if (BranchMI.getOpcode() != Bedrock::BRCC)
    return false;

  unsigned Cond = BranchMI.getOperand(1).getImm();
  if (Cond != 0x2 && Cond != 0x3)
    return false;

  const MachineInstr &AndMI = *AndI;
  bool IsLong;
  switch (AndMI.getOpcode()) {
  case Bedrock::ANDL3ri:
    IsLong = true;
    break;
  case Bedrock::ANDQ3ri:
    IsLong = false;
    break;
  default:
    return false;
  }

  if (!AndMI.getOperand(0).isReg() || !AndMI.getOperand(1).isReg() ||
      !getSingleBitMaskIndex(AndMI, IsLong, Bit))
    return false;

  Register AndDst = AndMI.getOperand(0).getReg();
  if (AndMI.getOperand(1).getReg() != AndDst ||
      TestMI.getOpcode() != (IsLong ? Bedrock::TESTLrr : Bedrock::TESTQrr) ||
      !TestMI.getOperand(0).isReg() || !TestMI.getOperand(1).isReg() ||
      TestMI.getOperand(0).getReg() != AndDst ||
      TestMI.getOperand(1).getReg() != AndDst)
    return false;

  if (AndI == MBB.begin())
    return false;
  auto CopyI = std::prev(AndI);
  while (CopyI != MBB.begin() && CopyI->isDebugInstr())
    --CopyI;
  if (CopyI->isDebugInstr())
    return false;

  if (CopyI->getOpcode() != Bedrock::MOVQrr &&
      CopyI->getOpcode() != Bedrock::MOVLrr)
    return false;
  if (!CopyI->getOperand(0).isReg() || !CopyI->getOperand(1).isReg() ||
      CopyI->getOperand(0).getReg() != AndDst)
    return false;

  Reg = CopyI->getOperand(1).getReg();
  if (Reg == AndDst)
    return false;

  CopyMI = &*CopyI;
  return true;
}

static bool isZeroMaterialization(const MachineInstr &MI, Register &Reg) {
  switch (MI.getOpcode()) {
  case Bedrock::CLRQr:
    if (!MI.getOperand(0).isReg())
      return false;
    Reg = MI.getOperand(0).getReg();
    return true;
  case Bedrock::CONST32:
  case Bedrock::CONST64:
    if (!MI.getOperand(0).isReg() || !MI.getOperand(1).isImm() ||
        MI.getOperand(1).getImm() != 0)
      return false;
    Reg = MI.getOperand(0).getReg();
    return true;
  default:
    return false;
  }
}

static bool storeAddressUsesReg(const MachineInstr &MI, MemAddrKind AddrKind,
                                Register Reg) {
  switch (AddrKind) {
  case MemAddrKind::Reg:
  case MemAddrKind::RegOffset:
    return MI.getOperand(1).isReg() && MI.getOperand(1).getReg() == Reg;
  case MemAddrKind::RegIndex:
    return (MI.getOperand(1).isReg() && MI.getOperand(1).getReg() == Reg) ||
           (MI.getOperand(2).isReg() && MI.getOperand(2).getReg() == Reg);
  case MemAddrKind::Abs:
  case MemAddrKind::Frame:
    return false;
  }
  llvm_unreachable("unknown Bedrock memory address kind");
}

void BedrockAsmPrinter::collectZeroMemStores(const MachineFunction &MF) {
  ZeroMemStoreClears.clear();
  ZeroMemStores.clear();

  for (const MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(), E = MBB.end(); I != E; ++I) {
      Register ZeroReg;
      if (!isZeroMaterialization(*I, ZeroReg) || !ZeroReg.isPhysical() ||
          !Bedrock::GPR64RegClass.contains(ZeroReg))
        continue;

      SmallVector<const MachineInstr *, 8> Stores;
      auto Commit = [&]() {
        if (Stores.empty())
          return;
        ZeroMemStoreClears.insert(&*I);
        for (const MachineInstr *StoreMI : Stores)
          ZeroMemStores.insert(StoreMI);
      };

      for (auto J = std::next(I); J != E; ++J) {
        if (J->isDebugInstr())
          continue;

        unsigned Size;
        MemAddrKind AddrKind;
        Register SrcReg;
        if (getRegStoreInfo(*J, Size, AddrKind, SrcReg) &&
            SrcReg == ZeroReg) {
          if (storeAddressUsesReg(*J, AddrKind, ZeroReg))
            break;
          Stores.push_back(&*J);
          if (J->getOperand(0).isKill()) {
            Commit();
            break;
          }
          continue;
        }

        if (usesReg(*J, ZeroReg))
          break;

        if (definesReg(*J, ZeroReg)) {
          Commit();
          break;
        }

        if (J->isCall() || J->isTerminator())
          break;
      }
    }
  }
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

void BedrockAsmPrinter::collectConstStores(const MachineFunction &MF) {
  ConstStoreStores.clear();
  ConstStoreConsts.clear();

  for (const MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(), E = MBB.end(); I != E; ++I) {
      if (I->getOpcode() != Bedrock::CONST32 &&
          I->getOpcode() != Bedrock::CONST64)
        continue;
      if (!isSymbolicConstOperand(I->getOperand(1)))
        continue;

      Register Reg = I->getOperand(0).getReg();
      auto StoreI = std::next(I);
      while (StoreI != E && StoreI->isDebugInstr())
        ++StoreI;
      if (StoreI == E)
        continue;

      unsigned Size;
      MemAddrKind AddrKind;
      Register SrcReg;
      if (!getRegStoreInfo(*StoreI, Size, AddrKind, SrcReg) || SrcReg != Reg ||
          !StoreI->getOperand(0).isKill())
        continue;

      ConstStoreStores[&*StoreI] = &*I;
      ConstStoreConsts.insert(&*I);
    }
  }
}

static bool getMemMoveLoadInfo(const MachineInstr &MI, unsigned &Size,
                               MemAddrKind &AddrKind) {
  switch (MI.getOpcode()) {
  case Bedrock::LOADB_Zrr:
  case Bedrock::LOADB_Srr:
    Size = 0;
    AddrKind = MemAddrKind::Reg;
    return true;
  case Bedrock::LOADW_Zrr:
  case Bedrock::LOADW_Srr:
    Size = 1;
    AddrKind = MemAddrKind::Reg;
    return true;
  case Bedrock::LOADL_Zrr:
  case Bedrock::LOADL_Srr:
  case Bedrock::LOADLrr:
    Size = 2;
    AddrKind = MemAddrKind::Reg;
    return true;
  case Bedrock::LOADQrr:
    Size = 3;
    AddrKind = MemAddrKind::Reg;
    return true;
  case Bedrock::LOADB_Zro:
  case Bedrock::LOADB_Sro:
    Size = 0;
    AddrKind = MemAddrKind::RegOffset;
    return true;
  case Bedrock::LOADW_Zro:
  case Bedrock::LOADW_Sro:
    Size = 1;
    AddrKind = MemAddrKind::RegOffset;
    return true;
  case Bedrock::LOADL_Zro:
  case Bedrock::LOADL_Sro:
  case Bedrock::LOADLro:
    Size = 2;
    AddrKind = MemAddrKind::RegOffset;
    return true;
  case Bedrock::LOADQro:
    Size = 3;
    AddrKind = MemAddrKind::RegOffset;
    return true;
  case Bedrock::LOADB_Zabs:
  case Bedrock::LOADB_Sabs:
    Size = 0;
    AddrKind = MemAddrKind::Abs;
    return true;
  case Bedrock::LOADW_Zabs:
  case Bedrock::LOADW_Sabs:
    Size = 1;
    AddrKind = MemAddrKind::Abs;
    return true;
  case Bedrock::LOADL_Zabs:
  case Bedrock::LOADL_Sabs:
  case Bedrock::LOADLabs:
    Size = 2;
    AddrKind = MemAddrKind::Abs;
    return true;
  case Bedrock::LOADQabs:
    Size = 3;
    AddrKind = MemAddrKind::Abs;
    return true;
  default:
    return false;
  }
}

static bool getMemMoveStoreInfo(const MachineInstr &MI, unsigned &Size,
                                MemAddrKind &AddrKind) {
  switch (MI.getOpcode()) {
  case Bedrock::STOREBrr:
    Size = 0;
    AddrKind = MemAddrKind::Reg;
    return true;
  case Bedrock::STOREWrr:
    Size = 1;
    AddrKind = MemAddrKind::Reg;
    return true;
  case Bedrock::STORELrr:
    Size = 2;
    AddrKind = MemAddrKind::Reg;
    return true;
  case Bedrock::STOREQrr:
    Size = 3;
    AddrKind = MemAddrKind::Reg;
    return true;
  case Bedrock::STOREBro:
    Size = 0;
    AddrKind = MemAddrKind::RegOffset;
    return true;
  case Bedrock::STOREWro:
    Size = 1;
    AddrKind = MemAddrKind::RegOffset;
    return true;
  case Bedrock::STORELro:
    Size = 2;
    AddrKind = MemAddrKind::RegOffset;
    return true;
  case Bedrock::STOREQro:
    Size = 3;
    AddrKind = MemAddrKind::RegOffset;
    return true;
  case Bedrock::STOREBabs:
    Size = 0;
    AddrKind = MemAddrKind::Abs;
    return true;
  case Bedrock::STOREWabs:
    Size = 1;
    AddrKind = MemAddrKind::Abs;
    return true;
  case Bedrock::STORELabs:
    Size = 2;
    AddrKind = MemAddrKind::Abs;
    return true;
  case Bedrock::STOREQabs:
    Size = 3;
    AddrKind = MemAddrKind::Abs;
    return true;
  default:
    return false;
  }
}

static unsigned getMemMoveAccessSize(const MachineInstr &MI,
                                     MemAddrKind AddrKind) {
  switch (AddrKind) {
  case MemAddrKind::Reg:
    return 3;
  case MemAddrKind::RegIndex:
    return 5;
  case MemAddrKind::RegOffset:
    return 3 + getMemAddrTailSize(MI, 1, AddrKind);
  case MemAddrKind::Abs:
    return 7;
  case MemAddrKind::Frame:
    return 3 + getMemAddrTailSize(MI, 1, AddrKind);
  }
  llvm_unreachable("unknown Bedrock memory address kind");
}

static bool memMoveAddressUsesReg(const MachineInstr &MI,
                                  MemAddrKind AddrKind, Register Reg) {
  switch (AddrKind) {
  case MemAddrKind::Reg:
  case MemAddrKind::RegOffset:
    return MI.getOperand(1).getReg() == Reg;
  case MemAddrKind::RegIndex:
    return MI.getOperand(1).getReg() == Reg ||
           MI.getOperand(2).getReg() == Reg;
  case MemAddrKind::Abs:
  case MemAddrKind::Frame:
    return false;
  }
  llvm_unreachable("unknown Bedrock memory address kind");
}

static unsigned getMemMoveFoldSize(const MachineInstr &LoadMI,
                                   MemAddrKind SrcKind,
                                   const MachineInstr &StoreMI,
                                   MemAddrKind DstKind) {
  return 4 + getMemAddrTailSize(LoadMI, 1, SrcKind) +
         getMemAddrTailSize(StoreMI, 1, DstKind);
}

static bool getMemMoveFold(const MachineInstr &LoadMI,
                           const MachineInstr &StoreMI, unsigned &Size,
                           MemAddrKind &SrcKind, MemAddrKind &DstKind) {
  if (hasVolatileMemOperand(LoadMI) || hasVolatileMemOperand(StoreMI))
    return false;

  unsigned LoadSize;
  unsigned StoreSize;
  if (!getMemMoveLoadInfo(LoadMI, LoadSize, SrcKind) ||
      !getMemMoveStoreInfo(StoreMI, StoreSize, DstKind) ||
      LoadSize != StoreSize)
    return false;

  Register LoadReg = LoadMI.getOperand(0).getReg();
  if (!StoreMI.getOperand(0).isReg() ||
      StoreMI.getOperand(0).getReg() != LoadReg ||
      !StoreMI.getOperand(0).isKill() ||
      memMoveAddressUsesReg(StoreMI, DstKind, LoadReg))
    return false;

  unsigned LoadStoreSize =
      getMemMoveAccessSize(LoadMI, SrcKind) + getMemMoveAccessSize(StoreMI, DstKind);
  unsigned FoldSize = getMemMoveFoldSize(LoadMI, SrcKind, StoreMI, DstKind);
  if (FoldSize >= LoadStoreSize)
    return false;

  Size = LoadSize;
  return true;
}

void BedrockAsmPrinter::collectCmpTestJumpBranches(const MachineFunction &MF) {
  CmpTestJumpCmps.clear();
  CmpTestJumpBranches.clear();
  CmpTestJumpCompareInstrs.clear();
  CmpTestJump8Branches.clear();
  CmpTestJumpDisplacements.clear();
  BitTestAnds.clear();
  BitTestSuppressedInstrs.clear();
  ZeroCopyClears.clear();
  ZeroMemStoreClears.clear();
  ZeroMemStores.clear();
  ConstStoreStores.clear();
  ConstStoreConsts.clear();
  MemMoveStores.clear();
  MemMoveLoads.clear();
  DJBranches.clear();
  DJTests.clear();
  DJCounterInstrs.clear();
  DJTestInstrs.clear();
  DJ8Branches.clear();
  DJ16Branches.clear();
  DJDisplacements.clear();
  IJBranches.clear();
  IJCmps.clear();
  IJCounterInstrs.clear();
  IJCompareInstrs.clear();

  collectZeroMemStores(MF);
  collectConstStores(MF);

  if (MF.getTarget().getOptLevel() == CodeGenOptLevel::None)
    return;

  for (const MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(), E = MBB.end(); I != E; ++I) {
      Register ZeroReg;
      if (isZeroMaterialization(*I, ZeroReg)) {
        auto CopyI = std::next(I);
        while (CopyI != E) {
          while (CopyI != E && CopyI->isDebugInstr())
            ++CopyI;
          if (CopyI == E || CopyI->getOpcode() != Bedrock::MOVQrr ||
              !CopyI->getOperand(0).isReg() ||
              !CopyI->getOperand(1).isReg() ||
              CopyI->getOperand(1).getReg() != ZeroReg)
            break;

          Register DstReg = CopyI->getOperand(0).getReg();
          if (DstReg != ZeroReg && DstReg.isPhysical())
            ZeroCopyClears.insert(&*CopyI);
          ++CopyI;
        }
      }

      unsigned MemMoveSize;
      MemAddrKind MemMoveSrcKind;
      MemAddrKind MemMoveDstKind;
      auto StoreI = std::next(I);
      while (StoreI != E && StoreI->isDebugInstr())
        ++StoreI;
      if (StoreI != E &&
          getMemMoveFold(*I, *StoreI, MemMoveSize, MemMoveSrcKind,
                         MemMoveDstKind)) {
        MemMoveStores[&*StoreI] = &*I;
        MemMoveLoads.insert(&*I);
      }

      if (!isCmpTestJumpCompareOpcode(I->getOpcode()))
        continue;

      auto BranchI = std::next(I);
      while (BranchI != E && BranchI->isDebugInstr())
        ++BranchI;
      if (BranchI == E || BranchI->getOpcode() != Bedrock::BRCC)
        continue;

      unsigned Cond = BranchI->getOperand(1).getImm();
      if (I != MBB.begin()) {
        auto PrevI = std::prev(I);
        while (PrevI != MBB.begin() && PrevI->isDebugInstr())
          --PrevI;
        if (!PrevI->isDebugInstr() && isDJCandidate(*PrevI, *I, *BranchI)) {
          DJBranches[&*BranchI] = &*PrevI;
          DJTests[&*BranchI] = &*I;
          DJCounterInstrs.insert(&*PrevI);
          DJTestInstrs.insert(&*I);
          continue;
        }
        if (!PrevI->isDebugInstr() && isIJCandidate(*PrevI, *I, *BranchI)) {
          IJBranches[&*BranchI] = &*PrevI;
          IJCmps[&*BranchI] = &*I;
          IJCounterInstrs.insert(&*PrevI);
          IJCompareInstrs.insert(&*I);
          continue;
        }
        const MachineInstr *CopyMI = nullptr;
        Register BitTestReg;
        unsigned Bit;
        if (!PrevI->isDebugInstr() &&
            getBitTestFold(MBB, PrevI, *I, *BranchI, CopyMI, BitTestReg,
                           Bit)) {
          BitTestAnds[&*PrevI] = {BitTestReg, Bit};
          BitTestSuppressedInstrs.insert(CopyMI);
          BitTestSuppressedInstrs.insert(&*I);
          continue;
        }
      }

      if (Cond < 0x2 || Cond > 0xf)
        continue;
      CmpTestJumpCmps[&*BranchI] = &*I;
      CmpTestJumpBranches.insert(&*BranchI);
      CmpTestJumpCompareInstrs.insert(&*I);
    }
  }
}

bool BedrockAsmPrinter::computeRepeatGroupBodyBytes(
    const MachineBasicBlock &MBB, const MachineInstr *StartMI,
    const MachineInstr *EndMI, const MachineInstr *SkipMI, Register CounterReg,
    bool AllowCounterDefs, uint16_t &BodyBytes) const {
  uint64_t Size = 0;
  bool InRange = false;
  bool SawBody = false;
  bool SeenSkippedDec = false;

  for (const MachineInstr &MI : MBB) {
    if (&MI == StartMI)
      InRange = true;
    if (!InRange)
      continue;
    if (&MI == EndMI)
      break;
    if (&MI == SkipMI) {
      SeenSkippedDec = true;
      continue;
    }
    if (MI.isDebugInstr())
      continue;

    if (SeenSkippedDec && (usesReg(MI, CounterReg) || mayReadFlags(MI)))
      return false;
    if (!isRepgBodyCandidate(MI, CounterReg, AllowCounterDefs))
      return false;

    Size += getInstSizeForBranchLayout(MI);
    SawBody = true;
    if (Size > std::numeric_limits<uint16_t>::max())
      return false;
  }

  if (!SawBody || !InRange || Size == 0)
    return false;

  BodyBytes = static_cast<uint16_t>(Size);
  return true;
}

bool BedrockAsmPrinter::tryCollectHeaderRepeatGroup(
    const MachineBasicBlock &HeaderMBB) {
  auto HeaderTestI = firstNonDebug(HeaderMBB);
  if (HeaderTestI == HeaderMBB.end() ||
      HeaderTestI->getOpcode() != Bedrock::TESTQrr ||
      HeaderTestI->getNumExplicitOperands() < 2 ||
      !HeaderTestI->getOperand(0).isReg() ||
      !HeaderTestI->getOperand(1).isReg() ||
      HeaderTestI->getOperand(0).getReg() !=
          HeaderTestI->getOperand(1).getReg())
    return false;

  Register CounterReg = HeaderTestI->getOperand(0).getReg();
  auto HeaderBrI = nextNonDebug(std::next(HeaderTestI), HeaderMBB);
  if (HeaderBrI == HeaderMBB.end() || HeaderBrI->getOpcode() != Bedrock::BRCC ||
      HeaderBrI->getOperand(1).getImm() != 0x2)
    return false;
  if (nextNonDebug(std::next(HeaderBrI), HeaderMBB) != HeaderMBB.end())
    return false;

  const MachineBasicBlock *ExitMBB = HeaderBrI->getOperand(0).getMBB();
  const MachineBasicBlock *BodyMBB = getLayoutNextBlock(HeaderMBB);
  if (!BodyMBB || BodyMBB == ExitMBB || !HeaderMBB.isSuccessor(BodyMBB) ||
      !HeaderMBB.isSuccessor(ExitMBB) ||
      getLayoutNextBlock(*BodyMBB) != ExitMBB)
    return false;
  if (BodyMBB->pred_size() != 1 || *BodyMBB->pred_begin() != &HeaderMBB)
    return false;

  auto BodyBrI = BodyMBB->getLastNonDebugInstr();
  if (BodyBrI == BodyMBB->end() || BodyBrI->getOpcode() != Bedrock::BR ||
      BodyBrI->getOperand(0).getMBB() != &HeaderMBB)
    return false;

  auto BodyStartI = firstNonDebug(*BodyMBB);
  if (BodyStartI == BodyMBB->end() || &*BodyStartI == &*BodyBrI)
    return false;

  const MachineInstr *ScratchMarker = nullptr;
  if (BodyStartI->getOpcode() == Bedrock::REPG_SCRATCH) {
    if (BodyStartI->getNumExplicitOperands() != 1 ||
        !BodyStartI->getOperand(0).isReg() ||
        BodyStartI->getOperand(0).getReg() != CounterReg)
      return false;
    ScratchMarker = &*BodyStartI;
    BodyStartI = nextNonDebug(std::next(BodyStartI), *BodyMBB);
    if (BodyStartI == BodyMBB->end() || &*BodyStartI == &*BodyBrI)
      return false;
  }

  const MachineInstr *DecMI = nullptr;
  for (auto I = BodyStartI; I != BodyBrI; ++I) {
    if (I->isDebugInstr())
      continue;
    if (isRepeatCounterDec(*I, CounterReg)) {
      DecMI = &*I;
      break;
    }
    if (!ScratchMarker && definesReg(*I, CounterReg))
      return false;
  }
  if (!DecMI)
    return false;

  uint16_t BodyBytes = 0;
  if (!computeRepeatGroupBodyBytes(*BodyMBB, &*BodyStartI, &*BodyBrI, DecMI,
                                   CounterReg, ScratchMarker != nullptr,
                                   BodyBytes))
    return false;

  RepgStarts[&*BodyStartI] = {CounterReg, BodyBytes};
  RepgSuppressedInstrs.insert(&*HeaderTestI);
  RepgSuppressedInstrs.insert(&*HeaderBrI);
  RepgSuppressedInstrs.insert(DecMI);
  RepgSuppressedInstrs.insert(&*BodyBrI);
  if (ScratchMarker)
    RepgSuppressedInstrs.insert(ScratchMarker);
  RepgEndMarkers.insert(&*BodyBrI);
  return true;
}

bool BedrockAsmPrinter::tryCollectGuardedSelfRepeatGroup(
    const MachineBasicBlock &BodyMBB) {
  const MachineInstr *DecMI = nullptr;
  const MachineInstr *TestMI = nullptr;
  const MachineInstr *BranchMI = nullptr;
  Register CounterReg;
  if (!getRepeatCounterDecTestBranch(BodyMBB, DecMI, TestMI, BranchMI,
                                     CounterReg))
    return false;

  const MachineBasicBlock *ExitMBB = getLayoutNextBlock(BodyMBB);
  if (!ExitMBB || !BodyMBB.isSuccessor(ExitMBB)) {
    LLVM_DEBUG(dbgs() << "Bedrock REPG: reject self loop missing layout exit "
                      << BodyMBB.getName() << '\n');
    return false;
  }

  MachineBasicBlock *PreheaderMBB = nullptr;
  for (MachineBasicBlock *PredMBB : BodyMBB.predecessors()) {
    if (PredMBB == &BodyMBB)
      continue;
    if (PreheaderMBB) {
      LLVM_DEBUG(dbgs() << "Bedrock REPG: reject self loop multiple "
                           "non-self predecessors "
                        << BodyMBB.getName() << '\n');
      return false;
    }
    PreheaderMBB = PredMBB;
  }
  if (!PreheaderMBB || PreheaderMBB->succ_size() != 1 ||
      !PreheaderMBB->isSuccessor(&BodyMBB)) {
    LLVM_DEBUG(dbgs() << "Bedrock REPG: reject self loop bad preheader "
                      << BodyMBB.getName() << '\n');
    return false;
  }

  Register SourceReg;
  if (!getCounterSourceDef(*PreheaderMBB, CounterReg, SourceReg)) {
    LLVM_DEBUG(dbgs() << "Bedrock REPG: reject self loop bad counter source "
                      << BodyMBB.getName() << '\n');
    return false;
  }

  MachineBasicBlock *GuardMBB = nullptr;
  for (MachineBasicBlock *PredMBB : PreheaderMBB->predecessors()) {
    if (GuardMBB) {
      LLVM_DEBUG(dbgs() << "Bedrock REPG: reject self loop multiple guards "
                        << BodyMBB.getName() << '\n');
      return false;
    }
    GuardMBB = PredMBB;
  }
  if (!GuardMBB ||
      !isPositiveCounterGuard(*GuardMBB, *PreheaderMBB, BodyMBB, SourceReg)) {
    LLVM_DEBUG(dbgs() << "Bedrock REPG: reject self loop bad guard "
                      << BodyMBB.getName() << '\n');
    return false;
  }

  auto BodyStartI = firstNonDebug(BodyMBB);
  if (BodyStartI == BodyMBB.end() || &*BodyStartI == DecMI) {
    LLVM_DEBUG(dbgs() << "Bedrock REPG: reject self loop empty body "
                      << BodyMBB.getName() << '\n');
    return false;
  }

  const MachineInstr *ScratchMarker = nullptr;
  if (BodyStartI->getOpcode() == Bedrock::REPG_SCRATCH) {
    if (BodyStartI->getNumExplicitOperands() != 1 ||
        !BodyStartI->getOperand(0).isReg() ||
        BodyStartI->getOperand(0).getReg() != CounterReg)
      return false;
    ScratchMarker = &*BodyStartI;
    BodyStartI = nextNonDebug(std::next(BodyStartI), BodyMBB);
    if (BodyStartI == BodyMBB.end() || &*BodyStartI == DecMI)
      return false;
  }

  uint16_t BodyBytes = 0;
  if (!computeRepeatGroupBodyBytes(BodyMBB, &*BodyStartI, DecMI, nullptr,
                                   CounterReg, ScratchMarker != nullptr,
                                   BodyBytes)) {
    LLVM_DEBUG(dbgs() << "Bedrock REPG: reject self loop invalid body "
                      << BodyMBB.getName() << '\n');
    return false;
  }

  LLVM_DEBUG(dbgs() << "Bedrock REPG: collect guarded self loop "
                    << BodyMBB.getName() << " body-bytes=" << BodyBytes
                    << '\n');
  RepgStarts[&*BodyStartI] = {CounterReg, BodyBytes};
  RepgSuppressedInstrs.insert(DecMI);
  RepgSuppressedInstrs.insert(TestMI);
  RepgSuppressedInstrs.insert(BranchMI);
  if (ScratchMarker)
    RepgSuppressedInstrs.insert(ScratchMarker);
  RepgEndMarkers.insert(BranchMI);

  DJBranches.erase(BranchMI);
  DJTests.erase(BranchMI);
  DJCounterInstrs.erase(DecMI);
  DJTestInstrs.erase(TestMI);
  DJ8Branches.erase(BranchMI);
  DJ16Branches.erase(BranchMI);
  DJDisplacements.erase(BranchMI);
  return true;
}

void BedrockAsmPrinter::collectRepeatGroups(const MachineFunction &MF) {
  RepgStarts.clear();
  RepgSuppressedInstrs.clear();
  RepgEndMarkers.clear();

  if (MF.getTarget().getOptLevel() == CodeGenOptLevel::None)
    return;

  for (const MachineBasicBlock &MBB : MF)
    tryCollectHeaderRepeatGroup(MBB);

  for (const MachineBasicBlock &MBB : MF)
    tryCollectGuardedSelfRepeatGroup(MBB);
}

unsigned
BedrockAsmPrinter::getInstSizeForBranchLayout(const MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();

  if (Opc == TargetOpcode::INLINEASM || Opc == TargetOpcode::INLINEASM_BR) {
    const TargetInstrInfo *TII = MI.getMF()->getSubtarget().getInstrInfo();
    return TII->getInlineAsmLength(MI.getOperand(0).getSymbolName(), *MAI,
                                   &MI.getMF()->getSubtarget());
  }

  if (MI.isMetaInstruction())
    return 0;

  if (RepgSuppressedInstrs.contains(&MI))
    return 0;
  unsigned RepgHeaderSize = RepgStarts.contains(&MI) ? 5 : 0;
  if (CmpTestJumpCompareInstrs.contains(&MI))
    return RepgHeaderSize;
  if (BitTestSuppressedInstrs.contains(&MI))
    return RepgHeaderSize;
  if (BitTestAnds.contains(&MI))
    return RepgHeaderSize + 4;
  if (ConstStoreConsts.contains(&MI))
    return 0;
  if (const MachineInstr *ConstMI = ConstStoreStores.lookup(&MI)) {
    unsigned Size;
    MemAddrKind AddrKind;
    Register SrcReg;
    (void)ConstMI;
    if (!getRegStoreInfo(MI, Size, AddrKind, SrcReg))
      report_fatal_error("invalid Bedrock constant store fold");
    return RepgHeaderSize + getConstStoreFoldSize(MI, AddrKind);
  }
  if (ZeroMemStoreClears.contains(&MI))
    return RepgHeaderSize;
  if (ZeroCopyClears.contains(&MI))
    return RepgHeaderSize + 1;
  if (ZeroMemStores.contains(&MI)) {
    unsigned Size;
    MemAddrKind AddrKind;
    Register SrcReg;
    if (!getRegStoreInfo(MI, Size, AddrKind, SrcReg))
      report_fatal_error("invalid Bedrock zero memory store fold");
    return RepgHeaderSize +
           getClearMemSize(MI, AddrKind, PostIncMemOps.contains(&MI));
  }
  {
    unsigned Size;
    MemAddrKind AddrKind;
    if (getImmZeroStoreInfo(MI, Size, AddrKind))
      return RepgHeaderSize + getClearMemSize(MI, AddrKind, /*IsPostInc=*/false);
  }
  if (MemMoveLoads.contains(&MI))
    return RepgHeaderSize;
  if (const MachineInstr *LoadMI = MemMoveStores.lookup(&MI)) {
    unsigned Size;
    MemAddrKind SrcKind;
    MemAddrKind DstKind;
    if (!getMemMoveFold(*LoadMI, MI, Size, SrcKind, DstKind))
      report_fatal_error("invalid Bedrock memory move fold");
    return RepgHeaderSize + getMemMoveFoldSize(*LoadMI, SrcKind, MI, DstKind);
  }
  if (CmpTestJumpBranches.contains(&MI))
    return RepgHeaderSize + (CmpTestJump8Branches.contains(&MI) ? 5 : 6);
  if (DJCounterInstrs.contains(&MI) || DJTestInstrs.contains(&MI))
    return RepgHeaderSize;
  if (DJBranches.contains(&MI))
    return RepgHeaderSize +
           (DJ8Branches.contains(&MI) ? 5
                                      : (DJ16Branches.contains(&MI) ? 6 : 8));
  if (IJCounterInstrs.contains(&MI) || IJCompareInstrs.contains(&MI))
    return RepgHeaderSize;
  if (IJBranches.contains(&MI))
    return RepgHeaderSize + 9;
  if (Opc == Bedrock::REP_MEMSETB)
    return 9;

  unsigned AtomicSize;
  StringRef AtomicPattern;
  bool IsCmpXchg;
  if (getAtomicEncodingInfo(Opc, AtomicSize, AtomicPattern, IsCmpXchg))
    return RepgHeaderSize + 5;

  switch (Opc) {
  case Bedrock::ILLEGAL:
  case Bedrock::NOP:
  case Bedrock::RET:
  case Bedrock::LRET:
  case Bedrock::ERET:
  case Bedrock::SYSCALL:
  case Bedrock::SYSRET:
  case Bedrock::BKPT:
  case Bedrock::WAIT:
  case Bedrock::YIELD:
  case Bedrock::RFENCE:
  case Bedrock::WFENCE:
  case Bedrock::AFENCE:
  case Bedrock::PUSHPi:
  case Bedrock::POPPi:
  case Bedrock::FPUSHPi:
  case Bedrock::FPOPPi:
  case Bedrock::PUSHr:
  case Bedrock::POPr:
  case Bedrock::MOVQrs:
  case Bedrock::MOVQsr:
  case Bedrock::CLRQr:
    return RepgHeaderSize + 1;
  case Bedrock::CONST32:
  case Bedrock::CONST64:
    return RepgHeaderSize + getConstSize(MI);
  case Bedrock::TLSDESC_CALL:
    return RepgHeaderSize +
           (MI.getOperand(0).getTargetFlags() == BedrockII::MO_TLSDESC64 ? 25
                                                                        : 21);
  case Bedrock::EXTSQBrr:
  case Bedrock::EXTSQWrr:
  case Bedrock::EXTZQBrr:
  case Bedrock::EXTZQWrr:
    return RepgHeaderSize + 3;
  case Bedrock::INCFL3r:
  case Bedrock::INCFQ3r:
  case Bedrock::DECFL3r:
  case Bedrock::DECFQ3r:
    return RepgHeaderSize + getOptionalRegCopySize(MI, 0, 1) + 3;
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
  case Bedrock::BSWAPL3r:
  case Bedrock::BSWAPQ3r:
    return RepgHeaderSize + getOptionalRegCopySize(MI, 0, 1) + 2;
  case Bedrock::CLZLrr:
  case Bedrock::CLZQrr:
  case Bedrock::CTZLrr:
  case Bedrock::CTZQrr:
  case Bedrock::POPCNTLrr:
  case Bedrock::POPCNTQrr:
  case Bedrock::PARITYLrr:
  case Bedrock::PARITYQrr:
  case Bedrock::CLSLrr:
  case Bedrock::CLSQrr:
  case Bedrock::CTSLrr:
  case Bedrock::CTSQrr:
    return RepgHeaderSize + 4;
  case Bedrock::BTESTLri:
  case Bedrock::BTESTQri:
    return RepgHeaderSize + 4;
  case Bedrock::ADCL3rr:
  case Bedrock::ADCQ3rr:
  case Bedrock::SBBL3rr:
  case Bedrock::SBBQ3rr:
    return RepgHeaderSize + getOptionalRegCopySize(MI, 0, 1) + 3;
  case Bedrock::ADDCL3rr:
  case Bedrock::ADDCQ3rr:
  case Bedrock::SUBCL3rr:
  case Bedrock::SUBCQ3rr:
    return RepgHeaderSize + getOptionalRegCopySize(MI, 0, 1) + 6;
  case Bedrock::MULHUQ3rr:
  case Bedrock::MULHSQ3rr:
  case Bedrock::MULHSUQ3rr:
  case Bedrock::CLMULL3rr:
  case Bedrock::CLMULQ3rr:
  case Bedrock::CLMULHQ3rr:
    return RepgHeaderSize + getOptionalRegCopySize(MI, 0, 1) + 4;
  case Bedrock::DIVMODULrr:
  case Bedrock::DIVMODUQrr:
  case Bedrock::DIVMODSLrr:
  case Bedrock::DIVMODSQrr:
    return RepgHeaderSize + getOptionalRegCopySize(MI, 0, 2) + 5;
  case Bedrock::EXTRACTLrrri:
  case Bedrock::EXTRACTQrrri:
    return RepgHeaderSize + getOptionalRegCopySize(MI, 0, 2) + 4;
  case Bedrock::SMAX_ZERO_L:
  case Bedrock::SMAX_ZERO_Q:
  case Bedrock::SMIN_ZERO_L:
  case Bedrock::SMIN_ZERO_Q:
    return RepgHeaderSize + getOptionalRegCopySize(MI, 0, 1) + 5;
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
  case Bedrock::SHRL3rr:
  case Bedrock::SHRQ3rr:
  case Bedrock::SARL3rr:
  case Bedrock::SARQ3rr:
  case Bedrock::ROLL3rr:
  case Bedrock::ROLQ3rr:
  case Bedrock::RORL3rr:
  case Bedrock::RORQ3rr:
    return RepgHeaderSize + getBinaryRegPseudoSize(MI, 2);
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
  case Bedrock::DIVUL3rr:
  case Bedrock::DIVUQ3rr:
  case Bedrock::DIVSL3rr:
  case Bedrock::DIVSQ3rr:
  case Bedrock::MODUL3rr:
  case Bedrock::MODUQ3rr:
  case Bedrock::MODSL3rr:
  case Bedrock::MODSQ3rr:
    return RepgHeaderSize + getBinaryRegPseudoSize(MI, 4);
  case Bedrock::MINUL3ri:
  case Bedrock::MINUQ3ri:
  case Bedrock::MINSL3ri:
  case Bedrock::MINSQ3ri:
  case Bedrock::MAXUL3ri:
  case Bedrock::MAXUQ3ri:
  case Bedrock::MAXSL3ri:
  case Bedrock::MAXSQ3ri:
  case Bedrock::MULL3ri:
  case Bedrock::MULQ3ri:
  case Bedrock::DIVSL3ri:
  case Bedrock::DIVSQ3ri:
    return RepgHeaderSize + getOptionalRegCopySize(MI, 0, 1) + 4 +
           getSignedAutoSize(MI.getOperand(2).getImm());
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
    return RepgHeaderSize + getBinaryImmPseudoSize(MI);
  case Bedrock::BSETL3ri:
  case Bedrock::BSETQ3ri:
  case Bedrock::BCLRL3ri:
  case Bedrock::BCLRQ3ri:
  case Bedrock::BCHGL3ri:
  case Bedrock::BCHGQ3ri:
    return RepgHeaderSize + getOptionalRegCopySize(MI, 0, 1) + 4;
  case Bedrock::BSET2Q3ri:
    return RepgHeaderSize + getOptionalRegCopySize(MI, 0, 1) + 8;
  case Bedrock::SHLL3ri:
  case Bedrock::SHLQ3ri:
  case Bedrock::ROLB3ri:
  case Bedrock::ROLL3ri:
  case Bedrock::ROLQ3ri:
  case Bedrock::RORL3ri:
  case Bedrock::RORQ3ri:
  case Bedrock::SHRL3ri:
  case Bedrock::SHRQ3ri:
  case Bedrock::SARL3ri:
  case Bedrock::SARQ3ri:
    return RepgHeaderSize + getOptionalRegCopySize(MI, 0, 1) + 4;
#define HANDLE_BINARY_MEM_SIZE(OP, IS_LONG)                                  \
  case Bedrock::OP##rm:                                                      \
    return RepgHeaderSize +                                                  \
           getBinaryMemPseudoSize(MI, IS_LONG, MemAddrKind::Reg) +           \
           (PostIncBinaryMemOps.contains(&MI) ? 1 : 0);                      \
  case Bedrock::OP##rmx:                                                     \
    return RepgHeaderSize +                                                  \
           getBinaryMemPseudoSize(MI, IS_LONG, MemAddrKind::RegIndex);       \
  case Bedrock::OP##rmo:                                                     \
    return RepgHeaderSize +                                                  \
           getBinaryMemPseudoSize(MI, IS_LONG, MemAddrKind::RegOffset);      \
  case Bedrock::OP##rmfi:                                                    \
    return RepgHeaderSize +                                                  \
           getBinaryMemPseudoSize(MI, IS_LONG, MemAddrKind::Frame);
    HANDLE_BINARY_MEM_SIZE(ADDL3, false)
    HANDLE_BINARY_MEM_SIZE(ADDQ3, false)
    HANDLE_BINARY_MEM_SIZE(SUBL3, false)
    HANDLE_BINARY_MEM_SIZE(SUBQ3, false)
    HANDLE_BINARY_MEM_SIZE(ANDL3, false)
    HANDLE_BINARY_MEM_SIZE(ANDQ3, false)
    HANDLE_BINARY_MEM_SIZE(ORL3, false)
    HANDLE_BINARY_MEM_SIZE(ORQ3, false)
    HANDLE_BINARY_MEM_SIZE(XORL3, false)
    HANDLE_BINARY_MEM_SIZE(XORQ3, false)
    HANDLE_BINARY_MEM_SIZE(MULL3, true)
    HANDLE_BINARY_MEM_SIZE(MULQ3, true)
    HANDLE_BINARY_MEM_SIZE(MINUL3, true)
    HANDLE_BINARY_MEM_SIZE(MINUQ3, true)
    HANDLE_BINARY_MEM_SIZE(MINSL3, true)
    HANDLE_BINARY_MEM_SIZE(MINSQ3, true)
    HANDLE_BINARY_MEM_SIZE(MAXUL3, true)
    HANDLE_BINARY_MEM_SIZE(MAXUQ3, true)
    HANDLE_BINARY_MEM_SIZE(MAXSL3, true)
    HANDLE_BINARY_MEM_SIZE(MAXSQ3, true)
    HANDLE_BINARY_MEM_SIZE(DIVUL3, true)
    HANDLE_BINARY_MEM_SIZE(DIVUQ3, true)
    HANDLE_BINARY_MEM_SIZE(DIVSL3, true)
    HANDLE_BINARY_MEM_SIZE(DIVSQ3, true)
    HANDLE_BINARY_MEM_SIZE(MODUL3, true)
    HANDLE_BINARY_MEM_SIZE(MODUQ3, true)
    HANDLE_BINARY_MEM_SIZE(MODSL3, true)
    HANDLE_BINARY_MEM_SIZE(MODSQ3, true)
#undef HANDLE_BINARY_MEM_SIZE
#define HANDLE_BINARY_MEM_DEST_SIZE(OP, IS_LONG)                             \
  case Bedrock::OP##mr:                                                       \
    return RepgHeaderSize +                                                  \
           getBinaryMemDestPseudoSize(MI, IS_LONG, MemAddrKind::Reg);        \
  case Bedrock::OP##mro:                                                      \
    return RepgHeaderSize +                                                  \
           getBinaryMemDestPseudoSize(MI, IS_LONG, MemAddrKind::RegOffset);  \
  case Bedrock::OP##mfi:                                                      \
    return RepgHeaderSize +                                                  \
           getBinaryMemDestPseudoSize(MI, IS_LONG, MemAddrKind::Frame);
    HANDLE_BINARY_MEM_DEST_SIZE(ADDL3, false)
    HANDLE_BINARY_MEM_DEST_SIZE(ADDQ3, false)
    HANDLE_BINARY_MEM_DEST_SIZE(SUBL3, false)
    HANDLE_BINARY_MEM_DEST_SIZE(SUBQ3, false)
    HANDLE_BINARY_MEM_DEST_SIZE(ANDL3, false)
    HANDLE_BINARY_MEM_DEST_SIZE(ANDQ3, false)
    HANDLE_BINARY_MEM_DEST_SIZE(ORL3, false)
    HANDLE_BINARY_MEM_DEST_SIZE(ORQ3, false)
    HANDLE_BINARY_MEM_DEST_SIZE(XORL3, false)
    HANDLE_BINARY_MEM_DEST_SIZE(XORQ3, false)
    HANDLE_BINARY_MEM_DEST_SIZE(MINUL3, true)
    HANDLE_BINARY_MEM_DEST_SIZE(MINUQ3, true)
    HANDLE_BINARY_MEM_DEST_SIZE(MINSL3, true)
    HANDLE_BINARY_MEM_DEST_SIZE(MINSQ3, true)
    HANDLE_BINARY_MEM_DEST_SIZE(MAXUL3, true)
    HANDLE_BINARY_MEM_DEST_SIZE(MAXUQ3, true)
    HANDLE_BINARY_MEM_DEST_SIZE(MAXSL3, true)
    HANDLE_BINARY_MEM_DEST_SIZE(MAXSQ3, true)
#undef HANDLE_BINARY_MEM_DEST_SIZE
  case Bedrock::CMPLri:
  case Bedrock::CMPQri:
    if (MI.getOperand(1).isImm())
      return RepgHeaderSize + 3 + getSignedAutoSize(MI.getOperand(1).getImm());
    return RepgHeaderSize + 7;
#define HANDLE_CMP_MEM_SIZE(OP)                                              \
  case Bedrock::OP##mr:                                                       \
    return RepgHeaderSize +                                                  \
           getCmpMemPseudoSize(MI, MemAddrKind::Reg, /*MemIsSrc=*/true);     \
  case Bedrock::OP##mxr:                                                      \
    return RepgHeaderSize +                                                  \
           getCmpMemPseudoSize(MI, MemAddrKind::RegIndex,                    \
                               /*MemIsSrc=*/true);                           \
  case Bedrock::OP##mor:                                                      \
    return RepgHeaderSize +                                                  \
           getCmpMemPseudoSize(MI, MemAddrKind::RegOffset,                   \
                               /*MemIsSrc=*/true);                           \
  case Bedrock::OP##mabsr:                                                    \
    return RepgHeaderSize +                                                  \
           getCmpMemPseudoSize(MI, MemAddrKind::Abs, /*MemIsSrc=*/true);     \
  case Bedrock::OP##mfir:                                                     \
    return RepgHeaderSize +                                                  \
           getCmpMemPseudoSize(MI, MemAddrKind::Frame, /*MemIsSrc=*/true);   \
  case Bedrock::OP##rm:                                                       \
    return RepgHeaderSize +                                                  \
           getCmpMemPseudoSize(MI, MemAddrKind::Reg, /*MemIsSrc=*/false);    \
  case Bedrock::OP##rmx:                                                      \
    return RepgHeaderSize +                                                  \
           getCmpMemPseudoSize(MI, MemAddrKind::RegIndex,                    \
                               /*MemIsSrc=*/false);                          \
  case Bedrock::OP##rmo:                                                      \
    return RepgHeaderSize +                                                  \
           getCmpMemPseudoSize(MI, MemAddrKind::RegOffset,                   \
                               /*MemIsSrc=*/false);                          \
  case Bedrock::OP##rmabs:                                                    \
    return RepgHeaderSize +                                                  \
           getCmpMemPseudoSize(MI, MemAddrKind::Abs, /*MemIsSrc=*/false);    \
  case Bedrock::OP##rmfi:                                                     \
    return RepgHeaderSize +                                                  \
           getCmpMemPseudoSize(MI, MemAddrKind::Frame, /*MemIsSrc=*/false);
    HANDLE_CMP_MEM_SIZE(CMPL)
    HANDLE_CMP_MEM_SIZE(CMPQ)
#undef HANDLE_CMP_MEM_SIZE
  case Bedrock::CMPLmfmf:
  case Bedrock::CMPQmfmf:
    return RepgHeaderSize + 4 + getFrameTailSize(MI, 0) +
           getFrameTailSize(MI, 2);
  case Bedrock::FMOVDrr:
    return RepgHeaderSize + 3;
  case Bedrock::BEDROCK_FCLR_S:
  case Bedrock::BEDROCK_FCLR_D:
    return RepgHeaderSize + 3;
  case Bedrock::BEDROCK_FMOVCR_D:
    return RepgHeaderSize + 6;
  case Bedrock::FCMPSrr:
  case Bedrock::FCMPDrr:
    return RepgHeaderSize + 3;
  case Bedrock::FTESTSr:
  case Bedrock::FTESTDr:
    return RepgHeaderSize + 4;
  case Bedrock::FP_SELECT_CC_S:
  case Bedrock::FP_SELECT_CC_D:
  case Bedrock::FP_SELECT_CC_SD:
  case Bedrock::FP_SELECT_CC_DS:
    return RepgHeaderSize +
           (MI.getOperand(5).getImm() == 0x11 ? 11 : 7);
  case Bedrock::FP_SELECT_TEST_SS:
  case Bedrock::FP_SELECT_TEST_SD:
  case Bedrock::FP_SELECT_TEST_DS:
  case Bedrock::FP_SELECT_TEST_DD:
    return RepgHeaderSize +
           (MI.getOperand(4).getImm() == 0x11 ? 12 : 8);
  case Bedrock::FADDSrr:
  case Bedrock::FADDDrr:
  case Bedrock::FSUBSrr:
  case Bedrock::FSUBDrr:
  case Bedrock::FMULSrr:
  case Bedrock::FMULDrr:
  case Bedrock::FDIVSrr:
  case Bedrock::FDIVDrr:
  case Bedrock::FMINSrr:
  case Bedrock::FMINDrr:
  case Bedrock::FMAXSrr:
  case Bedrock::FMAXDrr:
    return RepgHeaderSize +
           (MI.getOperand(0).getReg() == MI.getOperand(1).getReg() ? 3 : 6);
  case Bedrock::FMODSrr:
  case Bedrock::FMODDrr:
  case Bedrock::FSCALESrr:
  case Bedrock::FSCALEDrr:
    return RepgHeaderSize +
           (MI.getOperand(0).getReg() == MI.getOperand(1).getReg() ? 4 : 7);
  case Bedrock::FCOPYSIGNSrrr:
  case Bedrock::FCOPYSIGNDrrr:
    return RepgHeaderSize + 4;
  case Bedrock::FCVTSQSrr:
  case Bedrock::FCVTSQDrr:
  case Bedrock::FCVTUQSrr:
  case Bedrock::FCVTUQDrr:
  case Bedrock::FCVTStoQrr:
  case Bedrock::FCVTDtoQrr:
  case Bedrock::FCVTUStoQrr:
  case Bedrock::FCVTUDtoQrr:
  case Bedrock::FCVTDtoSrr:
  case Bedrock::FCVTStoDrr:
    return RepgHeaderSize + 4;
#define FPU_UNARY_SIZE_CASES(NAME)                                          \
  case Bedrock::BEDROCK_##NAME##_S:                                         \
  case Bedrock::BEDROCK_##NAME##_D:
    FPU_UNARY_SIZE_CASES(FABS)
    FPU_UNARY_SIZE_CASES(FNEG)
    FPU_UNARY_SIZE_CASES(FSQRT)
    FPU_UNARY_SIZE_CASES(FROUND)
    FPU_UNARY_SIZE_CASES(FTRUNC)
    FPU_UNARY_SIZE_CASES(FCEIL)
    FPU_UNARY_SIZE_CASES(FFLOOR)
#undef FPU_UNARY_SIZE_CASES
    return RepgHeaderSize + 3;
  case Bedrock::BEDROCK_FINT_S:
  case Bedrock::BEDROCK_FINT_D:
  case Bedrock::BEDROCK_FGETEXP_S:
  case Bedrock::BEDROCK_FGETEXP_D:
  case Bedrock::BEDROCK_FGETMAN_S:
  case Bedrock::BEDROCK_FGETMAN_D:
    return RepgHeaderSize + 4;
#define FUSED_SIZE_CASES(NAME)                                               \
  case Bedrock::BEDROCK_##NAME##_S:                                         \
  case Bedrock::BEDROCK_##NAME##_D:
    FUSED_SIZE_CASES(FMADD)
    FUSED_SIZE_CASES(FMSUB)
    FUSED_SIZE_CASES(FNMADD)
    FUSED_SIZE_CASES(FNMSUB)
#undef FUSED_SIZE_CASES
#define APPROX_SIZE_CASES(NAME)                                              \
  case Bedrock::BEDROCK_##NAME##_S:                                         \
  case Bedrock::BEDROCK_##NAME##_D:
    APPROX_SIZE_CASES(FACOSA)
    APPROX_SIZE_CASES(FASINA)
    APPROX_SIZE_CASES(FATANA)
    APPROX_SIZE_CASES(FATANHA)
    APPROX_SIZE_CASES(FCOSA)
    APPROX_SIZE_CASES(FCOSHA)
    APPROX_SIZE_CASES(FETOXA)
    APPROX_SIZE_CASES(FETOXM1A)
    APPROX_SIZE_CASES(FLOG10A)
    APPROX_SIZE_CASES(FLOG2A)
    APPROX_SIZE_CASES(FLOGNA)
    APPROX_SIZE_CASES(FLOGNP1A)
    APPROX_SIZE_CASES(FSINA)
    APPROX_SIZE_CASES(FSINHA)
    APPROX_SIZE_CASES(FTANA)
    APPROX_SIZE_CASES(FTANHA)
    APPROX_SIZE_CASES(FTENTOXA)
    APPROX_SIZE_CASES(FTWOTOXA)
#undef APPROX_SIZE_CASES
    return RepgHeaderSize + 4;
  case Bedrock::BEDROCK_FSINCOSA_S:
  case Bedrock::BEDROCK_FSINCOSA_D:
    return RepgHeaderSize + 5;
  case Bedrock::LEAfi:
    if (getFrameOffset(&MI, 1) == 0)
      return RepgHeaderSize + 1;
    return RepgHeaderSize + 3 + getFrameTailSize(MI, 1);
  case Bedrock::LEAro:
    if (MI.getOperand(2).isImm())
      return RepgHeaderSize + 3 + getOffsetTailSize(MI.getOperand(2).getImm());
    if (isSymbolicAddressOperand(MI.getOperand(2)))
      return RepgHeaderSize + 7;
    llvm_unreachable("invalid Bedrock register-offset LEA operand");
  case Bedrock::LEArx:
    return RepgHeaderSize + 5;
  case Bedrock::MOVLmmrr:
  case Bedrock::MOVQmmrr:
    return RepgHeaderSize + 4 +
           (PostIncMemMoveSrcOps.contains(&MI) ? 1 : 0) +
           (PostIncMemMoveDstOps.contains(&MI) ? 1 : 0);
  case Bedrock::INCLm:
  case Bedrock::INCQm:
  case Bedrock::DECLm:
  case Bedrock::DECQm:
    return RepgHeaderSize + 3;
  case Bedrock::INCLmo:
  case Bedrock::INCQmo:
  case Bedrock::DECLmo:
  case Bedrock::DECQmo:
    return RepgHeaderSize + 3 +
           getOffsetTailSize(MI.getOperand(1).getImm());
  case Bedrock::INCLfi:
  case Bedrock::INCQfi:
  case Bedrock::DECLfi:
  case Bedrock::DECQfi:
    return RepgHeaderSize + 3 + getFrameTailSize(MI, 0);
  case Bedrock::INCLabs:
  case Bedrock::INCQabs:
  case Bedrock::DECLabs:
  case Bedrock::DECQabs:
    return RepgHeaderSize + 7;
  case Bedrock::LOADB_Zrr:
  case Bedrock::LOADW_Zrr:
  case Bedrock::LOADL_Zrr:
  case Bedrock::LOADB_Srr:
  case Bedrock::LOADW_Srr:
  case Bedrock::LOADL_Srr:
  case Bedrock::LOADLrr:
  case Bedrock::LOADQrr:
  case Bedrock::STOREBrr:
  case Bedrock::STOREWrr:
  case Bedrock::STORELrr:
  case Bedrock::STOREQrr:
    return RepgHeaderSize + (PostIncMemOps.contains(&MI) ? 4 : 3);
  case Bedrock::STOREBrx:
  case Bedrock::STOREWrx:
  case Bedrock::STORELrx:
  case Bedrock::STOREQrx:
  case Bedrock::STOREBspx:
  case Bedrock::STOREWspx:
  case Bedrock::STORELspx:
  case Bedrock::STOREQspx:
    return RepgHeaderSize + 5;
  case Bedrock::LOADB_Zrx:
  case Bedrock::LOADW_Zrx:
  case Bedrock::LOADL_Zrx:
  case Bedrock::LOADB_Srx:
  case Bedrock::LOADW_Srx:
  case Bedrock::LOADL_Srx:
  case Bedrock::LOADLrx:
  case Bedrock::LOADQrx:
  case Bedrock::LOADB_Zspx:
  case Bedrock::LOADW_Zspx:
  case Bedrock::LOADL_Zspx:
  case Bedrock::LOADB_Sspx:
  case Bedrock::LOADW_Sspx:
  case Bedrock::LOADL_Sspx:
  case Bedrock::LOADLspx:
  case Bedrock::LOADQspx:
    return RepgHeaderSize + 5;
  case Bedrock::FLOADSrr:
  case Bedrock::FLOADDrr:
  case Bedrock::FSTORESrr:
  case Bedrock::FSTOREDrr:
    return RepgHeaderSize + 4;
  case Bedrock::LOADB_Zro:
  case Bedrock::LOADW_Zro:
  case Bedrock::LOADL_Zro:
  case Bedrock::LOADB_Sro:
  case Bedrock::LOADW_Sro:
  case Bedrock::LOADL_Sro:
  case Bedrock::LOADLro:
  case Bedrock::LOADQro:
  case Bedrock::STOREBro:
  case Bedrock::STOREWro:
  case Bedrock::STORELro:
  case Bedrock::STOREQro:
    return RepgHeaderSize + 3 +
           getOffsetTailSize(MI.getOperand(2).getImm());
  case Bedrock::FLOADSro:
  case Bedrock::FLOADDro:
  case Bedrock::FSTORESro:
  case Bedrock::FSTOREDro:
    return RepgHeaderSize + 4 +
           getOffsetTailSize(MI.getOperand(2).getImm());
  case Bedrock::LOADB_Zabs:
  case Bedrock::LOADW_Zabs:
  case Bedrock::LOADL_Zabs:
  case Bedrock::LOADB_Sabs:
  case Bedrock::LOADW_Sabs:
  case Bedrock::LOADL_Sabs:
  case Bedrock::LOADLabs:
  case Bedrock::LOADQabs:
  case Bedrock::STOREBabs:
  case Bedrock::STOREWabs:
  case Bedrock::STORELabs:
  case Bedrock::STOREQabs:
    return RepgHeaderSize + 7;
  case Bedrock::FLOADSabs:
  case Bedrock::FLOADDabs:
  case Bedrock::FSTORESabs:
  case Bedrock::FSTOREDabs:
    return RepgHeaderSize + 8;
  case Bedrock::LOADB_Zfi:
  case Bedrock::LOADW_Zfi:
  case Bedrock::LOADL_Zfi:
  case Bedrock::LOADB_Sfi:
  case Bedrock::LOADW_Sfi:
  case Bedrock::LOADL_Sfi:
  case Bedrock::LOADLfi:
  case Bedrock::LOADQfi:
  case Bedrock::STOREBfi:
  case Bedrock::STOREWfi:
  case Bedrock::STORELfi:
  case Bedrock::STOREQfi:
    return RepgHeaderSize + 3 + getFrameTailSize(MI, 1);
  case Bedrock::FLOADSfi:
  case Bedrock::FLOADDfi:
  case Bedrock::FSTORESfi:
  case Bedrock::FSTOREDfi:
    return RepgHeaderSize + 4 + getFrameTailSize(MI, 1);
  case Bedrock::STOREB_Immrr:
  case Bedrock::STOREW_Immrr:
  case Bedrock::STOREL_Immrr:
  case Bedrock::STOREQ_Immrr:
    return RepgHeaderSize + getImmStoreSize(MI, /*IsFrame=*/false);
  case Bedrock::STOREB_Immro:
  case Bedrock::STOREW_Immro:
  case Bedrock::STOREL_Immro:
  case Bedrock::STOREQ_Immro:
    return RepgHeaderSize + getImmStoreOffsetSize(MI);
  case Bedrock::STOREB_Immabs:
  case Bedrock::STOREW_Immabs:
  case Bedrock::STOREL_Immabs:
  case Bedrock::STOREQ_Immabs:
    return RepgHeaderSize + getImmStoreAbsSize(MI);
  case Bedrock::STOREB_Immfi:
  case Bedrock::STOREW_Immfi:
  case Bedrock::STOREL_Immfi:
  case Bedrock::STOREQ_Immfi:
    return RepgHeaderSize + getImmStoreSize(MI, /*IsFrame=*/true);
  case Bedrock::ADJSP_DOWN:
  case Bedrock::ADJSP_UP: {
    uint64_t Amount = MI.getOperand(0).getImm();
    if (Amount == 0)
      return RepgHeaderSize;
    if (Amount == 8)
      return RepgHeaderSize + 1;
    if (Amount <= 0xff)
      return RepgHeaderSize + 2;
    if (isUInt<16>(Amount))
      return RepgHeaderSize + 5;
    return RepgHeaderSize + 7;
  }
  case Bedrock::BR:
  case Bedrock::BRCC:
    if (ShortBranches.contains(&MI))
      return RepgHeaderSize + 2;
    if (MediumBranches.contains(&MI))
      return RepgHeaderSize + 5;
    return RepgHeaderSize + 7;
  case Bedrock::SETCC:
    return RepgHeaderSize + 2;
  case Bedrock::CALL:
  case Bedrock::CALL_TAIL:
    return RepgHeaderSize + 7;
  case Bedrock::TAILCALL:
    return RepgHeaderSize + 7;
  case Bedrock::CALLr:
  case Bedrock::CALLr_TAIL:
  case Bedrock::BRIND:
    return RepgHeaderSize + 4;
  case Bedrock::BEDROCK_CPUID:
    return RepgHeaderSize + 3;
  case Bedrock::BEDROCK_TRACE:
    return RepgHeaderSize + 5;
  case Bedrock::BEDROCK_RDPMC:
    return RepgHeaderSize + 6;
  case Bedrock::BEDROCK_RDSTATUS:
  case Bedrock::BEDROCK_RDFSTATUS:
  case Bedrock::BEDROCK_WRFSTATUS:
  case Bedrock::BEDROCK_RDFFLAGS:
  case Bedrock::BEDROCK_WRFFLAGS:
  case Bedrock::BEDROCK_CLMUL_B:
  case Bedrock::BEDROCK_CLMUL_W:
  case Bedrock::BEDROCK_CLMUL_L:
  case Bedrock::BEDROCK_CLMUL_Q:
  case Bedrock::BEDROCK_FCLASS_S:
  case Bedrock::BEDROCK_FCLASS_D:
    return RepgHeaderSize + 4;
  case Bedrock::BEDROCK_MOVNT_B:
  case Bedrock::BEDROCK_MOVNT_W:
  case Bedrock::BEDROCK_MOVNT_L:
  case Bedrock::BEDROCK_MOVNT_Q:
    return RepgHeaderSize + 5;
  case Bedrock::BEDROCK_RDCR:
  case Bedrock::BEDROCK_WRCR:
  case Bedrock::BEDROCK_INVASID:
    return RepgHeaderSize + 6;
  case Bedrock::BEDROCK_VTOP:
  case Bedrock::BEDROCK_PTQUERY:
    return RepgHeaderSize + 8;
  case Bedrock::BEDROCK_WRSTATUS:
  case Bedrock::BEDROCK_RDSEG:
  case Bedrock::BEDROCK_RDSEG_CS:
  case Bedrock::BEDROCK_WRSEG:
  case Bedrock::BEDROCK_FLSHDCACHE:
  case Bedrock::BEDROCK_INVDCACHE:
  case Bedrock::BEDROCK_INVICACHE:
  case Bedrock::BEDROCK_WRBKDCACHE:
  case Bedrock::BEDROCK_SYNCCACHE:
  case Bedrock::BEDROCK_INVTLB:
  case Bedrock::BEDROCK_INVPAGE:
  case Bedrock::BEDROCK_SWPT:
  case Bedrock::BEDROCK_SWPTA:
  case Bedrock::BEDROCK_SAVE:
  case Bedrock::BEDROCK_RESTORE:
    return RepgHeaderSize + 4;
  default:
    break;
  }

  unsigned Size = MI.getDesc().getSize();
  if (Size == 0 || MI.getDesc().isPseudo())
    report_fatal_error("missing Bedrock pseudo size for branch layout");
  return RepgHeaderSize + Size;
}

static uint64_t getAlignedBlockOffset(uint64_t Offset,
                                      const MachineBasicBlock &MBB) {
  Align Alignment = MBB.getAlignment();
  if (Alignment <= Align(1))
    return Offset;

  Align ParentAlign = MBB.getParent()->getAlignment();
  if (Alignment <= ParentAlign)
    return alignTo(Offset, Alignment);
  return alignTo(Offset, Alignment) + Alignment.value() - ParentAlign.value();
}

void BedrockAsmPrinter::computeBlockOffsets(
    const MachineFunction &MF, SmallVectorImpl<uint64_t> &BlockOffsets) const {
  BlockOffsets.assign(MF.getNumBlockIDs(), 0);
  uint64_t Offset = 0;
  bool IsFirst = true;

  for (const MachineBasicBlock &MBB : MF) {
    if (!IsFirst)
      Offset = getAlignedBlockOffset(Offset, MBB);
    IsFirst = false;

    BlockOffsets[MBB.getNumber()] = Offset;
    for (const MachineInstr &MI : MBB)
      Offset += getInstSizeForBranchLayout(MI);
  }
}

void BedrockAsmPrinter::computeShortBranches(const MachineFunction &MF) {
  ShortBranches.clear();
  MediumBranches.clear();
  ShortBranchDisplacements.clear();
  MediumBranchDisplacements.clear();
  CmpTestJump8Branches.clear();
  CmpTestJumpDisplacements.clear();
  DJ8Branches.clear();
  DJ16Branches.clear();
  DJDisplacements.clear();

  const bool PreferCmpTestJumpOverShortSplit =
      shouldPreferCmpTestJumpOverShortSplit(MF);

  SmallVector<uint64_t, 16> BlockOffsets;
  bool Changed;
  do {
    Changed = false;
    computeBlockOffsets(MF, BlockOffsets);

    for (const MachineBasicBlock &MBB : MF) {
      uint64_t Offset = BlockOffsets[MBB.getNumber()];
      for (const MachineInstr &MI : MBB) {
        if (IJBranches.contains(&MI)) {
          Offset += getInstSizeForBranchLayout(MI);
          continue;
        }
        if (DJBranches.contains(&MI)) {
          const MachineBasicBlock *TargetMBB = MI.getOperand(0).getMBB();
          int64_t Disp =
              static_cast<int64_t>(BlockOffsets[TargetMBB->getNumber()]) -
              static_cast<int64_t>(Offset + 4);
          if (isInt<8>(Disp)) {
            if (!DJ8Branches.contains(&MI)) {
              DJ8Branches.insert(&MI);
              DJ16Branches.erase(&MI);
              Changed = true;
            }
          } else if (DJ8Branches.erase(&MI)) {
            Changed = true;
          }

          if (!isInt<8>(Disp) && isInt<16>(Disp)) {
            if (!DJ16Branches.contains(&MI)) {
              DJ16Branches.insert(&MI);
              Changed = true;
            }
          } else if (DJ16Branches.erase(&MI)) {
            Changed = true;
          }

          if (!isInt<32>(Disp)) {
            if (const MachineInstr *DecMI = DJBranches.lookup(&MI))
              DJCounterInstrs.erase(DecMI);
            if (const MachineInstr *TestMI = DJTests.lookup(&MI))
              DJTestInstrs.erase(TestMI);
            DJBranches.erase(&MI);
            DJTests.erase(&MI);
            DJ8Branches.erase(&MI);
            DJ16Branches.erase(&MI);
            Changed = true;
          }

          Offset += getInstSizeForBranchLayout(MI);
          continue;
        }

        if (CmpTestJumpBranches.contains(&MI)) {
          const MachineBasicBlock *TargetMBB = MI.getOperand(0).getMBB();
          if (!PreferCmpTestJumpOverShortSplit) {
            int64_t SplitShortDisp =
                static_cast<int64_t>(BlockOffsets[TargetMBB->getNumber()]) -
                static_cast<int64_t>(Offset + 4);
            if (BlockOffsets[TargetMBB->getNumber()] > Offset)
              --SplitShortDisp;
            if (isInt<8>(SplitShortDisp)) {
              if (const MachineInstr *CmpMI = CmpTestJumpCmps.lookup(&MI))
                CmpTestJumpCompareInstrs.erase(CmpMI);
              CmpTestJumpCmps.erase(&MI);
              CmpTestJumpBranches.erase(&MI);
              CmpTestJump8Branches.erase(&MI);
              ShortBranches.insert(&MI);
              MediumBranches.erase(&MI);
              Changed = true;
              Offset += 4;
              continue;
            }
          }

          int64_t Disp8 =
              static_cast<int64_t>(BlockOffsets[TargetMBB->getNumber()]) -
              static_cast<int64_t>(Offset + 5);
          if (isInt<8>(Disp8)) {
            if (!CmpTestJump8Branches.contains(&MI)) {
              CmpTestJump8Branches.insert(&MI);
              Changed = true;
            }
          } else if (CmpTestJump8Branches.erase(&MI)) {
            Changed = true;
          }

          int64_t Disp16 =
              static_cast<int64_t>(BlockOffsets[TargetMBB->getNumber()]) -
              static_cast<int64_t>(Offset + 6);
          if (!isInt<16>(Disp16)) {
            if (const MachineInstr *CmpMI = CmpTestJumpCmps.lookup(&MI))
              CmpTestJumpCompareInstrs.erase(CmpMI);
            CmpTestJumpCmps.erase(&MI);
            CmpTestJumpBranches.erase(&MI);
            CmpTestJump8Branches.erase(&MI);
            Changed = true;
          }

          Offset += getInstSizeForBranchLayout(MI);
          continue;
        }

        if (isLayoutBranchOpcode(MI.getOpcode()) &&
            !ShortBranches.contains(&MI)) {
          const MachineBasicBlock *TargetMBB = MI.getOperand(0).getMBB();
          int64_t Disp = static_cast<int64_t>(BlockOffsets[TargetMBB->getNumber()]) -
                         static_cast<int64_t>(Offset + 2);
          if (isInt<8>(Disp)) {
            ShortBranches.insert(&MI);
            MediumBranches.erase(&MI);
            Changed = true;
          } else if (!MediumBranches.contains(&MI)) {
            Disp = static_cast<int64_t>(BlockOffsets[TargetMBB->getNumber()]) -
                   static_cast<int64_t>(Offset + 5);
            if (isInt<16>(Disp)) {
              MediumBranches.insert(&MI);
              Changed = true;
            }
          }
        }
        Offset += getInstSizeForBranchLayout(MI);
      }
    }
  } while (Changed);

  computeBlockOffsets(MF, BlockOffsets);
  for (const MachineBasicBlock &MBB : MF) {
    uint64_t Offset = BlockOffsets[MBB.getNumber()];
    for (const MachineInstr &MI : MBB) {
      if (CmpTestJumpBranches.contains(&MI)) {
        const MachineBasicBlock *TargetMBB = MI.getOperand(0).getMBB();
        unsigned Size = CmpTestJump8Branches.contains(&MI) ? 5 : 6;
        int64_t Disp =
            static_cast<int64_t>(BlockOffsets[TargetMBB->getNumber()]) -
            static_cast<int64_t>(Offset + Size);
        if (CmpTestJump8Branches.contains(&MI)) {
          if (!isInt<8>(Disp))
            report_fatal_error(
                "Bedrock cmpj/testj imm8 displacement out of range");
        } else if (!isInt<16>(Disp)) {
          report_fatal_error(
              "Bedrock cmpj/testj imm16 displacement out of range");
        }
        CmpTestJumpDisplacements[&MI] = Disp;
      } else if (DJBranches.contains(&MI)) {
        const MachineBasicBlock *TargetMBB = MI.getOperand(0).getMBB();
        int64_t Disp =
            static_cast<int64_t>(BlockOffsets[TargetMBB->getNumber()]) -
            static_cast<int64_t>(Offset + 4);
        if (DJ8Branches.contains(&MI)) {
          if (!isInt<8>(Disp))
            report_fatal_error("Bedrock djt disp8 out of range");
        } else if (DJ16Branches.contains(&MI)) {
          if (!isInt<16>(Disp))
            report_fatal_error("Bedrock djt disp16 out of range");
        } else if (!isInt<32>(Disp)) {
          report_fatal_error("Bedrock djt disp32 out of range");
        }
        DJDisplacements[&MI] = Disp;
      } else if (ShortBranches.contains(&MI)) {
        const MachineBasicBlock *TargetMBB = MI.getOperand(0).getMBB();
        int64_t Disp = static_cast<int64_t>(BlockOffsets[TargetMBB->getNumber()]) -
                       static_cast<int64_t>(Offset + 2);
        if (!isInt<8>(Disp))
          report_fatal_error("Bedrock short branch displacement out of range");
        ShortBranchDisplacements[&MI] = Disp;
      } else if (MediumBranches.contains(&MI)) {
        const MachineBasicBlock *TargetMBB = MI.getOperand(0).getMBB();
        int64_t Disp = static_cast<int64_t>(BlockOffsets[TargetMBB->getNumber()]) -
                       static_cast<int64_t>(Offset + 5);
        if (!isInt<16>(Disp))
          report_fatal_error("Bedrock medium branch displacement out of range");
        MediumBranchDisplacements[&MI] = Disp;
      }
      Offset += getInstSizeForBranchLayout(MI);
    }
  }
}

bool BedrockAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  PostIncMemOps.clear();
  PostIncBinaryMemOps.clear();
  PostIncMemMoveSrcOps.clear();
  PostIncMemMoveDstOps.clear();
  bool Changed =
      foldImmediateStores(MF, PostIncMemOps, PostIncBinaryMemOps,
                          PostIncMemMoveSrcOps, PostIncMemMoveDstOps);
  collectCmpTestJumpBranches(MF);
  collectRepeatGroups(MF);
  computeShortBranches(MF);
  return AsmPrinter::runOnMachineFunction(MF) || Changed;
}

const MCExpr *
BedrockAsmPrinter::lowerSymbolOperand(const MachineOperand &MO) const {
  const MCSymbol *Symbol = nullptr;
  int64_t Offset = 0;

  switch (MO.getType()) {
  case MachineOperand::MO_MachineBasicBlock:
    Symbol = MO.getMBB()->getSymbol();
    break;
  case MachineOperand::MO_GlobalAddress:
    Symbol = getSymbol(MO.getGlobal());
    if (const auto *GV = dyn_cast<GlobalVariable>(MO.getGlobal());
        GV && GV->isThreadLocal())
      static_cast<MCSymbolELF *>(const_cast<MCSymbol *>(Symbol))
          ->setType(ELF::STT_TLS);
    Offset = MO.getOffset();
    break;
  case MachineOperand::MO_ExternalSymbol:
    Symbol = GetExternalSymbolSymbol(MO.getSymbolName());
    Offset = MO.getOffset();
    break;
  case MachineOperand::MO_BlockAddress:
    Symbol = GetBlockAddressSymbol(MO.getBlockAddress());
    Offset = MO.getOffset();
    break;
  case MachineOperand::MO_ConstantPoolIndex:
    Symbol = GetCPISymbol(MO.getIndex());
    Offset = MO.getOffset();
    break;
  case MachineOperand::MO_JumpTableIndex:
    Symbol = GetJTISymbol(MO.getIndex());
    break;
  default:
    llvm_unreachable("unknown Bedrock symbol operand");
  }

  const MCExpr *Expr = MCSymbolRefExpr::create(Symbol, OutContext);
  if (Offset != 0)
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(Offset, OutContext), OutContext);
  return Expr;
}

MCOperand BedrockAsmPrinter::lowerOperand(const MachineOperand &MO) const {
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    if (MO.isImplicit())
      return {};
    return MCOperand::createReg(MO.getReg());
  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(MO.getImm());
  case MachineOperand::MO_MachineBasicBlock:
  case MachineOperand::MO_GlobalAddress:
  case MachineOperand::MO_ExternalSymbol:
  case MachineOperand::MO_BlockAddress:
  case MachineOperand::MO_ConstantPoolIndex:
  case MachineOperand::MO_JumpTableIndex:
    return MCOperand::createExpr(lowerSymbolOperand(MO));
  case MachineOperand::MO_RegisterMask:
    return {};
  default:
    report_fatal_error("unknown Bedrock machine operand");
  }
}

void BedrockAsmPrinter::emitMCInst(const MCInst &Inst) {
  EmitToStreamer(*OutStreamer, Inst);
}

void BedrockAsmPrinter::emitRaw(ArrayRef<uint8_t> Bytes) {
  MCInst Inst;
  BedrockMC::createRawInst(Bytes, Inst);
  emitMCInst(Inst);
}

void BedrockAsmPrinter::emitRawExpr(ArrayRef<uint8_t> Bytes,
                                    ArrayRef<RawExprFixup> Fixups) {
  MCInst Inst;
  Inst.setOpcode(Bedrock::RAW_EXPR);
  Inst.addOperand(MCOperand::createImm(Fixups.size()));
  for (const RawExprFixup &Fixup : Fixups) {
    Inst.addOperand(MCOperand::createImm(Fixup.Offset));
    Inst.addOperand(MCOperand::createImm(Fixup.Kind));
    Inst.addOperand(MCOperand::createExpr(Fixup.Expr));
  }
  for (uint8_t Byte : Bytes)
    Inst.addOperand(MCOperand::createImm(Byte));
  emitMCInst(Inst);
}

void BedrockAsmPrinter::emitRawExpr(ArrayRef<uint8_t> Bytes,
                                    unsigned FixupOffset, MCFixupKind Kind,
                                    const MCExpr *Expr) {
  emitRawExpr(Bytes, ArrayRef<RawExprFixup>{{FixupOffset, Kind, Expr}});
}

void BedrockAsmPrinter::emitRepgHeader(Register CounterReg, uint16_t BodyBytes) {
  if (OutStreamer->hasRawTextSupport()) {
    SmallString<64> Text;
    raw_svector_ostream OS(Text);
    OS << "\trepg\t" << BedrockInstPrinter::getRegisterName(CounterReg)
       << ", {";
    OutStreamer->emitRawText(OS.str());
    return;
  }

  SmallVector<uint8_t, 2> Tail;
  appendLE(Tail, BodyBytes, 2);
  SmallVector<uint8_t, 8> Bytes;
  if (!BedrockMC::encodeMedium(0x2680 | getGPRNo(CounterReg), Tail, Bytes))
    report_fatal_error("failed to encode Bedrock grouped repeat");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitRepMemset(const MachineInstr *MI) {
  Register DstReg = MI->getOperand(2).getReg();
  Register ValueReg = MI->getOperand(3).getReg();
  Register CountReg = MI->getOperand(4).getReg();

  emitRepgHeader(CountReg, /*BodyBytes=*/4);
  if (OutStreamer->hasRawTextSupport()) {
    SmallString<80> Text;
    raw_svector_ostream OS(Text);
    OS << "\tmov.b\t" << BedrockInstPrinter::getRegisterName(ValueReg)
       << ", [" << BedrockInstPrinter::getRegisterName(DstReg) << "++]";
    OutStreamer->emitRawText(OS.str());
    OutStreamer->emitRawText("\t}");
    return;
  }

  uint8_t EA;
  SmallVector<uint8_t, 4> Tail;
  getMemEAForRegPostInc(DstReg, EA, Tail);
  SmallVector<uint8_t, 8> Bytes;
  if (!BedrockMC::encodeMedium(
          getMovPayload(/*IsLoad=*/false, /*Size=*/0, EA, ValueReg), Tail,
          Bytes))
    report_fatal_error("failed to encode Bedrock repeated memset");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitRR(unsigned Opcode, Register DstReg,
                               Register SrcReg) {
  MCInst Inst;
  Inst.setOpcode(Opcode);
  Inst.addOperand(MCOperand::createReg(DstReg));
  Inst.addOperand(MCOperand::createReg(SrcReg));
  emitMCInst(Inst);
}

void BedrockAsmPrinter::emitConst(const MachineInstr *MI, bool Is64) {
  Register DstReg = MI->getOperand(0).getReg();
  const MachineOperand &ImmOp = MI->getOperand(1);
  unsigned Size = Is64 ? 3 : 2;

  if (ImmOp.isImm()) {
    int64_t Imm = ImmOp.getImm();
    if (Imm == 0) {
      MCInst Inst;
      Inst.setOpcode(Bedrock::CLRQr);
      Inst.addOperand(MCOperand::createReg(DstReg));
      emitMCInst(Inst);
      return;
    }
    if (Imm == 1) {
      MCInst Inst;
      Inst.setOpcode(Bedrock::SETr);
      Inst.addOperand(MCOperand::createReg(DstReg));
      emitMCInst(Inst);
      return;
    }
    if (Imm == -1) {
      MCInst Clear;
      Clear.setOpcode(Bedrock::CLRQr);
      Clear.addOperand(MCOperand::createReg(DstReg));
      emitMCInst(Clear);

      MCInst IncDec;
      IncDec.setOpcode(Is64 ? Bedrock::DECQr : Bedrock::DECLr);
      IncDec.addOperand(MCOperand::createReg(DstReg));
      emitMCInst(IncDec);
      return;
    }

    SmallVector<uint8_t, 8> Tail;
    SmallVector<uint8_t, 16> Bytes;
    unsigned WidthCode;
    appendSignedAuto(Imm, Tail, WidthCode);
    uint8_t EA = 0x6c + WidthCode;
    if (!BedrockMC::encodeMedium(getLeaPayload(EA, Size, DstReg), Tail, Bytes))
      report_fatal_error("failed to encode Bedrock constant");
    emitRaw(Bytes);
    return;
  }

  const MCExpr *Expr = lowerSymbolOperand(ImmOp);
  unsigned Flag = ImmOp.getTargetFlags();
  bool Is64BitField = Flag == BedrockII::MO_ABS64 ||
                      Flag == BedrockII::MO_PCREL64 ||
                      Flag == BedrockII::MO_GOTPCREL64 ||
                      Flag == BedrockII::MO_PLT64;
  bool IsPCRelative = Flag == BedrockII::MO_PCREL32 ||
                      Flag == BedrockII::MO_PCREL64 ||
                      Flag == BedrockII::MO_GOTPCREL32 ||
                      Flag == BedrockII::MO_GOTPCREL64 ||
                      Flag == BedrockII::MO_PLT32;
  bool IsGOT = Flag == BedrockII::MO_GOTPCREL32 ||
               Flag == BedrockII::MO_GOTPCREL64;
  bool IsTLSLE = Flag == BedrockII::MO_TLS_LE32 ||
                 Flag == BedrockII::MO_TLS_LE64;
  if (OutStreamer->hasRawTextSupport()) {
    SmallString<80> Text;
    raw_svector_ostream OS(Text);
    OS << "\tlea." << (Is64 ? 'q' : 'l') << "\t";
    if (IsTLSLE)
      OS << "[gs0:0 + ";
    else if (IsPCRelative)
      OS << "[pc + ";
    MAI->printExpr(OS, *Expr);
    if (IsTLSLE || IsPCRelative)
      OS << "]";
    OS << ", " << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    if (IsGOT) {
      SmallString<48> LoadText;
      raw_svector_ostream LoadOS(LoadText);
      LoadOS << "\tmov.q\t["
             << BedrockInstPrinter::getRegisterName(DstReg) << "], "
             << BedrockInstPrinter::getRegisterName(DstReg);
      OutStreamer->emitRawText(LoadOS.str());
    }
    return;
  }

  unsigned FieldBytes = Is64BitField || Flag == BedrockII::MO_TLS_LE64 ? 8 : 4;
  SmallVector<uint8_t, 8> Tail;
  uint8_t EA;
  if (IsTLSLE) {
    // Extended zero-base EA qualified by GS0, followed by the TLS offset.
    EA = FieldBytes == 8 ? 0x73 : 0x72;
    Tail.push_back(0xa3);
  } else {
    EA = IsPCRelative ? (FieldBytes == 8 ? 0x67 : 0x66)
                      : (FieldBytes == 8 ? 0x6f : 0x6e);
  }
  unsigned FixupOffset = 3 + Tail.size();
  Tail.append(FieldBytes, 0);
  SmallVector<uint8_t, 16> Bytes;
  if (!BedrockMC::encodeMedium(getLeaPayload(EA, Size, DstReg), Tail, Bytes))
    report_fatal_error("failed to encode Bedrock symbolic constant");

  MCFixupKind Kind;
  switch (Flag) {
  case BedrockII::MO_ABS64:
    Kind = FK_Data_8;
    break;
  case BedrockII::MO_PCREL32:
    Kind = MCFixupKind(Bedrock::fixup_bedrock_pcrel32);
    break;
  case BedrockII::MO_PCREL64:
    Kind = MCFixupKind(Bedrock::fixup_bedrock_pcrel64);
    break;
  case BedrockII::MO_GOTPCREL32:
    Kind = MCFixupKind(Bedrock::fixup_bedrock_gotpcrel32);
    break;
  case BedrockII::MO_GOTPCREL64:
    Kind = MCFixupKind(Bedrock::fixup_bedrock_gotpcrel64);
    break;
  case BedrockII::MO_PLT32:
    Kind = MCFixupKind(Bedrock::fixup_bedrock_plt32);
    break;
  case BedrockII::MO_PLT64:
    Kind = MCFixupKind(Bedrock::fixup_bedrock_plt64);
    break;
  case BedrockII::MO_TLS_LE32:
    Kind = MCFixupKind(Bedrock::fixup_bedrock_tls_offset32);
    break;
  case BedrockII::MO_TLS_LE64:
    Kind = MCFixupKind(Bedrock::fixup_bedrock_tls_offset64);
    break;
  default:
    Kind = MCFixupKind(Bedrock::fixup_bedrock_imm32);
    break;
  }
  emitRawExpr(Bytes, FixupOffset, Kind, Expr);

  if (IsGOT) {
    SmallVector<uint8_t, 8> LoadBytes;
    if (!BedrockMC::encodeMedium(
            getMovPayload(/*IsLoad=*/true, /*Size=*/3,
                          0x10 + getGPRNo(DstReg),
                          DstReg),
            {}, LoadBytes))
      report_fatal_error("failed to encode Bedrock GOT load");
    emitRaw(LoadBytes);
  }
}

void BedrockAsmPrinter::emitUnaryPseudo(const MachineInstr *MI,
                                        unsigned RealOpcode,
                                        unsigned CopyOpcode) {
  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();

  if (DstReg != SrcReg)
    emitRR(CopyOpcode, DstReg, SrcReg);

  MCInst Inst;
  Inst.setOpcode(RealOpcode);
  Inst.addOperand(MCOperand::createReg(DstReg));
  emitMCInst(Inst);
}

void BedrockAsmPrinter::emitLongCountPseudo(const MachineInstr *MI,
                                            StringRef Mnemonic,
                                            StringRef Pattern, unsigned Size) {
  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<64> Text;
    raw_svector_ostream OS(Text);
    OS << "\t" << Mnemonic << "." << getSizeSuffix(Size) << "\t"
       << BedrockInstPrinter::getRegisterName(SrcReg) << ", "
       << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint32_t Payload = applyPatternValues(
      Pattern, {{'z', Size}, {'d', getGPRNo(DstReg)}, {'e', getRegEA(SrcReg)}});
  SmallVector<uint8_t, 4> Bytes;
  if (!BedrockMC::encodeLong(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock bit count pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitLongMulHighPseudo(const MachineInstr *MI,
                                              StringRef Mnemonic,
                                              StringRef Pattern) {
  Register DstReg = MI->getOperand(0).getReg();
  Register LHSReg = MI->getOperand(1).getReg();
  Register RHSReg = MI->getOperand(2).getReg();

  if (DstReg != LHSReg)
    emitRR(Bedrock::MOVQrr, DstReg, LHSReg);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<64> Text;
    raw_svector_ostream OS(Text);
    OS << "\t" << Mnemonic << ".q\t"
       << BedrockInstPrinter::getRegisterName(RHSReg) << ", "
       << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint32_t Payload = applyPatternValues(
      Pattern, {{'s', getGPRNo(RHSReg)}, {'d', getGPRNo(DstReg)}});
  SmallVector<uint8_t, 4> Bytes;
  if (!BedrockMC::encodeLong(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock multiply-high pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitDivModPseudo(const MachineInstr *MI, bool IsSigned,
                                         unsigned Size) {
  Register QuotientReg = MI->getOperand(0).getReg();
  Register RemainderReg = MI->getOperand(1).getReg();
  Register DividendReg = MI->getOperand(2).getReg();
  Register DivisorReg = MI->getOperand(3).getReg();

  if (QuotientReg != DividendReg)
    emitRR(Size == 2 ? Bedrock::MOVLrr : Bedrock::MOVQrr, QuotientReg,
           DividendReg);

  StringRef Mnemonic = IsSigned ? "divmods" : "divmodu";
  if (OutStreamer->hasRawTextSupport()) {
    SmallString<80> Text;
    raw_svector_ostream OS(Text);
    OS << "\t" << Mnemonic << "." << getSizeSuffix(Size) << "\t"
       << BedrockInstPrinter::getRegisterName(DivisorReg) << ", "
       << BedrockInstPrinter::getRegisterName(QuotientReg) << ", "
       << BedrockInstPrinter::getRegisterName(RemainderReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  StringRef Pattern = IsSigned ? "111111000010zz00qqqq001rrrreeeeeee"
                               : "111111000010zz00qqqq000rrrreeeeeee";
  uint64_t Payload =
      applyPatternValues64(Pattern, {{'z', Size},
                                     {'q', getGPRNo(QuotientReg)},
                                     {'r', getGPRNo(RemainderReg)},
                                     {'e', getRegEA(DivisorReg)}});
  SmallVector<uint8_t, 5> Bytes;
  if (!BedrockMC::encodeExtraLong(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock divide-remainder pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitExtractPseudo(const MachineInstr *MI,
                                          unsigned Size) {
  Register DstReg = MI->getOperand(0).getReg();
  Register HighReg = MI->getOperand(1).getReg();
  Register LowReg = MI->getOperand(2).getReg();
  uint64_t Offset = MI->getOperand(3).getImm();

  if (DstReg != LowReg)
    emitRR(Size == 2 ? Bedrock::MOVLrr : Bedrock::MOVQrr, DstReg, LowReg);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<80> Text;
    raw_svector_ostream OS(Text);
    OS << "\textract." << getSizeSuffix(Size) << "\t" << Offset << ", "
       << BedrockInstPrinter::getRegisterName(HighReg) << ", "
       << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint32_t Payload = applyPatternValues("111100101zzhhhhlllliiiiiii",
                                        {{'z', Size},
                                         {'h', getGPRNo(HighReg)},
                                         {'l', getGPRNo(DstReg)},
                                         {'i', static_cast<unsigned>(Offset)}});
  SmallVector<uint8_t, 4> Bytes;
  if (!BedrockMC::encodeLong(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock extract pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitClearCarry() {
  if (OutStreamer->hasRawTextSupport()) {
    OutStreamer->emitRawText("\tclc");
    return;
  }

  uint32_t Payload = applyPatternValues("00001001011000mmmm", {{'m', 2}});
  SmallVector<uint8_t, 3> Bytes;
  if (!BedrockMC::encodeMedium(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock clear-carry instruction");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitCarryInstruction(Register DstReg, Register RHSReg,
                                             StringRef Mnemonic,
                                             StringRef Pattern, unsigned Size) {
  if (OutStreamer->hasRawTextSupport()) {
    SmallString<64> Text;
    raw_svector_ostream OS(Text);
    OS << "\t" << Mnemonic << "." << getSizeSuffix(Size) << "\t"
       << BedrockInstPrinter::getRegisterName(RHSReg) << ", "
       << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint32_t Payload = applyPatternValues(
      Pattern, {{'z', Size}, {'s', getGPRNo(RHSReg)}, {'d', getGPRNo(DstReg)}});
  SmallVector<uint8_t, 3> Bytes;
  if (!BedrockMC::encodeMedium(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock carry pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitCarryPseudo(const MachineInstr *MI,
                                        StringRef Mnemonic, StringRef Pattern,
                                        unsigned Size) {
  Register DstReg = MI->getOperand(0).getReg();
  Register LHSReg = MI->getOperand(1).getReg();
  Register RHSReg = MI->getOperand(2).getReg();

  if (DstReg != LHSReg)
    emitRR(Size == 2 ? Bedrock::MOVLrr : Bedrock::MOVQrr, DstReg, LHSReg);

  emitCarryInstruction(DstReg, RHSReg, Mnemonic, Pattern, Size);
}

void BedrockAsmPrinter::emitCarryStartPseudo(const MachineInstr *MI, bool IsAdd,
                                             unsigned Size) {
  Register DstReg = MI->getOperand(0).getReg();
  Register LHSReg = MI->getOperand(1).getReg();
  Register RHSReg = MI->getOperand(2).getReg();
  unsigned MoveOpcode = Size == 2 ? Bedrock::MOVLrr : Bedrock::MOVQrr;

  if (DstReg != LHSReg)
    emitRR(MoveOpcode, DstReg, LHSReg);
  emitClearCarry();
  emitCarryInstruction(DstReg, RHSReg, IsAdd ? "adc" : "sbb",
                       IsAdd ? "1100zz1ssss000dddd" : "1101zz1ssss000dddd",
                       Size);
}

void BedrockAsmPrinter::emitFlagUnaryPseudo(const MachineInstr *MI,
                                            StringRef Mnemonic,
                                            StringRef Pattern, unsigned Size) {
  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();
  if (DstReg != SrcReg)
    emitRR(Size == 2 ? Bedrock::MOVLrr : Bedrock::MOVQrr, DstReg, SrcReg);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<48> Text;
    raw_svector_ostream OS(Text);
    OS << "\t" << Mnemonic << "." << getSizeSuffix(Size) << "\t"
       << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint32_t Payload = applyPatternValues(
      Pattern, {{'z', Size}, {'r', getGPRNo(DstReg)}});
  SmallVector<uint8_t, 3> Bytes;
  if (!BedrockMC::encodeMedium(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock flag-unary pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitExtQRegPseudo(const MachineInstr *MI,
                                          StringRef Pattern) {
  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();

  SmallVector<uint8_t, 8> Bytes;
  uint32_t Payload = applyPattern(Pattern, getRegEA(DstReg), 0,
                                  getGPRNo(SrcReg), 's');
  if (!BedrockMC::encodeMedium(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock quad extend pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitLongZeroMinMaxPseudo(const MachineInstr *MI,
                                                 StringRef Pattern,
                                                 unsigned Size) {
  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();

  if (DstReg != SrcReg)
    emitRR(Size == 2 ? Bedrock::MOVLrr : Bedrock::MOVQrr, DstReg, SrcReg);

  SmallVector<uint8_t, 8> Bytes;
  uint32_t Payload = applyPattern(Pattern, 0x6c, Size, getGPRNo(DstReg), 'd');
  if (!BedrockMC::encodeLong(Payload, {0}, Bytes))
    report_fatal_error("failed to encode Bedrock zero min/max pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitBinaryPseudo(const MachineInstr *MI,
                                         unsigned RealOpcode) {
  Register DstReg = MI->getOperand(0).getReg();
  Register LHSReg = MI->getOperand(1).getReg();
  Register RHSReg = MI->getOperand(2).getReg();

  if (DstReg != LHSReg)
    emitRR(RealOpcode == Bedrock::ADDLrr || RealOpcode == Bedrock::SUBLrr ||
                   RealOpcode == Bedrock::ANDLrr ||
                   RealOpcode == Bedrock::ORLrr || RealOpcode == Bedrock::XORLrr
               ? Bedrock::MOVLrr
               : Bedrock::MOVQrr,
           DstReg, LHSReg);
  emitRR(RealOpcode, DstReg, RHSReg);
}

void BedrockAsmPrinter::emitBinaryImmPseudo(const MachineInstr *MI,
                                            StringRef Pattern, unsigned Size) {
  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();
  int64_t Imm = MI->getOperand(2).getImm();

  if (DstReg != SrcReg)
    emitRR(Size == 2 ? Bedrock::MOVLrr : Bedrock::MOVQrr, DstReg, SrcReg);

  SmallVector<uint8_t, 8> Tail;
  unsigned WidthCode;
  appendSignedAuto(Imm, Tail, WidthCode);

  SmallVector<uint8_t, 16> Bytes;
  uint32_t Payload =
      getBinaryImmPayload(Pattern, Size, 0x6c + WidthCode, DstReg);
  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock binary immediate pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitLongBinaryImmPseudo(const MachineInstr *MI,
                                                StringRef Pattern,
                                                unsigned Size) {
  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();
  int64_t Imm = MI->getOperand(2).getImm();

  if (DstReg != SrcReg)
    emitRR(Size == 2 ? Bedrock::MOVLrr : Bedrock::MOVQrr, DstReg, SrcReg);

  SmallVector<uint8_t, 8> Tail;
  unsigned WidthCode;
  appendSignedAuto(Imm, Tail, WidthCode);

  SmallVector<uint8_t, 16> Bytes;
  uint32_t Payload =
      applyPattern(Pattern, 0x6c + WidthCode, Size, getGPRNo(DstReg), 'd');
  if (!BedrockMC::encodeLong(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock long binary immediate pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitShiftImmPseudo(const MachineInstr *MI,
                                           StringRef Pattern, unsigned Size) {
  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();
  int64_t Imm = MI->getOperand(2).getImm();

  if (!isUInt<6>(Imm))
    report_fatal_error("Bedrock shift immediate does not fit imm6");

  if (DstReg != SrcReg)
    emitRR(Size == 2 ? Bedrock::MOVLrr : Bedrock::MOVQrr, DstReg, SrcReg);

  SmallVector<uint8_t, 8> Bytes;
  uint32_t Payload =
      applyPatternValues(Pattern, {{'z', Size},
                                   {'i', static_cast<unsigned>(Imm)},
                                   {'e', getRegEA(DstReg)}});
  if (!BedrockMC::encodeLong(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock shift immediate pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitBitImm(Register Reg, int64_t Imm,
                                   StringRef Mnemonic, StringRef Pattern) {
  if (!isUInt<6>(Imm))
    report_fatal_error("Bedrock " + Mnemonic + " immediate does not fit imm6");

  SmallVector<uint8_t, 8> Bytes;
  uint32_t Payload = applyPatternValues(
      Pattern, {{'i', static_cast<unsigned>(Imm)}, {'e', getRegEA(Reg)}});
  if (!BedrockMC::encodeLong(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock " + Mnemonic +
                       " immediate pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitBTestImm(Register Reg, unsigned Bit) {
  if (!isUInt<6>(Bit))
    report_fatal_error("Bedrock btest immediate does not fit imm6");

  SmallVector<uint8_t, 8> Bytes;
  uint32_t Payload =
      applyPatternValues("1111101110001iiiiiieeeeeee",
                         {{'i', Bit}, {'e', getRegEA(Reg)}});
  if (!BedrockMC::encodeLong(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock btest immediate pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitBitImmPseudo(const MachineInstr *MI,
                                         StringRef Mnemonic, StringRef Pattern,
                                         unsigned Size) {
  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();

  if (DstReg != SrcReg)
    emitRR(Size == 2 ? Bedrock::MOVLrr : Bedrock::MOVQrr, DstReg, SrcReg);

  emitBitImm(DstReg, MI->getOperand(2).getImm(), Mnemonic, Pattern);
}

void BedrockAsmPrinter::emitBSet2ImmPseudo(const MachineInstr *MI) {
  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();

  if (DstReg != SrcReg)
    emitRR(Bedrock::MOVQrr, DstReg, SrcReg);

  emitBitImm(DstReg, MI->getOperand(2).getImm(), "bset",
             "1111101110011iiiiiieeeeeee");
  emitBitImm(DstReg, MI->getOperand(3).getImm(), "bset",
             "1111101110011iiiiiieeeeeee");
}

void BedrockAsmPrinter::emitBinaryMemPseudo(const MachineInstr *MI,
                                            StringRef Pattern, unsigned Size,
                                            bool IsLong,
                                            MemAddrKind AddrKind) {
  Register DstReg = MI->getOperand(0).getReg();
  Register LHSReg = MI->getOperand(1).getReg();

  if (DstReg != LHSReg)
    emitRR(Size == 2 ? Bedrock::MOVLrr : Bedrock::MOVQrr, DstReg, LHSReg);

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  switch (AddrKind) {
  case MemAddrKind::Reg:
    if (PostIncBinaryMemOps.contains(MI))
      getMemEAForRegPostInc(MI->getOperand(2).getReg(), EA, Tail);
    else
      getMemEAForReg(MI->getOperand(2).getReg(), EA, Tail);
    break;
  case MemAddrKind::RegIndex:
    getMemEAForRegIndex(MI->getOperand(2).getReg(),
                        MI->getOperand(3).getReg(), EA, Tail);
    break;
  case MemAddrKind::RegOffset:
    getMemEAForRegOffset(MI->getOperand(2).getReg(), MI->getOperand(3).getImm(),
                         EA, Tail);
    break;
  case MemAddrKind::Abs:
    llvm_unreachable("absolute binary memory pseudo is not supported");
  case MemAddrKind::Frame:
    getMemEAForSP(getFrameOffset(MI, 2), EA, Tail);
    break;
  }

  SmallVector<uint8_t, 16> Bytes;
  uint32_t Payload = IsLong ? applyPattern(Pattern, EA, Size, getGPRNo(DstReg),
                                           'd')
                            : getBinaryImmPayload(Pattern, Size, EA, DstReg);
  bool Encoded = IsLong ? BedrockMC::encodeLong(Payload, Tail, Bytes)
                        : BedrockMC::encodeMedium(Payload, Tail, Bytes);
  if (!Encoded)
    report_fatal_error("failed to encode Bedrock binary memory pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitBinaryMemDestPseudo(const MachineInstr *MI,
                                                StringRef Pattern,
                                                unsigned Size, bool IsLong,
                                                MemAddrKind AddrKind) {
  Register SrcReg = MI->getOperand(0).getReg();

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  switch (AddrKind) {
  case MemAddrKind::Reg:
    getMemEAForReg(MI->getOperand(1).getReg(), EA, Tail);
    break;
  case MemAddrKind::RegIndex:
    llvm_unreachable("indexed binary memory destination pseudo is not "
                     "supported");
  case MemAddrKind::RegOffset:
    getMemEAForRegOffset(MI->getOperand(1).getReg(), MI->getOperand(2).getImm(),
                         EA, Tail);
    break;
  case MemAddrKind::Abs:
    llvm_unreachable("absolute binary memory destination pseudo is not "
                     "supported");
  case MemAddrKind::Frame:
    getMemEAForSP(getFrameOffset(MI, 1), EA, Tail);
    break;
  }

  SmallVector<uint8_t, 16> Bytes;
  uint32_t Payload = applyPattern(Pattern, EA, Size, getGPRNo(SrcReg), 's');
  bool Encoded = IsLong ? BedrockMC::encodeLong(Payload, Tail, Bytes)
                        : BedrockMC::encodeMedium(Payload, Tail, Bytes);
  if (!Encoded)
    report_fatal_error(
        "failed to encode Bedrock binary memory destination pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitLongBinaryPseudo(const MachineInstr *MI,
                                             StringRef Pattern, unsigned Size) {
  Register DstReg = MI->getOperand(0).getReg();
  Register LHSReg = MI->getOperand(1).getReg();
  Register RHSReg = MI->getOperand(2).getReg();

  if (DstReg != LHSReg)
    emitRR(Size == 2 ? Bedrock::MOVLrr : Bedrock::MOVQrr, DstReg, LHSReg);

  SmallVector<uint8_t, 8> Bytes;
  uint32_t Payload =
      applyPattern(Pattern, getRegEA(RHSReg), Size, getGPRNo(DstReg), 'd');
  if (!BedrockMC::encodeLong(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock long binary pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitCmpImmPseudo(const MachineInstr *MI,
                                         unsigned Size) {
  Register LHSReg = MI->getOperand(0).getReg();
  const MachineOperand &ImmOp = MI->getOperand(1);

  if (ImmOp.isImm()) {
    int64_t Imm = ImmOp.getImm();
    SmallVector<uint8_t, 8> Tail;
    unsigned WidthCode;
    appendSignedAuto(Imm, Tail, WidthCode);

    SmallVector<uint8_t, 16> Bytes;
    uint32_t Payload =
        applyPattern("011111zddddeeeeeee", 0x6c + WidthCode, Size & 1,
                     getGPRNo(LHSReg), 'd');
    if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
      report_fatal_error("failed to encode Bedrock compare immediate pseudo");
    emitRaw(Bytes);
    return;
  }

  const MCExpr *Expr = lowerSymbolOperand(ImmOp);
  if (OutStreamer->hasRawTextSupport()) {
    SmallString<80> Text;
    raw_svector_ostream OS(Text);
    OS << "\tcmp." << getSizeSuffix(Size) << "\t";
    MAI->printExpr(OS, *Expr);
    OS << ", " << BedrockInstPrinter::getRegisterName(LHSReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  SmallVector<uint8_t, 4> Tail(4, 0);
  SmallVector<uint8_t, 16> Bytes;
  uint32_t Payload = applyPattern("011111zddddeeeeeee", 0x6e, Size & 1,
                                  getGPRNo(LHSReg), 'd');
  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock symbolic compare immediate");
  emitRawExpr(Bytes, 3, MCFixupKind(Bedrock::fixup_bedrock_imm32), Expr);
}

void BedrockAsmPrinter::emitCmpMemPseudo(const MachineInstr *MI, unsigned Size,
                                         MemAddrKind AddrKind,
                                         bool MemIsSrc) {
  const MCExpr *AbsExpr = nullptr;
  if (AddrKind == MemAddrKind::Abs) {
    const MachineOperand &Addr = MI->getOperand(MemIsSrc ? 0 : 1);
    AbsExpr = lowerSymbolOperand(Addr);

    if (OutStreamer->hasRawTextSupport()) {
      SmallString<96> Text;
      raw_svector_ostream OS(Text);
      OS << "\tcmp." << getSizeSuffix(Size) << "\t";
      if (MemIsSrc) {
        OS << "[";
        MAI->printExpr(OS, *AbsExpr);
        OS << "], "
           << BedrockInstPrinter::getRegisterName(MI->getOperand(1).getReg());
      } else {
        OS << BedrockInstPrinter::getRegisterName(MI->getOperand(0).getReg())
           << ", [";
        MAI->printExpr(OS, *AbsExpr);
        OS << "]";
      }
      OutStreamer->emitRawText(OS.str());
      return;
    }
  }

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  Register Reg;
  StringRef Pattern;

  if (MemIsSrc) {
    getMemEAFromOperands(MI, 0, AddrKind, EA, Tail);
    unsigned RegOp =
        AddrKind == MemAddrKind::Reg || AddrKind == MemAddrKind::Abs ? 1 : 2;
    Reg = MI->getOperand(RegOp).getReg();
    Pattern = "011111zddddeeeeeee";
  } else {
    Reg = MI->getOperand(0).getReg();
    getMemEAFromOperands(MI, 1, AddrKind, EA, Tail);
    Pattern = "011101zsssseeeeeee";
  }

  SmallVector<uint8_t, 16> Bytes;
  uint32_t Payload =
      applyPattern(Pattern, EA, Size & 1, getGPRNo(Reg), MemIsSrc ? 'd' : 's');
  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock compare memory pseudo");
  if (AddrKind == MemAddrKind::Abs)
    emitRawExpr(Bytes, 3, FK_Data_4, AbsExpr);
  else
    emitRaw(Bytes);
}

void BedrockAsmPrinter::emitCmpFrameFramePseudo(const MachineInstr *MI,
                                                unsigned Size) {
  uint8_t SrcEA;
  uint8_t DstEA;
  SmallVector<uint8_t, 8> Tail;
  getMemEAForSP(getFrameOffset(MI, 0), SrcEA, Tail);
  getMemEAForSP(getFrameOffset(MI, 2), DstEA, Tail);

  SmallVector<uint8_t, 16> Bytes;
  uint32_t Payload =
      applyPatternValues("1111100001zzsssssssddddddd",
                         {{'z', Size}, {'s', SrcEA}, {'d', DstEA}});
  if (!BedrockMC::encodeLong(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock frame compare pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitMemMoveRegRegPseudo(const MachineInstr *MI,
                                                unsigned Size) {
  uint8_t SrcEA;
  uint8_t DstEA;
  SmallVector<uint8_t, 8> Tail;
  if (PostIncMemMoveSrcOps.contains(MI))
    getMemEAForRegPostInc(MI->getOperand(0).getReg(), SrcEA, Tail);
  else
    getMemEAForReg(MI->getOperand(0).getReg(), SrcEA, Tail);
  if (PostIncMemMoveDstOps.contains(MI))
    getMemEAForRegPostInc(MI->getOperand(1).getReg(), DstEA, Tail);
  else
    getMemEAForReg(MI->getOperand(1).getReg(), DstEA, Tail);

  SmallVector<uint8_t, 16> Bytes;
  uint32_t Payload = applyPatternValues(
      "1111100000zzsssssssddddddd", {{'z', Size}, {'s', SrcEA}, {'d', DstEA}});
  if (!BedrockMC::encodeLong(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock memory move pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitMemMoveFold(const MachineInstr *LoadMI,
                                        const MachineInstr *StoreMI,
                                        unsigned Size, MemAddrKind SrcKind,
                                        MemAddrKind DstKind) {
  uint8_t SrcEA;
  uint8_t DstEA;
  SmallVector<uint8_t, 8> Tail;

  unsigned SrcTailStart = Tail.size();
  getMemEAFromOperands(LoadMI, 1, SrcKind, SrcEA, Tail);
  unsigned DstTailStart = Tail.size();
  getMemEAFromOperands(StoreMI, 1, DstKind, DstEA, Tail);

  SmallVector<uint8_t, 16> Bytes;
  uint32_t Payload = applyPatternValues(
      "1111100000zzsssssssddddddd", {{'z', Size}, {'s', SrcEA}, {'d', DstEA}});
  if (!BedrockMC::encodeLong(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock memory move fold");

  SmallVector<RawExprFixup, 2> Fixups;
  if (SrcKind == MemAddrKind::Abs)
    Fixups.push_back({4 + SrcTailStart, FK_Data_4,
                      lowerSymbolOperand(LoadMI->getOperand(1))});
  if (DstKind == MemAddrKind::Abs)
    Fixups.push_back({4 + DstTailStart, FK_Data_4,
                      lowerSymbolOperand(StoreMI->getOperand(1))});

  if (!Fixups.empty()) {
    emitRawExpr(Bytes, Fixups);
    return;
  }

  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitUnaryMemPseudo(const MachineInstr *MI,
                                           StringRef Pattern, unsigned Size,
                                           MemAddrKind AddrKind) {
  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  getMemEAFromOperands(MI, 0, AddrKind, EA, Tail);

  SmallVector<uint8_t, 16> Bytes;
  uint32_t Payload =
      applyPatternValues(Pattern, {{'e', EA}, {'z', Size & 1}});
  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock unary memory pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitUnaryFramePseudo(const MachineInstr *MI,
                                             StringRef Pattern,
                                             unsigned Size) {
  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  getMemEAForSP(getFrameOffset(MI, 0), EA, Tail);

  SmallVector<uint8_t, 16> Bytes;
  uint32_t Payload =
      applyPatternValues(Pattern, {{'e', EA}, {'z', Size & 1}});
  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock unary frame pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitUnaryAbsPseudo(const MachineInstr *MI,
                                           StringRef Pattern, unsigned Size) {
  const MachineOperand &Addr = MI->getOperand(0);
  const MCExpr *Expr = lowerSymbolOperand(Addr);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<96> Text;
    raw_svector_ostream OS(Text);
    OS << '\t' << (Pattern.contains("0011000") ? "dec" : "inc") << '.'
       << (Size == 3 ? 'q' : 'l') << "\t[";
    MAI->printExpr(OS, *Expr);
    OS << "]";
    OutStreamer->emitRawText(OS.str());
    return;
  }

  SmallVector<uint8_t, 4> Tail(4, 0);
  SmallVector<uint8_t, 8> Bytes;
  uint32_t Payload =
      applyPatternValues(Pattern, {{'e', 0x6a}, {'z', Size & 1}});
  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock unary absolute pseudo");
  emitRawExpr(Bytes, 3, FK_Data_4, Expr);
}

void BedrockAsmPrinter::emitFrameAddress(const MachineInstr *MI) {
  Register DstReg = MI->getOperand(0).getReg();
  if (getFrameOffset(MI, 1) == 0) {
    MCInst Inst;
    Inst.setOpcode(Bedrock::MOVQsr);
    Inst.addOperand(MCOperand::createReg(DstReg));
    emitMCInst(Inst);
    return;
  }

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  getMemEAForSP(getFrameOffset(MI, 1), EA, Tail);

  SmallVector<uint8_t, 16> Bytes;
  if (!BedrockMC::encodeMedium(getLeaPayload(EA, 3, DstReg), Tail, Bytes))
    report_fatal_error("failed to encode Bedrock frame address");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitRegOffsetAddress(const MachineInstr *MI) {
  Register DstReg = MI->getOperand(0).getReg();
  Register BaseReg = MI->getOperand(1).getReg();
  const MachineOperand &OffsetOp = MI->getOperand(2);

  if (isSymbolicAddressOperand(OffsetOp) && OutStreamer->hasRawTextSupport()) {
    SmallString<96> Text;
    raw_svector_ostream OS(Text);
    OS << "\tlea.q\t[" << BedrockInstPrinter::getRegisterName(BaseReg)
       << " + ";
    MAI->printExpr(OS, *lowerSymbolOperand(OffsetOp));
    OS << "], " << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  if (OffsetOp.isImm()) {
    getMemEAForRegOffset(BaseReg, OffsetOp.getImm(), EA, Tail);
  } else if (isSymbolicAddressOperand(OffsetOp)) {
    getMemEAForRegSymbol(BaseReg, EA, Tail);
  } else {
    report_fatal_error("invalid Bedrock register-offset address operand");
  }

  SmallVector<uint8_t, 16> Bytes;
  if (!BedrockMC::encodeMedium(getLeaPayload(EA, 3, DstReg), Tail, Bytes))
    report_fatal_error("failed to encode Bedrock register-offset address");
  if (OffsetOp.isImm()) {
    emitRaw(Bytes);
    return;
  }
  emitRawExpr(Bytes, 3, MCFixupKind(Bedrock::fixup_bedrock_disp32),
              lowerSymbolOperand(OffsetOp));
}

void BedrockAsmPrinter::emitScaledIndexAddress(const MachineInstr *MI) {
  Register DstReg = MI->getOperand(0).getReg();
  Register BaseReg = MI->getOperand(1).getReg();
  Register IndexReg = MI->getOperand(2).getReg();
  int64_t Scale = MI->getOperand(3).getImm();
  if (Scale < 1 || Scale > 3)
    report_fatal_error("invalid Bedrock scaled-index LEA scale");

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  getMemEAForRegIndex(BaseReg, IndexReg, EA, Tail);

  SmallVector<uint8_t, 16> Bytes;
  if (!BedrockMC::encodeMedium(getLeaPayload(EA, Scale, DstReg), Tail, Bytes))
    report_fatal_error("failed to encode Bedrock scaled-index address");
  emitRaw(Bytes);
}

static char getSizeSuffix(unsigned Size) {
  static const char Suffixes[] = {'b', 'w', 'l', 'q'};
  assert(Size < std::size(Suffixes) && "invalid Bedrock size suffix");
  return Suffixes[Size];
}

void BedrockAsmPrinter::emitAbsLoad(const MachineInstr *MI, unsigned Size,
                                    bool IsExt, bool IsSigned) {
  Register DstReg = MI->getOperand(0).getReg();
  const MachineOperand &Addr = MI->getOperand(1);
  const MCExpr *Expr = lowerSymbolOperand(Addr);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<96> Text;
    raw_svector_ostream OS(Text);
    if (IsExt)
      OS << "\text" << (IsSigned ? 's' : 'z') << "q." << getSizeSuffix(Size)
         << "\t[";
    else
      OS << "\tmov." << getSizeSuffix(Size) << "\t[";
    MAI->printExpr(OS, *Expr);
    OS << "], " << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  SmallVector<uint8_t, 4> Tail(4, 0);
  SmallVector<uint8_t, 8> Bytes;
  uint32_t Payload = IsExt ? getExtLoadPayload(Size, IsSigned, 0x6a, DstReg)
                           : getMovPayload(/*IsLoad=*/true, Size, 0x6a, DstReg);
  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock absolute load");
  emitRawExpr(Bytes, 3, FK_Data_4, Expr);
}

void BedrockAsmPrinter::emitAbsStore(const MachineInstr *MI, unsigned Size) {
  Register SrcReg = MI->getOperand(0).getReg();
  const MachineOperand &Addr = MI->getOperand(1);
  const MCExpr *Expr = lowerSymbolOperand(Addr);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<96> Text;
    raw_svector_ostream OS(Text);
    OS << "\tmov." << getSizeSuffix(Size) << "\t"
       << BedrockInstPrinter::getRegisterName(SrcReg) << ", [";
    MAI->printExpr(OS, *Expr);
    OS << "]";
    OutStreamer->emitRawText(OS.str());
    return;
  }

  SmallVector<uint8_t, 4> Tail(4, 0);
  SmallVector<uint8_t, 8> Bytes;
  if (!BedrockMC::encodeMedium(
          getMovPayload(/*IsLoad=*/false, Size, 0x6a, SrcReg), Tail, Bytes))
    report_fatal_error("failed to encode Bedrock absolute store");
  emitRawExpr(Bytes, 3, FK_Data_4, Expr);
}

void BedrockAsmPrinter::emitLoad(const MachineInstr *MI, unsigned Size,
                                 bool IsFrame, bool IsExt, bool IsSigned) {
  Register DstReg = MI->getOperand(0).getReg();

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  if (IsFrame)
    getMemEAForSP(getFrameOffset(MI, 1), EA, Tail);
  else if (PostIncMemOps.contains(MI))
    getMemEAForRegPostInc(MI->getOperand(1).getReg(), EA, Tail);
  else
    getMemEAForReg(MI->getOperand(1).getReg(), EA, Tail);

  uint32_t Payload = IsExt ? getExtLoadPayload(Size, IsSigned, EA, DstReg)
                           : getMovPayload(/*IsLoad=*/true, Size, EA, DstReg);

  SmallVector<uint8_t, 16> Bytes;
  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock load pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitLoadOffset(const MachineInstr *MI, unsigned Size,
                                       bool IsExt, bool IsSigned) {
  Register DstReg = MI->getOperand(0).getReg();
  Register BaseReg = MI->getOperand(1).getReg();
  int64_t Offset = MI->getOperand(2).getImm();

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  getMemEAForRegOffset(BaseReg, Offset, EA, Tail);

  uint32_t Payload = IsExt ? getExtLoadPayload(Size, IsSigned, EA, DstReg)
                           : getMovPayload(/*IsLoad=*/true, Size, EA, DstReg);

  SmallVector<uint8_t, 16> Bytes;
  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock offset load pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitLoadIndex(const MachineInstr *MI, unsigned Size,
                                      bool IsExt, bool IsSigned) {
  Register DstReg = MI->getOperand(0).getReg();
  Register BaseReg = MI->getOperand(1).getReg();
  Register IndexReg = MI->getOperand(2).getReg();

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  getMemEAForRegIndex(BaseReg, IndexReg, EA, Tail);

  uint32_t Payload = IsExt ? getExtLoadPayload(Size, IsSigned, EA, DstReg)
                           : getMovPayload(/*IsLoad=*/true, Size, EA, DstReg);

  SmallVector<uint8_t, 16> Bytes;
  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock indexed load pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitLoadSPIndex(const MachineInstr *MI, unsigned Size,
                                        bool IsExt, bool IsSigned) {
  Register DstReg = MI->getOperand(0).getReg();
  Register IndexReg = MI->getOperand(1).getReg();

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  getMemEAForSPIndex(IndexReg, EA, Tail);

  uint32_t Payload = IsExt ? getExtLoadPayload(Size, IsSigned, EA, DstReg)
                           : getMovPayload(/*IsLoad=*/true, Size, EA, DstReg);

  SmallVector<uint8_t, 16> Bytes;
  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock SP-indexed load pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitStore(const MachineInstr *MI, unsigned Size,
                                  bool IsFrame) {
  Register SrcReg = MI->getOperand(0).getReg();

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  if (IsFrame)
    getMemEAForSP(getFrameOffset(MI, 1), EA, Tail);
  else if (PostIncMemOps.contains(MI))
    getMemEAForRegPostInc(MI->getOperand(1).getReg(), EA, Tail);
  else
    getMemEAForReg(MI->getOperand(1).getReg(), EA, Tail);

  SmallVector<uint8_t, 16> Bytes;
  if (!BedrockMC::encodeMedium(
          getMovPayload(/*IsLoad=*/false, Size, EA, SrcReg), Tail, Bytes))
    report_fatal_error("failed to encode Bedrock store pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitStoreIndex(const MachineInstr *MI, unsigned Size) {
  Register SrcReg = MI->getOperand(0).getReg();
  Register BaseReg = MI->getOperand(1).getReg();
  Register IndexReg = MI->getOperand(2).getReg();

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  getMemEAForRegIndex(BaseReg, IndexReg, EA, Tail);

  SmallVector<uint8_t, 16> Bytes;
  if (!BedrockMC::encodeMedium(
          getMovPayload(/*IsLoad=*/false, Size, EA, SrcReg), Tail, Bytes))
    report_fatal_error("failed to encode Bedrock indexed store pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitStoreSPIndex(const MachineInstr *MI,
                                         unsigned Size) {
  Register SrcReg = MI->getOperand(0).getReg();
  Register IndexReg = MI->getOperand(1).getReg();

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  getMemEAForSPIndex(IndexReg, EA, Tail);

  SmallVector<uint8_t, 16> Bytes;
  if (!BedrockMC::encodeMedium(
          getMovPayload(/*IsLoad=*/false, Size, EA, SrcReg), Tail, Bytes))
    report_fatal_error("failed to encode Bedrock SP-indexed store pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitStoreOffset(const MachineInstr *MI,
                                        unsigned Size) {
  Register SrcReg = MI->getOperand(0).getReg();
  Register BaseReg = MI->getOperand(1).getReg();
  int64_t Offset = MI->getOperand(2).getImm();

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  getMemEAForRegOffset(BaseReg, Offset, EA, Tail);

  SmallVector<uint8_t, 16> Bytes;
  if (!BedrockMC::encodeMedium(
          getMovPayload(/*IsLoad=*/false, Size, EA, SrcReg), Tail, Bytes))
    report_fatal_error("failed to encode Bedrock offset store pseudo");
  emitRaw(Bytes);
}

static StringRef getClearMemPattern(unsigned Size) {
  return Size < 2 ? "0eee10z0110000eeee" : "0eee10z0111000eeee";
}

static void printMemOffset(raw_ostream &OS, int64_t Offset) {
  if (Offset == 0)
    return;
  if (Offset > 0)
    OS << " + " << Offset;
  else
    OS << " - " << -Offset;
}

static uint32_t getImmStorePayload(unsigned Size, uint8_t SrcEA,
                                   uint8_t DstEA);

void BedrockAsmPrinter::emitConstStoreFold(const MachineInstr *ConstMI,
                                           const MachineInstr *StoreMI,
                                           unsigned Size,
                                           MemAddrKind AddrKind) {
  const MCExpr *SrcExpr = lowerSymbolOperand(ConstMI->getOperand(1));

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<128> Text;
    raw_svector_ostream OS(Text);
    OS << "\tmov." << getSizeSuffix(Size) << "\t";
    MAI->printExpr(OS, *SrcExpr);
    OS << ", ";

    switch (AddrKind) {
    case MemAddrKind::Reg:
      OS << "[" << BedrockInstPrinter::getRegisterName(
                       StoreMI->getOperand(1).getReg())
         << "]";
      break;
    case MemAddrKind::RegOffset:
      OS << "[" << BedrockInstPrinter::getRegisterName(
                       StoreMI->getOperand(1).getReg());
      printMemOffset(OS, StoreMI->getOperand(2).getImm());
      OS << "]";
      break;
    case MemAddrKind::Abs:
      OS << "[";
      MAI->printExpr(OS, *lowerSymbolOperand(StoreMI->getOperand(1)));
      OS << "]";
      break;
    case MemAddrKind::Frame:
      OS << "[sp";
      printMemOffset(OS, getFrameOffset(StoreMI, 1));
      OS << "]";
      break;
    case MemAddrKind::RegIndex:
      llvm_unreachable("store fold does not use indexed destinations");
    }
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint8_t DstEA;
  SmallVector<uint8_t, 16> Tail;
  appendLE(Tail, 0, 4);
  getMemEAFromOperands(StoreMI, 1, AddrKind, DstEA, Tail);

  SmallVector<uint8_t, 24> Bytes;
  if (!BedrockMC::encodeLong(getImmStorePayload(Size, 0x6e, DstEA), Tail,
                             Bytes))
    report_fatal_error("failed to encode Bedrock symbolic store fold");

  SmallVector<RawExprFixup, 2> Fixups;
  Fixups.push_back(
      {4, MCFixupKind(Bedrock::fixup_bedrock_imm32), SrcExpr});
  if (AddrKind == MemAddrKind::Abs)
    Fixups.push_back(
        {8, FK_Data_4, lowerSymbolOperand(StoreMI->getOperand(1))});
  emitRawExpr(Bytes, Fixups);
}

void BedrockAsmPrinter::emitZeroMemStore(const MachineInstr *MI, unsigned Size,
                                         MemAddrKind AddrKind) {
  bool IsPostInc = AddrKind == MemAddrKind::Reg && PostIncMemOps.contains(MI);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<96> Text;
    raw_svector_ostream OS(Text);
    OS << "\tclr." << getSizeSuffix(Size) << "\t[";
    switch (AddrKind) {
    case MemAddrKind::Reg:
      OS << BedrockInstPrinter::getRegisterName(MI->getOperand(1).getReg());
      if (IsPostInc)
        OS << "++";
      break;
    case MemAddrKind::RegIndex:
      OS << BedrockInstPrinter::getRegisterName(MI->getOperand(1).getReg())
         << " + "
         << BedrockInstPrinter::getRegisterName(MI->getOperand(2).getReg());
      break;
    case MemAddrKind::RegOffset:
      OS << BedrockInstPrinter::getRegisterName(MI->getOperand(1).getReg());
      printMemOffset(OS, MI->getOperand(2).getImm());
      break;
    case MemAddrKind::Abs:
      MAI->printExpr(OS, *lowerSymbolOperand(MI->getOperand(1)));
      break;
    case MemAddrKind::Frame:
      OS << "sp";
      printMemOffset(OS, getFrameOffset(MI, 1));
      break;
    }
    OS << "]";
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  if (IsPostInc)
    getMemEAForRegPostInc(MI->getOperand(1).getReg(), EA, Tail);
  else
    getMemEAFromOperands(MI, 1, AddrKind, EA, Tail);

  SmallVector<uint8_t, 16> Bytes;
  uint32_t Payload = applyPatternValues(
      getClearMemPattern(Size), {{'e', EA}, {'z', Size & 1}});
  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock zero memory store");

  if (AddrKind == MemAddrKind::Abs) {
    emitRawExpr(Bytes, 3, FK_Data_4, lowerSymbolOperand(MI->getOperand(1)));
    return;
  }

  emitRaw(Bytes);
}

static uint32_t getImmStorePayload(unsigned Size, uint8_t SrcEA,
                                   uint8_t DstEA) {
  return applyPatternValues("1111100000zzsssssssddddddd",
                            {{'z', Size}, {'s', SrcEA}, {'d', DstEA}});
}

void BedrockAsmPrinter::emitImmStore(const MachineInstr *MI, unsigned Size,
                                     bool IsFrame) {
  int64_t Imm = MI->getOperand(0).getImm();

  uint8_t SrcEA;
  uint8_t DstEA;
  SmallVector<uint8_t, 16> Tail;
  unsigned WidthCode;
  appendSignedAuto(Imm, Tail, WidthCode);
  SrcEA = 0x6c + WidthCode;

  if (IsFrame)
    getMemEAForSP(getFrameOffset(MI, 1), DstEA, Tail);
  else
    getMemEAForReg(MI->getOperand(1).getReg(), DstEA, Tail);

  SmallVector<uint8_t, 24> Bytes;
  if (!BedrockMC::encodeLong(getImmStorePayload(Size, SrcEA, DstEA), Tail,
                             Bytes))
    report_fatal_error("failed to encode Bedrock immediate store pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitImmStoreOffset(const MachineInstr *MI,
                                           unsigned Size) {
  int64_t Imm = MI->getOperand(0).getImm();
  Register BaseReg = MI->getOperand(1).getReg();
  int64_t Offset = MI->getOperand(2).getImm();

  uint8_t SrcEA;
  uint8_t DstEA;
  SmallVector<uint8_t, 16> Tail;
  unsigned WidthCode;
  appendSignedAuto(Imm, Tail, WidthCode);
  SrcEA = 0x6c + WidthCode;
  getMemEAForRegOffset(BaseReg, Offset, DstEA, Tail);

  SmallVector<uint8_t, 24> Bytes;
  if (!BedrockMC::encodeLong(getImmStorePayload(Size, SrcEA, DstEA), Tail,
                             Bytes))
    report_fatal_error("failed to encode Bedrock immediate offset store pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitImmStoreAbs(const MachineInstr *MI,
                                        unsigned Size) {
  int64_t Imm = MI->getOperand(0).getImm();
  const MachineOperand &Addr = MI->getOperand(1);
  const MCExpr *Expr = lowerSymbolOperand(Addr);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<96> Text;
    raw_svector_ostream OS(Text);
    OS << "\tmov." << getSizeSuffix(Size) << "\t" << Imm << ", [";
    MAI->printExpr(OS, *Expr);
    OS << "]";
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint8_t SrcEA;
  SmallVector<uint8_t, 16> Tail;
  unsigned WidthCode;
  appendSignedAuto(Imm, Tail, WidthCode);
  SrcEA = 0x6c + WidthCode;
  appendLE(Tail, 0, 4);

  SmallVector<uint8_t, 24> Bytes;
  if (!BedrockMC::encodeLong(getImmStorePayload(Size, SrcEA, 0x6a), Tail,
                             Bytes))
    report_fatal_error("failed to encode Bedrock immediate absolute store");
  emitRawExpr(Bytes, 4 + getSignedAutoSize(Imm), FK_Data_4, Expr);
}

void BedrockAsmPrinter::emitFpuMove(Register DstReg, Register SrcReg) {
  if (OutStreamer->hasRawTextSupport()) {
    SmallString<64> Text;
    raw_svector_ostream OS(Text);
    OS << "\tFMOV.D\t" << BedrockInstPrinter::getRegisterName(SrcReg) << ", "
       << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint32_t Payload = applyPatternValues(
      "100100zssss110dddd",
      {{'z', 1}, {'s', getFPRNo(SrcReg)}, {'d', getFPRNo(DstReg)}});
  SmallVector<uint8_t, 4> Bytes;
  if (!BedrockMC::encodeMedium(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock floating-point move");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitFpuMove(const MachineInstr *MI) {
  emitFpuMove(MI->getOperand(0).getReg(), MI->getOperand(1).getReg());
}

void BedrockAsmPrinter::emitFpuClear(const MachineInstr *MI) {
  Register DstReg = MI->getOperand(0).getReg();
  if (OutStreamer->hasRawTextSupport()) {
    SmallString<48> Text;
    raw_svector_ostream OS(Text);
    OS << "\tFCLR\t" << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint32_t Payload =
      applyPatternValues("10010000001000dddd", {{'d', getFPRNo(DstReg)}});
  SmallVector<uint8_t, 4> Bytes;
  if (!BedrockMC::encodeMedium(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock floating-point clear");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitFpuConstant(const MachineInstr *MI) {
  Register DstReg = MI->getOperand(0).getReg();
  uint16_t ConstantID = MI->getOperand(1).getImm();
  if (OutStreamer->hasRawTextSupport()) {
    SmallString<64> Text;
    raw_svector_ostream OS(Text);
    OS << "\tFMOVCR.D\t" << ConstantID << ", "
       << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint32_t Payload = applyPatternValues("1111010110z01100010000dddd",
                                        {{'z', 1}, {'d', getFPRNo(DstReg)}});
  SmallVector<uint8_t, 2> Tail;
  appendLE(Tail, ConstantID, 2);
  SmallVector<uint8_t, 8> Bytes;
  if (!BedrockMC::encodeLong(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock floating-point constant");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitFpuComparePseudo(const MachineInstr *MI,
                                             bool IsDouble) {
  Register LHSReg = MI->getOperand(0).getReg();
  Register RHSReg = MI->getOperand(1).getReg();

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<64> Text;
    raw_svector_ostream OS(Text);
    OS << "\tFCMP." << (IsDouble ? 'D' : 'S') << "\t"
       << BedrockInstPrinter::getRegisterName(RHSReg) << ", "
       << BedrockInstPrinter::getRegisterName(LHSReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint32_t Payload = applyPatternValues(
      "100010zdddd000ssss",
      {{'z', IsDouble},
       {'s', getFPRNo(RHSReg)},
       {'d', getFPRNo(LHSReg)}});
  SmallVector<uint8_t, 4> Bytes;
  if (!BedrockMC::encodeMedium(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock floating-point compare");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitFpuTestPseudo(const MachineInstr *MI,
                                          bool IsDouble) {
  Register SrcReg = MI->getOperand(0).getReg();
  if (OutStreamer->hasRawTextSupport()) {
    SmallString<48> Text;
    raw_svector_ostream OS(Text);
    OS << "\tFTEST." << (IsDouble ? 'D' : 'S') << "\t"
       << BedrockInstPrinter::getRegisterName(SrcReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint32_t Payload = applyPatternValues(
      "1111010110z01100000000ssss",
      {{'z', IsDouble}, {'s', getFPRNo(SrcReg)}});
  SmallVector<uint8_t, 4> Bytes;
  if (!BedrockMC::encodeLong(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock floating-point test");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitFpuSelectPseudo(const MachineInstr *MI,
                                            bool IsDouble) {
  Register DstReg = MI->getOperand(0).getReg();
  Register LHSReg = MI->getOperand(1).getReg();
  Register RHSReg = MI->getOperand(2).getReg();
  Register TrueReg = MI->getOperand(3).getReg();
  unsigned Cond = MI->getOperand(5).getImm();

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<64> CompareText;
    raw_svector_ostream CompareOS(CompareText);
    CompareOS << "\tFCMP." << (IsDouble ? 'D' : 'S') << "\t"
              << BedrockInstPrinter::getRegisterName(RHSReg) << ", "
              << BedrockInstPrinter::getRegisterName(LHSReg);
    OutStreamer->emitRawText(CompareOS.str());

    auto EmitMoveText = [&](unsigned MoveCond) {
      SmallString<64> MoveText;
      raw_svector_ostream MoveOS(MoveText);
      MoveOS << "\tFMOV" << getCondSuffix(MoveCond) << "\t"
             << BedrockInstPrinter::getRegisterName(TrueReg) << ", "
             << BedrockInstPrinter::getRegisterName(DstReg);
      OutStreamer->emitRawText(MoveOS.str());
    };
    if (Cond == 0x11) {
      EmitMoveText(0x2);
      EmitMoveText(0x8);
    } else {
      EmitMoveText(Cond);
    }
    return;
  }

  uint32_t ComparePayload = applyPatternValues(
      "100010zdddd000ssss",
      {{'z', IsDouble},
       {'s', getFPRNo(RHSReg)},
       {'d', getFPRNo(LHSReg)}});
  SmallVector<uint8_t, 4> CompareBytes;
  if (!BedrockMC::encodeMedium(ComparePayload, {}, CompareBytes))
    report_fatal_error("failed to encode Bedrock floating-point compare");
  emitRaw(CompareBytes);

  auto EmitMove = [&](unsigned MoveCond) {
    uint32_t MovePayload = applyPatternValues(
        "11110110010ccccssss000dddd",
        {{'c', MoveCond},
         {'s', getFPRNo(TrueReg)},
         {'d', getFPRNo(DstReg)}});
    SmallVector<uint8_t, 4> MoveBytes;
    if (!BedrockMC::encodeLong(MovePayload, {}, MoveBytes))
      report_fatal_error(
          "failed to encode Bedrock floating-point conditional move");
    emitRaw(MoveBytes);
  };
  if (Cond == 0x11) {
    EmitMove(0x2);
    EmitMove(0x8);
  } else {
    EmitMove(Cond);
  }
}

void BedrockAsmPrinter::emitFpuSelectTestPseudo(const MachineInstr *MI,
                                                bool IsDouble) {
  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();
  Register TrueReg = MI->getOperand(2).getReg();
  unsigned Cond = MI->getOperand(4).getImm();

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<48> TestText;
    raw_svector_ostream TestOS(TestText);
    TestOS << "\tFTEST." << (IsDouble ? 'D' : 'S') << "\t"
           << BedrockInstPrinter::getRegisterName(SrcReg);
    OutStreamer->emitRawText(TestOS.str());

    auto EmitMoveText = [&](unsigned MoveCond) {
      SmallString<64> MoveText;
      raw_svector_ostream MoveOS(MoveText);
      MoveOS << "\tFMOV" << getCondSuffix(MoveCond) << "\t"
             << BedrockInstPrinter::getRegisterName(TrueReg) << ", "
             << BedrockInstPrinter::getRegisterName(DstReg);
      OutStreamer->emitRawText(MoveOS.str());
    };
    if (Cond == 0x11) {
      EmitMoveText(0x2);
      EmitMoveText(0x8);
    } else {
      EmitMoveText(Cond);
    }
    return;
  }

  uint32_t TestPayload = applyPatternValues(
      "1111010110z01100000000ssss",
      {{'z', IsDouble}, {'s', getFPRNo(SrcReg)}});
  SmallVector<uint8_t, 4> TestBytes;
  if (!BedrockMC::encodeLong(TestPayload, {}, TestBytes))
    report_fatal_error("failed to encode Bedrock floating-point test");
  emitRaw(TestBytes);

  auto EmitMove = [&](unsigned MoveCond) {
    uint32_t MovePayload = applyPatternValues(
        "11110110010ccccssss000dddd",
        {{'c', MoveCond},
         {'s', getFPRNo(TrueReg)},
         {'d', getFPRNo(DstReg)}});
    SmallVector<uint8_t, 4> MoveBytes;
    if (!BedrockMC::encodeLong(MovePayload, {}, MoveBytes))
      report_fatal_error(
          "failed to encode Bedrock floating-point conditional move");
    emitRaw(MoveBytes);
  };
  if (Cond == 0x11) {
    EmitMove(0x2);
    EmitMove(0x8);
  } else {
    EmitMove(Cond);
  }
}

void BedrockAsmPrinter::emitFpuBinaryPseudo(const MachineInstr *MI,
                                            StringRef Mnemonic,
                                            StringRef Pattern,
                                            bool IsDouble, bool IsLong) {
  Register DstReg = MI->getOperand(0).getReg();
  Register LHSReg = MI->getOperand(1).getReg();
  Register RHSReg = MI->getOperand(2).getReg();

  if (DstReg != LHSReg)
    emitFpuMove(DstReg, LHSReg);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<64> Text;
    raw_svector_ostream OS(Text);
    OS << "\t" << Mnemonic << "." << (IsDouble ? 'D' : 'S') << "\t"
       << BedrockInstPrinter::getRegisterName(RHSReg) << ", "
       << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint32_t Payload =
      applyPatternValues(Pattern, {{'z', IsDouble},
                                   {'s', getFPRNo(RHSReg)},
                                   {'d', getFPRNo(DstReg)}});
  SmallVector<uint8_t, 4> Bytes;
  bool Encoded = IsLong ? BedrockMC::encodeLong(Payload, {}, Bytes)
                        : BedrockMC::encodeMedium(Payload, {}, Bytes);
  if (!Encoded)
    report_fatal_error("failed to encode Bedrock floating-point arithmetic");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitFpuCopySignPseudo(const MachineInstr *MI,
                                              bool IsDouble) {
  Register DstReg = MI->getOperand(0).getReg();
  Register MagnitudeReg = MI->getOperand(1).getReg();
  Register SignReg = MI->getOperand(2).getReg();

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<80> Text;
    raw_svector_ostream OS(Text);
    OS << "\tFCOPYSIGN." << (IsDouble ? 'D' : 'S') << "\t"
       << BedrockInstPrinter::getRegisterName(SignReg) << ", "
       << BedrockInstPrinter::getRegisterName(MagnitudeReg) << ", "
       << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint32_t Payload = applyPatternValues(
      "1111010111zssssmmmm011dddd",
      {{'z', IsDouble},
       {'s', getFPRNo(SignReg)},
       {'m', getFPRNo(MagnitudeReg)},
       {'d', getFPRNo(DstReg)}});
  SmallVector<uint8_t, 4> Bytes;
  if (!BedrockMC::encodeLong(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock floating-point copy sign");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitFpuUnaryPseudo(const MachineInstr *MI,
                                           StringRef Mnemonic,
                                           StringRef Pattern,
                                           bool IsDouble, bool IsLong) {
  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();
  if (OutStreamer->hasRawTextSupport()) {
    SmallString<64> Text;
    raw_svector_ostream OS(Text);
    OS << "\t" << Mnemonic << "." << (IsDouble ? 'D' : 'S') << "\t"
       << BedrockInstPrinter::getRegisterName(SrcReg) << ", "
       << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint32_t Payload =
      applyPatternValues(Pattern, {{'z', IsDouble},
                                   {'s', getFPRNo(SrcReg)},
                                   {'d', getFPRNo(DstReg)}});
  SmallVector<uint8_t, 4> Bytes;
  bool Encoded = IsLong ? BedrockMC::encodeLong(Payload, {}, Bytes)
                        : BedrockMC::encodeMedium(Payload, {}, Bytes);
  if (!Encoded)
    report_fatal_error("failed to encode Bedrock floating-point unary op");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitFusedPseudo(const MachineInstr *MI,
                                        StringRef Mnemonic,
                                        StringRef Pattern, bool IsDouble) {
  Register DstReg = MI->getOperand(0).getReg();
  Register LHSReg = MI->getOperand(1).getReg();
  Register RHSReg = MI->getOperand(2).getReg();
  assert(DstReg == MI->getOperand(3).getReg() &&
         "fused accumulator must be tied to its destination");

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<64> Text;
    raw_svector_ostream OS(Text);
    OS << "\t" << Mnemonic << "." << (IsDouble ? 'D' : 'S') << "\t"
       << BedrockInstPrinter::getRegisterName(LHSReg) << ", "
       << BedrockInstPrinter::getRegisterName(RHSReg) << ", "
       << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  PatternFieldValue Fields[] = {{'z', IsDouble},
                                {'l', getFPRNo(LHSReg)},
                                {'r', getFPRNo(RHSReg)},
                                {'d', getFPRNo(DstReg)}};
  SmallVector<uint8_t, 4> Bytes;
  if (!BedrockMC::encodeLong(applyPatternValues(Pattern, Fields), {}, Bytes))
    report_fatal_error("failed to encode Bedrock fused FPU instruction");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitApproxUnaryPseudo(const MachineInstr *MI,
                                              StringRef Mnemonic,
                                              StringRef Pattern,
                                              bool IsDouble) {
  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();
  if (OutStreamer->hasRawTextSupport()) {
    SmallString<64> Text;
    raw_svector_ostream OS(Text);
    OS << "\t" << Mnemonic << "." << (IsDouble ? 'D' : 'S') << "\t"
       << BedrockInstPrinter::getRegisterName(SrcReg) << ", "
       << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  PatternFieldValue Fields[] = {{'z', IsDouble},
                                {'s', getFPRNo(SrcReg)},
                                {'d', getFPRNo(DstReg)}};
  SmallVector<uint8_t, 4> Bytes;
  if (!BedrockMC::encodeLong(applyPatternValues(Pattern, Fields), {}, Bytes))
    report_fatal_error("failed to encode Bedrock FPTRANSA instruction");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitSincosPseudo(const MachineInstr *MI,
                                         bool IsDouble) {
  Register SinReg = MI->getOperand(0).getReg();
  Register CosReg = MI->getOperand(1).getReg();
  Register SrcReg = MI->getOperand(2).getReg();
  if (OutStreamer->hasRawTextSupport()) {
    SmallString<64> Text;
    raw_svector_ostream OS(Text);
    OS << "\tFSINCOSA." << (IsDouble ? 'D' : 'S') << "\t"
       << BedrockInstPrinter::getRegisterName(SrcReg) << ", "
       << BedrockInstPrinter::getRegisterName(SinReg) << ", "
       << BedrockInstPrinter::getRegisterName(CosReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  constexpr StringLiteral Pattern =
      "1111110000001z01ssss000dddd000cccc";
  PatternFieldValue Fields[] = {{'z', IsDouble},
                                {'s', getFPRNo(SrcReg)},
                                {'d', getFPRNo(SinReg)},
                                {'c', getFPRNo(CosReg)}};
  SmallVector<uint8_t, 5> Bytes;
  if (!BedrockMC::encodeExtraLong(applyPatternValues64(Pattern, Fields), {},
                                  Bytes))
    report_fatal_error("failed to encode Bedrock FSINCOSA instruction");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitFpuConvert(const MachineInstr *MI,
                                       StringRef Mnemonic, StringRef Pattern,
                                       bool IsDouble, bool SrcIsFPR,
                                       bool DstIsFPR) {
  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();
  if (OutStreamer->hasRawTextSupport()) {
    SmallString<64> Text;
    raw_svector_ostream OS(Text);
    OS << "\t" << Mnemonic << "." << (IsDouble ? 'D' : 'S') << "\t"
       << BedrockInstPrinter::getRegisterName(SrcReg) << ", "
       << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  PatternFieldValue Fields[] = {
      {'z', IsDouble},
      {'s', SrcIsFPR ? getFPRNo(SrcReg) : getGPRNo(SrcReg)},
      {'d', DstIsFPR ? getFPRNo(DstReg) : getGPRNo(DstReg)},
  };
  SmallVector<uint8_t, 4> Bytes;
  if (!BedrockMC::encodeLong(applyPatternValues(Pattern, Fields), {}, Bytes))
    report_fatal_error("failed to encode Bedrock floating-point conversion");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitFpuAbsLoad(const MachineInstr *MI,
                                       bool IsDouble) {
  Register DstReg = MI->getOperand(0).getReg();
  const MachineOperand &Addr = MI->getOperand(1);
  const MCExpr *Expr = lowerSymbolOperand(Addr);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<96> Text;
    raw_svector_ostream OS(Text);
    OS << "\tFMOV." << (IsDouble ? 'D' : 'S') << "\t[";
    MAI->printExpr(OS, *Expr);
    OS << "], " << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  SmallVector<uint8_t, 4> Tail(4, 0);
  SmallVector<uint8_t, 8> Bytes;
  uint32_t Payload = applyPatternValues(
      "1111010101z0000ddddeeeeeee",
      {{'z', IsDouble}, {'d', getFPRNo(DstReg)}, {'e', 0x6a}});
  if (!BedrockMC::encodeLong(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock absolute FPU load");
  emitRawExpr(Bytes, 4, FK_Data_4, Expr);
}

void BedrockAsmPrinter::emitFpuAbsStore(const MachineInstr *MI,
                                        bool IsDouble) {
  Register SrcReg = MI->getOperand(0).getReg();
  const MachineOperand &Addr = MI->getOperand(1);
  const MCExpr *Expr = lowerSymbolOperand(Addr);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<96> Text;
    raw_svector_ostream OS(Text);
    OS << "\tFMOV." << (IsDouble ? 'D' : 'S') << "\t"
       << BedrockInstPrinter::getRegisterName(SrcReg)
       << ", [";
    MAI->printExpr(OS, *Expr);
    OS << "]";
    OutStreamer->emitRawText(OS.str());
    return;
  }

  SmallVector<uint8_t, 4> Tail(4, 0);
  SmallVector<uint8_t, 8> Bytes;
  uint32_t Payload = applyPatternValues(
      "1111011000z0000sssseeeeeee",
      {{'z', IsDouble}, {'s', getFPRNo(SrcReg)}, {'e', 0x6a}});
  if (!BedrockMC::encodeLong(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock absolute FPU store");
  emitRawExpr(Bytes, 4, FK_Data_4, Expr);
}

void BedrockAsmPrinter::emitFpuLoad(const MachineInstr *MI, bool IsFrame,
                                    bool IsDouble) {
  Register DstReg = MI->getOperand(0).getReg();

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<80> Text;
    raw_svector_ostream OS(Text);
    OS << "\tFMOV." << (IsDouble ? 'D' : 'S') << "\t[";
    if (IsFrame) {
      OS << "sp";
      printMemOffset(OS, getFrameOffset(MI, 1));
    } else {
      OS << BedrockInstPrinter::getRegisterName(MI->getOperand(1).getReg());
    }
    OS << "], " << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  if (IsFrame)
    getMemEAForSP(getFrameOffset(MI, 1), EA, Tail);
  else
    getMemEAForReg(MI->getOperand(1).getReg(), EA, Tail);

  SmallVector<uint8_t, 12> Bytes;
  uint32_t Payload = applyPatternValues(
      "1111010101z0000ddddeeeeeee",
      {{'z', IsDouble}, {'d', getFPRNo(DstReg)}, {'e', EA}});
  if (!BedrockMC::encodeLong(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock FPU load");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitFpuLoadOffset(const MachineInstr *MI,
                                          bool IsDouble) {
  Register DstReg = MI->getOperand(0).getReg();

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<80> Text;
    raw_svector_ostream OS(Text);
    OS << "\tFMOV." << (IsDouble ? 'D' : 'S') << "\t["
       << BedrockInstPrinter::getRegisterName(MI->getOperand(1).getReg());
    printMemOffset(OS, MI->getOperand(2).getImm());
    OS << "], " << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  getMemEAForRegOffset(MI->getOperand(1).getReg(), MI->getOperand(2).getImm(),
                       EA, Tail);

  SmallVector<uint8_t, 12> Bytes;
  uint32_t Payload = applyPatternValues(
      "1111010101z0000ddddeeeeeee",
      {{'z', IsDouble}, {'d', getFPRNo(DstReg)}, {'e', EA}});
  if (!BedrockMC::encodeLong(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock offset FPU load");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitFpuStore(const MachineInstr *MI, bool IsFrame,
                                     bool IsDouble) {
  Register SrcReg = MI->getOperand(0).getReg();

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<80> Text;
    raw_svector_ostream OS(Text);
    OS << "\tFMOV." << (IsDouble ? 'D' : 'S') << "\t"
       << BedrockInstPrinter::getRegisterName(SrcReg) << ", [";
    if (IsFrame) {
      OS << "sp";
      printMemOffset(OS, getFrameOffset(MI, 1));
    } else {
      OS << BedrockInstPrinter::getRegisterName(MI->getOperand(1).getReg());
    }
    OS << "]";
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  if (IsFrame)
    getMemEAForSP(getFrameOffset(MI, 1), EA, Tail);
  else
    getMemEAForReg(MI->getOperand(1).getReg(), EA, Tail);

  SmallVector<uint8_t, 12> Bytes;
  uint32_t Payload = applyPatternValues(
      "1111011000z0000sssseeeeeee",
      {{'z', IsDouble}, {'s', getFPRNo(SrcReg)}, {'e', EA}});
  if (!BedrockMC::encodeLong(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock FPU store");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitFpuStoreOffset(const MachineInstr *MI,
                                           bool IsDouble) {
  Register SrcReg = MI->getOperand(0).getReg();

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<80> Text;
    raw_svector_ostream OS(Text);
    OS << "\tFMOV." << (IsDouble ? 'D' : 'S') << "\t"
       << BedrockInstPrinter::getRegisterName(SrcReg) << ", ["
       << BedrockInstPrinter::getRegisterName(MI->getOperand(1).getReg());
    printMemOffset(OS, MI->getOperand(2).getImm());
    OS << "]";
    OutStreamer->emitRawText(OS.str());
    return;
  }

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  getMemEAForRegOffset(MI->getOperand(1).getReg(), MI->getOperand(2).getImm(),
                       EA, Tail);

  SmallVector<uint8_t, 12> Bytes;
  uint32_t Payload = applyPatternValues(
      "1111011000z0000sssseeeeeee",
      {{'z', IsDouble}, {'s', getFPRNo(SrcReg)}, {'e', EA}});
  if (!BedrockMC::encodeLong(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock offset FPU store");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitStackAdjust(const MachineInstr *MI, bool IsDown) {
  uint64_t Amount = MI->getOperand(0).getImm();
  if (Amount == 0)
    return;

  if (Amount == 8) {
    emitRaw({static_cast<uint8_t>(IsDown ? 0x0f : 0x0e)});
    return;
  }

  if (Amount <= 0xff) {
    MCInst Inst;
    Inst.setOpcode(IsDown ? Bedrock::SUBQisp : Bedrock::ADDQisp);
    Inst.addOperand(MCOperand::createImm(Amount));
    emitMCInst(Inst);
    return;
  }

  SmallVector<uint8_t, 4> Tail;
  SmallVector<uint8_t, 8> Bytes;
  uint32_t Payload;
  if (isUInt<16>(Amount)) {
    appendLE(Tail, Amount, 2);
    Payload = IsDown ? 0x2782 : 0x2780;
  } else if (isUInt<32>(Amount)) {
    appendLE(Tail, Amount, 4);
    Payload = IsDown ? 0x2783 : 0x2781;
  } else {
    report_fatal_error("Bedrock stack adjustment does not fit");
  }

  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock stack adjustment");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitBranch(const MachineInstr *MI, bool IsCond) {
  const MachineOperand &Target = MI->getOperand(0);
  unsigned Cond = IsCond ? MI->getOperand(1).getImm() : 0;
  const MCExpr *Expr = lowerSymbolOperand(Target);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<80> Text;
    raw_svector_ostream OS(Text);
    if (IsCond)
      OS << "\tj" << getCondSuffix(Cond) << "\t";
    else
      OS << "\tjmp\t";
    MAI->printExpr(OS, *Expr);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  auto ShortDisp = ShortBranchDisplacements.find(MI);
  if (ShortDisp != ShortBranchDisplacements.end()) {
    uint16_t Payload =
        ((IsCond ? (0x30 | Cond) : 0x30) << 8) |
        (static_cast<uint8_t>(ShortDisp->second) & 0xff);
    SmallVector<uint8_t, 2> Bytes;
    if (!BedrockMC::encodeShort(Payload, Bytes))
      report_fatal_error("failed to encode Bedrock short branch");
    emitRaw(Bytes);
    return;
  }

  auto MediumDisp = MediumBranchDisplacements.find(MI);
  if (MediumDisp != MediumBranchDisplacements.end()) {
    SmallVector<uint8_t, 2> Tail;
    appendLE(Tail, static_cast<uint64_t>(MediumDisp->second), 2);
    SmallVector<uint8_t, 8> Bytes;
    if (!BedrockMC::encodeMedium(0x2600 | Cond, Tail, Bytes))
      report_fatal_error("failed to encode Bedrock medium branch");
    emitRaw(Bytes);
    return;
  }

  SmallVector<uint8_t, 4> Tail(4, 0);
  SmallVector<uint8_t, 8> Bytes;
  if (!BedrockMC::encodeMedium(0x6600 | Cond, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock branch");
  emitRawExpr(Bytes, 3, MCFixupKind(Bedrock::fixup_bedrock_brdisp32), Expr);
}

void BedrockAsmPrinter::emitCmpTestJump(const MachineInstr *MI) {
  const MachineInstr *CmpMI = CmpTestJumpCmps.lookup(MI);
  if (!CmpMI)
    report_fatal_error("missing Bedrock cmpj/testj compare instruction");

  StringRef Pattern8;
  StringRef Pattern16;
  StringRef Base;
  unsigned Size;
  if (!getCmpTestJumpForm(*CmpMI, Pattern8, Pattern16, Base, Size))
    report_fatal_error("invalid Bedrock cmpj/testj compare instruction");

  Register SrcReg = CmpMI->getOperand(0).getReg();
  Register DstReg = CmpMI->getOperand(1).getReg();
  unsigned Cond = MI->getOperand(1).getImm();
  const MachineOperand &Target = MI->getOperand(0);
  const MCExpr *Expr = lowerSymbolOperand(Target);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<96> Text;
    raw_svector_ostream OS(Text);
    OS << "\t" << Base << getCondSuffix(Cond) << "." << getSizeSuffix(Size)
       << "\t" << BedrockInstPrinter::getRegisterName(SrcReg) << ", "
       << BedrockInstPrinter::getRegisterName(DstReg) << ", ";
    MAI->printExpr(OS, *Expr);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  auto DispIt = CmpTestJumpDisplacements.find(MI);
  if (DispIt == CmpTestJumpDisplacements.end())
    report_fatal_error("missing Bedrock cmpj/testj displacement");

  bool IsImm8 = CmpTestJump8Branches.contains(MI);
  int64_t Disp = DispIt->second;
  SmallVector<uint8_t, 2> Tail;
  appendLE(Tail, static_cast<uint64_t>(Disp), IsImm8 ? 1 : 2);

  PatternFieldValue Fields[] = {
      {'z', Size},
      {'c', Cond},
      {'s', getGPRNo(SrcReg)},
      {'d', getGPRNo(DstReg)},
  };
  SmallVector<uint8_t, 8> Bytes;
  uint32_t Payload = applyPatternValues(IsImm8 ? Pattern8 : Pattern16, Fields);
  if (!BedrockMC::encodeLong(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock cmpj/testj");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitDJ(const MachineInstr *MI) {
  const MachineInstr *DecMI = DJBranches.lookup(MI);
  if (!DecMI)
    report_fatal_error("missing Bedrock djt counter instruction");

  Register CounterReg = DecMI->getOperand(0).getReg();
  const MachineOperand &Target = MI->getOperand(0);
  const MCExpr *Expr = lowerSymbolOperand(Target);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<96> Text;
    raw_svector_ostream OS(Text);
    OS << "\tdjt\t" << BedrockInstPrinter::getRegisterName(CounterReg)
       << ", [pc + ";
    MAI->printExpr(OS, *Expr);
    OS << "]";
    OutStreamer->emitRawText(OS.str());
    return;
  }

  auto DispIt = DJDisplacements.find(MI);
  if (DispIt == DJDisplacements.end())
    report_fatal_error("missing Bedrock djt displacement");

  unsigned WidthCode = DJ8Branches.contains(MI) ? 0
                       : DJ16Branches.contains(MI) ? 1
                                                    : 2;
  unsigned Width = WidthCode == 0 ? 1 : (WidthCode == 1 ? 2 : 4);
  SmallVector<uint8_t, 4> Tail;
  appendLE(Tail, static_cast<uint64_t>(DispIt->second), Width);

  PatternFieldValue Fields[] = {
      {'c', 0},
      {'r', getGPRNo(CounterReg)},
      {'e', 0x64 + WidthCode},
  };
  SmallVector<uint8_t, 8> Bytes;
  uint32_t Payload =
      applyPatternValues("11110000111ccccrrrreeeeeee", Fields);
  if (!BedrockMC::encodeLong(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock djt");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitIJ(const MachineInstr *MI) {
  const MachineInstr *IncMI = IJBranches.lookup(MI);
  const MachineInstr *CmpMI = IJCmps.lookup(MI);
  if (!IncMI || !CmpMI)
    report_fatal_error("missing Bedrock ij instruction components");

  Register IndexReg = IncMI->getOperand(0).getReg();
  Register BoundReg = CmpMI->getOperand(0).getReg();
  unsigned Cond = MI->getOperand(1).getImm();
  const MCExpr *Expr = lowerSymbolOperand(MI->getOperand(0));

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<112> Text;
    raw_svector_ostream OS(Text);
    OS << "\tij" << getCondSuffix(Cond) << "\t"
       << BedrockInstPrinter::getRegisterName(IndexReg) << ", "
       << BedrockInstPrinter::getRegisterName(BoundReg) << ", [pc + ";
    MAI->printExpr(OS, *Expr);
    OS << "]";
    OutStreamer->emitRawText(OS.str());
    return;
  }

  constexpr uint8_t EA = 0x66; // [pc + disp32]
  uint64_t Payload = applyPatternValues64(
      "111111000100cccciiii000bbbbeeeeeee",
      {{'c', Cond},
       {'i', getGPRNo(IndexReg)},
       {'b', getGPRNo(BoundReg)},
       {'e', EA}});
  SmallVector<uint8_t, 4> Tail(4, 0);
  SmallVector<uint8_t, 12> Bytes;
  if (!BedrockMC::encodeExtraLong(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock ij instruction");
  emitRawExpr(Bytes, /*FixupOffset=*/5,
              MCFixupKind(Bedrock::fixup_bedrock_pcrel32), Expr);
}

void BedrockAsmPrinter::emitSetCC(const MachineInstr *MI) {
  Register DstReg = MI->getOperand(0).getReg();
  unsigned Cond = MI->getOperand(1).getImm();
  SmallVector<uint8_t, 2> Bytes;
  uint16_t Payload = 0x2100 | (getGPRNo(DstReg) << 4) | Cond;
  if (!BedrockMC::encodeShort(Payload, Bytes))
    report_fatal_error("failed to encode Bedrock setcc");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitCall(const MachineInstr *MI) {
  const MachineOperand &Target = MI->getOperand(0);

  if (OutStreamer->hasRawTextSupport() && !Target.isImm()) {
    const MCExpr *Expr = lowerSymbolOperand(Target);
    SmallString<80> Text;
    raw_svector_ostream OS(Text);
    OS << "\tcall\t";
    MAI->printExpr(OS, *Expr);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  SmallVector<uint8_t, 4> Tail;
  SmallVector<uint8_t, 8> Bytes;
  if (Target.isImm())
    appendLE(Tail, static_cast<uint64_t>(Target.getImm()), 4);
  else
    Tail.append(4, 0);
  if (!BedrockMC::encodeMedium(0xe600, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock call");
  if (Target.isImm()) {
    emitRaw(Bytes);
    return;
  }

  MCFixupKind Kind = Target.getTargetFlags() == BedrockII::MO_PLT32
                         ? MCFixupKind(Bedrock::fixup_bedrock_plt32)
                         : MCFixupKind(Bedrock::fixup_bedrock_call32);
  emitRawExpr(Bytes, 3, Kind,
              lowerSymbolOperand(Target));
}

void BedrockAsmPrinter::emitTailCall(const MachineInstr *MI) {
  const MachineOperand &Target = MI->getOperand(0);

  if (OutStreamer->hasRawTextSupport() && !Target.isImm()) {
    const MCExpr *Expr = lowerSymbolOperand(Target);
    SmallString<80> Text;
    raw_svector_ostream OS(Text);
    OS << "\tjmp\t";
    MAI->printExpr(OS, *Expr);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  SmallVector<uint8_t, 4> Tail;
  SmallVector<uint8_t, 8> Bytes;
  if (Target.isImm())
    appendLE(Tail, static_cast<uint64_t>(Target.getImm()), 4);
  else
    Tail.append(4, 0);
  if (!BedrockMC::encodeMedium(0x6600, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock tail call");
  if (Target.isImm()) {
    emitRaw(Bytes);
    return;
  }

  MCFixupKind Kind = Target.getTargetFlags() == BedrockII::MO_PLT32
                         ? MCFixupKind(Bedrock::fixup_bedrock_plt32)
                         : MCFixupKind(Bedrock::fixup_bedrock_brdisp32);
  emitRawExpr(Bytes, 3, Kind,
              lowerSymbolOperand(Target));
}

void BedrockAsmPrinter::emitTLSDescCall(const MachineInstr *MI) {
  const MachineOperand &Symbol = MI->getOperand(0);
  const MCExpr *Expr = lowerSymbolOperand(Symbol);
  bool Is64BitField =
      Symbol.getTargetFlags() == BedrockII::MO_TLSDESC64;

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<96> Text;
    raw_svector_ostream OS(Text);
    OS << "\tlea.q\t[pc + ";
    MAI->printExpr(OS, *Expr);
    OS << "], r0\n\tmov.q\t[r0], r1\n\tsub.q\t8, sp\n\t.tlsdesccall\t";
    MAI->printExpr(OS, *Expr);
    OS << "\n\tcall\tr1\n\tadd.q\t8, sp\n"
          "\tlea.q\t[gs0:0 + r0], r0";
    OutStreamer->emitRawText(OS.str());
    return;
  }

  unsigned FieldBytes = Is64BitField ? 8 : 4;
  SmallVector<uint8_t, 8> Tail(FieldBytes, 0);
  SmallVector<uint8_t, 16> LeaBytes;
  if (!BedrockMC::encodeMedium(
          getLeaPayload(Is64BitField ? 0x67 : 0x66, /*Size=*/3, Bedrock::R0),
          Tail, LeaBytes))
    report_fatal_error("failed to encode Bedrock TLSDESC address");
  emitRawExpr(
      LeaBytes, 3,
      MCFixupKind(Is64BitField ? Bedrock::fixup_bedrock_tlsdesc_gotpcrel64
                               : Bedrock::fixup_bedrock_tlsdesc_gotpcrel32),
      Expr);

  SmallVector<uint8_t, 8> LoadBytes;
  if (!BedrockMC::encodeMedium(
          getMovPayload(/*IsLoad=*/true, /*Size=*/3,
                        0x10 + getGPRNo(Bedrock::R0),
                        Bedrock::R1),
          {}, LoadBytes))
    report_fatal_error("failed to encode Bedrock TLSDESC resolver load");
  emitRaw(LoadBytes);

  // A near CALL pushes an 8-byte return PC. Maintain the baseline ABI's
  // 16-byte callee entry alignment around the specialized resolver call.
  emitRaw({0x0f});

  SmallVector<uint8_t, 4> CallBytes;
  uint32_t CallPayload = applyPatternValues(
      "1111000011011100000eeeeeee", {{'e', getRegEA(Bedrock::R1)}});
  if (!BedrockMC::encodeLong(CallPayload, {}, CallBytes))
    report_fatal_error("failed to encode Bedrock TLSDESC resolver call");
  emitRawExpr(CallBytes, 0,
              MCFixupKind(Bedrock::fixup_bedrock_tlsdesc_call), Expr);
  emitRaw({0x0e});

  SmallVector<uint8_t, 2> GSTail = {0xa9, 0x20};
  SmallVector<uint8_t, 8> ResultBytes;
  if (!BedrockMC::encodeMedium(
          getLeaPayload(/*EXT0=*/0x74, /*Size=*/3, Bedrock::R0), GSTail,
          ResultBytes))
    report_fatal_error("failed to encode Bedrock GS0 TLS address");
  emitRaw(ResultBytes);
}

void BedrockAsmPrinter::emitIndirectCall(const MachineInstr *MI) {
  Register CalleeReg = MI->getOperand(0).getReg();
  SmallVector<uint8_t, 4> Bytes;
  uint32_t CallPayload = applyPatternValues(
      "1111000011011100000eeeeeee", {{'e', getRegEA(CalleeReg)}});
  if (!BedrockMC::encodeLong(CallPayload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock indirect call");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitIndirectJump(const MachineInstr *MI) {
  Register TargetReg = MI->getOperand(0).getReg();
  SmallVector<uint8_t, 4> Bytes;
  uint32_t JmpPayload = applyPatternValues(
      "1111001001z00000000eeeeeee",
      {{'z', 1}, {'e', getRegEA(TargetReg)}});
  if (!BedrockMC::encodeLong(JmpPayload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock indirect jump");
  emitRaw(Bytes);
}

static unsigned getBedrockAtomicOrder(AtomicOrdering Order) {
  switch (Order) {
  case AtomicOrdering::NotAtomic:
  case AtomicOrdering::Unordered:
  case AtomicOrdering::Monotonic:
    return 0;
  case AtomicOrdering::Acquire:
    return 1;
  case AtomicOrdering::Release:
    return 2;
  case AtomicOrdering::AcquireRelease:
    return 3;
  case AtomicOrdering::SequentiallyConsistent:
    return 4;
  }
  llvm_unreachable("unknown Bedrock atomic ordering");
}

void BedrockAsmPrinter::emitAtomic(const MachineInstr *MI) {
  unsigned Size;
  StringRef Pattern;
  bool IsCmpXchg;
  if (!getAtomicEncodingInfo(MI->getOpcode(), Size, Pattern, IsCmpXchg))
    report_fatal_error("invalid Bedrock atomic pseudo");
  if (MI->memoperands_empty())
    report_fatal_error("Bedrock atomic pseudo has no memory operand");

  unsigned Order =
      getBedrockAtomicOrder((*MI->memoperands_begin())->getMergedOrdering());
  unsigned AddressEA = 0x10 | getGPRNo(MI->getOperand(1).getReg());
  uint64_t Payload;
  if (IsCmpXchg) {
    Payload = applyPatternValues64(
        Pattern, {{'z', Size},
                  {'o', Order},
                  {'x', getGPRNo(MI->getOperand(0).getReg())},
                  {'d', getGPRNo(MI->getOperand(3).getReg())},
                  {'e', AddressEA}});
  } else {
    Payload = applyPatternValues64(
        Pattern, {{'z', Size},
                  {'o', Order},
                  {'s', getGPRNo(MI->getOperand(0).getReg())},
                  {'e', AddressEA}});
  }

  SmallVector<uint8_t, 8> Bytes;
  if (!BedrockMC::encodeExtraLong(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock atomic instruction");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitPublicIntrinsic(const MachineInstr *MI) {
  SmallVector<uint8_t, 8> Tail;
  SmallVector<uint8_t, 16> Bytes;
  uint32_t Payload = 0;
  bool IsMedium = false;

  switch (MI->getOpcode()) {
  case Bedrock::BEDROCK_CPUID:
    Payload = applyPatternValues("00001001110000rrrr",
                                 {{'r', getGPRNo(MI->getOperand(0).getReg())}});
    IsMedium = true;
    break;
  case Bedrock::BEDROCK_TRACE:
    Payload = applyPatternValues("000010011110000100", {});
    appendLE(Tail, MI->getOperand(0).getImm(), 2);
    IsMedium = true;
    break;
  case Bedrock::BEDROCK_RDPMC:
    Payload = applyPatternValues("1111101111010001111010dddd",
                                 {{'d', getGPRNo(MI->getOperand(0).getReg())}});
    appendLE(Tail, MI->getOperand(1).getImm(), 2);
    break;
  case Bedrock::BEDROCK_RDSTATUS:
  case Bedrock::BEDROCK_RDFSTATUS:
  case Bedrock::BEDROCK_RDFFLAGS: {
    StringRef Pattern = MI->getOpcode() == Bedrock::BEDROCK_RDSTATUS
                            ? "1111101111010001110110dddd"
                        : MI->getOpcode() == Bedrock::BEDROCK_RDFSTATUS
                            ? "1111101111010001111000dddd"
                            : "1111101111010001110100dddd";
    Payload = applyPatternValues(Pattern,
                                 {{'d', getGPRNo(MI->getOperand(0).getReg())}});
    break;
  }
  case Bedrock::BEDROCK_WRFSTATUS:
  case Bedrock::BEDROCK_WRFFLAGS: {
    StringRef Pattern = MI->getOpcode() == Bedrock::BEDROCK_WRFSTATUS
                            ? "1111101111010001111001ssss"
                            : "1111101111010001110101ssss";
    Payload = applyPatternValues(Pattern,
                                 {{'s', getGPRNo(MI->getOperand(0).getReg())}});
    break;
  }
  case Bedrock::BEDROCK_CLMUL_B:
  case Bedrock::BEDROCK_CLMUL_W:
  case Bedrock::BEDROCK_CLMUL_L:
  case Bedrock::BEDROCK_CLMUL_Q: {
    unsigned Size = 0;
    switch (MI->getOpcode()) {
    case Bedrock::BEDROCK_CLMUL_B:
      Size = 0;
      break;
    case Bedrock::BEDROCK_CLMUL_W:
      Size = 1;
      break;
    case Bedrock::BEDROCK_CLMUL_L:
      Size = 2;
      break;
    case Bedrock::BEDROCK_CLMUL_Q:
      Size = 3;
      break;
    default:
      llvm_unreachable("not a CLMUL pseudo");
    }
    Payload = applyPatternValues("1111000010zz011ddddeeeeeee",
                                 {{'z', Size},
                                  {'d', getGPRNo(MI->getOperand(0).getReg())},
                                  {'e', getRegEA(MI->getOperand(2).getReg())}});
    break;
  }
  case Bedrock::BEDROCK_MOVNT_B:
  case Bedrock::BEDROCK_MOVNT_W:
  case Bedrock::BEDROCK_MOVNT_L:
  case Bedrock::BEDROCK_MOVNT_Q: {
    unsigned Size = 0;
    switch (MI->getOpcode()) {
    case Bedrock::BEDROCK_MOVNT_B:
      Size = 0;
      break;
    case Bedrock::BEDROCK_MOVNT_W:
      Size = 1;
      break;
    case Bedrock::BEDROCK_MOVNT_L:
      Size = 2;
      break;
    case Bedrock::BEDROCK_MOVNT_Q:
      Size = 3;
      break;
    default:
      llvm_unreachable("not a MOVNT pseudo");
    }
    uint8_t EA;
    getMemEAForReg(MI->getOperand(1).getReg(), EA, Tail);
    Payload = applyPatternValues(
        "1111001000zz000sssseeeeeee",
        {{'z', Size}, {'s', getGPRNo(MI->getOperand(0).getReg())}, {'e', EA}});
    break;
  }
  case Bedrock::BEDROCK_FCLASS_S:
  case Bedrock::BEDROCK_FCLASS_D:
    Payload =
        applyPatternValues("1111010111z0000ssss010dddd",
                           {{'z', MI->getOpcode() == Bedrock::BEDROCK_FCLASS_D},
                            {'s', getFPRNo(MI->getOperand(1).getReg())},
                            {'d', getGPRNo(MI->getOperand(0).getReg())}});
    break;
  default:
    llvm_unreachable("not a Bedrock public intrinsic pseudo");
  }

  bool Encoded = IsMedium ? BedrockMC::encodeMedium(Payload, Tail, Bytes)
                          : BedrockMC::encodeLong(Payload, Tail, Bytes);
  if (!Encoded)
    report_fatal_error("failed to encode Bedrock public intrinsic");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitSystemIntrinsic(const MachineInstr *MI) {
  auto EmitLong = [&](uint32_t Payload, ArrayRef<uint8_t> Tail = {}) {
    SmallVector<uint8_t, 16> Bytes;
    if (!BedrockMC::encodeLong(Payload, Tail, Bytes))
      report_fatal_error("failed to encode Bedrock system intrinsic");
    emitRaw(Bytes);
  };
  auto RegEA = [&](Register Reg, SmallVectorImpl<uint8_t> &Tail) {
    uint8_t EA;
    getMemEAForReg(Reg, EA, Tail);
    return EA;
  };

  SmallVector<uint8_t, 8> Tail;
  uint32_t Payload = 0;
  switch (MI->getOpcode()) {
  case Bedrock::BEDROCK_WRSTATUS:
    Payload = applyPatternValues("1111101111010001110111ssss",
                                 {{'s', getGPRNo(MI->getOperand(0).getReg())}});
    break;
  case Bedrock::BEDROCK_RDCR:
    Payload = applyPatternValues("1111101111010001110000dddd",
                                 {{'d', getGPRNo(MI->getOperand(0).getReg())}});
    appendLE(Tail, MI->getOperand(1).getImm(), 2);
    break;
  case Bedrock::BEDROCK_WRCR:
    Payload = applyPatternValues("1111101111010001110001ssss",
                                 {{'s', getGPRNo(MI->getOperand(0).getReg())}});
    appendLE(Tail, MI->getOperand(1).getImm(), 2);
    break;
  case Bedrock::BEDROCK_RDSEG:
    Payload = applyPatternValues("1111101111010000000sssdddd",
                                 {{'s', unsigned(MI->getOperand(1).getImm())},
                                  {'d', getGPRNo(MI->getOperand(0).getReg())}});
    break;
  case Bedrock::BEDROCK_RDSEG_CS:
    Payload = applyPatternValues("1111101111010001111100dddd",
                                 {{'d', getGPRNo(MI->getOperand(0).getReg())}});
    break;
  case Bedrock::BEDROCK_WRSEG:
    Payload = applyPatternValues("1111101111010000001sssdddd",
                                 {{'s', unsigned(MI->getOperand(1).getImm())},
                                  {'d', getGPRNo(MI->getOperand(0).getReg())}});
    break;
  case Bedrock::BEDROCK_FLSHDCACHE:
  case Bedrock::BEDROCK_INVDCACHE:
  case Bedrock::BEDROCK_INVICACHE:
  case Bedrock::BEDROCK_WRBKDCACHE:
  case Bedrock::BEDROCK_SYNCCACHE: {
    StringRef Pattern;
    switch (MI->getOpcode()) {
    case Bedrock::BEDROCK_FLSHDCACHE:
      Pattern = "1111101111010000011eeeeeee";
      break;
    case Bedrock::BEDROCK_INVDCACHE:
      Pattern = "1111101111010000100eeeeeee";
      break;
    case Bedrock::BEDROCK_INVICACHE:
      Pattern = "1111101111010000101eeeeeee";
      break;
    case Bedrock::BEDROCK_SYNCCACHE:
      Pattern = "1111101111010000111eeeeeee";
      break;
    case Bedrock::BEDROCK_WRBKDCACHE:
      Pattern = "1111101111010001000eeeeeee";
      break;
    default:
      llvm_unreachable("not a cache pseudo");
    }
    Payload = applyPatternValues(
        Pattern, {{'e', RegEA(MI->getOperand(0).getReg(), Tail)}});
    break;
  }
  case Bedrock::BEDROCK_INVTLB:
    Payload = applyPatternValues("11111011110100011000000001", {});
    break;
  case Bedrock::BEDROCK_INVPAGE:
    Payload =
        applyPatternValues("1111101111010000010eeeeeee",
                           {{'e', RegEA(MI->getOperand(0).getReg(), Tail)}});
    break;
  case Bedrock::BEDROCK_INVASID:
    Payload = applyPatternValues("11111011110100011000000000", {});
    appendLE(Tail, MI->getOperand(0).getImm(), 2);
    break;
  case Bedrock::BEDROCK_SWPT:
    Payload = applyPatternValues("1111101111010001111011pppp",
                                 {{'p', getGPRNo(MI->getOperand(0).getReg())}});
    break;
  case Bedrock::BEDROCK_SWPTA:
    Payload = applyPatternValues("111110111101001aaaa001pppp",
                                 {{'p', getGPRNo(MI->getOperand(0).getReg())},
                                  {'a', getGPRNo(MI->getOperand(1).getReg())}});
    break;
  case Bedrock::BEDROCK_VTOP: {
    Payload = applyPatternValues("111110111101001vvvv000pppp",
                                 {{'v', getGPRNo(MI->getOperand(2).getReg())},
                                  {'p', getGPRNo(MI->getOperand(0).getReg())}});
    EmitLong(Payload);
    Payload = applyPatternValues("1111101111010001110010dddd",
                                 {{'d', getGPRNo(MI->getOperand(1).getReg())}});
    EmitLong(Payload);
    return;
  }
  case Bedrock::BEDROCK_PTQUERY: {
    uint8_t EA = RegEA(MI->getOperand(2).getReg(), Tail);
    Payload = applyPatternValues("111110111100iiiddddeeeeeee",
                                 {{'i', unsigned(MI->getOperand(3).getImm())},
                                  {'d', getGPRNo(MI->getOperand(0).getReg())},
                                  {'e', EA}});
    EmitLong(Payload, Tail);
    Payload = applyPatternValues("1111101111010001110010dddd",
                                 {{'d', getGPRNo(MI->getOperand(1).getReg())}});
    EmitLong(Payload);
    return;
  }
  case Bedrock::BEDROCK_SAVE:
  case Bedrock::BEDROCK_RESTORE: {
    StringRef Pattern = MI->getOpcode() == Bedrock::BEDROCK_SAVE
                            ? "1111101111010001001eeeeeee"
                            : "1111101111010001010eeeeeee";
    Payload = applyPatternValues(
        Pattern, {{'e', RegEA(MI->getOperand(0).getReg(), Tail)}});
    break;
  }
  default:
    llvm_unreachable("not a Bedrock system intrinsic pseudo");
  }
  EmitLong(Payload, Tail);
}

void BedrockAsmPrinter::emitInstruction(const MachineInstr *MI) {
  if (auto It = RepgStarts.find(MI); It != RepgStarts.end())
    emitRepgHeader(It->second.CounterReg, It->second.BodyBytes);
  if (RepgSuppressedInstrs.contains(MI)) {
    if (RepgEndMarkers.contains(MI) && OutStreamer->hasRawTextSupport())
      OutStreamer->emitRawText("\t}");
    return;
  }
  if (DJCounterInstrs.contains(MI) || DJTestInstrs.contains(MI))
    return;
  if (IJCounterInstrs.contains(MI) || IJCompareInstrs.contains(MI))
    return;
  if (CmpTestJumpCompareInstrs.contains(MI))
    return;
  if (BitTestSuppressedInstrs.contains(MI))
    return;
  if (ConstStoreConsts.contains(MI))
    return;
  if (const MachineInstr *ConstMI = ConstStoreStores.lookup(MI)) {
    unsigned Size;
    MemAddrKind AddrKind;
    Register SrcReg;
    if (!getRegStoreInfo(*MI, Size, AddrKind, SrcReg))
      report_fatal_error("invalid Bedrock constant store fold");
    emitConstStoreFold(ConstMI, MI, Size, AddrKind);
    return;
  }
  if (ZeroMemStoreClears.contains(MI))
    return;
  if (auto It = BitTestAnds.find(MI); It != BitTestAnds.end()) {
    emitBTestImm(It->second.first, It->second.second);
    return;
  }
  if (MemMoveLoads.contains(MI))
    return;
  if (MI->getOpcode() == Bedrock::REP_MEMSETB) {
    emitRepMemset(MI);
    return;
  }
  if (const MachineInstr *LoadMI = MemMoveStores.lookup(MI)) {
    unsigned Size;
    MemAddrKind SrcKind;
    MemAddrKind DstKind;
    if (!getMemMoveFold(*LoadMI, *MI, Size, SrcKind, DstKind))
      report_fatal_error("invalid Bedrock memory move fold");
    emitMemMoveFold(LoadMI, MI, Size, SrcKind, DstKind);
    return;
  }
  if (ZeroCopyClears.contains(MI)) {
    MCInst Inst;
    Inst.setOpcode(Bedrock::CLRQr);
    Inst.addOperand(MCOperand::createReg(MI->getOperand(0).getReg()));
    emitMCInst(Inst);
    return;
  }
  if (ZeroMemStores.contains(MI)) {
    unsigned Size;
    MemAddrKind AddrKind;
    Register SrcReg;
    if (!getRegStoreInfo(*MI, Size, AddrKind, SrcReg))
      report_fatal_error("invalid Bedrock zero memory store fold");
    emitZeroMemStore(MI, Size, AddrKind);
    return;
  }
  {
    unsigned Size;
    MemAddrKind AddrKind;
    if (getImmZeroStoreInfo(*MI, Size, AddrKind)) {
      emitZeroMemStore(MI, Size, AddrKind);
      return;
    }
  }

  unsigned AtomicSize;
  StringRef AtomicPattern;
  bool IsCmpXchg;
  if (getAtomicEncodingInfo(MI->getOpcode(), AtomicSize, AtomicPattern,
                            IsCmpXchg)) {
    emitAtomic(MI);
    return;
  }

  switch (MI->getOpcode()) {
  case Bedrock::CONST32:
    emitConst(MI, /*Is64=*/false);
    return;
  case Bedrock::CONST64:
    emitConst(MI, /*Is64=*/true);
    return;
  case Bedrock::EXTSQBrr:
    emitExtQRegPseudo(MI, "1100010sssseeeeeee");
    return;
  case Bedrock::EXTSQWrr:
    emitExtQRegPseudo(MI, "1100100sssseeeeeee");
    return;
  case Bedrock::EXTZQBrr:
    emitExtQRegPseudo(MI, "1101010sssseeeeeee");
    return;
  case Bedrock::EXTZQWrr:
    emitExtQRegPseudo(MI, "1101100sssseeeeeee");
    return;
  case Bedrock::INCL3r:
    emitUnaryPseudo(MI, Bedrock::INCLr, Bedrock::MOVLrr);
    return;
  case Bedrock::INCQ3r:
    emitUnaryPseudo(MI, Bedrock::INCQr, Bedrock::MOVQrr);
    return;
  case Bedrock::DECL3r:
    emitUnaryPseudo(MI, Bedrock::DECLr, Bedrock::MOVLrr);
    return;
  case Bedrock::DECQ3r:
    emitUnaryPseudo(MI, Bedrock::DECQr, Bedrock::MOVQrr);
    return;
  case Bedrock::INCLm:
    emitUnaryMemPseudo(MI, "0eee10z0001000eeee", 2, MemAddrKind::Reg);
    return;
  case Bedrock::INCQm:
    emitUnaryMemPseudo(MI, "0eee10z0001000eeee", 3, MemAddrKind::Reg);
    return;
  case Bedrock::DECLm:
    emitUnaryMemPseudo(MI, "0eee10z0011000eeee", 2, MemAddrKind::Reg);
    return;
  case Bedrock::DECQm:
    emitUnaryMemPseudo(MI, "0eee10z0011000eeee", 3, MemAddrKind::Reg);
    return;
  case Bedrock::INCLmo:
    emitUnaryMemPseudo(MI, "0eee10z0001000eeee", 2,
                       MemAddrKind::RegOffset);
    return;
  case Bedrock::INCQmo:
    emitUnaryMemPseudo(MI, "0eee10z0001000eeee", 3,
                       MemAddrKind::RegOffset);
    return;
  case Bedrock::DECLmo:
    emitUnaryMemPseudo(MI, "0eee10z0011000eeee", 2,
                       MemAddrKind::RegOffset);
    return;
  case Bedrock::DECQmo:
    emitUnaryMemPseudo(MI, "0eee10z0011000eeee", 3,
                       MemAddrKind::RegOffset);
    return;
  case Bedrock::INCLfi:
    emitUnaryFramePseudo(MI, "0eee10z0001000eeee", 2);
    return;
  case Bedrock::INCQfi:
    emitUnaryFramePseudo(MI, "0eee10z0001000eeee", 3);
    return;
  case Bedrock::DECLfi:
    emitUnaryFramePseudo(MI, "0eee10z0011000eeee", 2);
    return;
  case Bedrock::DECQfi:
    emitUnaryFramePseudo(MI, "0eee10z0011000eeee", 3);
    return;
  case Bedrock::INCLabs:
    emitUnaryAbsPseudo(MI, "0eee10z0001000eeee", 2);
    return;
  case Bedrock::INCQabs:
    emitUnaryAbsPseudo(MI, "0eee10z0001000eeee", 3);
    return;
  case Bedrock::DECLabs:
    emitUnaryAbsPseudo(MI, "0eee10z0011000eeee", 2);
    return;
  case Bedrock::DECQabs:
    emitUnaryAbsPseudo(MI, "0eee10z0011000eeee", 3);
    return;
  case Bedrock::NEGL3r:
    emitUnaryPseudo(MI, Bedrock::NEGLr, Bedrock::MOVLrr);
    return;
  case Bedrock::NEGQ3r:
    emitUnaryPseudo(MI, Bedrock::NEGQr, Bedrock::MOVQrr);
    return;
  case Bedrock::ABSL3r:
    emitUnaryPseudo(MI, Bedrock::ABSLr, Bedrock::MOVLrr);
    return;
  case Bedrock::ABSQ3r:
    emitUnaryPseudo(MI, Bedrock::ABSQr, Bedrock::MOVQrr);
    return;
  case Bedrock::NOTL3r:
    emitUnaryPseudo(MI, Bedrock::NOTLr, Bedrock::MOVLrr);
    return;
  case Bedrock::NOTQ3r:
    emitUnaryPseudo(MI, Bedrock::NOTQr, Bedrock::MOVQrr);
    return;
  case Bedrock::BSWAPL3r:
    emitUnaryPseudo(MI, Bedrock::REVBYTELr, Bedrock::MOVLrr);
    return;
  case Bedrock::BSWAPQ3r:
    emitUnaryPseudo(MI, Bedrock::REVBYTEQr, Bedrock::MOVQrr);
    return;
  case Bedrock::CLZLrr:
    emitLongCountPseudo(MI, "clz", "1111000000zz100ddddeeeeeee", 2);
    return;
  case Bedrock::CLZQrr:
    emitLongCountPseudo(MI, "clz", "1111000000zz100ddddeeeeeee", 3);
    return;
  case Bedrock::CTZLrr:
    emitLongCountPseudo(MI, "ctz", "1111000000zz101ddddeeeeeee", 2);
    return;
  case Bedrock::CTZQrr:
    emitLongCountPseudo(MI, "ctz", "1111000000zz101ddddeeeeeee", 3);
    return;
  case Bedrock::POPCNTLrr:
    emitLongCountPseudo(MI, "popcnt", "1111000010zz000ddddeeeeeee", 2);
    return;
  case Bedrock::POPCNTQrr:
    emitLongCountPseudo(MI, "popcnt", "1111000010zz000ddddeeeeeee", 3);
    return;
  case Bedrock::PARITYLrr:
    emitLongCountPseudo(MI, "parity", "1111000010zz001ddddeeeeeee", 2);
    return;
  case Bedrock::PARITYQrr:
    emitLongCountPseudo(MI, "parity", "1111000010zz001ddddeeeeeee", 3);
    return;
  case Bedrock::CLSLrr:
    emitLongCountPseudo(MI, "cls", "1111000000zz110ddddeeeeeee", 2);
    return;
  case Bedrock::CLSQrr:
    emitLongCountPseudo(MI, "cls", "1111000000zz110ddddeeeeeee", 3);
    return;
  case Bedrock::CTSLrr:
    emitLongCountPseudo(MI, "cts", "1111000000zz111ddddeeeeeee", 2);
    return;
  case Bedrock::CTSQrr:
    emitLongCountPseudo(MI, "cts", "1111000000zz111ddddeeeeeee", 3);
    return;
  case Bedrock::BTESTLri:
  case Bedrock::BTESTQri:
    emitBTestImm(MI->getOperand(0).getReg(), MI->getOperand(1).getImm());
    return;
  case Bedrock::ADDCL3rr:
    emitCarryStartPseudo(MI, /*IsAdd=*/true, 2);
    return;
  case Bedrock::ADDCQ3rr:
    emitCarryStartPseudo(MI, /*IsAdd=*/true, 3);
    return;
  case Bedrock::SUBCL3rr:
    emitCarryStartPseudo(MI, /*IsAdd=*/false, 2);
    return;
  case Bedrock::SUBCQ3rr:
    emitCarryStartPseudo(MI, /*IsAdd=*/false, 3);
    return;
  case Bedrock::INCFL3r:
    emitFlagUnaryPseudo(MI, "incf", "000010z0z01000rrrr", 2);
    return;
  case Bedrock::INCFQ3r:
    emitFlagUnaryPseudo(MI, "incf", "000010z0z01000rrrr", 3);
    return;
  case Bedrock::DECFL3r:
    emitFlagUnaryPseudo(MI, "decf", "000010z0z11000rrrr", 2);
    return;
  case Bedrock::DECFQ3r:
    emitFlagUnaryPseudo(MI, "decf", "000010z0z11000rrrr", 3);
    return;
  case Bedrock::ADCL3rr:
    emitCarryPseudo(MI, "adc", "1100zz1ssss000dddd", 2);
    return;
  case Bedrock::ADCQ3rr:
    emitCarryPseudo(MI, "adc", "1100zz1ssss000dddd", 3);
    return;
  case Bedrock::SBBL3rr:
    emitCarryPseudo(MI, "sbb", "1101zz1ssss000dddd", 2);
    return;
  case Bedrock::SBBQ3rr:
    emitCarryPseudo(MI, "sbb", "1101zz1ssss000dddd", 3);
    return;
  case Bedrock::MULHUQ3rr:
    emitLongMulHighPseudo(MI, "mulhu", "111110111101001ssss100dddd");
    return;
  case Bedrock::MULHSQ3rr:
    emitLongMulHighPseudo(MI, "mulhs", "111110111101001ssss101dddd");
    return;
  case Bedrock::MULHSUQ3rr:
    emitLongMulHighPseudo(MI, "mulhsu", "111110111101001ssss110dddd");
    return;
  case Bedrock::CLMULL3rr:
    emitLongBinaryPseudo(MI, "1111000010zz011ddddeeeeeee", 2);
    return;
  case Bedrock::CLMULQ3rr:
    emitLongBinaryPseudo(MI, "1111000010zz011ddddeeeeeee", 3);
    return;
  case Bedrock::CLMULHQ3rr:
    emitLongBinaryPseudo(MI, "111100001100110ddddeeeeeee", 3);
    return;
  case Bedrock::DIVMODULrr:
    emitDivModPseudo(MI, /*IsSigned=*/false, 2);
    return;
  case Bedrock::DIVMODUQrr:
    emitDivModPseudo(MI, /*IsSigned=*/false, 3);
    return;
  case Bedrock::DIVMODSLrr:
    emitDivModPseudo(MI, /*IsSigned=*/true, 2);
    return;
  case Bedrock::DIVMODSQrr:
    emitDivModPseudo(MI, /*IsSigned=*/true, 3);
    return;
  case Bedrock::EXTRACTLrrri:
    emitExtractPseudo(MI, 2);
    return;
  case Bedrock::EXTRACTQrrri:
    emitExtractPseudo(MI, 3);
    return;
  case Bedrock::SMAX_ZERO_L:
    emitLongZeroMinMaxPseudo(MI, "1111000001zz110ddddeeeeeee", 2);
    return;
  case Bedrock::SMAX_ZERO_Q:
    emitLongZeroMinMaxPseudo(MI, "1111000001zz110ddddeeeeeee", 3);
    return;
  case Bedrock::SMIN_ZERO_L:
    emitLongZeroMinMaxPseudo(MI, "1111000001zz010ddddeeeeeee", 2);
    return;
  case Bedrock::SMIN_ZERO_Q:
    emitLongZeroMinMaxPseudo(MI, "1111000001zz010ddddeeeeeee", 3);
    return;
  case Bedrock::ADDL3rr:
    emitBinaryPseudo(MI, Bedrock::ADDLrr);
    return;
  case Bedrock::ADDQ3rr:
    emitBinaryPseudo(MI, Bedrock::ADDQrr);
    return;
  case Bedrock::ADDL3ri:
    emitBinaryImmPseudo(MI, "000111zddddeeeeeee", 2);
    return;
  case Bedrock::ADDQ3ri:
    emitBinaryImmPseudo(MI, "000111zddddeeeeeee", 3);
    return;
  case Bedrock::SUBL3rr:
    emitBinaryPseudo(MI, Bedrock::SUBLrr);
    return;
  case Bedrock::SUBQ3rr:
    emitBinaryPseudo(MI, Bedrock::SUBQrr);
    return;
  case Bedrock::SUBL3ri:
    emitBinaryImmPseudo(MI, "001011zddddeeeeeee", 2);
    return;
  case Bedrock::SUBQ3ri:
    emitBinaryImmPseudo(MI, "001011zddddeeeeeee", 3);
    return;
  case Bedrock::ANDL3rr:
    emitBinaryPseudo(MI, Bedrock::ANDLrr);
    return;
  case Bedrock::ANDQ3rr:
    emitBinaryPseudo(MI, Bedrock::ANDQrr);
    return;
  case Bedrock::ANDL3ri:
    emitBinaryImmPseudo(MI, "001111zddddeeeeeee", 2);
    return;
  case Bedrock::ANDQ3ri:
    emitBinaryImmPseudo(MI, "001111zddddeeeeeee", 3);
    return;
  case Bedrock::ORL3rr:
    emitBinaryPseudo(MI, Bedrock::ORLrr);
    return;
  case Bedrock::ORQ3rr:
    emitBinaryPseudo(MI, Bedrock::ORQrr);
    return;
  case Bedrock::ORL3ri:
    emitBinaryImmPseudo(MI, "010011zddddeeeeeee", 2);
    return;
  case Bedrock::ORQ3ri:
    emitBinaryImmPseudo(MI, "010011zddddeeeeeee", 3);
    return;
  case Bedrock::BSETL3ri:
    emitBitImmPseudo(MI, "bset", "1111101110011iiiiiieeeeeee", 2);
    return;
  case Bedrock::BSETQ3ri:
    emitBitImmPseudo(MI, "bset", "1111101110011iiiiiieeeeeee", 3);
    return;
  case Bedrock::BSET2Q3ri:
    emitBSet2ImmPseudo(MI);
    return;
  case Bedrock::BCLRL3ri:
    emitBitImmPseudo(MI, "bclr", "1111101110101iiiiiieeeeeee", 2);
    return;
  case Bedrock::BCLRQ3ri:
    emitBitImmPseudo(MI, "bclr", "1111101110101iiiiiieeeeeee", 3);
    return;
  case Bedrock::BCHGL3ri:
    emitBitImmPseudo(MI, "bchg", "1111101110111iiiiiieeeeeee", 2);
    return;
  case Bedrock::BCHGQ3ri:
    emitBitImmPseudo(MI, "bchg", "1111101110111iiiiiieeeeeee", 3);
    return;
  case Bedrock::XORL3rr:
    emitBinaryPseudo(MI, Bedrock::XORLrr);
    return;
  case Bedrock::XORQ3rr:
    emitBinaryPseudo(MI, Bedrock::XORQrr);
    return;
  case Bedrock::XORL3ri:
    emitBinaryImmPseudo(MI, "010111zddddeeeeeee", 2);
    return;
  case Bedrock::XORQ3ri:
    emitBinaryImmPseudo(MI, "010111zddddeeeeeee", 3);
    return;
  case Bedrock::SHLL3rr:
    emitBinaryPseudo(MI, Bedrock::SHLLrr);
    return;
  case Bedrock::SHLQ3rr:
    emitBinaryPseudo(MI, Bedrock::SHLQrr);
    return;
  case Bedrock::SHLL3ri:
    emitShiftImmPseudo(MI, "1111101101zz0iiiiiieeeeeee", 2);
    return;
  case Bedrock::SHLQ3ri:
    emitShiftImmPseudo(MI, "1111101101zz0iiiiiieeeeeee", 3);
    return;
  case Bedrock::ROLL3rr:
    emitBinaryPseudo(MI, Bedrock::ROLLrr);
    return;
  case Bedrock::ROLQ3rr:
    emitBinaryPseudo(MI, Bedrock::ROLQrr);
    return;
  case Bedrock::ROLB3ri:
    emitShiftImmPseudo(MI, "1111101100zz0iiiiiieeeeeee", 0);
    return;
  case Bedrock::ROLL3ri:
    emitShiftImmPseudo(MI, "1111101100zz0iiiiiieeeeeee", 2);
    return;
  case Bedrock::ROLQ3ri:
    emitShiftImmPseudo(MI, "1111101100zz0iiiiiieeeeeee", 3);
    return;
  case Bedrock::RORL3rr:
    emitBinaryPseudo(MI, Bedrock::RORLrr);
    return;
  case Bedrock::RORQ3rr:
    emitBinaryPseudo(MI, Bedrock::RORQrr);
    return;
  case Bedrock::RORL3ri:
    emitShiftImmPseudo(MI, "1111101100zz1iiiiiieeeeeee", 2);
    return;
  case Bedrock::RORQ3ri:
    emitShiftImmPseudo(MI, "1111101100zz1iiiiiieeeeeee", 3);
    return;
  case Bedrock::MINUL3rr:
    emitLongBinaryPseudo(MI, "1111000001zz000ddddeeeeeee", 2);
    return;
  case Bedrock::MINUL3ri:
    emitLongBinaryImmPseudo(MI, "1111000001zz000ddddeeeeeee", 2);
    return;
  case Bedrock::MINUQ3rr:
    emitLongBinaryPseudo(MI, "1111000001zz000ddddeeeeeee", 3);
    return;
  case Bedrock::MINUQ3ri:
    emitLongBinaryImmPseudo(MI, "1111000001zz000ddddeeeeeee", 3);
    return;
  case Bedrock::MINSL3rr:
    emitLongBinaryPseudo(MI, "1111000001zz010ddddeeeeeee", 2);
    return;
  case Bedrock::MINSL3ri:
    emitLongBinaryImmPseudo(MI, "1111000001zz010ddddeeeeeee", 2);
    return;
  case Bedrock::MINSQ3rr:
    emitLongBinaryPseudo(MI, "1111000001zz010ddddeeeeeee", 3);
    return;
  case Bedrock::MINSQ3ri:
    emitLongBinaryImmPseudo(MI, "1111000001zz010ddddeeeeeee", 3);
    return;
  case Bedrock::MAXUL3rr:
    emitLongBinaryPseudo(MI, "1111000001zz100ddddeeeeeee", 2);
    return;
  case Bedrock::MAXUL3ri:
    emitLongBinaryImmPseudo(MI, "1111000001zz100ddddeeeeeee", 2);
    return;
  case Bedrock::MAXUQ3rr:
    emitLongBinaryPseudo(MI, "1111000001zz100ddddeeeeeee", 3);
    return;
  case Bedrock::MAXUQ3ri:
    emitLongBinaryImmPseudo(MI, "1111000001zz100ddddeeeeeee", 3);
    return;
  case Bedrock::MAXSL3rr:
    emitLongBinaryPseudo(MI, "1111000001zz110ddddeeeeeee", 2);
    return;
  case Bedrock::MAXSL3ri:
    emitLongBinaryImmPseudo(MI, "1111000001zz110ddddeeeeeee", 2);
    return;
  case Bedrock::MAXSQ3rr:
    emitLongBinaryPseudo(MI, "1111000001zz110ddddeeeeeee", 3);
    return;
  case Bedrock::MAXSQ3ri:
    emitLongBinaryImmPseudo(MI, "1111000001zz110ddddeeeeeee", 3);
    return;
  case Bedrock::SHRL3rr:
    emitBinaryPseudo(MI, Bedrock::SHRLrr);
    return;
  case Bedrock::SHRQ3rr:
    emitBinaryPseudo(MI, Bedrock::SHRQrr);
    return;
  case Bedrock::SHRL3ri:
    emitShiftImmPseudo(MI, "1111101101zz1iiiiiieeeeeee", 2);
    return;
  case Bedrock::SHRQ3ri:
    emitShiftImmPseudo(MI, "1111101101zz1iiiiiieeeeeee", 3);
    return;
  case Bedrock::SARL3rr:
    emitBinaryPseudo(MI, Bedrock::SARLrr);
    return;
  case Bedrock::SARQ3rr:
    emitBinaryPseudo(MI, Bedrock::SARQrr);
    return;
  case Bedrock::SARL3ri:
    emitShiftImmPseudo(MI, "1111101110zz0iiiiiieeeeeee", 2);
    return;
  case Bedrock::SARQ3ri:
    emitShiftImmPseudo(MI, "1111101110zz0iiiiiieeeeeee", 3);
    return;
  case Bedrock::MULL3rr:
    emitLongBinaryPseudo(MI, "1111000010zz010ddddeeeeeee", 2);
    return;
  case Bedrock::MULQ3rr:
    emitLongBinaryPseudo(MI, "1111000010zz010ddddeeeeeee", 3);
    return;
  case Bedrock::MULL3ri:
    emitLongBinaryImmPseudo(MI, "1111000010zz010ddddeeeeeee", 2);
    return;
  case Bedrock::MULQ3ri:
    emitLongBinaryImmPseudo(MI, "1111000010zz010ddddeeeeeee", 3);
    return;
  case Bedrock::DIVUL3rr:
    emitLongBinaryPseudo(MI, "1111000010zz100ddddeeeeeee", 2);
    return;
  case Bedrock::DIVUQ3rr:
    emitLongBinaryPseudo(MI, "1111000010zz100ddddeeeeeee", 3);
    return;
  case Bedrock::DIVSL3rr:
    emitLongBinaryPseudo(MI, "1111000010zz101ddddeeeeeee", 2);
    return;
  case Bedrock::DIVSQ3rr:
    emitLongBinaryPseudo(MI, "1111000010zz101ddddeeeeeee", 3);
    return;
  case Bedrock::DIVSL3ri:
    emitLongBinaryImmPseudo(MI, "1111000010zz101ddddeeeeeee", 2);
    return;
  case Bedrock::DIVSQ3ri:
    emitLongBinaryImmPseudo(MI, "1111000010zz101ddddeeeeeee", 3);
    return;
  case Bedrock::MODUL3rr:
    emitLongBinaryPseudo(MI, "1111000010zz110ddddeeeeeee", 2);
    return;
  case Bedrock::MODUQ3rr:
    emitLongBinaryPseudo(MI, "1111000010zz110ddddeeeeeee", 3);
    return;
  case Bedrock::MODSL3rr:
    emitLongBinaryPseudo(MI, "1111000010zz111ddddeeeeeee", 2);
    return;
  case Bedrock::MODSQ3rr:
    emitLongBinaryPseudo(MI, "1111000010zz111ddddeeeeeee", 3);
    return;
#define HANDLE_BINARY_MEM(OP, PATTERN, SIZE, IS_LONG)                         \
  case Bedrock::OP##rm:                                                       \
    emitBinaryMemPseudo(MI, PATTERN, SIZE, IS_LONG, MemAddrKind::Reg);        \
    return;                                                                   \
  case Bedrock::OP##rmx:                                                      \
    emitBinaryMemPseudo(MI, PATTERN, SIZE, IS_LONG, MemAddrKind::RegIndex);   \
    return;                                                                   \
  case Bedrock::OP##rmo:                                                      \
    emitBinaryMemPseudo(MI, PATTERN, SIZE, IS_LONG, MemAddrKind::RegOffset);  \
    return;                                                                   \
  case Bedrock::OP##rmfi:                                                     \
    emitBinaryMemPseudo(MI, PATTERN, SIZE, IS_LONG, MemAddrKind::Frame);      \
    return;
    HANDLE_BINARY_MEM(ADDL3, "000111zddddeeeeeee", 2, false)
    HANDLE_BINARY_MEM(ADDQ3, "000111zddddeeeeeee", 3, false)
    HANDLE_BINARY_MEM(SUBL3, "001011zddddeeeeeee", 2, false)
    HANDLE_BINARY_MEM(SUBQ3, "001011zddddeeeeeee", 3, false)
    HANDLE_BINARY_MEM(ANDL3, "001111zddddeeeeeee", 2, false)
    HANDLE_BINARY_MEM(ANDQ3, "001111zddddeeeeeee", 3, false)
    HANDLE_BINARY_MEM(ORL3, "010011zddddeeeeeee", 2, false)
    HANDLE_BINARY_MEM(ORQ3, "010011zddddeeeeeee", 3, false)
    HANDLE_BINARY_MEM(XORL3, "010111zddddeeeeeee", 2, false)
    HANDLE_BINARY_MEM(XORQ3, "010111zddddeeeeeee", 3, false)
    HANDLE_BINARY_MEM(MULL3, "1111000010zz010ddddeeeeeee", 2, true)
    HANDLE_BINARY_MEM(MULQ3, "1111000010zz010ddddeeeeeee", 3, true)
    HANDLE_BINARY_MEM(MINUL3, "1111000001zz000ddddeeeeeee", 2, true)
    HANDLE_BINARY_MEM(MINUQ3, "1111000001zz000ddddeeeeeee", 3, true)
    HANDLE_BINARY_MEM(MINSL3, "1111000001zz010ddddeeeeeee", 2, true)
    HANDLE_BINARY_MEM(MINSQ3, "1111000001zz010ddddeeeeeee", 3, true)
    HANDLE_BINARY_MEM(MAXUL3, "1111000001zz100ddddeeeeeee", 2, true)
    HANDLE_BINARY_MEM(MAXUQ3, "1111000001zz100ddddeeeeeee", 3, true)
    HANDLE_BINARY_MEM(MAXSL3, "1111000001zz110ddddeeeeeee", 2, true)
    HANDLE_BINARY_MEM(MAXSQ3, "1111000001zz110ddddeeeeeee", 3, true)
    HANDLE_BINARY_MEM(DIVUL3, "1111000010zz100ddddeeeeeee", 2, true)
    HANDLE_BINARY_MEM(DIVUQ3, "1111000010zz100ddddeeeeeee", 3, true)
    HANDLE_BINARY_MEM(DIVSL3, "1111000010zz101ddddeeeeeee", 2, true)
    HANDLE_BINARY_MEM(DIVSQ3, "1111000010zz101ddddeeeeeee", 3, true)
    HANDLE_BINARY_MEM(MODUL3, "1111000010zz110ddddeeeeeee", 2, true)
    HANDLE_BINARY_MEM(MODUQ3, "1111000010zz110ddddeeeeeee", 3, true)
    HANDLE_BINARY_MEM(MODSL3, "1111000010zz111ddddeeeeeee", 2, true)
    HANDLE_BINARY_MEM(MODSQ3, "1111000010zz111ddddeeeeeee", 3, true)
#undef HANDLE_BINARY_MEM
#define HANDLE_BINARY_MEM_DEST(OP, PATTERN, SIZE, IS_LONG)                    \
  case Bedrock::OP##mr:                                                       \
    emitBinaryMemDestPseudo(MI, PATTERN, SIZE, IS_LONG, MemAddrKind::Reg);   \
    return;                                                                   \
  case Bedrock::OP##mro:                                                      \
    emitBinaryMemDestPseudo(MI, PATTERN, SIZE, IS_LONG,                       \
                            MemAddrKind::RegOffset);                          \
    return;                                                                   \
  case Bedrock::OP##mfi:                                                      \
    emitBinaryMemDestPseudo(MI, PATTERN, SIZE, IS_LONG, MemAddrKind::Frame); \
    return;
    HANDLE_BINARY_MEM_DEST(ADDL3, "000101zsssseeeeeee", 2, false)
    HANDLE_BINARY_MEM_DEST(ADDQ3, "000101zsssseeeeeee", 3, false)
    HANDLE_BINARY_MEM_DEST(SUBL3, "001001zsssseeeeeee", 2, false)
    HANDLE_BINARY_MEM_DEST(SUBQ3, "001001zsssseeeeeee", 3, false)
    HANDLE_BINARY_MEM_DEST(ANDL3, "001101zsssseeeeeee", 2, false)
    HANDLE_BINARY_MEM_DEST(ANDQ3, "001101zsssseeeeeee", 3, false)
    HANDLE_BINARY_MEM_DEST(ORL3, "010001zsssseeeeeee", 2, false)
    HANDLE_BINARY_MEM_DEST(ORQ3, "010001zsssseeeeeee", 3, false)
    HANDLE_BINARY_MEM_DEST(XORL3, "010101zsssseeeeeee", 2, false)
    HANDLE_BINARY_MEM_DEST(XORQ3, "010101zsssseeeeeee", 3, false)
    HANDLE_BINARY_MEM_DEST(MINUL3, "1111000001zz001sssseeeeeee", 2, true)
    HANDLE_BINARY_MEM_DEST(MINUQ3, "1111000001zz001sssseeeeeee", 3, true)
    HANDLE_BINARY_MEM_DEST(MINSL3, "1111000001zz011sssseeeeeee", 2, true)
    HANDLE_BINARY_MEM_DEST(MINSQ3, "1111000001zz011sssseeeeeee", 3, true)
    HANDLE_BINARY_MEM_DEST(MAXUL3, "1111000001zz101sssseeeeeee", 2, true)
    HANDLE_BINARY_MEM_DEST(MAXUQ3, "1111000001zz101sssseeeeeee", 3, true)
    HANDLE_BINARY_MEM_DEST(MAXSL3, "1111000001zz111sssseeeeeee", 2, true)
    HANDLE_BINARY_MEM_DEST(MAXSQ3, "1111000001zz111sssseeeeeee", 3, true)
#undef HANDLE_BINARY_MEM_DEST
  case Bedrock::CMPLri:
    emitCmpImmPseudo(MI, 2);
    return;
  case Bedrock::CMPQri:
    emitCmpImmPseudo(MI, 3);
    return;
#define HANDLE_CMP_MEM(OP, SIZE)                                               \
  case Bedrock::OP##mr:                                                        \
    emitCmpMemPseudo(MI, SIZE, MemAddrKind::Reg, /*MemIsSrc=*/true);          \
    return;                                                                    \
  case Bedrock::OP##mxr:                                                       \
    emitCmpMemPseudo(MI, SIZE, MemAddrKind::RegIndex, /*MemIsSrc=*/true);      \
    return;                                                                    \
  case Bedrock::OP##mor:                                                       \
    emitCmpMemPseudo(MI, SIZE, MemAddrKind::RegOffset, /*MemIsSrc=*/true);    \
    return;                                                                    \
  case Bedrock::OP##mabsr:                                                     \
    emitCmpMemPseudo(MI, SIZE, MemAddrKind::Abs, /*MemIsSrc=*/true);          \
    return;                                                                    \
  case Bedrock::OP##mfir:                                                      \
    emitCmpMemPseudo(MI, SIZE, MemAddrKind::Frame, /*MemIsSrc=*/true);        \
    return;                                                                    \
  case Bedrock::OP##rm:                                                        \
    emitCmpMemPseudo(MI, SIZE, MemAddrKind::Reg, /*MemIsSrc=*/false);         \
    return;                                                                    \
  case Bedrock::OP##rmx:                                                       \
    emitCmpMemPseudo(MI, SIZE, MemAddrKind::RegIndex, /*MemIsSrc=*/false);     \
    return;                                                                    \
  case Bedrock::OP##rmo:                                                       \
    emitCmpMemPseudo(MI, SIZE, MemAddrKind::RegOffset, /*MemIsSrc=*/false);   \
    return;                                                                    \
  case Bedrock::OP##rmabs:                                                     \
    emitCmpMemPseudo(MI, SIZE, MemAddrKind::Abs, /*MemIsSrc=*/false);         \
    return;                                                                    \
  case Bedrock::OP##rmfi:                                                      \
    emitCmpMemPseudo(MI, SIZE, MemAddrKind::Frame, /*MemIsSrc=*/false);       \
    return;
    HANDLE_CMP_MEM(CMPL, 2)
    HANDLE_CMP_MEM(CMPQ, 3)
#undef HANDLE_CMP_MEM
  case Bedrock::CMPLmfmf:
    emitCmpFrameFramePseudo(MI, 2);
    return;
  case Bedrock::CMPQmfmf:
    emitCmpFrameFramePseudo(MI, 3);
    return;
  case Bedrock::FMOVDrr:
    emitFpuMove(MI);
    return;
  case Bedrock::BEDROCK_FCLR_S:
  case Bedrock::BEDROCK_FCLR_D:
    emitFpuClear(MI);
    return;
  case Bedrock::BEDROCK_FMOVCR_D:
    emitFpuConstant(MI);
    return;
  case Bedrock::FCMPSrr:
    emitFpuComparePseudo(MI, /*IsDouble=*/false);
    return;
  case Bedrock::FCMPDrr:
    emitFpuComparePseudo(MI, /*IsDouble=*/true);
    return;
  case Bedrock::FTESTSr:
    emitFpuTestPseudo(MI, /*IsDouble=*/false);
    return;
  case Bedrock::FTESTDr:
    emitFpuTestPseudo(MI, /*IsDouble=*/true);
    return;
  case Bedrock::FP_SELECT_CC_S:
    emitFpuSelectPseudo(MI, /*IsDouble=*/false);
    return;
  case Bedrock::FP_SELECT_CC_D:
    emitFpuSelectPseudo(MI, /*IsDouble=*/true);
    return;
  case Bedrock::FP_SELECT_CC_SD:
    emitFpuSelectPseudo(MI, /*IsDouble=*/false);
    return;
  case Bedrock::FP_SELECT_CC_DS:
    emitFpuSelectPseudo(MI, /*IsDouble=*/true);
    return;
  case Bedrock::FP_SELECT_TEST_SS:
  case Bedrock::FP_SELECT_TEST_SD:
    emitFpuSelectTestPseudo(MI, /*IsDouble=*/false);
    return;
  case Bedrock::FP_SELECT_TEST_DS:
  case Bedrock::FP_SELECT_TEST_DD:
    emitFpuSelectTestPseudo(MI, /*IsDouble=*/true);
    return;
  case Bedrock::FADDSrr:
    emitFpuBinaryPseudo(MI, "FADD", "100100zssss111dddd",
                        /*IsDouble=*/false);
    return;
  case Bedrock::FADDDrr:
    emitFpuBinaryPseudo(MI, "FADD", "100100zssss111dddd",
                        /*IsDouble=*/true);
    return;
  case Bedrock::FSUBSrr:
    emitFpuBinaryPseudo(MI, "FSUB", "100101zssss000dddd",
                        /*IsDouble=*/false);
    return;
  case Bedrock::FSUBDrr:
    emitFpuBinaryPseudo(MI, "FSUB", "100101zssss000dddd",
                        /*IsDouble=*/true);
    return;
  case Bedrock::FMULSrr:
    emitFpuBinaryPseudo(MI, "FMUL", "100101zssss001dddd",
                        /*IsDouble=*/false);
    return;
  case Bedrock::FMULDrr:
    emitFpuBinaryPseudo(MI, "FMUL", "100101zssss001dddd",
                        /*IsDouble=*/true);
    return;
  case Bedrock::FDIVSrr:
    emitFpuBinaryPseudo(MI, "FDIV", "100101zssss010dddd",
                        /*IsDouble=*/false);
    return;
  case Bedrock::FDIVDrr:
    emitFpuBinaryPseudo(MI, "FDIV", "100101zssss010dddd",
                        /*IsDouble=*/true);
    return;
  case Bedrock::FMODSrr:
    emitFpuBinaryPseudo(MI, "FMOD", "1111010110z0011dddd000ssss",
                        /*IsDouble=*/false, /*IsLong=*/true);
    return;
  case Bedrock::FMODDrr:
    emitFpuBinaryPseudo(MI, "FMOD", "1111010110z0011dddd000ssss",
                        /*IsDouble=*/true, /*IsLong=*/true);
    return;
  case Bedrock::FSCALESrr:
    emitFpuBinaryPseudo(MI, "FSCALE", "1111010110z0101dddd000ssss",
                        /*IsDouble=*/false, /*IsLong=*/true);
    return;
  case Bedrock::FSCALEDrr:
    emitFpuBinaryPseudo(MI, "FSCALE", "1111010110z0101dddd000ssss",
                        /*IsDouble=*/true, /*IsLong=*/true);
    return;
  case Bedrock::FMINSrr:
    emitFpuBinaryPseudo(MI, "FMIN", "100101zssss110dddd",
                        /*IsDouble=*/false);
    return;
  case Bedrock::FMINDrr:
    emitFpuBinaryPseudo(MI, "FMIN", "100101zssss110dddd",
                        /*IsDouble=*/true);
    return;
  case Bedrock::FMAXSrr:
    emitFpuBinaryPseudo(MI, "FMAX", "100101zssss111dddd",
                        /*IsDouble=*/false);
    return;
  case Bedrock::FMAXDrr:
    emitFpuBinaryPseudo(MI, "FMAX", "100101zssss111dddd",
                        /*IsDouble=*/true);
    return;
  case Bedrock::FCOPYSIGNSrrr:
    emitFpuCopySignPseudo(MI, /*IsDouble=*/false);
    return;
  case Bedrock::FCOPYSIGNDrrr:
    emitFpuCopySignPseudo(MI, /*IsDouble=*/true);
    return;
#define EMIT_FPU_UNARY(NAME, PATTERN)                                       \
  case Bedrock::BEDROCK_##NAME##_S:                                         \
    emitFpuUnaryPseudo(MI, #NAME, PATTERN, /*IsDouble=*/false);             \
    return;                                                                  \
  case Bedrock::BEDROCK_##NAME##_D:                                         \
    emitFpuUnaryPseudo(MI, #NAME, PATTERN, /*IsDouble=*/true);              \
    return;
    EMIT_FPU_UNARY(FABS, "100101zssss011dddd")
    EMIT_FPU_UNARY(FNEG, "100101zssss100dddd")
    EMIT_FPU_UNARY(FSQRT, "100101zssss101dddd")
    EMIT_FPU_UNARY(FROUND, "100001zdddd000ssss")
    EMIT_FPU_UNARY(FTRUNC, "100011zdddd000ssss")
    EMIT_FPU_UNARY(FCEIL, "101001zdddd000ssss")
    EMIT_FPU_UNARY(FFLOOR, "101011zdddd000ssss")
#undef EMIT_FPU_UNARY
  case Bedrock::BEDROCK_FINT_S:
    emitFpuUnaryPseudo(MI, "FINT", "1111010101z1111dddd000ssss",
                       /*IsDouble=*/false, /*IsLong=*/true);
    return;
  case Bedrock::BEDROCK_FINT_D:
    emitFpuUnaryPseudo(MI, "FINT", "1111010101z1111dddd000ssss",
                       /*IsDouble=*/true, /*IsLong=*/true);
    return;
  case Bedrock::BEDROCK_FGETEXP_S:
    emitFpuUnaryPseudo(MI, "FGETEXP", "1111010110z0001dddd000ssss",
                       /*IsDouble=*/false, /*IsLong=*/true);
    return;
  case Bedrock::BEDROCK_FGETEXP_D:
    emitFpuUnaryPseudo(MI, "FGETEXP", "1111010110z0001dddd000ssss",
                       /*IsDouble=*/true, /*IsLong=*/true);
    return;
  case Bedrock::BEDROCK_FGETMAN_S:
    emitFpuUnaryPseudo(MI, "FGETMAN", "1111010110z0010dddd000ssss",
                       /*IsDouble=*/false, /*IsLong=*/true);
    return;
  case Bedrock::BEDROCK_FGETMAN_D:
    emitFpuUnaryPseudo(MI, "FGETMAN", "1111010110z0010dddd000ssss",
                       /*IsDouble=*/true, /*IsLong=*/true);
    return;
#define EMIT_FUSED(NAME, PATTERN)                                            \
  case Bedrock::BEDROCK_##NAME##_S:                                          \
    emitFusedPseudo(MI, #NAME, PATTERN, /*IsDouble=*/false);                 \
    return;                                                                  \
  case Bedrock::BEDROCK_##NAME##_D:                                          \
    emitFusedPseudo(MI, #NAME, PATTERN, /*IsDouble=*/true);                  \
    return;
    EMIT_FUSED(FMADD, "1111010000zllllrrrr100dddd")
    EMIT_FUSED(FMSUB, "1111010000zllllrrrr101dddd")
    EMIT_FUSED(FNMADD, "1111010000zllllrrrr110dddd")
    EMIT_FUSED(FNMSUB, "1111010000zllllrrrr111dddd")
#undef EMIT_FUSED
#define EMIT_APPROX(NAME, PATTERN)                                           \
  case Bedrock::BEDROCK_##NAME##_S:                                          \
    emitApproxUnaryPseudo(MI, #NAME, PATTERN, /*IsDouble=*/false);           \
    return;                                                                  \
  case Bedrock::BEDROCK_##NAME##_D:                                          \
    emitApproxUnaryPseudo(MI, #NAME, PATTERN, /*IsDouble=*/true);            \
    return;
    EMIT_APPROX(FACOSA, "1111011100z0000dddd000ssss")
    EMIT_APPROX(FASINA, "1111011100z0000dddd001ssss")
    EMIT_APPROX(FATANA, "1111011100z0000dddd010ssss")
    EMIT_APPROX(FATANHA, "1111011100z0000dddd011ssss")
    EMIT_APPROX(FCOSA, "1111011100z0000dddd100ssss")
    EMIT_APPROX(FCOSHA, "1111011100z0000dddd101ssss")
    EMIT_APPROX(FETOXA, "1111011100z0000dddd110ssss")
    EMIT_APPROX(FETOXM1A, "1111011100z0000dddd111ssss")
    EMIT_APPROX(FLOG10A, "1111011100z0001dddd000ssss")
    EMIT_APPROX(FLOG2A, "1111011100z0001dddd001ssss")
    EMIT_APPROX(FLOGNA, "1111011100z0001dddd010ssss")
    EMIT_APPROX(FLOGNP1A, "1111011100z0001dddd011ssss")
    EMIT_APPROX(FSINA, "1111011100z0001dddd100ssss")
    EMIT_APPROX(FSINHA, "1111011100z0001dddd110ssss")
    EMIT_APPROX(FTANA, "1111011100z0001dddd111ssss")
    EMIT_APPROX(FTANHA, "1111011100z0010dddd000ssss")
    EMIT_APPROX(FTENTOXA, "1111011100z0010dddd001ssss")
    EMIT_APPROX(FTWOTOXA, "1111011100z0010dddd010ssss")
#undef EMIT_APPROX
  case Bedrock::BEDROCK_FSINCOSA_S:
    emitSincosPseudo(MI, /*IsDouble=*/false);
    return;
  case Bedrock::BEDROCK_FSINCOSA_D:
    emitSincosPseudo(MI, /*IsDouble=*/true);
    return;
  case Bedrock::FCVTSQSrr:
    emitFpuConvert(MI, "FCVT", "1111010111z0101ssss010dddd",
                   /*IsDouble=*/false, /*SrcIsFPR=*/false, /*DstIsFPR=*/true);
    return;
  case Bedrock::FCVTSQDrr:
    emitFpuConvert(MI, "FCVT", "1111010111z0101ssss010dddd",
                   /*IsDouble=*/true, /*SrcIsFPR=*/false, /*DstIsFPR=*/true);
    return;
  case Bedrock::FCVTUQSrr:
    emitFpuConvert(MI, "FCVTU", "1111010111z0110ssss010dddd",
                   /*IsDouble=*/false, /*SrcIsFPR=*/false, /*DstIsFPR=*/true);
    return;
  case Bedrock::FCVTUQDrr:
    emitFpuConvert(MI, "FCVTU", "1111010111z0110ssss010dddd",
                   /*IsDouble=*/true, /*SrcIsFPR=*/false, /*DstIsFPR=*/true);
    return;
  case Bedrock::FCVTStoQrr:
    emitFpuConvert(MI, "FCVT", "1111010111z0011ssss010dddd",
                   /*IsDouble=*/false, /*SrcIsFPR=*/true, /*DstIsFPR=*/false);
    return;
  case Bedrock::FCVTDtoQrr:
    emitFpuConvert(MI, "FCVT", "1111010111z0011ssss010dddd",
                   /*IsDouble=*/true, /*SrcIsFPR=*/true, /*DstIsFPR=*/false);
    return;
  case Bedrock::FCVTUStoQrr:
    emitFpuConvert(MI, "FCVTU", "1111010111z0100ssss010dddd",
                   /*IsDouble=*/false, /*SrcIsFPR=*/true, /*DstIsFPR=*/false);
    return;
  case Bedrock::FCVTUDtoQrr:
    emitFpuConvert(MI, "FCVTU", "1111010111z0100ssss010dddd",
                   /*IsDouble=*/true, /*SrcIsFPR=*/true, /*DstIsFPR=*/false);
    return;
  case Bedrock::FCVTDtoSrr:
    emitFpuConvert(MI, "FCVT", "1111010111z0001ssss010dddd",
                   /*IsDouble=*/false, /*SrcIsFPR=*/true, /*DstIsFPR=*/true);
    return;
  case Bedrock::FCVTStoDrr:
    emitFpuConvert(MI, "FCVT", "1111010111z0001ssss010dddd",
                   /*IsDouble=*/true, /*SrcIsFPR=*/true, /*DstIsFPR=*/true);
    return;
  case Bedrock::LEAfi:
    emitFrameAddress(MI);
    return;
  case Bedrock::LEAro:
    emitRegOffsetAddress(MI);
    return;
  case Bedrock::LEArx:
    emitScaledIndexAddress(MI);
    return;
  case Bedrock::MOVLmmrr:
    emitMemMoveRegRegPseudo(MI, 2);
    return;
  case Bedrock::MOVQmmrr:
    emitMemMoveRegRegPseudo(MI, 3);
    return;
  case Bedrock::LOADB_Zrr:
    emitLoad(MI, 0, /*IsFrame=*/false, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADW_Zrr:
    emitLoad(MI, 1, /*IsFrame=*/false, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADL_Zrr:
    emitLoad(MI, 2, /*IsFrame=*/false, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADB_Srr:
    emitLoad(MI, 0, /*IsFrame=*/false, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADW_Srr:
    emitLoad(MI, 1, /*IsFrame=*/false, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADL_Srr:
    emitLoad(MI, 2, /*IsFrame=*/false, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADLrr:
    emitLoad(MI, 2, /*IsFrame=*/false);
    return;
  case Bedrock::LOADQrr:
    emitLoad(MI, 3, /*IsFrame=*/false);
    return;
  case Bedrock::FLOADSrr:
    emitFpuLoad(MI, /*IsFrame=*/false, /*IsDouble=*/false);
    return;
  case Bedrock::FLOADDrr:
    emitFpuLoad(MI, /*IsFrame=*/false, /*IsDouble=*/true);
    return;
  case Bedrock::LOADB_Zrx:
    emitLoadIndex(MI, 0, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADW_Zrx:
    emitLoadIndex(MI, 1, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADL_Zrx:
    emitLoadIndex(MI, 2, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADB_Srx:
    emitLoadIndex(MI, 0, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADW_Srx:
    emitLoadIndex(MI, 1, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADL_Srx:
    emitLoadIndex(MI, 2, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADLrx:
    emitLoadIndex(MI, 2);
    return;
  case Bedrock::LOADQrx:
    emitLoadIndex(MI, 3);
    return;
  case Bedrock::LOADB_Zspx:
    emitLoadSPIndex(MI, 0, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADW_Zspx:
    emitLoadSPIndex(MI, 1, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADL_Zspx:
    emitLoadSPIndex(MI, 2, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADB_Sspx:
    emitLoadSPIndex(MI, 0, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADW_Sspx:
    emitLoadSPIndex(MI, 1, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADL_Sspx:
    emitLoadSPIndex(MI, 2, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADLspx:
    emitLoadSPIndex(MI, 2);
    return;
  case Bedrock::LOADQspx:
    emitLoadSPIndex(MI, 3);
    return;
  case Bedrock::LOADB_Zro:
    emitLoadOffset(MI, 0, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADW_Zro:
    emitLoadOffset(MI, 1, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADL_Zro:
    emitLoadOffset(MI, 2, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADB_Sro:
    emitLoadOffset(MI, 0, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADW_Sro:
    emitLoadOffset(MI, 1, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADL_Sro:
    emitLoadOffset(MI, 2, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADLro:
    emitLoadOffset(MI, 2);
    return;
  case Bedrock::LOADQro:
    emitLoadOffset(MI, 3);
    return;
  case Bedrock::FLOADSro:
    emitFpuLoadOffset(MI, /*IsDouble=*/false);
    return;
  case Bedrock::FLOADDro:
    emitFpuLoadOffset(MI, /*IsDouble=*/true);
    return;
  case Bedrock::LOADB_Zabs:
    emitAbsLoad(MI, 0, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADW_Zabs:
    emitAbsLoad(MI, 1, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADL_Zabs:
    emitAbsLoad(MI, 2, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADB_Sabs:
    emitAbsLoad(MI, 0, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADW_Sabs:
    emitAbsLoad(MI, 1, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADL_Sabs:
    emitAbsLoad(MI, 2, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADLabs:
    emitAbsLoad(MI, 2);
    return;
  case Bedrock::LOADQabs:
    emitAbsLoad(MI, 3);
    return;
  case Bedrock::FLOADSabs:
    emitFpuAbsLoad(MI, /*IsDouble=*/false);
    return;
  case Bedrock::FLOADDabs:
    emitFpuAbsLoad(MI, /*IsDouble=*/true);
    return;
  case Bedrock::LOADB_Zfi:
    emitLoad(MI, 0, /*IsFrame=*/true, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADW_Zfi:
    emitLoad(MI, 1, /*IsFrame=*/true, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADL_Zfi:
    emitLoad(MI, 2, /*IsFrame=*/true, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADB_Sfi:
    emitLoad(MI, 0, /*IsFrame=*/true, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADW_Sfi:
    emitLoad(MI, 1, /*IsFrame=*/true, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADL_Sfi:
    emitLoad(MI, 2, /*IsFrame=*/true, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADLfi:
    emitLoad(MI, 2, /*IsFrame=*/true);
    return;
  case Bedrock::LOADQfi:
    emitLoad(MI, 3, /*IsFrame=*/true);
    return;
  case Bedrock::FLOADSfi:
    emitFpuLoad(MI, /*IsFrame=*/true, /*IsDouble=*/false);
    return;
  case Bedrock::FLOADDfi:
    emitFpuLoad(MI, /*IsFrame=*/true, /*IsDouble=*/true);
    return;
  case Bedrock::STOREBrr:
    emitStore(MI, 0, /*IsFrame=*/false);
    return;
  case Bedrock::STOREWrr:
    emitStore(MI, 1, /*IsFrame=*/false);
    return;
  case Bedrock::STORELrr:
    emitStore(MI, 2, /*IsFrame=*/false);
    return;
  case Bedrock::STOREQrr:
    emitStore(MI, 3, /*IsFrame=*/false);
    return;
  case Bedrock::STOREBrx:
    emitStoreIndex(MI, 0);
    return;
  case Bedrock::STOREWrx:
    emitStoreIndex(MI, 1);
    return;
  case Bedrock::STORELrx:
    emitStoreIndex(MI, 2);
    return;
  case Bedrock::STOREQrx:
    emitStoreIndex(MI, 3);
    return;
  case Bedrock::STOREBspx:
    emitStoreSPIndex(MI, 0);
    return;
  case Bedrock::STOREWspx:
    emitStoreSPIndex(MI, 1);
    return;
  case Bedrock::STORELspx:
    emitStoreSPIndex(MI, 2);
    return;
  case Bedrock::STOREQspx:
    emitStoreSPIndex(MI, 3);
    return;
  case Bedrock::FSTORESrr:
    emitFpuStore(MI, /*IsFrame=*/false, /*IsDouble=*/false);
    return;
  case Bedrock::FSTOREDrr:
    emitFpuStore(MI, /*IsFrame=*/false, /*IsDouble=*/true);
    return;
  case Bedrock::STOREB_Immrr:
    emitImmStore(MI, 0, /*IsFrame=*/false);
    return;
  case Bedrock::STOREW_Immrr:
    emitImmStore(MI, 1, /*IsFrame=*/false);
    return;
  case Bedrock::STOREL_Immrr:
    emitImmStore(MI, 2, /*IsFrame=*/false);
    return;
  case Bedrock::STOREQ_Immrr:
    emitImmStore(MI, 3, /*IsFrame=*/false);
    return;
  case Bedrock::STOREBro:
    emitStoreOffset(MI, 0);
    return;
  case Bedrock::STOREWro:
    emitStoreOffset(MI, 1);
    return;
  case Bedrock::STORELro:
    emitStoreOffset(MI, 2);
    return;
  case Bedrock::STOREQro:
    emitStoreOffset(MI, 3);
    return;
  case Bedrock::FSTORESro:
    emitFpuStoreOffset(MI, /*IsDouble=*/false);
    return;
  case Bedrock::FSTOREDro:
    emitFpuStoreOffset(MI, /*IsDouble=*/true);
    return;
  case Bedrock::STOREB_Immro:
    emitImmStoreOffset(MI, 0);
    return;
  case Bedrock::STOREW_Immro:
    emitImmStoreOffset(MI, 1);
    return;
  case Bedrock::STOREL_Immro:
    emitImmStoreOffset(MI, 2);
    return;
  case Bedrock::STOREQ_Immro:
    emitImmStoreOffset(MI, 3);
    return;
  case Bedrock::STOREBabs:
    emitAbsStore(MI, 0);
    return;
  case Bedrock::STOREWabs:
    emitAbsStore(MI, 1);
    return;
  case Bedrock::STORELabs:
    emitAbsStore(MI, 2);
    return;
  case Bedrock::STOREQabs:
    emitAbsStore(MI, 3);
    return;
  case Bedrock::FSTORESabs:
    emitFpuAbsStore(MI, /*IsDouble=*/false);
    return;
  case Bedrock::FSTOREDabs:
    emitFpuAbsStore(MI, /*IsDouble=*/true);
    return;
  case Bedrock::STOREB_Immabs:
    emitImmStoreAbs(MI, 0);
    return;
  case Bedrock::STOREW_Immabs:
    emitImmStoreAbs(MI, 1);
    return;
  case Bedrock::STOREL_Immabs:
    emitImmStoreAbs(MI, 2);
    return;
  case Bedrock::STOREQ_Immabs:
    emitImmStoreAbs(MI, 3);
    return;
  case Bedrock::STOREBfi:
    emitStore(MI, 0, /*IsFrame=*/true);
    return;
  case Bedrock::STOREWfi:
    emitStore(MI, 1, /*IsFrame=*/true);
    return;
  case Bedrock::STORELfi:
    emitStore(MI, 2, /*IsFrame=*/true);
    return;
  case Bedrock::STOREQfi:
    emitStore(MI, 3, /*IsFrame=*/true);
    return;
  case Bedrock::FSTORESfi:
    emitFpuStore(MI, /*IsFrame=*/true, /*IsDouble=*/false);
    return;
  case Bedrock::FSTOREDfi:
    emitFpuStore(MI, /*IsFrame=*/true, /*IsDouble=*/true);
    return;
  case Bedrock::STOREB_Immfi:
    emitImmStore(MI, 0, /*IsFrame=*/true);
    return;
  case Bedrock::STOREW_Immfi:
    emitImmStore(MI, 1, /*IsFrame=*/true);
    return;
  case Bedrock::STOREL_Immfi:
    emitImmStore(MI, 2, /*IsFrame=*/true);
    return;
  case Bedrock::STOREQ_Immfi:
    emitImmStore(MI, 3, /*IsFrame=*/true);
    return;
  case Bedrock::ADJSP_DOWN:
    emitStackAdjust(MI, /*IsDown=*/true);
    return;
  case Bedrock::ADJSP_UP:
    emitStackAdjust(MI, /*IsDown=*/false);
    return;
  case Bedrock::BR:
    emitBranch(MI, /*IsCond=*/false);
    return;
  case Bedrock::BRCC:
    if (IJBranches.contains(MI)) {
      emitIJ(MI);
      return;
    }
    if (DJBranches.contains(MI)) {
      emitDJ(MI);
      return;
    }
    if (CmpTestJumpBranches.contains(MI)) {
      emitCmpTestJump(MI);
      return;
    }
    emitBranch(MI, /*IsCond=*/true);
    return;
  case Bedrock::BRIND:
    emitIndirectJump(MI);
    return;
  case Bedrock::SETCC:
    emitSetCC(MI);
    return;
  case Bedrock::CALL:
  case Bedrock::CALL_TAIL:
    emitCall(MI);
    return;
  case Bedrock::TAILCALL:
    emitTailCall(MI);
    return;
  case Bedrock::CALLr:
  case Bedrock::CALLr_TAIL:
    emitIndirectCall(MI);
    return;
  case Bedrock::TLSDESC_CALL:
    emitTLSDescCall(MI);
    return;
  case Bedrock::BEDROCK_CPUID:
  case Bedrock::BEDROCK_RDSTATUS:
  case Bedrock::BEDROCK_RDPMC:
  case Bedrock::BEDROCK_TRACE:
  case Bedrock::BEDROCK_RDFSTATUS:
  case Bedrock::BEDROCK_WRFSTATUS:
  case Bedrock::BEDROCK_RDFFLAGS:
  case Bedrock::BEDROCK_WRFFLAGS:
  case Bedrock::BEDROCK_CLMUL_B:
  case Bedrock::BEDROCK_CLMUL_W:
  case Bedrock::BEDROCK_CLMUL_L:
  case Bedrock::BEDROCK_CLMUL_Q:
  case Bedrock::BEDROCK_MOVNT_B:
  case Bedrock::BEDROCK_MOVNT_W:
  case Bedrock::BEDROCK_MOVNT_L:
  case Bedrock::BEDROCK_MOVNT_Q:
  case Bedrock::BEDROCK_FCLASS_S:
  case Bedrock::BEDROCK_FCLASS_D:
    emitPublicIntrinsic(MI);
    return;
  case Bedrock::BEDROCK_WRSTATUS:
  case Bedrock::BEDROCK_RDCR:
  case Bedrock::BEDROCK_WRCR:
  case Bedrock::BEDROCK_RDSEG:
  case Bedrock::BEDROCK_RDSEG_CS:
  case Bedrock::BEDROCK_WRSEG:
  case Bedrock::BEDROCK_FLSHDCACHE:
  case Bedrock::BEDROCK_INVDCACHE:
  case Bedrock::BEDROCK_INVICACHE:
  case Bedrock::BEDROCK_WRBKDCACHE:
  case Bedrock::BEDROCK_SYNCCACHE:
  case Bedrock::BEDROCK_INVTLB:
  case Bedrock::BEDROCK_INVPAGE:
  case Bedrock::BEDROCK_INVASID:
  case Bedrock::BEDROCK_SWPT:
  case Bedrock::BEDROCK_SWPTA:
  case Bedrock::BEDROCK_VTOP:
  case Bedrock::BEDROCK_PTQUERY:
  case Bedrock::BEDROCK_SAVE:
  case Bedrock::BEDROCK_RESTORE:
    emitSystemIntrinsic(MI);
    return;
  }

  Bedrock_MC::verifyInstructionPredicates(MI->getOpcode(),
                                          getSubtargetInfo().getFeatureBits());

  MCInst Inst;
  Inst.setOpcode(MI->getOpcode());
  for (const MachineOperand &MO : MI->operands()) {
    MCOperand MCOp = lowerOperand(MO);
    if (MCOp.isValid())
      Inst.addOperand(MCOp);
  }
  emitMCInst(Inst);
}

char BedrockAsmPrinter::ID = 0;

INITIALIZE_PASS(BedrockAsmPrinter, "bedrock-asm-printer",
                "Bedrock Assembly Printer", false, false)

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeBedrockAsmPrinter() {
  RegisterAsmPrinter<BedrockAsmPrinter> X(getTheBedrockTarget());
}
