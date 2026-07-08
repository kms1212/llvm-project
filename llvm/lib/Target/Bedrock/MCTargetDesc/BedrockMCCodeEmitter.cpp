//===-- BedrockMCCodeEmitter.cpp - Bedrock Machine Code Emitter -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockFixupKinds.h"
#include "BedrockInstPrinter.h"
#include "BedrockMCTargetDesc.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cctype>
#include <cstdint>
#include <string>

extern "C" {
#include "bedrock_asm_disasm.h"
}

using namespace llvm;

static bool isIdentChar(char Ch) {
  return std::isalnum(static_cast<unsigned char>(Ch)) || Ch == '_' ||
         Ch == '.' || Ch == '$';
}

static bool isRelocNameChar(char Ch) {
  return std::isalnum(static_cast<unsigned char>(Ch)) || Ch == '_';
}

static bool isPCRelFixup(MCFixupKind Kind) {
  return Bedrock::isPCRelFixup(Kind);
}

static void setDeclaredWords(uint16_t &Word0, unsigned WordCount) {
  Word0 = (Word0 & ~uint16_t(BEDROCK_WORD0_LENGTH_MASK)) |
          uint16_t((WordCount - 1) << 12);
}

static size_t instructionPayloadStart(const bedrock_form_desc *Form);

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

static size_t actualPayloadStartWord(const bedrock_form_desc *Form,
                                     const uint16_t *Words) {
  size_t Start = instructionPayloadStart(Form);
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

static uint64_t signExtendPayload(uint64_t Value, unsigned Bits) {
  if (Bits >= 64)
    return Value;
  uint64_t Mask = (uint64_t(1) << Bits) - 1;
  Value &= Mask;
  uint64_t SignBit = uint64_t(1) << (Bits - 1);
  return (Value ^ SignBit) - SignBit;
}

static unsigned shortestSignedImmediatePayloadWords(uint64_t Value) {
  if (signExtendPayload(Value, 16) == Value)
    return 1;
  if (signExtendPayload(Value, 32) == Value)
    return 2;
  return 4;
}

static uint64_t immediateEATokenForPayloadWords(unsigned Words) {
  switch (Words) {
  case 1:
    return BEDROCK_EA_IMM16;
  case 2:
    return BEDROCK_EA_IMM32;
  case 4:
    return BEDROCK_EA_IMM64;
  default:
    llvm_unreachable("invalid Bedrock immediate payload word count");
  }
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
  if (Kind == "LQ" && Value < 2) {
    Suffix = "LQ"[Value];
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

  size_t PayloadStart = actualPayloadStartWord(Form, Words);

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

static bool compactImmToDPayload(uint16_t *Words, size_t &WordCount,
                                 const bedrock_form_desc *Form) {
  StringRef ID(Form->id);
  if (!ID.ends_with("IMM_TO_D") || findFieldBySource(Form, "imm"))
    return true;

  char Suffix = 0;
  if (!sizeSuffixForForm(Form, Words, Suffix) || Suffix != 'L')
    return true;

  size_t PayloadStart = actualPayloadStartWord(Form, Words);
  if (PayloadStart >= WordCount)
    return false;
  if (WordCount <= PayloadStart + 1)
    return true;

  uint64_t ImmValue = readPayloadWords(Words, PayloadStart,
                                       unsigned(WordCount - PayloadStart));
  if (ImmValue > 0xffff)
    return true;

  Words[PayloadStart] = uint16_t(ImmValue);
  for (size_t I = PayloadStart + 1; I != WordCount; ++I)
    Words[I] = 0;
  WordCount = PayloadStart + 1;
  setDeclaredWords(Words[0], WordCount);
  return true;
}

static bool encodeClearReg(uint16_t *Words, size_t &WordCount, const char *FormID,
                           uint64_t Reg) {
  const bedrock_form_desc *NewForm = bedrock_find_form_by_id(FormID);
  if (!NewForm)
    return false;

  uint64_t FieldValues[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
  for (size_t I = 0; I != NewForm->field_count; ++I) {
    const bedrock_field_desc *Field = bedrock_form_field(NewForm, I);
    if (!Field)
      return false;
    if (StringRef(Field->source) == "dst")
      FieldValues[I] = Reg;
    else
      return false;
  }

  return bedrock_encode_form_words(NewForm, FieldValues, NewForm->field_count,
                                   Words, BEDROCK_MAX_INSTRUCTION_WORDS,
                                   &WordCount) == BEDROCK_OK;
}

static bool encodeMovImmToEAReg(uint16_t *Words, size_t &WordCount,
                                uint64_t DstEA, uint64_t ImmValue,
                                unsigned PayloadWords) {
  const bedrock_form_desc *NewForm = bedrock_find_form_by_id("MOV.EA_TO_EA");
  if (!NewForm)
    return false;

  uint64_t FieldValues[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
  for (size_t I = 0; I != NewForm->field_count; ++I) {
    const bedrock_field_desc *Field = bedrock_form_field(NewForm, I);
    if (!Field)
      return false;
    StringRef Source(Field->source);
    if (Source == "size")
      FieldValues[I] = 3;
    else if (Source == "src" && StringRef(Field->kind) == "EA")
      FieldValues[I] = immediateEATokenForPayloadWords(PayloadWords);
    else if (Source == "dst" && StringRef(Field->kind) == "EA")
      FieldValues[I] = DstEA;
    else
      return false;
  }

  size_t EncodedWordCount = 0;
  if (bedrock_encode_form_words(NewForm, FieldValues, NewForm->field_count,
                                Words, BEDROCK_MAX_INSTRUCTION_WORDS,
                                &EncodedWordCount) != BEDROCK_OK)
    return false;

  size_t PayloadStart = actualPayloadStartWord(NewForm, Words);
  if (PayloadStart + PayloadWords > BEDROCK_MAX_INSTRUCTION_WORDS)
    return false;
  for (unsigned I = 0; I != PayloadWords; ++I)
    Words[PayloadStart + I] = uint16_t((ImmValue >> (I * 16)) & 0xffff);

  WordCount = PayloadStart + PayloadWords;
  setDeclaredWords(Words[0], WordCount);
  return true;
}

static bool compactMovImmToA(uint16_t *Words, size_t &WordCount,
                             const bedrock_form_desc *Form) {
  if (StringRef(Form->id) != "MOV.IMM_TO_A")
    return true;

  const bedrock_field_desc *DstField = findFieldBySource(Form, "dst");
  if (!DstField)
    return false;
  uint64_t AReg = extractFormField(Words, DstField);

  size_t PayloadStart = actualPayloadStartWord(Form, Words);
  if (PayloadStart + 4 > WordCount)
    return false;
  uint64_t ImmValue = readPayloadWords(Words, PayloadStart, 4);
  if (ImmValue == 0)
    return encodeClearReg(Words, WordCount, "CLR.A", AReg);

  unsigned PayloadWords = shortestSignedImmediatePayloadWords(ImmValue);
  if (PayloadWords == 4)
    return true;
  return encodeMovImmToEAReg(Words, WordCount, BEDROCK_EA_AREG + AReg,
                             ImmValue, PayloadWords);
}

static bool compactMovQZeroToD(uint16_t *Words, size_t &WordCount,
                               const bedrock_form_desc *Form) {
  StringRef ID(Form->id);
  if (!ID.starts_with("MOV.EA_TO_D"))
    return true;

  char Suffix = 0;
  if (!sizeSuffixForForm(Form, Words, Suffix) || Suffix != 'Q')
    return true;

  const bedrock_field_desc *SrcField = findFieldBySource(Form, "src");
  const bedrock_field_desc *DstField = findFieldBySource(Form, "dst");
  if (!SrcField || !DstField || StringRef(SrcField->kind) != "EA" ||
      StringRef(DstField->kind) != "DREG")
    return true;

  unsigned ImmWords =
      immediateEAPayloadWords(extractFormField(Words, SrcField));
  if (ImmWords == 0)
    return true;
  size_t PayloadStart = actualPayloadStartWord(Form, Words);
  if (PayloadStart + ImmWords > WordCount)
    return false;
  if (signExtendPayload(readPayloadWords(Words, PayloadStart, ImmWords),
                        ImmWords * 16) != 0)
    return true;

  return encodeClearReg(Words, WordCount, "CLR.D",
                        extractFormField(Words, DstField));
}

static bool compactImmEA6Encoding(uint16_t *Words, size_t &WordCount,
                                  const bedrock_form_desc *Form) {
  if (!compactImmToDPayload(Words, WordCount, Form))
    return false;

  size_t OldWordCount = WordCount;
  if (!compactMovImmToA(Words, WordCount, Form))
    return false;
  if (WordCount != OldWordCount)
    return true;

  OldWordCount = WordCount;
  if (!compactMovQZeroToD(Words, WordCount, Form))
    return false;
  if (WordCount != OldWordCount)
    return true;

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

  size_t PayloadStart = actualPayloadStartWord(Form, Words);
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

static const bedrock_form_desc *decodeFormForActualWords(const uint16_t *Words,
                                                         size_t WordCount) {
  if (WordCount == 0 || WordCount > BEDROCK_MAX_INSTRUCTION_WORDS)
    return nullptr;

  uint16_t DecodeWords[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
  for (size_t I = 0; I != WordCount; ++I)
    DecodeWords[I] = Words[I];
  setDeclaredWords(DecodeWords[0], WordCount);
  return bedrock_decode_form(DecodeWords, WordCount);
}

static unsigned getFixupWidthBits(MCFixupKind Kind) {
  return Bedrock::getFixupWidthBits(Kind);
}

static std::string placeholderText(MCFixupKind Kind) {
  SmallString<32> Text;
  raw_svector_ostream OS(Text);
  OS << "0x";
  OS.write_hex(Bedrock::getRelocationPlaceholder(Kind));
  return std::string(OS.str());
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
  case Bedrock::MOV32imm:
  case Bedrock::OR32imm:
    return Bedrock::fixup_bedrock_imm32;
  case Bedrock::MOV64abs:
  case Bedrock::MOV64symmr:
  case Bedrock::MOV8absrm:
  case Bedrock::MOV16absrm:
  case Bedrock::MOV32absrm:
  case Bedrock::MOV64absrm:
  case Bedrock::MOV8absmr:
  case Bedrock::MOV16absmr:
  case Bedrock::MOV32absmr:
  case Bedrock::MOV64absmr:
  case Bedrock::MOV8absmi:
  case Bedrock::MOV16absmi:
  case Bedrock::MOV32absmi:
  case Bedrock::MOV64absmi:
  case Bedrock::MOV8mabs:
  case Bedrock::MOV16mabs:
  case Bedrock::MOV32mabs:
  case Bedrock::MOV64mabs:
  case Bedrock::FMOV32absrm:
  case Bedrock::FMOV64absrm:
  case Bedrock::FMOV32absmr:
  case Bedrock::FMOV64absmr:
  case Bedrock::INC8absm:
  case Bedrock::INC16absm:
  case Bedrock::INC32absm:
  case Bedrock::INC64absm:
  case Bedrock::DEC8absm:
  case Bedrock::DEC16absm:
  case Bedrock::DEC32absm:
  case Bedrock::DEC64absm:
  case Bedrock::CMP8absmi:
  case Bedrock::CMP16absmi:
  case Bedrock::CMP32absmi:
  case Bedrock::CMP64absmi:
  case Bedrock::TEST8absmi:
  case Bedrock::TEST16absmi:
  case Bedrock::TEST32absmi:
  case Bedrock::TEST64absmi:
  case Bedrock::EXTZQ8absrm:
  case Bedrock::EXTZQ16absrm:
  case Bedrock::EXTZQ32absrm:
  case Bedrock::EXTZQ8absmr:
  case Bedrock::EXTZQ16absmr:
  case Bedrock::EXTZQ32absmr:
  case Bedrock::EXTSQ8absrm:
  case Bedrock::EXTSQ16absrm:
  case Bedrock::EXTSQ32absrm:
  case Bedrock::EXTSQ8absmr:
  case Bedrock::EXTSQ16absmr:
  case Bedrock::EXTSQ32absmr:
  case Bedrock::EXTZL8absrm:
  case Bedrock::EXTZL16absrm:
  case Bedrock::EXTZL8absmr:
  case Bedrock::EXTZL16absmr:
  case Bedrock::EXTSL8absrm:
  case Bedrock::EXTSL16absrm:
  case Bedrock::EXTSL8absmr:
  case Bedrock::EXTSL16absmr:
    return Bedrock::fixup_bedrock_abs64;
  case Bedrock::MOV32idx4lrm:
    return Bedrock::fixup_bedrock_pcrel32;
  }
}

struct ImmToDFormInfo {
  const char *ID = nullptr;
  char Size = 0;
};

static std::optional<ImmToDFormInfo> immToDFormForOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    return std::nullopt;
  case Bedrock::ADD16ri:
    return ImmToDFormInfo{"ADD.IMM_TO_D", 'W'};
  case Bedrock::ADD32ri:
    return ImmToDFormInfo{"ADD.IMM_TO_D", 'L'};
  case Bedrock::SUB16ri:
    return ImmToDFormInfo{"SUB.IMM_TO_D", 'W'};
  case Bedrock::SUB32ri:
    return ImmToDFormInfo{"SUB.IMM_TO_D", 'L'};
  case Bedrock::AND16ri:
    return ImmToDFormInfo{"AND.IMM_TO_D", 'W'};
  case Bedrock::AND32ri:
    return ImmToDFormInfo{"AND.IMM_TO_D", 'L'};
  case Bedrock::OR16ri:
    return ImmToDFormInfo{"OR.IMM_TO_D", 'W'};
  case Bedrock::OR32ri:
    return ImmToDFormInfo{"OR.IMM_TO_D", 'L'};
  case Bedrock::XOR16ri:
    return ImmToDFormInfo{"XOR.IMM_TO_D", 'W'};
  case Bedrock::XOR32ri:
    return ImmToDFormInfo{"XOR.IMM_TO_D", 'L'};
  case Bedrock::CMP16ri:
    return ImmToDFormInfo{"CMP.IMM_TO_D", 'W'};
  case Bedrock::CMP32ri:
    return ImmToDFormInfo{"CMP.IMM_TO_D", 'L'};
  case Bedrock::TEST16ri:
    return ImmToDFormInfo{"TEST.IMM_TO_D", 'W'};
  case Bedrock::TEST32ri:
    return ImmToDFormInfo{"TEST.IMM_TO_D", 'L'};
  }
}

static std::optional<unsigned> dRegIndex(MCRegister Reg) {
  switch (Reg.id()) {
  default:
    return std::nullopt;
  case Bedrock::D0:
  case Bedrock::D1:
  case Bedrock::D2:
  case Bedrock::D3:
  case Bedrock::D4:
  case Bedrock::D5:
  case Bedrock::D6:
  case Bedrock::D7:
    return Reg.id() - Bedrock::D0;
  }
}

static bool getRegImmOperands(const MCInst &MI, MCRegister &Reg,
                              int64_t &Imm) {
  if (MI.getNumOperands() >= 3 && MI.getOperand(0).isReg() &&
      MI.getOperand(2).isImm()) {
    Reg = MI.getOperand(0).getReg();
    Imm = MI.getOperand(2).getImm();
    return true;
  }
  if (MI.getNumOperands() >= 2 && MI.getOperand(0).isReg() &&
      MI.getOperand(1).isImm()) {
    Reg = MI.getOperand(0).getReg();
    Imm = MI.getOperand(1).getImm();
    return true;
  }
  return false;
}

static bool fitsImm16Payload(char Size, int64_t Imm, uint16_t &Payload) {
  if (Size == 'W') {
    if (Imm < -32768 || Imm > 65535)
      return false;
    Payload = uint16_t(Imm);
    return true;
  }
  if (Size == 'L') {
    if (Imm < 0 || Imm > 65535)
      return false;
    Payload = uint16_t(Imm);
    return true;
  }
  return false;
}

static bool encodeDirectImmToD(const MCInst &MI, uint16_t *Words,
                               size_t &WordCount) {
  std::optional<ImmToDFormInfo> Info = immToDFormForOpcode(MI.getOpcode());
  if (!Info)
    return false;

  MCRegister Reg;
  int64_t Imm = 0;
  if (!getRegImmOperands(MI, Reg, Imm))
    return false;

  std::optional<unsigned> DReg = dRegIndex(Reg);
  if (!DReg)
    return false;

  uint16_t Payload = 0;
  if (!fitsImm16Payload(Info->Size, Imm, Payload))
    return false;

  const bedrock_form_desc *Form = bedrock_find_form_by_id(Info->ID);
  if (!Form)
    return false;

  uint64_t FieldValues[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
  for (size_t I = 0; I != Form->field_count; ++I) {
    const bedrock_field_desc *Field = bedrock_form_field(Form, I);
    if (!Field)
      return false;

    StringRef Source(Field->source);
    if (Source == "size") {
      FieldValues[I] = Info->Size == 'L' ? 1 : 0;
      continue;
    }
    if (Source == "dst" || Source == "rhs") {
      FieldValues[I] = *DReg;
      continue;
    }
    return false;
  }

  size_t EncodedWordCount = 0;
  if (bedrock_encode_form_words(Form, FieldValues, Form->field_count, Words,
                                BEDROCK_MAX_INSTRUCTION_WORDS,
                                &EncodedWordCount) != BEDROCK_OK)
    return false;

  size_t PayloadStart = instructionPayloadStart(Form);
  if (PayloadStart >= BEDROCK_MAX_INSTRUCTION_WORDS)
    return false;
  (void)EncodedWordCount;
  Words[PayloadStart] = Payload;
  WordCount = PayloadStart + 1;
  setDeclaredWords(Words[0], WordCount);
  return true;
}

static const MCExpr *getExprOperand(const MCInst &MI) {
  for (const MCOperand &Operand : MI.getOperands()) {
    if (Operand.isExpr())
      return Operand.getExpr();
  }
  return nullptr;
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

static size_t instructionPayloadStart(const bedrock_form_desc *Form) {
  size_t Start = (Form->kind == BEDROCK_FORM_EXTENDED ||
                  Form->kind == BEDROCK_FORM_EXTENDED_ALIAS)
                     ? 2
                     : 1;
  for (size_t I = 0; I != Form->field_count; ++I) {
    const bedrock_field_desc *Field = bedrock_form_field(Form, I);
    if (Field && size_t(Field->token) + 1 > Start)
      Start = size_t(Field->token) + 1;
  }
  return Start;
}

static size_t findFixupOffset(ArrayRef<char> Bytes, const uint16_t *Words,
                              size_t WordCount, const bedrock_form_desc *Form,
                              MCFixupKind Kind, uint64_t Placeholder) {
  unsigned WidthBits = getFixupWidthBits(Kind);
  size_t ByteWidth = WidthBits / 8;
  size_t PayloadWord = instructionPayloadStart(Form) +
                       ((Words[0] & BEDROCK_WORD0_PREFIX_BIT) != 0 ? 1 : 0);
  size_t FallbackOffset = PayloadWord * 2;

  if (ByteWidth == 0 || ByteWidth > 8 || Bytes.size() < ByteWidth)
    return FallbackOffset;

  for (size_t Cursor = FallbackOffset; Cursor + ByteWidth <= Bytes.size();
       ++Cursor) {
    bool Match = true;
    for (size_t I = 0; I != ByteWidth; ++I) {
      if (static_cast<uint8_t>(Bytes[Cursor + I]) !=
          static_cast<uint8_t>((Placeholder >> (I * 8)) & 0xff)) {
        Match = false;
        break;
      }
    }
    if (Match)
      return Cursor;
  }
  (void)WordCount;
  return FallbackOffset;
}

namespace {

struct EncodedFixup {
  MCFixupKind Kind = FK_NONE;
  uint64_t Placeholder = 0;
  const MCExpr *Expr = nullptr;
};

class BedrockMCCodeEmitter : public MCCodeEmitter {
  MCContext &Ctx;
  mutable BedrockInstPrinter InstPrinter;

public:
  BedrockMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx)
      : Ctx(Ctx), InstPrinter(*Ctx.getAsmInfo(), MCII, *Ctx.getRegisterInfo()) {
  }

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;
};

} // end anonymous namespace

void BedrockMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                             SmallVectorImpl<char> &CB,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  if (MI.getOpcode() == Bedrock::ENCODED) {
    constexpr unsigned EncodedWordOperandCount =
        BEDROCK_MAX_INSTRUCTION_WORDS + 1;
    if (MI.getNumOperands() < EncodedWordOperandCount ||
        !MI.getOperand(0).isImm()) {
      Ctx.reportError(MI.getLoc(), "invalid Bedrock encoded instruction");
      return;
    }
    uint64_t WordCount = MI.getOperand(0).getImm();
    if (WordCount == 0 || WordCount > BEDROCK_MAX_INSTRUCTION_WORDS) {
      Ctx.reportError(MI.getLoc(), "invalid Bedrock encoded word count");
      return;
    }
    uint16_t Words[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
    SmallString<16> Encoded;
    raw_svector_ostream OS(Encoded);
    for (uint64_t I = 0; I != WordCount; ++I) {
      const MCOperand &Word = MI.getOperand(I + 1);
      if (!Word.isImm() || Word.getImm() < 0 || Word.getImm() > 0xffff) {
        Ctx.reportError(MI.getLoc(), "invalid Bedrock encoded word");
        return;
      }
      Words[I] = uint16_t(Word.getImm());
      support::endian::write<uint16_t>(OS, Words[I], llvm::endianness::little);
    }

    SmallVector<EncodedFixup, 4> EncodedFixups;
    if (MI.getNumOperands() != EncodedWordOperandCount) {
      if (!MI.getOperand(EncodedWordOperandCount).isImm()) {
        Ctx.reportError(MI.getLoc(), "invalid Bedrock encoded fixup");
        return;
      }
      uint64_t FixupCount = MI.getOperand(EncodedWordOperandCount).getImm();
      if (MI.getNumOperands() != EncodedWordOperandCount + 1 + FixupCount * 3) {
        Ctx.reportError(MI.getLoc(), "invalid Bedrock encoded fixup count");
        return;
      }
      for (uint64_t I = 0; I != FixupCount; ++I) {
        unsigned Base = EncodedWordOperandCount + 1 + I * 3;
        if (!MI.getOperand(Base).isImm() ||
            !MI.getOperand(Base + 1).isImm() ||
            !MI.getOperand(Base + 2).isExpr()) {
          Ctx.reportError(MI.getLoc(), "invalid Bedrock encoded fixup");
          return;
        }
        EncodedFixup Fixup;
        Fixup.Kind = static_cast<MCFixupKind>(MI.getOperand(Base).getImm());
        Fixup.Placeholder = MI.getOperand(Base + 1).getImm();
        Fixup.Expr = MI.getOperand(Base + 2).getExpr();
        EncodedFixups.push_back(Fixup);
      }
    }

    if (!EncodedFixups.empty()) {
      const bedrock_form_desc *Form =
          decodeFormForActualWords(Words, WordCount);
      if (Form == nullptr) {
        Ctx.reportError(MI.getLoc(), "cannot decode Bedrock encoded fixup");
        return;
      }
      for (const EncodedFixup &Fixup : EncodedFixups) {
        size_t FixupOffset =
            findFixupOffset(ArrayRef<char>(Encoded.data(), Encoded.size()),
                            Words, WordCount, Form, Fixup.Kind,
                            Fixup.Placeholder);
        unsigned WidthBits = getFixupWidthBits(Fixup.Kind);
        size_t ByteWidth = WidthBits / 8;
        if (ByteWidth != 0 && FixupOffset + ByteWidth > Encoded.size()) {
          Ctx.reportError(MI.getLoc(), "invalid Bedrock encoded fixup offset");
          return;
        }
        for (size_t I = 0; I != ByteWidth; ++I)
          Encoded[FixupOffset + I] = 0;
        Fixups.push_back(MCFixup::create(FixupOffset, Fixup.Expr, Fixup.Kind,
                                         isPCRelFixup(Fixup.Kind)));
      }
    }
    CB.append(Encoded.begin(), Encoded.end());
    return;
  }

  uint16_t DirectWords[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
  size_t DirectWordCount = 0;
  if (encodeDirectImmToD(MI, DirectWords, DirectWordCount)) {
    SmallString<16> Encoded;
    raw_svector_ostream EncodedOS(Encoded);
    for (size_t I = 0; I != DirectWordCount; ++I)
      support::endian::write<uint16_t>(EncodedOS, DirectWords[I],
                                       llvm::endianness::little);
    CB.append(Encoded.begin(), Encoded.end());
    return;
  }

  SmallString<256> Printed;
  raw_svector_ostream OS(Printed);
  InstPrinter.printInst(&MI, /*Address*/ 0, /*Annot*/ "", STI, OS);

  std::string Line = std::string(Printed.str());
  MCFixupKind FixupKind = getFixupKindForOpcode(MI.getOpcode());
  const MCExpr *FixupExpr = getExprOperand(MI);
  bool HasFixup = FixupKind != FK_NONE && FixupExpr != nullptr;
  if (FixupExpr != nullptr && FixupKind == FK_NONE) {
    Ctx.reportError(MI.getLoc(), "unsupported Bedrock symbolic operand in `" +
                                     Twine(Line) + "`");
    return;
  }
  if (HasFixup && !replaceRelocExpression(Line, FixupKind)) {
    Ctx.reportError(MI.getLoc(), "missing Bedrock relocation annotation in `" +
                                     Twine(Printed) + "`");
    return;
  }

  uint16_t Words[BEDROCK_MAX_INSTRUCTION_WORDS];
  size_t WordCount = 0;
  const bedrock_form_desc *Form = nullptr;
  int Status = bedrock_assemble_line(
      Line.c_str(), Words, BEDROCK_MAX_INSTRUCTION_WORDS, &WordCount, &Form);
  if (Status != BEDROCK_OK || WordCount == 0 || Form == nullptr ||
      !compactImmEA6Encoding(Words, WordCount, Form)) {
    Ctx.reportError(MI.getLoc(), "cannot encode Bedrock instruction `" +
                                     Twine(Printed) + "`");
    return;
  }
  setDeclaredWords(Words[0], WordCount);

  SmallString<16> Encoded;
  raw_svector_ostream EncodedOS(Encoded);
  for (size_t I = 0; I != WordCount; ++I)
    support::endian::write<uint16_t>(EncodedOS, Words[I],
                                     llvm::endianness::little);

  if (HasFixup) {
    const bedrock_form_desc *DecodedForm =
        decodeFormForActualWords(Words, WordCount);
    if (DecodedForm == nullptr) {
      Ctx.reportError(MI.getLoc(), "cannot decode Bedrock fixup in `" +
                                       Twine(Printed) + "`");
      return;
    }
    size_t FixupOffset =
        findFixupOffset(ArrayRef<char>(Encoded.data(), Encoded.size()), Words,
                        WordCount, DecodedForm, FixupKind,
                        Bedrock::getRelocationPlaceholder(FixupKind));
    unsigned WidthBits = getFixupWidthBits(FixupKind);
    size_t ByteWidth = WidthBits / 8;
    if (FixupOffset + ByteWidth > Encoded.size()) {
      Ctx.reportError(MI.getLoc(), "invalid Bedrock fixup offset in `" +
                                       Twine(Printed) + "`");
      return;
    }
    for (size_t I = 0; I != ByteWidth; ++I)
      Encoded[FixupOffset + I] = 0;
    Fixups.push_back(MCFixup::create(FixupOffset, FixupExpr, FixupKind,
                                     isPCRelFixup(FixupKind)));
  }

  CB.append(Encoded.begin(), Encoded.end());
}

MCCodeEmitter *llvm::createBedrockMCCodeEmitter(const MCInstrInfo &MCII,
                                                MCContext &Ctx) {
  return new BedrockMCCodeEmitter(MCII, Ctx);
}
