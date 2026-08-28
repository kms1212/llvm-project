//===-- BedrockAsmParser.cpp - Bedrock assembly parser --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/BedrockFixupKinds.h"
#include "MCTargetDesc/BedrockMCEncoding.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "TargetInfo/BedrockTargetInfo.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <string>

using namespace llvm;

#define DEBUG_TYPE "bedrock-asm-parser"

static MCRegister MatchRegisterName(StringRef Name);

namespace {

class BedrockAsmParser : public MCTargetAsmParser {
  MCAsmParser &Parser;
#define GET_ASSEMBLER_HEADER
#include "BedrockGenAsmMatcher.inc"

  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;

  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;
  bool isLabel(AsmToken &Token) override;
  bool parseLengthAnnotation(OperandVector &Operands);

  bool parseOperand(OperandVector &Operands);
  bool parseMemoryOperand(OperandVector &Operands);
  bool parseVectorStepMemoryOperand(OperandVector &Operands);
  bool parseRegisterMaskOperand(OperandVector &Operands);

  MCAsmParser &getParser() const { return Parser; }
  AsmLexer &getLexer() const { return Parser.getLexer(); }

public:
  BedrockAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                   const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII), Parser(Parser) {
    MCAsmParserExtension::Initialize(Parser);
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};

class BedrockOperand : public MCParsedAsmOperand {
public:
  enum MemBaseKind { MemReg, MemSP, MemPC, MemAbs, MemZero };
  enum MemUpdateKind { MemNoUpdate, MemPostInc, MemPreDec };
  enum ImmVariantKind {
    ImmNoVariant,
    ImmAbs64,
    ImmPCRel32,
    ImmFP64Literal,
    ImmFP32Bits,
    ImmFP64Bits
  };

private:
  enum KindTy { TokenKind, RegKind, ImmKind, MemKind } Kind;

  std::string Tok;
  MCRegister Register = Bedrock::NoRegister;
  const MCExpr *Imm = nullptr;
  ImmVariantKind ImmVariant = ImmNoVariant;
  MemBaseKind BaseKind = MemAbs;
  unsigned BaseReg = 0;
  const MCExpr *Disp = nullptr;
  bool HasDisp = false;
  MemUpdateKind BaseUpdate = MemNoUpdate;
  bool HasSegment = false;
  unsigned Segment = 0;
  bool HasIndex = false;
  unsigned IndexReg = 0;
  MemUpdateKind IndexUpdate = MemNoUpdate;
  bool VectorStride = false;
  SMLoc Start;
  SMLoc End;

public:
  BedrockOperand(StringRef Tok, SMLoc Start)
      : Kind(TokenKind), Tok(Tok), Start(Start), End(Start) {}

  BedrockOperand(MCRegister Register, SMLoc Start, SMLoc End)
      : Kind(RegKind), Register(Register), Start(Start), End(End) {}

  BedrockOperand(const MCExpr *Imm, SMLoc Start, SMLoc End,
                 ImmVariantKind ImmVariant = ImmNoVariant)
      : Kind(ImmKind), Imm(Imm), ImmVariant(ImmVariant), Start(Start),
        End(End) {}

  BedrockOperand(MemBaseKind BaseKind, unsigned BaseReg, const MCExpr *Disp,
                 bool HasDisp, SMLoc Start, SMLoc End,
                 MemUpdateKind BaseUpdate = MemNoUpdate,
                 bool HasSegment = false, unsigned Segment = 0,
                 bool HasIndex = false, unsigned IndexReg = 0,
                 MemUpdateKind IndexUpdate = MemNoUpdate,
                 bool VectorStride = false)
      : Kind(MemKind), BaseKind(BaseKind), BaseReg(BaseReg), Disp(Disp),
        HasDisp(HasDisp), BaseUpdate(BaseUpdate), HasSegment(HasSegment),
        Segment(Segment), HasIndex(HasIndex), IndexReg(IndexReg),
        IndexUpdate(IndexUpdate), VectorStride(VectorStride), Start(Start),
        End(End) {}

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(Kind == RegKind && "unexpected operand kind");
    assert(N == 1 && "invalid operand count");
    Inst.addOperand(MCOperand::createReg(Register));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(Kind == ImmKind && "unexpected operand kind");
    assert(N == 1 && "invalid operand count");

    if (const auto *Value = dyn_cast<MCConstantExpr>(Imm)) {
      Inst.addOperand(MCOperand::createImm(Value->getValue()));
      return;
    }

    Inst.addOperand(MCOperand::createExpr(Imm));
  }

  bool isToken() const override { return Kind == TokenKind; }
  bool isReg() const override { return Kind == RegKind; }
  bool isImm() const override { return Kind == ImmKind; }
  bool isMem() const override { return Kind == MemKind; }

  template <unsigned Width> bool isSImm() const {
    if (!isImm())
      return false;
    int64_t Value;
    return Imm->evaluateAsAbsolute(Value) && isIntN(Width, Value);
  }

  template <unsigned Width> bool isUImm() const {
    if (!isImm())
      return false;
    int64_t Value;
    return Imm->evaluateAsAbsolute(Value) && isUIntN(Width, Value);
  }

  StringRef getToken() const {
    assert(Kind == TokenKind && "invalid access");
    return Tok;
  }

  MCRegister getReg() const override {
    assert(Kind == RegKind && "invalid access");
    return Register;
  }

  const MCExpr *getImm() const {
    assert(Kind == ImmKind && "invalid access");
    return Imm;
  }

  ImmVariantKind getImmVariant() const {
    assert(Kind == ImmKind && "invalid access");
    return ImmVariant;
  }

  MemBaseKind getMemBaseKind() const {
    assert(Kind == MemKind && "invalid access");
    return BaseKind;
  }

  unsigned getMemBaseReg() const {
    assert(Kind == MemKind && "invalid access");
    return BaseReg;
  }

  const MCExpr *getMemDisp() const {
    assert(Kind == MemKind && "invalid access");
    return Disp;
  }

  bool hasMemDisp() const {
    assert(Kind == MemKind && "invalid access");
    return HasDisp;
  }

  MemUpdateKind getMemBaseUpdate() const {
    assert(Kind == MemKind && "invalid access");
    return BaseUpdate;
  }

  bool hasMemSegment() const {
    assert(Kind == MemKind && "invalid access");
    return HasSegment;
  }

  unsigned getMemSegment() const {
    assert(Kind == MemKind && "invalid access");
    return Segment;
  }

  bool hasMemIndex() const {
    assert(Kind == MemKind && "invalid access");
    return HasIndex;
  }

  unsigned getMemIndexReg() const {
    assert(Kind == MemKind && "invalid access");
    return IndexReg;
  }

  MemUpdateKind getMemIndexUpdate() const {
    assert(Kind == MemKind && "invalid access");
    return IndexUpdate;
  }

  bool isVectorStride() const {
    assert(Kind == MemKind && "invalid access");
    return VectorStride;
  }

  SMLoc getStartLoc() const override { return Start; }
  SMLoc getEndLoc() const override { return End; }

  void print(raw_ostream &OS, const MCAsmInfo &MAI) const override {
    switch (Kind) {
    case TokenKind:
      OS << "Token " << Tok;
      break;
    case RegKind:
      OS << "Register " << Register.id();
      break;
    case ImmKind:
      OS << "Immediate";
      break;
    case MemKind:
      OS << "Memory";
      break;
    }
  }

  static std::unique_ptr<BedrockOperand> createToken(StringRef Tok,
                                                     SMLoc Start) {
    return std::make_unique<BedrockOperand>(Tok, Start);
  }

  static std::unique_ptr<BedrockOperand> createReg(MCRegister Reg, SMLoc Start,
                                                   SMLoc End) {
    return std::make_unique<BedrockOperand>(Reg, Start, End);
  }

  static std::unique_ptr<BedrockOperand>
  createImm(const MCExpr *Imm, SMLoc Start, SMLoc End,
            ImmVariantKind ImmVariant = ImmNoVariant) {
    return std::make_unique<BedrockOperand>(Imm, Start, End, ImmVariant);
  }

  static std::unique_ptr<BedrockOperand>
  createMem(MemBaseKind BaseKind, unsigned BaseReg, const MCExpr *Disp,
            bool HasDisp, SMLoc Start, SMLoc End,
            MemUpdateKind BaseUpdate = MemNoUpdate, bool HasSegment = false,
            unsigned Segment = 0, bool HasIndex = false, unsigned IndexReg = 0,
            MemUpdateKind IndexUpdate = MemNoUpdate,
            bool VectorStride = false) {
    return std::make_unique<BedrockOperand>(
        BaseKind, BaseReg, Disp, HasDisp, Start, End, BaseUpdate, HasSegment,
        Segment, HasIndex, IndexReg, IndexUpdate, VectorStride);
  }
};

MCRegister getGPRByNo(unsigned RegNo) {
  static const MCRegister Regs[] = {
      Bedrock::R0,  Bedrock::R1,  Bedrock::R2,  Bedrock::R3,
      Bedrock::R4,  Bedrock::R5,  Bedrock::R6,  Bedrock::R7,
      Bedrock::R8,  Bedrock::R9,  Bedrock::R10, Bedrock::R11,
      Bedrock::R12, Bedrock::R13, Bedrock::R14, Bedrock::R15,
  };

  if (RegNo >= std::size(Regs))
    return Bedrock::NoRegister;
  return Regs[RegNo];
}

MCRegister getFPRByNo(unsigned RegNo) {
  static const MCRegister Regs[] = {
      Bedrock::F0,  Bedrock::F1,  Bedrock::F2,  Bedrock::F3,
      Bedrock::F4,  Bedrock::F5,  Bedrock::F6,  Bedrock::F7,
      Bedrock::F8,  Bedrock::F9,  Bedrock::F10, Bedrock::F11,
      Bedrock::F12, Bedrock::F13, Bedrock::F14, Bedrock::F15,
  };

  if (RegNo >= std::size(Regs))
    return Bedrock::NoRegister;
  return Regs[RegNo];
}

MCRegister getVRByNo(unsigned RegNo) {
  static const MCRegister Regs[] = {
      Bedrock::V0,  Bedrock::V1,  Bedrock::V2,  Bedrock::V3,  Bedrock::V4,
      Bedrock::V5,  Bedrock::V6,  Bedrock::V7,  Bedrock::V8,  Bedrock::V9,
      Bedrock::V10, Bedrock::V11, Bedrock::V12, Bedrock::V13, Bedrock::V14,
      Bedrock::V15, Bedrock::V16, Bedrock::V17, Bedrock::V18, Bedrock::V19,
      Bedrock::V20, Bedrock::V21, Bedrock::V22, Bedrock::V23, Bedrock::V24,
      Bedrock::V25, Bedrock::V26, Bedrock::V27, Bedrock::V28, Bedrock::V29,
      Bedrock::V30, Bedrock::V31,
  };
  return RegNo < std::size(Regs) ? Regs[RegNo] : Bedrock::NoRegister;
}

MCRegister getPRByNo(unsigned RegNo) {
  static const MCRegister Regs[] = {
      Bedrock::P0,  Bedrock::P1,  Bedrock::P2,  Bedrock::P3,
      Bedrock::P4,  Bedrock::P5,  Bedrock::P6,  Bedrock::P7,
      Bedrock::P8,  Bedrock::P9,  Bedrock::P10, Bedrock::P11,
      Bedrock::P12, Bedrock::P13, Bedrock::P14, Bedrock::P15,
  };
  return RegNo < std::size(Regs) ? Regs[RegNo] : Bedrock::NoRegister;
}

bool getRegNo(MCRegister Reg, unsigned &RegNo) {
  for (unsigned I = 0; I != 16; ++I) {
    if (Reg == getGPRByNo(I)) {
      RegNo = I;
      return true;
    }
  }
  return false;
}

bool parseRegisterAlias(StringRef Name, unsigned &RegNo) {
  if (Name.size() < 2)
    return false;

  char Prefix = Name.front();
  if (Prefix != 'd' && Prefix != 'a')
    return false;

  unsigned long long AliasNo;
  StringRef Tail = Name.drop_front();
  if (Tail.consumeInteger(10, AliasNo) || !Tail.empty() || AliasNo >= 8)
    return false;

  RegNo = Prefix == 'd' ? AliasNo : AliasNo + 8;
  return true;
}

bool parseNumberedRegister(StringRef Name, char Prefix, unsigned Limit,
                           unsigned &RegNo) {
  if (Name.size() < 2 || Name.front() != Prefix)
    return false;

  unsigned long long ParsedNo;
  StringRef Tail = Name.drop_front();
  if (Tail.consumeInteger(10, ParsedNo) || !Tail.empty() || ParsedNo >= Limit)
    return false;

  RegNo = ParsedNo;
  return true;
}

MCRegister matchBedrockRegisterName(StringRef Name) {
  unsigned RegNo;
  if (parseRegisterAlias(Name, RegNo))
    return getGPRByNo(RegNo);
  if (parseNumberedRegister(Name, 'r', 16, RegNo))
    return getGPRByNo(RegNo);
  if (parseNumberedRegister(Name, 'f', 16, RegNo))
    return getFPRByNo(RegNo);
  if (parseNumberedRegister(Name, 'v', 32, RegNo))
    return getVRByNo(RegNo);
  if (parseNumberedRegister(Name, 'p', 16, RegNo))
    return getPRByNo(RegNo);
  return ::MatchRegisterName(Name);
}

bool getConstantImm(const BedrockOperand &Op, int64_t &Value) {
  return Op.isImm() && Op.getImmVariant() < BedrockOperand::ImmFP64Literal &&
         Op.getImm()->evaluateAsAbsolute(Value);
}

bool getFloatLiteral(const BedrockOperand &Op, uint64_t &Bits) {
  if (!Op.isImm() || Op.getImmVariant() != BedrockOperand::ImmFP64Literal)
    return false;
  int64_t StoredBits;
  if (!Op.getImm()->evaluateAsAbsolute(StoredBits))
    return false;
  Bits = static_cast<uint64_t>(StoredBits);
  return true;
}

const MCExpr *getImmExpr(const BedrockOperand &Op) {
  return Op.isImm() ? Op.getImm() : nullptr;
}

bool isToken(const BedrockOperand &Op, StringRef Token) {
  return Op.isToken() && Op.getToken() == Token;
}

bool isStackPointer(const BedrockOperand &Op) {
  return isToken(Op, "sp") || (Op.isReg() && Op.getReg() == Bedrock::SP);
}

bool getAtomicOrderNo(StringRef Name, unsigned &OrderNo) {
  int Value = StringSwitch<int>(Name.lower())
                  .Case("relaxed", 0)
                  .Case("acquire", 1)
                  .Case("acq", 1)
                  .Case("release", 2)
                  .Case("rel", 2)
                  .Case("acqrel", 3)
                  .Case("acq_rel", 3)
                  .Case("seqcst", 4)
                  .Case("seq_cst", 4)
                  .Default(-1);
  if (Value < 0)
    return false;
  OrderNo = Value;
  return true;
}

bool isAtomicOrderMnemonic(StringRef Mnemonic) {
  StringRef Base = Mnemonic.split('.').first;
  return StringSwitch<bool>(Base)
      .Cases({"cmpxchg", "fetchadd", "fetchand", "fetchor"}, true)
      .Cases({"fetchsub", "fetchxor"}, true)
      .Default(false);
}

bool getSRegNo(StringRef Name, unsigned &RegNo) {
  int Value = StringSwitch<int>(Name)
                  .Case("ds", 0)
                  .Case("ss", 1)
                  .Case("gs0", 2)
                  .Case("gs1", 3)
                  .Case("gs2", 4)
                  .Case("gs3", 5)
                  .Case("gs4", 6)
                  .Case("gs5", 7)
                  .Default(-1);
  if (Value < 0)
    return false;
  RegNo = Value;
  return true;
}

bool getSRegNo(const BedrockOperand &Op, unsigned &RegNo) {
  if (Op.isToken())
    return getSRegNo(Op.getToken(), RegNo);
  if (!Op.isReg())
    return false;
  switch (Op.getReg()) {
  case Bedrock::DS:
    RegNo = 0;
    return true;
  case Bedrock::SS:
    RegNo = 1;
    return true;
  case Bedrock::GS0:
    RegNo = 2;
    return true;
  case Bedrock::GS1:
    RegNo = 3;
    return true;
  case Bedrock::GS2:
    RegNo = 4;
    return true;
  case Bedrock::GS3:
    RegNo = 5;
    return true;
  case Bedrock::GS4:
    RegNo = 6;
    return true;
  case Bedrock::GS5:
    RegNo = 7;
    return true;
  default:
    return false;
  }
}

bool isCS(const BedrockOperand &Op) {
  return (Op.isToken() && Op.getToken() == "cs") ||
         (Op.isReg() && Op.getReg() == Bedrock::CS);
}

bool getFRegNo(MCRegister Reg, unsigned &RegNo) {
  for (unsigned I = 0; I != 16; ++I) {
    if (Reg == getFPRByNo(I)) {
      RegNo = I;
      return true;
    }
  }
  return false;
}

bool getVRegNo(MCRegister Reg, unsigned &RegNo) {
  for (unsigned I = 0; I != 32; ++I) {
    if (Reg == getVRByNo(I)) {
      RegNo = I;
      return true;
    }
  }
  return false;
}

bool getPRegNo(MCRegister Reg, unsigned &RegNo) {
  for (unsigned I = 0; I != 16; ++I) {
    if (Reg == getPRByNo(I)) {
      RegNo = I;
      return true;
    }
  }
  return false;
}

struct RawFixup {
  unsigned Offset;
  MCFixupKind Kind;
  const MCExpr *Expr;
};

void appendLE(SmallVectorImpl<uint8_t> &Bytes, uint64_t Value, unsigned Width) {
  for (unsigned I = 0; I != Width; ++I)
    Bytes.push_back((Value >> (I * 8)) & 0xff);
}

MCFixupKind getDataFixupKind(unsigned Width) {
  switch (Width) {
  case 1:
    return FK_Data_1;
  case 2:
    return FK_Data_2;
  case 4:
    return FK_Data_4;
  case 8:
    return FK_Data_8;
  default:
    llvm_unreachable("invalid Bedrock fixup width");
  }
}

bool appendExprTail(const MCExpr *Expr, unsigned Width,
                    SmallVectorImpl<uint8_t> &Tail,
                    SmallVectorImpl<RawFixup> *Fixups, MCFixupKind Kind) {
  if (!Fixups)
    return false;

  unsigned Offset = Tail.size();
  appendLE(Tail, 0, Width);
  Fixups->push_back({Offset, Kind, Expr});
  return true;
}

bool parseConditionSuffix(StringRef Suffix, unsigned &Cond, bool AllowTF) {
  int Value = StringSwitch<int>(Suffix)
                  .Case("t", AllowTF ? 0x0 : -1)
                  .Case("f", AllowTF ? 0x1 : -1)
                  .Case("eq", 0x2)
                  .Case("z", 0x2)
                  .Case("ne", 0x3)
                  .Case("nz", 0x3)
                  .Case("ult", 0x4)
                  .Case("c", 0x4)
                  .Case("uge", 0x5)
                  .Case("nc", 0x5)
                  .Case("mi", 0x6)
                  .Case("n", 0x6)
                  .Case("pl", 0x7)
                  .Case("nn", 0x7)
                  .Case("vs", 0x8)
                  .Case("v", 0x8)
                  .Case("vc", 0x9)
                  .Case("nv", 0x9)
                  .Case("ule", 0xa)
                  .Case("ugt", 0xb)
                  .Case("lt", 0xc)
                  .Case("ge", 0xd)
                  .Case("le", 0xe)
                  .Case("gt", 0xf)
                  .Default(-1);
  if (Value < 0)
    return false;

  Cond = Value;
  return true;
}

bool getConditionSuffix(StringRef Mnemonic, StringRef Base, unsigned &Cond,
                        bool AllowTF) {
  if (!Mnemonic.consume_front(Base))
    return false;
  return parseConditionSuffix(Mnemonic, Cond, AllowTF);
}

bool getConditionSuffix(StringRef Mnemonic, StringRef Base, unsigned &Cond) {
  return getConditionSuffix(Mnemonic, Base, Cond, /*AllowTF=*/false);
}

bool getRepeatCondition(StringRef Mnemonic, unsigned &Cond) {
  int Value = StringSwitch<int>(Mnemonic)
                  .Case("rep", 0x0)
                  .Case("rept", 0x0)
                  .Case("repeq", 0x2)
                  .Case("repz", 0x2)
                  .Case("repne", 0x3)
                  .Case("repnz", 0x3)
                  .Case("repult", 0x4)
                  .Case("repc", 0x4)
                  .Case("repuge", 0x5)
                  .Case("repnc", 0x5)
                  .Case("repmi", 0x6)
                  .Case("repn", 0x6)
                  .Case("reppl", 0x7)
                  .Case("repnn", 0x7)
                  .Case("repvs", 0x8)
                  .Case("repv", 0x8)
                  .Case("repvc", 0x9)
                  .Case("repnv", 0x9)
                  .Case("repule", 0xa)
                  .Case("repugt", 0xb)
                  .Case("replt", 0xc)
                  .Case("repge", 0xd)
                  .Case("reple", 0xe)
                  .Case("repgt", 0xf)
                  .Default(-1);
  if (Value < 0)
    return false;

  Cond = Value;
  return true;
}

bool getConditionSizeSuffix(StringRef Mnemonic, StringRef Base, unsigned &Cond,
                            unsigned &Size, bool AllowTF) {
  if (!Mnemonic.consume_front(Base))
    return false;

  std::pair<StringRef, StringRef> Parts = Mnemonic.rsplit('.');
  if (Parts.second.empty() || Parts.first.empty())
    return false;

  if (!parseConditionSuffix(Parts.first, Cond, AllowTF))
    return false;

  Size = StringSwitch<unsigned>(Parts.second)
             .Case("b", 0)
             .Case("w", 1)
             .Case("l", 2)
             .Case("q", 3)
             .Default(4);
  return Size < 4;
}

StringRef getCanonicalConditionSuffix(unsigned Cond) {
  switch (Cond) {
  case 0x0:
    return "t";
  case 0x1:
    return "f";
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
    llvm_unreachable("invalid Bedrock condition code");
  }
}

void canonicalizeConditionMnemonic(std::string &Mnemonic) {
  static constexpr const char *SizedBases[] = {"cmpj", "testj"};
  StringRef Name(Mnemonic);
  for (const char *Base : SizedBases) {
    unsigned Cond;
    unsigned Size;
    if (!getConditionSizeSuffix(Name, Base, Cond, Size, /*AllowTF=*/false))
      continue;

    StringRef BaseRef(Base);
    StringRef Suffix = getCanonicalConditionSuffix(Cond);
    Mnemonic.assign(BaseRef.data(), BaseRef.size());
    Mnemonic.append(Suffix.data(), Suffix.size());
    Mnemonic.push_back('.');
    Mnemonic.push_back("bwlq"[Size]);
    return;
  }

  unsigned DJCond;
  if (getConditionSuffix(Name, "dj", DJCond, /*AllowTF=*/true)) {
    StringRef Suffix = getCanonicalConditionSuffix(DJCond);
    Mnemonic.assign("dj");
    Mnemonic.append(Suffix.data(), Suffix.size());
    return;
  }

  static constexpr const char *Bases[] = {"call", "set", "j"};
  for (const char *Base : Bases) {
    unsigned Cond;
    if (!getConditionSuffix(Name, Base, Cond))
      continue;

    StringRef BaseRef(Base);
    StringRef Suffix = getCanonicalConditionSuffix(Cond);
    Mnemonic.assign(BaseRef.data(), BaseRef.size());
    Mnemonic.append(Suffix.data(), Suffix.size());
    return;
  }
}

bool appendSignedAuto(int64_t Value, SmallVectorImpl<uint8_t> &Tail,
                      unsigned &WidthCode) {
  if (isIntN(8, Value)) {
    WidthCode = 0;
    appendLE(Tail, static_cast<uint64_t>(Value), 1);
    return true;
  }
  if (isIntN(16, Value)) {
    WidthCode = 1;
    appendLE(Tail, static_cast<uint64_t>(Value), 2);
    return true;
  }
  if (isIntN(32, Value)) {
    WidthCode = 2;
    appendLE(Tail, static_cast<uint64_t>(Value), 4);
    return true;
  }

  WidthCode = 3;
  appendLE(Tail, static_cast<uint64_t>(Value), 8);
  return true;
}

bool appendSignedAuto(const MCExpr *Expr, SmallVectorImpl<uint8_t> &Tail,
                      unsigned &WidthCode, SmallVectorImpl<RawFixup> *Fixups,
                      MCFixupKind Kind = FK_Data_4) {
  int64_t Value;
  if (Expr->evaluateAsAbsolute(Value))
    return appendSignedAuto(Value, Tail, WidthCode);

  WidthCode = 2;
  return appendExprTail(Expr, 4, Tail, Fixups, Kind);
}

bool encodeExt0EA(const BedrockOperand &Op, uint8_t &EA,
                  SmallVectorImpl<uint8_t> &Tail,
                  SmallVectorImpl<RawFixup> *Fixups = nullptr) {
  if (!Op.isMem())
    return false;

  if (Op.isVectorStride())
    return false;

  bool NeedsExt0 = Op.hasMemSegment() || Op.hasMemIndex() ||
                   Op.getMemBaseUpdate() != BedrockOperand::MemNoUpdate ||
                   Op.getMemBaseKind() == BedrockOperand::MemZero;
  if (!NeedsExt0)
    return false;

  SmallVector<uint8_t, 4> Descriptor;
  unsigned Segment = Op.hasMemSegment() ? Op.getMemSegment() : 0;
  auto IndexMode = [&]() -> int {
    switch (Op.getMemIndexUpdate()) {
    case BedrockOperand::MemPostInc:
      return 0;
    case BedrockOperand::MemPreDec:
      return 1;
    case BedrockOperand::MemNoUpdate:
      return 2;
    }
    llvm_unreachable("unknown Bedrock index update kind");
  };

  switch (Op.getMemBaseKind()) {
  case BedrockOperand::MemReg:
    if (Op.hasMemIndex()) {
      if (Op.getMemBaseUpdate() != BedrockOperand::MemNoUpdate)
        return false;
      Descriptor.push_back(0x80 | (Segment << 4) | IndexMode());
      Descriptor.push_back((Op.getMemBaseReg() << 4) | Op.getMemIndexReg());
      break;
    }

    if (Op.getMemBaseUpdate() != BedrockOperand::MemNoUpdate) {
      if (Op.hasMemSegment()) {
        Descriptor.push_back(0x80 | (Segment << 4) | 0x08);
        Descriptor.push_back(
            (Op.getMemBaseReg() << 4) |
            (Op.getMemBaseUpdate() == BedrockOperand::MemPostInc ? 0 : 1));
      } else {
        Descriptor.push_back(
            0x80 | (Op.getMemBaseReg() << 3) |
            (Op.getMemBaseUpdate() == BedrockOperand::MemPostInc ? 0x04
                                                                 : 0x05));
      }
      break;
    }

    if (!Op.hasMemSegment())
      return false;
    Descriptor.push_back((Segment << 4) | Op.getMemBaseReg());
    break;

  case BedrockOperand::MemZero:
    if (Op.hasMemIndex()) {
      Descriptor.push_back(0x80 | (Segment << 4) | 0x09);
      Descriptor.push_back((IndexMode() << 4) | Op.getMemIndexReg());
    } else {
      Descriptor.push_back(0x80 | (Segment << 4) | 0x03);
    }
    break;

  case BedrockOperand::MemSP:
  case BedrockOperand::MemPC:
    if (!Op.hasMemIndex() ||
        Op.getMemBaseUpdate() != BedrockOperand::MemNoUpdate ||
        Op.hasMemSegment())
      return false;
    Descriptor.push_back(Op.getMemBaseKind() == BedrockOperand::MemSP ? 0x8a
                                                                      : 0x8b);
    Descriptor.push_back((IndexMode() << 4) | Op.getMemIndexReg());
    break;

  case BedrockOperand::MemAbs:
    return false;
  }

  Tail.append(Descriptor.begin(), Descriptor.end());

  bool IsExt2 = Descriptor.size() == 2;
  if (Op.hasMemDisp()) {
    unsigned WidthCode;
    MCFixupKind Kind = Op.getMemBaseKind() == BedrockOperand::MemPC
                           ? MCFixupKind(Bedrock::fixup_bedrock_pcrel32)
                           : MCFixupKind(Bedrock::fixup_bedrock_disp32);
    if (!appendSignedAuto(Op.getMemDisp(), Tail, WidthCode, Fixups, Kind))
      return false;
    EA = (IsExt2 ? 0x64 : 0x5f) + WidthCode;
  } else {
    EA = IsExt2 ? 0x68 : 0x63;
  }

  return true;
}

bool encodeCompactEA(const BedrockOperand &Op, bool AllowImmediate, uint8_t &EA,
                     SmallVectorImpl<uint8_t> &Tail,
                     SmallVectorImpl<RawFixup> *Fixups = nullptr) {
  if (Op.isReg()) {
    return false;
  }

  if (isToken(Op, "sp")) {
    return false;
  }

  int64_t Value;
  unsigned WidthCode;
  if (Op.isImm()) {
    if (!AllowImmediate)
      return false;
    if (Op.getImmVariant() == BedrockOperand::ImmAbs64) {
      if (Op.getImm()->evaluateAsAbsolute(Value)) {
        WidthCode = 3;
        appendLE(Tail, static_cast<uint64_t>(Value), 8);
      } else {
        WidthCode = 3;
        if (!appendExprTail(Op.getImm(), 8, Tail, Fixups, FK_Data_8))
          return false;
      }
      EA = 0x5b + WidthCode;
      return true;
    }
    MCFixupKind ImmFixup = Op.getImmVariant() == BedrockOperand::ImmPCRel32
                               ? MCFixupKind(Bedrock::fixup_bedrock_pcrel32)
                               : MCFixupKind(Bedrock::fixup_bedrock_imm32);
    if (!appendSignedAuto(Op.getImm(), Tail, WidthCode, Fixups, ImmFixup))
      return false;
    EA = 0x5b + WidthCode;
    return true;
  }

  if (!Op.isMem())
    return false;

  if (encodeExt0EA(Op, EA, Tail, Fixups))
    return true;

  switch (Op.getMemBaseKind()) {
  case BedrockOperand::MemReg:
    if (!Op.hasMemDisp()) {
      EA = Op.getMemBaseReg();
      return true;
    }
    if (!appendSignedAuto(Op.getMemDisp(), Tail, WidthCode, Fixups,
                          Bedrock::fixup_bedrock_disp32))
      return false;
    EA = 0x10 + (WidthCode << 4) + Op.getMemBaseReg();
    return true;
  case BedrockOperand::MemSP:
    if (!Op.hasMemDisp()) {
      EA = 0x58;
      return true;
    }
    if (!appendSignedAuto(Op.getMemDisp(), Tail, WidthCode, Fixups,
                          Bedrock::fixup_bedrock_disp32))
      return false;
    EA = 0x50 + WidthCode;
    return true;
  case BedrockOperand::MemPC:
    if (!Op.hasMemDisp() ||
        !appendSignedAuto(Op.getMemDisp(), Tail, WidthCode, Fixups,
                          Bedrock::fixup_bedrock_pcrel32))
      return false;
    EA = 0x54 + WidthCode;
    return true;
  case BedrockOperand::MemAbs:
    if (!Op.hasMemDisp())
      return false;
    if (Op.getMemDisp()->evaluateAsAbsolute(Value)) {
      if (!isIntN(32, Value)) {
        EA = 0x5a;
        appendLE(Tail, static_cast<uint64_t>(Value), 8);
        return true;
      }
      EA = 0x59;
      appendLE(Tail, static_cast<uint64_t>(Value), 4);
      return true;
    }
    EA = 0x59;
    return appendExprTail(Op.getMemDisp(), 4, Tail, Fixups, FK_Data_4);
  case BedrockOperand::MemZero:
    return false;
  }

  llvm_unreachable("unknown Bedrock memory base kind");
}

bool encodeCompactFEA(const BedrockOperand &Op, bool AllowImmediate,
                      unsigned OperationSize, uint8_t &EA,
                      SmallVectorImpl<uint8_t> &Tail,
                      SmallVectorImpl<RawFixup> *Fixups = nullptr) {
  if (Op.isImm()) {
    if (!AllowImmediate || OperationSize > 1)
      return false;

    if (Op.getImmVariant() == BedrockOperand::ImmFP32Bits ||
        Op.getImmVariant() == BedrockOperand::ImmFP64Bits) {
      int64_t StoredBits;
      if (!Op.getImm()->evaluateAsAbsolute(StoredBits))
        return false;
      bool IsSingle = Op.getImmVariant() == BedrockOperand::ImmFP32Bits;
      EA = IsSingle ? 0x5d : 0x5e;
      appendLE(Tail, static_cast<uint64_t>(StoredBits), IsSingle ? 4 : 8);
      return true;
    }

    const fltSemantics &Semantics =
        OperationSize == 0 ? APFloat::IEEEsingle() : APFloat::IEEEdouble();
    APFloat FloatValue = APFloat::getZero(Semantics);
    uint64_t LiteralBits;
    if (getFloatLiteral(Op, LiteralBits)) {
      FloatValue = APFloat(APFloat::IEEEdouble(), APInt(64, LiteralBits));
      bool LosesInfo;
      FloatValue.convert(Semantics, APFloat::rmNearestTiesToEven, &LosesInfo);
    } else {
      int64_t IntegerValue;
      if (!getConstantImm(Op, IntegerValue))
        return false;
      FloatValue.convertFromAPInt(
          APInt(64, static_cast<uint64_t>(IntegerValue), /*isSigned=*/true),
          /*IsSigned=*/true, APFloat::rmNearestTiesToEven);
    }

    unsigned Width = OperationSize == 0 ? 4 : 8;
    EA = OperationSize == 0 ? 0x5d : 0x5e;
    appendLE(Tail, FloatValue.bitcastToAPInt().getZExtValue(), Width);
    return true;
  }

  if (!encodeCompactEA(Op, /*AllowImmediate=*/false, EA, Tail, Fixups))
    return false;
  if (EA == 0x58) {
    EA = 0x50;
    Tail.push_back(0);
  }
  return EA != 0x5b && EA != 0x5c;
}

bool encodeVectorEA(const BedrockOperand &Op, uint8_t &EA,
                    SmallVectorImpl<uint8_t> &Tail,
                    SmallVectorImpl<RawFixup> *Fixups = nullptr) {
  if (!Op.isMem())
    return false;

  if (!Op.isVectorStride()) {
    if (!encodeCompactEA(Op, /*AllowImmediate=*/false, EA, Tail, Fixups))
      return false;
    return EA != 0x58 && !(EA >= 0x5b && EA <= 0x5e);
  }

  if (Op.getMemBaseKind() != BedrockOperand::MemReg ||
      Op.getMemBaseUpdate() != BedrockOperand::MemNoUpdate ||
      Op.hasMemSegment() || !Op.hasMemIndex() ||
      Op.getMemIndexUpdate() != BedrockOperand::MemNoUpdate)
    return false;

  unsigned WidthCode = 0;
  if (Op.hasMemDisp()) {
    if (!appendSignedAuto(Op.getMemDisp(), Tail, WidthCode, Fixups,
                          Bedrock::fixup_bedrock_disp32))
      return false;
    EA = 0x5b + WidthCode;
  } else {
    EA = 0x58;
  }
  Tail.insert(Tail.begin(),
              uint8_t((Op.getMemBaseReg() << 4) | Op.getMemIndexReg()));
  if (Fixups) {
    for (RawFixup &Fixup : *Fixups)
      ++Fixup.Offset;
  }
  return true;
}

struct PatternFieldValue {
  char Field;
  unsigned Value;
};

uint32_t applyPatternValues(StringRef Pattern,
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

uint64_t applyPatternValues64(StringRef Pattern,
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

  uint64_t Payload = 0;
  unsigned Width = Pattern.size();
  for (unsigned I = 0; I != Width; ++I) {
    unsigned Bit = Width - I - 1;
    char C = Pattern[I];
    if (C == '0')
      continue;
    if (C == '1') {
      Payload |= uint64_t(1) << Bit;
      continue;
    }

    unsigned Index = static_cast<unsigned char>(C);
    assert(Active[Index] && "missing Bedrock pattern field");
    Payload |= uint64_t((Values[Index] >> --Counts[Index]) & 1) << Bit;
  }
  return Payload;
}

bool isFPTRANSAMnemonic(StringRef Mnemonic) {
  StringRef Base = Mnemonic.split('.').first;
  return StringSwitch<bool>(Base)
      .Cases({"facosa", "fasina", "fatana", "fatanha"}, true)
      .Cases({"fcosa", "fcosha", "fetoxa", "fetoxm1a"}, true)
      .Cases({"flog10a", "flog2a", "flogna", "flognp1a"}, true)
      .Cases({"fsina", "fsincosa", "fsinha", "ftana"}, true)
      .Cases({"ftanha", "ftentoxa", "ftwotoxa"}, true)
      .Default(false);
}

bool encodeMediumWithTail(uint32_t Payload, ArrayRef<uint8_t> Tail,
                          SmallVectorImpl<uint8_t> &Bytes,
                          SmallVectorImpl<RawFixup> *Fixups = nullptr) {
  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    return false;

  if (Fixups) {
    for (RawFixup &Fixup : *Fixups)
      Fixup.Offset += 3;
  }
  return true;
}

bool encodeLongWithTail(uint32_t Payload, ArrayRef<uint8_t> Tail,
                        SmallVectorImpl<uint8_t> &Bytes,
                        SmallVectorImpl<RawFixup> *Fixups = nullptr) {
  if (!BedrockMC::encodeLong(Payload, Tail, Bytes))
    return false;

  if (Fixups) {
    for (RawFixup &Fixup : *Fixups)
      Fixup.Offset += 4;
  }
  return true;
}

bool encodeExtraLongWithTail(uint64_t Payload, ArrayRef<uint8_t> Tail,
                             SmallVectorImpl<uint8_t> &Bytes,
                             SmallVectorImpl<RawFixup> *Fixups = nullptr) {
  if (!BedrockMC::encodeExtraLong(Payload, Tail, Bytes))
    return false;

  if (Fixups) {
    for (RawFixup &Fixup : *Fixups)
      Fixup.Offset += 5;
  }
  return true;
}

bool encodeXxlongWithTail(uint64_t Payload, ArrayRef<uint8_t> Tail,
                          SmallVectorImpl<uint8_t> &Bytes,
                          SmallVectorImpl<RawFixup> *Fixups = nullptr) {
  if (!BedrockMC::encodeXxlong(Payload, Tail, Bytes))
    return false;

  if (Fixups) {
    for (RawFixup &Fixup : *Fixups)
      Fixup.Offset += 6;
  }
  return true;
}

bool encodePatternWithTail(StringRef Pattern, uint64_t Payload,
                           ArrayRef<uint8_t> Tail,
                           SmallVectorImpl<uint8_t> &Bytes,
                           SmallVectorImpl<RawFixup> *Fixups = nullptr) {
  switch (Pattern.size()) {
  case 18:
    return encodeMediumWithTail(static_cast<uint32_t>(Payload), Tail, Bytes,
                                Fixups);
  case 26:
    return encodeLongWithTail(static_cast<uint32_t>(Payload), Tail, Bytes,
                              Fixups);
  case 34:
    return encodeExtraLongWithTail(Payload, Tail, Bytes, Fixups);
  case 42:
    return encodeXxlongWithTail(Payload, Tail, Bytes, Fixups);
  default:
    return false;
  }
}

void createRawExprInst(ArrayRef<uint8_t> Bytes, ArrayRef<RawFixup> Fixups,
                       MCInst &Inst) {
  Inst.setOpcode(Bedrock::RAW_EXPR);
  Inst.addOperand(MCOperand::createImm(Fixups.size()));
  for (const RawFixup &Fixup : Fixups) {
    Inst.addOperand(MCOperand::createImm(Fixup.Offset));
    Inst.addOperand(MCOperand::createImm(Fixup.Kind));
    Inst.addOperand(MCOperand::createExpr(Fixup.Expr));
  }
  for (uint8_t Byte : Bytes)
    Inst.addOperand(MCOperand::createImm(Byte));
}

void createRawInst(ArrayRef<uint8_t> Bytes, ArrayRef<RawFixup> Fixups,
                   MCInst &Inst) {
  if (Fixups.empty()) {
    BedrockMC::createRawInst(Bytes, Inst);
    return;
  }

  createRawExprInst(Bytes, Fixups, Inst);
}

bool matchVectorMnemonic(const BedrockMC::VectorEncodingForm &Form,
                         StringRef Mnemonic, unsigned &Suffix,
                         unsigned &Condition) {
  Suffix = 0;
  Condition = 0;
  StringRef Stem = Mnemonic;
  if (Form.SuffixField != '\0') {
    auto Parts = Mnemonic.rsplit('.');
    if (Parts.first.empty() || Parts.second.size() != 1)
      return false;
    Stem = Parts.first;
    char SuffixChar = Parts.second.front();
    StringRef Suffixes(Form.Suffixes);
    size_t Index = Suffixes.find(SuffixChar);
    if (Index == StringRef::npos && Form.HasWidthOnlyAliases) {
      char Canonical = StringSwitch<char>(StringRef(&SuffixChar, 1))
                           .Case("h", 'w')
                           .Case("s", 'l')
                           .Case("d", 'q')
                           .Default('\0');
      if (Canonical != '\0')
        Index = Suffixes.find(Canonical);
    }
    if (Index == StringRef::npos || Index >= 8 ||
        (Form.AllowedSuffixMask & (1u << Index)) == 0)
      return false;
    Suffix = Index;
  }

  if (!Form.HasCondition)
    return Stem == Form.Mnemonic;
  if (!Stem.consume_front(Form.Mnemonic) || Stem.empty())
    return false;
  return parseConditionSuffix(Stem, Condition, /*AllowTF=*/true) &&
         (Form.AllowedConditionMask & (1u << Condition)) != 0;
}

bool isVectorMnemonic(StringRef Mnemonic, bool &RequiresFPU) {
  for (const BedrockMC::VectorEncodingForm &Form :
       BedrockMC::vectorEncodingForms()) {
    unsigned Suffix;
    unsigned Condition;
    if (!matchVectorMnemonic(Form, Mnemonic, Suffix, Condition))
      continue;
    char SuffixChar = Form.SuffixField == '\0' ? '\0' : Form.Suffixes[Suffix];
    RequiresFPU = SuffixChar == 'h' || SuffixChar == 's' || SuffixChar == 'd';
    return true;
  }
  RequiresFPU = false;
  return false;
}

bool isRepeatEligibleMnemonic(StringRef Mnemonic, unsigned RepeatCondition) {
  StringRef Stem = Mnemonic.split('.').first;
  for (const BedrockMC::RepeatEligibility &Entry :
       BedrockMC::repeatEligibilityEntries()) {
    bool Matches = false;
    if (Entry.HasCondition) {
      StringRef ConditionText = Stem;
      if (ConditionText.consume_front(Entry.Mnemonic) &&
          !ConditionText.empty()) {
        unsigned Ignored = 0;
        Matches = parseConditionSuffix(ConditionText, Ignored,
                                       /*AllowTF=*/false);
      }
    } else {
      Matches = Stem == Entry.Mnemonic;
    }
    if (Matches)
      return RepeatCondition == 0 ? Entry.AllowsREP : Entry.AllowsREPcc;
  }
  return false;
}

bool matchScalarMnemonic(const BedrockMC::ScalarEncodingForm &Form,
                         StringRef Mnemonic, unsigned &Suffix,
                         unsigned &Condition) {
  Suffix = 0;
  Condition = 0;
  StringRef Stem = Mnemonic;
  if (Form.SuffixField != '\0') {
    auto Parts = Mnemonic.rsplit('.');
    if (Parts.first.empty() || Parts.second.size() != 1)
      return false;
    Stem = Parts.first;
    size_t Index = StringRef(Form.Suffixes).find(Parts.second.front());
    if (Index == StringRef::npos || Index >= 16 ||
        (Form.AllowedSuffixMask & (1u << Index)) == 0)
      return false;
    Suffix = Index;
  }

  if (Form.ConditionField == '\0')
    return Stem == Form.Mnemonic;
  if (!Stem.consume_front(Form.Mnemonic) || Stem.empty())
    return false;
  return parseConditionSuffix(Stem, Condition, /*AllowTF=*/true) &&
         (Form.AllowedConditionMask & (1u << Condition)) != 0;
}

bool tryEncodeScalarTableInstruction(OperandVector &Operands,
                                     SmallVectorImpl<uint8_t> &Bytes,
                                     SmallVectorImpl<RawFixup> &Fixups,
                                     unsigned RequestedLength = 0) {
  if (Operands.empty() || !Operands[0]->isToken())
    return false;
  StringRef Mnemonic = static_cast<const BedrockOperand &>(*Operands[0]).getToken();
  auto GetOp = [&](unsigned I) -> const BedrockOperand & {
    return static_cast<const BedrockOperand &>(*Operands[I]);
  };
  SmallVector<uint8_t, 16> BestBytes;
  SmallVector<RawFixup, 4> BestFixups;

  for (const BedrockMC::ScalarEncodingForm &Form :
       BedrockMC::scalarEncodingForms()) {
    if (Operands.size() != Form.OperandCount + 1)
      continue;
    unsigned Suffix = 0;
    unsigned Condition = 0;
    if (!matchScalarMnemonic(Form, Mnemonic, Suffix, Condition))
      continue;

    SmallVector<PatternFieldValue, 8> Fields;
    SmallVector<uint8_t, 8> EATail;
    SmallVector<uint8_t, 8> Tail;
    SmallVector<RawFixup, 4> CandidateFixups;
    if (Form.SuffixField != '\0')
      Fields.push_back({static_cast<char>(Form.SuffixField), Suffix});
    if (Form.ConditionField != '\0')
      Fields.push_back({static_cast<char>(Form.ConditionField), Condition});
    unsigned ExplicitValues[4] = {};
    bool Valid = true;
    for (unsigned I = 0; I != Form.OperandCount; ++I) {
      BedrockMC::ScalarOperandDesc Desc = Form.operand(I);
      const BedrockOperand &Op = GetOp(I + 1);
      unsigned Value = 0;
      switch (Desc.Kind) {
      case BedrockMC::ScalarOperandKind::GPR:
        Valid = Op.isReg() && getRegNo(Op.getReg(), Value);
        break;
      case BedrockMC::ScalarOperandKind::FPR:
        Valid = Op.isReg() && getFRegNo(Op.getReg(), Value);
        break;
      case BedrockMC::ScalarOperandKind::Segment:
        Valid = getSRegNo(Op, Value);
        break;
      case BedrockMC::ScalarOperandKind::EA: {
        uint8_t EA = 0;
        Valid = encodeCompactEA(Op, Desc.AllowImmediateEA, EA, EATail,
                                &CandidateFixups);
        Value = EA;
        break;
      }
      case BedrockMC::ScalarOperandKind::FEA: {
        uint8_t EA = 0;
        Valid = encodeCompactFEA(Op, Desc.AllowImmediateEA, Suffix, EA, EATail,
                                 &CandidateFixups);
        Value = EA;
        break;
      }
      case BedrockMC::ScalarOperandKind::Immediate: {
        int64_t Imm = 0;
        Valid = getConstantImm(Op, Imm) &&
                (Desc.Signed ? isIntN(Desc.Width, Imm)
                             : (Imm >= 0 && isUIntN(Desc.Width, Imm)));
        if (Valid)
          Value = static_cast<unsigned>(Imm) & ((1u << Desc.Width) - 1);
        break;
      }
      case BedrockMC::ScalarOperandKind::MemoryOrder: {
        int64_t Order = 0;
        Valid = getConstantImm(Op, Order) && Order >= 0 &&
                isUIntN(Desc.Width, Order);
        if (Valid)
          Value = static_cast<unsigned>(Order);
        break;
      }
      case BedrockMC::ScalarOperandKind::TailSigned:
      case BedrockMC::ScalarOperandKind::TailUnsigned: {
        int64_t Imm = 0;
        bool IsSigned = Desc.Kind == BedrockMC::ScalarOperandKind::TailSigned;
        Valid = getConstantImm(Op, Imm) &&
                (IsSigned ? isIntN(Desc.Width, Imm)
                          : (Imm >= 0 && isUIntN(Desc.Width, Imm)));
        if (Valid) {
          Value = static_cast<unsigned>(Imm);
          appendLE(Tail, static_cast<uint64_t>(Imm), Desc.Width / 8);
        }
        break;
      }
      case BedrockMC::ScalarOperandKind::RegisterSelector: {
        int64_t Imm = 0;
        uint64_t Selector = 0;
        Valid = (getConstantImm(Op, Imm) && Imm >= 0 &&
                 isUIntN(Desc.Width, Imm));
        if (!Valid && Op.isToken())
          Valid = BedrockMC::lookupRegisterSelector(
              Desc.FixedValue, Op.getToken(), Selector);
        else if (Valid)
          Selector = static_cast<uint64_t>(Imm);
        if (Valid) {
          Value = static_cast<unsigned>(Selector);
          appendLE(Tail, Selector, Desc.Width / 8);
        }
        break;
      }
      case BedrockMC::ScalarOperandKind::FixedSP:
        Valid = isStackPointer(Op);
        break;
      case BedrockMC::ScalarOperandKind::FixedCS:
        Valid = isCS(Op);
        break;
      case BedrockMC::ScalarOperandKind::FixedImmediate: {
        int64_t Imm = 0;
        Valid = getConstantImm(Op, Imm) &&
                static_cast<uint64_t>(Imm) == Desc.FixedValue;
        break;
      }
      default:
        Valid = false;
        break;
      }
      if (!Valid)
        break;
      if (Desc.Field != '\0') {
        if (!Desc.allows(Value)) {
          Valid = false;
          break;
        }
        Fields.push_back({Desc.Field, Value});
      }
      ExplicitValues[I] = Value;
    }
    if (!Valid)
      continue;
    if (Form.distinctOperandA() >= 0 && Form.distinctOperandB() >= 0 &&
        ExplicitValues[Form.distinctOperandA()] ==
            ExplicitValues[Form.distinctOperandB()])
      continue;

    StringRef Pattern(Form.Pattern);
    uint64_t Payload = applyPatternValues64(Pattern, Fields);
    if (Tail.size() != Form.FixedPayloadBytes)
      continue;
    EATail.append(Tail.begin(), Tail.end());
    SmallVector<uint8_t, 16> CandidateBytes;
    bool Encoded = false;
    if (Pattern.size() == 7)
      Encoded = BedrockMC::encodeExtraShort(Payload, CandidateBytes);
    else if (Pattern.size() == 14)
      Encoded = BedrockMC::encodeShort(Payload, CandidateBytes);
    else
      Encoded = encodePatternWithTail(Pattern, Payload, EATail, CandidateBytes,
                                      &CandidateFixups);
    bool CandidateIsExact =
        RequestedLength != 0 && CandidateBytes.size() == RequestedLength;
    bool BestIsExact = RequestedLength != 0 && BestBytes.size() == RequestedLength;
    if (Encoded &&
        (BestBytes.empty() || (CandidateIsExact && !BestIsExact) ||
         (CandidateIsExact == BestIsExact &&
          CandidateBytes.size() < BestBytes.size()))) {
      BestBytes = std::move(CandidateBytes);
      BestFixups = std::move(CandidateFixups);
    }
  }
  if (BestBytes.empty())
    return false;
  Bytes.append(BestBytes.begin(), BestBytes.end());
  Fixups.append(BestFixups.begin(), BestFixups.end());
  return true;
}

bool tryEncodeVectorStepInstruction(OperandVector &Operands,
                                    SmallVectorImpl<uint8_t> &Bytes,
                                    SmallVectorImpl<RawFixup> &Fixups,
                                    unsigned RequestedLength = 0) {
  if (Operands.size() < 6 || !Operands[0]->isToken())
    return false;
  const auto &MnemonicOp = static_cast<const BedrockOperand &>(*Operands[0]);
  StringRef Mnemonic = MnemonicOp.getToken();
  auto Parts = Mnemonic.rsplit('.');
  bool IsGather = Parts.first == "vgather1";
  bool IsScatter = Parts.first == "vscatter1";
  if ((!IsGather && !IsScatter) || Parts.second.size() != 1)
    return false;
  size_t SizePos = StringRef("bwlq").find(Parts.second.front());
  if (SizePos == StringRef::npos)
    return false;
  unsigned Size = SizePos;

  auto GetOp = [&](unsigned I) -> const BedrockOperand & {
    return static_cast<const BedrockOperand &>(*Operands[I]);
  };
  unsigned Predicate;
  if (!GetOp(1).isReg() || !getPRegNo(GetOp(1).getReg(), Predicate))
    return false;

  unsigned MarkerIndex = IsGather ? 2 : 3;
  if (!GetOp(MarkerIndex).isToken())
    return false;
  StringRef Marker = GetOp(MarkerIndex).getToken();
  unsigned VectorIndex = IsGather ? Operands.size() - 1 : 2;
  unsigned Vector;
  if (!GetOp(VectorIndex).isReg() ||
      !getVRegNo(GetOp(VectorIndex).getReg(), Vector))
    return false;

  unsigned FieldIndex = MarkerIndex + 1;
  SmallVector<PatternFieldValue, 8> Fields = {
      {'z', Size}, {'p', Predicate}, {'v', Vector}};
  SmallVector<uint8_t, 8> Tail;
  SmallVector<RawFixup, 2> LocalFixups;
  StringRef Pattern;

  if (Marker == "__step_scalar_stride") {
    if (Operands.size() != 7)
      return false;
    unsigned Base;
    unsigned Cursor;
    unsigned Stride;
    if (!GetOp(FieldIndex).isReg() ||
        !getRegNo(GetOp(FieldIndex).getReg(), Base) ||
        !GetOp(FieldIndex + 1).isReg() ||
        !getRegNo(GetOp(FieldIndex + 1).getReg(), Cursor) ||
        !GetOp(FieldIndex + 2).isReg() ||
        !getRegNo(GetOp(FieldIndex + 2).getReg(), Stride))
      return false;
    Fields.append({{'b', Base}, {'i', Cursor}, {'s', Stride}});
    Pattern = IsGather ? "111111110000010000zz0bbbbppppiiiissssvvvvv"
                       : "111111110000011000zz0bbbbppppiiiissssvvvvv";
  } else if (Marker == "__step_vector") {
    unsigned Address;
    unsigned Cursor;
    if (!GetOp(FieldIndex).isReg() ||
        !getVRegNo(GetOp(FieldIndex).getReg(), Address) ||
        !GetOp(FieldIndex + 1).isReg() ||
        !getRegNo(GetOp(FieldIndex + 1).getReg(), Cursor))
      return false;
    Fields.append({{'x', Address}, {'i', Cursor}});
    if (Operands.size() == 6) {
      if (Size != 2 && Size != 3)
        return false;
      if (IsGather)
        Pattern = Size == 2 ? "111111110000010001000000ppppiiiivvvvvxxxxx"
                            : "111111110000010001000001ppppiiiivvvvvxxxxx";
      else
        Pattern = Size == 2 ? "111111110000011001000000ppppiiiivvvvvxxxxx"
                            : "111111110000011001000001ppppiiiivvvvvxxxxx";
    } else {
      return false;
    }
  } else if (Marker == "__step_vector_base" ||
             Marker == "__step_vector_scaled" ||
             Marker == "__step_vector_disp") {
    unsigned Base;
    unsigned Address;
    unsigned Cursor;
    if (!GetOp(FieldIndex).isReg() ||
        !getRegNo(GetOp(FieldIndex).getReg(), Base) ||
        !GetOp(FieldIndex + 1).isReg() ||
        !getVRegNo(GetOp(FieldIndex + 1).getReg(), Address) ||
        !GetOp(FieldIndex + 2).isReg() ||
        !getRegNo(GetOp(FieldIndex + 2).getReg(), Cursor))
      return false;
    Fields.append({{'b', Base}, {'x', Address}, {'i', Cursor}});
    if (Marker == "__step_vector_base") {
      Pattern = IsGather ? "111111110000010010zzbbbbppppiiiivvvvvxxxxx"
                         : "111111110000011010zzbbbbppppiiiivvvvvxxxxx";
    } else if (Marker == "__step_vector_scaled") {
      Pattern = IsGather ? "111111110000010011zzbbbbppppiiiivvvvvxxxxx"
                         : "111111110000011011zzbbbbppppiiiivvvvvxxxxx";
    } else {
      if (Operands.size() != 8)
        return false;
      int64_t Disp;
      const BedrockOperand &DispOp = GetOp(FieldIndex + 3);
      if (!getConstantImm(DispOp, Disp))
        return false;
      unsigned WidthCode;
      unsigned Width;
      if (isIntN(8, Disp)) {
        WidthCode = 0;
        Width = 1;
      } else if (isIntN(16, Disp)) {
        WidthCode = 1;
        Width = 2;
      } else if (isIntN(32, Disp)) {
        WidthCode = 2;
        Width = 4;
      } else {
        WidthCode = 3;
        Width = 8;
      }
      for (unsigned CandidateCode = 0; CandidateCode != 4; ++CandidateCode) {
        unsigned CandidateWidth = 1u << CandidateCode;
        if (RequestedLength == 6 + CandidateWidth &&
            isIntN(CandidateWidth * 8, Disp)) {
          WidthCode = CandidateCode;
          Width = CandidateWidth;
          break;
        }
      }
      appendLE(Tail, static_cast<uint64_t>(Disp), Width);
      static constexpr StringLiteral GatherPatterns[] = {
          "111111110000010100zzbbbbppppiiiivvvvvxxxxx",
          "111111110000010101zzbbbbppppiiiivvvvvxxxxx",
          "111111110000010110zzbbbbppppiiiivvvvvxxxxx",
          "111111110000010111zzbbbbppppiiiivvvvvxxxxx"};
      static constexpr StringLiteral ScatterPatterns[] = {
          "111111110000011100zzbbbbppppiiiivvvvvxxxxx",
          "111111110000011101zzbbbbppppiiiivvvvvxxxxx",
          "111111110000011110zzbbbbppppiiiivvvvvxxxxx",
          "111111110000011111zzbbbbppppiiiivvvvvxxxxx"};
      Pattern =
          IsGather ? GatherPatterns[WidthCode] : ScatterPatterns[WidthCode];
    }
  } else {
    return false;
  }

  uint64_t Payload = applyPatternValues64(Pattern, Fields);
  if (!encodeXxlongWithTail(Payload, Tail, Bytes, &LocalFixups))
    return false;
  Fixups.append(LocalFixups.begin(), LocalFixups.end());
  return true;
}

bool tryEncodeVectorInstruction(OperandVector &Operands,
                                SmallVectorImpl<uint8_t> &Bytes,
                                SmallVectorImpl<RawFixup> &Fixups) {
  if (Operands.empty() || !Operands[0]->isToken())
    return false;
  StringRef Mnemonic =
      static_cast<const BedrockOperand &>(*Operands[0]).getToken();

  for (const BedrockMC::VectorEncodingForm &Form :
       BedrockMC::vectorEncodingForms()) {
    unsigned Suffix;
    unsigned Condition;
    if (!matchVectorMnemonic(Form, Mnemonic, Suffix, Condition))
      continue;

    unsigned ExplicitCount = 0;
    for (unsigned I = 0; I != Form.OperandCount; ++I)
      ExplicitCount +=
          Form.operand(I).Kind != BedrockMC::VectorOperandKind::Condition;
    if (Operands.size() != ExplicitCount + 1)
      continue;

    SmallVector<PatternFieldValue, 8> Fields;
    if (Form.SuffixField != '\0')
      Fields.push_back({static_cast<char>(Form.SuffixField), Suffix});
    if (Form.HasCondition)
      Fields.push_back({'c', Condition});
    SmallVector<uint8_t, 16> Tail;
    SmallVector<RawFixup, 4> LocalFixups;
    SmallVector<unsigned, 5> ExplicitValues;
    unsigned OperandIndex = 1;
    bool Valid = true;
    for (unsigned I = 0; I != Form.OperandCount && Valid; ++I) {
      const BedrockMC::VectorOperandDesc Desc = Form.operand(I);
      if (Desc.Kind == BedrockMC::VectorOperandKind::Condition)
        continue;
      const auto &Operand =
          static_cast<const BedrockOperand &>(*Operands[OperandIndex++]);
      unsigned Value = 0;
      int64_t Immediate = 0;
      switch (Desc.Kind) {
      case BedrockMC::VectorOperandKind::None:
      case BedrockMC::VectorOperandKind::Condition:
        llvm_unreachable("invalid explicit vector operand kind");
      case BedrockMC::VectorOperandKind::GPR:
        Valid = Operand.isReg() && getRegNo(Operand.getReg(), Value);
        break;
      case BedrockMC::VectorOperandKind::FPR:
        Valid = Operand.isReg() && getFRegNo(Operand.getReg(), Value);
        break;
      case BedrockMC::VectorOperandKind::Vector:
        Valid = Operand.isReg() && getVRegNo(Operand.getReg(), Value);
        break;
      case BedrockMC::VectorOperandKind::Predicate:
        Valid = Operand.isReg() && getPRegNo(Operand.getReg(), Value);
        break;
      case BedrockMC::VectorOperandKind::EA: {
        uint8_t EA = 0;
        Valid = encodeCompactEA(Operand, Desc.AllowImmediateEA, EA, Tail,
                                &LocalFixups);
        Value = EA;
        break;
      }
      case BedrockMC::VectorOperandKind::VEA: {
        uint8_t EA = 0;
        Valid = encodeVectorEA(Operand, EA, Tail, &LocalFixups);
        Value = EA;
        break;
      }
      case BedrockMC::VectorOperandKind::Immediate:
        Valid = getConstantImm(Operand, Immediate) &&
                isUIntN(Desc.Width, Immediate);
        Value = static_cast<unsigned>(Immediate);
        break;
      case BedrockMC::VectorOperandKind::TailSigned:
      case BedrockMC::VectorOperandKind::TailUnsigned: {
        unsigned Width = Desc.Width / 8;
        if (getConstantImm(Operand, Immediate)) {
          Valid = Desc.Kind == BedrockMC::VectorOperandKind::TailSigned
                      ? isIntN(Desc.Width, Immediate)
                      : isUIntN(Desc.Width, Immediate);
          if (Valid)
            appendLE(Tail, static_cast<uint64_t>(Immediate), Width);
        } else if (Operand.isImm()) {
          MCFixupKind Kind =
              Desc.Kind == BedrockMC::VectorOperandKind::TailSigned &&
                      Desc.Width == 32 &&
                      StringRef(Form.Mnemonic).starts_with("bp")
                  ? MCFixupKind(Bedrock::fixup_bedrock_pcrel32)
                  : getDataFixupKind(Width);
          Valid =
              appendExprTail(Operand.getImm(), Width, Tail, &LocalFixups, Kind);
        } else {
          Valid = false;
        }
        break;
      }
      }
      if (Valid) {
        ExplicitValues.push_back(Value);
        if (Desc.Field != '\0')
          Fields.push_back({Desc.Field, Value});
      }
    }
    if (!Valid)
      continue;
    if (Form.distinctOperandA() >= 0 && Form.distinctOperandB() >= 0 &&
        ExplicitValues[Form.distinctOperandA()] ==
            ExplicitValues[Form.distinctOperandB()])
      continue;

    uint64_t Payload = applyPatternValues64(Form.Pattern, Fields);
    bool Encoded = false;
    switch (Form.encodingClass()) {
    case BedrockMC::VectorEncodingClass::Long:
      Encoded = encodeLongWithTail(Payload, Tail, Bytes, &LocalFixups);
      break;
    case BedrockMC::VectorEncodingClass::ExtraLong:
      Encoded = encodeExtraLongWithTail(Payload, Tail, Bytes, &LocalFixups);
      break;
    case BedrockMC::VectorEncodingClass::Xxlong:
      Encoded = encodeXxlongWithTail(Payload, Tail, Bytes, &LocalFixups);
      break;
    }
    if (!Encoded)
      continue;
    Fixups.append(LocalFixups.begin(), LocalFixups.end());
    return true;
  }
  return false;
}

bool extractRepeatOperands(OperandVector &Operands, unsigned &Cond,
                           unsigned &RegNo, OperandVector &BodyOperands) {
  if (Operands.size() < 4)
    return false;

  const auto &MarkerOp = static_cast<const BedrockOperand &>(*Operands[0]);
  if (!MarkerOp.isToken() || MarkerOp.getToken() != "__rep")
    return false;

  int64_t CondValue = 0;
  int64_t RegValue = 0;
  if (!getConstantImm(static_cast<const BedrockOperand &>(*Operands[1]),
                      CondValue) ||
      !getConstantImm(static_cast<const BedrockOperand &>(*Operands[2]),
                      RegValue) ||
      !isUIntN(4, CondValue) || CondValue == 0x1 || !isUIntN(4, RegValue))
    return false;

  Cond = CondValue;
  RegNo = RegValue;
  for (unsigned I = 3; I != Operands.size(); ++I)
    BodyOperands.push_back(std::move(Operands[I]));
  return true;
}

bool extractLengthOperands(OperandVector &Operands, unsigned &RequestedLength,
                           bool &HasExplicitPadding,
                           SmallVectorImpl<uint8_t> &PaddingBytes,
                           OperandVector &BodyOperands) {
  if (Operands.size() < 4)
    return false;

  const auto &MarkerOp = static_cast<const BedrockOperand &>(*Operands[0]);
  if (!MarkerOp.isToken() || MarkerOp.getToken() != "__len")
    return false;

  int64_t LengthValue = 0;
  int64_t PaddingCount = 0;
  if (!getConstantImm(static_cast<const BedrockOperand &>(*Operands[1]),
                      LengthValue) ||
      !getConstantImm(static_cast<const BedrockOperand &>(*Operands[2]),
                      PaddingCount) ||
      LengthValue < 3 || LengthValue > 18 || PaddingCount < -1 ||
      static_cast<uint64_t>(PaddingCount + 3) > Operands.size())
    return false;

  RequestedLength = LengthValue;
  HasExplicitPadding = PaddingCount >= 0;
  for (int64_t I = 0; I < PaddingCount; ++I) {
    int64_t Byte = 0;
    if (!getConstantImm(static_cast<const BedrockOperand &>(*Operands[3 + I]),
                        Byte) ||
        !isUInt<8>(Byte))
      return false;
    PaddingBytes.push_back(static_cast<uint8_t>(Byte));
  }
  unsigned BodyStart = 3 + (HasExplicitPadding ? PaddingCount : 0);
  for (unsigned I = BodyStart; I != Operands.size(); ++I)
    BodyOperands.push_back(std::move(Operands[I]));
  return true;
}

bool tryEncodeSymbolicInstruction(OperandVector &Operands,
                                  SmallVectorImpl<uint8_t> &Bytes,
                                  SmallVectorImpl<RawFixup> &Fixups);

bool encodeInstructionOperands(OperandVector &Operands,
                               SmallVectorImpl<uint8_t> &Bytes,
                               SmallVectorImpl<RawFixup> &Fixups,
                               unsigned RequestedLength = 0) {
  return tryEncodeVectorStepInstruction(Operands, Bytes, Fixups,
                                        RequestedLength) ||
         tryEncodeVectorInstruction(Operands, Bytes, Fixups) ||
         tryEncodeScalarTableInstruction(Operands, Bytes, Fixups,
                                         RequestedLength) ||
         tryEncodeSymbolicInstruction(Operands, Bytes, Fixups);
}

bool prependRepeatInstruction(unsigned Cond, unsigned RegNo,
                              SmallVectorImpl<uint8_t> &Bytes,
                              SmallVectorImpl<RawFixup> &Fixups) {
  SmallVector<uint8_t, 4> RepBytes;
  uint32_t Payload =
      applyPatternValues("1110111110ccccrrrr", {{'c', Cond}, {'r', RegNo}});
  if (!BedrockMC::encodeMedium(Payload, {}, RepBytes))
    return false;

  for (RawFixup &Fixup : Fixups)
    Fixup.Offset += RepBytes.size();

  Bytes.insert(Bytes.begin(), RepBytes.begin(), RepBytes.end());
  return true;
}

bool applyRequestedLength(unsigned RequestedLength, bool HasExplicitPadding,
                          ArrayRef<uint8_t> PaddingBytes,
                          SmallVectorImpl<uint8_t> &Bytes,
                          SmallVectorImpl<RawFixup> &Fixups) {
  uint64_t RequiredLength = 0;
  if (!BedrockMC::getInstructionSize(Bytes, RequiredLength) || Bytes.empty() ||
      (Bytes[0] & 0xc0) != 0xc0 || RequestedLength < RequiredLength)
    return false;

  unsigned PaddingLength = RequestedLength - RequiredLength;
  if (HasExplicitPadding && PaddingBytes.size() != PaddingLength)
    return false;
  if (PaddingLength != 0) {
    for (RawFixup &Fixup : Fixups) {
      if (Fixup.Offset >= RequiredLength)
        Fixup.Offset += PaddingLength;
    }
    if (HasExplicitPadding)
      Bytes.insert(Bytes.begin() + RequiredLength, PaddingBytes.begin(),
                   PaddingBytes.end());
    else
      Bytes.insert(Bytes.begin() + RequiredLength, PaddingLength, 0);
  }
  Bytes[0] = (Bytes[0] & 0xc3) | ((RequestedLength - 3) << 2);
  return true;
}

bool tryEncodeSymbolicInstruction(OperandVector &Operands,
                                  SmallVectorImpl<uint8_t> &Bytes,
                                  SmallVectorImpl<RawFixup> &Fixups) {
  if (Operands.empty() || !Operands[0]->isToken())
    return false;

  StringRef Mnemonic = static_cast<BedrockOperand &>(*Operands[0]).getToken();
  auto GetOp = [&](unsigned I) -> const BedrockOperand & {
    return static_cast<const BedrockOperand &>(*Operands[I]);
  };

  if (Operands.size() == 2 && GetOp(1).isImm()) {
    int64_t Absolute;
    if (getConstantImm(GetOp(1), Absolute))
      return false;
    if (GetOp(1).getImmVariant() == BedrockOperand::ImmAbs64)
      return false;

    unsigned Cond = 0;
    uint32_t Payload = 0;
    bool IsCall = false;
    if (Mnemonic == "jmp") {
      Payload = applyPatternValues("111011110000000110", {});
    } else if (getConditionSuffix(Mnemonic, "j", Cond)) {
      Payload = applyPatternValues("11101110000010cccc", {{'c', Cond}});
    } else if (Mnemonic == "call") {
      Payload = applyPatternValues("111011110000000010", {});
      IsCall = true;
    } else if (getConditionSuffix(Mnemonic, "call", Cond)) {
      Payload = applyPatternValues("11101110000000cccc", {{'c', Cond}});
      IsCall = true;
    } else {
      return false;
    }

    SmallVector<uint8_t, 4> Tail(2, 0);
    if (!encodeMediumWithTail(Payload, Tail, Bytes))
      return false;

    const unsigned FixupOffset = 3;
    MCFixupKind Kind = IsCall ? MCFixupKind(Bedrock::fixup_bedrock_call16)
                              : MCFixupKind(Bedrock::fixup_bedrock_brdisp16);
    Fixups.push_back({FixupOffset, Kind, getImmExpr(GetOp(1))});
    return true;
  }

  return false;
}

} // end anonymous namespace

bool BedrockAsmParser::matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                               OperandVector &Operands,
                                               MCStreamer &Out,
                                               uint64_t &ErrorInfo,
                                               bool MatchingInlineAsm) {
  MCInst Inst;
  unsigned RequestedLength = 0;
  bool HasExplicitPadding = false;
  SmallVector<uint8_t, 16> PaddingBytes;
  SmallVector<std::unique_ptr<MCParsedAsmOperand>, 8> LengthBodyOperands;
  bool HasRequestedLength = extractLengthOperands(
      Operands, RequestedLength, HasExplicitPadding, PaddingBytes,
      LengthBodyOperands);
  OperandVector &EffectiveOperands =
      HasRequestedLength ? LengthBodyOperands : Operands;
  unsigned EncodingLengthHint =
      HasRequestedLength && HasExplicitPadding &&
              PaddingBytes.size() <= RequestedLength
          ? RequestedLength - PaddingBytes.size()
          : RequestedLength;

  auto EmitRawInstruction = [&](SmallVectorImpl<uint8_t> &RawBytes,
                                SmallVectorImpl<RawFixup> &RawFixups) {
    if (HasRequestedLength &&
        !applyRequestedLength(RequestedLength, HasExplicitPadding,
                              PaddingBytes, RawBytes, RawFixups)) {
      if (HasExplicitPadding)
        return Error(IDLoc, "explicit LEN padding byte count must equal "
                            "requested length minus required instruction "
                            "length");
      return Error(IDLoc, "LEN requires an extended instruction and must cover "
                          "its required length");
    }
    createRawInst(RawBytes, RawFixups, Inst);
    Inst.setLoc(IDLoc);
    Out.emitInstruction(Inst, *STI);
    return false;
  };

  bool IsVectorInstruction = false;
  if (!EffectiveOperands.empty()) {
    const auto &MnemonicOp =
        static_cast<const BedrockOperand &>(*EffectiveOperands[0]);
    bool RequiresVectorFPU = false;
    if (MnemonicOp.isToken() &&
        isVectorMnemonic(MnemonicOp.getToken(), RequiresVectorFPU)) {
      IsVectorInstruction = true;
      if (!getSTI().hasFeature(Bedrock::FeatureVector))
        return Error(IDLoc, "instruction requires the +vector feature");
      if (RequiresVectorFPU && !getSTI().hasFeature(Bedrock::FeatureFPU))
        return Error(
            IDLoc,
            "floating-point vector instruction requires the +fpu feature");
    }
    if (MnemonicOp.isToken() && isFPTRANSAMnemonic(MnemonicOp.getToken()) &&
        !getSTI().hasFeature(Bedrock::FeatureFPTRANSA))
      return Error(IDLoc, "instruction requires the +fptransa feature");
  }
  if (!IsVectorInstruction) {
    for (const auto &Operand : EffectiveOperands) {
      const auto &BedrockOp = static_cast<const BedrockOperand &>(*Operand);
      if (BedrockOp.isMem() && BedrockOp.isVectorStride())
        return Error(
            BedrockOp.getStartLoc(),
            "'* lane' effective address requires a vector instruction");
    }
  }
  unsigned RepCond = 0;
  unsigned RepRegNo = 0;
  SmallVector<std::unique_ptr<MCParsedAsmOperand>, 8> RepeatBodyOperands;
  if (extractRepeatOperands(EffectiveOperands, RepCond, RepRegNo,
                            RepeatBodyOperands)) {
    unsigned RepeatBodyLength = 0;
    bool HasExplicitRepeatBodyPadding = false;
    SmallVector<uint8_t, 16> RepeatBodyPaddingBytes;
    SmallVector<std::unique_ptr<MCParsedAsmOperand>, 8>
        LengthRepeatBodyOperands;
    bool HasRepeatBodyLength = extractLengthOperands(
        RepeatBodyOperands, RepeatBodyLength, HasExplicitRepeatBodyPadding,
        RepeatBodyPaddingBytes, LengthRepeatBodyOperands);
    OperandVector &EffectiveRepeatBodyOperands =
        HasRepeatBodyLength ? LengthRepeatBodyOperands : RepeatBodyOperands;
    unsigned RepeatBodyEncodingLengthHint =
        HasRepeatBodyLength && HasExplicitRepeatBodyPadding &&
                RepeatBodyPaddingBytes.size() <= RepeatBodyLength
            ? RepeatBodyLength - RepeatBodyPaddingBytes.size()
            : RepeatBodyLength;
    const auto &BodyMnemonic =
        static_cast<const BedrockOperand &>(*EffectiveRepeatBodyOperands[0]);
    if (!BodyMnemonic.isToken() ||
        !isRepeatEligibleMnemonic(BodyMnemonic.getToken(), RepCond))
      return Error(IDLoc, "instruction is not eligible as a repeat body");
    SmallVector<uint8_t, 16> RawBytes;
    SmallVector<RawFixup, 4> RawFixups;
    if (encodeInstructionOperands(EffectiveRepeatBodyOperands, RawBytes,
                                  RawFixups, RepeatBodyEncodingLengthHint) &&
        (!HasRepeatBodyLength ||
         applyRequestedLength(RepeatBodyLength, HasExplicitRepeatBodyPadding,
                              RepeatBodyPaddingBytes, RawBytes, RawFixups)) &&
        prependRepeatInstruction(RepCond, RepRegNo, RawBytes, RawFixups))
      return EmitRawInstruction(RawBytes, RawFixups);
    return Error(IDLoc, "invalid repeated instruction");
  }

  SmallVector<uint8_t, 16> RawBytes;
  SmallVector<RawFixup, 4> RawFixups;
  if (tryEncodeVectorStepInstruction(EffectiveOperands, RawBytes, RawFixups,
                                     EncodingLengthHint))
    return EmitRawInstruction(RawBytes, RawFixups);
  if (tryEncodeVectorInstruction(EffectiveOperands, RawBytes, RawFixups))
    return EmitRawInstruction(RawBytes, RawFixups);
  if (tryEncodeScalarTableInstruction(EffectiveOperands, RawBytes, RawFixups,
                                      EncodingLengthHint))
    return EmitRawInstruction(RawBytes, RawFixups);

  if (tryEncodeSymbolicInstruction(EffectiveOperands, RawBytes, RawFixups))
    return EmitRawInstruction(RawBytes, RawFixups);

  if (HasRequestedLength)
    return Error(IDLoc, "invalid instruction after LEN");

  unsigned MatchResult = MatchInstructionImpl(EffectiveOperands, Inst,
                                              ErrorInfo, MatchingInlineAsm);

  if (MatchResult == Match_Success) {
    Inst.setLoc(IDLoc);
    Out.emitInstruction(Inst, *STI);
    return false;
  }

  switch (MatchResult) {
  case Match_MnemonicFail:
    return Error(IDLoc, "invalid instruction mnemonic");
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;
    if (ErrorInfo != ~0U && ErrorInfo < Operands.size())
      ErrorLoc =
          static_cast<BedrockOperand &>(*Operands[ErrorInfo]).getStartLoc();
    return Error(ErrorLoc, "invalid operand for instruction");
  }
  default:
    return true;
  }
}

static MCRegister MatchRegisterName(StringRef Name);

bool BedrockAsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                     SMLoc &EndLoc) {
  ParseStatus Res = tryParseRegister(Reg, StartLoc, EndLoc);
  if (Res.isSuccess())
    return false;
  return Error(StartLoc, "invalid register name");
}

ParseStatus BedrockAsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                               SMLoc &EndLoc) {
  if (getLexer().getKind() != AsmToken::Identifier)
    return ParseStatus::NoMatch;

  StringRef Name = getLexer().getTok().getIdentifier();
  Reg = matchBedrockRegisterName(Name.lower());
  if (Reg == Bedrock::NoRegister)
    return ParseStatus::NoMatch;

  StartLoc = getLexer().getTok().getLoc();
  EndLoc = getLexer().getTok().getEndLoc();
  getLexer().Lex();
  return ParseStatus::Success;
}

bool BedrockAsmParser::parseOperand(OperandVector &Operands) {
  if (getLexer().is(AsmToken::Error))
    return Error(getLexer().getErrLoc(), getLexer().getErr());
  if (getLexer().is(AsmToken::LBrac))
    return parseMemoryOperand(Operands);
  if (getLexer().is(AsmToken::LCurly))
    return parseRegisterMaskOperand(Operands);

  MCRegister Reg;
  SMLoc StartLoc;
  SMLoc EndLoc;
  ParseStatus RegStatus = tryParseRegister(Reg, StartLoc, EndLoc);
  if (RegStatus.isSuccess()) {
    Operands.push_back(BedrockOperand::createReg(Reg, StartLoc, EndLoc));
    return false;
  }

  if (getLexer().is(AsmToken::Identifier)) {
    StringRef Name = getLexer().getTok().getIdentifier();
    std::string Lower = Name.lower();
    if (Lower == "sp" || Lower == "pc" || Lower == "cs" || Lower == "ds" ||
        Lower == "ss" || Lower == "gs0" || Lower == "gs1" || Lower == "gs2" ||
        Lower == "gs3" || Lower == "gs4" || Lower == "gs5" ||
        BedrockMC::isRegisterSelectorName(Lower)) {
      StartLoc = getLexer().getTok().getLoc();
      Operands.push_back(BedrockOperand::createToken(Lower, StartLoc));
      getLexer().Lex();
      return false;
    }
  }

  if (getLexer().is(AsmToken::Identifier) &&
      getLexer().peekTok().is(AsmToken::LParen)) {
    StringRef Name = getLexer().getTok().getIdentifier();
    bool IsSingle = Name.equals_insensitive("immsf");
    bool IsDouble = Name.equals_insensitive("immdf");
    if (IsSingle || IsDouble) {
      StartLoc = getLexer().getTok().getLoc();
      getLexer().Lex();
      getLexer().Lex();
      if (!getLexer().is(AsmToken::Integer))
        return Error(getLexer().getLoc(),
                     "expected floating-point immediate payload bits");
      APInt Bits = getLexer().getTok().getAPIntVal();
      unsigned Width = IsSingle ? 32 : 64;
      if (Bits.getActiveBits() > Width)
        return Error(getLexer().getLoc(),
                     "floating-point immediate payload is out of range");
      getLexer().Lex();
      if (!getLexer().is(AsmToken::RParen))
        return Error(getLexer().getLoc(),
                     "expected ')' after floating-point immediate payload");
      EndLoc = getLexer().getTok().getEndLoc();
      getLexer().Lex();
      Operands.push_back(BedrockOperand::createImm(
          MCConstantExpr::create(static_cast<int64_t>(Bits.getZExtValue()),
                                 getParser().getContext()),
          StartLoc, EndLoc,
          IsSingle ? BedrockOperand::ImmFP32Bits
                   : BedrockOperand::ImmFP64Bits));
      return false;
    }
  }

  if (getLexer().is(AsmToken::Identifier)) {
    StringRef Name = getLexer().getTok().getIdentifier();
    size_t At = Name.find('@');
    if (At != StringRef::npos) {
      StartLoc = getLexer().getTok().getLoc();
      StringRef SymbolName = Name.take_front(At);
      StringRef VariantName = Name.drop_front(At + 1);
      auto Variant =
          StringSwitch<BedrockOperand::ImmVariantKind>(VariantName.lower())
              .Case("abs64", BedrockOperand::ImmAbs64)
              .Case("pcrel32", BedrockOperand::ImmPCRel32)
              .Default(BedrockOperand::ImmNoVariant);
      if (SymbolName.empty() || Variant == BedrockOperand::ImmNoVariant)
        return Error(getLexer().getTok().getLoc(),
                     "invalid Bedrock symbol variant");

      EndLoc = getLexer().getTok().getEndLoc();
      getLexer().Lex();

      const MCExpr *Expr = MCSymbolRefExpr::create(
          getParser().getContext().getOrCreateSymbol(SymbolName),
          getParser().getContext());
      Operands.push_back(
          BedrockOperand::createImm(Expr, StartLoc, EndLoc, Variant));
      return false;
    }
  }

  if ((getLexer().is(AsmToken::Identifier) ||
       getLexer().is(AsmToken::String)) &&
      getLexer().peekTok().is(AsmToken::At)) {
    StartLoc = getLexer().getTok().getLoc();
    std::string SymbolName = getLexer().getTok().getIdentifier().str();
    bool IsQuoted = getLexer().is(AsmToken::String);
    getLexer().Lex();
    getLexer().Lex();

    if (!getLexer().is(AsmToken::Identifier))
      return Error(getLexer().getLoc(), "expected Bedrock symbol variant");

    StringRef VariantName = getLexer().getTok().getIdentifier();
    auto Variant =
        StringSwitch<BedrockOperand::ImmVariantKind>(VariantName.lower())
            .Case("abs64", BedrockOperand::ImmAbs64)
            .Case("pcrel32", BedrockOperand::ImmPCRel32)
            .Default(BedrockOperand::ImmNoVariant);
    if (Variant == BedrockOperand::ImmNoVariant)
      return Error(getLexer().getTok().getLoc(),
                   "invalid Bedrock symbol variant");

    EndLoc = getLexer().getTok().getEndLoc();
    getLexer().Lex();

    MCSymbol *Symbol = IsQuoted
                           ? getParser().getContext().parseSymbol(SymbolName)
                           : getParser().getContext().getOrCreateSymbol(
                                 SymbolName);
    const MCExpr *Expr =
        MCSymbolRefExpr::create(Symbol, getParser().getContext());
    Operands.push_back(
        BedrockOperand::createImm(Expr, StartLoc, EndLoc, Variant));
    return false;
  }

  bool NegateReal = false;
  bool HasRealSign =
      (getLexer().is(AsmToken::Minus) || getLexer().is(AsmToken::Plus)) &&
      getLexer().peekTok().is(AsmToken::Real);
  if (getLexer().is(AsmToken::Real) || HasRealSign) {
    StartLoc = getLexer().getTok().getLoc();
    if (HasRealSign) {
      NegateReal = getLexer().is(AsmToken::Minus);
      getLexer().Lex();
    }
    APFloat RealValue(APFloat::IEEEdouble(), getLexer().getTok().getString());
    if (NegateReal)
      RealValue.changeSign();
    EndLoc = getLexer().getTok().getEndLoc();
    getLexer().Lex();
    uint64_t Bits = RealValue.bitcastToAPInt().getZExtValue();
    Operands.push_back(BedrockOperand::createImm(
        MCConstantExpr::create(static_cast<int64_t>(Bits),
                               getParser().getContext()),
        StartLoc, EndLoc, BedrockOperand::ImmFP64Literal));
    return false;
  }

  if (getLexer().is(AsmToken::Integer) || getLexer().is(AsmToken::Minus) ||
      getLexer().is(AsmToken::Plus) || getLexer().is(AsmToken::Identifier) ||
      getLexer().is(AsmToken::String)) {
    const MCExpr *Expr = nullptr;
    StartLoc = getLexer().getTok().getLoc();
    if (getParser().parseExpression(Expr))
      return true;
    EndLoc = getLexer().getTok().getLoc();
    Operands.push_back(BedrockOperand::createImm(Expr, StartLoc, EndLoc));
    return false;
  }

  return true;
}

bool BedrockAsmParser::parseVectorStepMemoryOperand(OperandVector &Operands) {
  SMLoc Start = getLexer().getTok().getLoc();
  if (parseToken(AsmToken::LBrac, "expected '['"))
    return true;

  auto ParseReg = [&](MCRegister &Reg, SMLoc &RegStart, SMLoc &RegEnd) -> bool {
    return !tryParseRegister(Reg, RegStart, RegEnd).isSuccess();
  };
  auto PushReg = [&](MCRegister Reg, SMLoc RegStart, SMLoc RegEnd) {
    Operands.push_back(BedrockOperand::createReg(Reg, RegStart, RegEnd));
  };

  MCRegister First;
  SMLoc FirstStart;
  SMLoc FirstEnd;
  if (ParseReg(First, FirstStart, FirstEnd))
    return Error(getLexer().getLoc(), "expected register in vector address");

  unsigned FirstGPR;
  unsigned FirstVector;
  if (getVRegNo(First, FirstVector)) {
    if (parseToken(AsmToken::LBrac, "expected '[' before vector cursor"))
      return true;
    MCRegister Cursor;
    SMLoc CursorStart;
    SMLoc CursorEnd;
    unsigned CursorNo;
    if (ParseReg(Cursor, CursorStart, CursorEnd) || !getRegNo(Cursor, CursorNo))
      return Error(getLexer().getLoc(), "expected general register cursor");
    if (parseToken(AsmToken::RBrac, "expected ']' after vector cursor") ||
        parseToken(AsmToken::RBrac, "expected ']' after vector address"))
      return true;
    Operands.push_back(BedrockOperand::createToken("__step_vector", Start));
    PushReg(First, FirstStart, FirstEnd);
    PushReg(Cursor, CursorStart, CursorEnd);
    return false;
  }
  if (!getRegNo(First, FirstGPR))
    return Error(FirstStart, "expected general or vector address register");
  if (parseToken(AsmToken::Plus, "expected '+' in vector address"))
    return true;

  MCRegister Second;
  SMLoc SecondStart;
  SMLoc SecondEnd;
  if (ParseReg(Second, SecondStart, SecondEnd))
    return Error(getLexer().getLoc(), "expected vector address term");
  unsigned SecondGPR;
  unsigned SecondVector;
  if (getRegNo(Second, SecondGPR)) {
    if (parseToken(AsmToken::Star, "expected '*' before stride register"))
      return true;
    MCRegister Stride;
    SMLoc StrideStart;
    SMLoc StrideEnd;
    unsigned StrideNo;
    if (ParseReg(Stride, StrideStart, StrideEnd) || !getRegNo(Stride, StrideNo))
      return Error(getLexer().getLoc(), "expected general stride register");
    if (parseToken(AsmToken::RBrac, "expected ']' after vector address"))
      return true;
    Operands.push_back(
        BedrockOperand::createToken("__step_scalar_stride", Start));
    PushReg(First, FirstStart, FirstEnd);
    PushReg(Second, SecondStart, SecondEnd);
    PushReg(Stride, StrideStart, StrideEnd);
    return false;
  }
  if (!getVRegNo(Second, SecondVector))
    return Error(SecondStart, "expected general or vector address register");

  if (parseToken(AsmToken::LBrac, "expected '[' before vector cursor"))
    return true;
  MCRegister Cursor;
  SMLoc CursorStart;
  SMLoc CursorEnd;
  unsigned CursorNo;
  if (ParseReg(Cursor, CursorStart, CursorEnd) || !getRegNo(Cursor, CursorNo))
    return Error(getLexer().getLoc(), "expected general register cursor");
  if (parseToken(AsmToken::RBrac, "expected ']' after vector cursor"))
    return true;

  StringRef Marker = "__step_vector_base";
  if (parseOptionalToken(AsmToken::Star)) {
    if (!getLexer().is(AsmToken::Integer))
      return Error(getLexer().getLoc(), "expected constant vector scale");
    int64_t Scale = getLexer().getTok().getIntVal();
    StringRef Mnemonic =
        static_cast<const BedrockOperand &>(*Operands[0]).getToken();
    auto Parts = Mnemonic.rsplit('.');
    size_t Size = Parts.second.size() == 1
                      ? StringRef("bwlq").find(Parts.second.front())
                      : StringRef::npos;
    if (Size == StringRef::npos || Scale != (int64_t(1) << Size))
      return Error(getLexer().getLoc(), "vector scale does not match suffix");
    getLexer().Lex();
    Marker = "__step_vector_scaled";
  }

  const MCExpr *Disp = nullptr;
  SMLoc DispStart;
  SMLoc DispEnd;
  if (getLexer().is(AsmToken::Plus) || getLexer().is(AsmToken::Minus)) {
    bool IsMinus = getLexer().is(AsmToken::Minus);
    SMLoc SignLoc = getLexer().getTok().getLoc();
    getLexer().Lex();
    DispStart = getLexer().getTok().getLoc();
    if (getParser().parseExpression(Disp))
      return true;
    if (IsMinus)
      Disp = MCUnaryExpr::createMinus(Disp, getParser().getContext(), SignLoc);
    DispEnd = getLexer().getLoc();
    Marker = "__step_vector_disp";
  }
  if (parseToken(AsmToken::RBrac, "expected ']' after vector address"))
    return true;

  Operands.push_back(BedrockOperand::createToken(Marker, Start));
  PushReg(First, FirstStart, FirstEnd);
  PushReg(Second, SecondStart, SecondEnd);
  PushReg(Cursor, CursorStart, CursorEnd);
  if (Disp)
    Operands.push_back(BedrockOperand::createImm(Disp, DispStart, DispEnd));
  return false;
}

bool BedrockAsmParser::parseRegisterMaskOperand(OperandVector &Operands) {
  SMLoc StartLoc = getLexer().getTok().getLoc();
  if (parseToken(AsmToken::LCurly, "expected '{'"))
    return true;

  uint64_t Mask = 0;
  for (;;) {
    MCRegister FirstReg;
    SMLoc FirstStart;
    SMLoc FirstEnd;
    if (!tryParseRegister(FirstReg, FirstStart, FirstEnd).isSuccess())
      return Error(getLexer().getLoc(), "expected register in register mask");

    unsigned FirstNo;
    if (!getRegNo(FirstReg, FirstNo))
      return Error(FirstStart, "expected general register in register mask");

    unsigned LastNo = FirstNo;
    if (parseOptionalToken(AsmToken::Minus)) {
      MCRegister LastReg;
      SMLoc LastStart;
      SMLoc LastEnd;
      if (!tryParseRegister(LastReg, LastStart, LastEnd).isSuccess())
        return Error(getLexer().getLoc(), "expected register after '-'");
      if (!getRegNo(LastReg, LastNo))
        return Error(LastStart, "expected general register in register mask");
      if (LastNo < FirstNo)
        return Error(LastStart, "register mask range must be ascending");
    }

    for (unsigned RegNo = FirstNo; RegNo <= LastNo; ++RegNo)
      Mask |= uint64_t(1) << RegNo;

    if (!parseOptionalToken(AsmToken::Comma))
      break;
  }

  SMLoc EndLoc = getLexer().getTok().getEndLoc();
  if (parseToken(AsmToken::RCurly, "expected '}'"))
    return true;

  Operands.push_back(BedrockOperand::createImm(
      MCConstantExpr::create(Mask, getParser().getContext()), StartLoc,
      EndLoc));
  return false;
}

bool BedrockAsmParser::parseMemoryOperand(OperandVector &Operands) {
  SMLoc StartLoc = getLexer().getTok().getLoc();
  getLexer().Lex();

  BedrockOperand::MemBaseKind BaseKind = BedrockOperand::MemAbs;
  unsigned BaseReg = 0;
  const MCExpr *Disp = nullptr;
  bool HasDisp = false;
  BedrockOperand::MemUpdateKind BaseUpdate = BedrockOperand::MemNoUpdate;
  bool HasSegment = false;
  unsigned Segment = 0;
  bool HasIndex = false;
  unsigned IndexReg = 0;
  BedrockOperand::MemUpdateKind IndexUpdate = BedrockOperand::MemNoUpdate;
  bool VectorStride = false;

  auto ParseAutoUpdateReg = [&](unsigned &RegNo,
                                BedrockOperand::MemUpdateKind &Update,
                                bool &Matched) -> bool {
    Matched = false;
    Update = BedrockOperand::MemNoUpdate;

    bool IsPreDec = false;
    if (getLexer().is(AsmToken::Minus) &&
        getLexer().peekTok().is(AsmToken::Minus)) {
      getLexer().Lex();
      getLexer().Lex();
      IsPreDec = true;
    }

    MCRegister Reg;
    SMLoc RegStart;
    SMLoc RegEnd;
    ParseStatus RegStatus = tryParseRegister(Reg, RegStart, RegEnd);
    if (!RegStatus.isSuccess()) {
      if (IsPreDec)
        return true;
      return false;
    }

    if (!getRegNo(Reg, RegNo))
      return true;

    Matched = true;
    if (IsPreDec) {
      Update = BedrockOperand::MemPreDec;
      return false;
    }

    if (getLexer().is(AsmToken::Plus) &&
        getLexer().peekTok().is(AsmToken::Plus)) {
      getLexer().Lex();
      getLexer().Lex();
      Update = BedrockOperand::MemPostInc;
    }

    return false;
  };

  auto ParseDisplacement = [&](bool IsMinus, SMLoc SignLoc) -> bool {
    if (HasDisp)
      return Error(getLexer().getLoc(),
                   "multiple displacements in memory operand");

    const MCExpr *Expr = nullptr;
    if (getParser().parseExpression(Expr))
      return true;
    if (IsMinus)
      Expr = MCUnaryExpr::createMinus(Expr, getParser().getContext(), SignLoc);
    Disp = Expr;
    HasDisp = true;
    return false;
  };

  auto ParseIndexTerm = [&](bool &Matched) -> bool {
    Matched = false;
    BedrockOperand::MemUpdateKind Update;
    if (ParseAutoUpdateReg(IndexReg, Update, Matched))
      return true;
    if (!Matched)
      return false;

    if (HasIndex)
      return Error(getLexer().getLoc(), "multiple indexes in memory operand");

    HasIndex = true;
    IndexUpdate = Update;
    if (parseOptionalToken(AsmToken::Star)) {
      if (!getLexer().is(AsmToken::Identifier) ||
          !getLexer().getTok().getIdentifier().equals_insensitive("lane"))
        return Error(getLexer().getLoc(), "expected 'lane' after '*'");
      getLexer().Lex();
      VectorStride = true;
    }
    return false;
  };

  if (getLexer().is(AsmToken::Identifier) &&
      getLexer().peekTok().is(AsmToken::Colon)) {
    StringRef Name = getLexer().getTok().getIdentifier();
    if (getSRegNo(Name.lower(), Segment)) {
      HasSegment = true;
      getLexer().Lex();
      getLexer().Lex();
    }
  }

  if (HasSegment && getLexer().is(AsmToken::Integer)) {
    if (getLexer().getTok().getIntVal() == 0) {
      BaseKind = BedrockOperand::MemZero;
      getLexer().Lex();
    } else {
      BaseKind = BedrockOperand::MemZero;
      if (ParseDisplacement(/*IsMinus=*/false, SMLoc()))
        return true;
    }
  } else if (!HasSegment && getLexer().is(AsmToken::Integer) &&
             getLexer().getTok().getIntVal() == 0 &&
             getLexer().peekTok().is(AsmToken::Plus)) {
    BaseKind = BedrockOperand::MemZero;
    getLexer().Lex();
  } else if (getLexer().is(AsmToken::Identifier)) {
    StringRef Name = getLexer().getTok().getIdentifier();
    std::string Lower = Name.lower();
    if (Lower == "sp" || Lower == "pc") {
      BaseKind = Lower == "sp" ? BedrockOperand::MemSP : BedrockOperand::MemPC;
      getLexer().Lex();
    } else {
      bool Matched = false;
      if (ParseAutoUpdateReg(BaseReg, BaseUpdate, Matched))
        return true;
      if (Matched) {
        BaseKind = BedrockOperand::MemReg;
      } else if (HasSegment) {
        BaseKind = BedrockOperand::MemZero;
        if (ParseDisplacement(/*IsMinus=*/false, SMLoc()))
          return true;
      } else {
        BaseKind = BedrockOperand::MemAbs;
        if (ParseDisplacement(/*IsMinus=*/false, SMLoc()))
          return true;
      }
    }
  } else if (getLexer().is(AsmToken::Minus) &&
             getLexer().peekTok().is(AsmToken::Minus)) {
    bool Matched = false;
    if (ParseAutoUpdateReg(BaseReg, BaseUpdate, Matched))
      return true;
    if (!Matched)
      return true;
    BaseKind = BedrockOperand::MemReg;
  } else {
    BaseKind = HasSegment ? BedrockOperand::MemZero : BedrockOperand::MemAbs;
    if (ParseDisplacement(/*IsMinus=*/false, SMLoc()))
      return true;
  }

  while (getLexer().is(AsmToken::Plus) || getLexer().is(AsmToken::Minus)) {
    bool IsMinus = getLexer().is(AsmToken::Minus);
    SMLoc SignLoc = getLexer().getTok().getLoc();
    getLexer().Lex();
    if (!IsMinus) {
      bool MatchedIndex = false;
      if (ParseIndexTerm(MatchedIndex))
        return true;
      if (MatchedIndex)
        continue;
    }

    if (ParseDisplacement(IsMinus, SignLoc))
      return true;
  }

  SMLoc EndLoc = getLexer().getTok().getEndLoc();
  if (parseToken(AsmToken::RBrac, "expected ']'"))
    return true;

  Operands.push_back(BedrockOperand::createMem(
      BaseKind, BaseReg, Disp, HasDisp, StartLoc, EndLoc, BaseUpdate,
      HasSegment, Segment, HasIndex, IndexReg, IndexUpdate, VectorStride));
  return false;
}

bool BedrockAsmParser::parseLengthAnnotation(OperandVector &Operands) {
  SMLoc ColonLoc = getLexer().getLoc();
  getLexer().Lex();
  if (!getLexer().is(AsmToken::Identifier) ||
      !getLexer().getTok().getIdentifier().equals_insensitive("len"))
    return Error(getLexer().getLoc(), "expected LEN after ':'");
  getLexer().Lex();
  if (!getLexer().is(AsmToken::Integer))
    return Error(getLexer().getLoc(), "expected instruction length");

  int64_t RequestedLength = getLexer().getTok().getIntVal();
  SMLoc LengthLoc = getLexer().getTok().getLoc();
  SMLoc LengthEndLoc = getLexer().getTok().getEndLoc();
  getLexer().Lex();
  if (RequestedLength < 3 || RequestedLength > 18)
    return Error(LengthLoc, "instruction length must be between 3 and 18");

  SmallVector<std::unique_ptr<MCParsedAsmOperand>, 16> PaddingOperands;
  bool HasExplicitPadding = false;
  while (parseOptionalToken(AsmToken::Comma)) {
    HasExplicitPadding = true;
    int64_t Byte = -1;
    SMLoc ByteLoc = getLexer().getLoc();
    SMLoc ByteEndLoc = getLexer().getTok().getEndLoc();
    if (getLexer().is(AsmToken::Integer)) {
      Byte = getLexer().getTok().getIntVal();
    } else if (getLexer().is(AsmToken::Identifier)) {
      StringRef Name = getLexer().getTok().getIdentifier();
      if (Name.equals_insensitive("illegal"))
        Byte = 0;
      else if (Name.equals_insensitive("nop"))
        Byte = 1;
    }
    if (Byte < 0 || Byte > 255)
      return Error(ByteLoc,
                   "padding byte must be ILLEGAL, NOP, or an integer from 0 "
                   "through 255");
    PaddingOperands.push_back(BedrockOperand::createImm(
        MCConstantExpr::create(Byte, getParser().getContext()), ByteLoc,
        ByteEndLoc));
    getLexer().Lex();
  }

  SmallVector<std::unique_ptr<MCParsedAsmOperand>, 16> BodyOperands;
  for (auto &Operand : Operands)
    BodyOperands.push_back(std::move(Operand));
  Operands.clear();
  Operands.push_back(BedrockOperand::createToken("__len", ColonLoc));
  Operands.push_back(BedrockOperand::createImm(
      MCConstantExpr::create(RequestedLength, getParser().getContext()),
      LengthLoc, LengthEndLoc));
  int64_t PaddingCount =
      HasExplicitPadding ? static_cast<int64_t>(PaddingOperands.size()) : -1;
  Operands.push_back(BedrockOperand::createImm(
      MCConstantExpr::create(PaddingCount, getParser().getContext()), ColonLoc,
      ColonLoc));
  for (auto &Operand : PaddingOperands)
    Operands.push_back(std::move(Operand));
  for (auto &Operand : BodyOperands)
    Operands.push_back(std::move(Operand));
  return false;
}

bool BedrockAsmParser::isLabel(AsmToken &Token) {
  const AsmToken &Next = getLexer().peekTok();
  if (!Next.is(AsmToken::Identifier) ||
      !Next.getIdentifier().equals_insensitive("len"))
    return true;

  StringRef Name = Token.getIdentifier();
  return llvm::none_of(BedrockMC::scalarEncodingForms(),
                       [&](const BedrockMC::ScalarEncodingForm &Form) {
                         return Form.OperandCount == 0 &&
                                Name.equals_insensitive(Form.Mnemonic);
                       });
}

bool BedrockAsmParser::parseInstruction(ParseInstructionInfo &Info,
                                        StringRef Name, SMLoc NameLoc,
                                        OperandVector &Operands) {
  std::string Mnemonic = Name.lower();
  SMLoc MnemonicLoc = NameLoc;

  unsigned RepCond;
  if (getRepeatCondition(Mnemonic, RepCond)) {
    MCRegister Reg;
    SMLoc RegStart;
    SMLoc RegEnd;
    if (!tryParseRegister(Reg, RegStart, RegEnd).isSuccess())
      return Error(getLexer().getLoc(), "expected repeat counter register");

    unsigned RegNo;
    if (!getRegNo(Reg, RegNo))
      return Error(RegStart, "expected general repeat counter register");

    if (!parseOptionalToken(AsmToken::Comma))
      return Error(getLexer().getLoc(),
                   "expected ',' after repeat counter register");
    if (parseToken(AsmToken::LParen,
                   "expected '(' before repeated instruction"))
      return true;
    if (!getLexer().is(AsmToken::Identifier))
      return Error(getLexer().getLoc(), "expected repeated instruction");

    std::string BodyMnemonic = getLexer().getTok().getIdentifier().lower();
    SMLoc BodyLoc = getLexer().getTok().getLoc();
    getLexer().Lex();
    canonicalizeConditionMnemonic(BodyMnemonic);
    if (BodyMnemonic == "lea")
      BodyMnemonic = "lea.q";

    SmallVector<std::unique_ptr<MCParsedAsmOperand>, 8> BodyOperands;
    BodyOperands.push_back(BedrockOperand::createToken(BodyMnemonic, BodyLoc));
    if (getLexer().isNot(AsmToken::RParen)) {
      if (parseOperand(BodyOperands))
        return Error(getLexer().getLoc(), "expected repeated operand");
      while (parseOptionalToken(AsmToken::Comma)) {
        if (parseOperand(BodyOperands))
          return Error(getLexer().getLoc(), "expected repeated operand");
      }
    }

    if (getLexer().is(AsmToken::Colon) &&
        parseLengthAnnotation(BodyOperands))
      return true;

    if (parseToken(AsmToken::RParen, "expected ')' after repeated instruction"))
      return true;

    Operands.push_back(BedrockOperand::createToken("__rep", NameLoc));
    Operands.push_back(BedrockOperand::createImm(
        MCConstantExpr::create(RepCond, getParser().getContext()), NameLoc,
        NameLoc));
    Operands.push_back(BedrockOperand::createImm(
        MCConstantExpr::create(RegNo, getParser().getContext()), RegStart,
        RegEnd));
    for (auto &Operand : BodyOperands)
      Operands.push_back(std::move(Operand));

    if (getLexer().is(AsmToken::Colon) && parseLengthAnnotation(Operands))
      return true;
    if (getLexer().isNot(AsmToken::EndOfStatement)) {
      SMLoc Loc = getLexer().getLoc();
      getParser().eatToEndOfStatement();
      return Error(Loc, "unexpected token");
    }

    getParser().Lex();
    return false;
  }

  canonicalizeConditionMnemonic(Mnemonic);

  if (Mnemonic == "lea")
    Mnemonic = "lea.q";

  Operands.push_back(BedrockOperand::createToken(Mnemonic, MnemonicLoc));

  if (isAtomicOrderMnemonic(Mnemonic) && parseOptionalToken(AsmToken::Slash)) {
    if (!getLexer().is(AsmToken::Identifier))
      return Error(getLexer().getLoc(), "expected atomic memory order");

    unsigned OrderNo;
    SMLoc OrderLoc = getLexer().getTok().getLoc();
    StringRef OrderName = getLexer().getTok().getIdentifier();
    if (!getAtomicOrderNo(OrderName, OrderNo))
      return Error(OrderLoc, "invalid atomic memory order");

    Operands.push_back(BedrockOperand::createImm(
        MCConstantExpr::create(OrderNo, getParser().getContext()), OrderLoc,
        getLexer().getTok().getEndLoc()));
    getLexer().Lex();
  }

  if (getLexer().is(AsmToken::EndOfStatement))
    return false;

  if (getLexer().is(AsmToken::Colon)) {
    if (parseLengthAnnotation(Operands))
      return true;
    if (getLexer().isNot(AsmToken::EndOfStatement))
      return Error(getLexer().getLoc(), "unexpected token");
    getParser().Lex();
    return false;
  }

  bool IsVectorStep = StringRef(Mnemonic).starts_with("vgather1.") ||
                      StringRef(Mnemonic).starts_with("vscatter1.");
  auto ParseOperand = [&]() {
    if (IsVectorStep && getLexer().is(AsmToken::LBrac))
      return parseVectorStepMemoryOperand(Operands);
    return parseOperand(Operands);
  };

  if (ParseOperand())
    return Error(getLexer().getLoc(), "expected operand");

  while (parseOptionalToken(AsmToken::Comma)) {
    if (ParseOperand())
      return Error(getLexer().getLoc(), "expected operand");
  }

  if (getLexer().is(AsmToken::Colon) && parseLengthAnnotation(Operands))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    SMLoc Loc = getLexer().getLoc();
    getParser().eatToEndOfStatement();
    return Error(Loc, "unexpected token");
  }

  getParser().Lex();
  return false;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeBedrockAsmParser() {
  RegisterMCAsmParser<BedrockAsmParser> X(getTheBedrockTarget());
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "BedrockGenAsmMatcher.inc"
