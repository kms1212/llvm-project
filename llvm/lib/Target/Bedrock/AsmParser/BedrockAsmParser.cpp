//===-- BedrockAsmParser.cpp - Parse Bedrock assembly ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/BedrockCondCode.h"
#include "MCTargetDesc/BedrockFixupKinds.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "TargetInfo/BedrockTargetInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cctype>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include "bedrock_asm_disasm.h"
}

using namespace llvm;

namespace {

struct BedrockRawFixup {
  MCFixupKind Kind = FK_NONE;
  const MCExpr *Expr = nullptr;
  uint64_t Placeholder = 0;
};

class BedrockOperand : public MCParsedAsmOperand {
public:
  enum KindTy {
    TokenKind,
    RegKind,
    ImmKind,
    MemKind,
    RepgBlockKind,
    RawLineKind
  };

private:
  KindTy Kind;
  SMLoc StartLoc;
  SMLoc EndLoc;
  std::string TokenText;
  MCRegister RegNum;
  const MCExpr *Expr = nullptr;
  MCRegister BaseReg;
  const MCExpr *Offset = nullptr;
  std::string RelocName;
  unsigned RepgCounter = 0;
  bool RepgFast = false;
  std::vector<std::string> RepgItems;
  std::string RawLine;
  uint16_t RawWords[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
  size_t RawWordCount = 0;
  std::vector<BedrockRawFixup> RawFixups;

  BedrockOperand(KindTy Kind, SMLoc StartLoc, SMLoc EndLoc)
      : Kind(Kind), StartLoc(StartLoc), EndLoc(EndLoc), RegNum(0), BaseReg(0) {}

public:
  static std::unique_ptr<BedrockOperand> createToken(StringRef Text,
                                                     SMLoc Loc) {
    auto Operand = std::unique_ptr<BedrockOperand>(
        new BedrockOperand(TokenKind, Loc, Loc));
    Operand->TokenText = std::string(Text);
    return Operand;
  }

  static std::unique_ptr<BedrockOperand>
  createReg(MCRegister Reg, SMLoc StartLoc, SMLoc EndLoc) {
    auto Operand = std::unique_ptr<BedrockOperand>(
        new BedrockOperand(RegKind, StartLoc, EndLoc));
    Operand->RegNum = Reg;
    return Operand;
  }

  static std::unique_ptr<BedrockOperand> createImm(const MCExpr *Expr,
                                                   StringRef RelocName,
                                                   SMLoc StartLoc,
                                                   SMLoc EndLoc) {
    auto Operand = std::unique_ptr<BedrockOperand>(
        new BedrockOperand(ImmKind, StartLoc, EndLoc));
    Operand->Expr = Expr;
    Operand->RelocName = RelocName.upper();
    return Operand;
  }

  static std::unique_ptr<BedrockOperand> createMem(MCRegister BaseReg,
                                                   const MCExpr *Offset,
                                                   SMLoc StartLoc,
                                                   SMLoc EndLoc) {
    auto Operand = std::unique_ptr<BedrockOperand>(
        new BedrockOperand(MemKind, StartLoc, EndLoc));
    Operand->BaseReg = BaseReg;
    Operand->Offset = Offset;
    return Operand;
  }

  static std::unique_ptr<BedrockOperand>
  createRepgBlock(unsigned Counter, bool Fast, std::vector<std::string> Items,
                  SMLoc StartLoc, SMLoc EndLoc) {
    auto Operand = std::unique_ptr<BedrockOperand>(
        new BedrockOperand(RepgBlockKind, StartLoc, EndLoc));
    Operand->RepgCounter = Counter;
    Operand->RepgFast = Fast;
    Operand->RepgItems = std::move(Items);
    return Operand;
  }

  static std::unique_ptr<BedrockOperand>
  createRawLine(StringRef Line, const uint16_t *Words, size_t WordCount,
                ArrayRef<BedrockRawFixup> Fixups,
                SMLoc StartLoc, SMLoc EndLoc) {
    auto Operand = std::unique_ptr<BedrockOperand>(
        new BedrockOperand(RawLineKind, StartLoc, EndLoc));
    Operand->RawLine = std::string(Line);
    Operand->RawWordCount = WordCount;
    Operand->RawFixups.assign(Fixups.begin(), Fixups.end());
    for (size_t I = 0; I != BEDROCK_MAX_INSTRUCTION_WORDS; ++I)
      Operand->RawWords[I] = I < WordCount ? Words[I] : 0;
    return Operand;
  }

  SMLoc getStartLoc() const override { return StartLoc; }
  SMLoc getEndLoc() const override { return EndLoc; }

  bool isToken() const override { return Kind == TokenKind; }
  bool isReg() const override { return Kind == RegKind; }
  bool isImm() const override { return Kind == ImmKind; }
  bool isMem() const override { return Kind == MemKind; }
  bool isRepgBlock() const { return Kind == RepgBlockKind; }
  bool isRawLine() const { return Kind == RawLineKind; }

  MCRegister getReg() const override {
    assert(isReg() && "invalid operand access");
    return RegNum;
  }

  StringRef getToken() const {
    assert(isToken() && "invalid operand access");
    return TokenText;
  }

  const MCExpr *getImm() const {
    assert(isImm() && "invalid operand access");
    return Expr;
  }

  StringRef getRelocName() const {
    assert(isImm() && "invalid operand access");
    return RelocName;
  }

  MCRegister getMemBaseReg() const {
    assert(isMem() && "invalid operand access");
    return BaseReg;
  }

  const MCExpr *getMemOffset() const {
    assert(isMem() && "invalid operand access");
    return Offset;
  }

  unsigned getRepgCounter() const {
    assert(isRepgBlock() && "invalid operand access");
    return RepgCounter;
  }

  bool isRepgFast() const {
    assert(isRepgBlock() && "invalid operand access");
    return RepgFast;
  }

  ArrayRef<std::string> getRepgItems() const {
    assert(isRepgBlock() && "invalid operand access");
    return RepgItems;
  }

  StringRef getRawLine() const {
    assert(isRawLine() && "invalid operand access");
    return RawLine;
  }

  ArrayRef<uint16_t> getRawWords() const {
    assert(isRawLine() && "invalid operand access");
    return ArrayRef<uint16_t>(RawWords, RawWordCount);
  }

  ArrayRef<BedrockRawFixup> getRawFixups() const {
    assert(isRawLine() && "invalid operand access");
    return RawFixups;
  }

  void print(raw_ostream &OS, const MCAsmInfo &MAI) const override {
    switch (Kind) {
    case TokenKind:
      OS << getToken();
      break;
    case RegKind:
      OS << "<reg " << RegNum << ">";
      break;
    case ImmKind:
      (void)MAI;
      MCOperand::createExpr(Expr).print(OS);
      if (!RelocName.empty())
        OS << '@' << RelocName;
      break;
    case MemKind:
      OS << "<mem>";
      break;
    case RepgBlockKind:
      OS << "<repg block>";
      break;
    case RawLineKind:
      OS << getRawLine();
      break;
    }
  }
};

class BedrockAsmParser : public MCTargetAsmParser {
public:
  BedrockAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                   const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII) {
    MCAsmParserExtension::Initialize(Parser);
  }

  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;
  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;
  bool matchAndEmitInstruction(SMLoc IdLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;
  void convertToMapAndConstraints(unsigned, const OperandVector &) override {}

private:
  bool parseRepgBlockInstruction(StringRef Name, SMLoc NameLoc,
                                 OperandVector &Operands);
  bool parseOperand(OperandVector &Operands);
  bool parseMemoryOperand(OperandVector &Operands);
  bool parseExpressionOperand(const MCExpr *&Expr, std::string &RelocName,
                              SMLoc &StartLoc, SMLoc &EndLoc);
  bool parseLenInstruction(SMLoc NameLoc, OperandVector &Operands);
  bool emitRepgBlock(SMLoc Loc, const BedrockOperand &Block, MCStreamer &Out);

  bool emitInst(MCInst &Inst, SMLoc Loc, MCStreamer &Out) const {
    Inst.setLoc(Loc);
    Out.emitInstruction(Inst, getSTI());
    return false;
  }
};

} // end anonymous namespace

static MCRegister matchRegisterName(StringRef Name) {
  std::string UpperStorage = Name.upper();
  StringRef Upper(UpperStorage);
  if (Upper == "SP")
    return Bedrock::SP;
  if (Upper == "PC")
    return Bedrock::PC;
  if (Upper == "FLAGS")
    return Bedrock::FLAGS;
  if (Upper == "STATUS")
    return Bedrock::STATUS;

  if (Upper.size() >= 2 && (Upper[0] == 'D' || Upper[0] == 'A')) {
    unsigned Index;
    if (!Upper.drop_front().getAsInteger(10, Index) && Index < 8)
      return Upper[0] == 'D' ? Bedrock::D0 + Index : Bedrock::A0 + Index;
  }

  if (Upper.size() >= 2 && Upper[0] == 'F') {
    unsigned Index;
    if (!Upper.drop_front().getAsInteger(10, Index) && Index < 16)
      return Bedrock::F0 + Index;
  }

  return Bedrock::NoRegister;
}

static bool isDReg(MCRegister Reg) {
  return Reg >= Bedrock::D0 && Reg <= Bedrock::D7;
}

static bool isFReg(MCRegister Reg) {
  return Reg >= Bedrock::F0 && Reg <= Bedrock::F15;
}

ParseStatus BedrockAsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                               SMLoc &EndLoc) {
  if (!getLexer().is(AsmToken::Identifier) && !getLexer().is(AsmToken::String))
    return ParseStatus::NoMatch;

  Reg = matchRegisterName(getTok().getIdentifier());
  if (Reg == Bedrock::NoRegister)
    return ParseStatus::NoMatch;

  StartLoc = getTok().getLoc();
  EndLoc = getTok().getEndLoc();
  Lex();
  return ParseStatus::Success;
}

bool BedrockAsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                     SMLoc &EndLoc) {
  ParseStatus Status = tryParseRegister(Reg, StartLoc, EndLoc);
  if (Status.isSuccess())
    return false;
  return Error(getTok().getLoc(), "expected Bedrock register");
}

bool BedrockAsmParser::parseExpressionOperand(const MCExpr *&Expr,
                                              std::string &RelocName,
                                              SMLoc &StartLoc, SMLoc &EndLoc) {
  StartLoc = getTok().getLoc();
  RelocName.clear();

  auto ParseOptionalAddend = [&]() -> bool {
    if (!getLexer().is(AsmToken::Plus) && !getLexer().is(AsmToken::Minus))
      return false;

    bool IsSub = getLexer().is(AsmToken::Minus);
    Lex();
    const MCExpr *Addend = nullptr;
    SMLoc AddendEnd;
    if (getParser().parseExpression(Addend, AddendEnd))
      return Error(getTok().getLoc(), "expected relocation addend");
    Expr = IsSub ? MCBinaryExpr::createSub(Expr, Addend, getContext())
                 : MCBinaryExpr::createAdd(Expr, Addend, getContext());
    EndLoc = AddendEnd;
    return false;
  };

  auto ParseRelocName = [&]() -> bool {
    if (parseToken(AsmToken::At, "expected relocation annotation"))
      return true;
    if (!getLexer().is(AsmToken::Identifier))
      return Error(getTok().getLoc(), "expected relocation name");
    RelocName = getTok().getIdentifier().upper();
    EndLoc = getTok().getEndLoc();
    Lex();
    return false;
  };

  if (getLexer().is(AsmToken::Identifier) || getLexer().is(AsmToken::String)) {
    StringRef TokenText = getTok().getIdentifier();
    size_t At = TokenText.rfind('@');
    if (At != StringRef::npos) {
      if (At == 0 || At == TokenText.size() - 1)
        return Error(getTok().getLoc(), "invalid relocation annotation");
      StringRef SymbolName = TokenText.take_front(At);
      RelocName = TokenText.drop_front(At + 1).upper();
      MCSymbol *Symbol = getContext().getOrCreateSymbol(SymbolName);
      Expr = MCSymbolRefExpr::create(Symbol, getContext(), StartLoc);
      EndLoc = getTok().getEndLoc();
      Lex();
      return ParseOptionalAddend();
    }

    StringRef SymbolName = TokenText;
    MCSymbol *Symbol = getContext().getOrCreateSymbol(SymbolName);
    Expr = MCSymbolRefExpr::create(Symbol, getContext(), StartLoc);
    EndLoc = getTok().getEndLoc();
    Lex();

    if (ParseOptionalAddend())
      return true;

    if (getLexer().is(AsmToken::At) && ParseRelocName())
      return true;

    return false;
  }

  return getParser().parseExpression(Expr, EndLoc);
}

bool BedrockAsmParser::parseMemoryOperand(OperandVector &Operands) {
  SMLoc StartLoc = getTok().getLoc();
  if (parseToken(AsmToken::LBrac, "expected '['"))
    return true;

  MCRegister BaseReg;
  SMLoc RegStartLoc, RegEndLoc;
  if (parseRegister(BaseReg, RegStartLoc, RegEndLoc))
    return true;

  const MCExpr *Offset = MCConstantExpr::create(0, getContext());
  if (getLexer().is(AsmToken::Plus) || getLexer().is(AsmToken::Minus)) {
    bool IsSub = getLexer().is(AsmToken::Minus);
    Lex();

    std::string IgnoredReloc;
    SMLoc OffsetStart, OffsetEnd;
    if (parseExpressionOperand(Offset, IgnoredReloc, OffsetStart, OffsetEnd))
      return true;
    if (!IgnoredReloc.empty())
      return Error(OffsetStart, "relocation annotations are not supported in "
                                "Bedrock base+offset operands");
    if (IsSub)
      Offset = MCBinaryExpr::createSub(MCConstantExpr::create(0, getContext()),
                                       Offset, getContext());
  }

  SMLoc EndLoc = getTok().getLoc();
  if (parseToken(AsmToken::RBrac, "expected ']'"))
    return true;

  Operands.push_back(
      BedrockOperand::createMem(BaseReg, Offset, StartLoc, EndLoc));
  return false;
}

bool BedrockAsmParser::parseOperand(OperandVector &Operands) {
  if (getLexer().is(AsmToken::LBrac))
    return parseMemoryOperand(Operands);

  MCRegister Reg;
  SMLoc StartLoc, EndLoc;
  if (tryParseRegister(Reg, StartLoc, EndLoc).isSuccess()) {
    Operands.push_back(BedrockOperand::createReg(Reg, StartLoc, EndLoc));
    return false;
  }

  const MCExpr *Expr = nullptr;
  std::string RelocName;
  if (parseExpressionOperand(Expr, RelocName, StartLoc, EndLoc))
    return true;
  Operands.push_back(
      BedrockOperand::createImm(Expr, RelocName, StartLoc, EndLoc));
  return false;
}

static bool appendNeedsSpace(StringRef Current, StringRef Token) {
  if (Current.empty() || Token.empty())
    return false;

  char Last = Current.back();
  char First = Token.front();
  if (First == '+' || First == '-')
    return Last != '[' && Last != '(' && Last != '{' && Last != ',' &&
           Last != '+' && Last != '-' && Last != '*';
  if (First == ',' || First == ']' || First == ')' || First == '}' ||
      First == '.' || First == '/' || First == '@' || First == ':' ||
      First == '*')
    return false;
  if (Last == '[' || Last == '(' || Last == '{' || Last == ',' || Last == '.' ||
      Last == '/' || Last == '@' || Last == ':' || Last == '+' || Last == '-' ||
      Last == '*')
    return false;
  return true;
}

static void appendRepgToken(std::string &Line, const AsmToken &Tok) {
  StringRef Text = Tok.getString();
  if (appendNeedsSpace(Line, Text))
    Line.push_back(' ');
  Line.append(Text.data(), Text.size());
}

static bool tokenEndsStatement(const AsmToken &Tok) {
  return Tok.is(AsmToken::EndOfStatement) || Tok.is(AsmToken::Eof);
}

static bool isRawSymbolChar(char Ch) {
  return std::isalnum(static_cast<unsigned char>(Ch)) || Ch == '_' ||
         Ch == '.' || Ch == '$';
}

static bool isRawRelocNameChar(char Ch) {
  return std::isalnum(static_cast<unsigned char>(Ch)) || Ch == '_';
}

static void appendRawToken(std::string &Line, const AsmToken &Tok) {
  StringRef Text = Tok.getString();
  if (Text.empty())
    return;
  if (appendNeedsSpace(Line, Text))
    Line.push_back(' ');
  Line.append(Text.data(), Text.size());
}

static std::string buildRawInstructionLine(BedrockAsmParser &Parser,
                                           StringRef Name) {
  std::string Line = Name.str();
  if (tokenEndsStatement(Parser.getTok()))
    return Line;

  appendRawToken(Line, Parser.getTok());

  SmallVector<AsmToken, 256> Peeked;
  Peeked.resize(256);
  size_t Count = Parser.getLexer().peekTokens(Peeked, /*ShouldSkipSpace*/ true);
  for (size_t I = 0; I != Count; ++I) {
    if (tokenEndsStatement(Peeked[I]))
      break;
    appendRawToken(Line, Peeked[I]);
  }
  return Line;
}

static std::string buildRawInstructionLineFromCurrent(
    BedrockAsmParser &Parser) {
  std::string Line;
  if (tokenEndsStatement(Parser.getTok()))
    return Line;

  appendRawToken(Line, Parser.getTok());

  SmallVector<AsmToken, 256> Peeked;
  Peeked.resize(256);
  size_t Count = Parser.getLexer().peekTokens(Peeked, /*ShouldSkipSpace*/ true);
  for (size_t I = 0; I != Count; ++I) {
    if (tokenEndsStatement(Peeked[I]))
      break;
    appendRawToken(Line, Peeked[I]);
  }
  return Line;
}

enum : uint64_t {
  BEDROCK_EA_DREG = 0x00,
  BEDROCK_EA_AREG = 0x08,
  BEDROCK_EA_IMM16 = 0x32,
  BEDROCK_EA_IMM32 = 0x33,
  BEDROCK_EA_IMM64 = 0x34,
};

static const bedrock_field_desc *
findFieldBySource(const bedrock_form_desc *Form, StringRef Source) {
  if (!Form)
    return nullptr;
  for (size_t I = 0; I != Form->field_count; ++I) {
    const bedrock_field_desc *Field = bedrock_form_field(Form, I);
    if (Field && Source == Field->source)
      return Field;
  }
  return nullptr;
}

static const bedrock_field_desc *
findImmEA6Field(const bedrock_form_desc *Form) {
  const bedrock_field_desc *Field = findFieldBySource(Form, "imm");
  if (Field && StringRef(Field->kind).starts_with("IMM") &&
      Field->width == 6)
    return Field;
  return nullptr;
}

static unsigned actualFieldToken(const uint16_t *Words,
                                 const bedrock_field_desc *Field) {
  unsigned Token = Field->token;
  if (Token != 0 && (Words[0] & BEDROCK_WORD0_PREFIX_BIT) != 0)
    ++Token;
  return Token;
}

static uint64_t extractFormField(const uint16_t *Words,
                                 const bedrock_field_desc *Field) {
  unsigned Token = actualFieldToken(Words, Field);
  uint64_t Mask =
      Field->width >= 16 ? 0xffffull : ((1ull << Field->width) - 1ull);
  return (uint64_t(Words[Token]) >> Field->low_bit) & Mask;
}

static bool insertFormField(uint16_t *Words, size_t WordCount,
                            const bedrock_field_desc *Field, uint64_t Value) {
  unsigned Token = actualFieldToken(Words, Field);
  if (Token >= WordCount)
    return false;
  uint64_t Mask =
      Field->width >= 16 ? 0xffffull : ((1ull << Field->width) - 1ull);
  uint16_t ShiftedMask = uint16_t(Mask << Field->low_bit);
  Words[Token] = uint16_t((Words[Token] & ~ShiftedMask) |
                          ((Value & Mask) << Field->low_bit));
  return true;
}

static size_t payloadStartWord(const bedrock_form_desc *Form,
                               const uint16_t *Words) {
  size_t Start =
      (Form->kind == BEDROCK_FORM_EXTENDED ||
       Form->kind == BEDROCK_FORM_EXTENDED_ALIAS)
          ? 2
          : 1;
  for (size_t I = 0; I != Form->field_count; ++I) {
    const bedrock_field_desc *Field = bedrock_form_field(Form, I);
    if (Field && size_t(Field->token) + 1 > Start)
      Start = size_t(Field->token) + 1;
  }
  if ((Words[0] & BEDROCK_WORD0_PREFIX_BIT) != 0)
    ++Start;
  return Start;
}

static unsigned immediateEAPayloadWords(uint64_t Value) {
  if (Value == BEDROCK_EA_IMM16)
    return 1;
  if (Value == BEDROCK_EA_IMM32)
    return 2;
  if (Value == BEDROCK_EA_IMM64)
    return 4;
  return 0;
}

static uint64_t readPayloadWords(const uint16_t *Words, size_t Start,
                                 unsigned Count) {
  uint64_t Value = 0;
  for (unsigned I = 0; I != Count; ++I)
    Value |= uint64_t(Words[Start + I]) << (I * 16);
  return Value;
}

static bool sizeSuffixForForm(const bedrock_form_desc *Form,
                              const uint16_t *Words, char &Suffix) {
  const bedrock_field_desc *Field = findFieldBySource(Form, "size");
  if (!Field)
    return false;
  uint64_t Value = extractFormField(Words, Field);
  StringRef Kind(Field->kind);
  if (Kind == "BWLQ" && Value < 4) {
    Suffix = "BWLQ"[Value];
    return true;
  }
  if (Kind == "BWL" && Value < 3) {
    Suffix = "BWL"[Value];
    return true;
  }
  if (Kind == "BW" && Value < 2) {
    Suffix = "BW"[Value];
    return true;
  }
  if (Kind == "WL" && Value < 2) {
    Suffix = "WL"[Value];
    return true;
  }
  return false;
}

static bool bwlqCode(char Suffix, uint64_t &Code) {
  switch (Suffix) {
  case 'B':
    Code = 0;
    return true;
  case 'W':
    Code = 1;
    return true;
  case 'L':
    Code = 2;
    return true;
  case 'Q':
    Code = 3;
    return true;
  default:
    return false;
  }
}

static const char *immToEAFormID(StringRef Mnemonic) {
  return StringSwitch<const char *>(Mnemonic)
      .Case("ADC", "ADC.IMM_TO_EA")
      .Case("ADD", "ADD.IMM_TO_EA")
      .Case("SBB", "SBB.IMM_TO_EA")
      .Case("SUB", "SUB.IMM_TO_EA")
      .Case("AND", "AND.IMM_TO_EA")
      .Case("OR", "OR.IMM_TO_EA")
      .Case("TEST", "TEST.IMM_TO_EA")
      .Case("XOR", "XOR.IMM_TO_EA")
      .Case("CMP", "CMP.IMM_TO_EA")
      .Default(nullptr);
}

static bool encodeImmToEATarget(uint16_t *Words, size_t &WordCount,
                                StringRef Mnemonic, char Suffix,
                                uint64_t ImmValue, uint64_t TargetEA) {
  if (ImmValue > 63 || immediateEAPayloadWords(ImmValue) != 0)
    return true;
  const char *FormID = immToEAFormID(Mnemonic);
  if (!FormID)
    return true;
  const bedrock_form_desc *NewForm = bedrock_find_form_by_id(FormID);
  if (!NewForm)
    return false;

  uint64_t SizeCode = 0;
  if (!bwlqCode(Suffix, SizeCode))
    return false;

  uint64_t FieldValues[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
  for (size_t I = 0; I != NewForm->field_count; ++I) {
    const bedrock_field_desc *Field = bedrock_form_field(NewForm, I);
    if (!Field)
      return false;
    if (StringRef(Field->source) == "size")
      FieldValues[I] = SizeCode;
    else if (StringRef(Field->source) == "imm")
      FieldValues[I] = ImmValue;
    else if (StringRef(Field->kind) == "EA")
      FieldValues[I] = TargetEA;
    else
      return false;
  }

  return bedrock_encode_form_words(NewForm, FieldValues, NewForm->field_count,
                                   Words, BEDROCK_MAX_INSTRUCTION_WORDS,
                                   &WordCount) == BEDROCK_OK;
}

static const bedrock_field_desc *
findRegImmEATargetField(const bedrock_form_desc *Form) {
  for (StringRef Source : {"dst", "rhs"}) {
    const bedrock_field_desc *Field = findFieldBySource(Form, Source);
    if (!Field)
      continue;
    StringRef Kind(Field->kind);
    if (Kind == "DREG" || Kind == "AREG")
      return Field;
  }
  return nullptr;
}

static std::optional<uint64_t>
registerFieldToEA(const bedrock_field_desc *Field, uint64_t Value) {
  StringRef Kind(Field->kind);
  if (Kind == "DREG")
    return BEDROCK_EA_DREG + Value;
  if (Kind == "AREG")
    return BEDROCK_EA_AREG + Value;
  return std::nullopt;
}

static const bedrock_field_desc *
findImmediateEAField(const bedrock_form_desc *Form, const uint16_t *Words) {
  for (size_t I = 0; I != Form->field_count; ++I) {
    const bedrock_field_desc *Field = bedrock_form_field(Form, I);
    if (!Field || StringRef(Field->kind) != "EA")
      continue;
    if (immediateEAPayloadWords(extractFormField(Words, Field)) != 0)
      return Field;
  }
  return nullptr;
}

static bool rewriteRegTargetImmToEA(uint16_t *Words, size_t &WordCount,
                                    const bedrock_form_desc *Form) {
  StringRef ID(Form->id);
  StringRef Mnemonic(Form->mnemonic);
  char Suffix = 0;
  if (!sizeSuffixForForm(Form, Words, Suffix)) {
    if (ID.ends_with("_TO_A"))
      Suffix = 'Q';
    else
      return true;
  }

  const bedrock_field_desc *DstField = findRegImmEATargetField(Form);
  if (!DstField)
    return true;
  std::optional<uint64_t> DstEA =
      registerFieldToEA(DstField, extractFormField(Words, DstField));
  if (!DstEA)
    return true;

  size_t PayloadStart = payloadStartWord(Form, Words);
  if (ID.ends_with("IMM_TO_D")) {
    if (PayloadStart >= WordCount)
      return false;
    uint64_t ImmValue = readPayloadWords(Words, PayloadStart,
                                         unsigned(WordCount - PayloadStart));
    return encodeImmToEATarget(Words, WordCount, Mnemonic, Suffix, ImmValue,
                               *DstEA);
  }

  const bedrock_field_desc *SrcField = findImmediateEAField(Form, Words);
  if (!SrcField)
    return true;

  if (ID.contains("EA_TO_")) {
    unsigned ImmWords =
        immediateEAPayloadWords(extractFormField(Words, SrcField));
    if (PayloadStart + ImmWords > WordCount)
      return false;
    uint64_t ImmValue = readPayloadWords(Words, PayloadStart, ImmWords);
    return encodeImmToEATarget(Words, WordCount, Mnemonic, Suffix, ImmValue,
                               *DstEA);
  }
  return true;
}

static void setRawDeclaredWords(uint16_t &Word0, unsigned WordCount) {
  Word0 = (Word0 & ~uint16_t(BEDROCK_WORD0_LENGTH_MASK)) |
          uint16_t((WordCount - 1) << 12);
}

static bool compactImmEA6Encoding(uint16_t *Words, size_t &WordCount,
                                  const bedrock_form_desc *Form) {
  const bedrock_field_desc *ImmField = findImmEA6Field(Form);
  if (!ImmField)
    return rewriteRegTargetImmToEA(Words, WordCount, Form);

  uint64_t EncodedImmEA = extractFormField(Words, ImmField);
  unsigned ImmPayloadWords = immediateEAPayloadWords(EncodedImmEA);
  if (ImmPayloadWords == 0)
    return true;
  if (EncodedImmEA == BEDROCK_EA_IMM16 || EncodedImmEA == BEDROCK_EA_IMM32 ||
      EncodedImmEA == BEDROCK_EA_IMM64)
    return false;

  size_t PayloadStart = payloadStartWord(Form, Words);
  if (PayloadStart + ImmPayloadWords > WordCount)
    return false;

  uint64_t ImmValue = readPayloadWords(Words, PayloadStart, ImmPayloadWords);
  if (ImmValue > 63 || immediateEAPayloadWords(ImmValue) != 0)
    return true;
  if (!insertFormField(Words, WordCount, ImmField, ImmValue))
    return false;

  for (size_t I = PayloadStart; I + ImmPayloadWords < WordCount; ++I)
    Words[I] = Words[I + ImmPayloadWords];
  for (size_t I = WordCount - ImmPayloadWords; I < WordCount; ++I)
    Words[I] = 0;
  WordCount -= ImmPayloadWords;
  setRawDeclaredWords(Words[0], WordCount);
  return true;
}

static SMLoc consumeRawInstructionLine(BedrockAsmParser &Parser) {
  SMLoc EndLoc = Parser.getTok().getLoc();
  while (!tokenEndsStatement(Parser.getTok())) {
    EndLoc = Parser.getTok().getEndLoc();
    Parser.Lex();
  }
  if (Parser.getLexer().is(AsmToken::EndOfStatement))
    Parser.Lex();
  return EndLoc;
}

static bool assembleRawLine(StringRef Line, uint16_t *Words,
                            size_t &WordCount) {
  const bedrock_form_desc *Form = nullptr;
  WordCount = 0;
  int Status = bedrock_assemble_line(Line.str().c_str(), Words,
                                     BEDROCK_MAX_INSTRUCTION_WORDS, &WordCount,
                                     &Form);
  if (Status != BEDROCK_OK || WordCount == 0 || Form == nullptr ||
      !compactImmEA6Encoding(Words, WordCount, Form))
    return false;
  setRawDeclaredWords(Words[0], WordCount);
  return true;
}

static std::string placeholderText(uint64_t Placeholder) {
  SmallString<32> Text;
  raw_svector_ostream OS(Text);
  OS << "0x";
  OS.write_hex(Placeholder);
  return std::string(OS.str());
}

struct RawInstructionEncoding {
  uint16_t Words[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
  size_t WordCount = 0;
  SmallVector<BedrockRawFixup, 4> Fixups;
};

static bool parseRawRelocAddend(StringRef Text, int64_t &Value) {
  Text = Text.trim();
  if (Text.empty())
    return false;
  bool Negative = Text.consume_front("-");
  Text.consume_front("+");
  uint64_t Magnitude = 0;
  if (Text.getAsInteger(0, Magnitude))
    return false;
  Value = Negative ? -int64_t(Magnitude) : int64_t(Magnitude);
  return true;
}

struct RawRelocReplacement {
  size_t Start = 0;
  size_t End = 0;
  BedrockRawFixup Fixup;
};

static bool assembleRawLineWithReloc(BedrockAsmParser &Parser, StringRef Line,
                                     RawInstructionEncoding &Encoding,
                                     std::string &ErrorMessage) {
  SmallVector<RawRelocReplacement, 4> Replacements;
  size_t SearchFrom = 0;
  for (;;) {
    size_t At = Line.find('@', SearchFrom);
    if (At == StringRef::npos)
      break;

    size_t Start = At;
    while (Start > 0 && isRawSymbolChar(Line[Start - 1]))
      --Start;
    if (Start == At) {
      ErrorMessage = "expected symbol before relocation annotation";
      return false;
    }

    size_t RelocEnd = At + 1;
    while (RelocEnd < Line.size() && isRawRelocNameChar(Line[RelocEnd]))
      ++RelocEnd;
    if (RelocEnd == At + 1) {
      ErrorMessage = "expected relocation name";
      return false;
    }

    StringRef RelocName = Line.slice(At + 1, RelocEnd);
    MCFixupKind FixupKind = Bedrock::getFixupKindForRelocName(RelocName);
    if (FixupKind == FK_NONE) {
      ErrorMessage = ("unsupported Bedrock relocation annotation @" + RelocName)
                         .str();
      return false;
    }

    size_t End = RelocEnd;
    while (End < Line.size() &&
           std::isspace(static_cast<unsigned char>(Line[End])))
      ++End;

    bool HasAddend = false;
    bool IsSub = false;
    size_t AddendStart = End;
    size_t AddendEnd = End;
    if (End < Line.size() && (Line[End] == '+' || Line[End] == '-')) {
      IsSub = Line[End] == '-';
      AddendStart = End + 1;
      while (AddendStart < Line.size() &&
             std::isspace(static_cast<unsigned char>(Line[AddendStart])))
        ++AddendStart;
      if (AddendStart < Line.size() &&
          std::isdigit(static_cast<unsigned char>(Line[AddendStart]))) {
        AddendEnd = AddendStart;
        while (AddendEnd < Line.size() &&
               std::isalnum(static_cast<unsigned char>(Line[AddendEnd])))
          ++AddendEnd;
        HasAddend = true;
        End = AddendEnd;
      }
    }

    StringRef SymbolName = Line.slice(Start, At);
    MCSymbol *Symbol = Parser.getContext().getOrCreateSymbol(SymbolName);
    const MCExpr *Expr = MCSymbolRefExpr::create(
        Symbol, Parser.getContext(), Parser.getTok().getLoc());
    if (HasAddend) {
      int64_t Addend = 0;
      if (!parseRawRelocAddend(Line.slice(AddendStart, AddendEnd), Addend)) {
        ErrorMessage = "invalid relocation addend";
        return false;
      }
      const MCExpr *AddendExpr =
          MCConstantExpr::create(Addend, Parser.getContext());
      Expr =
          IsSub ? MCBinaryExpr::createSub(Expr, AddendExpr, Parser.getContext())
                : MCBinaryExpr::createAdd(Expr, AddendExpr, Parser.getContext());
    }

    BedrockRawFixup Fixup;
    Fixup.Kind = FixupKind;
    Fixup.Expr = Expr;
    Fixup.Placeholder =
        Bedrock::getRelocationPlaceholder(FixupKind, Replacements.size());
    Replacements.push_back({Start, End, Fixup});
    SearchFrom = End;
  }

  if (Replacements.empty())
    return false;

  std::string Transformed;
  Transformed.reserve(Line.size() + Replacements.size() * 16);
  size_t Cursor = 0;
  for (const RawRelocReplacement &Replacement : Replacements) {
    Transformed.append(Line.data() + Cursor, Replacement.Start - Cursor);
    Transformed += placeholderText(Replacement.Fixup.Placeholder);
    Cursor = Replacement.End;
  }
  Transformed.append(Line.data() + Cursor, Line.size() - Cursor);

  size_t WordCount = 0;
  if (!assembleRawLine(Transformed, Encoding.Words, WordCount))
    return false;
  Encoding.WordCount = WordCount;
  for (const RawRelocReplacement &Replacement : Replacements)
    Encoding.Fixups.push_back(Replacement.Fixup);
  return true;
}

static void setEncodedInstWords(MCInst &Inst, ArrayRef<uint16_t> Words,
                                ArrayRef<BedrockRawFixup> Fixups) {
  Inst.setOpcode(Bedrock::ENCODED);
  Inst.addOperand(MCOperand::createImm(Words.size()));
  for (size_t I = 0; I != BEDROCK_MAX_INSTRUCTION_WORDS; ++I)
    Inst.addOperand(MCOperand::createImm(I < Words.size() ? Words[I] : 0));
  if (!Fixups.empty()) {
    Inst.addOperand(MCOperand::createImm(Fixups.size()));
    for (const BedrockRawFixup &Fixup : Fixups) {
      Inst.addOperand(MCOperand::createImm(Fixup.Kind));
      Inst.addOperand(MCOperand::createImm(Fixup.Placeholder));
      Inst.addOperand(MCOperand::createExpr(Fixup.Expr));
    }
  }
}

static unsigned declaredWords(uint16_t Word0) {
  return ((Word0 & BEDROCK_WORD0_LENGTH_MASK) >> 12) + 1;
}

static void setDeclaredWords(uint16_t &Word0, unsigned WordCount) {
  Word0 = (Word0 & ~uint16_t(BEDROCK_WORD0_LENGTH_MASK)) |
          uint16_t((WordCount - 1) << 12);
}

bool BedrockAsmParser::parseLenInstruction(SMLoc NameLoc,
                                           OperandVector &Operands) {
  const MCExpr *DeclaredExpr = nullptr;
  SMLoc DeclaredEnd;
  if (getParser().parseExpression(DeclaredExpr, DeclaredEnd))
    return true;

  int64_t DeclaredValue = 0;
  if (!DeclaredExpr->evaluateAsAbsolute(DeclaredValue))
    return Error(NameLoc, "expected absolute LEN word count");
  if (DeclaredValue < 1 ||
      DeclaredValue > int64_t(BEDROCK_MAX_INSTRUCTION_WORDS))
    return Error(NameLoc, "LEN word count must be in range 1..8");

  if (parseToken(AsmToken::Comma, "expected `,' after LEN word count"))
    return true;
  if (tokenEndsStatement(getTok()))
    return Error(getTok().getLoc(),
                 "expected Bedrock instruction after LEN word count");

  std::string RawLine = buildRawInstructionLineFromCurrent(*this);
  RawInstructionEncoding RawEncoding;
  std::string RawRelocError;
  if (!assembleRawLineWithReloc(*this, RawLine, RawEncoding, RawRelocError) &&
      !assembleRawLine(RawLine, RawEncoding.Words, RawEncoding.WordCount)) {
    if (!RawRelocError.empty())
      return Error(getTok().getLoc(), RawRelocError);
    return Error(getTok().getLoc(), "cannot assemble Bedrock instruction `" +
                                      Twine(RawLine) + "`");
  }

  SMLoc EndLoc = consumeRawInstructionLine(*this);

  unsigned ActualWords = RawEncoding.WordCount;
  unsigned DeclaredWords = unsigned(DeclaredValue);
  if (DeclaredWords <= ActualWords)
    return Error(NameLoc, "LEN word count must be greater than the actual "
                         "instruction word count");
  if (DeclaredWords < declaredWords(RawEncoding.Words[0]))
    return Error(NameLoc, "LEN word count cannot shrink Word0 L");

  setDeclaredWords(RawEncoding.Words[0], DeclaredWords);

  Operands.push_back(BedrockOperand::createRawLine(
      RawLine, RawEncoding.Words, RawEncoding.WordCount, RawEncoding.Fixups,
      NameLoc, EndLoc));
  return false;
}

static bool isRepgHeaderName(StringRef Line) {
  Line = Line.trim();
  if (!Line.starts_with_insensitive("REPG"))
    return false;
  if (Line.size() == 4)
    return true;
  if (Line.starts_with_insensitive("REPGF") &&
      (Line.size() == 5 || std::isspace(static_cast<unsigned char>(Line[5]))))
    return true;
  return std::isspace(static_cast<unsigned char>(Line[4]));
}

static bool appendRepgItem(BedrockAsmParser &Parser, SMLoc Loc,
                           std::string &Line, std::vector<std::string> &Items) {
  StringRef Trimmed(Line);
  Trimmed = Trimmed.trim();
  if (Trimmed.empty())
    return false;
  std::string Item(Trimmed);
  StringRef ItemRef(Item);
  if (ItemRef.size() > 511)
    return Parser.Error(Loc, "REPG instruction line is too long");
  if (ItemRef.front() == '.' || ItemRef.contains(':'))
    return Parser.Error(
        Loc, "directives and labels are not supported inside REPG blocks");
  if (ItemRef.contains('@'))
    return Parser.Error(Loc,
                        "relocations are not supported inside REPG blocks");
  if (isRepgHeaderName(ItemRef))
    return Parser.Error(Loc, "nested REPG blocks are not supported");
  if (Items.size() >= 32)
    return Parser.Error(
        Loc, "REPG group is too large to fit in one 64-byte I-cache line");
  Line.clear();
  Items.push_back(std::move(Item));
  return false;
}

bool BedrockAsmParser::parseRepgBlockInstruction(StringRef Name, SMLoc NameLoc,
                                                 OperandVector &Operands) {
  bool Fast = Name.equals_insensitive("REPGF");
  SMLoc RegStartLoc, RegEndLoc;
  MCRegister CounterReg;
  if (parseRegister(CounterReg, RegStartLoc, RegEndLoc))
    return true;
  if (!isDReg(CounterReg))
    return Error(RegStartLoc, "REPG/REPGF requires a D register counter");

  parseOptionalToken(AsmToken::Comma);
  if (parseToken(AsmToken::LCurly, "REPG/REPGF requires `{` after the counter"))
    return true;

  std::vector<std::string> Items;
  std::string Current;
  SMLoc CurrentLoc = getTok().getLoc();
  unsigned BracketDepth = 0;
  for (;;) {
    const AsmToken &Tok = getTok();
    if (Tok.is(AsmToken::Eof))
      return Error(NameLoc, "unterminated REPG block");
    if (Tok.is(AsmToken::Error))
      return Error(Tok.getLoc(), "invalid token in REPG block");

    if (Tok.is(AsmToken::EndOfStatement)) {
      if (appendRepgItem(*this, CurrentLoc, Current, Items))
        return true;
      Lex();
      CurrentLoc = getTok().getLoc();
      continue;
    }

    if (Tok.is(AsmToken::LBrac))
      ++BracketDepth;
    else if (Tok.is(AsmToken::RBrac)) {
      if (BracketDepth == 0)
        return Error(Tok.getLoc(), "unexpected `]` in REPG block");
      --BracketDepth;
    } else if (Tok.is(AsmToken::LCurly)) {
      return Error(Tok.getLoc(), "nested REPG blocks are not supported");
    } else if (Tok.is(AsmToken::RCurly) && BracketDepth == 0) {
      SMLoc EndLoc = Tok.getLoc();
      if (appendRepgItem(*this, CurrentLoc, Current, Items))
        return true;
      Lex();
      if (!getLexer().is(AsmToken::EndOfStatement) &&
          !getLexer().is(AsmToken::Eof)) {
        SMLoc Loc = getTok().getLoc();
        getParser().eatToEndOfStatement();
        return Error(Loc, "unexpected token after REPG block");
      }
      if (getLexer().is(AsmToken::EndOfStatement))
        Lex();
      if (Items.empty())
        return Error(NameLoc, "REPG requires at least one grouped instruction");

      Operands.push_back(BedrockOperand::createToken(Name.upper(), NameLoc));
      Operands.push_back(BedrockOperand::createRepgBlock(
          CounterReg - Bedrock::D0, Fast, std::move(Items), NameLoc, EndLoc));
      return false;
    }

    if (Current.empty())
      CurrentLoc = Tok.getLoc();
    appendRepgToken(Current, Tok);
    Lex();
  }
}

bool BedrockAsmParser::parseInstruction(ParseInstructionInfo &Info,
                                        StringRef Name, SMLoc NameLoc,
                                        OperandVector &Operands) {
  (void)Info;
  std::string Mnemonic = Name.upper();
  if (Mnemonic == "REPG" || Mnemonic == "REPGF")
    return parseRepgBlockInstruction(Mnemonic, NameLoc, Operands);
  if (Mnemonic == "LEN")
    return parseLenInstruction(NameLoc, Operands);

  std::string RawLine = buildRawInstructionLine(*this, Mnemonic);
  RawInstructionEncoding RawEncoding;
  std::string RawRelocError;
  if (assembleRawLineWithReloc(*this, RawLine, RawEncoding, RawRelocError) ||
      assembleRawLine(RawLine, RawEncoding.Words, RawEncoding.WordCount)) {
    SMLoc EndLoc = consumeRawInstructionLine(*this);
    Operands.push_back(BedrockOperand::createRawLine(
        RawLine, RawEncoding.Words, RawEncoding.WordCount,
        RawEncoding.Fixups, NameLoc, EndLoc));
    return false;
  }
  if (!RawRelocError.empty())
    return Error(NameLoc, RawRelocError);

  if (parseOptionalToken(AsmToken::Slash)) {
    if (!getLexer().is(AsmToken::Identifier))
      return Error(getTok().getLoc(), "expected atomic memory order");
    Mnemonic += '/';
    Mnemonic += getTok().getIdentifier().upper();
    Lex();
  }
  Operands.push_back(BedrockOperand::createToken(Mnemonic, NameLoc));

  if (getLexer().is(AsmToken::EndOfStatement)) {
    Lex();
    return false;
  }

  if (parseOperand(Operands))
    return true;
  while (parseOptionalToken(AsmToken::Comma)) {
    if (parseOperand(Operands))
      return true;
  }

  if (!getLexer().is(AsmToken::EndOfStatement)) {
    SMLoc Loc = getTok().getLoc();
    getParser().eatToEndOfStatement();
    return Error(Loc, "unexpected token");
  }
  Lex();
  return false;
}

static const BedrockOperand &operand(const OperandVector &Operands,
                                     unsigned Index) {
  return static_cast<const BedrockOperand &>(*Operands[Index]);
}

static bool expectOperandCount(BedrockAsmParser &Parser, SMLoc Loc,
                               const OperandVector &Operands, unsigned Count) {
  if (Operands.size() == Count + 1)
    return false;
  return Parser.Error(Loc, "invalid operand count");
}

static bool expectReloc(BedrockAsmParser &Parser, const BedrockOperand &Op,
                        StringRef Expected) {
  if (!Op.isImm())
    return Parser.Error(Op.getStartLoc(), "expected expression operand");
  if (Op.getRelocName().empty() || Op.getRelocName() == Expected)
    return false;
  return Parser.Error(Op.getStartLoc(), Twine("expected @") + Expected +
                                            " relocation annotation");
}

static void addExprOrImm(MCInst &Inst, const MCExpr *Expr) {
  int64_t Value;
  if (Expr->evaluateAsAbsolute(Value))
    Inst.addOperand(MCOperand::createImm(Value));
  else
    Inst.addOperand(MCOperand::createExpr(Expr));
}

static void addMem(MCInst &Inst, const BedrockOperand &Op) {
  Inst.addOperand(MCOperand::createReg(Op.getMemBaseReg()));
  addExprOrImm(Inst, Op.getMemOffset());
}

static bool parseSuffix(StringRef Name, StringRef Base, char &Suffix) {
  if (!Name.starts_with(Base) || Name.size() != Base.size() + 2 ||
      Name[Base.size()] != '.')
    return true;
  Suffix = Name.back();
  return Suffix != 'B' && Suffix != 'W' && Suffix != 'L' && Suffix != 'Q';
}

static bool parseFPSuffix(StringRef Name, StringRef Base, char &Suffix) {
  if (!Name.starts_with(Base) || Name.size() != Base.size() + 2 ||
      Name[Base.size()] != '.')
    return true;
  Suffix = Name.back();
  return Suffix != 'S' && Suffix != 'D';
}

static bool parseAtomicSuffixOrder(StringRef Name, StringRef Base, char &Suffix,
                                   unsigned &Order) {
  StringRef Head;
  StringRef OrderName;
  std::tie(Head, OrderName) = Name.split('/');
  if (OrderName.empty())
    return true;
  if (parseSuffix(Head, Base, Suffix))
    return true;

  std::string UpperStorage = OrderName.upper();
  StringRef Upper(UpperStorage);
  std::optional<unsigned> Parsed = StringSwitch<std::optional<unsigned>>(Upper)
                                       .Case("RELAXED", 0)
                                       .Case("ACQUIRE", 1)
                                       .Case("RELEASE", 2)
                                       .Case("ACQREL", 3)
                                       .Case("SEQCST", 4)
                                       .Default(std::nullopt);
  if (!Parsed)
    return true;
  Order = *Parsed;
  return false;
}

static unsigned movRROpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::MOV8rr;
  case 'W':
    return Bedrock::MOV16rr;
  case 'L':
    return Bedrock::MOV32rr;
  case 'Q':
    return Bedrock::MOV64rr;
  }
  llvm_unreachable("invalid size suffix");
}

static unsigned movRIOpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::MOV8ri;
  case 'W':
    return Bedrock::MOV16ri;
  case 'L':
    return Bedrock::MOV32ri;
  case 'Q':
    return Bedrock::MOV64ri;
  }
  llvm_unreachable("invalid size suffix");
}

static unsigned movRMOpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::MOV8rm;
  case 'W':
    return Bedrock::MOV16rm;
  case 'L':
    return Bedrock::MOV32rm;
  case 'Q':
    return Bedrock::MOV64rm;
  }
  llvm_unreachable("invalid size suffix");
}

static unsigned movMROpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::MOV8mr;
  case 'W':
    return Bedrock::MOV16mr;
  case 'L':
    return Bedrock::MOV32mr;
  case 'Q':
    return Bedrock::MOV64mr;
  }
  llvm_unreachable("invalid size suffix");
}

static unsigned movMIOpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::MOV8mi;
  case 'W':
    return Bedrock::MOV16mi;
  case 'L':
    return Bedrock::MOV32mi;
  case 'Q':
    return Bedrock::MOV64mi;
  }
  llvm_unreachable("invalid size suffix");
}

static unsigned incDecROpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("INC", Suffix == 'B'   ? Bedrock::INC8r
                   : Suffix == 'W' ? Bedrock::INC16r
                   : Suffix == 'L' ? Bedrock::INC32r
                                   : Bedrock::INC64r)
      .Case("DEC", Suffix == 'B'   ? Bedrock::DEC8r
                   : Suffix == 'W' ? Bedrock::DEC16r
                   : Suffix == 'L' ? Bedrock::DEC32r
                                   : Bedrock::DEC64r)
      .Default(0);
}

static unsigned incDecMOpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("INC", Suffix == 'B'   ? Bedrock::INC8m
                   : Suffix == 'W' ? Bedrock::INC16m
                   : Suffix == 'L' ? Bedrock::INC32m
                                   : Bedrock::INC64m)
      .Case("DEC", Suffix == 'B'   ? Bedrock::DEC8m
                   : Suffix == 'W' ? Bedrock::DEC16m
                   : Suffix == 'L' ? Bedrock::DEC32m
                                   : Bedrock::DEC64m)
      .Default(0);
}

static unsigned binRROpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("ADD", Suffix == 'B'   ? Bedrock::ADD8rr
                   : Suffix == 'W' ? Bedrock::ADD16rr
                   : Suffix == 'L' ? Bedrock::ADD32rr
                                   : Bedrock::ADD64rr)
      .Case("SUB", Suffix == 'B'   ? Bedrock::SUB8rr
                   : Suffix == 'W' ? Bedrock::SUB16rr
                   : Suffix == 'L' ? Bedrock::SUB32rr
                                   : Bedrock::SUB64rr)
      .Case("AND", Suffix == 'B'   ? Bedrock::AND8rr
                   : Suffix == 'W' ? Bedrock::AND16rr
                   : Suffix == 'L' ? Bedrock::AND32rr
                                   : Bedrock::AND64rr)
      .Case("OR", Suffix == 'B'   ? Bedrock::OR8rr
                  : Suffix == 'W' ? Bedrock::OR16rr
                  : Suffix == 'L' ? Bedrock::OR32rr
                                  : Bedrock::OR64rr)
      .Case("XOR", Suffix == 'B'   ? Bedrock::XOR8rr
                   : Suffix == 'W' ? Bedrock::XOR16rr
                   : Suffix == 'L' ? Bedrock::XOR32rr
                                   : Bedrock::XOR64rr)
      .Case("MULU", Suffix == 'B'   ? Bedrock::MULU8rr
                    : Suffix == 'W' ? Bedrock::MULU16rr
                    : Suffix == 'L' ? Bedrock::MULU32rr
                                    : Bedrock::MULU64rr)
      .Case("MULHS", Suffix == 'B'   ? Bedrock::MULHS8rr
                     : Suffix == 'W' ? Bedrock::MULHS16rr
                     : Suffix == 'L' ? Bedrock::MULHS32rr
                                     : Bedrock::MULHS64rr)
      .Case("MULHU", Suffix == 'B'   ? Bedrock::MULHU8rr
                     : Suffix == 'W' ? Bedrock::MULHU16rr
                     : Suffix == 'L' ? Bedrock::MULHU32rr
                                     : Bedrock::MULHU64rr)
      .Case("DIVS", Suffix == 'B'   ? Bedrock::DIVS8rr
                    : Suffix == 'W' ? Bedrock::DIVS16rr
                    : Suffix == 'L' ? Bedrock::DIVS32rr
                                    : Bedrock::DIVS64rr)
      .Case("DIVU", Suffix == 'B'   ? Bedrock::DIVU8rr
                    : Suffix == 'W' ? Bedrock::DIVU16rr
                    : Suffix == 'L' ? Bedrock::DIVU32rr
                                    : Bedrock::DIVU64rr)
      .Case("MODS", Suffix == 'B'   ? Bedrock::MODS8rr
                    : Suffix == 'W' ? Bedrock::MODS16rr
                    : Suffix == 'L' ? Bedrock::MODS32rr
                                    : Bedrock::MODS64rr)
      .Case("MODU", Suffix == 'B'   ? Bedrock::MODU8rr
                    : Suffix == 'W' ? Bedrock::MODU16rr
                    : Suffix == 'L' ? Bedrock::MODU32rr
                                    : Bedrock::MODU64rr)
      .Default(0);
}

static unsigned binRIOpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("ADC", Suffix == 'B'   ? Bedrock::ADC8ri
                   : Suffix == 'W' ? Bedrock::ADC16ri
                   : Suffix == 'L' ? Bedrock::ADC32ri
                                   : Bedrock::ADC64ri)
      .Case("ADD", Suffix == 'B'   ? Bedrock::ADD8ri
                   : Suffix == 'W' ? Bedrock::ADD16ri
                   : Suffix == 'L' ? Bedrock::ADD32ri
                                   : Bedrock::ADD64ri)
      .Case("SBB", Suffix == 'B'   ? Bedrock::SBB8ri
                   : Suffix == 'W' ? Bedrock::SBB16ri
                   : Suffix == 'L' ? Bedrock::SBB32ri
                                   : Bedrock::SBB64ri)
      .Case("SUB", Suffix == 'B'   ? Bedrock::SUB8ri
                   : Suffix == 'W' ? Bedrock::SUB16ri
                   : Suffix == 'L' ? Bedrock::SUB32ri
                                   : Bedrock::SUB64ri)
      .Case("AND", Suffix == 'B'   ? Bedrock::AND8ri
                   : Suffix == 'W' ? Bedrock::AND16ri
                   : Suffix == 'L' ? Bedrock::AND32ri
                                   : Bedrock::AND64ri)
      .Case("OR", Suffix == 'B'   ? Bedrock::OR8ri
                  : Suffix == 'W' ? Bedrock::OR16ri
                  : Suffix == 'L' ? Bedrock::OR32ri
                                  : Bedrock::OR64ri)
      .Case("XOR", Suffix == 'B'   ? Bedrock::XOR8ri
                   : Suffix == 'W' ? Bedrock::XOR16ri
                   : Suffix == 'L' ? Bedrock::XOR32ri
                                   : Bedrock::XOR64ri)
      .Case("MULU", Suffix == 'L' ? Bedrock::MULU32ri : 0)
      .Case("DIVS", Suffix == 'Q' ? Bedrock::DIVS64ri : 0)
      .Default(0);
}

static unsigned binRMOpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("ADD", Suffix == 'B'   ? Bedrock::ADD8rm
                   : Suffix == 'W' ? Bedrock::ADD16rm
                   : Suffix == 'L' ? Bedrock::ADD32rm
                                   : Bedrock::ADD64rm)
      .Case("SUB", Suffix == 'B'   ? Bedrock::SUB8rm
                   : Suffix == 'W' ? Bedrock::SUB16rm
                   : Suffix == 'L' ? Bedrock::SUB32rm
                                   : Bedrock::SUB64rm)
      .Case("AND", Suffix == 'B'   ? Bedrock::AND8rm
                   : Suffix == 'W' ? Bedrock::AND16rm
                   : Suffix == 'L' ? Bedrock::AND32rm
                                   : Bedrock::AND64rm)
      .Case("OR", Suffix == 'B'   ? Bedrock::OR8rm
                  : Suffix == 'W' ? Bedrock::OR16rm
                  : Suffix == 'L' ? Bedrock::OR32rm
                                  : Bedrock::OR64rm)
      .Case("XOR", Suffix == 'B'   ? Bedrock::XOR8rm
                   : Suffix == 'W' ? Bedrock::XOR16rm
                   : Suffix == 'L' ? Bedrock::XOR32rm
                                   : Bedrock::XOR64rm)
      .Case("MULU", Suffix == 'B'   ? Bedrock::MULU8rm
                    : Suffix == 'W' ? Bedrock::MULU16rm
                    : Suffix == 'L' ? Bedrock::MULU32rm
                                    : Bedrock::MULU64rm)
      .Default(0);
}

static unsigned binMROpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("ADD", Suffix == 'B'   ? Bedrock::ADD8mr
                   : Suffix == 'W' ? Bedrock::ADD16mr
                   : Suffix == 'L' ? Bedrock::ADD32mr
                                   : Bedrock::ADD64mr)
      .Case("SUB", Suffix == 'B'   ? Bedrock::SUB8mr
                   : Suffix == 'W' ? Bedrock::SUB16mr
                   : Suffix == 'L' ? Bedrock::SUB32mr
                                   : Bedrock::SUB64mr)
      .Case("AND", Suffix == 'B'   ? Bedrock::AND8mr
                   : Suffix == 'W' ? Bedrock::AND16mr
                   : Suffix == 'L' ? Bedrock::AND32mr
                                   : Bedrock::AND64mr)
      .Case("OR", Suffix == 'B'   ? Bedrock::OR8mr
                  : Suffix == 'W' ? Bedrock::OR16mr
                  : Suffix == 'L' ? Bedrock::OR32mr
                                  : Bedrock::OR64mr)
      .Case("XOR", Suffix == 'B'   ? Bedrock::XOR8mr
                   : Suffix == 'W' ? Bedrock::XOR16mr
                   : Suffix == 'L' ? Bedrock::XOR32mr
                                   : Bedrock::XOR64mr)
      .Case("MULU", Suffix == 'B'   ? Bedrock::MULU8mr
                    : Suffix == 'W' ? Bedrock::MULU16mr
                    : Suffix == 'L' ? Bedrock::MULU32mr
                                    : Bedrock::MULU64mr)
      .Default(0);
}

static unsigned binMIOpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("ADC", Suffix == 'B'   ? Bedrock::ADC8mi
                   : Suffix == 'W' ? Bedrock::ADC16mi
                   : Suffix == 'L' ? Bedrock::ADC32mi
                                   : Bedrock::ADC64mi)
      .Case("ADD", Suffix == 'B'   ? Bedrock::ADD8mi
                   : Suffix == 'W' ? Bedrock::ADD16mi
                   : Suffix == 'L' ? Bedrock::ADD32mi
                                   : Bedrock::ADD64mi)
      .Case("SBB", Suffix == 'B'   ? Bedrock::SBB8mi
                   : Suffix == 'W' ? Bedrock::SBB16mi
                   : Suffix == 'L' ? Bedrock::SBB32mi
                                   : Bedrock::SBB64mi)
      .Case("SUB", Suffix == 'B'   ? Bedrock::SUB8mi
                   : Suffix == 'W' ? Bedrock::SUB16mi
                   : Suffix == 'L' ? Bedrock::SUB32mi
                                   : Bedrock::SUB64mi)
      .Case("AND", Suffix == 'B'   ? Bedrock::AND8mi
                   : Suffix == 'W' ? Bedrock::AND16mi
                   : Suffix == 'L' ? Bedrock::AND32mi
                                   : Bedrock::AND64mi)
      .Case("OR", Suffix == 'B'   ? Bedrock::OR8mi
                  : Suffix == 'W' ? Bedrock::OR16mi
                  : Suffix == 'L' ? Bedrock::OR32mi
                                  : Bedrock::OR64mi)
      .Case("XOR", Suffix == 'B'   ? Bedrock::XOR8mi
                   : Suffix == 'W' ? Bedrock::XOR16mi
                   : Suffix == 'L' ? Bedrock::XOR32mi
                                   : Bedrock::XOR64mi)
      .Default(0);
}

static unsigned cmpRIOpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::CMP8ri;
  case 'W':
    return Bedrock::CMP16ri;
  case 'L':
    return Bedrock::CMP32ri;
  case 'Q':
    return Bedrock::CMP64ri;
  }
  llvm_unreachable("invalid size suffix");
}

static unsigned cmpMIOpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::CMP8mi;
  case 'W':
    return Bedrock::CMP16mi;
  case 'L':
    return Bedrock::CMP32mi;
  case 'Q':
    return Bedrock::CMP64mi;
  }
  llvm_unreachable("invalid size suffix");
}

static unsigned testRROpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::TEST8rr;
  case 'W':
    return Bedrock::TEST16rr;
  case 'L':
    return Bedrock::TEST32rr;
  case 'Q':
    return Bedrock::TEST64rr;
  }
  llvm_unreachable("invalid size suffix");
}

static unsigned testRIOpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::TEST8ri;
  case 'W':
    return Bedrock::TEST16ri;
  case 'L':
    return Bedrock::TEST32ri;
  case 'Q':
    return Bedrock::TEST64ri;
  }
  llvm_unreachable("invalid size suffix");
}

static unsigned testMIOpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::TEST8mi;
  case 'W':
    return Bedrock::TEST16mi;
  case 'L':
    return Bedrock::TEST32mi;
  case 'Q':
    return Bedrock::TEST64mi;
  }
  llvm_unreachable("invalid size suffix");
}

static unsigned shiftOpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("SHL", Suffix == 'B'   ? Bedrock::SHL8ri
                   : Suffix == 'W' ? Bedrock::SHL16ri
                   : Suffix == 'L' ? Bedrock::SHL32ri
                                   : Bedrock::SHL64ri)
      .Case("SHR", Suffix == 'B'   ? Bedrock::SHR8ri
                   : Suffix == 'W' ? Bedrock::SHR16ri
                   : Suffix == 'L' ? Bedrock::SHR32ri
                                   : Bedrock::SHR64ri)
      .Case("SAR", Suffix == 'B'   ? Bedrock::SAR8ri
                   : Suffix == 'W' ? Bedrock::SAR16ri
                   : Suffix == 'L' ? Bedrock::SAR32ri
                                   : Bedrock::SAR64ri)
      .Default(0);
}

static unsigned shiftRROpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("SHL", Suffix == 'B'   ? Bedrock::SHL8rr
                   : Suffix == 'W' ? Bedrock::SHL16rr
                   : Suffix == 'L' ? Bedrock::SHL32rr
                                   : Bedrock::SHL64rr)
      .Case("SHR", Suffix == 'B'   ? Bedrock::SHR8rr
                   : Suffix == 'W' ? Bedrock::SHR16rr
                   : Suffix == 'L' ? Bedrock::SHR32rr
                                   : Bedrock::SHR64rr)
      .Case("SAR", Suffix == 'B'   ? Bedrock::SAR8rr
                   : Suffix == 'W' ? Bedrock::SAR16rr
                   : Suffix == 'L' ? Bedrock::SAR32rr
                                   : Bedrock::SAR64rr)
      .Default(0);
}

static unsigned shiftMIOpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("SHL", Suffix == 'B'   ? Bedrock::SHL8mi
                   : Suffix == 'W' ? Bedrock::SHL16mi
                   : Suffix == 'L' ? Bedrock::SHL32mi
                                   : Bedrock::SHL64mi)
      .Case("SHR", Suffix == 'B'   ? Bedrock::SHR8mi
                   : Suffix == 'W' ? Bedrock::SHR16mi
                   : Suffix == 'L' ? Bedrock::SHR32mi
                                   : Bedrock::SHR64mi)
      .Case("SAR", Suffix == 'B'   ? Bedrock::SAR8mi
                   : Suffix == 'W' ? Bedrock::SAR16mi
                   : Suffix == 'L' ? Bedrock::SAR32mi
                                   : Bedrock::SAR64mi)
      .Default(0);
}

static unsigned shiftMROpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("SHL", Suffix == 'B'   ? Bedrock::SHL8mr
                   : Suffix == 'W' ? Bedrock::SHL16mr
                   : Suffix == 'L' ? Bedrock::SHL32mr
                                   : Bedrock::SHL64mr)
      .Case("SHR", Suffix == 'B'   ? Bedrock::SHR8mr
                   : Suffix == 'W' ? Bedrock::SHR16mr
                   : Suffix == 'L' ? Bedrock::SHR32mr
                                   : Bedrock::SHR64mr)
      .Case("SAR", Suffix == 'B'   ? Bedrock::SAR8mr
                   : Suffix == 'W' ? Bedrock::SAR16mr
                   : Suffix == 'L' ? Bedrock::SAR32mr
                                   : Bedrock::SAR64mr)
      .Default(0);
}

static unsigned extRROpcode(StringRef Mnemonic, char Suffix) {
  if (Mnemonic == "EXTZQ")
    return Suffix == 'B'   ? Bedrock::EXTZQ8rr
           : Suffix == 'W' ? Bedrock::EXTZQ16rr
           : Suffix == 'L' ? Bedrock::EXTZQ32rr
                           : 0;
  if (Mnemonic == "EXTSQ")
    return Suffix == 'B'   ? Bedrock::EXTSQ8rr
           : Suffix == 'W' ? Bedrock::EXTSQ16rr
           : Suffix == 'L' ? Bedrock::EXTSQ32rr
                           : 0;
  if (Mnemonic == "EXTZL")
    return Suffix == 'B'   ? Bedrock::EXTZL8rr
           : Suffix == 'W' ? Bedrock::EXTZL16rr
                           : 0;
  if (Mnemonic == "EXTSL")
    return Suffix == 'B'   ? Bedrock::EXTSL8rr
           : Suffix == 'W' ? Bedrock::EXTSL16rr
                           : 0;
  if (Mnemonic == "EXTZW")
    return Suffix == 'B' ? Bedrock::EXTZW8rr : 0;
  if (Mnemonic == "EXTSW")
    return Suffix == 'B' ? Bedrock::EXTSW8rr : 0;
  return 0;
}

static unsigned extRMOpcode(StringRef Mnemonic, char Suffix) {
  if (Mnemonic == "EXTZQ")
    return Suffix == 'B'   ? Bedrock::EXTZQ8rm
           : Suffix == 'W' ? Bedrock::EXTZQ16rm
           : Suffix == 'L' ? Bedrock::EXTZQ32rm
                           : 0;
  if (Mnemonic == "EXTSQ")
    return Suffix == 'B'   ? Bedrock::EXTSQ8rm
           : Suffix == 'W' ? Bedrock::EXTSQ16rm
           : Suffix == 'L' ? Bedrock::EXTSQ32rm
                           : 0;
  if (Mnemonic == "EXTZL")
    return Suffix == 'B'   ? Bedrock::EXTZL8rm
           : Suffix == 'W' ? Bedrock::EXTZL16rm
                           : 0;
  if (Mnemonic == "EXTSL")
    return Suffix == 'B'   ? Bedrock::EXTSL8rm
           : Suffix == 'W' ? Bedrock::EXTSL16rm
                           : 0;
  if (Mnemonic == "EXTZW")
    return Suffix == 'B' ? Bedrock::EXTZW8rm : 0;
  if (Mnemonic == "EXTSW")
    return Suffix == 'B' ? Bedrock::EXTSW8rm : 0;
  return 0;
}

static unsigned extMROpcode(StringRef Mnemonic, char Suffix) {
  if (Mnemonic == "EXTZQ")
    return Suffix == 'B'   ? Bedrock::EXTZQ8mr
           : Suffix == 'W' ? Bedrock::EXTZQ16mr
           : Suffix == 'L' ? Bedrock::EXTZQ32mr
                           : 0;
  if (Mnemonic == "EXTSQ")
    return Suffix == 'B'   ? Bedrock::EXTSQ8mr
           : Suffix == 'W' ? Bedrock::EXTSQ16mr
           : Suffix == 'L' ? Bedrock::EXTSQ32mr
                           : 0;
  if (Mnemonic == "EXTZL")
    return Suffix == 'B'   ? Bedrock::EXTZL8mr
           : Suffix == 'W' ? Bedrock::EXTZL16mr
                           : 0;
  if (Mnemonic == "EXTSL")
    return Suffix == 'B'   ? Bedrock::EXTSL8mr
           : Suffix == 'W' ? Bedrock::EXTSL16mr
                           : 0;
  if (Mnemonic == "EXTZW")
    return Suffix == 'B' ? Bedrock::EXTZW8mr : 0;
  if (Mnemonic == "EXTSW")
    return Suffix == 'B' ? Bedrock::EXTSW8mr : 0;
  return 0;
}

static unsigned cmpOpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::CMP8rr;
  case 'W':
    return Bedrock::CMP16rr;
  case 'L':
    return Bedrock::CMP32rr;
  case 'Q':
    return Bedrock::CMP64rr;
  }
  llvm_unreachable("invalid size suffix");
}

static unsigned fmovRROpcode(char Suffix) {
  switch (Suffix) {
  case 'S':
    return Bedrock::FMOV32rr;
  case 'D':
    return Bedrock::FMOV64rr;
  }
  llvm_unreachable("invalid floating-point size suffix");
}

static unsigned fmovRMOpcode(char Suffix) {
  switch (Suffix) {
  case 'S':
    return Bedrock::FMOV32rm;
  case 'D':
    return Bedrock::FMOV64rm;
  }
  llvm_unreachable("invalid floating-point size suffix");
}

static unsigned fmovMROpcode(char Suffix) {
  switch (Suffix) {
  case 'S':
    return Bedrock::FMOV32mr;
  case 'D':
    return Bedrock::FMOV64mr;
  }
  llvm_unreachable("invalid floating-point size suffix");
}

static unsigned fbinRROpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("FADD", Suffix == 'S' ? Bedrock::FADD32rr : Bedrock::FADD64rr)
      .Case("FSUB", Suffix == 'S' ? Bedrock::FSUB32rr : Bedrock::FSUB64rr)
      .Case("FMUL", Suffix == 'S' ? Bedrock::FMUL32rr : Bedrock::FMUL64rr)
      .Case("FDIV", Suffix == 'S' ? Bedrock::FDIV32rr : Bedrock::FDIV64rr)
      .Default(0);
}

static unsigned fbinRMOpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("FADD", Suffix == 'S' ? Bedrock::FADD32rm : Bedrock::FADD64rm)
      .Case("FSUB", Suffix == 'S' ? Bedrock::FSUB32rm : Bedrock::FSUB64rm)
      .Case("FMUL", Suffix == 'S' ? Bedrock::FMUL32rm : Bedrock::FMUL64rm)
      .Case("FDIV", Suffix == 'S' ? Bedrock::FDIV32rm : Bedrock::FDIV64rm)
      .Default(0);
}

static unsigned ffmaRRROpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("FMADD", Suffix == 'S' ? Bedrock::FMADD32rrr : Bedrock::FMADD64rrr)
      .Case("FMSUB", Suffix == 'S' ? Bedrock::FMSUB32rrr : Bedrock::FMSUB64rrr)
      .Case("FNMADD",
            Suffix == 'S' ? Bedrock::FNMADD32rrr : Bedrock::FNMADD64rrr)
      .Case("FNMSUB",
            Suffix == 'S' ? Bedrock::FNMSUB32rrr : Bedrock::FNMSUB64rrr)
      .Default(0);
}

static unsigned ffmaRMROpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("FMADD", Suffix == 'S' ? Bedrock::FMADD32rmr : Bedrock::FMADD64rmr)
      .Case("FMSUB", Suffix == 'S' ? Bedrock::FMSUB32rmr : Bedrock::FMSUB64rmr)
      .Case("FNMADD",
            Suffix == 'S' ? Bedrock::FNMADD32rmr : Bedrock::FNMADD64rmr)
      .Case("FNMSUB",
            Suffix == 'S' ? Bedrock::FNMSUB32rmr : Bedrock::FNMSUB64rmr)
      .Default(0);
}

static unsigned ffmaMRROpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("FMADD", Suffix == 'S' ? Bedrock::FMADD32mrr : Bedrock::FMADD64mrr)
      .Case("FMSUB", Suffix == 'S' ? Bedrock::FMSUB32mrr : Bedrock::FMSUB64mrr)
      .Case("FNMADD",
            Suffix == 'S' ? Bedrock::FNMADD32mrr : Bedrock::FNMADD64mrr)
      .Case("FNMSUB",
            Suffix == 'S' ? Bedrock::FNMSUB32mrr : Bedrock::FNMSUB64mrr)
      .Default(0);
}

static unsigned fcmpRROpcode(StringRef Mnemonic, char Suffix) {
  if (Mnemonic != "FCMP")
    return 0;
  return Suffix == 'S' ? Bedrock::FCMP32rr : Bedrock::FCMP64rr;
}

static unsigned fcmpRMOpcode(StringRef Mnemonic, char Suffix) {
  if (Mnemonic != "FCMP")
    return 0;
  return Suffix == 'S' ? Bedrock::FCMP32rm : Bedrock::FCMP64rm;
}

static unsigned funaryRROpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("FABS", Suffix == 'S' ? Bedrock::FABS32rr : Bedrock::FABS64rr)
      .Case("FNEG", Suffix == 'S' ? Bedrock::FNEG32rr : Bedrock::FNEG64rr)
      .Case("FSQRT", Suffix == 'S' ? Bedrock::FSQRT32rr : Bedrock::FSQRT64rr)
      .Default(0);
}

static unsigned funaryRMOpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("FABS", Suffix == 'S' ? Bedrock::FABS32rm : Bedrock::FABS64rm)
      .Case("FNEG", Suffix == 'S' ? Bedrock::FNEG32rm : Bedrock::FNEG64rm)
      .Case("FSQRT", Suffix == 'S' ? Bedrock::FSQRT32rm : Bedrock::FSQRT64rm)
      .Default(0);
}

static unsigned funaryMROpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("FABS", Suffix == 'S' ? Bedrock::FABS32mr : Bedrock::FABS64mr)
      .Case("FNEG", Suffix == 'S' ? Bedrock::FNEG32mr : Bedrock::FNEG64mr)
      .Case("FSQRT", Suffix == 'S' ? Bedrock::FSQRT32mr : Bedrock::FSQRT64mr)
      .Default(0);
}

static unsigned fcopySignRROpcode(StringRef Mnemonic, char Suffix) {
  if (Mnemonic != "FCOPYSIGN")
    return 0;
  return Suffix == 'S' ? Bedrock::FCOPYSIGN32rr : Bedrock::FCOPYSIGN64rr;
}

static unsigned fcvtOpcode(StringRef Mnemonic, MCRegister SrcReg,
                           MCRegister DstReg) {
  bool IsUnsigned = Mnemonic == "FCVTU";
  if (!IsUnsigned && Mnemonic != "FCVT")
    return 0;

  if (isDReg(SrcReg) && isFReg(DstReg))
    return IsUnsigned ? Bedrock::FCVTUI64toF64 : Bedrock::FCVTSI64toF64;
  if (isFReg(SrcReg) && isDReg(DstReg))
    return IsUnsigned ? Bedrock::FCVTF64toUI64 : Bedrock::FCVTF64toSI64;
  if (isFReg(SrcReg) && isFReg(DstReg))
    return IsUnsigned ? Bedrock::FCVTU32to64 : Bedrock::FCVT32to64;
  return 0;
}

static unsigned fetchOpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("FETCHADD", Suffix == 'B'   ? Bedrock::FETCHADD8
                        : Suffix == 'W' ? Bedrock::FETCHADD16
                        : Suffix == 'L' ? Bedrock::FETCHADD32
                                        : Bedrock::FETCHADD64)
      .Case("FETCHSUB", Suffix == 'B'   ? Bedrock::FETCHSUB8
                        : Suffix == 'W' ? Bedrock::FETCHSUB16
                        : Suffix == 'L' ? Bedrock::FETCHSUB32
                                        : Bedrock::FETCHSUB64)
      .Case("FETCHAND", Suffix == 'B'   ? Bedrock::FETCHAND8
                        : Suffix == 'W' ? Bedrock::FETCHAND16
                        : Suffix == 'L' ? Bedrock::FETCHAND32
                                        : Bedrock::FETCHAND64)
      .Case("FETCHOR", Suffix == 'B'   ? Bedrock::FETCHOR8
                       : Suffix == 'W' ? Bedrock::FETCHOR16
                       : Suffix == 'L' ? Bedrock::FETCHOR32
                                       : Bedrock::FETCHOR64)
      .Case("FETCHXOR", Suffix == 'B'   ? Bedrock::FETCHXOR8
                        : Suffix == 'W' ? Bedrock::FETCHXOR16
                        : Suffix == 'L' ? Bedrock::FETCHXOR32
                                        : Bedrock::FETCHXOR64)
      .Default(0);
}

static unsigned cmpXchgOpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::CMPXCHG8;
  case 'W':
    return Bedrock::CMPXCHG16;
  case 'L':
    return Bedrock::CMPXCHG32;
  case 'Q':
    return Bedrock::CMPXCHG64;
  }
  llvm_unreachable("invalid size suffix");
}

static std::optional<BedrockCC::CondCode> parseCondCode(StringRef Name) {
  return StringSwitch<std::optional<BedrockCC::CondCode>>(Name)
      .Case("T", BedrockCC::T)
      .Case("F", BedrockCC::F)
      .Case("EQ", BedrockCC::EQ)
      .Case("NE", BedrockCC::NE)
      .Case("ULT", BedrockCC::ULT)
      .Case("UGE", BedrockCC::UGE)
      .Case("MI", BedrockCC::MI)
      .Case("PL", BedrockCC::PL)
      .Case("VS", BedrockCC::VS)
      .Case("VC", BedrockCC::VC)
      .Case("ULE", BedrockCC::ULE)
      .Case("UGT", BedrockCC::UGT)
      .Case("LT", BedrockCC::LT)
      .Case("GE", BedrockCC::GE)
      .Case("LE", BedrockCC::LE)
      .Case("GT", BedrockCC::GT)
      .Default(std::nullopt);
}

static unsigned noOperandOpcode(StringRef Name) {
  return StringSwitch<unsigned>(Name)
      .Case("AFENCE", Bedrock::AFENCE)
      .Case("HALT", Bedrock::HALT)
      .Case("NOP", Bedrock::NOP)
      .Case("RET", Bedrock::RET)
      .Default(0);
}

namespace {

constexpr uint16_t BedrockRepgStartPrefix = 0x70u;
constexpr uint16_t BedrockRepgEndPrefix = 0x78u;
constexpr size_t BedrockRepgMaxGroupInstructions = 32u;
constexpr size_t BedrockRepgIcacheLineBytes = 64u;

struct RepgEncodedInstruction {
  uint16_t Words[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
  size_t WordCount = 0;
};

} // end anonymous namespace

static bool prefixIsRepeat(uint16_t Prefix) {
  return Prefix >= 0x80u ||
         (Prefix >= BedrockRepgStartPrefix && Prefix <= BedrockRepgEndPrefix);
}

static bool prefixWordHasRepeat(uint16_t Word) {
  uint16_t Low = Word & 0x00ffu;
  uint16_t High = (Word >> 8) & 0x00ffu;
  return prefixIsRepeat(Low) || prefixIsRepeat(High);
}

static bool mergePrefixByte(uint16_t &Word, uint16_t Prefix) {
  uint16_t Low = Word & 0x00ffu;
  uint16_t High = (Word >> 8) & 0x00ffu;
  if (Prefix == 0u || Low == Prefix || High == Prefix)
    return true;
  if (Low == 0u) {
    Word = (Word & 0xff00u) | Prefix;
    return true;
  }
  if (High == 0u) {
    Word = (Word & 0x00ffu) | (Prefix << 8);
    return true;
  }
  return false;
}

static bool addPrefixToInstruction(RepgEncodedInstruction &Instruction,
                                   uint16_t Prefix) {
  if (Instruction.WordCount == 0)
    return false;
  if ((Instruction.Words[0] & BEDROCK_WORD0_PREFIX_BIT) != 0)
    return mergePrefixByte(Instruction.Words[1], Prefix);
  if (Instruction.WordCount + 1 > BEDROCK_MAX_INSTRUCTION_WORDS)
    return false;

  for (size_t I = Instruction.WordCount; I > 1; --I)
    Instruction.Words[I] = Instruction.Words[I - 1];
  Instruction.Words[1] = Prefix;
  ++Instruction.WordCount;
  Instruction.Words[0] =
      (Instruction.Words[0] & ~uint16_t(BEDROCK_WORD0_LENGTH_MASK)) |
      BEDROCK_WORD0_PREFIX_BIT | uint16_t((Instruction.WordCount - 1) << 12);
  return true;
}

static bool repgFormIsForbidden(const bedrock_form_desc *Form) {
  if (!Form)
    return true;

  StringRef Mnemonic(Form->mnemonic ? Form->mnemonic : "");
  StringRef Category(Form->category ? Form->category : "");
  StringRef Group(Form->group ? Form->group : "");
  StringRef ID(Form->id ? Form->id : "");
  if (Category.contains_insensitive("control") ||
      Category.contains_insensitive("system") ||
      Category.contains_insensitive("atomic"))
    return true;
  if (Group.contains_insensitive("TLB") ||
      Group.contains_insensitive("CACHE") ||
      Group.contains_insensitive("FENCE"))
    return true;
  if (ID.contains_insensitive("TLB") || ID.contains_insensitive("CACHE") ||
      ID.contains_insensitive("FENCE"))
    return true;
  if (Mnemonic.equals_insensitive("PUSH") ||
      Mnemonic.equals_insensitive("POP") ||
      Mnemonic.equals_insensitive("PUSHM") ||
      Mnemonic.equals_insensitive("POPM"))
    return true;
  if (Mnemonic.starts_with_insensitive("MOVSET") ||
      Mnemonic.starts_with_insensitive("XCHGSET") ||
      Mnemonic.starts_with_insensitive("XCHG"))
    return true;
  if (Mnemonic.equals_insensitive("WAIT") ||
      Mnemonic.equals_insensitive("YIELD") ||
      Mnemonic.equals_insensitive("CPUID"))
    return true;
  return false;
}

static void splitRepgInstructionOperands(StringRef Line,
                                         SmallVectorImpl<std::string> &Out) {
  Line = Line.trim();
  size_t Pos = 0;
  while (Pos < Line.size() &&
         !std::isspace(static_cast<unsigned char>(Line[Pos])))
    ++Pos;
  Line = Line.drop_front(Pos).trim();

  while (!Line.empty()) {
    int Depth = 0;
    size_t I = 0;
    for (; I < Line.size(); ++I) {
      if (Line[I] == '[' || Line[I] == '{')
        ++Depth;
      else if (Line[I] == ']' || Line[I] == '}')
        --Depth;
      else if (Line[I] == ',' && Depth == 0)
        break;
    }
    Out.push_back(std::string(Line.take_front(I).trim()));
    Line = I == Line.size() ? StringRef() : Line.drop_front(I + 1).trim();
  }
}

static bool operandIsSelectedDReg(StringRef Operand, unsigned Counter) {
  Operand = Operand.trim();
  if (Operand.size() < 2 ||
      std::toupper(static_cast<unsigned char>(Operand[0])) != 'D')
    return false;
  unsigned Value = 0;
  if (Operand.drop_front().getAsInteger(10, Value))
    return false;
  return Value == Counter;
}

static bool repgfWritesSelectedCounter(const bedrock_form_desc *Form,
                                       StringRef Line, unsigned Counter) {
  SmallVector<std::string, 5> Operands;
  splitRepgInstructionOperands(Line, Operands);
  StringRef Mnemonic(Form && Form->mnemonic ? Form->mnemonic : "");
  if (Operands.empty())
    return false;
  if (Mnemonic.equals_insensitive("CMP") ||
      Mnemonic.equals_insensitive("TEST") ||
      Mnemonic.equals_insensitive("BTEST") ||
      Mnemonic.equals_insensitive("PREFETCH"))
    return false;
  if (Mnemonic.equals_insensitive("DIVMODU") ||
      Mnemonic.equals_insensitive("DIVMODS")) {
    return (Operands.size() >= 2 &&
            operandIsSelectedDReg(Operands[Operands.size() - 1], Counter)) ||
           (Operands.size() >= 3 &&
            operandIsSelectedDReg(Operands[Operands.size() - 2], Counter));
  }
  if (Mnemonic.starts_with_insensitive("XCHG")) {
    for (StringRef Operand : Operands)
      if (operandIsSelectedDReg(Operand, Counter))
        return true;
    return false;
  }
  return operandIsSelectedDReg(Operands.back(), Counter);
}

static bool isAsmIdentChar(char Ch) {
  return std::isalnum(static_cast<unsigned char>(Ch)) || Ch == '_' ||
         Ch == '.' || Ch == '$';
}

static bool repgfUsesPCRelativeAddressing(StringRef Line) {
  int BracketDepth = 0;
  for (size_t I = 0; I < Line.size(); ++I) {
    if (Line[I] == '[') {
      ++BracketDepth;
      continue;
    }
    if (Line[I] == ']') {
      if (BracketDepth > 0)
        --BracketDepth;
      continue;
    }
    if (BracketDepth == 0 || I + 2 > Line.size())
      continue;

    if (std::toupper(static_cast<unsigned char>(Line[I])) != 'P' ||
        std::toupper(static_cast<unsigned char>(Line[I + 1])) != 'C')
      continue;

    bool BeforeOK = I == 0 || !isAsmIdentChar(Line[I - 1]);
    bool AfterOK = I + 2 == Line.size() || !isAsmIdentChar(Line[I + 2]);
    if (BeforeOK && AfterOK)
      return true;
  }
  return false;
}

static bool validateRepgInstruction(BedrockAsmParser &Parser, SMLoc Loc,
                                    StringRef Line,
                                    const bedrock_form_desc *Form) {
  if (!Form)
    return Parser.Error(Loc, "REPG could not classify grouped instruction `" +
                                 Twine(Line) + "`");
  if (repgFormIsForbidden(Form))
    return Parser.Error(Loc, "REPG instruction `" + Twine(Line) +
                                 "` is not eligible for grouped repeat");
  return false;
}

static bool validateRepgfInstruction(BedrockAsmParser &Parser, SMLoc Loc,
                                     unsigned Counter, StringRef Line,
                                     const bedrock_form_desc *Form) {
  if (validateRepgInstruction(Parser, Loc, Line, Form))
    return true;
  if (repgfUsesPCRelativeAddressing(Line))
    return Parser.Error(Loc, "REPGF instruction `" + Twine(Line) +
                                 "` uses PC-relative addressing");
  if (repgfWritesSelectedCounter(Form, Line, Counter))
    return Parser.Error(Loc, "REPGF instruction `" + Twine(Line) +
                                 "` writes the selected counter D" +
                                 Twine(Counter));
  return false;
}

static std::optional<uint64_t> currentSectionOffset(MCStreamer &Out) {
  if (!Out.isObj())
    return std::nullopt;

  auto &ObjectOut = static_cast<MCObjectStreamer &>(Out);
  MCFragment *Fragment = ObjectOut.getCurrentFragment();
  if (!Fragment || !Fragment->getParent())
    return std::nullopt;

  MCAssembler &Assembler = ObjectOut.getAssembler();
  uint64_t Offset = 0;
  for (MCFragment &F : *Fragment->getParent()) {
    if (&F == Fragment)
      return Offset + F.getFixedSize();

    if (F.getKind() == MCFragment::FT_Align) {
      Offset += F.getFixedSize();
      uint64_t Padding = offsetToAlignment(Offset, F.getAlignment());
      if (Padding > F.getAlignMaxBytesToEmit())
        Padding = 0;
      Offset += Padding;
      continue;
    }
    Offset += Assembler.computeFragmentSize(F);
  }
  return std::nullopt;
}

bool BedrockAsmParser::emitRepgBlock(SMLoc Loc, const BedrockOperand &Block,
                                     MCStreamer &Out) {
  ArrayRef<std::string> Items = Block.getRepgItems();
  if (Items.empty() || Items.size() > BedrockRepgMaxGroupInstructions)
    return Error(Loc, "REPG requires at least one grouped instruction within "
                      "one 64-byte I-cache line");

  SmallVector<RepgEncodedInstruction, 32> Encoded;
  Encoded.resize(Items.size());
  for (size_t I = 0; I != Items.size(); ++I) {
    const bedrock_form_desc *Form = nullptr;
    int Status = bedrock_assemble_line(Items[I].c_str(), Encoded[I].Words,
                                       BEDROCK_MAX_INSTRUCTION_WORDS,
                                       &Encoded[I].WordCount, &Form);
    if (Status != BEDROCK_OK || Encoded[I].WordCount == 0 ||
        Form == nullptr ||
        !compactImmEA6Encoding(Encoded[I].Words, Encoded[I].WordCount, Form))
      return Error(Loc, "cannot assemble REPG instruction `" + Twine(Items[I]) +
                            "`");

    if ((Encoded[I].Words[0] & BEDROCK_WORD0_PREFIX_BIT) != 0 &&
        prefixWordHasRepeat(Encoded[I].Words[1]))
      return Error(Loc, "nested repeat prefixes are not supported inside REPG "
                        "blocks");

    if (Block.isRepgFast()) {
      if (validateRepgfInstruction(*this, Loc, Block.getRepgCounter(), Items[I],
                                   Form))
        return true;
    } else if (validateRepgInstruction(*this, Loc, Items[I], Form)) {
      return true;
    }
  }

  if (!addPrefixToInstruction(Encoded.front(),
                              BedrockRepgStartPrefix |
                                  uint16_t(Block.getRepgCounter() & 0x07u)))
    return Error(Loc, "first REPG instruction cannot fit its prefix within "
                      "the 8-word instruction limit");
  if (!addPrefixToInstruction(Encoded.back(), BedrockRepgEndPrefix))
    return Error(Loc, "final REPG instruction cannot fit its ENDG prefix "
                      "within the 8-word instruction limit");

  size_t TotalWords = 0;
  for (const RepgEncodedInstruction &Instruction : Encoded)
    TotalWords += Instruction.WordCount;
  size_t TotalBytes = TotalWords * 2;
  if (TotalBytes > BedrockRepgIcacheLineBytes)
    return Error(Loc, "REPG group exceeds one 64-byte I-cache line");

  if (std::optional<uint64_t> Offset = currentSectionOffset(Out)) {
    uint64_t LineOffset = *Offset & (BedrockRepgIcacheLineBytes - 1);
    if (LineOffset + TotalBytes > BedrockRepgIcacheLineBytes)
      return Error(Loc, "REPG group crosses a 64-byte I-cache line; align it "
                        "explicitly before the block");
  }

  SmallString<128> Bytes;
  raw_svector_ostream OS(Bytes);
  for (const RepgEncodedInstruction &Instruction : Encoded)
    for (size_t I = 0; I != Instruction.WordCount; ++I)
      support::endian::write<uint16_t>(OS, Instruction.Words[I],
                                       llvm::endianness::little);

  Out.emitBytes(Bytes);
  if (Out.isObj()) {
    auto &ObjectOut = static_cast<MCObjectStreamer &>(Out);
    ObjectOut.getCurrentSectionOnly()->setHasInstructions(true);
    ObjectOut.getCurrentFragment()->setHasInstructions(getSTI());
  }
  return false;
}

bool BedrockAsmParser::matchAndEmitInstruction(SMLoc IdLoc, unsigned &Opcode,
                                               OperandVector &Operands,
                                               MCStreamer &Out,
                                               uint64_t &ErrorInfo,
                                               bool MatchingInlineAsm) {
  (void)Opcode;
  (void)ErrorInfo;
  (void)MatchingInlineAsm;
  if (Operands.empty())
    return Error(IdLoc, "missing instruction mnemonic");

  MCInst Inst;

  if (operand(Operands, 0).isRawLine()) {
    const BedrockOperand &Raw = operand(Operands, 0);
    if (Raw.getRawWords().empty())
      return Error(IdLoc, "cannot assemble Bedrock instruction `" +
                              Twine(Raw.getRawLine()) + "`");
    setEncodedInstWords(Inst, Raw.getRawWords(), Raw.getRawFixups());
    return emitInst(Inst, IdLoc, Out);
  }

  if (!operand(Operands, 0).isToken())
    return Error(IdLoc, "missing instruction mnemonic");

  StringRef Name = operand(Operands, 0).getToken();
  char Suffix;
  StringRef BaseName = Name.split('.').first;

  if (Name == "REPG" || Name == "REPGF") {
    if (Operands.size() != 2 || !operand(Operands, 1).isRepgBlock())
      return Error(IdLoc, "invalid REPG block");
    return emitRepgBlock(IdLoc, operand(Operands, 1), Out);
  }

  if (unsigned Opc = noOperandOpcode(Name)) {
    if (expectOperandCount(*this, IdLoc, Operands, 0))
      return true;
    Inst.setOpcode(Opc);
    return emitInst(Inst, IdLoc, Out);
  }

  if (Name == "CALL") {
    if (expectOperandCount(*this, IdLoc, Operands, 1))
      return true;
    const BedrockOperand &Target = operand(Operands, 1);
    if (Target.isReg()) {
      Inst.setOpcode(Bedrock::CALLind);
      Inst.addOperand(MCOperand::createReg(Target.getReg()));
      return emitInst(Inst, IdLoc, Out);
    }
    if (Target.getRelocName() == "PCREL16")
      Inst.setOpcode(Bedrock::CALLpcrel16);
    else {
      if (expectReloc(*this, Target, "PCREL32"))
        return true;
      Inst.setOpcode(Bedrock::CALLpcrel);
    }
    addExprOrImm(Inst, Target.getImm());
    return emitInst(Inst, IdLoc, Out);
  }

  if (Name == "JMP.W") {
    if (expectOperandCount(*this, IdLoc, Operands, 1))
      return true;
    const BedrockOperand &Target = operand(Operands, 1);
    if (expectReloc(*this, Target, "WORD_PCREL16"))
      return true;
    Inst.setOpcode(Bedrock::JMP);
    addExprOrImm(Inst, Target.getImm());
    return emitInst(Inst, IdLoc, Out);
  }

  if (Name.starts_with("J") && Name.ends_with(".W") && Name.size() > 3) {
    if (expectOperandCount(*this, IdLoc, Operands, 1))
      return true;
    StringRef CondName = Name.drop_front().drop_back(2);
    std::optional<BedrockCC::CondCode> CC = parseCondCode(CondName);
    if (!CC)
      return Error(IdLoc, "invalid Bedrock condition code");
    const BedrockOperand &Target = operand(Operands, 1);
    if (expectReloc(*this, Target, "WORD_PCREL16"))
      return true;
    Inst.setOpcode(Bedrock::JCC);
    addExprOrImm(Inst, Target.getImm());
    Inst.addOperand(MCOperand::createImm(*CC));
    return emitInst(Inst, IdLoc, Out);
  }

  if (Name == "CLR" || !parseSuffix(Name, "CLR", Suffix)) {
    if (expectOperandCount(*this, IdLoc, Operands, 1))
      return true;
    const BedrockOperand &Dst = operand(Operands, 1);
    if (Dst.isReg()) {
      Inst.setOpcode(Bedrock::CLR64r);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      return emitInst(Inst, IdLoc, Out);
    }
    if (Dst.isMem()) {
      Inst.setOpcode(Bedrock::CLRm);
      addMem(Inst, Dst);
      return emitInst(Inst, IdLoc, Out);
    }
    return Error(IdLoc, "invalid CLR operand");
  }

  if (Name == "FCLR") {
    if (expectOperandCount(*this, IdLoc, Operands, 1))
      return true;
    const BedrockOperand &Dst = operand(Operands, 1);
    if (Dst.isReg()) {
      Inst.setOpcode(Bedrock::FCLRr);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      return emitInst(Inst, IdLoc, Out);
    }
    if (Dst.isMem()) {
      Inst.setOpcode(Bedrock::FCLRm);
      addMem(Inst, Dst);
      return emitInst(Inst, IdLoc, Out);
    }
    return Error(IdLoc, "invalid FCLR operand");
  }

  if (!parseFPSuffix(Name, "FCOPYSIGN", Suffix)) {
    if (expectOperandCount(*this, IdLoc, Operands, 3))
      return true;
    const BedrockOperand &Sign = operand(Operands, 1);
    const BedrockOperand &Mag = operand(Operands, 2);
    const BedrockOperand &Dst = operand(Operands, 3);
    unsigned Opc = fcopySignRROpcode(BaseName, Suffix);
    if (Opc && Sign.isReg() && Mag.isReg() && Dst.isReg()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Mag.getReg()));
      Inst.addOperand(MCOperand::createReg(Sign.getReg()));
      return emitInst(Inst, IdLoc, Out);
    }
    return Error(IdLoc, "invalid FCOPYSIGN operands");
  }

  if (!parseFPSuffix(Name, BaseName, Suffix) &&
      ffmaRRROpcode(BaseName, Suffix)) {
    if (expectOperandCount(*this, IdLoc, Operands, 3))
      return true;
    const BedrockOperand &Lhs = operand(Operands, 1);
    const BedrockOperand &Rhs = operand(Operands, 2);
    const BedrockOperand &Dst = operand(Operands, 3);
    unsigned Opc;
    if (Lhs.isReg() && Rhs.isReg() && Dst.isReg()) {
      Inst.setOpcode(ffmaRRROpcode(BaseName, Suffix));
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Lhs.getReg()));
      Inst.addOperand(MCOperand::createReg(Rhs.getReg()));
      return emitInst(Inst, IdLoc, Out);
    }
    Opc = ffmaRMROpcode(BaseName, Suffix);
    if (Opc && Lhs.isReg() && Rhs.isMem() && Dst.isReg()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Lhs.getReg()));
      addMem(Inst, Rhs);
      return emitInst(Inst, IdLoc, Out);
    }
    Opc = ffmaMRROpcode(BaseName, Suffix);
    if (Opc && Lhs.isMem() && Rhs.isReg() && Dst.isReg()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      addMem(Inst, Lhs);
      Inst.addOperand(MCOperand::createReg(Rhs.getReg()));
      return emitInst(Inst, IdLoc, Out);
    }
    return Error(IdLoc, "invalid FMA operands");
  }

  if (Name.contains('/')) {
    unsigned Order;
    if (!parseAtomicSuffixOrder(Name, BaseName, Suffix, Order)) {
      unsigned Opc = fetchOpcode(BaseName, Suffix);
      if (Opc) {
        if (expectOperandCount(*this, IdLoc, Operands, 2))
          return true;
        const BedrockOperand &Src = operand(Operands, 1);
        const BedrockOperand &Dst = operand(Operands, 2);
        if (Src.isReg() && Dst.isMem()) {
          Inst.setOpcode(Opc);
          Inst.addOperand(MCOperand::createReg(Src.getReg()));
          Inst.addOperand(MCOperand::createReg(Src.getReg()));
          addMem(Inst, Dst);
          Inst.addOperand(MCOperand::createImm(Order));
          return emitInst(Inst, IdLoc, Out);
        }
        return Error(IdLoc, "invalid atomic fetch operands");
      }

      if (BaseName == "CMPXCHG") {
        if (expectOperandCount(*this, IdLoc, Operands, 3))
          return true;
        const BedrockOperand &Expected = operand(Operands, 1);
        const BedrockOperand &Desired = operand(Operands, 2);
        const BedrockOperand &Memory = operand(Operands, 3);
        if (Expected.isReg() && Desired.isReg() && Memory.isMem()) {
          Inst.setOpcode(cmpXchgOpcode(Suffix));
          Inst.addOperand(MCOperand::createReg(Expected.getReg()));
          Inst.addOperand(MCOperand::createReg(Expected.getReg()));
          Inst.addOperand(MCOperand::createReg(Desired.getReg()));
          addMem(Inst, Memory);
          Inst.addOperand(MCOperand::createImm(Order));
          return emitInst(Inst, IdLoc, Out);
        }
        return Error(IdLoc, "invalid CMPXCHG operands");
      }
    }
  }

  if (!parseSuffix(Name, BaseName, Suffix)) {
    unsigned Opc = incDecROpcode(BaseName, Suffix);
    if (Opc) {
      if (expectOperandCount(*this, IdLoc, Operands, 1))
        return true;
      const BedrockOperand &Dst = operand(Operands, 1);
      if (Dst.isReg()) {
        Inst.setOpcode(Opc);
        Inst.addOperand(MCOperand::createReg(Dst.getReg()));
        Inst.addOperand(MCOperand::createReg(Dst.getReg()));
        return emitInst(Inst, IdLoc, Out);
      }

      Opc = incDecMOpcode(BaseName, Suffix);
      if (Dst.isMem()) {
        Inst.setOpcode(Opc);
        addMem(Inst, Dst);
        return emitInst(Inst, IdLoc, Out);
      }
      return Error(IdLoc, "invalid INC/DEC operand");
    }
  }

  if (expectOperandCount(*this, IdLoc, Operands, 2))
    return true;

  if (!parseSuffix(Name, "MOV", Suffix)) {
    const BedrockOperand &Src = operand(Operands, 1);
    const BedrockOperand &Dst = operand(Operands, 2);
    if (Dst.isReg()) {
      if (Src.isReg()) {
        Inst.setOpcode(movRROpcode(Suffix));
        Inst.addOperand(MCOperand::createReg(Dst.getReg()));
        Inst.addOperand(MCOperand::createReg(Src.getReg()));
        return emitInst(Inst, IdLoc, Out);
      }
      if (Src.isMem()) {
        Inst.setOpcode(movRMOpcode(Suffix));
        Inst.addOperand(MCOperand::createReg(Dst.getReg()));
        addMem(Inst, Src);
        return emitInst(Inst, IdLoc, Out);
      }
      if (Src.isImm()) {
        if (!Src.getRelocName().empty()) {
          if (Suffix != 'Q' || expectReloc(*this, Src, "ABS64"))
            return true;
          Inst.setOpcode(Bedrock::MOV64abs);
        } else {
          Inst.setOpcode(movRIOpcode(Suffix));
        }
        Inst.addOperand(MCOperand::createReg(Dst.getReg()));
        addExprOrImm(Inst, Src.getImm());
        return emitInst(Inst, IdLoc, Out);
      }
    }
    if (Dst.isMem() && Src.isReg()) {
      Inst.setOpcode(movMROpcode(Suffix));
      Inst.addOperand(MCOperand::createReg(Src.getReg()));
      addMem(Inst, Dst);
      return emitInst(Inst, IdLoc, Out);
    }
    if (Dst.isMem() && Src.isImm()) {
      Inst.setOpcode(movMIOpcode(Suffix));
      addExprOrImm(Inst, Src.getImm());
      addMem(Inst, Dst);
      return emitInst(Inst, IdLoc, Out);
    }
    return Error(IdLoc, "invalid MOV operands");
  }

  if (!parseFPSuffix(Name, "FMOV", Suffix)) {
    const BedrockOperand &Src = operand(Operands, 1);
    const BedrockOperand &Dst = operand(Operands, 2);
    if (Dst.isReg()) {
      if (Src.isReg()) {
        Inst.setOpcode(fmovRROpcode(Suffix));
        Inst.addOperand(MCOperand::createReg(Dst.getReg()));
        Inst.addOperand(MCOperand::createReg(Src.getReg()));
        return emitInst(Inst, IdLoc, Out);
      }
      if (Src.isMem()) {
        Inst.setOpcode(fmovRMOpcode(Suffix));
        Inst.addOperand(MCOperand::createReg(Dst.getReg()));
        addMem(Inst, Src);
        return emitInst(Inst, IdLoc, Out);
      }
    }
    if (Dst.isMem() && Src.isReg()) {
      Inst.setOpcode(fmovMROpcode(Suffix));
      Inst.addOperand(MCOperand::createReg(Src.getReg()));
      addMem(Inst, Dst);
      return emitInst(Inst, IdLoc, Out);
    }
    return Error(IdLoc, "invalid FMOV operands");
  }

  if (Name == "FCVT" || Name == "FCVTU") {
    const BedrockOperand &Src = operand(Operands, 1);
    const BedrockOperand &Dst = operand(Operands, 2);
    if (Src.isReg() && Dst.isReg()) {
      unsigned Opc = fcvtOpcode(Name, Src.getReg(), Dst.getReg());
      if (Opc) {
        Inst.setOpcode(Opc);
        Inst.addOperand(MCOperand::createReg(Dst.getReg()));
        Inst.addOperand(MCOperand::createReg(Src.getReg()));
        return emitInst(Inst, IdLoc, Out);
      }
    }
    return Error(IdLoc, "invalid FCVT operands");
  }

  if (!parseFPSuffix(Name, BaseName, Suffix)) {
    unsigned Opc;
    const BedrockOperand &Src = operand(Operands, 1);
    const BedrockOperand &Dst = operand(Operands, 2);
    Opc = fcmpRROpcode(BaseName, Suffix);
    if (Opc && Src.isReg() && Dst.isReg()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Src.getReg()));
      return emitInst(Inst, IdLoc, Out);
    }
    Opc = fcmpRMOpcode(BaseName, Suffix);
    if (Opc && Src.isMem() && Dst.isReg()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      addMem(Inst, Src);
      return emitInst(Inst, IdLoc, Out);
    }

    Opc = funaryRROpcode(BaseName, Suffix);
    if (Opc && Src.isReg() && Dst.isReg()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Src.getReg()));
      return emitInst(Inst, IdLoc, Out);
    }
    Opc = funaryRMOpcode(BaseName, Suffix);
    if (Opc && Src.isMem() && Dst.isReg()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      addMem(Inst, Src);
      return emitInst(Inst, IdLoc, Out);
    }
    Opc = funaryMROpcode(BaseName, Suffix);
    if (Opc && Src.isReg() && Dst.isMem()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Src.getReg()));
      addMem(Inst, Dst);
      return emitInst(Inst, IdLoc, Out);
    }

    Opc = fbinRROpcode(BaseName, Suffix);
    if (Opc && Src.isReg() && Dst.isReg()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Src.getReg()));
      return emitInst(Inst, IdLoc, Out);
    }

    Opc = fbinRMOpcode(BaseName, Suffix);
    if (Opc && Src.isMem() && Dst.isReg()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      addMem(Inst, Src);
      return emitInst(Inst, IdLoc, Out);
    }
    return Error(IdLoc, "invalid floating-point operands");
  }

  if (!parseSuffix(Name, BaseName, Suffix)) {
    unsigned Opc = binRROpcode(BaseName, Suffix);
    const BedrockOperand &Src = operand(Operands, 1);
    const BedrockOperand &Dst = operand(Operands, 2);

    if (Opc && Src.isReg() && Dst.isReg()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Src.getReg()));
      return emitInst(Inst, IdLoc, Out);
    }

    Opc = binRIOpcode(BaseName, Suffix);
    if (Opc && Src.isImm() && Dst.isReg()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      addExprOrImm(Inst, Src.getImm());
      return emitInst(Inst, IdLoc, Out);
    }

    Opc = binRMOpcode(BaseName, Suffix);
    if (Opc && Src.isMem() && Dst.isReg()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      addMem(Inst, Src);
      return emitInst(Inst, IdLoc, Out);
    }

    Opc = binMROpcode(BaseName, Suffix);
    if (Opc && Src.isReg() && Dst.isMem()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Src.getReg()));
      addMem(Inst, Dst);
      return emitInst(Inst, IdLoc, Out);
    }

    Opc = binMIOpcode(BaseName, Suffix);
    if (Opc && Src.isImm() && Dst.isMem()) {
      Inst.setOpcode(Opc);
      addExprOrImm(Inst, Src.getImm());
      addMem(Inst, Dst);
      return emitInst(Inst, IdLoc, Out);
    }

    Opc = shiftOpcode(BaseName, Suffix);
    if (Opc && Src.isImm() && Dst.isReg()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      addExprOrImm(Inst, Src.getImm());
      return emitInst(Inst, IdLoc, Out);
    }

    Opc = shiftRROpcode(BaseName, Suffix);
    if (Opc && Src.isReg() && Dst.isReg()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Src.getReg()));
      return emitInst(Inst, IdLoc, Out);
    }

    Opc = shiftMIOpcode(BaseName, Suffix);
    if (Opc && Src.isImm() && Dst.isMem()) {
      Inst.setOpcode(Opc);
      addExprOrImm(Inst, Src.getImm());
      addMem(Inst, Dst);
      return emitInst(Inst, IdLoc, Out);
    }

    Opc = shiftMROpcode(BaseName, Suffix);
    if (Opc && Src.isReg() && Dst.isMem()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Src.getReg()));
      addMem(Inst, Dst);
      return emitInst(Inst, IdLoc, Out);
    }

    Opc = extRROpcode(BaseName, Suffix);
    if (Opc && Src.isReg() && Dst.isReg()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Src.getReg()));
      return emitInst(Inst, IdLoc, Out);
    }

    Opc = extRMOpcode(BaseName, Suffix);
    if (Opc && Src.isMem() && Dst.isReg()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      addMem(Inst, Src);
      return emitInst(Inst, IdLoc, Out);
    }

    Opc = extMROpcode(BaseName, Suffix);
    if (Opc && Src.isReg() && Dst.isMem()) {
      Inst.setOpcode(Opc);
      Inst.addOperand(MCOperand::createReg(Src.getReg()));
      addMem(Inst, Dst);
      return emitInst(Inst, IdLoc, Out);
    }

    if (BaseName == "CMP" && Src.isReg() && Dst.isReg()) {
      Inst.setOpcode(cmpOpcode(Suffix));
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Src.getReg()));
      return emitInst(Inst, IdLoc, Out);
    }

    if (BaseName == "CMP" && Src.isImm() && Dst.isReg()) {
      Inst.setOpcode(cmpRIOpcode(Suffix));
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      addExprOrImm(Inst, Src.getImm());
      return emitInst(Inst, IdLoc, Out);
    }

    if (BaseName == "CMP" && Src.isImm() && Dst.isMem()) {
      Inst.setOpcode(cmpMIOpcode(Suffix));
      addExprOrImm(Inst, Src.getImm());
      addMem(Inst, Dst);
      return emitInst(Inst, IdLoc, Out);
    }

    if (BaseName == "TEST" && Src.isReg() && Dst.isReg()) {
      Inst.setOpcode(testRROpcode(Suffix));
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      Inst.addOperand(MCOperand::createReg(Src.getReg()));
      return emitInst(Inst, IdLoc, Out);
    }

    if (BaseName == "TEST" && Src.isImm() && Dst.isReg()) {
      Inst.setOpcode(testRIOpcode(Suffix));
      Inst.addOperand(MCOperand::createReg(Dst.getReg()));
      addExprOrImm(Inst, Src.getImm());
      return emitInst(Inst, IdLoc, Out);
    }

    if (BaseName == "TEST" && Src.isImm() && Dst.isMem()) {
      Inst.setOpcode(testMIOpcode(Suffix));
      addExprOrImm(Inst, Src.getImm());
      addMem(Inst, Dst);
      return emitInst(Inst, IdLoc, Out);
    }
  }

  return Error(IdLoc, "invalid Bedrock instruction");
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeBedrockAsmParser() {
  RegisterMCAsmParser<BedrockAsmParser> X(getTheBedrockTarget());
}
