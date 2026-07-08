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

  ParseStatus parseDirective(AsmToken DirectiveID) override {
    return ParseStatus::NoMatch;
  }

  bool parseOperand(OperandVector &Operands);
  bool parseMemoryOperand(OperandVector &Operands);

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

private:
  enum KindTy { TokenKind, RegKind, ImmKind, MemKind } Kind;

  std::string Tok;
  MCRegister Register = Bedrock::NoRegister;
  const MCExpr *Imm = nullptr;
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
  SMLoc Start;
  SMLoc End;

public:
  BedrockOperand(StringRef Tok, SMLoc Start)
      : Kind(TokenKind), Tok(Tok), Start(Start), End(Start) {}

  BedrockOperand(MCRegister Register, SMLoc Start, SMLoc End)
      : Kind(RegKind), Register(Register), Start(Start), End(End) {}

  BedrockOperand(const MCExpr *Imm, SMLoc Start, SMLoc End)
      : Kind(ImmKind), Imm(Imm), Start(Start), End(End) {}

  BedrockOperand(MemBaseKind BaseKind, unsigned BaseReg, const MCExpr *Disp,
                 bool HasDisp, SMLoc Start, SMLoc End,
                 MemUpdateKind BaseUpdate = MemNoUpdate,
                 bool HasSegment = false, unsigned Segment = 0,
                 bool HasIndex = false, unsigned IndexReg = 0,
                 MemUpdateKind IndexUpdate = MemNoUpdate)
      : Kind(MemKind), BaseKind(BaseKind), BaseReg(BaseReg), Disp(Disp),
        HasDisp(HasDisp), BaseUpdate(BaseUpdate), HasSegment(HasSegment),
        Segment(Segment), HasIndex(HasIndex), IndexReg(IndexReg),
        IndexUpdate(IndexUpdate), Start(Start), End(End) {}

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

  static std::unique_ptr<BedrockOperand> createImm(const MCExpr *Imm,
                                                   SMLoc Start, SMLoc End) {
    return std::make_unique<BedrockOperand>(Imm, Start, End);
  }

  static std::unique_ptr<BedrockOperand>
  createMem(MemBaseKind BaseKind, unsigned BaseReg, const MCExpr *Disp,
            bool HasDisp, SMLoc Start, SMLoc End,
            MemUpdateKind BaseUpdate = MemNoUpdate, bool HasSegment = false,
            unsigned Segment = 0, bool HasIndex = false, unsigned IndexReg = 0,
            MemUpdateKind IndexUpdate = MemNoUpdate) {
    return std::make_unique<BedrockOperand>(
        BaseKind, BaseReg, Disp, HasDisp, Start, End, BaseUpdate, HasSegment,
        Segment, HasIndex, IndexReg, IndexUpdate);
  }
};

bool getRegNo(MCRegister Reg, unsigned &RegNo) {
  static const MCRegister Regs[] = {
      Bedrock::R0,  Bedrock::R1,  Bedrock::R2,  Bedrock::R3,
      Bedrock::R4,  Bedrock::R5,  Bedrock::R6,  Bedrock::R7,
      Bedrock::R8,  Bedrock::R9,  Bedrock::R10, Bedrock::R11,
      Bedrock::R12, Bedrock::R13, Bedrock::R14, Bedrock::R15,
  };

  for (unsigned I = 0; I != std::size(Regs); ++I) {
    if (Reg == Regs[I]) {
      RegNo = I;
      return true;
    }
  }
  return false;
}

bool getConstantImm(const BedrockOperand &Op, int64_t &Value) {
  return Op.isImm() && Op.getImm()->evaluateAsAbsolute(Value);
}

const MCExpr *getImmExpr(const BedrockOperand &Op) {
  return Op.isImm() ? Op.getImm() : nullptr;
}

bool isToken(const BedrockOperand &Op, StringRef Token) {
  return Op.isToken() && Op.getToken() == Token;
}

bool getSRegNo(StringRef Name, unsigned &RegNo) {
  int Value = StringSwitch<int>(Name)
                  .Case("cs", 0)
                  .Case("ds", 1)
                  .Case("ss", 2)
                  .Case("gs0", 3)
                  .Case("gs1", 4)
                  .Case("gs2", 5)
                  .Case("gs3", 6)
                  .Case("gs4", 7)
                  .Default(-1);
  if (Value < 0)
    return false;
  RegNo = Value;
  return true;
}

bool getSRegNo(const BedrockOperand &Op, unsigned &RegNo) {
  return Op.isToken() && getSRegNo(Op.getToken(), RegNo);
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

void appendFixups(SmallVectorImpl<RawFixup> *OutFixups,
                  ArrayRef<RawFixup> Fixups) {
  if (OutFixups)
    OutFixups->append(Fixups.begin(), Fixups.end());
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

bool getConditionSuffix(StringRef Mnemonic, StringRef Base, unsigned &Cond) {
  if (!Mnemonic.consume_front(Base) || !Mnemonic.consume_front("."))
    return false;

  int Value = StringSwitch<int>(Mnemonic)
                  .Case("eq", 0x2)
                  .Case("ne", 0x3)
                  .Case("ult", 0x4)
                  .Case("uge", 0x5)
                  .Case("mi", 0x6)
                  .Case("pl", 0x7)
                  .Case("vs", 0x8)
                  .Case("vc", 0x9)
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

bool getPrefixBase(StringRef Mnemonic, uint8_t &Prefix, bool &NeedsReg) {
  NeedsReg = false;
  int Fixed = StringSwitch<int>(Mnemonic)
                  .Case("npx", 0x00)
                  .Case("nospec", 0x01)
                  .Case("saturate", 0x02)
                  .Case("nontemporal", 0x03)
                  .Case("u2c", 0x08)
                  .Case("c2u", 0x09)
                  .Case("u2u", 0x0a)
                  .Default(-1);
  if (Fixed >= 0) {
    Prefix = Fixed;
    return true;
  }

  int Cond = StringSwitch<int>(Mnemonic)
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
  if (Cond < 0)
    return false;

  Prefix = 0x80 | (Cond << 3);
  NeedsReg = true;
  return true;
}

bool encodeUnsignedTail(const BedrockOperand &Op, unsigned Width,
                        SmallVectorImpl<uint8_t> &Tail,
                        SmallVectorImpl<RawFixup> *Fixups = nullptr) {
  int64_t Value;
  if (getConstantImm(Op, Value)) {
    if (!isUIntN(Width * 8, Value))
      return false;
    appendLE(Tail, static_cast<uint64_t>(Value), Width);
    return true;
  }

  if (!Op.isImm())
    return false;
  return appendExprTail(Op.getImm(), Width, Tail, Fixups,
                        getDataFixupKind(Width));
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

  bool NeedsExt0 = Op.hasMemSegment() || Op.hasMemIndex() ||
                   Op.getMemBaseUpdate() != BedrockOperand::MemNoUpdate ||
                   Op.getMemBaseKind() == BedrockOperand::MemZero;
  if (!NeedsExt0)
    return false;

  SmallVector<uint8_t, 4> Descriptor;
  unsigned Segment = Op.hasMemSegment() ? Op.getMemSegment() : 1;
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

  if (Op.hasMemDisp()) {
    unsigned WidthCode;
    MCFixupKind Kind = Op.getMemBaseKind() == BedrockOperand::MemPC
                           ? MCFixupKind(Bedrock::fixup_bedrock_pcrel32)
                           : MCFixupKind(Bedrock::fixup_bedrock_disp32);
    if (!appendSignedAuto(Op.getMemDisp(), Tail, WidthCode, Fixups, Kind))
      return false;
    EA = 0x70 + WidthCode;
  } else {
    EA = 0x74;
  }

  return true;
}

bool encodeCompactEA(const BedrockOperand &Op, bool AllowImmediate, uint8_t &EA,
                     SmallVectorImpl<uint8_t> &Tail,
                     SmallVectorImpl<RawFixup> *Fixups = nullptr) {
  if (Op.isReg()) {
    unsigned RegNo;
    if (!getRegNo(Op.getReg(), RegNo))
      return false;
    EA = RegNo;
    return true;
  }

  if (isToken(Op, "sp")) {
    EA = 0x68;
    return true;
  }

  int64_t Value;
  unsigned WidthCode;
  if (Op.isImm()) {
    if (!AllowImmediate)
      return false;
    if (!appendSignedAuto(Op.getImm(), Tail, WidthCode, Fixups,
                          Bedrock::fixup_bedrock_imm32))
      return false;
    EA = 0x6c + WidthCode;
    return true;
  }

  if (!Op.isMem())
    return false;

  if (encodeExt0EA(Op, EA, Tail, Fixups))
    return true;

  switch (Op.getMemBaseKind()) {
  case BedrockOperand::MemReg:
    if (!Op.hasMemDisp()) {
      EA = 0x10 | Op.getMemBaseReg();
      return true;
    }
    if (!appendSignedAuto(Op.getMemDisp(), Tail, WidthCode, Fixups,
                          Bedrock::fixup_bedrock_disp32))
      return false;
    EA = 0x20 + (WidthCode << 4) + Op.getMemBaseReg();
    return true;
  case BedrockOperand::MemSP:
    if (!Op.hasMemDisp()) {
      EA = 0x69;
      return true;
    }
    if (!appendSignedAuto(Op.getMemDisp(), Tail, WidthCode, Fixups,
                          Bedrock::fixup_bedrock_disp32))
      return false;
    EA = 0x60 + WidthCode;
    return true;
  case BedrockOperand::MemPC:
    if (!Op.hasMemDisp() ||
        !appendSignedAuto(Op.getMemDisp(), Tail, WidthCode, Fixups,
                          Bedrock::fixup_bedrock_pcrel32))
      return false;
    EA = 0x64 + WidthCode;
    return true;
  case BedrockOperand::MemAbs:
    if (!Op.hasMemDisp())
      return false;
    if (Op.getMemDisp()->evaluateAsAbsolute(Value)) {
      if (!isIntN(32, Value)) {
        EA = 0x6b;
        appendLE(Tail, static_cast<uint64_t>(Value), 8);
        return true;
      }
      EA = 0x6a;
      appendLE(Tail, static_cast<uint64_t>(Value), 4);
      return true;
    }
    EA = 0x6a;
    return appendExprTail(Op.getMemDisp(), 4, Tail, Fixups, FK_Data_4);
  case BedrockOperand::MemZero:
    return false;
  }

  llvm_unreachable("unknown Bedrock memory base kind");
}

uint32_t applyPattern(StringRef Pattern, uint8_t EA, unsigned Z,
                      unsigned Reg = 0, char RegField = '\0', unsigned Reg2 = 0,
                      char RegField2 = '\0') {
  uint32_t Payload = 0;
  unsigned FieldBits[4] = {0, 0, 0, 0};

  for (char C : Pattern) {
    if (C == 'e')
      ++FieldBits[0];
    else if (C == 'z')
      ++FieldBits[1];
    else if (C == RegField)
      ++FieldBits[2];
    else if (C == RegField2)
      ++FieldBits[3];
  }

  unsigned Width = Pattern.size();
  unsigned EBit = FieldBits[0];
  unsigned ZBit = FieldBits[1];
  unsigned RBit = FieldBits[2];
  unsigned RBit2 = FieldBits[3];
  for (unsigned I = 0; I != Width; ++I) {
    unsigned Bit = Width - I - 1;
    switch (Pattern[I]) {
    case '0':
      break;
    case '1':
      Payload |= 1u << Bit;
      break;
    case 'e':
      Payload |= ((EA >> --EBit) & 1) << Bit;
      break;
    case 'z':
      Payload |= ((Z >> --ZBit) & 1) << Bit;
      break;
    case 's':
    case 'd':
    case 'r':
      if (Pattern[I] == RegField)
        Payload |= ((Reg >> --RBit) & 1) << Bit;
      else if (Pattern[I] == RegField2)
        Payload |= ((Reg2 >> --RBit2) & 1) << Bit;
      else
        llvm_unreachable("unexpected register field");
      break;
    default:
      llvm_unreachable("unexpected Bedrock bit-pattern field");
    }
  }
  return Payload;
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

bool getSizeSuffix(StringRef Mnemonic, StringRef Base, unsigned &Size) {
  if (!Mnemonic.consume_front(Base) || !Mnemonic.consume_front("."))
    return false;
  Size = StringSwitch<unsigned>(Mnemonic)
             .Case("b", 0)
             .Case("w", 1)
             .Case("l", 2)
             .Case("q", 3)
             .Default(4);
  return Size < 4;
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

void encodeShortPayload(uint16_t Payload, SmallVectorImpl<uint8_t> &Bytes) {
  Bytes.push_back((Payload >> 8) & 0x3f);
  Bytes.push_back(Payload & 0xff);
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

void applyPrefixFixupShift(SmallVectorImpl<RawFixup> &Fixups) {
  for (RawFixup &Fixup : Fixups) {
    if (Fixup.Offset >= 2)
      Fixup.Offset += 2;
  }
}

bool tryEncodeShortInstruction(OperandVector &Operands,
                               SmallVectorImpl<uint8_t> &Bytes) {
  if (Operands.empty() || !Operands[0]->isToken())
    return false;

  StringRef Mnemonic = static_cast<BedrockOperand &>(*Operands[0]).getToken();
  auto GetOp = [&](unsigned I) -> const BedrockOperand & {
    return static_cast<const BedrockOperand &>(*Operands[I]);
  };

  struct FixedForm {
    StringRef Mnemonic;
    uint16_t Payload;
  };
  static const FixedForm FixedForms[] = {
      {"nop", 0x2040},     {"ret", 0x2041},     {"lret", 0x2042},
      {"syscall", 0x2043}, {"sysret", 0x2044},  {"iret", 0x2045},
      {"bkpt", 0x2046},    {"wait", 0x2047},    {"yield", 0x2048},
      {"halt", 0x2049},    {"illegal", 0x204a}, {"rfence", 0x204b},
      {"wfence", 0x204c},  {"afence", 0x204d},  {"reset", 0x204e},
      {"trap", 0x2050},
  };

  if (Operands.size() == 1) {
    for (const FixedForm &Form : FixedForms) {
      if (Mnemonic == Form.Mnemonic) {
        encodeShortPayload(Form.Payload, Bytes);
        return true;
      }
    }

    unsigned Cond;
    if (getConditionSuffix(Mnemonic, "trap", Cond)) {
      encodeShortPayload(0x2050 | Cond, Bytes);
      return true;
    }
  }

  if (Operands.size() == 3 && GetOp(1).isReg() && GetOp(2).isReg()) {
    struct RRForm {
      StringRef Mnemonic;
      uint8_t Opcode;
    };
    static const RRForm RRForms[] = {
        {"mov.l", 0x00},   {"mov.q", 0x01},   {"add.l", 0x02},
        {"add.q", 0x03},   {"sub.l", 0x04},   {"sub.q", 0x05},
        {"cmp.l", 0x06},   {"cmp.q", 0x07},   {"and.l", 0x08},
        {"and.q", 0x09},   {"or.l", 0x0a},    {"or.q", 0x0b},
        {"xor.l", 0x0c},   {"xor.q", 0x0d},   {"test.l", 0x0e},
        {"test.q", 0x0f},  {"xchg.l", 0x10},  {"xchg.q", 0x11},
        {"shr.l", 0x12},   {"shr.q", 0x13},   {"shl.l", 0x14},
        {"shl.q", 0x15},   {"ror.l", 0x16},   {"ror.q", 0x17},
        {"rol.l", 0x18},   {"rol.q", 0x19},   {"sar.l", 0x1a},
        {"sar.q", 0x1b},   {"extzl.b", 0x1c}, {"extzl.w", 0x1d},
        {"extsl.b", 0x1e}, {"extsl.w", 0x1f}, {"extzq.l", 0x28},
        {"extsq.l", 0x29},
    };
    unsigned SrcReg;
    unsigned DstReg;
    if (!getRegNo(GetOp(1).getReg(), SrcReg) ||
        !getRegNo(GetOp(2).getReg(), DstReg))
      return false;
    for (const RRForm &Form : RRForms) {
      if (Mnemonic == Form.Mnemonic) {
        encodeShortPayload((Form.Opcode << 8) | (SrcReg << 4) | DstReg, Bytes);
        return true;
      }
    }
  }

  if (Operands.size() == 3 && Mnemonic == "mov.q") {
    unsigned RegNo;
    if (GetOp(1).isReg() && isToken(GetOp(2), "sp") &&
        getRegNo(GetOp(1).getReg(), RegNo)) {
      encodeShortPayload(0x2000 | RegNo, Bytes);
      return true;
    }
    if (isToken(GetOp(1), "sp") && GetOp(2).isReg() &&
        getRegNo(GetOp(2).getReg(), RegNo)) {
      encodeShortPayload(0x2010 | RegNo, Bytes);
      return true;
    }
  }

  if (Operands.size() == 2 && GetOp(1).isReg()) {
    unsigned RegNo;
    if (!getRegNo(GetOp(1).getReg(), RegNo))
      return false;

    if (Mnemonic == "push") {
      encodeShortPayload(0x2020 | RegNo, Bytes);
      return true;
    }
    if (Mnemonic == "pop") {
      encodeShortPayload(0x2030 | RegNo, Bytes);
      return true;
    }
    if (Mnemonic == "set") {
      encodeShortPayload(0x2100 | (RegNo << 4), Bytes);
      return true;
    }
    unsigned Cond;
    if (getConditionSuffix(Mnemonic, "set", Cond)) {
      encodeShortPayload(0x2100 | (RegNo << 4) | Cond, Bytes);
      return true;
    }

    struct UnaryForm {
      StringRef Mnemonic;
      uint16_t Prefix;
    };
    static const UnaryForm UnaryForms[] = {
        {"inc.l", 0x220},     {"inc.q", 0x230},     {"dec.l", 0x221},
        {"dec.q", 0x231},     {"neg.l", 0x222},     {"neg.q", 0x232},
        {"clr.l", 0x223},     {"clr.q", 0x233},     {"abs.l", 0x224},
        {"abs.q", 0x234},     {"not.l", 0x225},     {"not.q", 0x235},
        {"revbyte.w", 0x229}, {"revbyte.l", 0x22a}, {"revbyte.q", 0x22b},
    };
    for (const UnaryForm &Form : UnaryForms) {
      if (Mnemonic == Form.Mnemonic) {
        encodeShortPayload((Form.Prefix << 4) | RegNo, Bytes);
        return true;
      }
    }
  }

  if (Operands.size() == 3 && GetOp(1).isImm() && isToken(GetOp(2), "sp")) {
    int64_t Imm;
    if (!getConstantImm(GetOp(1), Imm))
      return false;
    if (Mnemonic == "add.q" && isUIntN(8, Imm)) {
      encodeShortPayload(0x2f00 | static_cast<uint8_t>(Imm), Bytes);
      return true;
    }
    if (Mnemonic == "sub.q" && isUIntN(8, Imm)) {
      encodeShortPayload(0x3100 | static_cast<uint8_t>(Imm), Bytes);
      return true;
    }
  }

  if (Operands.size() == 2 && GetOp(1).isImm()) {
    int64_t Imm;
    if (!getConstantImm(GetOp(1), Imm) || !isIntN(8, Imm))
      return false;
    if (Mnemonic == "jmp") {
      encodeShortPayload(0x3000 | static_cast<uint8_t>(Imm), Bytes);
      return true;
    }
    unsigned Cond;
    if (getConditionSuffix(Mnemonic, "j", Cond)) {
      encodeShortPayload(0x3000 | (Cond << 8) | static_cast<uint8_t>(Imm),
                         Bytes);
      return true;
    }
  }

  return false;
}

bool extractPrefixOperands(OperandVector &Operands, uint8_t &LowPrefix,
                           uint8_t &HighPrefix, OperandVector &RealOperands) {
  if (Operands.size() < 4)
    return false;

  unsigned Marker = Operands.size() - 3;
  const auto &MarkerOp = static_cast<const BedrockOperand &>(*Operands[Marker]);
  if (!MarkerOp.isToken() || MarkerOp.getToken() != "__prefix")
    return false;

  int64_t Low = 0;
  int64_t High = 0;
  if (!getConstantImm(
          static_cast<const BedrockOperand &>(*Operands[Marker + 1]), Low) ||
      !getConstantImm(
          static_cast<const BedrockOperand &>(*Operands[Marker + 2]), High) ||
      !isUIntN(8, Low) || !isUIntN(8, High))
    return false;

  LowPrefix = Low;
  HighPrefix = High;
  for (unsigned I = 0; I != Marker; ++I)
    RealOperands.push_back(std::move(Operands[I]));
  return true;
}

bool applyPrefixes(SmallVectorImpl<uint8_t> &Bytes, uint8_t LowPrefix,
                   uint8_t HighPrefix) {
  if (Bytes.size() < 2 || (Bytes[0] & 0x80))
    return false;

  Bytes[0] |= 0x80;
  Bytes.insert(Bytes.begin() + 2, HighPrefix);
  Bytes.insert(Bytes.begin() + 2, LowPrefix);
  return true;
}

bool encodeMediumSigned16Or32(uint32_t Payload16, uint32_t Payload32,
                              const BedrockOperand &Imm,
                              SmallVectorImpl<uint8_t> &Bytes,
                              SmallVectorImpl<RawFixup> *Fixups = nullptr) {
  SmallVector<uint8_t, 4> Tail;
  int64_t Value;
  if (!getConstantImm(Imm, Value)) {
    if (!Imm.isImm() || !appendExprTail(Imm.getImm(), 4, Tail, Fixups,
                                        Bedrock::fixup_bedrock_imm32))
      return false;
    return encodeMediumWithTail(Payload32, Tail, Bytes, Fixups);
  }

  if (isIntN(16, Value)) {
    appendLE(Tail, static_cast<uint64_t>(Value), 2);
    return encodeMediumWithTail(Payload16, Tail, Bytes);
  }
  if (isIntN(32, Value)) {
    appendLE(Tail, static_cast<uint64_t>(Value), 4);
    return encodeMediumWithTail(Payload32, Tail, Bytes);
  }
  return false;
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

    unsigned Cond = 0;
    uint32_t Payload = 0;
    bool IsCall = false;
    if (Mnemonic == "jmp") {
      Payload = 0x6600;
    } else if (getConditionSuffix(Mnemonic, "j", Cond)) {
      Payload = 0x6600 | Cond;
    } else if (Mnemonic == "call") {
      Payload = 0xe600;
      IsCall = true;
    } else if (getConditionSuffix(Mnemonic, "call", Cond)) {
      Payload = 0xe600 | Cond;
      IsCall = true;
    } else {
      return false;
    }

    SmallVector<uint8_t, 4> Tail(4, 0);
    if (!encodeMediumWithTail(Payload, Tail, Bytes))
      return false;

    const unsigned FixupOffset = 3;
    MCFixupKind Kind = IsCall ? MCFixupKind(Bedrock::fixup_bedrock_call32)
                              : MCFixupKind(Bedrock::fixup_bedrock_brdisp32);
    Fixups.push_back({FixupOffset, Kind, getImmExpr(GetOp(1))});
    return true;
  }

  return false;
}

bool tryEncodeMediumInstruction(OperandVector &Operands,
                                SmallVectorImpl<uint8_t> &Bytes,
                                SmallVectorImpl<RawFixup> *Fixups = nullptr) {
  if (Operands.empty() || !Operands[0]->isToken())
    return false;

  StringRef Mnemonic = static_cast<BedrockOperand &>(*Operands[0]).getToken();
  auto GetOp = [&](unsigned I) -> const BedrockOperand & {
    return static_cast<const BedrockOperand &>(*Operands[I]);
  };
  auto FinishMedium = [&](uint32_t Payload, ArrayRef<uint8_t> Tail,
                          SmallVectorImpl<RawFixup> &LocalFixups) -> bool {
    if (!encodeMediumWithTail(Payload, Tail, Bytes, &LocalFixups))
      return false;
    appendFixups(Fixups, LocalFixups);
    return true;
  };
  auto FinishLong = [&](uint32_t Payload, ArrayRef<uint8_t> Tail,
                        SmallVectorImpl<RawFixup> &LocalFixups) -> bool {
    if (!encodeLongWithTail(Payload, Tail, Bytes, &LocalFixups))
      return false;
    appendFixups(Fixups, LocalFixups);
    return true;
  };

  if (Operands.size() == 2) {
    struct UnaryForm {
      StringRef Base;
      StringRef BWPattern;
      StringRef LQPattern;
    };

    static const UnaryForm UnaryForms[] = {
        {"inc", "0eee10z0000000eeee", "0eee10z0001000eeee"},
        {"dec", "0eee10z0010000eeee", "0eee10z0011000eeee"},
        {"neg", "0eee10z0100000eeee", "0eee10z0101000eeee"},
        {"clr", "0eee10z0110000eeee", "0eee10z0111000eeee"},
        {"abs", "0eee10z1000000eeee", "0eee10z1001000eeee"},
        {"not", "0eee10z1010000eeee", "0eee10z1011000eeee"},
    };

    unsigned Size;
    for (const UnaryForm &Form : UnaryForms) {
      if (!getSizeSuffix(Mnemonic, Form.Base, Size))
        continue;
      SmallVector<uint8_t, 8> Tail;
      SmallVector<RawFixup, 2> LocalFixups;
      uint8_t EA;
      if (!encodeCompactEA(GetOp(1), /*AllowImmediate=*/false, EA, Tail,
                           &LocalFixups))
        return false;
      if (Size >= 2 && EA < 0x10)
        return false;
      StringRef Pattern = Size < 2 ? Form.BWPattern : Form.LQPattern;
      uint32_t Payload = applyPattern(Pattern, EA, Size & 1);
      return FinishMedium(Payload, Tail, LocalFixups);
    }

    if (getSizeSuffix(Mnemonic, "revbyte", Size) && Size != 0) {
      SmallVector<uint8_t, 8> Tail;
      SmallVector<RawFixup, 2> LocalFixups;
      uint8_t EA;
      if (!encodeCompactEA(GetOp(1), /*AllowImmediate=*/false, EA, Tail,
                           &LocalFixups) ||
          EA < 0x10)
        return false;
      StringRef Pattern = Size == 1   ? "0eee1001101000eeee"
                          : Size == 2 ? "0eee1001110000eeee"
                                      : "0eee1001111000eeee";
      uint32_t Payload = applyPattern(Pattern, EA, 0);
      return FinishMedium(Payload, Tail, LocalFixups);
    }
  }

  if (Operands.size() == 3) {
    enum class BinaryDir { RnEA, EARn };
    struct BinaryForm {
      StringRef Base;
      StringRef Pattern;
      StringRef Suffixes;
      BinaryDir Dir;
      bool RequireNonRegEA;
      bool DisallowSPDirect;
      bool AllowImmediateEA;
    };

    static const BinaryForm BinaryForms[] = {
        {"lea", "0eeez1zdddd000eeee", "bwlq", BinaryDir::EARn, false, false,
         true},
        {"mov", "000000zsssseeeeeee", "bw", BinaryDir::RnEA, false, true,
         false},
        {"mov", "000001zsssseeeeeee", "lq", BinaryDir::RnEA, true, true, false},
        {"mov", "000010zddddeeeeeee", "bw", BinaryDir::EARn, true, true, true},
        {"mov", "000011zddddeeeeeee", "lq", BinaryDir::EARn, true, true, true},
        {"add", "000100zsssseeeeeee", "bw", BinaryDir::RnEA, false, false,
         false},
        {"add", "000101zsssseeeeeee", "lq", BinaryDir::RnEA, true, true, false},
        {"add", "000110zddddeeeeeee", "bw", BinaryDir::EARn, true, false, true},
        {"add", "000111zddddeeeeeee", "lq", BinaryDir::EARn, true, true, true},
        {"sub", "001000zsssseeeeeee", "bw", BinaryDir::RnEA, false, false,
         false},
        {"sub", "001001zsssseeeeeee", "lq", BinaryDir::RnEA, true, true, false},
        {"sub", "001010zddddeeeeeee", "bw", BinaryDir::EARn, true, false, true},
        {"sub", "001011zddddeeeeeee", "lq", BinaryDir::EARn, true, true, true},
        {"and", "001100zsssseeeeeee", "bw", BinaryDir::RnEA, false, false,
         false},
        {"and", "001101zsssseeeeeee", "lq", BinaryDir::RnEA, true, true, false},
        {"and", "001110zddddeeeeeee", "bw", BinaryDir::EARn, true, false, true},
        {"and", "001111zddddeeeeeee", "lq", BinaryDir::EARn, true, true, true},
        {"or", "010000zsssseeeeeee", "bw", BinaryDir::RnEA, false, false,
         false},
        {"or", "010001zsssseeeeeee", "lq", BinaryDir::RnEA, true, true, false},
        {"or", "010010zddddeeeeeee", "bw", BinaryDir::EARn, true, false, true},
        {"or", "010011zddddeeeeeee", "lq", BinaryDir::EARn, true, true, true},
        {"xor", "010100zsssseeeeeee", "bw", BinaryDir::RnEA, false, false,
         false},
        {"xor", "010101zsssseeeeeee", "lq", BinaryDir::RnEA, true, true, false},
        {"xor", "010110zddddeeeeeee", "bw", BinaryDir::EARn, true, false, true},
        {"xor", "010111zddddeeeeeee", "lq", BinaryDir::EARn, true, true, true},
        {"test", "011000zsssseeeeeee", "bw", BinaryDir::RnEA, false, false,
         true},
        {"test", "011001zsssseeeeeee", "lq", BinaryDir::RnEA, true, true, true},
        {"test", "011010zddddeeeeeee", "bw", BinaryDir::EARn, true, false,
         true},
        {"test", "011011zddddeeeeeee", "lq", BinaryDir::EARn, true, true, true},
        {"cmp", "011100zsssseeeeeee", "bw", BinaryDir::RnEA, false, false,
         true},
        {"cmp", "011101zsssseeeeeee", "lq", BinaryDir::RnEA, true, true, true},
        {"cmp", "011110zddddeeeeeee", "bw", BinaryDir::EARn, true, false, true},
        {"cmp", "011111zddddeeeeeee", "lq", BinaryDir::EARn, true, true, true},
        {"xchg", "100000zsssseeeeeee", "bw", BinaryDir::RnEA, false, false,
         false},
        {"xchg", "100001zsssseeeeeee", "lq", BinaryDir::RnEA, true, true,
         false},
        {"xchg", "100010zddddeeeeeee", "bw", BinaryDir::EARn, true, false,
         false},
        {"xchg", "100011zddddeeeeeee", "lq", BinaryDir::EARn, true, true,
         false},
        {"rol", "100110zsssseeeeeee", "bw", BinaryDir::RnEA, false, false,
         false},
        {"rol", "100111zsssseeeeeee", "lq", BinaryDir::RnEA, true, false,
         false},
        {"ror", "101000zsssseeeeeee", "bw", BinaryDir::RnEA, false, false,
         false},
        {"ror", "101001zsssseeeeeee", "lq", BinaryDir::RnEA, true, false,
         false},
        {"shl", "101010zsssseeeeeee", "bw", BinaryDir::RnEA, false, false,
         false},
        {"shl", "101011zsssseeeeeee", "lq", BinaryDir::RnEA, true, false,
         false},
        {"shr", "101100zsssseeeeeee", "bw", BinaryDir::RnEA, false, false,
         false},
        {"shr", "101101zsssseeeeeee", "lq", BinaryDir::RnEA, true, false,
         false},
        {"sar", "101110zsssseeeeeee", "bw", BinaryDir::RnEA, false, false,
         false},
        {"sar", "101111zsssseeeeeee", "lq", BinaryDir::RnEA, true, false,
         false},
    };

    for (const BinaryForm &Form : BinaryForms) {
      unsigned Size;
      if (!getSizeSuffix(Mnemonic, Form.Base, Size))
        continue;
      char SizeChar = "bwlq"[Size];
      size_t SizeIndex = Form.Suffixes.find(SizeChar);
      if (SizeIndex == StringRef::npos)
        continue;

      unsigned RegNo;
      uint8_t EA;
      SmallVector<uint8_t, 8> Tail;
      SmallVector<RawFixup, 2> LocalFixups;
      if (Form.Dir == BinaryDir::RnEA) {
        if (!GetOp(1).isReg() || !getRegNo(GetOp(1).getReg(), RegNo) ||
            !encodeCompactEA(GetOp(2), Form.AllowImmediateEA, EA, Tail,
                             &LocalFixups))
          continue;
      } else {
        if (!encodeCompactEA(GetOp(1), Form.AllowImmediateEA, EA, Tail,
                             &LocalFixups) ||
            !GetOp(2).isReg() || !getRegNo(GetOp(2).getReg(), RegNo))
          continue;
      }

      if (Form.RequireNonRegEA && EA < 0x10)
        continue;
      if (Form.DisallowSPDirect && EA == 0x68)
        continue;

      char RegField = Form.Dir == BinaryDir::RnEA ? 's' : 'd';
      uint32_t Payload =
          applyPattern(Form.Pattern, EA, SizeIndex, RegNo, RegField);
      return FinishMedium(Payload, Tail, LocalFixups);
    }
  }

  if (Operands.size() == 3 && GetOp(1).isReg() && GetOp(2).isReg()) {
    unsigned Size;
    unsigned SrcReg;
    unsigned DstReg;
    if (getSizeSuffix(Mnemonic, "adc", Size) &&
        getRegNo(GetOp(1).getReg(), SrcReg) &&
        getRegNo(GetOp(2).getReg(), DstReg)) {
      uint32_t Payload =
          applyPattern("1100zz1ssss000dddd", 0, Size, SrcReg, 's', DstReg, 'd');
      return encodeMediumWithTail(Payload, {}, Bytes);
    }
    if (getSizeSuffix(Mnemonic, "sbb", Size) &&
        getRegNo(GetOp(1).getReg(), SrcReg) &&
        getRegNo(GetOp(2).getReg(), DstReg)) {
      uint32_t Payload =
          applyPattern("1101zz1ssss000dddd", 0, Size, SrcReg, 's', DstReg, 'd');
      return encodeMediumWithTail(Payload, {}, Bytes);
    }
  }

  if (Operands.size() == 3) {
    enum class ExtDir { RnEA, EARn };
    struct ExtForm {
      StringRef Mnemonic;
      StringRef Pattern;
      ExtDir Dir;
      bool RequireNonRegEA;
    };

    static const ExtForm ExtForms[] = {
        {"extsw.b", "1100000sssseeeeeee", ExtDir::RnEA, false},
        {"extsw.b", "1100001ddddeeeeeee", ExtDir::EARn, true},
        {"extsq.b", "1100010sssseeeeeee", ExtDir::RnEA, false},
        {"extsq.b", "1100011ddddeeeeeee", ExtDir::EARn, true},
        {"extsq.w", "1100100sssseeeeeee", ExtDir::RnEA, false},
        {"extsq.w", "1100101ddddeeeeeee", ExtDir::EARn, true},
        {"extsq.l", "1100110sssseeeeeee", ExtDir::RnEA, true},
        {"extsq.l", "1100111ddddeeeeeee", ExtDir::EARn, true},
        {"extzw.b", "1101000sssseeeeeee", ExtDir::RnEA, false},
        {"extzw.b", "1101001ddddeeeeeee", ExtDir::EARn, true},
        {"extzq.b", "1101010sssseeeeeee", ExtDir::RnEA, false},
        {"extzq.b", "1101011ddddeeeeeee", ExtDir::EARn, true},
        {"extzq.w", "1101100sssseeeeeee", ExtDir::RnEA, false},
        {"extzq.w", "1101101ddddeeeeeee", ExtDir::EARn, true},
        {"extzq.l", "1101110sssseeeeeee", ExtDir::RnEA, true},
        {"extzq.l", "1101111ddddeeeeeee", ExtDir::EARn, true},
        {"extsl.b", "1110000sssseeeeeee", ExtDir::RnEA, true},
        {"extsl.b", "1110001ddddeeeeeee", ExtDir::EARn, true},
        {"extsl.w", "1110010sssseeeeeee", ExtDir::RnEA, true},
        {"extsl.w", "1110011ddddeeeeeee", ExtDir::EARn, true},
        {"extzl.b", "1110100sssseeeeeee", ExtDir::RnEA, true},
        {"extzl.b", "1110101ddddeeeeeee", ExtDir::EARn, true},
        {"extzl.w", "1110110sssseeeeeee", ExtDir::RnEA, true},
        {"extzl.w", "1110111ddddeeeeeee", ExtDir::EARn, true},
    };

    for (const ExtForm &Form : ExtForms) {
      if (Mnemonic != Form.Mnemonic)
        continue;

      unsigned RegNo;
      uint8_t EA;
      SmallVector<uint8_t, 8> Tail;
      SmallVector<RawFixup, 2> LocalFixups;
      if (Form.Dir == ExtDir::RnEA) {
        if (!GetOp(1).isReg() || !getRegNo(GetOp(1).getReg(), RegNo) ||
            !encodeCompactEA(GetOp(2), /*AllowImmediate=*/false, EA, Tail,
                             &LocalFixups))
          continue;
      } else {
        if (!encodeCompactEA(GetOp(1), /*AllowImmediate=*/true, EA, Tail,
                             &LocalFixups) ||
            !GetOp(2).isReg() || !getRegNo(GetOp(2).getReg(), RegNo))
          continue;
      }
      if (Form.RequireNonRegEA && EA < 0x10)
        continue;

      char RegField = Form.Dir == ExtDir::RnEA ? 's' : 'd';
      uint32_t Payload = applyPattern(Form.Pattern, EA, 0, RegNo, RegField);
      return FinishMedium(Payload, Tail, LocalFixups);
    }
  }

  if (Operands.size() == 3) {
    enum class LongDir { RnEA, EARn };
    struct LongRegEAForm {
      StringRef Mnemonic;
      StringRef Pattern;
      StringRef Suffixes;
      LongDir Dir;
      char RegField;
      bool RequireNonRegEA;
      bool AllowImmediateEA;
    };

    static const LongRegEAForm LongRegEAForms[] = {
        {"adc", "1111000000zz000sssseeeeeee", "bwlq", LongDir::RnEA, 's', true,
         false},
        {"adc", "1111000000zz001sssseeeeeee", "bwlq", LongDir::EARn, 's', true,
         true},
        {"sbb", "1111000000zz010sssseeeeeee", "bwlq", LongDir::RnEA, 's', true,
         false},
        {"sbb", "1111000000zz011sssseeeeeee", "bwlq", LongDir::EARn, 's', true,
         true},
        {"clz", "1111000000zz100ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
         true},
        {"ctz", "1111000000zz101ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
         true},
        {"cls", "1111000000zz110ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
         true},
        {"cts", "1111000000zz111ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
         true},
        {"minu", "1111000001zz000ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
         false, true},
        {"minu", "1111000001zz001sssseeeeeee", "bwlq", LongDir::RnEA, 's', true,
         false},
        {"mins", "1111000001zz010ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
         false, true},
        {"mins", "1111000001zz011sssseeeeeee", "bwlq", LongDir::RnEA, 's', true,
         false},
        {"maxu", "1111000001zz100ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
         false, true},
        {"maxu", "1111000001zz101sssseeeeeee", "bwlq", LongDir::RnEA, 's', true,
         false},
        {"maxs", "1111000001zz110ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
         false, true},
        {"maxs", "1111000001zz111sssseeeeeee", "bwlq", LongDir::RnEA, 's', true,
         false},
        {"popcnt", "1111000010zz000ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
         false, true},
        {"parity", "1111000010zz001ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
         false, true},
        {"mul", "1111000010zz010ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
         true},
        {"clmul", "1111000010zz011ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
         false, true},
        {"divu", "1111000010zz100ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
         false, true},
        {"divs", "1111000010zz101ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
         false, true},
        {"modu", "1111000010zz110ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
         false, true},
        {"mods", "1111000010zz111ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
         false, true},
        {"btest", "111100001100000bbbbeeeeeee", "", LongDir::RnEA, 'b', false,
         true},
        {"bset", "111100001100001bbbbeeeeeee", "", LongDir::RnEA, 'b', false,
         false},
        {"bclr", "111100001100010bbbbeeeeeee", "", LongDir::RnEA, 'b', false,
         false},
        {"bchg", "111100001100011bbbbeeeeeee", "", LongDir::RnEA, 'b', false,
         false},
        {"lcall", "111100001100101rrrreeeeeee", "", LongDir::RnEA, 'r', false,
         true},
        {"ljmp", "111100001100110rrrreeeeeee", "", LongDir::RnEA, 'r', false,
         true},
        {"clmulh.q", "111100001100111ddddeeeeeee", "", LongDir::EARn, 'd',
         false, true},
    };

    for (const LongRegEAForm &Form : LongRegEAForms) {
      unsigned Z = 0;
      if (Form.Suffixes.empty()) {
        if (Mnemonic != Form.Mnemonic)
          continue;
      } else {
        unsigned Size;
        if (!getSizeSuffix(Mnemonic, Form.Mnemonic, Size))
          continue;
        Z = Size;
      }

      unsigned RegNo;
      uint8_t EA;
      SmallVector<uint8_t, 8> Tail;
      SmallVector<RawFixup, 2> LocalFixups;
      if (Form.Dir == LongDir::RnEA) {
        if (!GetOp(1).isReg() || !getRegNo(GetOp(1).getReg(), RegNo) ||
            !encodeCompactEA(GetOp(2), Form.AllowImmediateEA, EA, Tail,
                             &LocalFixups))
          continue;
      } else {
        if (!encodeCompactEA(GetOp(1), Form.AllowImmediateEA, EA, Tail,
                             &LocalFixups) ||
            !GetOp(2).isReg() || !getRegNo(GetOp(2).getReg(), RegNo))
          continue;
      }
      if (Form.RequireNonRegEA && EA < 0x10)
        continue;

      PatternFieldValue Fields[] = {
          {'e', EA},
          {'z', Z},
          {Form.RegField, RegNo},
      };
      uint32_t Payload = applyPatternValues(Form.Pattern, Fields);
      return FinishLong(Payload, Tail, LocalFixups);
    }
  }

  if (Operands.size() == 2) {
    unsigned Cond;
    if (getConditionSuffix(Mnemonic, "dj", Cond)) {
      uint8_t EA;
      SmallVector<uint8_t, 8> Tail;
      SmallVector<RawFixup, 2> LocalFixups;
      if (encodeCompactEA(GetOp(1), /*AllowImmediate=*/true, EA, Tail,
                          &LocalFixups)) {
        PatternFieldValue Fields[] = {{'c', Cond}, {'e', EA}};
        uint32_t Payload =
            applyPatternValues("111100001100100cccceeeeeee", Fields);
        return FinishLong(Payload, Tail, LocalFixups);
      }
    }

    struct LongEAOnlyForm {
      StringRef Mnemonic;
      StringRef Pattern;
      StringRef Suffixes;
      bool AllowImmediateEA;
    };

    static const LongEAOnlyForm LongEAOnlyForms[] = {
        {"seglea", "1111000100zz1100000eeeeeee", "bwlq", true},
        {"invpage", "1111101111010000110eeeeeee", "", true},
        {"flshdcache", "1111101111010000111eeeeeee", "", true},
        {"invdcache", "1111101111010001000eeeeeee", "", true},
        {"invicache", "1111101111010001001eeeeeee", "", true},
        {"prefetch", "1111101111010001010eeeeeee", "", true},
        {"synccache", "1111101111010001011eeeeeee", "", true},
        {"wrbkdcache", "1111101111010001100eeeeeee", "", true},
        {"save", "1111101111010001101eeeeeee", "", true},
        {"restore", "1111101111010001110eeeeeee", "", true},
    };

    for (const LongEAOnlyForm &Form : LongEAOnlyForms) {
      unsigned Z = 0;
      if (Form.Suffixes.empty()) {
        if (Mnemonic != Form.Mnemonic)
          continue;
      } else {
        unsigned Size;
        if (!getSizeSuffix(Mnemonic, Form.Mnemonic, Size))
          continue;
        Z = Size;
      }

      uint8_t EA;
      SmallVector<uint8_t, 8> Tail;
      SmallVector<RawFixup, 2> LocalFixups;
      if (!encodeCompactEA(GetOp(1), Form.AllowImmediateEA, EA, Tail,
                           &LocalFixups))
        continue;
      PatternFieldValue Fields[] = {{'e', EA}, {'z', Z}};
      uint32_t Payload = applyPatternValues(Form.Pattern, Fields);
      return FinishLong(Payload, Tail, LocalFixups);
    }

    struct LongRegOnlyForm {
      StringRef Base;
      StringRef Pattern;
      StringRef Suffixes;
      char RegField;
    };

    static const LongRegOnlyForm LongRegOnlyForms[] = {
        {"incn", "1111000100zz1100001000rrrr", "bwlq", 'r'},
        {"decn", "1111000100zz1100001001rrrr", "bwlq", 'r'},
    };

    for (const LongRegOnlyForm &Form : LongRegOnlyForms) {
      unsigned Size;
      unsigned RegNo;
      if (getSizeSuffix(Mnemonic, Form.Base, Size) && GetOp(1).isReg() &&
          getRegNo(GetOp(1).getReg(), RegNo)) {
        PatternFieldValue Fields[] = {{'z', Size}, {Form.RegField, RegNo}};
        uint32_t Payload = applyPatternValues(Form.Pattern, Fields);
        return encodeLongWithTail(Payload, {}, Bytes);
      }
    }
  }

  if (Operands.size() == 3) {
    struct LongEAEAForm {
      StringRef Mnemonic;
      StringRef Pattern;
      StringRef Suffixes;
      bool AllowDestImmediate;
    };

    static const LongEAEAForm LongEAEAForms[] = {
        {"mov", "1111100000zzsssssssddddddd", "bwlq", false},
        {"cmp", "1111100001zzsssssssddddddd", "bwlq", true},
        {"extsw.b", "111110100000sssssssddddddd", "", false},
        {"extsq.b", "111110100001sssssssddddddd", "", false},
        {"extsq.w", "111110100010sssssssddddddd", "", false},
        {"extsq.l", "111110100011sssssssddddddd", "", false},
        {"extzw.b", "111110100100sssssssddddddd", "", false},
        {"extzq.b", "111110100101sssssssddddddd", "", false},
        {"extzq.w", "111110100110sssssssddddddd", "", false},
        {"extzq.l", "111110100111sssssssddddddd", "", false},
        {"extsl", "11111010100zsssssssddddddd", "bw", false},
        {"extzl", "11111010101zsssssssddddddd", "bw", false},
    };

    for (const LongEAEAForm &Form : LongEAEAForms) {
      unsigned Z = 0;
      if (Form.Suffixes.empty()) {
        if (Mnemonic != Form.Mnemonic)
          continue;
      } else {
        unsigned Size;
        if (!getSizeSuffix(Mnemonic, Form.Mnemonic, Size))
          continue;
        char SizeChar = "bwlq"[Size];
        size_t SizeIndex = Form.Suffixes.find(SizeChar);
        if (SizeIndex == StringRef::npos)
          continue;
        Z = SizeIndex;
      }

      SmallVector<uint8_t, 16> Tail;
      SmallVector<RawFixup, 4> LocalFixups;
      uint8_t SrcEA;
      uint8_t DstEA;
      if (!encodeCompactEA(GetOp(1), /*AllowImmediate=*/true, SrcEA, Tail,
                           &LocalFixups) ||
          !encodeCompactEA(GetOp(2), Form.AllowDestImmediate, DstEA, Tail,
                           &LocalFixups))
        continue;
      if (SrcEA < 0x10 || DstEA < 0x10)
        continue;

      PatternFieldValue Fields[] = {{'s', SrcEA}, {'d', DstEA}, {'z', Z}};
      uint32_t Payload = applyPatternValues(Form.Pattern, Fields);
      return FinishLong(Payload, Tail, LocalFixups);
    }
  }

  if (Operands.size() == 3 || Operands.size() == 4) {
    struct LongImmEAForm {
      StringRef Base;
      StringRef Pattern;
      StringRef Suffixes;
      unsigned ImmBits;
      bool HasRegDst;
      bool AllowDestImmediate;
    };

    static const LongImmEAForm LongImmEAForms[] = {
        {"rol", "1111101100zz0iiiiiieeeeeee", "bwlq", 6, false, false},
        {"ror", "1111101100zz1iiiiiieeeeeee", "bwlq", 6, false, false},
        {"shl", "1111101101zz0iiiiiieeeeeee", "bwlq", 6, false, false},
        {"shr", "1111101101zz1iiiiiieeeeeee", "bwlq", 6, false, false},
        {"sar", "1111101110zz0iiiiiieeeeeee", "bwlq", 6, false, false},
        {"btest", "1111101110001iiiiiieeeeeee", "", 6, false, true},
        {"bset", "1111101110011iiiiiieeeeeee", "", 6, false, false},
        {"bclr", "1111101110101iiiiiieeeeeee", "", 6, false, false},
        {"bchg", "1111101110111iiiiiieeeeeee", "", 6, false, false},
        {"ptquery", "111110111100iiiddddeeeeeee", "", 3, true, true},
    };

    for (const LongImmEAForm &Form : LongImmEAForms) {
      if (Form.HasRegDst && Operands.size() != 4)
        continue;
      if (!Form.HasRegDst && Operands.size() != 3)
        continue;

      unsigned Z = 0;
      if (Form.Suffixes.empty()) {
        if (Mnemonic != Form.Base)
          continue;
      } else {
        unsigned Size;
        if (!getSizeSuffix(Mnemonic, Form.Base, Size))
          continue;
        Z = Size;
      }

      int64_t ImmValue;
      if (!getConstantImm(GetOp(1), ImmValue) ||
          !isUIntN(Form.ImmBits, ImmValue))
        continue;

      SmallVector<uint8_t, 8> Tail;
      SmallVector<RawFixup, 2> LocalFixups;
      uint8_t EA;
      if (!encodeCompactEA(GetOp(2), Form.AllowDestImmediate, EA, Tail,
                           &LocalFixups))
        continue;

      unsigned RegNo = 0;
      if (Form.HasRegDst &&
          (!GetOp(3).isReg() || !getRegNo(GetOp(3).getReg(), RegNo)))
        continue;

      PatternFieldValue Fields[] = {{'i', static_cast<unsigned>(ImmValue)},
                                    {'e', EA},
                                    {'z', Z},
                                    {'d', RegNo}};
      uint32_t Payload = applyPatternValues(Form.Pattern, Fields);
      return FinishLong(Payload, Tail, LocalFixups);
    }
  }

  if (Operands.size() == 3) {
    unsigned RegA;
    unsigned RegB;
    if (Mnemonic == "vtop" && GetOp(1).isReg() && GetOp(2).isReg() &&
        getRegNo(GetOp(1).getReg(), RegA) &&
        getRegNo(GetOp(2).getReg(), RegB)) {
      PatternFieldValue Fields[] = {{'v', RegA}, {'p', RegB}};
      uint32_t Payload =
          applyPatternValues("111110111101000000vvvvpppp", Fields);
      return encodeLongWithTail(Payload, {}, Bytes);
    }
    if (Mnemonic == "swpta" && GetOp(1).isReg() && GetOp(2).isReg() &&
        getRegNo(GetOp(1).getReg(), RegA) &&
        getRegNo(GetOp(2).getReg(), RegB)) {
      PatternFieldValue Fields[] = {{'p', RegA}, {'a', RegB}};
      uint32_t Payload =
          applyPatternValues("111110111101000001ppppaaaa", Fields);
      return encodeLongWithTail(Payload, {}, Bytes);
    }
    if (Mnemonic == "rdseg" && GetOp(2).isReg() && getSRegNo(GetOp(1), RegA) &&
        getRegNo(GetOp(2).getReg(), RegB)) {
      PatternFieldValue Fields[] = {{'s', RegA}, {'d', RegB}};
      uint32_t Payload =
          applyPatternValues("1111101111010000100sssdddd", Fields);
      return encodeLongWithTail(Payload, {}, Bytes);
    }
    if (Mnemonic == "wrseg" && GetOp(1).isReg() &&
        getRegNo(GetOp(1).getReg(), RegA) && getSRegNo(GetOp(2), RegB)) {
      PatternFieldValue Fields[] = {{'d', RegA}, {'s', RegB}};
      uint32_t Payload =
          applyPatternValues("1111101111010000101sssdddd", Fields);
      return encodeLongWithTail(Payload, {}, Bytes);
    }
  }

  if (Operands.size() == 3) {
    struct LongImm16RegForm {
      StringRef Mnemonic;
      StringRef Pattern;
      bool ImmFirst;
      char RegField;
    };
    static const LongImm16RegForm Forms[] = {
        {"rdcr", "1111101111010010000000dddd", true, 'd'},
        {"wrcr", "1111101111010010000001ssss", false, 's'},
        {"rdpmc", "1111101111010010001010dddd", true, 'd'},
    };
    for (const LongImm16RegForm &Form : Forms) {
      if (Mnemonic != Form.Mnemonic)
        continue;
      const BedrockOperand &ImmOp = Form.ImmFirst ? GetOp(1) : GetOp(2);
      const BedrockOperand &RegOp = Form.ImmFirst ? GetOp(2) : GetOp(1);
      SmallVector<uint8_t, 2> Tail;
      SmallVector<RawFixup, 1> LocalFixups;
      unsigned RegNo;
      if (encodeUnsignedTail(ImmOp, 2, Tail, &LocalFixups) && RegOp.isReg() &&
          getRegNo(RegOp.getReg(), RegNo)) {
        PatternFieldValue Fields[] = {{Form.RegField, RegNo}};
        uint32_t Payload = applyPatternValues(Form.Pattern, Fields);
        return FinishLong(Payload, Tail, LocalFixups);
      }
    }
  }

  if (Operands.size() == 2) {
    struct LongSysRegForm {
      StringRef Mnemonic;
      StringRef Pattern;
      char RegField;
    };
    static const LongSysRegForm Forms[] = {
        {"rdflags", "1111101111010010000010dddd", 'd'},
        {"wrflags", "1111101111010010000011ssss", 's'},
        {"rdfflags", "1111101111010010000100dddd", 'd'},
        {"wrfflags", "1111101111010010000101ssss", 's'},
        {"rdstatus", "1111101111010010000110dddd", 'd'},
        {"wrstatus", "1111101111010010000111ssss", 's'},
        {"rdfstatus", "1111101111010010001000dddd", 'd'},
        {"wrfstatus", "1111101111010010001001ssss", 's'},
        {"swpt", "1111101111010010001011pppp", 'p'},
    };
    for (const LongSysRegForm &Form : Forms) {
      unsigned RegNo;
      if (Mnemonic == Form.Mnemonic && GetOp(1).isReg() &&
          getRegNo(GetOp(1).getReg(), RegNo)) {
        PatternFieldValue Fields[] = {{Form.RegField, RegNo}};
        uint32_t Payload = applyPatternValues(Form.Pattern, Fields);
        return encodeLongWithTail(Payload, {}, Bytes);
      }
    }
    if (Mnemonic == "invasid") {
      SmallVector<uint8_t, 2> Tail;
      SmallVector<RawFixup, 1> LocalFixups;
      if (encodeUnsignedTail(GetOp(1), 2, Tail, &LocalFixups))
        return FinishLong(0x3ef4f00, Tail, LocalFixups);
    }
  }

  if (Operands.size() == 1 && Mnemonic == "invtlb")
    return encodeLongWithTail(0x3ef4f01, {}, Bytes);

  if (Operands.size() == 3 && GetOp(1).isReg() && GetOp(2).isReg()) {
    struct LongQRRForm {
      StringRef Mnemonic;
      StringRef Pattern;
    };
    static const LongQRRForm Forms[] = {
        {"mulhu.q", "111110111110000000ssssdddd"},
        {"mulhs.q", "111110111110000001ssssdddd"},
        {"mulhsu.q", "111110111110000010ssssdddd"},
    };
    for (const LongQRRForm &Form : Forms) {
      unsigned SrcReg;
      unsigned DstReg;
      if (Mnemonic == Form.Mnemonic && getRegNo(GetOp(1).getReg(), SrcReg) &&
          getRegNo(GetOp(2).getReg(), DstReg)) {
        PatternFieldValue Fields[] = {{'s', SrcReg}, {'d', DstReg}};
        uint32_t Payload = applyPatternValues(Form.Pattern, Fields);
        return encodeLongWithTail(Payload, {}, Bytes);
      }
    }
  }

  if (Operands.size() == 2 && GetOp(1).isImm()) {
    unsigned Cond = 0;
    if (Mnemonic == "jmp")
      return encodeMediumSigned16Or32(0x2600, 0x6600, GetOp(1), Bytes);
    if (getConditionSuffix(Mnemonic, "j", Cond))
      return encodeMediumSigned16Or32(0x2600 | Cond, 0x6600 | Cond, GetOp(1),
                                      Bytes);
    if (Mnemonic == "call")
      return encodeMediumSigned16Or32(0xa600, 0xe600, GetOp(1), Bytes);
    if (getConditionSuffix(Mnemonic, "call", Cond))
      return encodeMediumSigned16Or32(0xa600 | Cond, 0xe600 | Cond, GetOp(1),
                                      Bytes);

    if (Mnemonic == "pushm") {
      SmallVector<uint8_t, 2> Tail;
      SmallVector<RawFixup, 1> LocalFixups;
      if (encodeUnsignedTail(GetOp(1), 2, Tail, &LocalFixups))
        return FinishMedium(0x2784, Tail, LocalFixups);
    }
    if (Mnemonic == "popm") {
      SmallVector<uint8_t, 2> Tail;
      SmallVector<RawFixup, 1> LocalFixups;
      if (encodeUnsignedTail(GetOp(1), 2, Tail, &LocalFixups))
        return FinishMedium(0x2785, Tail, LocalFixups);
    }
    if (Mnemonic == "trace") {
      SmallVector<uint8_t, 2> Tail;
      SmallVector<RawFixup, 1> LocalFixups;
      if (encodeUnsignedTail(GetOp(1), 2, Tail, &LocalFixups))
        return FinishMedium(0x2786, Tail, LocalFixups);
    }
  }

  if (Operands.size() == 2 && GetOp(1).isReg()) {
    unsigned RegNo;
    if (Mnemonic == "cpuid" && getRegNo(GetOp(1).getReg(), RegNo))
      return encodeMediumWithTail(0x2700 | RegNo, {}, Bytes);
  }

  if (Operands.size() == 3 && GetOp(1).isReg() && GetOp(2).isImm()) {
    unsigned RegNo;
    SmallVector<uint8_t, 2> Tail;
    SmallVector<RawFixup, 1> LocalFixups;
    if (Mnemonic == "repg" && getRegNo(GetOp(1).getReg(), RegNo) &&
        encodeUnsignedTail(GetOp(2), 2, Tail, &LocalFixups))
      return FinishMedium(0x2680 | RegNo, Tail, LocalFixups);
  }

  if (Operands.size() == 3 && GetOp(1).isImm() && isToken(GetOp(2), "sp")) {
    if (Mnemonic == "add.q") {
      SmallVector<RawFixup, 1> LocalFixups;
      if (!encodeMediumSigned16Or32(0x2780, 0x2781, GetOp(1), Bytes,
                                    &LocalFixups))
        return false;
      appendFixups(Fixups, LocalFixups);
      return true;
    }
    if (Mnemonic == "sub.q") {
      SmallVector<RawFixup, 1> LocalFixups;
      if (!encodeMediumSigned16Or32(0x2782, 0x2783, GetOp(1), Bytes,
                                    &LocalFixups))
        return false;
      appendFixups(Fixups, LocalFixups);
      return true;
    }
  }

  if (Operands.size() == 3 && GetOp(1).isImm() && GetOp(2).isReg() &&
      Mnemonic.starts_with("sum.")) {
    unsigned Size = StringSwitch<unsigned>(Mnemonic.drop_front(4))
                        .Case("b", 0)
                        .Case("w", 1)
                        .Case("l", 2)
                        .Case("q", 3)
                        .Default(4);
    unsigned RegNo;
    SmallVector<uint8_t, 2> Tail;
    SmallVector<RawFixup, 1> LocalFixups;
    if (Size < 4 && getRegNo(GetOp(2).getReg(), RegNo) &&
        encodeUnsignedTail(GetOp(1), 2, Tail, &LocalFixups))
      return FinishMedium(0x12600 | (Size << 14) | RegNo, Tail, LocalFixups);
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
  uint8_t LowPrefix = 0;
  uint8_t HighPrefix = 0;
  SmallVector<std::unique_ptr<MCParsedAsmOperand>, 8> RealOperands;
  if (extractPrefixOperands(Operands, LowPrefix, HighPrefix, RealOperands)) {
    SmallVector<uint8_t, 16> RawBytes;
    SmallVector<RawFixup, 4> RawFixups;
    if ((tryEncodeShortInstruction(RealOperands, RawBytes) ||
         tryEncodeSymbolicInstruction(RealOperands, RawBytes, RawFixups) ||
         tryEncodeMediumInstruction(RealOperands, RawBytes, &RawFixups)) &&
        applyPrefixes(RawBytes, LowPrefix, HighPrefix)) {
      applyPrefixFixupShift(RawFixups);
      createRawInst(RawBytes, RawFixups, Inst);
      Inst.setLoc(IDLoc);
      Out.emitInstruction(Inst, *STI);
      return false;
    }
    return Error(IDLoc, "invalid prefixed instruction");
  }

  unsigned MatchResult =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm);

  if (MatchResult == Match_Success) {
    Inst.setLoc(IDLoc);
    Out.emitInstruction(Inst, *STI);
    return false;
  }

  SmallVector<uint8_t, 16> RawBytes;
  SmallVector<RawFixup, 4> RawFixups;
  if (tryEncodeSymbolicInstruction(Operands, RawBytes, RawFixups)) {
    createRawInst(RawBytes, RawFixups, Inst);
    Inst.setLoc(IDLoc);
    Out.emitInstruction(Inst, *STI);
    return false;
  }

  if (tryEncodeMediumInstruction(Operands, RawBytes, &RawFixups)) {
    createRawInst(RawBytes, RawFixups, Inst);
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
  Reg = MatchRegisterName(Name.lower());
  if (Reg == Bedrock::NoRegister)
    return ParseStatus::NoMatch;

  StartLoc = getLexer().getTok().getLoc();
  EndLoc = getLexer().getTok().getEndLoc();
  getLexer().Lex();
  return ParseStatus::Success;
}

bool BedrockAsmParser::parseOperand(OperandVector &Operands) {
  if (getLexer().is(AsmToken::LBrac))
    return parseMemoryOperand(Operands);

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
        Lower == "gs3" || Lower == "gs4") {
      StartLoc = getLexer().getTok().getLoc();
      Operands.push_back(BedrockOperand::createToken(Lower, StartLoc));
      getLexer().Lex();
      return false;
    }
  }

  if (getLexer().is(AsmToken::Integer) || getLexer().is(AsmToken::Minus) ||
      getLexer().is(AsmToken::Plus) || getLexer().is(AsmToken::Identifier)) {
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
      HasSegment, Segment, HasIndex, IndexReg, IndexUpdate));
  return false;
}

bool BedrockAsmParser::parseInstruction(ParseInstructionInfo &Info,
                                        StringRef Name, SMLoc NameLoc,
                                        OperandVector &Operands) {
  SmallVector<uint8_t, 2> Prefixes;
  std::string Mnemonic = Name.lower();
  SMLoc MnemonicLoc = NameLoc;
  auto AppendPrefixOperands = [&]() {
    if (Prefixes.empty())
      return;

    Operands.push_back(BedrockOperand::createToken("__prefix", NameLoc));
    Operands.push_back(BedrockOperand::createImm(
        MCConstantExpr::create(Prefixes[0], getParser().getContext()), NameLoc,
        NameLoc));
    Operands.push_back(BedrockOperand::createImm(
        MCConstantExpr::create(Prefixes.size() > 1 ? Prefixes[1] : 0,
                               getParser().getContext()),
        NameLoc, NameLoc));
  };

  for (;;) {
    uint8_t Prefix = 0;
    bool NeedsReg = false;
    if (!getPrefixBase(Mnemonic, Prefix, NeedsReg))
      break;

    if (Prefixes.size() == 2)
      return Error(MnemonicLoc, "too many instruction prefixes");

    if (NeedsReg) {
      MCRegister Reg;
      SMLoc RegStart;
      SMLoc RegEnd;
      if (!tryParseRegister(Reg, RegStart, RegEnd).isSuccess())
        return Error(getLexer().getLoc(), "expected repeat counter register");

      unsigned RegNo;
      if (!getRegNo(Reg, RegNo) || RegNo > 7)
        return Error(RegStart, "repeat counter must be r0-r7");

      Prefix |= RegNo;
    }

    Prefixes.push_back(Prefix);

    if (!parseOptionalToken(AsmToken::Comma))
      return Error(getLexer().getLoc(),
                   "expected ',' after instruction prefix");
    if (!getLexer().is(AsmToken::Identifier))
      return Error(getLexer().getLoc(), "expected instruction after prefix");

    MnemonicLoc = getLexer().getTok().getLoc();
    Mnemonic = getLexer().getTok().getIdentifier().lower();
    getLexer().Lex();
  }

  Operands.push_back(BedrockOperand::createToken(Mnemonic, MnemonicLoc));

  if (getLexer().is(AsmToken::EndOfStatement)) {
    AppendPrefixOperands();
    return false;
  }

  if (parseOperand(Operands))
    return Error(getLexer().getLoc(), "expected operand");

  while (parseOptionalToken(AsmToken::Comma)) {
    if (parseOperand(Operands))
      return Error(getLexer().getLoc(), "expected operand");
  }

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    SMLoc Loc = getLexer().getLoc();
    getParser().eatToEndOfStatement();
    return Error(Loc, "unexpected token");
  }

  AppendPrefixOperands();

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
