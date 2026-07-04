//===-- BedrockAsmPrinter.cpp - Bedrock Assembly Printer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Bedrock.h"
#include "BedrockMCInstLower.h"
#include "MCTargetDesc/BedrockCondCode.h"
#include "MCTargetDesc/BedrockFixupKinds.h"
#include "MCTargetDesc/BedrockInstPrinter.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "TargetInfo/BedrockTargetInfo.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include <cctype>

extern "C" {
#include "bedrock_asm_disasm.h"
}

using namespace llvm;

#define DEBUG_TYPE "bedrock-asm-printer"

namespace {

constexpr uint16_t BedrockRepgStartPrefix = 0x70u;
constexpr uint16_t BedrockRepgEndPrefix = 0x78u;
constexpr uint64_t BedrockRepgIcacheLineBytes = 64u;

struct RepgEncodedInstruction {
  uint16_t Words[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
  size_t WordCount = 0;
  std::string Text;
};

struct RepgCandidate {
  SmallVector<const MachineInstr *, 8> Body;
  const MachineInstr *Dec = nullptr;
  const MachineInstr *Latch = nullptr;
  Register CountReg;
};

static bool isDReg(Register Reg) {
  return Reg >= Bedrock::D0 && Reg <= Bedrock::D7;
}

static bool isDJccOpcode(unsigned Opcode) {
  return Opcode == Bedrock::DJCC8r || Opcode == Bedrock::DJCC16r ||
         Opcode == Bedrock::DJCC32r || Opcode == Bedrock::DJCC64r;
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

static bool instrTouchesReg(const MachineInstr &MI, Register Reg,
                            const TargetRegisterInfo &TRI) {
  return instrUsesReg(MI, Reg, TRI) || instrDefinesReg(MI, Reg, TRI);
}

static MachineBasicBlock::const_iterator
nextNonDebug(MachineBasicBlock::const_iterator I,
             const MachineBasicBlock &MBB) {
  for (++I; I != MBB.end() && I->isDebugInstr(); ++I)
    ;
  return I;
}

static MachineBasicBlock::const_iterator
prevNonDebug(MachineBasicBlock::const_iterator I,
             const MachineBasicBlock &MBB) {
  while (I != MBB.begin()) {
    --I;
    if (!I->isDebugInstr())
      return I;
  }
  return MBB.end();
}

static MachineBasicBlock::const_iterator
firstNonDebug(const MachineBasicBlock &MBB) {
  auto I = MBB.begin();
  while (I != MBB.end() && I->isDebugInstr())
    ++I;
  return I;
}

static std::optional<int64_t> getCondCodeImm(const MachineOperand &MO) {
  if (MO.isImm())
    return MO.getImm();
  if (MO.isCImm())
    return MO.getCImm()->getSExtValue();
  return std::nullopt;
}

static MachineBasicBlock *
findSingleNonSelfPredecessor(const MachineBasicBlock &MBB) {
  MachineBasicBlock *Found = nullptr;
  SmallPtrSet<MachineBasicBlock *, 4> Seen;
  for (MachineBasicBlock *Pred : MBB.predecessors()) {
    if (!Seen.insert(Pred).second || Pred == &MBB)
      continue;
    if (Found)
      return nullptr;
    Found = Pred;
  }
  return Found;
}

static bool isPositiveCountPretest(const MachineBasicBlock &Header,
                                   const MachineBasicBlock &Body,
                                   Register CountReg,
                                   const TargetRegisterInfo &TRI) {
  if (!Header.isSuccessor(&Body))
    return false;

  auto BranchI = Header.getLastNonDebugInstr();
  if (BranchI == Header.end() || BranchI->getOpcode() != Bedrock::JCC ||
      BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
      BranchI->getOperand(0).getMBB() == &Body)
    return false;

  std::optional<int64_t> BranchCC = getCondCodeImm(BranchI->getOperand(1));
  if (!BranchCC || *BranchCC != BedrockCC::LE)
    return false;

  auto TestI = prevNonDebug(BranchI, Header);
  if (TestI == Header.end() || TestI->getOpcode() != Bedrock::TEST32rr ||
      TestI->getNumOperands() < 2 || !TestI->getOperand(0).isReg() ||
      !TestI->getOperand(1).isReg())
    return false;

  return regsOverlap(TRI, TestI->getOperand(0).getReg(), CountReg) &&
         regsOverlap(TRI, TestI->getOperand(1).getReg(), CountReg);
}

static bool isZeroExitCountPretest(const MachineBasicBlock &Header,
                                   const MachineBasicBlock &Body,
                                   Register CountReg,
                                   const TargetRegisterInfo &TRI) {
  if (!Header.isSuccessor(&Body))
    return false;

  auto BranchI = Header.getLastNonDebugInstr();
  if (BranchI == Header.end() || BranchI->getOpcode() != Bedrock::JCC ||
      BranchI->getNumOperands() < 2 || !BranchI->getOperand(0).isMBB() ||
      BranchI->getOperand(0).getMBB() == &Body)
    return false;

  std::optional<int64_t> BranchCC = getCondCodeImm(BranchI->getOperand(1));
  if (!BranchCC || *BranchCC != BedrockCC::EQ)
    return false;

  auto TestI = prevNonDebug(BranchI, Header);
  if (TestI == Header.end() ||
      (TestI->getOpcode() != Bedrock::TEST32rr &&
       TestI->getOpcode() != Bedrock::TEST64rr) ||
      TestI->getNumOperands() < 2 || !TestI->getOperand(0).isReg() ||
      !TestI->getOperand(1).isReg())
    return false;

  return regsOverlap(TRI, TestI->getOperand(0).getReg(), CountReg) &&
         regsOverlap(TRI, TestI->getOperand(1).getReg(), CountReg);
}

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

static bool isPositiveConstCountSetup(const MachineBasicBlock &Header,
                                      const MachineBasicBlock &Body,
                                      Register CountReg,
                                      const TargetRegisterInfo &TRI) {
  if (!Header.isSuccessor(&Body) || Header.succ_size() != 1)
    return false;

  std::optional<int64_t> LastConst;
  bool SawCountDef = false;
  for (const MachineInstr &MI : Header) {
    if (MI.isDebugInstr() || MI.isPosition())
      continue;
    if (MI.isBranch() || MI.isCall() || MI.isReturn() || MI.isTerminator())
      return false;
    if (!instrTouchesReg(MI, CountReg, TRI))
      continue;

    LastConst = getConstDefForReg(MI, CountReg, TRI);
    SawCountDef = LastConst.has_value();
    if (!SawCountDef)
      return false;
  }

  return SawCountDef && LastConst && *LastConst > 0;
}

static bool hasOrderedMemOperand(const MachineInstr &MI) {
  for (MachineMemOperand *MMO : MI.memoperands())
    if (MMO->isVolatile() || !MMO->isUnordered())
      return true;
  return false;
}

static bool hasSymbolicOperand(const MachineInstr &MI) {
  for (const MachineOperand &MO : MI.operands()) {
    switch (MO.getType()) {
    default:
      break;
    case MachineOperand::MO_MachineBasicBlock:
    case MachineOperand::MO_GlobalAddress:
    case MachineOperand::MO_ExternalSymbol:
    case MachineOperand::MO_BlockAddress:
    case MachineOperand::MO_ConstantPoolIndex:
    case MachineOperand::MO_JumpTableIndex:
      return true;
    }
  }
  return false;
}

static bool isRepgBodyEligible(const MachineInstr &MI, Register CountReg,
                               const TargetRegisterInfo &TRI) {
  if (MI.isDebugInstr() || MI.isPosition() || MI.isPseudo() || MI.isBundle() ||
      MI.isBranch() || MI.isCall() || MI.isReturn() || MI.isTerminator() ||
      hasOrderedMemOperand(MI) || hasSymbolicOperand(MI) ||
      instrTouchesReg(MI, CountReg, TRI) || MI.getPreInstrSymbol() ||
      MI.getPostInstrSymbol())
    return false;
  return true;
}

static bool flagsUsedBeforeDefAfter(const MachineBasicBlock &Body,
                                    const TargetRegisterInfo &TRI) {
  auto Next = std::next(Body.getIterator());
  if (Next == Body.getParent()->end())
    return false;
  for (const MachineInstr &MI : *Next) {
    if (MI.isDebugInstr())
      continue;
    if (instrUsesReg(MI, Bedrock::FLAGS, TRI))
      return true;
    if (instrDefinesReg(MI, Bedrock::FLAGS, TRI))
      return false;
    if (!MI.isPosition())
      return false;
  }
  return false;
}

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

static MCFixupKind getFixupKindForOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return FK_NONE;
  case Bedrock::JMP:
  case Bedrock::JMPWpcrel:
  case Bedrock::JCC:
  case Bedrock::DJCC8r:
  case Bedrock::DJCC16r:
  case Bedrock::DJCC32r:
  case Bedrock::DJCC64r:
  case Bedrock::IJCC32r:
  case Bedrock::IJCC64r:
    return Bedrock::fixup_bedrock_word_pcrel16;
  case Bedrock::JMPLpcrel:
    return Bedrock::fixup_bedrock_word_pcrel32;
  case Bedrock::CALLpcrel16:
    return Bedrock::fixup_bedrock_pcrel16;
  case Bedrock::CALLpcrel:
    return Bedrock::fixup_bedrock_pcrel32;
  case Bedrock::MOV64abs:
    return Bedrock::fixup_bedrock_abs64;
  case Bedrock::MOV32idx4lrm:
    return Bedrock::fixup_bedrock_pcrel32;
  }
}

static const MCExpr *getExprOperand(const MCInst &MI) {
  for (const MCOperand &Operand : MI.getOperands())
    if (Operand.isExpr())
      return Operand.getExpr();
  return nullptr;
}

static bool isIdentChar(char Ch) {
  return std::isalnum(static_cast<unsigned char>(Ch)) || Ch == '_' ||
         Ch == '.' || Ch == '$';
}

static bool isRelocNameChar(char Ch) {
  return std::isalnum(static_cast<unsigned char>(Ch)) || Ch == '_';
}

static std::string placeholderText(MCFixupKind Kind) {
  SmallString<32> Text;
  raw_svector_ostream OS(Text);
  OS << "0x";
  OS.write_hex(Bedrock::getRelocationPlaceholder(Kind));
  return std::string(OS.str());
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

static void setDeclaredWords(uint16_t &Word0, unsigned WordCount) {
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
  setDeclaredWords(Words[0], WordCount);
  return true;
}

static bool replaceRelocExpression(std::string &Line, MCFixupKind Kind) {
  size_t At = Line.find('@');
  if (At == std::string::npos)
    return false;

  size_t Start = At;
  if (Start > 0 && Line[Start - 1] == '"') {
    size_t Quote = Line.rfind('"', Start - 2);
    if (Quote != std::string::npos)
      Start = Quote;
  } else {
    while (Start > 0 && isIdentChar(Line[Start - 1]))
      --Start;
  }

  size_t End = At + 1;
  while (End < Line.size() && isRelocNameChar(Line[End]))
    ++End;
  if (End < Line.size() && (Line[End] == '+' || Line[End] == '-')) {
    ++End;
    while (End < Line.size() &&
           std::isspace(static_cast<unsigned char>(Line[End])))
      ++End;
    while (End < Line.size() &&
           (std::isalnum(static_cast<unsigned char>(Line[End])) ||
            Line[End] == 'x' || Line[End] == 'X'))
      ++End;
  }

  Line.replace(Start, End - Start, placeholderText(Kind));
  return true;
}

static std::string printMCInstForEncoding(const MCInst &MI,
                                          const MCAsmInfo &MAI,
                                          const MCInstrInfo &MII,
                                          const MCRegisterInfo &MRI,
                                          const MCSubtargetInfo &STI) {
  BedrockInstPrinter Printer(MAI, MII, MRI);
  SmallString<256> Printed;
  raw_svector_ostream OS(Printed);
  Printer.printInst(&MI, /*Address*/ 0, /*Annot*/ "", STI, OS);
  return std::string(OS.str());
}

static bool encodeLine(StringRef Line, RepgEncodedInstruction &Encoded,
                       const bedrock_form_desc **OutForm = nullptr) {
  const bedrock_form_desc *Form = nullptr;
  Encoded.WordCount = 0;
  int Status = bedrock_assemble_line(Line.str().c_str(), Encoded.Words,
                                     BEDROCK_MAX_INSTRUCTION_WORDS,
                                     &Encoded.WordCount, &Form);
  if (OutForm)
    *OutForm = Form;
  return Status == BEDROCK_OK && Encoded.WordCount != 0 && Form != nullptr &&
         compactImmEA6Encoding(Encoded.Words, Encoded.WordCount, Form);
}

static std::optional<unsigned>
encodeMCInst(const MCInst &MI, const MCAsmInfo &MAI, const MCInstrInfo &MII,
             const MCRegisterInfo &MRI, const MCSubtargetInfo &STI,
             RepgEncodedInstruction &Encoded, bool ValidateRepgBody) {
  std::string Line = printMCInstForEncoding(MI, MAI, MII, MRI, STI);
  MCFixupKind FixupKind = getFixupKindForOpcode(MI.getOpcode());
  const MCExpr *FixupExpr = getExprOperand(MI);
  if (FixupExpr != nullptr) {
    if (FixupKind == FK_NONE || !replaceRelocExpression(Line, FixupKind))
      return std::nullopt;
  }

  const bedrock_form_desc *Form = nullptr;
  if (!encodeLine(Line, Encoded, &Form))
    return std::nullopt;
  Encoded.Text = std::move(Line);
  if (ValidateRepgBody && (Encoded.Words[0] & BEDROCK_WORD0_PREFIX_BIT) != 0 &&
      prefixWordHasRepeat(Encoded.Words[1]))
    return std::nullopt;
  if (ValidateRepgBody && repgFormIsForbidden(Form))
    return std::nullopt;
  return unsigned(Encoded.WordCount * sizeof(uint16_t));
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

class BedrockAsmPrinter : public AsmPrinter {
  SmallPtrSet<const MachineInstr *, 16> RepgSkipped;
  std::optional<uint64_t> EstimatedTextOffset = 0;

public:
  BedrockAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override { return "Bedrock Assembly Printer"; }

  void emitFunctionBodyStart() override {
    RepgSkipped.clear();
    if (EstimatedTextOffset && MF)
      *EstimatedTextOffset = alignTo(*EstimatedTextOffset, MF->getAlignment());
  }

  void emitGlobalVariable(const GlobalVariable *GV) override {
    if (!GV->hasInitializer()) {
      AsmPrinter::emitGlobalVariable(GV);
      return;
    }

    if (emitSpecialLLVMGlobal(GV))
      return;

    if (GV->isThreadLocal()) {
      AsmPrinter::emitGlobalVariable(GV);
      return;
    }

    MCSymbol *GVSym = getSymbol(GV);
    emitVisibility(GVSym, GV->getVisibility(), !GV->isDeclaration());

    GVSym->redefineIfPossible();
    if (GVSym->isDefined() || GVSym->isVariable())
      OutContext.reportError(SMLoc(), "symbol '" + Twine(GVSym->getName()) +
                                          "' is already defined");

    SectionKind GVKind = TargetLoweringObjectFile::getKindForGlobal(GV, TM);
    MCSection *TheSection =
        getObjFileLowering().SectionForGlobal(GV, GVKind, TM);
    const DataLayout &DL = GV->getDataLayout();
    Align Alignment = getGVAlignment(GV, DL);

    OutStreamer->switchSection(TheSection);
    emitLinkage(GV, GVSym);
    if (Alignment != Align(1))
      OutStreamer->emitValueToAlignment(Alignment);
    OutStreamer->emitLabel(GVSym);
    emitGlobalConstant(DL, GV->getInitializer());
    OutStreamer->addBlankLine();
  }

  void emitInstruction(const MachineInstr *MI) override {
    if (RepgSkipped.contains(MI))
      return;
    if (tryEmitRepgLoop(MI))
      return;

    MCInst TmpInst;
    BedrockMCInstLower Lowerer(OutContext, *this);
    Lowerer.lower(MI, TmpInst);
    EmitToStreamer(*OutStreamer, TmpInst);
    updateEstimatedOffset(TmpInst);
  }

  static char ID;

private:
  std::optional<unsigned> estimateInstBytes(const MCInst &Inst) const {
    RepgEncodedInstruction Encoded;
    return encodeMCInst(Inst, *MAI, *TM.getMCInstrInfo(),
                        *TM.getMCRegisterInfo(), getSubtargetInfo(), Encoded,
                        /*ValidateRepgBody*/ false);
  }

  void updateEstimatedOffset(const MCInst &Inst) {
    if (!EstimatedTextOffset)
      return;
    std::optional<unsigned> Bytes = estimateInstBytes(Inst);
    if (!Bytes) {
      EstimatedTextOffset = std::nullopt;
      return;
    }
    *EstimatedTextOffset += *Bytes;
  }

  bool findRepgCandidate(const MachineInstr &MI, RepgCandidate &Candidate) {
    if (!MF || !MF->getFunction().hasMinSize() || !MI.getParent())
      return false;

    const MachineBasicBlock &Body = *MI.getParent();
    auto First = firstNonDebug(Body);
    if (First == Body.end() || &*First != &MI)
      return false;

    auto LatchI = Body.getLastNonDebugInstr();
    if (LatchI == Body.end())
      return false;

    const TargetRegisterInfo &TRI = *MF->getSubtarget().getRegisterInfo();
    MachineBasicBlock::const_iterator BodyEnd = LatchI;
    MachineBasicBlock::const_iterator DecI = Body.end();
    Register CountReg = Bedrock::NoRegister;
    if (LatchI->getOpcode() == Bedrock::JCC) {
      if (LatchI->getNumOperands() < 2 || !LatchI->getOperand(0).isMBB() ||
          LatchI->getOperand(0).getMBB() != &Body)
        return false;
      std::optional<int64_t> LatchCC = getCondCodeImm(LatchI->getOperand(1));
      if (!LatchCC || *LatchCC != BedrockCC::NE)
        return false;

      DecI = prevNonDebug(LatchI, Body);
      if (DecI == Body.end() ||
          (DecI->getOpcode() != Bedrock::DEC32r &&
           DecI->getOpcode() != Bedrock::DEC64r) ||
          DecI->getNumOperands() < 2 || !DecI->getOperand(0).isReg() ||
          !DecI->getOperand(1).isReg())
        return false;

      CountReg = DecI->getOperand(0).getReg();
      if (!isDReg(CountReg) ||
          !regsOverlap(TRI, CountReg, DecI->getOperand(1).getReg()))
        return false;
      BodyEnd = DecI;
    } else if (isDJccOpcode(LatchI->getOpcode())) {
      if (LatchI->getNumOperands() < 4 || !LatchI->getOperand(0).isReg() ||
          !LatchI->getOperand(1).isReg() || !LatchI->getOperand(2).isMBB() ||
          LatchI->getOperand(2).getMBB() != &Body)
        return false;
      std::optional<int64_t> LatchCC = getCondCodeImm(LatchI->getOperand(3));
      if (!LatchCC || *LatchCC != BedrockCC::T)
        return false;
      CountReg = LatchI->getOperand(0).getReg();
      if (!isDReg(CountReg) ||
          !regsOverlap(TRI, CountReg, LatchI->getOperand(1).getReg()))
        return false;
    } else {
      return false;
    }

    MachineBasicBlock *Header = findSingleNonSelfPredecessor(Body);
    if (!Header ||
        (!isPositiveCountPretest(*Header, Body, CountReg, TRI) &&
         !isZeroExitCountPretest(*Header, Body, CountReg, TRI) &&
         !isPositiveConstCountSetup(*Header, Body, CountReg, TRI)))
      return false;

    if (flagsUsedBeforeDefAfter(Body, TRI))
      return false;

    SmallVector<const MachineInstr *, 8> BodyInstrs;
    for (auto I = First; I != BodyEnd; I = nextNonDebug(I, Body)) {
      if (!isRepgBodyEligible(*I, CountReg, TRI))
        return false;
      BodyInstrs.push_back(&*I);
      if (BodyInstrs.size() > 32)
        return false;
    }
    if (BodyInstrs.empty())
      return false;

    Candidate.Body = std::move(BodyInstrs);
    Candidate.Dec = DecI == Body.end() ? nullptr : &*DecI;
    Candidate.Latch = &*LatchI;
    Candidate.CountReg = CountReg;
    return true;
  }

  bool buildEncodedGroup(const RepgCandidate &Candidate,
                         SmallVectorImpl<RepgEncodedInstruction> &Encoded,
                         unsigned &ScalarBytes, unsigned &GroupBytes) {
    BedrockMCInstLower Lowerer(OutContext, *this);
    ScalarBytes = 0;
    GroupBytes = 0;
    Encoded.clear();

    auto EncodeMI = [&](const MachineInstr &MI, RepgEncodedInstruction &Out,
                        bool ValidateRepgBody) -> bool {
      MCInst Inst;
      Lowerer.lower(&MI, Inst);
      std::optional<unsigned> Bytes = encodeMCInst(
          Inst, *MAI, *TM.getMCInstrInfo(), *TM.getMCRegisterInfo(),
          getSubtargetInfo(), Out, ValidateRepgBody);
      if (!Bytes)
        return false;
      ScalarBytes += *Bytes;
      return true;
    };

    for (const MachineInstr *MI : Candidate.Body) {
      RepgEncodedInstruction Inst;
      if (!EncodeMI(*MI, Inst, /*ValidateRepgBody*/ true))
        return false;
      Encoded.push_back(std::move(Inst));
    }

    RepgEncodedInstruction Scratch;
    if (Candidate.Dec &&
        !EncodeMI(*Candidate.Dec, Scratch, /*ValidateRepgBody*/ false))
      return false;
    if (!EncodeMI(*Candidate.Latch, Scratch, /*ValidateRepgBody*/ false))
      return false;

    unsigned Counter = Candidate.CountReg - Bedrock::D0;
    if (!addPrefixToInstruction(Encoded.front(),
                                BedrockRepgStartPrefix |
                                    uint16_t(Counter & 0x07u)) ||
        !addPrefixToInstruction(Encoded.back(), BedrockRepgEndPrefix))
      return false;

    for (const RepgEncodedInstruction &Inst : Encoded)
      GroupBytes += Inst.WordCount * sizeof(uint16_t);
    return GroupBytes <= BedrockRepgIcacheLineBytes;
  }

  std::optional<uint64_t> getCurrentCodeOffset() {
    if (std::optional<uint64_t> Offset = currentSectionOffset(*OutStreamer))
      return Offset;
    return EstimatedTextOffset;
  }

  void emitRepgPadding(uint64_t PadBytes) {
    if (PadBytes == 0)
      return;
    assert((PadBytes % 2) == 0 && "Bedrock instruction padding is word sized");
    if (OutStreamer->hasRawTextSupport()) {
      for (uint64_t I = 0; I != PadBytes; I += 2)
        OutStreamer->emitRawText("\tNOP");
    } else {
      const bedrock_form_desc *Nop = bedrock_find_form_by_id("NOP");
      assert(Nop && "Bedrock NOP form must exist");
      uint16_t Words[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
      size_t WordCount = 0;
      [[maybe_unused]] int Status =
          bedrock_encode_form_words(Nop, nullptr, 0, Words,
                                    BEDROCK_MAX_INSTRUCTION_WORDS, &WordCount);
      assert(Status == BEDROCK_OK && WordCount == 1 &&
             "Bedrock NOP must encode to one word");
      setDeclaredWords(Words[0], 1);

      SmallString<64> Bytes;
      raw_svector_ostream OS(Bytes);
      for (uint64_t I = 0; I != PadBytes; I += 2)
        support::endian::write<uint16_t>(OS, Words[0],
                                         llvm::endianness::little);
      OutStreamer->emitBytes(Bytes);
      if (OutStreamer->isObj()) {
        auto &ObjectOut = static_cast<MCObjectStreamer &>(*OutStreamer);
        ObjectOut.getCurrentSectionOnly()->setHasInstructions(true);
        ObjectOut.getCurrentFragment()->setHasInstructions(getSubtargetInfo());
      }
    }
    if (EstimatedTextOffset)
      *EstimatedTextOffset += PadBytes;
  }

  void emitEncodedRepgGroup(ArrayRef<RepgEncodedInstruction> Encoded,
                            Register CountReg) {
    if (OutStreamer->hasRawTextSupport()) {
      unsigned Counter = CountReg - Bedrock::D0;
      SmallString<256> Text;
      raw_svector_ostream OS(Text);
      OS << "\tREPG D" << Counter << ", {\n";
      for (const RepgEncodedInstruction &Inst : Encoded)
        OS << "\t  " << Inst.Text << "\n";
      OS << "\t}";
      OutStreamer->emitRawText(OS.str());
    } else {
      SmallString<128> Bytes;
      raw_svector_ostream OS(Bytes);
      for (const RepgEncodedInstruction &Inst : Encoded)
        for (size_t I = 0; I != Inst.WordCount; ++I)
          support::endian::write<uint16_t>(OS, Inst.Words[I],
                                           llvm::endianness::little);
      OutStreamer->emitBytes(Bytes);
      if (OutStreamer->isObj()) {
        auto &ObjectOut = static_cast<MCObjectStreamer &>(*OutStreamer);
        ObjectOut.getCurrentSectionOnly()->setHasInstructions(true);
        ObjectOut.getCurrentFragment()->setHasInstructions(getSubtargetInfo());
      }
    }

    if (EstimatedTextOffset) {
      for (const RepgEncodedInstruction &Inst : Encoded)
        *EstimatedTextOffset += Inst.WordCount * sizeof(uint16_t);
    }
  }

  bool tryEmitRepgLoop(const MachineInstr *MI) {
    RepgCandidate Candidate;
    if (!findRepgCandidate(*MI, Candidate))
      return false;

    SmallVector<RepgEncodedInstruction, 8> Encoded;
    unsigned ScalarBytes = 0;
    unsigned GroupBytes = 0;
    if (!buildEncodedGroup(Candidate, Encoded, ScalarBytes, GroupBytes))
      return false;

    std::optional<uint64_t> Offset = getCurrentCodeOffset();
    if (!Offset)
      return false;
    uint64_t LineOffset = *Offset & (BedrockRepgIcacheLineBytes - 1);
    // Raw text output cannot query final fragment offsets, and alignment plus
    // fixup relaxation can make the text estimate drift. Keep near-boundary
    // groups conservative so the emitted .s reassembles.
    uint64_t RawTextMargin = 0;
    uint64_t PadBytes = LineOffset + GroupBytes + RawTextMargin <=
                                BedrockRepgIcacheLineBytes
                            ? 0
                            : BedrockRepgIcacheLineBytes - LineOffset;
    if (PadBytes + GroupBytes >= ScalarBytes)
      return false;

    emitRepgPadding(PadBytes);
    emitEncodedRepgGroup(Encoded, Candidate.CountReg);

    for (const MachineInstr *BodyMI : Candidate.Body)
      RepgSkipped.insert(BodyMI);
    if (Candidate.Dec)
      RepgSkipped.insert(Candidate.Dec);
    RepgSkipped.insert(Candidate.Latch);
    return true;
  }
};
} // end anonymous namespace

char BedrockAsmPrinter::ID = 0;

INITIALIZE_PASS(BedrockAsmPrinter, DEBUG_TYPE, "Bedrock Assembly Printer",
                false, false)

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeBedrockAsmPrinter() {
  RegisterAsmPrinter<BedrockAsmPrinter> X(getTheBedrockTarget());
}
