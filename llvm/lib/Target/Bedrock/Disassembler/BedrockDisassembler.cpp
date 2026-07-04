//===-- BedrockDisassembler.cpp - Disassembler for Bedrock ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "TargetInfo/BedrockTargetInfo.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <optional>

extern "C" {
#include "bedrock_asm_disasm.h"
}

using namespace llvm;

#define DEBUG_TYPE "bedrock-disassembler"

using DecodeStatus = MCDisassembler::DecodeStatus;

namespace {

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

struct BedrockEA {
  enum KindTy { Register, Memory, Immediate } Kind;
  MCRegister Reg = Bedrock::NoRegister;
  MCRegister Index = Bedrock::NoRegister;
  int64_t Imm = 0;
  unsigned Scale = 1;
  bool Signed32Index = false;
  unsigned Update = Bedrock::UpdateNone;
};

class BedrockDisassembler : public MCDisassembler {
public:
  BedrockDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};

} // end anonymous namespace

static unsigned declaredWords(uint16_t Word0) {
  return ((Word0 & BEDROCK_WORD0_LENGTH_MASK) >> 12) + 1;
}

static void setDeclaredWords(uint16_t &Word0, unsigned WordCount) {
  Word0 = (Word0 & ~uint16_t(BEDROCK_WORD0_LENGTH_MASK)) |
          uint16_t((WordCount - 1) << 12);
}

static bool isExtendedForm(const bedrock_form_desc *Form) {
  return Form->kind == BEDROCK_FORM_EXTENDED ||
         Form->kind == BEDROCK_FORM_EXTENDED_ALIAS;
}

static size_t payloadStartWord(const bedrock_form_desc *Form,
                               const uint16_t *Words) {
  size_t Start = isExtendedForm(Form) ? 2 : 1;
  for (size_t I = 0; I != Form->field_count; ++I) {
    const bedrock_field_desc *Field = bedrock_form_field(Form, I);
    if (Field && size_t(Field->token) + 1 > Start)
      Start = size_t(Field->token) + 1;
  }
  if ((Words[0] & BEDROCK_WORD0_PREFIX_BIT) != 0)
    ++Start;
  return Start;
}

static unsigned repeatGroupFlags(const uint16_t *Words, size_t WordCount) {
  if (WordCount < 2 || (Words[0] & BEDROCK_WORD0_PREFIX_BIT) == 0)
    return 0;

  unsigned Flags = 0;
  uint16_t PrefixWord = Words[1];
  for (uint16_t Prefix : {uint16_t(PrefixWord & 0x00ffu),
                          uint16_t((PrefixWord >> 8) & 0x00ffu)}) {
    if (Prefix >= 0x70u && Prefix <= 0x77u) {
      Flags |= Bedrock::RepgStart;
      Flags &= ~Bedrock::RepgCounterMask;
      Flags |= unsigned(Prefix & 0x07u) << Bedrock::RepgCounterShift;
    } else if (Prefix == 0x78u) {
      Flags |= Bedrock::RepgEnd;
    }
  }
  return Flags;
}

static uint64_t readPayload(const uint16_t *Words, size_t Start, size_t Count) {
  uint64_t Value = 0;
  for (size_t I = 0; I != Count; ++I)
    Value |= uint64_t(Words[Start + I]) << (I * 16);
  return Value;
}

static DecodeStatus setEncodedInstWords(MCInst &MI, const uint16_t *Words,
                                        size_t WordCount) {
  if (WordCount == 0 || WordCount > BEDROCK_MAX_INSTRUCTION_WORDS)
    return MCDisassembler::Fail;

  MI.clear();
  MI.setOpcode(Bedrock::ENCODED);
  MI.addOperand(MCOperand::createImm(WordCount));
  for (size_t I = 0; I != BEDROCK_MAX_INSTRUCTION_WORDS; ++I)
    MI.addOperand(MCOperand::createImm(I < WordCount ? Words[I] : 0));
  return MCDisassembler::Success;
}

static int64_t signExtend(uint64_t Value, unsigned Bits) {
  if (Bits >= 64)
    return static_cast<int64_t>(Value);
  uint64_t Sign = 1ULL << (Bits - 1);
  return static_cast<int64_t>((Value ^ Sign) - Sign);
}

static const bedrock_field_desc *
findFieldBySource(const bedrock_form_desc *Form, StringRef Source) {
  for (size_t I = 0; I != Form->field_count; ++I) {
    const bedrock_field_desc *Field = bedrock_form_field(Form, I);
    if (Field && Source == Field->source)
      return Field;
  }
  return nullptr;
}

static const bedrock_field_desc *findFieldByName(const bedrock_form_desc *Form,
                                                 StringRef Name) {
  for (size_t I = 0; I != Form->field_count; ++I) {
    const bedrock_field_desc *Field = bedrock_form_field(Form, I);
    if (Field && Name == Field->name)
      return Field;
  }
  return nullptr;
}

static uint64_t extractField(const uint16_t *Words,
                             const bedrock_field_desc *Field) {
  if (!Words || !Field || Field->width == 0)
    return 0;
  unsigned Token = Field->token;
  if (Token != 0 && (Words[0] & BEDROCK_WORD0_PREFIX_BIT) != 0)
    ++Token;
  uint64_t Mask =
      Field->width >= 16 ? 0xffffull : ((1ull << Field->width) - 1ull);
  return (uint64_t(Words[Token]) >> Field->low_bit) & Mask;
}

static std::optional<uint64_t> fieldValueBySource(const bedrock_form_desc *Form,
                                                  const uint16_t *Words,
                                                  StringRef Source) {
  const bedrock_field_desc *Field = findFieldBySource(Form, Source);
  if (!Field)
    return std::nullopt;
  return extractField(Words, Field);
}

static std::optional<char> sizeSuffix(const bedrock_form_desc *Form,
                                      const uint16_t *Words) {
  const bedrock_field_desc *Field = findFieldBySource(Form, "size");
  if (!Field)
    return std::nullopt;

  uint64_t Value = extractField(Words, Field);
  StringRef Kind(Field->kind);
  if (Kind == "BWLQ" && Value < 4)
    return "BWLQ"[Value];
  if (Kind == "BWL" && Value < 3)
    return "BWL"[Value];
  if (Kind == "BW" && Value < 2)
    return "BW"[Value];
  if (Kind == "LQ" && Value < 2)
    return "LQ"[Value];
  if (Kind == "WL" && Value < 2)
    return "WL"[Value];
  if (Kind == "S_D" && Value < 2)
    return Value == 0 ? 'S' : 'D';
  return std::nullopt;
}

static unsigned bitsForSizeSuffix(char Suffix) {
  switch (Suffix) {
  case 'B':
    return 8;
  case 'W':
    return 16;
  case 'L':
  case 'S':
    return 32;
  case 'Q':
  case 'D':
    return 64;
  default:
    return 0;
  }
}

static size_t wordsForBits(unsigned Bits) { return (Bits + 15) / 16; }

static unsigned immediateEAPayloadBits(uint64_t Value) {
  if (Value == BEDROCK_EA_IMM16)
    return 16;
  if (Value == BEDROCK_EA_IMM32)
    return 32;
  if (Value == BEDROCK_EA_IMM64)
    return 64;
  return 0;
}

static MCRegister dReg(uint64_t Index) {
  return Index < 8 ? MCRegister(Bedrock::D0 + Index) : Bedrock::NoRegister;
}

static MCRegister aReg(uint64_t Index) {
  return Index < 8 ? MCRegister(Bedrock::A0 + Index) : Bedrock::NoRegister;
}

static MCRegister fReg(uint64_t Index) {
  return Index < 16 ? MCRegister(Bedrock::F0 + Index) : Bedrock::NoRegister;
}

static MCRegister regFromFieldKind(StringRef Kind, uint64_t Value) {
  if (Kind == "DREG")
    return dReg(Value);
  if (Kind == "AREG")
    return aReg(Value);
  if (Kind == "FREG")
    return fReg(Value);
  return Bedrock::NoRegister;
}

static std::optional<MCRegister> regBySource(const bedrock_form_desc *Form,
                                             const uint16_t *Words,
                                             StringRef Source) {
  const bedrock_field_desc *Field = findFieldBySource(Form, Source);
  if (!Field)
    return std::nullopt;
  MCRegister Reg = regFromFieldKind(Field->kind, extractField(Words, Field));
  if (Reg == Bedrock::NoRegister)
    return std::nullopt;
  return Reg;
}

static std::optional<MCRegister> regByName(const bedrock_form_desc *Form,
                                           const uint16_t *Words,
                                           StringRef Name) {
  const bedrock_field_desc *Field = findFieldByName(Form, Name);
  if (!Field)
    return std::nullopt;
  MCRegister Reg = regFromFieldKind(Field->kind, extractField(Words, Field));
  if (Reg == Bedrock::NoRegister)
    return std::nullopt;
  return Reg;
}

static bool addReg(MCInst &MI, std::optional<MCRegister> Reg) {
  if (!Reg)
    return false;
  MI.addOperand(MCOperand::createReg(*Reg));
  return true;
}

static bool addReg(MCInst &MI, MCRegister Reg) {
  if (Reg == Bedrock::NoRegister)
    return false;
  MI.addOperand(MCOperand::createReg(Reg));
  return true;
}

static void addMem(MCInst &MI, MCRegister Base, int64_t Offset) {
  MI.addOperand(MCOperand::createReg(Base));
  MI.addOperand(MCOperand::createImm(Offset));
}

static bool hasUpdate(const BedrockEA &EA) {
  return EA.Update != Bedrock::UpdateNone;
}

static bool isPostInc(const BedrockEA &EA) {
  return EA.Update == Bedrock::UpdatePostInc;
}

static void addMaybePostMem(MCInst &MI, const BedrockEA &EA) {
  MI.addOperand(MCOperand::createReg(EA.Reg));
  if (!hasUpdate(EA))
    MI.addOperand(MCOperand::createImm(EA.Imm));
}

static void addUpdateMem(MCInst &MI, const BedrockEA &EA) {
  MI.addOperand(MCOperand::createReg(EA.Reg));
  MI.addOperand(MCOperand::createImm(EA.Update));
}

static bool addIndexedMem(MCInst &MI, const BedrockEA &EA) {
  if (EA.Reg == Bedrock::NoRegister || EA.Index == Bedrock::NoRegister)
    return false;
  MI.addOperand(MCOperand::createReg(EA.Reg));
  MI.addOperand(MCOperand::createReg(EA.Index));
  MI.addOperand(MCOperand::createImm(EA.Imm));
  return true;
}

static bool isIndexedEA(const BedrockEA &EA) {
  return EA.Index != Bedrock::NoRegister;
}

struct PrefixState {
  uint8_t Bytes[2] = {0, 0};
};

static PrefixState getPrefixState(const uint16_t *Words, size_t WordCount) {
  PrefixState State;
  if (WordCount >= 2 && (Words[0] & BEDROCK_WORD0_PREFIX_BIT) != 0) {
    State.Bytes[0] = Words[1] & 0xffu;
    State.Bytes[1] = (Words[1] >> 8) & 0xffu;
  }
  return State;
}

static std::optional<MCRegister> repeatCounterReg(const uint16_t *Words,
                                                  size_t WordCount) {
  if (WordCount < 2 || (Words[0] & BEDROCK_WORD0_PREFIX_BIT) == 0)
    return std::nullopt;
  uint16_t PrefixWord = Words[1];
  for (uint16_t Prefix : {uint16_t(PrefixWord & 0x00ffu),
                          uint16_t((PrefixWord >> 8) & 0x00ffu)}) {
    if (Prefix >= 0x80u && Prefix <= 0x87u)
      return dReg(Prefix & 0x07u);
  }
  return std::nullopt;
}

static bool prefixIsRepeat(uint8_t Prefix) {
  return Prefix >= 0x80u || (Prefix >= 0x70u && Prefix <= 0x78u);
}

static bool isUpdatePrefix(uint8_t Prefix) {
  return Prefix >= Bedrock::UpdatePostInc && Prefix <= Bedrock::UpdatePreDec;
}

static std::optional<unsigned> updatePrefix(const PrefixState &State) {
  std::optional<unsigned> Update;
  for (uint8_t Prefix : State.Bytes) {
    if (isUpdatePrefix(Prefix))
      Update = Prefix;
  }
  return Update;
}

static bool consumeUpdatePrefix(PrefixState *State, BedrockEA &EA) {
  if (!State || EA.Kind != BedrockEA::Memory)
    return true;
  std::optional<unsigned> Update = updatePrefix(*State);
  if (!Update)
    return true;
  for (uint8_t Prefix : State->Bytes) {
    if (Prefix == 0 || prefixIsRepeat(Prefix) || isUpdatePrefix(Prefix))
      continue;
    return false;
  }
  if (EA.Imm != 0 || isIndexedEA(EA))
    return false;
  EA.Update = *Update;
  return true;
}

static bool decodeEA(const uint16_t *Words, size_t WordCount, uint64_t Value,
                     size_t &PayloadCursor, BedrockEA &EA) {
  if (Value >= BEDROCK_EA_DREG && Value < BEDROCK_EA_AREG) {
    EA.Kind = BedrockEA::Register;
    EA.Reg = dReg(Value - BEDROCK_EA_DREG);
    return EA.Reg != Bedrock::NoRegister;
  }
  if (Value >= BEDROCK_EA_AREG && Value < BEDROCK_EA_INDIRECT) {
    EA.Kind = BedrockEA::Register;
    EA.Reg = aReg(Value - BEDROCK_EA_AREG);
    return EA.Reg != Bedrock::NoRegister;
  }
  if (Value == BEDROCK_EA_SPREG) {
    EA.Kind = BedrockEA::Register;
    EA.Reg = Bedrock::SP;
    return true;
  }
  if (Value >= BEDROCK_EA_INDIRECT && Value < BEDROCK_EA_A_DISP16) {
    EA.Kind = BedrockEA::Memory;
    EA.Reg = aReg(Value - BEDROCK_EA_INDIRECT);
    EA.Imm = 0;
    return EA.Reg != Bedrock::NoRegister;
  }

  auto DecodeDisp = [&](MCRegister Base, size_t WordsToRead,
                        unsigned Bits) -> bool {
    if (PayloadCursor + WordsToRead > WordCount)
      return false;
    EA.Kind = BedrockEA::Memory;
    EA.Reg = Base;
    EA.Imm = signExtend(readPayload(Words, PayloadCursor, WordsToRead), Bits);
    PayloadCursor += WordsToRead;
    return true;
  };

  if (Value >= BEDROCK_EA_A_DISP16 && Value < BEDROCK_EA_A_DISP32)
    return DecodeDisp(aReg(Value - BEDROCK_EA_A_DISP16), 1, 16);
  if (Value >= BEDROCK_EA_A_DISP32 && Value < BEDROCK_EA_PC_DISP16)
    return DecodeDisp(aReg(Value - BEDROCK_EA_A_DISP32), 2, 32);
  if (Value == BEDROCK_EA_PC_DISP16)
    return DecodeDisp(Bedrock::PC, 1, 16);
  if (Value == BEDROCK_EA_PC_DISP32)
    return DecodeDisp(Bedrock::PC, 2, 32);
  if (Value == BEDROCK_EA_PC_DISP64)
    return DecodeDisp(Bedrock::PC, 4, 64);
  if (Value == BEDROCK_EA_SP_DISP16)
    return DecodeDisp(Bedrock::SP, 1, 16);
  if (Value == BEDROCK_EA_SP_DISP32)
    return DecodeDisp(Bedrock::SP, 2, 32);
  if (Value == BEDROCK_EA_SP_DISP64)
    return DecodeDisp(Bedrock::SP, 4, 64);

  auto DecodeIndexed = [&](bool Signed32Index) -> bool {
    if (PayloadCursor >= WordCount)
      return false;

    uint16_t Desc = Words[PayloadCursor++];
    unsigned Mode = (Desc >> 11) & 0x1f;
    unsigned IndexNo = (Desc >> 2) & 0x7;
    unsigned ScaleCode = Desc & 0x3;
    MCRegister Base = Bedrock::NoRegister;
    size_t DispWords = 0;
    unsigned DispBits = 0;

    switch (Mode) {
    default:
      return false;
    case 0x0:
      Base = aReg((Desc >> 5) & 0x7);
      break;
    case 0x1:
      Base = aReg((Desc >> 5) & 0x7);
      DispWords = 1;
      DispBits = 16;
      break;
    case 0x2:
      Base = aReg((Desc >> 5) & 0x7);
      DispWords = 2;
      DispBits = 32;
      break;
    case 0x3:
      Base = aReg((Desc >> 5) & 0x7);
      DispWords = 4;
      DispBits = 64;
      break;
    case 0x9:
    case 0xc:
      Base = Mode == 0x9 ? Bedrock::SP : Bedrock::PC;
      DispWords = 1;
      DispBits = 16;
      break;
    case 0xa:
    case 0xd:
      Base = Mode == 0xa ? Bedrock::SP : Bedrock::PC;
      DispWords = 2;
      DispBits = 32;
      break;
    case 0xb:
    case 0xe:
      Base = Mode == 0xb ? Bedrock::SP : Bedrock::PC;
      DispWords = 4;
      DispBits = 64;
      break;
    }

    if (Base == Bedrock::NoRegister || PayloadCursor + DispWords > WordCount)
      return false;

    EA.Kind = BedrockEA::Memory;
    EA.Reg = Base;
    EA.Index = dReg(IndexNo);
    EA.Scale = 1u << ScaleCode;
    EA.Signed32Index = Signed32Index;
    EA.Imm = DispWords == 0
                 ? 0
                 : signExtend(readPayload(Words, PayloadCursor, DispWords),
                              DispBits);
    PayloadCursor += DispWords;
    return EA.Index != Bedrock::NoRegister;
  };

  if (Value == BEDROCK_EA_EXTENDED)
    return DecodeIndexed(false);
  if (Value == BEDROCK_EA_S32_INDEXED_EXTENDED)
    return DecodeIndexed(true);

  auto DecodeImm = [&](size_t WordsToRead, unsigned Bits) -> bool {
    if (PayloadCursor + WordsToRead > WordCount)
      return false;
    EA.Kind = BedrockEA::Immediate;
    EA.Imm = signExtend(readPayload(Words, PayloadCursor, WordsToRead), Bits);
    PayloadCursor += WordsToRead;
    return true;
  };

  if (Value == BEDROCK_EA_IMM16)
    return DecodeImm(1, 16);
  if (Value == BEDROCK_EA_IMM32)
    return DecodeImm(2, 32);
  if (Value == BEDROCK_EA_IMM64)
    return DecodeImm(4, 64);

  return false;
}

static std::optional<BedrockEA>
decodeEABySource(const bedrock_form_desc *Form, const uint16_t *Words,
                 size_t WordCount, StringRef Source, size_t &PayloadCursor,
                 PrefixState *Prefixes = nullptr) {
  const bedrock_field_desc *Field = findFieldBySource(Form, Source);
  if (!Field)
    return std::nullopt;
  uint64_t Value = extractField(Words, Field);
  MCRegister Reg = regFromFieldKind(Field->kind, Value);
  if (Reg != Bedrock::NoRegister) {
    BedrockEA EA;
    EA.Kind = BedrockEA::Register;
    EA.Reg = Reg;
    return EA;
  }
  BedrockEA EA;
  if (!decodeEA(Words, WordCount, Value, PayloadCursor, EA))
    return std::nullopt;
  if (!consumeUpdatePrefix(Prefixes, EA))
    return std::nullopt;
  return EA;
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
  default:
    return 0;
  }
}

static unsigned movccRROpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::MOVCC8rr;
  case 'W':
    return Bedrock::MOVCC16rr;
  case 'L':
    return Bedrock::MOVCC32rr;
  case 'Q':
    return Bedrock::MOVCC64rr;
  default:
    return 0;
  }
}

static unsigned movccRMOpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::MOVCC8rm;
  case 'W':
    return Bedrock::MOVCC16rm;
  case 'L':
    return Bedrock::MOVCC32rm;
  case 'Q':
    return Bedrock::MOVCC64rm;
  default:
    return 0;
  }
}

static unsigned movccMROpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::MOVCC8mr;
  case 'W':
    return Bedrock::MOVCC16mr;
  case 'L':
    return Bedrock::MOVCC32mr;
  case 'Q':
    return Bedrock::MOVCC64mr;
  default:
    return 0;
  }
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
  default:
    return 0;
  }
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
  default:
    return 0;
  }
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
  default:
    return 0;
  }
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
  default:
    return 0;
  }
}

static unsigned movIndexedRMOpcode(char Suffix, const BedrockEA &EA) {
  if (!EA.Signed32Index && EA.Scale == 1) {
    switch (Suffix) {
    case 'B':
      return Bedrock::MOV8idx1rm;
    case 'W':
      return Bedrock::MOV16idx1rm;
    case 'L':
      return Bedrock::MOV32idx1rm;
    case 'Q':
      return Bedrock::MOV64idx1rm;
    default:
      return 0;
    }
  }
  if (!EA.Signed32Index && EA.Scale == 4) {
    switch (Suffix) {
    case 'B':
      return Bedrock::MOV8idx4rm;
    case 'W':
      return Bedrock::MOV16idx4rm;
    case 'L':
      return Bedrock::MOV32idx4rm;
    case 'Q':
      return Bedrock::MOV64idx4rm;
    default:
      return 0;
    }
  }
  if (EA.Signed32Index && EA.Scale == 4) {
    switch (Suffix) {
    case 'B':
      return Bedrock::MOV8idx4lrm;
    case 'W':
      return Bedrock::MOV16idx4lrm;
    case 'L':
      return Bedrock::MOV32idx4lrm;
    case 'Q':
      return Bedrock::MOV64idx4lrm;
    default:
      return 0;
    }
  }
  return 0;
}

static unsigned movIndexedMROpcode(char Suffix, const BedrockEA &EA) {
  if (!EA.Signed32Index && EA.Scale == 1) {
    switch (Suffix) {
    case 'B':
      return Bedrock::MOV8idx1mr;
    case 'W':
      return Bedrock::MOV16idx1mr;
    case 'L':
      return Bedrock::MOV32idx1mr;
    case 'Q':
      return Bedrock::MOV64idx1mr;
    default:
      return 0;
    }
  }
  if (!EA.Signed32Index && EA.Scale == 4) {
    switch (Suffix) {
    case 'B':
      return Bedrock::MOV8idx4mr;
    case 'W':
      return Bedrock::MOV16idx4mr;
    case 'L':
      return Bedrock::MOV32idx4mr;
    case 'Q':
      return Bedrock::MOV64idx4mr;
    default:
      return 0;
    }
  }
  if (EA.Signed32Index && EA.Scale == 4) {
    switch (Suffix) {
    case 'B':
      return Bedrock::MOV8idx4lmr;
    case 'W':
      return Bedrock::MOV16idx4lmr;
    case 'L':
      return Bedrock::MOV32idx4lmr;
    case 'Q':
      return Bedrock::MOV64idx4lmr;
    default:
      return 0;
    }
  }
  return 0;
}

static unsigned movPostRMOpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::MOV8postrm;
  case 'W':
    return Bedrock::MOV16postrm;
  case 'L':
    return Bedrock::MOV32postrm;
  case 'Q':
    return Bedrock::MOV64postrm;
  default:
    return 0;
  }
}

static unsigned movPostMROpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::MOV8postmr;
  case 'W':
    return Bedrock::MOV16postmr;
  case 'L':
    return Bedrock::MOV32postmr;
  case 'Q':
    return Bedrock::MOV64postmr;
  default:
    return 0;
  }
}

static unsigned movMMOpcode(char Suffix, bool SrcPost, bool DstPost) {
  switch (Suffix) {
  case 'B':
    return SrcPost == DstPost
               ? (SrcPost ? Bedrock::MOV8mmpostboth : Bedrock::MOV8mm)
               : 0;
  case 'W':
    return SrcPost == DstPost
               ? (SrcPost ? Bedrock::MOV16mmpostboth : Bedrock::MOV16mm)
               : 0;
  case 'L':
    return SrcPost == DstPost
               ? (SrcPost ? Bedrock::MOV32mmpostboth : Bedrock::MOV32mm)
               : 0;
  case 'Q':
    return SrcPost == DstPost
               ? (SrcPost ? Bedrock::MOV64mmpostboth : Bedrock::MOV64mm)
               : 0;
  default:
    return 0;
  }
}

static unsigned repMovPostMROpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::REPMOV8postmr64;
  case 'L':
    return Bedrock::REPMOV32postmr;
  default:
    return 0;
  }
}

static unsigned repMovMMOpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::REPMOV8mmpostboth64;
  case 'L':
    return Bedrock::REPMOV32mmpostboth;
  case 'Q':
    return Bedrock::REPMOV64mmpostboth;
  default:
    return 0;
  }
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

static unsigned incDecIndexedMOpcode(StringRef Mnemonic, const BedrockEA &EA,
                                     char Suffix) {
  if (Suffix != 'L')
    return 0;
  if (!EA.Signed32Index && EA.Scale == 1)
    return Mnemonic == "INC"   ? Bedrock::INC32idx1m
           : Mnemonic == "DEC" ? Bedrock::DEC32idx1m
                               : 0;
  if (!EA.Signed32Index && EA.Scale == 4)
    return Mnemonic == "INC"   ? Bedrock::INC32idx4m
           : Mnemonic == "DEC" ? Bedrock::DEC32idx4m
                               : 0;
  if (EA.Signed32Index && EA.Scale == 4)
    return Mnemonic == "INC"   ? Bedrock::INC32idx4lm
           : Mnemonic == "DEC" ? Bedrock::DEC32idx4lm
                               : 0;
  return 0;
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
      .Case("MAXS", Suffix == 'B'   ? Bedrock::MAXS8rr
                    : Suffix == 'W' ? Bedrock::MAXS16rr
                    : Suffix == 'L' ? Bedrock::MAXS32rr
                                    : Bedrock::MAXS64rr)
      .Case("MAXU", Suffix == 'B'   ? Bedrock::MAXU8rr
                    : Suffix == 'W' ? Bedrock::MAXU16rr
                    : Suffix == 'L' ? Bedrock::MAXU32rr
                                    : Bedrock::MAXU64rr)
      .Case("MINS", Suffix == 'B'   ? Bedrock::MINS8rr
                    : Suffix == 'W' ? Bedrock::MINS16rr
                    : Suffix == 'L' ? Bedrock::MINS32rr
                                    : Bedrock::MINS64rr)
      .Case("MINU", Suffix == 'B'   ? Bedrock::MINU8rr
                    : Suffix == 'W' ? Bedrock::MINU16rr
                    : Suffix == 'L' ? Bedrock::MINU32rr
                                    : Bedrock::MINU64rr)
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
      .Case("MAXS", Suffix == 'B'   ? Bedrock::MAXS8ri
                    : Suffix == 'W' ? Bedrock::MAXS16ri
                    : Suffix == 'L' ? Bedrock::MAXS32ri
                                    : Bedrock::MAXS64ri)
      .Case("MAXU", Suffix == 'B'   ? Bedrock::MAXU8ri
                    : Suffix == 'W' ? Bedrock::MAXU16ri
                    : Suffix == 'L' ? Bedrock::MAXU32ri
                                    : Bedrock::MAXU64ri)
      .Case("MINS", Suffix == 'B'   ? Bedrock::MINS8ri
                    : Suffix == 'W' ? Bedrock::MINS16ri
                    : Suffix == 'L' ? Bedrock::MINS32ri
                                    : Bedrock::MINS64ri)
      .Case("MINU", Suffix == 'B'   ? Bedrock::MINU8ri
                    : Suffix == 'W' ? Bedrock::MINU16ri
                    : Suffix == 'L' ? Bedrock::MINU32ri
                                    : Bedrock::MINU64ri)
      .Default(0);
}

static unsigned binRMOpcode(StringRef Mnemonic, char Suffix, bool PostInc) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("ADD", Suffix == 'B'
                       ? (PostInc ? Bedrock::ADD8postrm : Bedrock::ADD8rm)
                   : Suffix == 'W'
                       ? (PostInc ? Bedrock::ADD16postrm : Bedrock::ADD16rm)
                   : Suffix == 'L'
                       ? (PostInc ? Bedrock::ADD32postrm : Bedrock::ADD32rm)
                       : (PostInc ? Bedrock::ADD64postrm : Bedrock::ADD64rm))
      .Case("SUB", Suffix == 'B'
                       ? (PostInc ? Bedrock::SUB8postrm : Bedrock::SUB8rm)
                   : Suffix == 'W'
                       ? (PostInc ? Bedrock::SUB16postrm : Bedrock::SUB16rm)
                   : Suffix == 'L'
                       ? (PostInc ? Bedrock::SUB32postrm : Bedrock::SUB32rm)
                       : (PostInc ? Bedrock::SUB64postrm : Bedrock::SUB64rm))
      .Case("AND", Suffix == 'B'
                       ? (PostInc ? Bedrock::AND8postrm : Bedrock::AND8rm)
                   : Suffix == 'W'
                       ? (PostInc ? Bedrock::AND16postrm : Bedrock::AND16rm)
                   : Suffix == 'L'
                       ? (PostInc ? Bedrock::AND32postrm : Bedrock::AND32rm)
                       : (PostInc ? Bedrock::AND64postrm : Bedrock::AND64rm))
      .Case("OR",
            Suffix == 'B'   ? (PostInc ? Bedrock::OR8postrm : Bedrock::OR8rm)
            : Suffix == 'W' ? (PostInc ? Bedrock::OR16postrm : Bedrock::OR16rm)
            : Suffix == 'L' ? (PostInc ? Bedrock::OR32postrm : Bedrock::OR32rm)
                            : (PostInc ? Bedrock::OR64postrm : Bedrock::OR64rm))
      .Case("XOR", Suffix == 'B'
                       ? (PostInc ? Bedrock::XOR8postrm : Bedrock::XOR8rm)
                   : Suffix == 'W'
                       ? (PostInc ? Bedrock::XOR16postrm : Bedrock::XOR16rm)
                   : Suffix == 'L'
                       ? (PostInc ? Bedrock::XOR32postrm : Bedrock::XOR32rm)
                       : (PostInc ? Bedrock::XOR64postrm : Bedrock::XOR64rm))
      .Case("MULU", Suffix == 'B'   ? Bedrock::MULU8rm
                    : Suffix == 'W' ? Bedrock::MULU16rm
                    : Suffix == 'L' ? Bedrock::MULU32rm
                                    : Bedrock::MULU64rm)
      .Default(0);
}

static unsigned binIndexedRMOpcode(StringRef Mnemonic, const BedrockEA &EA,
                                   char Suffix) {
  if (Suffix != 'L')
    return 0;
  if (!EA.Signed32Index && EA.Scale == 1) {
    return StringSwitch<unsigned>(Mnemonic)
        .Case("ADD", Bedrock::ADD32idx1rm)
        .Case("SUB", Bedrock::SUB32idx1rm)
        .Case("AND", Bedrock::AND32idx1rm)
        .Case("OR", Bedrock::OR32idx1rm)
        .Case("XOR", Bedrock::XOR32idx1rm)
        .Case("MULU", Bedrock::MULU32idx1rm)
        .Default(0);
  }
  if (!EA.Signed32Index && EA.Scale == 4) {
    return StringSwitch<unsigned>(Mnemonic)
        .Case("ADD", Bedrock::ADD32idx4rm)
        .Case("SUB", Bedrock::SUB32idx4rm)
        .Case("AND", Bedrock::AND32idx4rm)
        .Case("OR", Bedrock::OR32idx4rm)
        .Case("XOR", Bedrock::XOR32idx4rm)
        .Case("MULU", Bedrock::MULU32idx4rm)
        .Default(0);
  }
  if (EA.Signed32Index && EA.Scale == 4) {
    return StringSwitch<unsigned>(Mnemonic)
        .Case("ADD", Bedrock::ADD32idx4lrm)
        .Case("SUB", Bedrock::SUB32idx4lrm)
        .Case("AND", Bedrock::AND32idx4lrm)
        .Case("OR", Bedrock::OR32idx4lrm)
        .Case("XOR", Bedrock::XOR32idx4lrm)
        .Case("MULU", Bedrock::MULU32idx4lrm)
        .Default(0);
  }
  return 0;
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
  default:
    return 0;
  }
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
  default:
    return 0;
  }
}

static unsigned cmpRMOpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::CMP8rm;
  case 'W':
    return Bedrock::CMP16rm;
  case 'L':
    return Bedrock::CMP32rm;
  case 'Q':
    return Bedrock::CMP64rm;
  default:
    return 0;
  }
}

static unsigned cmpMROpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::CMP8mr;
  case 'W':
    return Bedrock::CMP16mr;
  case 'L':
    return Bedrock::CMP32mr;
  case 'Q':
    return Bedrock::CMP64mr;
  default:
    return 0;
  }
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
  default:
    return 0;
  }
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
  default:
    return 0;
  }
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
  default:
    return 0;
  }
}

static unsigned testRMOpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::TEST8rm;
  case 'W':
    return Bedrock::TEST16rm;
  case 'L':
    return Bedrock::TEST32rm;
  case 'Q':
    return Bedrock::TEST64rm;
  default:
    return 0;
  }
}

static unsigned testMROpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::TEST8mr;
  case 'W':
    return Bedrock::TEST16mr;
  case 'L':
    return Bedrock::TEST32mr;
  case 'Q':
    return Bedrock::TEST64mr;
  default:
    return 0;
  }
}

static unsigned flagIndexedRMOpcode(StringRef Mnemonic, const BedrockEA &EA,
                                    char Suffix) {
  if (!EA.Signed32Index && EA.Scale == 1)
    return Mnemonic == "CMP"    ? (Suffix == 'B'   ? Bedrock::CMP8idx1rm
                                   : Suffix == 'W' ? Bedrock::CMP16idx1rm
                                   : Suffix == 'L' ? Bedrock::CMP32idx1rm
                                                   : Bedrock::CMP64idx1rm)
           : Mnemonic == "TEST" ? (Suffix == 'B'   ? Bedrock::TEST8idx1rm
                                   : Suffix == 'W' ? Bedrock::TEST16idx1rm
                                   : Suffix == 'L' ? Bedrock::TEST32idx1rm
                                                   : Bedrock::TEST64idx1rm)
                                : 0;
  if (!EA.Signed32Index && EA.Scale == 4)
    return Mnemonic == "CMP"    ? (Suffix == 'B'   ? Bedrock::CMP8idx4rm
                                   : Suffix == 'W' ? Bedrock::CMP16idx4rm
                                   : Suffix == 'L' ? Bedrock::CMP32idx4rm
                                                   : Bedrock::CMP64idx4rm)
           : Mnemonic == "TEST" ? (Suffix == 'B'   ? Bedrock::TEST8idx4rm
                                   : Suffix == 'W' ? Bedrock::TEST16idx4rm
                                   : Suffix == 'L' ? Bedrock::TEST32idx4rm
                                                   : Bedrock::TEST64idx4rm)
                                : 0;
  if (EA.Signed32Index && EA.Scale == 4)
    return Mnemonic == "CMP"    ? (Suffix == 'B'   ? Bedrock::CMP8idx4lrm
                                   : Suffix == 'W' ? Bedrock::CMP16idx4lrm
                                   : Suffix == 'L' ? Bedrock::CMP32idx4lrm
                                                   : Bedrock::CMP64idx4lrm)
           : Mnemonic == "TEST" ? (Suffix == 'B'   ? Bedrock::TEST8idx4lrm
                                   : Suffix == 'W' ? Bedrock::TEST16idx4lrm
                                   : Suffix == 'L' ? Bedrock::TEST32idx4lrm
                                                   : Bedrock::TEST64idx4lrm)
                                : 0;
  return 0;
}

static unsigned flagIndexedMROpcode(StringRef Mnemonic, const BedrockEA &EA,
                                    char Suffix) {
  if (!EA.Signed32Index && EA.Scale == 1)
    return Mnemonic == "CMP"    ? (Suffix == 'B'   ? Bedrock::CMP8idx1mr
                                   : Suffix == 'W' ? Bedrock::CMP16idx1mr
                                   : Suffix == 'L' ? Bedrock::CMP32idx1mr
                                                   : Bedrock::CMP64idx1mr)
           : Mnemonic == "TEST" ? (Suffix == 'B'   ? Bedrock::TEST8idx1mr
                                   : Suffix == 'W' ? Bedrock::TEST16idx1mr
                                   : Suffix == 'L' ? Bedrock::TEST32idx1mr
                                                   : Bedrock::TEST64idx1mr)
                                : 0;
  if (!EA.Signed32Index && EA.Scale == 4)
    return Mnemonic == "CMP"    ? (Suffix == 'B'   ? Bedrock::CMP8idx4mr
                                   : Suffix == 'W' ? Bedrock::CMP16idx4mr
                                   : Suffix == 'L' ? Bedrock::CMP32idx4mr
                                                   : Bedrock::CMP64idx4mr)
           : Mnemonic == "TEST" ? (Suffix == 'B'   ? Bedrock::TEST8idx4mr
                                   : Suffix == 'W' ? Bedrock::TEST16idx4mr
                                   : Suffix == 'L' ? Bedrock::TEST32idx4mr
                                                   : Bedrock::TEST64idx4mr)
                                : 0;
  if (EA.Signed32Index && EA.Scale == 4)
    return Mnemonic == "CMP"    ? (Suffix == 'B'   ? Bedrock::CMP8idx4lmr
                                   : Suffix == 'W' ? Bedrock::CMP16idx4lmr
                                   : Suffix == 'L' ? Bedrock::CMP32idx4lmr
                                                   : Bedrock::CMP64idx4lmr)
           : Mnemonic == "TEST" ? (Suffix == 'B'   ? Bedrock::TEST8idx4lmr
                                   : Suffix == 'W' ? Bedrock::TEST16idx4lmr
                                   : Suffix == 'L' ? Bedrock::TEST32idx4lmr
                                                   : Bedrock::TEST64idx4lmr)
                                : 0;
  return 0;
}

static unsigned flagIndexedMIOpcode(StringRef Mnemonic, const BedrockEA &EA,
                                    char Suffix) {
  if (!EA.Signed32Index && EA.Scale == 1)
    return Mnemonic == "CMP"    ? (Suffix == 'B'   ? Bedrock::CMP8idx1mi
                                   : Suffix == 'W' ? Bedrock::CMP16idx1mi
                                   : Suffix == 'L' ? Bedrock::CMP32idx1mi
                                                   : Bedrock::CMP64idx1mi)
           : Mnemonic == "TEST" ? (Suffix == 'B'   ? Bedrock::TEST8idx1mi
                                   : Suffix == 'W' ? Bedrock::TEST16idx1mi
                                   : Suffix == 'L' ? Bedrock::TEST32idx1mi
                                                   : Bedrock::TEST64idx1mi)
                                : 0;
  if (!EA.Signed32Index && EA.Scale == 4)
    return Mnemonic == "CMP"    ? (Suffix == 'B'   ? Bedrock::CMP8idx4mi
                                   : Suffix == 'W' ? Bedrock::CMP16idx4mi
                                   : Suffix == 'L' ? Bedrock::CMP32idx4mi
                                                   : Bedrock::CMP64idx4mi)
           : Mnemonic == "TEST" ? (Suffix == 'B'   ? Bedrock::TEST8idx4mi
                                   : Suffix == 'W' ? Bedrock::TEST16idx4mi
                                   : Suffix == 'L' ? Bedrock::TEST32idx4mi
                                                   : Bedrock::TEST64idx4mi)
                                : 0;
  if (EA.Signed32Index && EA.Scale == 4)
    return Mnemonic == "CMP"    ? (Suffix == 'B'   ? Bedrock::CMP8idx4lmi
                                   : Suffix == 'W' ? Bedrock::CMP16idx4lmi
                                   : Suffix == 'L' ? Bedrock::CMP32idx4lmi
                                                   : Bedrock::CMP64idx4lmi)
           : Mnemonic == "TEST" ? (Suffix == 'B'   ? Bedrock::TEST8idx4lmi
                                   : Suffix == 'W' ? Bedrock::TEST16idx4lmi
                                   : Suffix == 'L' ? Bedrock::TEST32idx4lmi
                                                   : Bedrock::TEST64idx4lmi)
                                : 0;
  return 0;
}

static unsigned bitRIOpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("BCHG", Suffix == 'B'   ? Bedrock::BCHG8ri
                    : Suffix == 'W' ? Bedrock::BCHG16ri
                    : Suffix == 'L' ? Bedrock::BCHG32ri
                                    : Bedrock::BCHG64ri)
      .Case("BCLR", Suffix == 'B'   ? Bedrock::BCLR8ri
                    : Suffix == 'W' ? Bedrock::BCLR16ri
                    : Suffix == 'L' ? Bedrock::BCLR32ri
                                    : Bedrock::BCLR64ri)
      .Case("BSET", Suffix == 'B'   ? Bedrock::BSET8ri
                    : Suffix == 'W' ? Bedrock::BSET16ri
                    : Suffix == 'L' ? Bedrock::BSET32ri
                                    : Bedrock::BSET64ri)
      .Case("BTEST", Suffix == 'B'   ? Bedrock::BTEST8ri
                     : Suffix == 'W' ? Bedrock::BTEST16ri
                     : Suffix == 'L' ? Bedrock::BTEST32ri
                                     : Bedrock::BTEST64ri)
      .Default(0);
}

static unsigned bitMIOpcode(StringRef Mnemonic, char Suffix) {
  return StringSwitch<unsigned>(Mnemonic)
      .Case("BCHG", Suffix == 'B'   ? Bedrock::BCHG8mi
                    : Suffix == 'W' ? Bedrock::BCHG16mi
                    : Suffix == 'L' ? Bedrock::BCHG32mi
                                    : Bedrock::BCHG64mi)
      .Case("BCLR", Suffix == 'B'   ? Bedrock::BCLR8mi
                    : Suffix == 'W' ? Bedrock::BCLR16mi
                    : Suffix == 'L' ? Bedrock::BCLR32mi
                                    : Bedrock::BCLR64mi)
      .Case("BSET", Suffix == 'B'   ? Bedrock::BSET8mi
                    : Suffix == 'W' ? Bedrock::BSET16mi
                    : Suffix == 'L' ? Bedrock::BSET32mi
                                    : Bedrock::BSET64mi)
      .Default(0);
}

static unsigned maddRRROpcode(char Suffix) {
  switch (Suffix) {
  case 'B':
    return Bedrock::MADD8rrr;
  case 'W':
    return Bedrock::MADD16rrr;
  case 'L':
    return Bedrock::MADD32rrr;
  case 'Q':
    return Bedrock::MADD64rrr;
  default:
    return 0;
  }
}

static unsigned maddMRROpcode(char Suffix, bool PostInc) {
  switch (Suffix) {
  case 'B':
    return PostInc ? Bedrock::MADD8postmrr : Bedrock::MADD8mrr;
  case 'W':
    return PostInc ? Bedrock::MADD16postmrr : Bedrock::MADD16mrr;
  case 'L':
    return PostInc ? Bedrock::MADD32postmrr : Bedrock::MADD32mrr;
  case 'Q':
    return PostInc ? Bedrock::MADD64postmrr : Bedrock::MADD64mrr;
  default:
    return 0;
  }
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
  default:
    return 0;
  }
}

static unsigned shiftRIOpcode(StringRef Mnemonic, char Suffix) {
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

static unsigned fmovRROpcode(char Suffix) {
  return Suffix == 'S'   ? Bedrock::FMOV32rr
         : Suffix == 'D' ? Bedrock::FMOV64rr
                         : 0;
}

static unsigned fmovRMOpcode(char Suffix) {
  return Suffix == 'S'   ? Bedrock::FMOV32rm
         : Suffix == 'D' ? Bedrock::FMOV64rm
                         : 0;
}

static unsigned fmovMROpcode(char Suffix) {
  return Suffix == 'S'   ? Bedrock::FMOV32mr
         : Suffix == 'D' ? Bedrock::FMOV64mr
                         : 0;
}

static unsigned fmovccRROpcode(char Suffix) {
  return Suffix == 'S'   ? Bedrock::FMOVCC32rr
         : Suffix == 'D' ? Bedrock::FMOVCC64rr
                         : 0;
}

static unsigned fmovccRMOpcode(char Suffix) {
  return Suffix == 'S'   ? Bedrock::FMOVCC32rm
         : Suffix == 'D' ? Bedrock::FMOVCC64rm
                         : 0;
}

static unsigned fmovccMROpcode(char Suffix) {
  return Suffix == 'S'   ? Bedrock::FMOVCC32mr
         : Suffix == 'D' ? Bedrock::FMOVCC64mr
                         : 0;
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

static unsigned fcmpRMOpcode(StringRef Mnemonic, char Suffix) {
  if (Mnemonic != "FCMP")
    return 0;
  return Suffix == 'S' ? Bedrock::FCMP32rm : Bedrock::FCMP64rm;
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
  default:
    return 0;
  }
}

static bool setOpcode(MCInst &MI, unsigned Opcode) {
  if (!Opcode)
    return false;
  MI.setOpcode(Opcode);
  return true;
}

static unsigned noOperandOpcode(StringRef ID) {
  return StringSwitch<unsigned>(ID)
      .Case("AFENCE", Bedrock::AFENCE)
      .Case("HALT", Bedrock::HALT)
      .Case("NOP", Bedrock::NOP)
      .Case("RET", Bedrock::RET)
      .Default(0);
}

static DecodeStatus decodeMove(const bedrock_form_desc *Form,
                               const uint16_t *Words, size_t WordCount,
                               MCInst &MI) {
  StringRef ID(Form->id);
  std::optional<char> Suffix = sizeSuffix(Form, Words);

  if (ID == "MOV.IMM_TO_A") {
    size_t Cursor = payloadStartWord(Form, Words);
    if (Cursor + 4 > WordCount)
      return MCDisassembler::Fail;
    MI.setOpcode(Bedrock::MOV64ri);
    if (!addReg(MI, regBySource(Form, Words, "dst")))
      return MCDisassembler::Fail;
    MI.addOperand(MCOperand::createImm(readPayload(Words, Cursor, 4)));
    return MCDisassembler::Success;
  }

  if (!Suffix)
    return MCDisassembler::Fail;

  size_t Cursor = payloadStartWord(Form, Words);
  if (ID.starts_with("MOV.EA_TO_D")) {
    PrefixState Prefixes = getPrefixState(Words, WordCount);
    std::optional<BedrockEA> Src =
        decodeEABySource(Form, Words, WordCount, "src", Cursor, &Prefixes);
    std::optional<MCRegister> Dst = regBySource(Form, Words, "dst");
    if (!Src || !Dst)
      return MCDisassembler::Fail;
    if (Src->Kind == BedrockEA::Immediate) {
      if (!setOpcode(MI, movRIOpcode(*Suffix)) || !addReg(MI, Dst))
        return MCDisassembler::Fail;
      MI.addOperand(MCOperand::createImm(Src->Imm));
      return MCDisassembler::Success;
    }
    if (Src->Kind == BedrockEA::Register) {
      if (!setOpcode(MI, movRROpcode(*Suffix)) || !addReg(MI, Dst) ||
          !addReg(MI, Src->Reg))
        return MCDisassembler::Fail;
      return MCDisassembler::Success;
    }
    if (Src->Kind == BedrockEA::Memory && isIndexedEA(*Src)) {
      unsigned Opc = movIndexedRMOpcode(*Suffix, *Src);
      if (!setOpcode(MI, Opc) || !addReg(MI, Dst) || !addIndexedMem(MI, *Src))
        return MCDisassembler::Fail;
      return MCDisassembler::Success;
    }
    if (hasUpdate(*Src) && !isPostInc(*Src))
      return MCDisassembler::Fail;
    if (!setOpcode(MI, isPostInc(*Src) ? movPostRMOpcode(*Suffix)
                                       : movRMOpcode(*Suffix)) ||
        !addReg(MI, Dst))
      return MCDisassembler::Fail;
    addMaybePostMem(MI, *Src);
    return MCDisassembler::Success;
  }

  if (ID.starts_with("MOV.D_TO_EA")) {
    PrefixState Prefixes = getPrefixState(Words, WordCount);
    std::optional<MCRegister> Src = regBySource(Form, Words, "src");
    std::optional<BedrockEA> Dst =
        decodeEABySource(Form, Words, WordCount, "dst", Cursor, &Prefixes);
    if (!Src || !Dst)
      return MCDisassembler::Fail;
    if (Dst->Kind == BedrockEA::Register) {
      if (!setOpcode(MI, movRROpcode(*Suffix)) || !addReg(MI, Dst->Reg) ||
          !addReg(MI, Src))
        return MCDisassembler::Fail;
      return MCDisassembler::Success;
    }
    if (Dst->Kind != BedrockEA::Memory)
      return MCDisassembler::Fail;
    if (isIndexedEA(*Dst)) {
      unsigned Opc = movIndexedMROpcode(*Suffix, *Dst);
      if (!setOpcode(MI, Opc) || !addReg(MI, Src) || !addIndexedMem(MI, *Dst))
        return MCDisassembler::Fail;
      return MCDisassembler::Success;
    }
    if (hasUpdate(*Dst)) {
      if (std::optional<MCRegister> Counter =
              repeatCounterReg(Words, WordCount)) {
        if (!setOpcode(MI, repMovPostMROpcode(*Suffix)) ||
            !addReg(MI, *Counter) || !addReg(MI, *Counter) || !addReg(MI, Src))
          return MCDisassembler::Fail;
        addUpdateMem(MI, *Dst);
        return MCDisassembler::Success;
      } else if (!isPostInc(*Dst)) {
        return MCDisassembler::Fail;
      } else if (!setOpcode(MI, movPostMROpcode(*Suffix)) || !addReg(MI, Src)) {
        return MCDisassembler::Fail;
      }
    } else if (!setOpcode(MI, movMROpcode(*Suffix)) || !addReg(MI, Src)) {
      return MCDisassembler::Fail;
    }
    addMaybePostMem(MI, *Dst);
    return MCDisassembler::Success;
  }

  if (ID == "MOV.EA_TO_EA") {
    size_t Cursor = payloadStartWord(Form, Words);
    PrefixState Prefixes = getPrefixState(Words, WordCount);
    std::optional<BedrockEA> Src =
        decodeEABySource(Form, Words, WordCount, "src", Cursor, &Prefixes);
    std::optional<BedrockEA> Dst =
        decodeEABySource(Form, Words, WordCount, "dst", Cursor, &Prefixes);
    if (!Src || !Dst)
      return MCDisassembler::Fail;

    if (Src->Kind == BedrockEA::Immediate && Dst->Kind == BedrockEA::Register) {
      if (!setOpcode(MI, movRIOpcode(*Suffix)) || !addReg(MI, Dst->Reg))
        return MCDisassembler::Fail;
      MI.addOperand(MCOperand::createImm(Src->Imm));
      return MCDisassembler::Success;
    }
    if (Src->Kind == BedrockEA::Immediate && Dst->Kind == BedrockEA::Memory) {
      if (isIndexedEA(*Dst) || hasUpdate(*Dst) ||
          !setOpcode(MI, movMIOpcode(*Suffix)))
        return MCDisassembler::Fail;
      MI.addOperand(MCOperand::createImm(Src->Imm));
      addMaybePostMem(MI, *Dst);
      return MCDisassembler::Success;
    }
    if (Src->Kind == BedrockEA::Register && Dst->Kind == BedrockEA::Register) {
      if (!setOpcode(MI, movRROpcode(*Suffix)) || !addReg(MI, Dst->Reg) ||
          !addReg(MI, Src->Reg))
        return MCDisassembler::Fail;
      return MCDisassembler::Success;
    }
    if (Src->Kind == BedrockEA::Memory && Dst->Kind == BedrockEA::Register) {
      if (hasUpdate(*Src) && !isPostInc(*Src))
        return MCDisassembler::Fail;
      if (!setOpcode(MI, isPostInc(*Src) ? movPostRMOpcode(*Suffix)
                                         : movRMOpcode(*Suffix)) ||
          !addReg(MI, Dst->Reg))
        return MCDisassembler::Fail;
      addMaybePostMem(MI, *Src);
      return MCDisassembler::Success;
    }
    if (Src->Kind == BedrockEA::Register && Dst->Kind == BedrockEA::Memory) {
      if (isIndexedEA(*Dst)) {
        if (*Suffix == 'L' && !Dst->Signed32Index && Dst->Scale == 1 &&
            setOpcode(MI, Bedrock::MOV32idx1mr) && addReg(MI, Src->Reg) &&
            addIndexedMem(MI, *Dst))
          return MCDisassembler::Success;
        if (*Suffix == 'L' && !Dst->Signed32Index && Dst->Scale == 4 &&
            setOpcode(MI, Bedrock::MOV32idx4mr) && addReg(MI, Src->Reg) &&
            addIndexedMem(MI, *Dst))
          return MCDisassembler::Success;
        if (*Suffix == 'L' && Dst->Signed32Index && Dst->Scale == 4 &&
            setOpcode(MI, Bedrock::MOV32idx4lmr) && addReg(MI, Src->Reg) &&
            addIndexedMem(MI, *Dst))
          return MCDisassembler::Success;
        return MCDisassembler::Fail;
      }
      if (hasUpdate(*Dst)) {
        if (std::optional<MCRegister> Counter =
                repeatCounterReg(Words, WordCount)) {
          if (!setOpcode(MI, repMovPostMROpcode(*Suffix)) ||
              !addReg(MI, *Counter) || !addReg(MI, *Counter) ||
              !addReg(MI, Src->Reg))
            return MCDisassembler::Fail;
          addUpdateMem(MI, *Dst);
          return MCDisassembler::Success;
        } else if (!isPostInc(*Dst)) {
          return MCDisassembler::Fail;
        } else if (!setOpcode(MI, movPostMROpcode(*Suffix)) ||
                   !addReg(MI, Src->Reg)) {
          return MCDisassembler::Fail;
        }
      } else if (!setOpcode(MI, movMROpcode(*Suffix)) ||
                 !addReg(MI, Src->Reg)) {
        return MCDisassembler::Fail;
      }
      addMaybePostMem(MI, *Dst);
      return MCDisassembler::Success;
    }
    if (Src->Kind == BedrockEA::Memory && Dst->Kind == BedrockEA::Memory) {
      if (isIndexedEA(*Src)) {
        if (isIndexedEA(*Dst) || hasUpdate(*Dst))
          return MCDisassembler::Fail;
        if (*Suffix == 'L' && !Src->Signed32Index && Src->Scale == 1 &&
            setOpcode(MI, Bedrock::MOV32idx1mm) && addIndexedMem(MI, *Src)) {
          addMaybePostMem(MI, *Dst);
          return MCDisassembler::Success;
        }
        if (*Suffix == 'L' && !Src->Signed32Index && Src->Scale == 4 &&
            setOpcode(MI, Bedrock::MOV32idx4mm) && addIndexedMem(MI, *Src)) {
          addMaybePostMem(MI, *Dst);
          return MCDisassembler::Success;
        }
        if (*Suffix == 'L' && Src->Signed32Index && Src->Scale == 4 &&
            setOpcode(MI, Bedrock::MOV32idx4lmm) && addIndexedMem(MI, *Src)) {
          addMaybePostMem(MI, *Dst);
          return MCDisassembler::Success;
        }
        return MCDisassembler::Fail;
      }
      if (isIndexedEA(*Dst)) {
        if (hasUpdate(*Src))
          return MCDisassembler::Fail;
        if (*Suffix == 'L' && !Dst->Signed32Index && Dst->Scale == 1 &&
            setOpcode(MI, Bedrock::MOV32midx1)) {
          addMaybePostMem(MI, *Src);
          if (!addIndexedMem(MI, *Dst))
            return MCDisassembler::Fail;
          return MCDisassembler::Success;
        }
        if (*Suffix == 'L' && !Dst->Signed32Index && Dst->Scale == 4 &&
            setOpcode(MI, Bedrock::MOV32midx4)) {
          addMaybePostMem(MI, *Src);
          if (!addIndexedMem(MI, *Dst))
            return MCDisassembler::Fail;
          return MCDisassembler::Success;
        }
        if (*Suffix == 'L' && Dst->Signed32Index && Dst->Scale == 4 &&
            setOpcode(MI, Bedrock::MOV32midx4l)) {
          addMaybePostMem(MI, *Src);
          if (!addIndexedMem(MI, *Dst))
            return MCDisassembler::Fail;
          return MCDisassembler::Success;
        }
        return MCDisassembler::Fail;
      }
      if (hasUpdate(*Src) || hasUpdate(*Dst)) {
        if (!hasUpdate(*Src) || !hasUpdate(*Dst) || Src->Update != Dst->Update)
          return MCDisassembler::Fail;
        if (std::optional<MCRegister> Counter =
                repeatCounterReg(Words, WordCount)) {
          if (!setOpcode(MI, repMovMMOpcode(*Suffix)) ||
              !addReg(MI, *Counter) || !addReg(MI, *Counter))
            return MCDisassembler::Fail;
          addUpdateMem(MI, *Src);
          addUpdateMem(MI, *Dst);
          return MCDisassembler::Success;
        }
        if (!isPostInc(*Src))
          return MCDisassembler::Fail;
      }
      if (!setOpcode(MI,
                     movMMOpcode(*Suffix, isPostInc(*Src), isPostInc(*Dst))))
        return MCDisassembler::Fail;
      addMaybePostMem(MI, *Src);
      addMaybePostMem(MI, *Dst);
      return MCDisassembler::Success;
    }
  }

  return MCDisassembler::Fail;
}

static DecodeStatus decodeMovcc(const bedrock_form_desc *Form,
                                const uint16_t *Words, size_t WordCount,
                                MCInst &MI) {
  StringRef ID(Form->id);
  std::optional<char> Suffix = sizeSuffix(Form, Words);
  std::optional<uint64_t> CC = fieldValueBySource(Form, Words, "cc");
  if (!Suffix || !CC)
    return MCDisassembler::Fail;

  auto EmitRR = [&](MCRegister Dst, MCRegister Src) -> DecodeStatus {
    if (Dst == Bedrock::NoRegister || Src == Bedrock::NoRegister ||
        !setOpcode(MI, movccRROpcode(*Suffix)) || !addReg(MI, Dst) ||
        !addReg(MI, Dst) || !addReg(MI, Src))
      return MCDisassembler::Fail;
    MI.addOperand(MCOperand::createImm(*CC));
    return MCDisassembler::Success;
  };

  size_t Cursor = payloadStartWord(Form, Words);
  if (ID.starts_with("MOVcc.D_TO_EA") || ID.starts_with("MOVcc.A_TO_EA")) {
    std::optional<MCRegister> Src = regBySource(Form, Words, "src");
    std::optional<BedrockEA> Dst =
        decodeEABySource(Form, Words, WordCount, "dst", Cursor);
    if (Src && Dst && Dst->Kind == BedrockEA::Register)
      return EmitRR(Dst->Reg, *Src);
    if (!Src || !Dst || Dst->Kind != BedrockEA::Memory || hasUpdate(*Dst) ||
        isIndexedEA(*Dst) || !setOpcode(MI, movccMROpcode(*Suffix)) ||
        !addReg(MI, *Src))
      return MCDisassembler::Fail;
    addMem(MI, Dst->Reg, Dst->Imm);
    MI.addOperand(MCOperand::createImm(*CC));
    return MCDisassembler::Success;
  }

  if (ID.starts_with("MOVcc.EA_TO_D") || ID.starts_with("MOVcc.EA_TO_A")) {
    std::optional<BedrockEA> Src =
        decodeEABySource(Form, Words, WordCount, "src", Cursor);
    std::optional<MCRegister> Dst = regBySource(Form, Words, "dst");
    if (Src && Src->Kind == BedrockEA::Register && Dst)
      return EmitRR(*Dst, Src->Reg);
    if (!Src || Src->Kind != BedrockEA::Memory || hasUpdate(*Src) ||
        isIndexedEA(*Src) || !Dst || !setOpcode(MI, movccRMOpcode(*Suffix)) ||
        !addReg(MI, *Dst) || !addReg(MI, *Dst))
      return MCDisassembler::Fail;
    addMem(MI, Src->Reg, Src->Imm);
    MI.addOperand(MCOperand::createImm(*CC));
    return MCDisassembler::Success;
  }

  return MCDisassembler::Fail;
}

static bool isIntBinMnemonic(StringRef Mnemonic) {
  return Mnemonic == "ADC" || Mnemonic == "ADD" || Mnemonic == "SBB" ||
         Mnemonic == "SUB" || Mnemonic == "AND" || Mnemonic == "OR" ||
         Mnemonic == "TEST" || Mnemonic == "XOR" || Mnemonic == "MULU" ||
         Mnemonic == "MULHS" || Mnemonic == "MULHU" || Mnemonic == "DIVS" ||
         Mnemonic == "DIVU" || Mnemonic == "MODS" || Mnemonic == "MODU" ||
         Mnemonic == "MAXS" || Mnemonic == "MAXU" || Mnemonic == "MINS" ||
         Mnemonic == "MINU";
}

static DecodeStatus decodeIntBinOrCmp(const bedrock_form_desc *Form,
                                      const uint16_t *Words, size_t WordCount,
                                      MCInst &MI) {
  StringRef ID(Form->id);
  StringRef Mnemonic(Form->mnemonic);

  if (ID == "ADD.EA_TO_A" || ID == "SUB.EA_TO_A") {
    size_t Cursor = payloadStartWord(Form, Words);
    std::optional<BedrockEA> Src =
        decodeEABySource(Form, Words, WordCount, "src", Cursor);
    std::optional<MCRegister> Dst = regBySource(Form, Words, "dst");
    if (!Src || !Dst)
      return MCDisassembler::Fail;

    if (Src->Kind == BedrockEA::Immediate) {
      if (!setOpcode(MI, binRIOpcode(Mnemonic, 'Q')) || !addReg(MI, Dst) ||
          !addReg(MI, Dst))
        return MCDisassembler::Fail;
      MI.addOperand(MCOperand::createImm(Src->Imm));
      return MCDisassembler::Success;
    }
    if (Src->Kind == BedrockEA::Register) {
      if (!setOpcode(MI, binRROpcode(Mnemonic, 'Q')) || !addReg(MI, Dst) ||
          !addReg(MI, Dst) || !addReg(MI, Src->Reg))
        return MCDisassembler::Fail;
      return MCDisassembler::Success;
    }
    return MCDisassembler::Fail;
  }

  if (ID == "CMP.EA_TO_A") {
    size_t Cursor = payloadStartWord(Form, Words);
    std::optional<BedrockEA> LHS =
        decodeEABySource(Form, Words, WordCount, "lhs", Cursor);
    std::optional<MCRegister> RHS = regBySource(Form, Words, "rhs");
    if (!LHS || LHS->Kind != BedrockEA::Register ||
        !setOpcode(MI, Bedrock::CMP64rr) || !addReg(MI, RHS) ||
        !addReg(MI, LHS->Reg))
      return MCDisassembler::Fail;
    return MCDisassembler::Success;
  }

  std::optional<char> Suffix = sizeSuffix(Form, Words);
  if (!Suffix)
    return MCDisassembler::Fail;

  if (ID.ends_with("IMM_TO_D") && !findFieldBySource(Form, "imm")) {
    size_t Cursor = payloadStartWord(Form, Words);
    unsigned Bits = bitsForSizeSuffix(*Suffix);
    size_t PayloadWords = wordsForBits(Bits);
    if (Bits == 0 || PayloadWords == 0 || Cursor + PayloadWords > WordCount)
      return MCDisassembler::Fail;
    StringRef TargetSource =
        (Mnemonic == "CMP" || Mnemonic == "TEST") ? "rhs" : "dst";
    std::optional<MCRegister> Dst = regBySource(Form, Words, TargetSource);
    unsigned Opc = Mnemonic == "CMP"    ? cmpRIOpcode(*Suffix)
                   : Mnemonic == "TEST" ? testRIOpcode(*Suffix)
                                         : binRIOpcode(Mnemonic, *Suffix);
    if (!setOpcode(MI, Opc) || !addReg(MI, Dst))
      return MCDisassembler::Fail;
    if (Mnemonic != "CMP" && Mnemonic != "TEST" && !addReg(MI, Dst))
      return MCDisassembler::Fail;
    MI.addOperand(MCOperand::createImm(
        signExtend(readPayload(Words, Cursor, PayloadWords), Bits)));
    return MCDisassembler::Success;
  }

  auto AddRR = [&](MCRegister Dst, MCRegister Src) -> DecodeStatus {
    unsigned Opc = Mnemonic == "CMP"    ? cmpOpcode(*Suffix)
                   : Mnemonic == "TEST" ? testRROpcode(*Suffix)
                                        : binRROpcode(Mnemonic, *Suffix);
    if (!setOpcode(MI, Opc) || !addReg(MI, Dst))
      return MCDisassembler::Fail;
    if (Mnemonic != "CMP" && Mnemonic != "TEST" && !addReg(MI, Dst))
      return MCDisassembler::Fail;
    if (!addReg(MI, Src))
      return MCDisassembler::Fail;
    return MCDisassembler::Success;
  };

  if (ID.contains("IMM_TO")) {
    size_t Cursor = payloadStartWord(Form, Words);
    std::optional<int64_t> ImmValue;
    if (const bedrock_field_desc *ImmField = findFieldBySource(Form, "imm")) {
      if (StringRef(ImmField->kind).starts_with("IMM") &&
          ImmField->width == 6) {
        uint64_t EncodedImm = extractField(Words, ImmField);
        unsigned PayloadBits = immediateEAPayloadBits(EncodedImm);
        size_t PayloadWords = wordsForBits(PayloadBits);
        if (PayloadBits != 0) {
          if (Cursor + PayloadWords > WordCount)
            return MCDisassembler::Fail;
          ImmValue =
              signExtend(readPayload(Words, Cursor, PayloadWords), PayloadBits);
          Cursor += PayloadWords;
        } else {
          ImmValue = static_cast<int64_t>(EncodedImm);
        }
      }
    }
    if (!ImmValue) {
      std::optional<BedrockEA> Imm =
          decodeEABySource(Form, Words, WordCount, "imm", Cursor);
      if (!Imm || Imm->Kind != BedrockEA::Immediate)
        return MCDisassembler::Fail;
      ImmValue = Imm->Imm;
    }

    MCRegister Dst = Bedrock::NoRegister;
    std::optional<BedrockEA> EADst;
    StringRef TargetSource =
        (Mnemonic == "CMP" || Mnemonic == "TEST") ? "rhs" : "dst";
    if (std::optional<MCRegister> DirectDst =
            regBySource(Form, Words, TargetSource))
      Dst = *DirectDst;
    else
      EADst = decodeEABySource(Form, Words, WordCount, TargetSource, Cursor);
    if (EADst && EADst->Kind == BedrockEA::Register)
      Dst = EADst->Reg;

    if (Dst != Bedrock::NoRegister) {
      unsigned Opc = Mnemonic == "CMP"    ? cmpRIOpcode(*Suffix)
                     : Mnemonic == "TEST" ? testRIOpcode(*Suffix)
                                          : binRIOpcode(Mnemonic, *Suffix);
      if (!setOpcode(MI, Opc) || !addReg(MI, Dst))
        return MCDisassembler::Fail;
      if (Mnemonic != "CMP" && Mnemonic != "TEST" && !addReg(MI, Dst))
        return MCDisassembler::Fail;
      MI.addOperand(MCOperand::createImm(*ImmValue));
      return MCDisassembler::Success;
    }

    if (EADst && EADst->Kind == BedrockEA::Memory) {
      unsigned Opc = Mnemonic == "CMP"    ? cmpMIOpcode(*Suffix)
                     : Mnemonic == "TEST" ? testMIOpcode(*Suffix)
                                          : binMIOpcode(Mnemonic, *Suffix);
      if (!setOpcode(MI, Opc))
        return MCDisassembler::Fail;
      MI.addOperand(MCOperand::createImm(*ImmValue));
      addMaybePostMem(MI, *EADst);
      return MCDisassembler::Success;
    }
    return MCDisassembler::Fail;
  }

  if (ID.contains("D_TO_D")) {
    std::optional<MCRegister> Src = regBySource(Form, Words, "src");
    std::optional<MCRegister> Dst = regBySource(Form, Words, "dst");
    if (!Src || !Dst)
      return MCDisassembler::Fail;
    return AddRR(*Dst, *Src);
  }

  if (ID.contains("EA_TO_D")) {
    size_t Cursor = payloadStartWord(Form, Words);
    PrefixState Prefixes = getPrefixState(Words, WordCount);
    bool IsFlagBin = Mnemonic == "CMP" || Mnemonic == "TEST";
    std::optional<BedrockEA> Src = decodeEABySource(
        Form, Words, WordCount, IsFlagBin ? "lhs" : "src", Cursor, &Prefixes);
    std::optional<MCRegister> Dst =
        regBySource(Form, Words, IsFlagBin ? "rhs" : "dst");
    if (!Src || !Dst)
      return MCDisassembler::Fail;
    if (Src->Kind == BedrockEA::Immediate) {
      if (!setOpcode(MI, binRIOpcode(Mnemonic, *Suffix)) || !addReg(MI, Dst) ||
          !addReg(MI, Dst))
        return MCDisassembler::Fail;
      MI.addOperand(MCOperand::createImm(Src->Imm));
      return MCDisassembler::Success;
    }
    if (Src->Kind == BedrockEA::Memory && !IsFlagBin) {
      if (isIndexedEA(*Src)) {
        unsigned Opc = binIndexedRMOpcode(Mnemonic, *Src, *Suffix);
        if (!setOpcode(MI, Opc) || !addReg(MI, Dst) || !addReg(MI, Dst) ||
            !addIndexedMem(MI, *Src))
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      if (hasUpdate(*Src) && Mnemonic == "ADD" && *Suffix == 'L') {
        if (std::optional<MCRegister> Counter =
                repeatCounterReg(Words, WordCount)) {
          if (!setOpcode(MI, Bedrock::REPADD32postrm) ||
              !addReg(MI, *Counter) || !addReg(MI, Dst) ||
              !addReg(MI, *Counter) || !addReg(MI, Dst))
            return MCDisassembler::Fail;
          addUpdateMem(MI, *Src);
          return MCDisassembler::Success;
        }
      }
      if (hasUpdate(*Src) && !isPostInc(*Src))
        return MCDisassembler::Fail;
      if (!setOpcode(MI, binRMOpcode(Mnemonic, *Suffix, isPostInc(*Src))) ||
          !addReg(MI, Dst) || !addReg(MI, Dst))
        return MCDisassembler::Fail;
      addMaybePostMem(MI, *Src);
      return MCDisassembler::Success;
    }
    if (Src->Kind == BedrockEA::Memory && IsFlagBin && !hasUpdate(*Src)) {
      if (isIndexedEA(*Src)) {
        unsigned Opc = flagIndexedRMOpcode(Mnemonic, *Src, *Suffix);
        if (!setOpcode(MI, Opc) || !addReg(MI, *Dst) ||
            !addIndexedMem(MI, *Src))
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      unsigned Opc =
          Mnemonic == "CMP" ? cmpRMOpcode(*Suffix) : testRMOpcode(*Suffix);
      if (!setOpcode(MI, Opc) || !addReg(MI, *Dst))
        return MCDisassembler::Fail;
      addMem(MI, Src->Reg, Src->Imm);
      return MCDisassembler::Success;
    }
    if (Src->Kind != BedrockEA::Register)
      return MCDisassembler::Fail;
    return AddRR(*Dst, Src->Reg);
  }

  if (ID.contains("EA_TO_EA") && (Mnemonic == "CMP" || Mnemonic == "TEST")) {
    size_t Cursor = payloadStartWord(Form, Words);
    std::optional<BedrockEA> LHS =
        decodeEABySource(Form, Words, WordCount, "lhs", Cursor);
    std::optional<BedrockEA> RHS =
        decodeEABySource(Form, Words, WordCount, "rhs", Cursor);
    if (!LHS || !RHS)
      return MCDisassembler::Fail;

    if (LHS->Kind == BedrockEA::Immediate && RHS->Kind == BedrockEA::Register) {
      unsigned Opc =
          Mnemonic == "CMP" ? cmpRIOpcode(*Suffix) : testRIOpcode(*Suffix);
      if (!setOpcode(MI, Opc) || !addReg(MI, RHS->Reg))
        return MCDisassembler::Fail;
      MI.addOperand(MCOperand::createImm(LHS->Imm));
      return MCDisassembler::Success;
    }
    if (LHS->Kind == BedrockEA::Immediate && RHS->Kind == BedrockEA::Memory &&
        !hasUpdate(*RHS)) {
      unsigned Opc = isIndexedEA(*RHS)
                         ? flagIndexedMIOpcode(Mnemonic, *RHS, *Suffix)
                         : (Mnemonic == "CMP" ? cmpMIOpcode(*Suffix)
                                              : testMIOpcode(*Suffix));
      if (!setOpcode(MI, Opc))
        return MCDisassembler::Fail;
      MI.addOperand(MCOperand::createImm(LHS->Imm));
      if (isIndexedEA(*RHS)) {
        if (!addIndexedMem(MI, *RHS))
          return MCDisassembler::Fail;
      } else {
        addMem(MI, RHS->Reg, RHS->Imm);
      }
      return MCDisassembler::Success;
    }
    if (LHS->Kind == BedrockEA::Memory && RHS->Kind == BedrockEA::Register &&
        !hasUpdate(*LHS)) {
      if (isIndexedEA(*LHS)) {
        unsigned Opc = flagIndexedRMOpcode(Mnemonic, *LHS, *Suffix);
        if (!setOpcode(MI, Opc) || !addReg(MI, RHS->Reg) ||
            !addIndexedMem(MI, *LHS))
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      unsigned Opc =
          Mnemonic == "CMP" ? cmpRMOpcode(*Suffix) : testRMOpcode(*Suffix);
      if (!setOpcode(MI, Opc) || !addReg(MI, RHS->Reg))
        return MCDisassembler::Fail;
      addMem(MI, LHS->Reg, LHS->Imm);
      return MCDisassembler::Success;
    }
    if (LHS->Kind == BedrockEA::Register && RHS->Kind == BedrockEA::Memory &&
        !hasUpdate(*RHS)) {
      if (isIndexedEA(*RHS)) {
        unsigned Opc = flagIndexedMROpcode(Mnemonic, *RHS, *Suffix);
        if (!setOpcode(MI, Opc) || !addReg(MI, LHS->Reg) ||
            !addIndexedMem(MI, *RHS))
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      unsigned Opc =
          Mnemonic == "CMP" ? cmpMROpcode(*Suffix) : testMROpcode(*Suffix);
      if (!setOpcode(MI, Opc) || !addReg(MI, LHS->Reg))
        return MCDisassembler::Fail;
      addMem(MI, RHS->Reg, RHS->Imm);
      return MCDisassembler::Success;
    }
    if (LHS->Kind == BedrockEA::Register && RHS->Kind == BedrockEA::Register)
      return AddRR(LHS->Reg, RHS->Reg);
    return MCDisassembler::Fail;
  }

  if (ID.contains("D_TO_EA")) {
    size_t Cursor = payloadStartWord(Form, Words);
    bool IsFlagBin = Mnemonic == "CMP" || Mnemonic == "TEST";
    std::optional<MCRegister> Src =
        regBySource(Form, Words, IsFlagBin ? "lhs" : "src");
    std::optional<BedrockEA> Dst = decodeEABySource(
        Form, Words, WordCount, IsFlagBin ? "rhs" : "dst", Cursor);
    if (!Src || !Dst)
      return MCDisassembler::Fail;
    if (Dst->Kind == BedrockEA::Memory && !IsFlagBin) {
      if (!setOpcode(MI, binMROpcode(Mnemonic, *Suffix)))
        return MCDisassembler::Fail;
      MI.addOperand(MCOperand::createReg(*Src));
      addMaybePostMem(MI, *Dst);
      return MCDisassembler::Success;
    }
    if (Dst->Kind == BedrockEA::Memory && IsFlagBin && !hasUpdate(*Dst)) {
      if (isIndexedEA(*Dst)) {
        unsigned Opc = flagIndexedMROpcode(Mnemonic, *Dst, *Suffix);
        if (!setOpcode(MI, Opc) || !addReg(MI, *Src) ||
            !addIndexedMem(MI, *Dst))
          return MCDisassembler::Fail;
        return MCDisassembler::Success;
      }
      unsigned Opc =
          Mnemonic == "CMP" ? cmpMROpcode(*Suffix) : testMROpcode(*Suffix);
      if (!setOpcode(MI, Opc))
        return MCDisassembler::Fail;
      MI.addOperand(MCOperand::createReg(*Src));
      addMem(MI, Dst->Reg, Dst->Imm);
      return MCDisassembler::Success;
    }
    if (Dst->Kind != BedrockEA::Register)
      return MCDisassembler::Fail;
    return AddRR(Dst->Reg, *Src);
  }

  return MCDisassembler::Fail;
}

static DecodeStatus decodeMAdd(const bedrock_form_desc *Form,
                               const uint16_t *Words, size_t WordCount,
                               MCInst &MI) {
  std::optional<char> Suffix = sizeSuffix(Form, Words);
  if (!Suffix)
    return MCDisassembler::Fail;

  size_t Cursor = payloadStartWord(Form, Words);
  PrefixState Prefixes = getPrefixState(Words, WordCount);
  std::optional<BedrockEA> LHS =
      decodeEABySource(Form, Words, WordCount, "src", Cursor, &Prefixes);
  std::optional<MCRegister> RHS = regByName(Form, Words, "d");
  std::optional<MCRegister> Dst = regByName(Form, Words, "D");
  if (!LHS || !RHS || !Dst)
    return MCDisassembler::Fail;

  if (LHS->Kind == BedrockEA::Register) {
    if (!setOpcode(MI, maddRRROpcode(*Suffix)) || !addReg(MI, Dst) ||
        !addReg(MI, Dst) || !addReg(MI, LHS->Reg) || !addReg(MI, RHS))
      return MCDisassembler::Fail;
    return MCDisassembler::Success;
  }

  if (LHS->Kind != BedrockEA::Memory || (hasUpdate(*LHS) && !isPostInc(*LHS)) ||
      !setOpcode(MI, maddMRROpcode(*Suffix, isPostInc(*LHS))) ||
      !addReg(MI, Dst) || !addReg(MI, Dst))
    return MCDisassembler::Fail;
  addMaybePostMem(MI, *LHS);
  return addReg(MI, RHS) ? MCDisassembler::Success : MCDisassembler::Fail;
}

static DecodeStatus decodeBitOp(const bedrock_form_desc *Form,
                                const uint16_t *Words, size_t WordCount,
                                MCInst &MI) {
  StringRef ID(Form->id);
  StringRef Mnemonic(Form->mnemonic);
  if (!ID.ends_with("I6_TO_EA"))
    return MCDisassembler::Fail;

  std::optional<char> Suffix = sizeSuffix(Form, Words);
  std::optional<uint64_t> BitIndex =
      fieldValueBySource(Form, Words, "bit_index");
  if (!Suffix || !BitIndex)
    return MCDisassembler::Fail;

  size_t Cursor = payloadStartWord(Form, Words);
  std::optional<BedrockEA> Dst =
      decodeEABySource(Form, Words, WordCount, "dst", Cursor);
  if (!Dst)
    return MCDisassembler::Fail;

  if (Dst->Kind == BedrockEA::Register) {
    if (!setOpcode(MI, bitRIOpcode(Mnemonic, *Suffix)))
      return MCDisassembler::Fail;
    if (Mnemonic == "BTEST") {
      if (!addReg(MI, Dst->Reg))
        return MCDisassembler::Fail;
    } else {
      if (!addReg(MI, Dst->Reg) || !addReg(MI, Dst->Reg))
        return MCDisassembler::Fail;
    }
    MI.addOperand(MCOperand::createImm(*BitIndex));
    return MCDisassembler::Success;
  }

  if (Dst->Kind == BedrockEA::Memory && Mnemonic != "BTEST" &&
      !hasUpdate(*Dst) && !isIndexedEA(*Dst)) {
    if (!setOpcode(MI, bitMIOpcode(Mnemonic, *Suffix)))
      return MCDisassembler::Fail;
    MI.addOperand(MCOperand::createImm(*BitIndex));
    addMem(MI, Dst->Reg, Dst->Imm);
    return MCDisassembler::Success;
  }

  return MCDisassembler::Fail;
}

static DecodeStatus decodeExt(const bedrock_form_desc *Form,
                              const uint16_t *Words, size_t WordCount,
                              MCInst &MI) {
  StringRef ID(Form->id);
  StringRef Mnemonic(Form->mnemonic);
  std::optional<char> Suffix = sizeSuffix(Form, Words);
  if (!Suffix && (Mnemonic == "EXTSW" || Mnemonic == "EXTZW"))
    Suffix = 'B';
  if (!Suffix)
    return MCDisassembler::Fail;

  auto EmitRR = [&](MCRegister Dst, MCRegister Src) -> DecodeStatus {
    if (!setOpcode(MI, extRROpcode(Mnemonic, *Suffix)) || !addReg(MI, Dst) ||
        !addReg(MI, Src))
      return MCDisassembler::Fail;
    return MCDisassembler::Success;
  };

  auto EmitRM = [&](MCRegister Dst, const BedrockEA &Src) -> DecodeStatus {
    if (Src.Kind != BedrockEA::Memory ||
        !setOpcode(MI, extRMOpcode(Mnemonic, *Suffix)) || !addReg(MI, Dst))
      return MCDisassembler::Fail;
    addMem(MI, Src.Reg, Src.Imm);
    return MCDisassembler::Success;
  };

  auto EmitMR = [&](MCRegister Src, const BedrockEA &Dst) -> DecodeStatus {
    if (Dst.Kind != BedrockEA::Memory ||
        !setOpcode(MI, extMROpcode(Mnemonic, *Suffix)) || !addReg(MI, Src))
      return MCDisassembler::Fail;
    addMem(MI, Dst.Reg, Dst.Imm);
    return MCDisassembler::Success;
  };

  if (ID.contains("D_TO_D")) {
    return EmitRR(
        regBySource(Form, Words, "dst").value_or(Bedrock::NoRegister),
        regBySource(Form, Words, "src").value_or(Bedrock::NoRegister));
  }

  if (ID.contains("EA_TO_D")) {
    size_t Cursor = payloadStartWord(Form, Words);
    std::optional<BedrockEA> EASrc =
        decodeEABySource(Form, Words, WordCount, "src", Cursor);
    std::optional<MCRegister> Dst = regBySource(Form, Words, "dst");
    if (!EASrc || !Dst)
      return MCDisassembler::Fail;
    if (EASrc->Kind == BedrockEA::Register)
      return EmitRR(*Dst, EASrc->Reg);
    return EmitRM(*Dst, *EASrc);
  }

  if (ID.contains("D_TO_EA")) {
    size_t Cursor = payloadStartWord(Form, Words);
    std::optional<BedrockEA> EADst =
        decodeEABySource(Form, Words, WordCount, "dst", Cursor);
    std::optional<MCRegister> Src = regBySource(Form, Words, "src");
    if (!EADst || !Src)
      return MCDisassembler::Fail;
    if (EADst->Kind == BedrockEA::Register)
      return EmitRR(EADst->Reg, *Src);
    return EmitMR(*Src, *EADst);
  }

  if (ID.contains("EA_TO_EA")) {
    size_t Cursor = payloadStartWord(Form, Words);
    std::optional<BedrockEA> Src =
        decodeEABySource(Form, Words, WordCount, "src", Cursor);
    std::optional<BedrockEA> Dst =
        decodeEABySource(Form, Words, WordCount, "dst", Cursor);
    if (!Src || !Dst)
      return MCDisassembler::Fail;
    if (Src->Kind == BedrockEA::Register && Dst->Kind == BedrockEA::Register)
      return EmitRR(Dst->Reg, Src->Reg);
    if (Dst->Kind == BedrockEA::Register)
      return EmitRM(Dst->Reg, *Src);
    if (Src->Kind == BedrockEA::Register)
      return EmitMR(Src->Reg, *Dst);
  }

  return MCDisassembler::Fail;
}

static DecodeStatus decodeIntUnary(const bedrock_form_desc *Form,
                                   const uint16_t *Words, size_t WordCount,
                                   MCInst &MI) {
  StringRef Mnemonic(Form->mnemonic);
  size_t Cursor = payloadStartWord(Form, Words);
  std::optional<BedrockEA> Dst =
      decodeEABySource(Form, Words, WordCount, "dst", Cursor);
  if (!Dst)
    return MCDisassembler::Fail;

  if (Mnemonic == "CLR") {
    if (Dst->Kind == BedrockEA::Register) {
      MI.setOpcode(Bedrock::CLR64r);
      return addReg(MI, Dst->Reg) ? MCDisassembler::Success
                                  : MCDisassembler::Fail;
    }
    if (Dst->Kind == BedrockEA::Memory) {
      MI.setOpcode(Bedrock::CLRm);
      addMem(MI, Dst->Reg, Dst->Imm);
      return MCDisassembler::Success;
    }
    return MCDisassembler::Fail;
  }

  std::optional<char> Suffix = sizeSuffix(Form, Words);
  if (!Suffix)
    return MCDisassembler::Fail;

  if (Dst->Kind == BedrockEA::Register) {
    if (!setOpcode(MI, incDecROpcode(Mnemonic, *Suffix)) ||
        !addReg(MI, Dst->Reg) || !addReg(MI, Dst->Reg))
      return MCDisassembler::Fail;
    return MCDisassembler::Success;
  }
  if (Dst->Kind != BedrockEA::Memory)
    return MCDisassembler::Fail;
  if (isIndexedEA(*Dst)) {
    if (!setOpcode(MI, incDecIndexedMOpcode(Mnemonic, *Dst, *Suffix)) ||
        !addIndexedMem(MI, *Dst))
      return MCDisassembler::Fail;
    return MCDisassembler::Success;
  }
  if (!setOpcode(MI, incDecMOpcode(Mnemonic, *Suffix)))
    return MCDisassembler::Fail;
  addMem(MI, Dst->Reg, Dst->Imm);
  return MCDisassembler::Success;
}

static unsigned djccOpcode(char Suffix) {
  switch (Suffix) {
  default:
    return 0;
  case 'B':
    return Bedrock::DJCC8r;
  case 'W':
    return Bedrock::DJCC16r;
  case 'L':
    return Bedrock::DJCC32r;
  case 'Q':
    return Bedrock::DJCC64r;
  }
}

static unsigned ijccOpcode(char Suffix) {
  switch (Suffix) {
  default:
    return 0;
  case 'L':
    return Bedrock::IJCC32r;
  case 'Q':
    return Bedrock::IJCC64r;
  }
}

static DecodeStatus decodeCountBranch(const bedrock_form_desc *Form,
                                      const uint16_t *Words, size_t WordCount,
                                      MCInst &MI) {
  StringRef Mnemonic(Form->mnemonic);
  std::optional<char> Suffix = sizeSuffix(Form, Words);
  std::optional<uint64_t> CC = fieldValueBySource(Form, Words, "cc");
  size_t Cursor = payloadStartWord(Form, Words);
  std::optional<BedrockEA> Target =
      decodeEABySource(Form, Words, WordCount, "target", Cursor);
  if (!Suffix || !CC || !Target || Target->Kind != BedrockEA::Immediate)
    return MCDisassembler::Fail;

  if (Mnemonic == "DJcc") {
    std::optional<MCRegister> Counter = regBySource(Form, Words, "counter");
    if (!Counter || !setOpcode(MI, djccOpcode(*Suffix)) ||
        !addReg(MI, *Counter) || !addReg(MI, *Counter))
      return MCDisassembler::Fail;
  } else if (Mnemonic == "IJcc") {
    std::optional<MCRegister> Index = regBySource(Form, Words, "index");
    std::optional<MCRegister> Bound = regBySource(Form, Words, "bound");
    if (!Index || !Bound || !setOpcode(MI, ijccOpcode(*Suffix)) ||
        !addReg(MI, *Index) || !addReg(MI, *Index) || !addReg(MI, *Bound))
      return MCDisassembler::Fail;
  } else {
    return MCDisassembler::Fail;
  }

  MI.addOperand(MCOperand::createImm(Target->Imm));
  MI.addOperand(MCOperand::createImm(*CC));
  return MCDisassembler::Success;
}

static bool isExtMnemonic(StringRef Mnemonic) {
  return Mnemonic == "EXTZQ" || Mnemonic == "EXTSQ" || Mnemonic == "EXTZL" ||
         Mnemonic == "EXTSL" || Mnemonic == "EXTZW" || Mnemonic == "EXTSW";
}

static DecodeStatus decodeShift(const bedrock_form_desc *Form,
                                const uint16_t *Words, size_t WordCount,
                                MCInst &MI) {
  StringRef ID(Form->id);
  StringRef Mnemonic(Form->mnemonic);
  std::optional<char> Suffix = sizeSuffix(Form, Words);
  if (!Suffix)
    return MCDisassembler::Fail;

  if (ID.contains("I6_TO_EA")) {
    size_t Cursor = payloadStartWord(Form, Words);
    std::optional<uint64_t> Count = fieldValueBySource(Form, Words, "count");
    std::optional<BedrockEA> Dst =
        decodeEABySource(Form, Words, WordCount, "dst", Cursor);
    if (!Count || !Dst)
      return MCDisassembler::Fail;
    if (Dst->Kind == BedrockEA::Register) {
      if (!setOpcode(MI, shiftRIOpcode(Mnemonic, *Suffix)) ||
          !addReg(MI, Dst->Reg) || !addReg(MI, Dst->Reg))
        return MCDisassembler::Fail;
      MI.addOperand(MCOperand::createImm(*Count));
      return MCDisassembler::Success;
    }
    if (Dst->Kind != BedrockEA::Memory ||
        !setOpcode(MI, shiftMIOpcode(Mnemonic, *Suffix)))
      return MCDisassembler::Fail;
    MI.addOperand(MCOperand::createImm(*Count));
    addMem(MI, Dst->Reg, Dst->Imm);
    return MCDisassembler::Success;
  }

  if (ID.contains("D_TO_D")) {
    std::optional<MCRegister> Count = regByName(Form, Words, "n");
    std::optional<MCRegister> Dst = regByName(Form, Words, "d");
    if (!Count || !Dst || !setOpcode(MI, shiftRROpcode(Mnemonic, *Suffix)) ||
        !addReg(MI, Dst) || !addReg(MI, Dst) || !addReg(MI, Count))
      return MCDisassembler::Fail;
    return MCDisassembler::Success;
  }

  if (ID.contains("D_TO_EA")) {
    size_t Cursor = payloadStartWord(Form, Words);
    std::optional<MCRegister> Count = regBySource(Form, Words, "count");
    std::optional<BedrockEA> Dst =
        decodeEABySource(Form, Words, WordCount, "dst", Cursor);
    if (!Count || !Dst)
      return MCDisassembler::Fail;
    if (Dst->Kind == BedrockEA::Register) {
      if (!setOpcode(MI, shiftRROpcode(Mnemonic, *Suffix)) ||
          !addReg(MI, Dst->Reg) || !addReg(MI, Dst->Reg) || !addReg(MI, Count))
        return MCDisassembler::Fail;
      return MCDisassembler::Success;
    }
    if (Dst->Kind != BedrockEA::Memory ||
        !setOpcode(MI, shiftMROpcode(Mnemonic, *Suffix)) || !addReg(MI, Count))
      return MCDisassembler::Fail;
    addMem(MI, Dst->Reg, Dst->Imm);
    return MCDisassembler::Success;
  }

  return MCDisassembler::Fail;
}

static DecodeStatus decodeFloat(const bedrock_form_desc *Form,
                                const uint16_t *Words, size_t WordCount,
                                MCInst &MI) {
  StringRef ID(Form->id);
  StringRef Mnemonic(Form->mnemonic);

  if (Mnemonic == "FCLR") {
    if (ID == "FCLR.F") {
      MI.setOpcode(Bedrock::FCLRr);
      return addReg(MI, regBySource(Form, Words, "dst"))
                 ? MCDisassembler::Success
                 : MCDisassembler::Fail;
    }
    if (ID == "FCLR.EA") {
      size_t Cursor = payloadStartWord(Form, Words);
      std::optional<BedrockEA> Dst =
          decodeEABySource(Form, Words, WordCount, "dst", Cursor);
      if (!Dst || Dst->Kind != BedrockEA::Memory)
        return MCDisassembler::Fail;
      MI.setOpcode(Bedrock::FCLRm);
      addMem(MI, Dst->Reg, Dst->Imm);
      return MCDisassembler::Success;
    }
  }

  if (Mnemonic == "FCVT" || Mnemonic == "FCVTU") {
    bool IsUnsigned = Mnemonic == "FCVTU";
    if (ID.contains("D_TO_F")) {
      MI.setOpcode(IsUnsigned ? Bedrock::FCVTUI64toF64
                              : Bedrock::FCVTSI64toF64);
      return addReg(MI, regBySource(Form, Words, "dst")) &&
                     addReg(MI, regBySource(Form, Words, "src"))
                 ? MCDisassembler::Success
                 : MCDisassembler::Fail;
    }
    if (ID.contains("F_TO_D")) {
      MI.setOpcode(IsUnsigned ? Bedrock::FCVTF64toUI64
                              : Bedrock::FCVTF64toSI64);
      return addReg(MI, regBySource(Form, Words, "dst")) &&
                     addReg(MI, regBySource(Form, Words, "src"))
                 ? MCDisassembler::Success
                 : MCDisassembler::Fail;
    }
    if (ID.contains("F_TO_F")) {
      MI.setOpcode(IsUnsigned ? Bedrock::FCVTU32to64 : Bedrock::FCVT32to64);
      return addReg(MI, regBySource(Form, Words, "dst")) &&
                     addReg(MI, regBySource(Form, Words, "src"))
                 ? MCDisassembler::Success
                 : MCDisassembler::Fail;
    }
  }

  if (Mnemonic == "FMOVcc") {
    std::optional<uint64_t> CC = fieldValueBySource(Form, Words, "cc");
    if (!CC)
      return MCDisassembler::Fail;

    if (ID.contains("F_TO_F")) {
      if (!setOpcode(MI, fmovccRROpcode('D')) ||
          !addReg(MI, regBySource(Form, Words, "dst")) ||
          !addReg(MI, regBySource(Form, Words, "dst")) ||
          !addReg(MI, regBySource(Form, Words, "src")))
        return MCDisassembler::Fail;
      MI.addOperand(MCOperand::createImm(*CC));
      return MCDisassembler::Success;
    }

    std::optional<char> CondMovSuffix = sizeSuffix(Form, Words);
    if (!CondMovSuffix)
      return MCDisassembler::Fail;
    size_t Cursor = payloadStartWord(Form, Words);
    if (ID.contains("EA_TO_F")) {
      std::optional<BedrockEA> Src =
          decodeEABySource(Form, Words, WordCount, "src", Cursor);
      if (!Src || Src->Kind != BedrockEA::Memory ||
          !setOpcode(MI, fmovccRMOpcode(*CondMovSuffix)) ||
          !addReg(MI, regBySource(Form, Words, "dst")) ||
          !addReg(MI, regBySource(Form, Words, "dst")))
        return MCDisassembler::Fail;
      addMem(MI, Src->Reg, Src->Imm);
      MI.addOperand(MCOperand::createImm(*CC));
      return MCDisassembler::Success;
    }
    if (ID.contains("F_TO_EA")) {
      std::optional<BedrockEA> Dst =
          decodeEABySource(Form, Words, WordCount, "dst", Cursor);
      if (!Dst || Dst->Kind != BedrockEA::Memory ||
          !setOpcode(MI, fmovccMROpcode(*CondMovSuffix)) ||
          !addReg(MI, regBySource(Form, Words, "src")))
        return MCDisassembler::Fail;
      addMem(MI, Dst->Reg, Dst->Imm);
      MI.addOperand(MCOperand::createImm(*CC));
      return MCDisassembler::Success;
    }
  }

  std::optional<char> Suffix = sizeSuffix(Form, Words);
  if (!Suffix)
    return MCDisassembler::Fail;

  if (Mnemonic == "FMOV") {
    if (ID.contains("F_TO_F")) {
      if (!setOpcode(MI, fmovRROpcode(*Suffix)) ||
          !addReg(MI, regBySource(Form, Words, "dst")) ||
          !addReg(MI, regBySource(Form, Words, "src")))
        return MCDisassembler::Fail;
      return MCDisassembler::Success;
    }
    size_t Cursor = payloadStartWord(Form, Words);
    if (ID.contains("EA_TO_F")) {
      std::optional<BedrockEA> Src =
          decodeEABySource(Form, Words, WordCount, "src", Cursor);
      if (!Src || Src->Kind != BedrockEA::Memory ||
          !setOpcode(MI, fmovRMOpcode(*Suffix)) ||
          !addReg(MI, regBySource(Form, Words, "dst")))
        return MCDisassembler::Fail;
      addMem(MI, Src->Reg, Src->Imm);
      return MCDisassembler::Success;
    }
    if (ID.contains("F_TO_EA")) {
      std::optional<BedrockEA> Dst =
          decodeEABySource(Form, Words, WordCount, "dst", Cursor);
      if (!Dst || Dst->Kind != BedrockEA::Memory ||
          !setOpcode(MI, fmovMROpcode(*Suffix)) ||
          !addReg(MI, regBySource(Form, Words, "src")))
        return MCDisassembler::Fail;
      addMem(MI, Dst->Reg, Dst->Imm);
      return MCDisassembler::Success;
    }
  }

  if (Mnemonic == "FCMP") {
    if (ID.contains("EA_TO_F")) {
      size_t Cursor = payloadStartWord(Form, Words);
      std::optional<BedrockEA> Src =
          decodeEABySource(Form, Words, WordCount, "src", Cursor);
      if (!Src || Src->Kind != BedrockEA::Memory ||
          !setOpcode(MI, fcmpRMOpcode(Mnemonic, *Suffix)) ||
          !addReg(MI, regBySource(Form, Words, "dst")))
        return MCDisassembler::Fail;
      addMem(MI, Src->Reg, Src->Imm);
      return MCDisassembler::Success;
    }
    MI.setOpcode(*Suffix == 'S' ? Bedrock::FCMP32rr : Bedrock::FCMP64rr);
    return addReg(MI, regBySource(Form, Words, "dst")) &&
                   addReg(MI, regBySource(Form, Words, "src"))
               ? MCDisassembler::Success
               : MCDisassembler::Fail;
  }

  if (Mnemonic == "FCOPYSIGN") {
    if (!setOpcode(MI, *Suffix == 'S' ? Bedrock::FCOPYSIGN32rr
                                      : Bedrock::FCOPYSIGN64rr) ||
        !addReg(MI, regBySource(Form, Words, "dst")) ||
        !addReg(MI, regBySource(Form, Words, "magnitude_src")) ||
        !addReg(MI, regBySource(Form, Words, "sign_src")))
      return MCDisassembler::Fail;
    return MCDisassembler::Success;
  }

  if (unsigned Opc = ffmaRRROpcode(Mnemonic, *Suffix)) {
    std::optional<MCRegister> Dst = regBySource(Form, Words, "dst");
    if (!Dst)
      return MCDisassembler::Fail;

    if (ID.contains("F_TO_F_TO_F")) {
      MI.setOpcode(Opc);
      return addReg(MI, Dst) && addReg(MI, regBySource(Form, Words, "lhs")) &&
                     addReg(MI, regBySource(Form, Words, "rhs"))
                 ? MCDisassembler::Success
                 : MCDisassembler::Fail;
    }

    size_t Cursor = payloadStartWord(Form, Words);
    if (ID.contains("F_TO_EA_TO_F")) {
      std::optional<BedrockEA> Rhs =
          decodeEABySource(Form, Words, WordCount, "rhs", Cursor);
      if (!Rhs || Rhs->Kind != BedrockEA::Memory ||
          !setOpcode(MI, ffmaRMROpcode(Mnemonic, *Suffix)) ||
          !addReg(MI, Dst) || !addReg(MI, regBySource(Form, Words, "lhs")))
        return MCDisassembler::Fail;
      addMem(MI, Rhs->Reg, Rhs->Imm);
      return MCDisassembler::Success;
    }

    if (ID.contains("EA_TO_F_TO_F")) {
      std::optional<BedrockEA> Lhs =
          decodeEABySource(Form, Words, WordCount, "lhs", Cursor);
      if (!Lhs || Lhs->Kind != BedrockEA::Memory ||
          !setOpcode(MI, ffmaMRROpcode(Mnemonic, *Suffix)) || !addReg(MI, Dst))
        return MCDisassembler::Fail;
      addMem(MI, Lhs->Reg, Lhs->Imm);
      return addReg(MI, regBySource(Form, Words, "rhs"))
                 ? MCDisassembler::Success
                 : MCDisassembler::Fail;
    }
  }

  if (unsigned Opc = funaryRROpcode(Mnemonic, *Suffix)) {
    size_t Cursor = payloadStartWord(Form, Words);
    if (ID.contains("EA_TO_F")) {
      std::optional<BedrockEA> Src =
          decodeEABySource(Form, Words, WordCount, "src", Cursor);
      if (!Src || Src->Kind != BedrockEA::Memory ||
          !setOpcode(MI, funaryRMOpcode(Mnemonic, *Suffix)) ||
          !addReg(MI, regBySource(Form, Words, "dst")))
        return MCDisassembler::Fail;
      addMem(MI, Src->Reg, Src->Imm);
      return MCDisassembler::Success;
    }
    if (ID.contains("F_TO_EA")) {
      std::optional<BedrockEA> Dst =
          decodeEABySource(Form, Words, WordCount, "dst", Cursor);
      if (!Dst || Dst->Kind != BedrockEA::Memory ||
          !setOpcode(MI, funaryMROpcode(Mnemonic, *Suffix)) ||
          !addReg(MI, regBySource(Form, Words, "src")))
        return MCDisassembler::Fail;
      addMem(MI, Dst->Reg, Dst->Imm);
      return MCDisassembler::Success;
    }
    MI.setOpcode(Opc);
    return addReg(MI, regBySource(Form, Words, "dst")) &&
                   addReg(MI, regBySource(Form, Words, "src"))
               ? MCDisassembler::Success
               : MCDisassembler::Fail;
  }

  if (unsigned Opc = fbinRROpcode(Mnemonic, *Suffix)) {
    std::optional<MCRegister> Dst = regBySource(Form, Words, "dst");
    if (ID.contains("EA_TO_F")) {
      size_t Cursor = payloadStartWord(Form, Words);
      std::optional<BedrockEA> Src =
          decodeEABySource(Form, Words, WordCount, "src", Cursor);
      if (!Src || !Dst)
        return MCDisassembler::Fail;
      if (Src->Kind == BedrockEA::Register) {
        MI.setOpcode(Opc);
        return addReg(MI, Dst) && addReg(MI, Dst) && addReg(MI, Src->Reg)
                   ? MCDisassembler::Success
                   : MCDisassembler::Fail;
      }
      if (Src->Kind != BedrockEA::Memory ||
          !setOpcode(MI, fbinRMOpcode(Mnemonic, *Suffix)) || !addReg(MI, Dst) ||
          !addReg(MI, Dst))
        return MCDisassembler::Fail;
      addMem(MI, Src->Reg, Src->Imm);
      return MCDisassembler::Success;
    }

    MI.setOpcode(Opc);
    std::optional<MCRegister> Src = regBySource(Form, Words, "src");
    return addReg(MI, Dst) && addReg(MI, Dst) && addReg(MI, Src)
               ? MCDisassembler::Success
               : MCDisassembler::Fail;
  }

  return MCDisassembler::Fail;
}

static DecodeStatus decodeAtomic(const bedrock_form_desc *Form,
                                 const uint16_t *Words, size_t WordCount,
                                 MCInst &MI) {
  StringRef ID(Form->id);
  StringRef Mnemonic(Form->mnemonic);
  std::optional<char> Suffix = sizeSuffix(Form, Words);
  std::optional<uint64_t> Order = fieldValueBySource(Form, Words, "order");
  size_t Cursor = payloadStartWord(Form, Words);
  std::optional<BedrockEA> Memory =
      decodeEABySource(Form, Words, WordCount, "memory", Cursor);
  if (!Suffix || !Order || !Memory || Memory->Kind != BedrockEA::Memory)
    return MCDisassembler::Fail;

  if (ID.starts_with("FETCH")) {
    std::optional<MCRegister> Src = regBySource(Form, Words, "src");
    if (!setOpcode(MI, fetchOpcode(Mnemonic, *Suffix)) || !addReg(MI, Src) ||
        !addReg(MI, Src))
      return MCDisassembler::Fail;
    addMem(MI, Memory->Reg, Memory->Imm);
    MI.addOperand(MCOperand::createImm(*Order));
    return MCDisassembler::Success;
  }

  if (ID.starts_with("CMPXCHG")) {
    std::optional<MCRegister> Expected = regBySource(Form, Words, "expected");
    if (!setOpcode(MI, cmpXchgOpcode(*Suffix)) || !addReg(MI, Expected) ||
        !addReg(MI, Expected) ||
        !addReg(MI, regBySource(Form, Words, "desired")))
      return MCDisassembler::Fail;
    addMem(MI, Memory->Reg, Memory->Imm);
    MI.addOperand(MCOperand::createImm(*Order));
    return MCDisassembler::Success;
  }

  return MCDisassembler::Fail;
}

static DecodeStatus decodePushPop(const bedrock_form_desc *Form,
                                  const uint16_t *Words, MCInst &MI) {
  StringRef ID(Form->id);
  std::optional<MCRegister> Reg = regBySource(Form, Words, "reg");
  if (!Reg)
    return MCDisassembler::Fail;

  if (ID == "PUSH.D")
    MI.setOpcode(Bedrock::PUSHD);
  else if (ID == "PUSH.A")
    MI.setOpcode(Bedrock::PUSHA);
  else if (ID == "POP.D")
    MI.setOpcode(Bedrock::POPD);
  else if (ID == "POP.A")
    MI.setOpcode(Bedrock::POPA);
  else
    return MCDisassembler::Fail;

  MI.addOperand(MCOperand::createReg(*Reg));
  return MCDisassembler::Success;
}

static DecodeStatus decodeLea(const bedrock_form_desc *Form,
                              const uint16_t *Words, size_t WordCount,
                              MCInst &MI) {
  if (StringRef(Form->id) != "LEA.EA_TO_A")
    return MCDisassembler::Fail;

  size_t Cursor = payloadStartWord(Form, Words);
  std::optional<BedrockEA> Src =
      decodeEABySource(Form, Words, WordCount, "src", Cursor);
  std::optional<MCRegister> Dst = regBySource(Form, Words, "dst");
  if (!Src || Src->Kind != BedrockEA::Memory || !Dst)
    return MCDisassembler::Fail;

  if (isIndexedEA(*Src)) {
    if (Src->Imm != 0 || Src->Scale != 4 ||
        !setOpcode(MI, Src->Signed32Index ? Bedrock::LEA4L : Bedrock::LEA4) ||
        !addReg(MI, *Dst) || !addReg(MI, Src->Reg) || !addReg(MI, Src->Index))
      return MCDisassembler::Fail;
    return MCDisassembler::Success;
  }

  if (!setOpcode(MI, Bedrock::LEAri) || !addReg(MI, *Dst))
    return MCDisassembler::Fail;
  addMem(MI, Src->Reg, Src->Imm);
  return MCDisassembler::Success;
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

static bool formFitsCandidateWords(const bedrock_form_desc *Form,
                                   const uint16_t *Words, size_t WordCount) {
  return payloadStartWord(Form, Words) <= WordCount;
}

static DecodeStatus decodeNativeInstruction(const bedrock_form_desc *Form,
                                            const uint16_t *Words,
                                            size_t WordCount, MCInst &Instr) {
  StringRef ID(Form->id);
  StringRef Mnemonic(Form->mnemonic);

  if (unsigned Opc = noOperandOpcode(ID)) {
    Instr.setOpcode(Opc);
    return MCDisassembler::Success;
  }

  if (ID == "CALL.IMM16" || ID == "CALL.IMM32") {
    size_t Cursor = payloadStartWord(Form, Words);
    unsigned PayloadWords = ID == "CALL.IMM16" ? 1 : 2;
    unsigned PayloadBits = ID == "CALL.IMM16" ? 16 : 32;
    if (Cursor + PayloadWords > WordCount)
      return MCDisassembler::Fail;
    Instr.setOpcode(ID == "CALL.IMM16" ? Bedrock::CALLpcrel16
                                       : Bedrock::CALLpcrel);
    Instr.addOperand(MCOperand::createImm(
        signExtend(readPayload(Words, Cursor, PayloadWords), PayloadBits)));
    return MCDisassembler::Success;
  }

  if (ID == "JMP.IMM" || ID == "Jcc.IMM") {
    size_t Cursor = payloadStartWord(Form, Words);
    if (Cursor + 1 > WordCount || sizeSuffix(Form, Words) != 'W')
      return MCDisassembler::Fail;
    Instr.setOpcode(ID == "JMP.IMM" ? Bedrock::JMP : Bedrock::JCC);
    Instr.addOperand(
        MCOperand::createImm(signExtend(readPayload(Words, Cursor, 1), 16)));
    if (ID == "Jcc.IMM") {
      std::optional<uint64_t> CC = fieldValueBySource(Form, Words, "cc");
      if (!CC)
        return MCDisassembler::Fail;
      Instr.addOperand(MCOperand::createImm(*CC));
    }
    return MCDisassembler::Success;
  }

  DecodeStatus Status = MCDisassembler::Fail;
  if (Mnemonic == "MOV")
    Status = decodeMove(Form, Words, WordCount, Instr);
  else if (Mnemonic == "MOVcc")
    Status = decodeMovcc(Form, Words, WordCount, Instr);
  else if (Mnemonic == "MADD")
    Status = decodeMAdd(Form, Words, WordCount, Instr);
  else if (Mnemonic == "BCHG" || Mnemonic == "BCLR" || Mnemonic == "BSET" ||
           Mnemonic == "BTEST")
    Status = decodeBitOp(Form, Words, WordCount, Instr);
  else if (Mnemonic == "CLR" || Mnemonic == "INC" || Mnemonic == "DEC")
    Status = decodeIntUnary(Form, Words, WordCount, Instr);
  else if (Mnemonic == "DJcc" || Mnemonic == "IJcc")
    Status = decodeCountBranch(Form, Words, WordCount, Instr);
  else if (isIntBinMnemonic(Mnemonic) || Mnemonic == "CMP")
    Status = decodeIntBinOrCmp(Form, Words, WordCount, Instr);
  else if (isExtMnemonic(Mnemonic))
    Status = decodeExt(Form, Words, WordCount, Instr);
  else if (Mnemonic == "SHL" || Mnemonic == "SHR" || Mnemonic == "SAR")
    Status = decodeShift(Form, Words, WordCount, Instr);
  else if (Mnemonic.starts_with("FETCH") || Mnemonic == "CMPXCHG")
    Status = decodeAtomic(Form, Words, WordCount, Instr);
  else if (Mnemonic == "PUSH" || Mnemonic == "POP")
    Status = decodePushPop(Form, Words, Instr);
  else if (Mnemonic == "LEA")
    Status = decodeLea(Form, Words, WordCount, Instr);
  else if (Mnemonic.starts_with("F"))
    Status = decodeFloat(Form, Words, WordCount, Instr);

  return Status;
}

static bool referenceDisassemblesComplete(const uint16_t *Words,
                                          size_t WordCount) {
  char Text[256];
  if (bedrock_disassemble_line(Words, WordCount, Text, sizeof(Text), nullptr) !=
      BEDROCK_OK)
    return false;
  StringRef Disassembled(Text);
  return !Disassembled.contains("disp") && !Disassembled.contains("imm") &&
         !Disassembled.contains("<");
}

DecodeStatus BedrockDisassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                                                 ArrayRef<uint8_t> Bytes,
                                                 uint64_t Address,
                                                 raw_ostream &CStream) const {
  (void)Address;
  (void)CStream;

  if (Bytes.size() < 2) {
    Size = 0;
    return Fail;
  }

  uint16_t Words[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
  Words[0] = support::endian::read16le(Bytes.data());
  size_t DeclaredWordCount = declaredWords(Words[0]);
  if (DeclaredWordCount == 0 ||
      DeclaredWordCount > BEDROCK_MAX_INSTRUCTION_WORDS) {
    Size = 0;
    return Fail;
  }

  size_t AvailableWordCount = Bytes.size() / 2;
  if (AvailableWordCount > BEDROCK_MAX_INSTRUCTION_WORDS)
    AvailableWordCount = BEDROCK_MAX_INSTRUCTION_WORDS;
  bool HasPrefix = (Words[0] & BEDROCK_WORD0_PREFIX_BIT) != 0;
  size_t CandidateLimit = HasPrefix ? AvailableWordCount : DeclaredWordCount;
  if (CandidateLimit > AvailableWordCount)
    CandidateLimit = AvailableWordCount;

  for (size_t I = 1; I != AvailableWordCount; ++I)
    Words[I] = support::endian::read16le(Bytes.data() + I * 2);

  size_t WordCount = 0;
  const bedrock_form_desc *Form = nullptr;
  size_t FallbackWordCount = 0;
  const bedrock_form_desc *FallbackForm = nullptr;
  size_t FirstCandidate = HasPrefix ? 2 : 1;
  for (size_t CandidateWords = FirstCandidate; CandidateWords <= CandidateLimit;
       ++CandidateWords) {
    uint16_t DecodeWords[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
    for (size_t I = 0; I != CandidateWords; ++I)
      DecodeWords[I] = Words[I];
    setDeclaredWords(DecodeWords[0], CandidateWords);

    const bedrock_form_desc *CandidateForm =
        bedrock_decode_form(DecodeWords, CandidateWords);
    if (!CandidateForm && (DecodeWords[0] & BEDROCK_WORD0_PREFIX_BIT) != 0 &&
        CandidateWords > 1) {
      uint16_t UnprefixedWords[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
      size_t UnprefixedWordCount = CandidateWords - 1;
      UnprefixedWords[0] =
          (DecodeWords[0] &
           ~uint16_t(BEDROCK_WORD0_PREFIX_BIT | BEDROCK_WORD0_LENGTH_MASK)) |
          uint16_t((UnprefixedWordCount - 1) << 12);
      for (size_t I = 1; I != UnprefixedWordCount; ++I)
        UnprefixedWords[I] = DecodeWords[I + 1];
      CandidateForm = bedrock_decode_form(UnprefixedWords, UnprefixedWordCount);
    }
    if (!CandidateForm)
      continue;

    CandidateForm = decodeUnprefixedFormIfNeeded(CandidateForm, DecodeWords,
                                                 CandidateWords);
    if (!formFitsCandidateWords(CandidateForm, DecodeWords, CandidateWords))
      continue;

    MCInst Probe;
    bool NativeOK = decodeNativeInstruction(CandidateForm, DecodeWords,
                                            CandidateWords, Probe) == Success;
    if (NativeOK) {
      WordCount = CandidateWords;
      Form = CandidateForm;
      break;
    }

    if (FallbackWordCount == 0 &&
        referenceDisassemblesComplete(DecodeWords, CandidateWords)) {
      FallbackWordCount = CandidateWords;
      FallbackForm = CandidateForm;
    }
  }

  if (!Form && FallbackForm) {
    WordCount = FallbackWordCount;
    Form = FallbackForm;
  }

  if (!Form) {
    Size = 0;
    return Fail;
  }

  Size = WordCount * 2;
  Form = decodeUnprefixedFormIfNeeded(Form, Words, WordCount);
  unsigned Flags = repeatGroupFlags(Words, WordCount) | Bedrock::DecodedInst;
  if (DeclaredWordCount > WordCount)
    Flags |= unsigned(DeclaredWordCount) << Bedrock::DeclaredLenShift;
  Instr.setFlags(Flags);
  auto DecodeEncoded = [&]() -> DecodeStatus {
    DecodeStatus Status = setEncodedInstWords(Instr, Words, WordCount);
    Instr.setFlags(Flags);
    return Status;
  };

  DecodeStatus Status = decodeNativeInstruction(Form, Words, WordCount, Instr);
  if (Status == Success)
    return Success;
  return DecodeEncoded();
}

static MCDisassembler *createBedrockDisassembler(const Target &T,
                                                 const MCSubtargetInfo &STI,
                                                 MCContext &Ctx) {
  (void)T;
  return new BedrockDisassembler(STI, Ctx);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeBedrockDisassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheBedrockTarget(),
                                         createBedrockDisassembler);
}
