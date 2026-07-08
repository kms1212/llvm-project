//===-- BedrockInstPrinter.cpp - Convert Bedrock MCInst to asm ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockInstPrinter.h"

#include "BedrockCondCode.h"
#include "BedrockMCTargetDesc.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>

extern "C" {
#include "bedrock_asm_disasm.h"
}

using namespace llvm;

#define PRINT_ALIAS_INSTR
#include "BedrockGenAsmWriter.inc"

static unsigned declaredWords(uint16_t Word0) {
  return ((Word0 & BEDROCK_WORD0_LENGTH_MASK) >> 12) + 1;
}

static void setDeclaredWords(uint16_t &Word0, unsigned WordCount) {
  Word0 = (Word0 & ~uint16_t(BEDROCK_WORD0_LENGTH_MASK)) |
          uint16_t((WordCount - 1) << 12);
}

enum : uint64_t {
  BEDROCK_EA_DREG = 0x00,
  BEDROCK_EA_AREG = 0x08,
  BEDROCK_EA_INDIRECT = 0x10,
  BEDROCK_EA_A_DISP16 = 0x18,
  BEDROCK_EA_A_DISP32 = 0x20,
  BEDROCK_EA_PC_DISP16 = 0x28,
  BEDROCK_EA_PC_DISP32 = 0x29,
  BEDROCK_EA_PC_DISP64 = 0x2a,
  BEDROCK_EA_SP_DISP16 = 0x2c,
  BEDROCK_EA_SP_DISP32 = 0x2d,
  BEDROCK_EA_SP_DISP64 = 0x2e,
  BEDROCK_EA_SPREG = 0x2f,
  BEDROCK_EA_ABS32 = 0x30,
  BEDROCK_EA_ABS64 = 0x31,
  BEDROCK_EA_IMM16 = 0x32,
  BEDROCK_EA_IMM32 = 0x33,
  BEDROCK_EA_IMM64 = 0x34,
  BEDROCK_EA_S32_INDEXED_EXTENDED = 0x3e,
  BEDROCK_EA_EXTENDED = 0x3f,
};

enum : uint16_t {
  BEDROCK_PREFIX_NOSPEC = 0x01,
  BEDROCK_PREFIX_SATURATE = 0x02,
  BEDROCK_PREFIX_NONTEMPORAL = 0x03,
  BEDROCK_PREFIX_POSTINC = 0x04,
  BEDROCK_PREFIX_PREINC = 0x05,
  BEDROCK_PREFIX_POSTDEC = 0x06,
  BEDROCK_PREFIX_PREDEC = 0x07,
  BEDROCK_PREFIX_U2C = 0x08,
  BEDROCK_PREFIX_C2U = 0x09,
  BEDROCK_PREFIX_U2U = 0x0a,
};

static const bedrock_field_desc *
findFieldBySource(const bedrock_form_desc *Form, StringRef Source) {
  for (size_t I = 0; I != Form->field_count; ++I) {
    const bedrock_field_desc *Field = bedrock_form_field(Form, I);
    if (Field && Source == Field->source)
      return Field;
  }
  return nullptr;
}

static uint64_t extractField(const uint16_t *Words,
                             const bedrock_field_desc *Field) {
  unsigned Token = Field->token;
  if (Token != 0 && (Words[0] & BEDROCK_WORD0_PREFIX_BIT) != 0)
    ++Token;
  uint64_t Mask =
      Field->width >= 16 ? 0xffffull : ((1ull << Field->width) - 1ull);
  return (uint64_t(Words[Token]) >> Field->low_bit) & Mask;
}

static size_t payloadStartWord(const bedrock_form_desc *Form,
                               const uint16_t *Words) {
  size_t Start = (Form->kind == BEDROCK_FORM_EXTENDED ||
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

static uint64_t readPayload(const uint16_t *Words, size_t Start, size_t Count) {
  uint64_t Value = 0;
  for (size_t I = 0; I != Count; ++I)
    Value |= uint64_t(Words[Start + I]) << (I * 16);
  return Value;
}

static int64_t signExtendValue(uint64_t Value, unsigned Bits) {
  if (Bits >= 64)
    return static_cast<int64_t>(Value);
  uint64_t Sign = 1ULL << (Bits - 1);
  return static_cast<int64_t>((Value ^ Sign) - Sign);
}

static unsigned immediateEAPayloadBits(uint64_t Value) {
  if (Value == BEDROCK_EA_IMM16)
    return 16;
  if (Value == BEDROCK_EA_IMM32)
    return 32;
  if (Value == BEDROCK_EA_IMM64)
    return 64;
  return 0;
}

static bool sizeSuffixForForm(const bedrock_form_desc *Form,
                              const uint16_t *Words, char &Suffix) {
  const bedrock_field_desc *Field = findFieldBySource(Form, "size");
  if (!Field)
    return false;
  uint64_t Value = extractField(Words, Field);
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
  if (Kind == "LQ" && Value < 2) {
    Suffix = "LQ"[Value];
    return true;
  }
  if (Kind == "WL" && Value < 2) {
    Suffix = "WL"[Value];
    return true;
  }
  return false;
}

static const bedrock_form_desc *
decodeUnprefixedFormIfNeeded(const bedrock_form_desc *Form,
                             const uint16_t *Words, size_t WordCount) {
  if ((Words[0] & BEDROCK_WORD0_PREFIX_BIT) == 0 || WordCount <= 1)
    return Form;

  uint16_t UnprefixedWords[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
  size_t UnprefixedWordCount = WordCount - 1;
  UnprefixedWords[0] = (Words[0] & ~uint16_t(BEDROCK_WORD0_PREFIX_BIT |
                                             BEDROCK_WORD0_LENGTH_MASK)) |
                       uint16_t((UnprefixedWordCount - 1) << 12);
  for (size_t I = 1; I != UnprefixedWordCount; ++I)
    UnprefixedWords[I] = Words[I + 1];
  if (const bedrock_form_desc *UnprefixedForm =
          bedrock_decode_form(UnprefixedWords, UnprefixedWordCount))
    return UnprefixedForm;
  return Form;
}

static bool printEAValue(raw_ostream &OS, const uint16_t *Words,
                         size_t WordCount, uint64_t Value,
                         size_t &PayloadCursor) {
  auto PrintDisp = [&](StringRef Base, size_t WordsToRead,
                       unsigned Bits) -> bool {
    if (PayloadCursor + WordsToRead > WordCount)
      return false;
    int64_t Disp =
        signExtendValue(readPayload(Words, PayloadCursor, WordsToRead), Bits);
    PayloadCursor += WordsToRead;
    OS << '[' << Base;
    if (Disp != 0)
      OS << " + " << Disp;
    OS << ']';
    return true;
  };

  if (Value >= BEDROCK_EA_DREG && Value < BEDROCK_EA_AREG) {
    OS << 'D' << unsigned(Value - BEDROCK_EA_DREG);
    return true;
  }
  if (Value >= BEDROCK_EA_AREG && Value < BEDROCK_EA_INDIRECT) {
    OS << 'A' << unsigned(Value - BEDROCK_EA_AREG);
    return true;
  }
  if (Value >= BEDROCK_EA_INDIRECT && Value < BEDROCK_EA_A_DISP16) {
    OS << "[A" << unsigned(Value - BEDROCK_EA_INDIRECT) << ']';
    return true;
  }
  if (Value >= BEDROCK_EA_A_DISP16 && Value < BEDROCK_EA_A_DISP32) {
    SmallString<4> Base;
    raw_svector_ostream BaseOS(Base);
    BaseOS << 'A' << unsigned(Value - BEDROCK_EA_A_DISP16);
    return PrintDisp(BaseOS.str(), 1, 16);
  }
  if (Value >= BEDROCK_EA_A_DISP32 && Value < BEDROCK_EA_PC_DISP16) {
    SmallString<4> Base;
    raw_svector_ostream BaseOS(Base);
    BaseOS << 'A' << unsigned(Value - BEDROCK_EA_A_DISP32);
    return PrintDisp(BaseOS.str(), 2, 32);
  }
  if (Value == BEDROCK_EA_PC_DISP16)
    return PrintDisp("PC", 1, 16);
  if (Value == BEDROCK_EA_PC_DISP32)
    return PrintDisp("PC", 2, 32);
  if (Value == BEDROCK_EA_PC_DISP64)
    return PrintDisp("PC", 4, 64);
  if (Value == BEDROCK_EA_SP_DISP16)
    return PrintDisp("SP", 1, 16);
  if (Value == BEDROCK_EA_SP_DISP32)
    return PrintDisp("SP", 2, 32);
  if (Value == BEDROCK_EA_SP_DISP64)
    return PrintDisp("SP", 4, 64);
  if (Value == BEDROCK_EA_SPREG) {
    OS << "SP";
    return true;
  }
  if (Value == BEDROCK_EA_ABS32)
    return PrintDisp("", 2, 32);
  if (Value == BEDROCK_EA_ABS64)
    return PrintDisp("", 4, 64);
  auto PrintImm = [&](size_t WordsToRead, unsigned Bits) -> bool {
    if (PayloadCursor + WordsToRead > WordCount)
      return false;
    OS << signExtendValue(readPayload(Words, PayloadCursor, WordsToRead), Bits);
    PayloadCursor += WordsToRead;
    return true;
  };
  if (Value == BEDROCK_EA_IMM16)
    return PrintImm(1, 16);
  if (Value == BEDROCK_EA_IMM32)
    return PrintImm(2, 32);
  if (Value == BEDROCK_EA_IMM64)
    return PrintImm(4, 64);
  return false;
}

static const char *segmentName(unsigned Segment) {
  switch (Segment) {
  case 0:
    return "CS";
  case 1:
    return "DS";
  case 2:
    return "SS";
  case 3:
    return "GS0";
  case 4:
    return "GS1";
  case 5:
    return "GS2";
  case 6:
    return "GS3";
  case 7:
    return "GS4";
  default:
    return nullptr;
  }
}

static const char *simplePrefixName(uint16_t Prefix) {
  switch (Prefix) {
  case BEDROCK_PREFIX_NOSPEC:
    return "NOSPEC";
  case BEDROCK_PREFIX_SATURATE:
    return "SATURATE";
  case BEDROCK_PREFIX_NONTEMPORAL:
    return "NONTEMPORAL";
  default:
    return nullptr;
  }
}

static bool isUpdatePrefix(uint16_t Prefix) {
  return Prefix >= BEDROCK_PREFIX_POSTINC && Prefix <= BEDROCK_PREFIX_PREDEC;
}

static uint16_t prefixWord(const uint16_t *Words, size_t WordCount) {
  if (WordCount < 2 || (Words[0] & BEDROCK_WORD0_PREFIX_BIT) == 0)
    return 0;
  return Words[1];
}

static uint16_t updatePrefixFromWord(uint16_t PrefixWord) {
  uint16_t Result = 0;
  for (uint16_t Prefix :
       {uint16_t(PrefixWord & 0x00ffu), uint16_t(PrefixWord >> 8)}) {
    if (isUpdatePrefix(Prefix))
      Result = Prefix;
  }
  return Result;
}

static uint16_t accessPrefixFromWord(uint16_t PrefixWord) {
  uint16_t Result = 0;
  for (uint16_t Prefix :
       {uint16_t(PrefixWord & 0x00ffu), uint16_t(PrefixWord >> 8)}) {
    if (Prefix == BEDROCK_PREFIX_U2C || Prefix == BEDROCK_PREFIX_C2U ||
        Prefix == BEDROCK_PREFIX_U2U)
      Result = Prefix;
  }
  return Result;
}

static bool prefixNeedsSpecPrint(uint16_t Prefix) {
  return simplePrefixName(Prefix) != nullptr ||
         Prefix == BEDROCK_PREFIX_U2C || Prefix == BEDROCK_PREFIX_C2U ||
         Prefix == BEDROCK_PREFIX_U2U;
}

static unsigned extendedEAPayloadWords(uint16_t Descriptor) {
  unsigned Mode = (Descriptor >> 11) & 0x1f;
  switch (Mode) {
  case 0x1:
  case 0x5:
  case 0x9:
  case 0xc:
    return 2;
  case 0x2:
  case 0x6:
  case 0x7:
  case 0xa:
  case 0xd:
    return 3;
  case 0x3:
  case 0x8:
  case 0xb:
  case 0xe:
    return 5;
  default:
    return 1;
  }
}

static bool extendedEANeedsSpecPrint(uint16_t Descriptor) {
  unsigned Mode = (Descriptor >> 11) & 0x1f;
  unsigned Segment = (Descriptor >> 8) & 0x7;
  if (Mode <= 0x3)
    return Segment != 1;
  return Mode >= 0x4 && Mode <= 0x8;
}

static unsigned rawEAPayloadWords(uint64_t Value, const uint16_t *Words,
                                  size_t PayloadCursor, size_t WordCount) {
  if (Value >= BEDROCK_EA_A_DISP16 && Value < BEDROCK_EA_A_DISP32)
    return 1;
  if (Value >= BEDROCK_EA_A_DISP32 && Value < BEDROCK_EA_PC_DISP16)
    return 2;
  if (Value == BEDROCK_EA_PC_DISP16 || Value == BEDROCK_EA_SP_DISP16)
    return 1;
  if (Value == BEDROCK_EA_PC_DISP32 || Value == BEDROCK_EA_SP_DISP32 ||
      Value == BEDROCK_EA_ABS32 || Value == BEDROCK_EA_IMM32)
    return 2;
  if (Value == BEDROCK_EA_PC_DISP64 || Value == BEDROCK_EA_SP_DISP64 ||
      Value == BEDROCK_EA_ABS64 || Value == BEDROCK_EA_IMM64)
    return 4;
  if (Value == BEDROCK_EA_IMM16)
    return 1;
  if (Value == BEDROCK_EA_EXTENDED ||
      Value == BEDROCK_EA_S32_INDEXED_EXTENDED) {
    if (PayloadCursor >= WordCount)
      return 0;
    return extendedEAPayloadWords(Words[PayloadCursor]);
  }
  return 0;
}

static bool rawSpecEncodedInstNeeded(const uint16_t *Words, size_t WordCount,
                                     const bedrock_form_desc *Form) {
  uint16_t Prefix = prefixWord(Words, WordCount);
  for (uint16_t Byte : {uint16_t(Prefix & 0x00ffu), uint16_t(Prefix >> 8)}) {
    if (prefixNeedsSpecPrint(Byte))
      return true;
  }

  size_t PayloadCursor = payloadStartWord(Form, Words);
  for (size_t I = 0; I != Form->operand_count; ++I) {
    const bedrock_operand_desc *Operand = bedrock_form_operand(Form, I);
    if (!Operand || Operand->field_index == BEDROCK_NO_FIELD)
      continue;
    const bedrock_field_desc *Field = &bedrock_fields[Operand->field_index];
    StringRef Kind(Operand->kind ? Operand->kind : "");
    StringRef DeclaredKind(Operand->declared_kind ? Operand->declared_kind
                                                  : "");
    uint64_t Value = extractField(Words, Field);
    if (Kind.equals_insensitive("EA") || DeclaredKind.equals_insensitive("EA")) {
      unsigned PayloadWords =
          rawEAPayloadWords(Value, Words, PayloadCursor, WordCount);
      if (PayloadCursor + PayloadWords > WordCount)
        return false;
      if ((Value == BEDROCK_EA_EXTENDED ||
           Value == BEDROCK_EA_S32_INDEXED_EXTENDED) &&
          PayloadWords != 0 && extendedEANeedsSpecPrint(Words[PayloadCursor]))
        return true;
      PayloadCursor += PayloadWords;
      continue;
    }

    unsigned ImmBits = immediateEAPayloadBits(Value);
    if (ImmBits != 0 &&
        (StringRef(Field->kind).starts_with("IMM") ||
         Kind.starts_with_insensitive("IMM") ||
         DeclaredKind.starts_with_insensitive("IMM"))) {
      unsigned PayloadWords = (ImmBits + 15) / 16;
      if (PayloadCursor + PayloadWords > WordCount)
        return false;
      PayloadCursor += PayloadWords;
    }
  }
  return false;
}

static void printSignedDisplacement(raw_ostream &OS, int64_t Disp) {
  if (Disp < 0)
    OS << " - " << uint64_t(-Disp);
  else if (Disp > 0)
    OS << " + " << uint64_t(Disp);
}

static void printAccessDomain(raw_ostream &OS, char Domain) {
  if (Domain == 'U' || Domain == 'C')
    OS << Domain << ':';
}

static void printARegMemory(raw_ostream &OS, unsigned Reg,
                            uint16_t Update) {
  switch (Update) {
  case BEDROCK_PREFIX_PREINC:
    OS << "[++A" << Reg << ']';
    break;
  case BEDROCK_PREFIX_PREDEC:
    OS << "[--A" << Reg << ']';
    break;
  case BEDROCK_PREFIX_POSTINC:
    OS << "[A" << Reg << "++]";
    break;
  case BEDROCK_PREFIX_POSTDEC:
    OS << "[A" << Reg << "--]";
    break;
  default:
    OS << "[A" << Reg << ']';
    break;
  }
}

static void printSegmentARegMemory(raw_ostream &OS, unsigned Segment,
                                   unsigned Reg, uint16_t Update) {
  const char *SegmentText = segmentName(Segment);
  if (!SegmentText) {
    OS << "<bad-segment>";
    return;
  }

  switch (Update) {
  case BEDROCK_PREFIX_PREINC:
    OS << '[' << SegmentText << ":++A" << Reg << ']';
    break;
  case BEDROCK_PREFIX_PREDEC:
    OS << '[' << SegmentText << ":--A" << Reg << ']';
    break;
  case BEDROCK_PREFIX_POSTINC:
    OS << '[' << SegmentText << ":A" << Reg << "++]";
    break;
  case BEDROCK_PREFIX_POSTDEC:
    OS << '[' << SegmentText << ":A" << Reg << "--]";
    break;
  default:
    OS << '[' << SegmentText << ":A" << Reg << ']';
    break;
  }
}

static bool formStoresToEAWithoutReadingDest(const bedrock_form_desc *Form) {
  StringRef ID(Form->id ? Form->id : "");
  StringRef Mnemonic(Form->mnemonic ? Form->mnemonic : "");
  if (!ID.ends_with("_TO_EA"))
    return false;
  return Mnemonic == "MOV" || Mnemonic.starts_with("EXT") ||
         Mnemonic.starts_with("F");
}

static const bedrock_field_desc *
findFieldBySymbolOccurrence(const bedrock_form_desc *Form, StringRef Symbol,
                            unsigned Occurrence) {
  for (size_t I = 0; I != Form->field_count; ++I) {
    const bedrock_field_desc *Field = bedrock_form_field(Form, I);
    if (!Field || Symbol != Field->symbol)
      continue;
    if (Occurrence == 0)
      return Field;
    --Occurrence;
  }
  return nullptr;
}

static const bedrock_operand_desc *
operandForField(const bedrock_form_desc *Form,
                const bedrock_field_desc *Field) {
  if (!Field)
    return nullptr;
  ptrdiff_t FieldIndex = Field - bedrock_fields;
  if (FieldIndex < 0 || size_t(FieldIndex) >= bedrock_fields_count)
    return nullptr;
  for (size_t I = 0; I != Form->operand_count; ++I) {
    const bedrock_operand_desc *Operand = bedrock_form_operand(Form, I);
    if (Operand && Operand->field_index == uint16_t(FieldIndex))
      return Operand;
  }
  return nullptr;
}

static char accessDomainForOperand(const bedrock_form_desc *Form,
                                   const bedrock_operand_desc *Operand,
                                   uint16_t AccessPrefix) {
  if (!Operand || AccessPrefix == 0)
    return 0;

  StringRef Kind(Operand->kind ? Operand->kind : "");
  StringRef DeclaredKind(Operand->declared_kind ? Operand->declared_kind : "");
  if (!Kind.equals_insensitive("EA") && !DeclaredKind.equals_insensitive("EA"))
    return 0;

  if (AccessPrefix == BEDROCK_PREFIX_U2U)
    return 'U';

  StringRef Role(Operand->role ? Operand->role : "");
  bool IsDst = Role.equals_insensitive("dst");
  if (AccessPrefix == BEDROCK_PREFIX_U2C)
    return IsDst && formStoresToEAWithoutReadingDest(Form) ? 'C' : 'U';
  if (AccessPrefix == BEDROCK_PREFIX_C2U)
    return IsDst ? 'U' : 'C';
  return 0;
}

static bool printRawEAValue(raw_ostream &OS, const uint16_t *Words,
                            size_t WordCount, uint64_t Value,
                            size_t &PayloadCursor, uint16_t UpdatePrefix,
                            char AccessDomain) {
  auto PrintDisp = [&](StringRef Base, size_t WordsToRead,
                       unsigned Bits) -> bool {
    if (PayloadCursor + WordsToRead > WordCount)
      return false;
    int64_t Disp =
        signExtendValue(readPayload(Words, PayloadCursor, WordsToRead), Bits);
    PayloadCursor += WordsToRead;
    printAccessDomain(OS, AccessDomain);
    OS << '[' << Base;
    printSignedDisplacement(OS, Disp);
    OS << ']';
    return true;
  };

  if (Value >= BEDROCK_EA_DREG && Value < BEDROCK_EA_AREG) {
    OS << 'D' << unsigned(Value - BEDROCK_EA_DREG);
    return true;
  }
  if (Value >= BEDROCK_EA_AREG && Value < BEDROCK_EA_INDIRECT) {
    OS << 'A' << unsigned(Value - BEDROCK_EA_AREG);
    return true;
  }
  if (Value >= BEDROCK_EA_INDIRECT && Value < BEDROCK_EA_A_DISP16) {
    printAccessDomain(OS, AccessDomain);
    printARegMemory(OS, unsigned(Value - BEDROCK_EA_INDIRECT), UpdatePrefix);
    return true;
  }
  if (Value >= BEDROCK_EA_A_DISP16 && Value < BEDROCK_EA_A_DISP32) {
    SmallString<4> Base;
    raw_svector_ostream BaseOS(Base);
    BaseOS << 'A' << unsigned(Value - BEDROCK_EA_A_DISP16);
    return PrintDisp(BaseOS.str(), 1, 16);
  }
  if (Value >= BEDROCK_EA_A_DISP32 && Value < BEDROCK_EA_PC_DISP16) {
    SmallString<4> Base;
    raw_svector_ostream BaseOS(Base);
    BaseOS << 'A' << unsigned(Value - BEDROCK_EA_A_DISP32);
    return PrintDisp(BaseOS.str(), 2, 32);
  }
  if (Value == BEDROCK_EA_PC_DISP16)
    return PrintDisp("PC", 1, 16);
  if (Value == BEDROCK_EA_PC_DISP32)
    return PrintDisp("PC", 2, 32);
  if (Value == BEDROCK_EA_PC_DISP64)
    return PrintDisp("PC", 4, 64);
  if (Value == BEDROCK_EA_SP_DISP16)
    return PrintDisp("SP", 1, 16);
  if (Value == BEDROCK_EA_SP_DISP32)
    return PrintDisp("SP", 2, 32);
  if (Value == BEDROCK_EA_SP_DISP64)
    return PrintDisp("SP", 4, 64);
  if (Value == BEDROCK_EA_SPREG) {
    OS << "SP";
    return true;
  }
  if (Value == BEDROCK_EA_ABS32 || Value == BEDROCK_EA_ABS64) {
    size_t WordsToRead = Value == BEDROCK_EA_ABS32 ? 2 : 4;
    unsigned Bits = Value == BEDROCK_EA_ABS32 ? 32 : 64;
    if (PayloadCursor + WordsToRead > WordCount)
      return false;
    printAccessDomain(OS, AccessDomain);
    OS << '['
       << signExtendValue(readPayload(Words, PayloadCursor, WordsToRead),
                          Bits)
       << ']';
    PayloadCursor += WordsToRead;
    return true;
  }

  auto PrintImm = [&](size_t WordsToRead, unsigned Bits) -> bool {
    if (PayloadCursor + WordsToRead > WordCount)
      return false;
    OS << signExtendValue(readPayload(Words, PayloadCursor, WordsToRead), Bits);
    PayloadCursor += WordsToRead;
    return true;
  };
  if (Value == BEDROCK_EA_IMM16)
    return PrintImm(1, 16);
  if (Value == BEDROCK_EA_IMM32)
    return PrintImm(2, 32);
  if (Value == BEDROCK_EA_IMM64)
    return PrintImm(4, 64);

  if (Value != BEDROCK_EA_EXTENDED &&
      Value != BEDROCK_EA_S32_INDEXED_EXTENDED)
    return false;
  if (PayloadCursor >= WordCount)
    return false;

  uint16_t Desc = Words[PayloadCursor++];
  unsigned Mode = (Desc >> 11) & 0x1f;
  unsigned Segment = (Desc >> 8) & 0x7;
  unsigned BaseReg = (Desc >> 5) & 0x7;
  unsigned IndexReg = (Desc >> 2) & 0x7;
  unsigned Scale = 1u << (Desc & 0x3);
  bool Signed32Index = Value == BEDROCK_EA_S32_INDEXED_EXTENDED;
  size_t DispWords = 0;
  unsigned DispBits = 0;
  bool HasIndex = false;
  bool IsAbsolute = false;
  bool IsSegmentA = false;
  bool UseSegmentPrefix = false;
  StringRef FixedBase;

  switch (Mode) {
  default:
    return false;
  case 0x0:
    HasIndex = true;
    UseSegmentPrefix = Segment != 1;
    break;
  case 0x1:
    HasIndex = true;
    UseSegmentPrefix = Segment != 1;
    DispWords = 1;
    DispBits = 16;
    break;
  case 0x2:
    HasIndex = true;
    UseSegmentPrefix = Segment != 1;
    DispWords = 2;
    DispBits = 32;
    break;
  case 0x3:
    HasIndex = true;
    UseSegmentPrefix = Segment != 1;
    DispWords = 4;
    DispBits = 64;
    break;
  case 0x4:
    IsSegmentA = true;
    UseSegmentPrefix = true;
    break;
  case 0x5:
    IsSegmentA = true;
    UseSegmentPrefix = true;
    DispWords = 1;
    DispBits = 16;
    break;
  case 0x6:
    IsSegmentA = true;
    UseSegmentPrefix = true;
    DispWords = 2;
    DispBits = 32;
    break;
  case 0x7:
    IsAbsolute = true;
    UseSegmentPrefix = true;
    DispWords = 2;
    DispBits = 32;
    break;
  case 0x8:
    IsAbsolute = true;
    UseSegmentPrefix = true;
    DispWords = 4;
    DispBits = 64;
    break;
  case 0x9:
  case 0xa:
  case 0xb:
    HasIndex = true;
    FixedBase = "SP";
    DispWords = Mode == 0x9 ? 1 : Mode == 0xa ? 2 : 4;
    DispBits = Mode == 0x9 ? 16 : Mode == 0xa ? 32 : 64;
    break;
  case 0xc:
  case 0xd:
  case 0xe:
    HasIndex = true;
    FixedBase = "PC";
    DispWords = Mode == 0xc ? 1 : Mode == 0xd ? 2 : 4;
    DispBits = Mode == 0xc ? 16 : Mode == 0xd ? 32 : 64;
    break;
  }

  if (PayloadCursor + DispWords > WordCount)
    return false;
  uint64_t RawDisp = DispWords == 0
                         ? 0
                         : readPayload(Words, PayloadCursor, DispWords);
  int64_t Disp = DispWords == 0 ? 0 : signExtendValue(RawDisp, DispBits);
  PayloadCursor += DispWords;

  printAccessDomain(OS, AccessDomain);
  const char *SegmentText = segmentName(Segment);
  if (UseSegmentPrefix && !SegmentText)
    return false;

  if (IsAbsolute) {
    OS << '[' << SegmentText << ':';
    if (Mode == 0x8)
      OS << RawDisp;
    else
      OS << Disp;
    OS << ']';
    return true;
  }

  if (IsSegmentA && DispWords == 0) {
    printSegmentARegMemory(OS, Segment, BaseReg, UpdatePrefix);
    return true;
  }

  OS << '[';
  if (UseSegmentPrefix)
    OS << SegmentText << ':';
  if (!FixedBase.empty())
    OS << FixedBase;
  else
    OS << 'A' << BaseReg;

  if (HasIndex) {
    OS << " + D" << IndexReg;
    if (Signed32Index)
      OS << ".L";
    OS << " * " << Scale;
  }
  printSignedDisplacement(OS, Disp);
  OS << ']';
  return true;
}

static bool printIntegerPayload(raw_ostream &OS, const uint16_t *Words,
                                size_t WordCount, size_t &PayloadCursor,
                                size_t PayloadWords, unsigned Bits) {
  if (PayloadCursor + PayloadWords > WordCount)
    return false;
  OS << signExtendValue(readPayload(Words, PayloadCursor, PayloadWords), Bits);
  PayloadCursor += PayloadWords;
  return true;
}

static bool printRawSpecEncodedInst(const uint16_t *Words, size_t WordCount,
                                    raw_ostream &OS) {
  const bedrock_form_desc *Form = bedrock_decode_form(Words, WordCount);
  if (!Form)
    return false;
  Form = decodeUnprefixedFormIfNeeded(Form, Words, WordCount);
  if (!rawSpecEncodedInstNeeded(Words, WordCount, Form))
    return false;

  uint16_t PrefixWord = prefixWord(Words, WordCount);
  for (uint16_t Prefix :
       {uint16_t(PrefixWord & 0x00ffu), uint16_t(PrefixWord >> 8)}) {
    if (const char *Name = simplePrefixName(Prefix))
      OS << Name << ' ';
  }

  uint16_t UpdatePrefix = updatePrefixFromWord(PrefixWord);
  uint16_t AccessPrefix = accessPrefixFromWord(PrefixWord);
  size_t PayloadCursor = payloadStartWord(Form, Words);
  unsigned SymbolOccurrence[256] = {};
  StringRef Syntax(Form->syntax ? Form->syntax : "");

  for (size_t I = 0; I != Syntax.size();) {
    if (Syntax.substr(I).starts_with(".X(")) {
      char Suffix = 0;
      if (!sizeSuffixForForm(Form, Words, Suffix))
        return false;
      OS << '.' << Suffix;
      size_t Close = Syntax.find(')', I + 3);
      if (Close == StringRef::npos)
        return false;
      I = Close + 1;
      continue;
    }

    if (Syntax.substr(I).starts_with("{condition}")) {
      const bedrock_field_desc *Field = findFieldBySource(Form, "cc");
      uint16_t CC = 0;
      if (Field)
        CC = uint16_t(extractField(Words, Field));
      else if (Form->alias_condition && Form->alias_condition[0] != '\0') {
        uint16_t Named = 0;
        for (size_t N = 0; N != bedrock_condition_names_count; ++N) {
          if (StringRef(bedrock_condition_names[N].name) ==
              Form->alias_condition) {
            Named = bedrock_condition_names[N].value;
            break;
          }
        }
        CC = Named;
      }
      const char *Name = nullptr;
      for (size_t N = 0; N != bedrock_condition_names_count; ++N) {
        if (bedrock_condition_names[N].value == CC) {
          Name = bedrock_condition_names[N].name;
          break;
        }
      }
      if (!Name)
        return false;
      OS << Name;
      I += strlen("{condition}");
      continue;
    }

    bool IsToken = Syntax.substr(I).starts_with("Dn(") ||
                   Syntax.substr(I).starts_with("An(") ||
                   Syntax.substr(I).starts_with("Fn(") ||
                   Syntax.substr(I).starts_with("Sreg(") ||
                   Syntax.substr(I).starts_with("ORDER(") ||
                   Syntax.substr(I).starts_with("order(") ||
                   Syntax.substr(I).starts_with("<ea(") ||
                   Syntax.substr(I).starts_with("<imm(") ||
                   Syntax.substr(I).starts_with("<cr(") ||
                   Syntax.substr(I).starts_with("<count(") ||
                   Syntax.substr(I).starts_with("<bit_index(");
    if (!IsToken) {
      if (Syntax.substr(I).starts_with("imm64")) {
        if (!printIntegerPayload(OS, Words, WordCount, PayloadCursor, 4, 64))
          return false;
        I += 5;
        continue;
      }
      if (Syntax.substr(I).starts_with("imm32")) {
        if (!printIntegerPayload(OS, Words, WordCount, PayloadCursor, 2, 32))
          return false;
        I += 5;
        continue;
      }
      if (Syntax.substr(I).starts_with("imm16")) {
        if (!printIntegerPayload(OS, Words, WordCount, PayloadCursor, 1, 16))
          return false;
        I += 5;
        continue;
      }
      OS << Syntax[I++];
      continue;
    }

    bool AngleToken = Syntax[I] == '<';
    size_t Open = Syntax.find('(', I);
    size_t Close = Open == StringRef::npos ? StringRef::npos
                                           : Syntax.find(')', Open + 1);
    if (Open == StringRef::npos || Close == StringRef::npos)
      return false;

    StringRef Symbol = Syntax.slice(Open + 1, Close);
    unsigned char Key = Symbol.empty() ? 0 : (unsigned char)Symbol.front();
    const bedrock_field_desc *Field = findFieldBySymbolOccurrence(
        Form, Symbol, SymbolOccurrence[Key]++);
    if (!Field)
      return false;

    uint64_t Value = extractField(Words, Field);
    StringRef Token = Syntax.slice(I, Open);
    if (Token == "Dn") {
      OS << 'D' << unsigned(Value);
    } else if (Token == "An") {
      OS << 'A' << unsigned(Value);
    } else if (Token == "Fn") {
      OS << 'F' << unsigned(Value);
    } else if (Token == "Sreg") {
      const char *Name = segmentName(unsigned(Value));
      if (!Name)
        return false;
      OS << Name;
    } else if (Token == "ORDER" || Token == "order") {
      const char *Name = nullptr;
      for (size_t N = 0; N != bedrock_memory_order_names_count; ++N) {
        if (bedrock_memory_order_names[N].value == Value) {
          Name = bedrock_memory_order_names[N].name;
          break;
        }
      }
      if (!Name)
        return false;
      OS << Name;
    } else if (Token == "<ea") {
      const bedrock_operand_desc *Operand = operandForField(Form, Field);
      char Domain = accessDomainForOperand(Form, Operand, AccessPrefix);
      if (!printRawEAValue(OS, Words, WordCount, Value, PayloadCursor,
                           UpdatePrefix, Domain))
        return false;
    } else if (Token == "<imm") {
      unsigned Bits = immediateEAPayloadBits(Value);
      if (Bits != 0) {
        if (!printIntegerPayload(OS, Words, WordCount, PayloadCursor,
                                 (Bits + 15) / 16, Bits))
          return false;
      } else {
        OS << Value;
      }
    } else if (Token == "<cr" || Token == "<count" ||
               Token == "<bit_index") {
      OS << Value;
    } else {
      return false;
    }

    I = Close + 1;
    if (AngleToken && I < Syntax.size() && Syntax[I] == '>')
      ++I;
  }

  return true;
}

static const char *condCodeName(uint64_t CC) {
  switch (CC) {
  default:
    return nullptr;
  case BedrockCC::T:
    return "T";
  case BedrockCC::F:
    return "F";
  case BedrockCC::EQ:
    return "EQ";
  case BedrockCC::NE:
    return "NE";
  case BedrockCC::ULT:
    return "ULT";
  case BedrockCC::UGE:
    return "UGE";
  case BedrockCC::MI:
    return "MI";
  case BedrockCC::PL:
    return "PL";
  case BedrockCC::VS:
    return "VS";
  case BedrockCC::VC:
    return "VC";
  case BedrockCC::ULE:
    return "ULE";
  case BedrockCC::UGT:
    return "UGT";
  case BedrockCC::LT:
    return "LT";
  case BedrockCC::GE:
    return "GE";
  case BedrockCC::LE:
    return "LE";
  case BedrockCC::GT:
    return "GT";
  }
}

static bool printDRegField(raw_ostream &OS, const bedrock_form_desc *Form,
                           const uint16_t *Words, StringRef Source) {
  const bedrock_field_desc *Field = findFieldBySource(Form, Source);
  if (!Field || StringRef(Field->kind) != "DREG")
    return false;
  uint64_t RegNo = extractField(Words, Field);
  if (RegNo >= 8)
    return false;
  OS << 'D' << unsigned(RegNo);
  return true;
}

static bool printCountBranchEncoded(const uint16_t *Words, size_t WordCount,
                                    raw_ostream &OS) {
  const bedrock_form_desc *Form = bedrock_decode_form(Words, WordCount);
  if (!Form)
    return false;

  StringRef Mnemonic(Form->mnemonic);
  if (Mnemonic != "DJcc" && Mnemonic != "IJcc")
    return false;

  char Suffix = 0;
  if (!sizeSuffixForForm(Form, Words, Suffix))
    return false;

  const bedrock_field_desc *CCField = findFieldBySource(Form, "cc");
  const bedrock_field_desc *TargetField = findFieldBySource(Form, "target");
  if (!CCField || !TargetField)
    return false;

  const char *CCName = condCodeName(extractField(Words, CCField));
  if (!CCName)
    return false;

  size_t PayloadCursor = payloadStartWord(Form, Words);
  OS << (Mnemonic == "DJcc" ? "DJ" : "IJ") << CCName << '.' << Suffix << '\t';
  if (Mnemonic == "DJcc") {
    if (!printDRegField(OS, Form, Words, "counter"))
      return false;
    OS << ", ";
  } else {
    if (!printDRegField(OS, Form, Words, "index"))
      return false;
    OS << ", ";
    if (!printDRegField(OS, Form, Words, "bound"))
      return false;
    OS << ", ";
  }
  return printEAValue(OS, Words, WordCount, extractField(Words, TargetField),
                      PayloadCursor);
}

static bool printImmToEAEncoded(const uint16_t *Words, size_t WordCount,
                                raw_ostream &OS) {
  const bedrock_form_desc *Form = bedrock_decode_form(Words, WordCount);
  if (!Form || !StringRef(Form->id).contains("IMM_TO"))
    return false;

  const bedrock_field_desc *ImmField = findFieldBySource(Form, "imm");
  if (!ImmField || !StringRef(ImmField->kind).starts_with("IMM") ||
      ImmField->width != 6)
    return false;

  StringRef TargetSource = (StringRef(Form->mnemonic) == "CMP" ||
                            StringRef(Form->mnemonic) == "TEST")
                               ? "rhs"
                               : "dst";
  const bedrock_field_desc *TargetField = findFieldBySource(Form, TargetSource);
  if (!TargetField)
    return false;

  char Suffix = 0;
  if (!sizeSuffixForForm(Form, Words, Suffix))
    return false;

  size_t PayloadCursor = payloadStartWord(Form, Words);
  uint64_t EncodedImm = extractField(Words, ImmField);
  unsigned ImmPayloadBits = immediateEAPayloadBits(EncodedImm);
  size_t ImmPayloadWords = (ImmPayloadBits + 15) / 16;
  int64_t Imm = static_cast<int64_t>(EncodedImm);
  if (ImmPayloadBits != 0 && PayloadCursor + ImmPayloadWords <= WordCount) {
    Imm = signExtendValue(readPayload(Words, PayloadCursor, ImmPayloadWords),
                          ImmPayloadBits);
    PayloadCursor += ImmPayloadWords;
  }

  OS << Form->mnemonic << '.' << Suffix << '\t' << Imm << ", ";
  return printEAValue(OS, Words, WordCount, extractField(Words, TargetField),
                      PayloadCursor);
}

static bool disassembleWithDeclaredWords(const uint16_t *Words,
                                         size_t WordCount,
                                         unsigned DeclaredWordCount, char *Text,
                                         size_t TextSize) {
  if (DeclaredWordCount == 0 ||
      DeclaredWordCount > BEDROCK_MAX_INSTRUCTION_WORDS)
    return false;

  uint16_t PaddedWords[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
  for (size_t I = 0; I != WordCount && I != DeclaredWordCount; ++I)
    PaddedWords[I] = Words[I];
  setDeclaredWords(PaddedWords[0], DeclaredWordCount);
  return bedrock_disassemble_line(PaddedWords, DeclaredWordCount, Text,
                                  TextSize, nullptr) == BEDROCK_OK;
}

static unsigned canonicalDeclaredWords(StringRef Text) {
  uint16_t Words[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
  size_t WordCount = 0;
  const bedrock_form_desc *Form = nullptr;
  if (bedrock_assemble_line(Text.str().c_str(), Words,
                            BEDROCK_MAX_INSTRUCTION_WORDS, &WordCount,
                            &Form) != BEDROCK_OK ||
      WordCount == 0)
    return 0;
  return declaredWords(Words[0]);
}

static bool shouldPrintExplicitZeroOffset(MCRegister BaseReg) {
  return BaseReg == Bedrock::SP || BaseReg == Bedrock::PC;
}

void BedrockInstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) {
  OS << getRegisterName(Reg);
}

void BedrockInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                   StringRef Annot, const MCSubtargetInfo &STI,
                                   raw_ostream &OS) {
  unsigned Flags = MI->getFlags();
  if ((Flags & Bedrock::RepgStart) != 0) {
    unsigned Counter =
        (Flags & Bedrock::RepgCounterMask) >> Bedrock::RepgCounterShift;
    OS << "REPG ";
    printRegName(OS, MCRegister(Bedrock::D0 + Counter));
    OS << ", ";
  }
  if ((Flags & Bedrock::RepgEnd) != 0)
    OS << "ENDG ";

  if (MI->getOpcode() == Bedrock::ENCODED) {
    printEncodedInst(MI, OS);
    printAnnotation(OS, Annot);
    return;
  }

  unsigned DeclaredLen =
      (Flags & Bedrock::DeclaredLenMask) >> Bedrock::DeclaredLenShift;
  if (DeclaredLen != 0)
    OS << "LEN " << DeclaredLen << ", ";

  if (!printAliasInstr(MI, Address, OS))
    printInstruction(MI, Address, OS);
  printAnnotation(OS, Annot);
}

void BedrockInstPrinter::printEncodedInst(const MCInst *MI, raw_ostream &OS) {
  uint16_t Words[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
  if (MI->getNumOperands() < BEDROCK_MAX_INSTRUCTION_WORDS + 1 ||
      !MI->getOperand(0).isImm()) {
    OS << "<invalid>";
    return;
  }

  uint64_t WordCount = MI->getOperand(0).getImm();
  if (WordCount == 0 || WordCount > BEDROCK_MAX_INSTRUCTION_WORDS) {
    OS << "<invalid>";
    return;
  }

  for (uint64_t I = 0; I != WordCount; ++I) {
    const MCOperand &Word = MI->getOperand(I + 1);
    if (!Word.isImm() || Word.getImm() < 0 || Word.getImm() > 0xffff) {
      OS << "<invalid>";
      return;
    }
    Words[I] = uint16_t(Word.getImm());
  }

  unsigned DeclaredWordCount = declaredWords(Words[0]);
  if (DeclaredWordCount > WordCount &&
      DeclaredWordCount <= BEDROCK_MAX_INSTRUCTION_WORDS) {
    char Text[256];
    if (disassembleWithDeclaredWords(Words, WordCount, DeclaredWordCount, Text,
                                     sizeof(Text))) {
      unsigned CanonicalDeclared = canonicalDeclaredWords(Text);
      if (DeclaredWordCount > WordCount && CanonicalDeclared != 0 &&
          DeclaredWordCount > CanonicalDeclared)
        OS << "LEN " << DeclaredWordCount << ", ";
      OS << Text;
      return;
    }
  }

  char Text[256];
  if (printCountBranchEncoded(Words, WordCount, OS))
    return;

  if (printImmToEAEncoded(Words, WordCount, OS))
    return;

  if (printRawSpecEncodedInst(Words, WordCount, OS))
    return;

  if (bedrock_disassemble_line(Words, WordCount, Text, sizeof(Text), nullptr) ==
      BEDROCK_OK) {
    OS << Text;
    return;
  }

  OS << ".short";
  for (uint64_t I = 0; I != WordCount; ++I) {
    if (I != 0)
      OS << ',';
    OS << " 0x";
    OS.write_hex(Words[I]);
  }
}

void BedrockInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                      raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    printRegName(OS, Op.getReg());
    return;
  }
  if (Op.isImm()) {
    OS << Op.getImm();
    return;
  }
  assert(Op.isExpr() && "unknown operand kind");
  MAI.printExpr(OS, *Op.getExpr());
}

void BedrockInstPrinter::printShortImmOperand(const MCInst *MI, unsigned OpNo,
                                              raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (!Op.isImm() || (MI->getFlags() & Bedrock::DecodedInst) != 0) {
    printOperand(MI, OpNo, OS);
    return;
  }

  OS << Op.getImm();
}

void BedrockInstPrinter::printMemOperand(const MCInst *MI, unsigned OpNo,
                                         raw_ostream &OS) {
  const MCOperand &Base = MI->getOperand(OpNo);
  const MCOperand &Offset = MI->getOperand(OpNo + 1);

  OS << '[';
  if (Base.isReg())
    printRegName(OS, Base.getReg());
  else if (Base.isExpr())
    MAI.printExpr(OS, *Base.getExpr());
  else
    OS << Base.getImm();

  if (Offset.isImm()) {
    int64_t Imm = Offset.getImm();
    if (Imm == 0 && Base.isReg() &&
        shouldPrintExplicitZeroOffset(Base.getReg()))
      OS << " + 0";
    else if (Imm > 0)
      OS << " + " << Imm;
    else if (Imm < 0)
      OS << " + " << Imm;
  } else if (Offset.isExpr()) {
    OS << " + ";
    MAI.printExpr(OS, *Offset.getExpr());
  }
  OS << ']';
}

void BedrockInstPrinter::printAbs64MemOperand(const MCInst *MI, unsigned OpNo,
                                              raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  OS << '[';
  if (Op.isExpr())
    printRelocOperand(Op.getExpr(), "ABS64", OS);
  else
    printOperand(MI, OpNo, OS);
  OS << ']';
}

void BedrockInstPrinter::printPostIncMemOperand(const MCInst *MI, unsigned OpNo,
                                                raw_ostream &OS) {
  const MCOperand &Base = MI->getOperand(OpNo);
  OS << '[';
  if (Base.isReg())
    printRegName(OS, Base.getReg());
  else if (Base.isExpr())
    MAI.printExpr(OS, *Base.getExpr());
  else
    OS << Base.getImm();
  OS << "++]";
}

void BedrockInstPrinter::printUpdateMemOperand(const MCInst *MI, unsigned OpNo,
                                               raw_ostream &OS) {
  const MCOperand &Base = MI->getOperand(OpNo);
  const MCOperand &Update = MI->getOperand(OpNo + 1);
  unsigned Mode = Update.isImm() ? Update.getImm() : Bedrock::UpdateNone;

  switch (Mode) {
  case Bedrock::UpdatePreInc:
    OS << "[++";
    break;
  case Bedrock::UpdatePreDec:
    OS << "[--";
    break;
  default:
    OS << '[';
    break;
  }

  if (Base.isReg())
    printRegName(OS, Base.getReg());
  else if (Base.isExpr())
    MAI.printExpr(OS, *Base.getExpr());
  else
    OS << Base.getImm();

  switch (Mode) {
  case Bedrock::UpdatePostInc:
    OS << "++]";
    break;
  case Bedrock::UpdatePostDec:
    OS << "--]";
    break;
  default:
    OS << ']';
    break;
  }
}

void BedrockInstPrinter::printLeaScale4Operand(const MCInst *MI, unsigned OpNo,
                                               raw_ostream &OS) {
  OS << '[';
  printOperand(MI, OpNo, OS);
  OS << " + ";
  printOperand(MI, OpNo + 1, OS);
  OS << " * 4]";
}

void BedrockInstPrinter::printLeaScale4LOperand(const MCInst *MI, unsigned OpNo,
                                                raw_ostream &OS) {
  OS << '[';
  printOperand(MI, OpNo, OS);
  OS << " + ";
  printOperand(MI, OpNo + 1, OS);
  OS << ".L * 4]";
}

void BedrockInstPrinter::printRelocOperand(const MCExpr *Expr, StringRef Reloc,
                                           raw_ostream &OS) const {
  if (auto *Bin = dyn_cast<MCBinaryExpr>(Expr)) {
    if (auto *Addend = dyn_cast<MCConstantExpr>(Bin->getRHS())) {
      if (Bin->getOpcode() == MCBinaryExpr::Add ||
          Bin->getOpcode() == MCBinaryExpr::Sub) {
        MAI.printExpr(OS, *Bin->getLHS());
        OS << '@' << Reloc;
        int64_t Value = Addend->getValue();
        if (Bin->getOpcode() == MCBinaryExpr::Sub)
          Value = -Value;
        if (Value > 0)
          OS << '+' << Value;
        else if (Value < 0)
          OS << Value;
        return;
      }
    }
  }

  MAI.printExpr(OS, *Expr);
  OS << '@' << Reloc;
}

static void printLongIndexOperand(BedrockInstPrinter &Printer, const MCInst *MI,
                                  unsigned OpNo, unsigned Scale,
                                  bool Signed32Index, raw_ostream &OS) {
  OS << '[';
  Printer.printOperand(MI, OpNo, OS);
  OS << " + ";
  Printer.printOperand(MI, OpNo + 1, OS);
  if (Signed32Index)
    OS << ".L";
  OS << " * " << Scale;

  const MCOperand &Offset = MI->getOperand(OpNo + 2);
  if (Offset.isImm()) {
    int64_t Imm = Offset.getImm();
    if (Imm > 0)
      OS << " + " << Imm;
    else if (Imm < 0)
      OS << " - " << -Imm;
  } else if (Offset.isExpr()) {
    OS << " + ";
    const MCOperand &Base = MI->getOperand(OpNo);
    if (Base.isReg() && Base.getReg() == Bedrock::PC)
      Printer.printRelocOperand(Offset.getExpr(), "PCREL32", OS);
    else
      Printer.printOperand(MI, OpNo + 2, OS);
  }
  OS << ']';
}

void BedrockInstPrinter::printIndexScale1Operand(const MCInst *MI,
                                                 unsigned OpNo,
                                                 raw_ostream &OS) {
  printLongIndexOperand(*this, MI, OpNo, 1, false, OS);
}

void BedrockInstPrinter::printIndexScale4Operand(const MCInst *MI,
                                                 unsigned OpNo,
                                                 raw_ostream &OS) {
  printLongIndexOperand(*this, MI, OpNo, 4, false, OS);
}

void BedrockInstPrinter::printLongIndexScale4Operand(const MCInst *MI,
                                                     unsigned OpNo,
                                                     raw_ostream &OS) {
  printLongIndexOperand(*this, MI, OpNo, 4, true, OS);
}

void BedrockInstPrinter::printLongIndexScale8Operand(const MCInst *MI,
                                                     unsigned OpNo,
                                                     raw_ostream &OS) {
  printLongIndexOperand(*this, MI, OpNo, 8, true, OS);
}

void BedrockInstPrinter::printCondCode(const MCInst *MI, unsigned OpNo,
                                       raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  assert(Op.isImm() && "condition code must be an immediate");

  if (const char *Name = condCodeName(Op.getImm())) {
    OS << Name;
    return;
  }
  llvm_unreachable("unknown Bedrock condition code");
}

void BedrockInstPrinter::printRegMask16(const MCInst *MI, unsigned OpNo,
                                        raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  assert(Op.isImm() && "register mask must be an immediate");

  uint16_t Mask = static_cast<uint16_t>(Op.getImm());
  OS << '{';
  bool NeedComma = false;
  for (unsigned I = 0; I != 16; ++I) {
    if ((Mask & (uint16_t(1) << I)) == 0)
      continue;
    if (NeedComma)
      OS << ',';
    NeedComma = true;
    if (I < 8)
      printRegName(OS, MCRegister(Bedrock::D0 + I));
    else
      printRegName(OS, MCRegister(Bedrock::A0 + I - 8));
  }
  OS << '}';
}

void BedrockInstPrinter::printFRegMask16(const MCInst *MI, unsigned OpNo,
                                         raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  assert(Op.isImm() && "F register mask must be an immediate");

  uint16_t Mask = static_cast<uint16_t>(Op.getImm());
  OS << '{';
  bool NeedComma = false;
  for (unsigned I = 0; I != 16; ++I) {
    if ((Mask & (uint16_t(1) << I)) == 0)
      continue;
    if (NeedComma)
      OS << ',';
    NeedComma = true;
    printRegName(OS, MCRegister(Bedrock::F0 + I));
  }
  OS << '}';
}

void BedrockInstPrinter::printMemoryOrder(const MCInst *MI, unsigned OpNo,
                                          raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  assert(Op.isImm() && "memory order must be an immediate");

  switch (Op.getImm()) {
  default:
    llvm_unreachable("unknown Bedrock memory order");
  case 0:
    OS << "RELAXED";
    return;
  case 1:
    OS << "ACQUIRE";
    return;
  case 2:
    OS << "RELEASE";
    return;
  case 3:
    OS << "ACQREL";
    return;
  case 4:
    OS << "SEQCST";
    return;
  }
}

void BedrockInstPrinter::printAbs64Operand(const MCInst *MI, unsigned OpNo,
                                           raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isExpr()) {
    printRelocOperand(Op.getExpr(), "ABS64", OS);
    return;
  }
  printOperand(MI, OpNo, OS);
}

void BedrockInstPrinter::printImm32Operand(const MCInst *MI, unsigned OpNo,
                                           raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isExpr()) {
    printRelocOperand(Op.getExpr(), "IMM32", OS);
    return;
  }
  printOperand(MI, OpNo, OS);
}
