//===-- BedrockMCEncoding.cpp - Bedrock MC encoding helpers ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockMCEncoding.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>

using namespace llvm;

namespace llvm::BedrockMC {
#define GET_BedrockISAForms_IMPL
#define GET_BedrockRepeatEligibilityTable_IMPL
#define GET_BedrockRegisterSelectors_IMPL
#define GET_BedrockScalarEncodingForms_IMPL
#define GET_BedrockVectorEncodingForms_IMPL
#include "BedrockGenSearchableTables.inc"

ScalarOperandDesc ScalarEncodingForm::operand(unsigned Index) const {
#define BEDROCK_SCALAR_OPERAND_CASE(N)                                      \
  case N:                                                                  \
    return {static_cast<ScalarOperandKind>(Operand##N##Kind),               \
            static_cast<char>(Operand##N##Field), Operand##N##Width,        \
            Operand##N##Signed, Operand##N##AllowImmediateEA,              \
            Operand##N##FixedValue,                                        \
            {Operand##N##AllowedMask0, Operand##N##AllowedMask1,           \
             Operand##N##AllowedMask2, Operand##N##AllowedMask3}}
  switch (Index) {
    BEDROCK_SCALAR_OPERAND_CASE(0);
    BEDROCK_SCALAR_OPERAND_CASE(1);
    BEDROCK_SCALAR_OPERAND_CASE(2);
    BEDROCK_SCALAR_OPERAND_CASE(3);
  default:
    llvm_unreachable("scalar operand index is out of range");
  }
#undef BEDROCK_SCALAR_OPERAND_CASE
}

VectorOperandDesc VectorEncodingForm::operand(unsigned Index) const {
#define BEDROCK_VECTOR_OPERAND_CASE(N)                                      \
  case N:                                                                  \
    return {static_cast<VectorOperandKind>(Operand##N##Kind),               \
            static_cast<char>(Operand##N##Field), Operand##N##Width,        \
            Operand##N##AllowImmediateEA}
  switch (Index) {
    BEDROCK_VECTOR_OPERAND_CASE(0);
    BEDROCK_VECTOR_OPERAND_CASE(1);
    BEDROCK_VECTOR_OPERAND_CASE(2);
    BEDROCK_VECTOR_OPERAND_CASE(3);
    BEDROCK_VECTOR_OPERAND_CASE(4);
    BEDROCK_VECTOR_OPERAND_CASE(5);
  default:
    llvm_unreachable("vector operand index is out of range");
  }
#undef BEDROCK_VECTOR_OPERAND_CASE
}

ArrayRef<VectorEncodingForm> vectorEncodingForms() {
  return BedrockVectorEncodingForms;
}

ArrayRef<ScalarEncodingForm> scalarEncodingForms() {
  return BedrockScalarEncodingForms;
}

ArrayRef<RepeatEligibility> repeatEligibilityEntries() {
  return BedrockRepeatEligibilityTable;
}

bool isRegisterSelectorName(StringRef Name) {
  return llvm::any_of(BedrockRegisterSelectors,
                      [Name](const RegisterSelector &Selector) {
                        return Name == Selector.Name;
                      });
}

bool isReservedAssemblyName(StringRef Name) {
  std::string LowerStorage = Name.lower();
  StringRef Lower = LowerStorage;
  if (Lower == "sp" || Lower == "pc" || Lower == "flags" ||
      Lower == "status" || Lower == "cs" || Lower == "ds" ||
      Lower == "ss" || Lower == "gs0" || Lower == "gs1" ||
      Lower == "gs2" || Lower == "gs3" || Lower == "gs4" ||
      Lower == "gs5" || Lower == "fflags" || Lower == "fstatus" ||
      isRegisterSelectorName(Lower))
    return true;

  auto IsNumbered = [Lower](char Prefix, unsigned Limit) {
    if (Lower.size() < 2 || Lower.front() != Prefix)
      return false;
    unsigned long long Number;
    StringRef Tail = Lower.drop_front();
    return !Tail.consumeInteger(10, Number) && Tail.empty() && Number < Limit;
  };

  return IsNumbered('r', 16) || IsNumbered('d', 8) || IsNumbered('a', 8) ||
         IsNumbered('f', 16) || IsNumbered('v', 32) || IsNumbered('p', 16);
}

bool lookupRegisterSelector(unsigned Group, StringRef Name,
                            uint64_t &Encoding) {
  for (const RegisterSelector &Selector : BedrockRegisterSelectors) {
    if (Selector.Group != Group || Name != Selector.Name)
      continue;
    Encoding = Selector.Encoding;
    return true;
  }
  return false;
}
} // namespace llvm::BedrockMC

namespace {

const char *getCondName(unsigned Cond) {
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
    return nullptr;
  }
}

const char *getFullCondName(unsigned Cond) {
  switch (Cond) {
  case 0x0:
    return "t";
  case 0x1:
    return "f";
  default:
    return getCondName(Cond);
  }
}

const char *getRepCondName(unsigned Cond) {
  switch (Cond) {
  case 0x0:
    return "rep";
  case 0x2:
    return "repeq";
  case 0x3:
    return "repne";
  case 0x4:
    return "repult";
  case 0x5:
    return "repuge";
  case 0x6:
    return "repmi";
  case 0x7:
    return "reppl";
  case 0x8:
    return "repvs";
  case 0x9:
    return "repvc";
  case 0xa:
    return "repule";
  case 0xb:
    return "repugt";
  case 0xc:
    return "replt";
  case 0xd:
    return "repge";
  case 0xe:
    return "reple";
  case 0xf:
    return "repgt";
  default:
    return nullptr;
  }
}

const char *getMemoryOrderName(unsigned Order) {
  switch (Order) {
  case 0:
    return "relaxed";
  case 1:
    return "acquire";
  case 2:
    return "release";
  case 3:
    return "acqrel";
  case 4:
    return "seqcst";
  default:
    return nullptr;
  }
}

bool matchPattern(StringRef Pattern, uint32_t Payload);
unsigned extractPatternField(StringRef Pattern, uint32_t Payload, char Field);

bool isRepPayload(uint32_t Payload) {
  constexpr StringLiteral Pattern = "1110111110ccccrrrr";
  return matchPattern(Pattern, Payload) &&
         extractPatternField(Pattern, Payload, 'c') != 0x1;
}

const char *getSRegName(unsigned Reg) {
  static const char *Names[] = {"ds",  "ss",  "gs0", "gs1",
                                "gs2", "gs3", "gs4", "gs5"};
  return Reg < std::size(Names) ? Names[Reg] : nullptr;
}

int64_t signExtend(uint64_t Value, unsigned Bits) {
  uint64_t Sign = uint64_t(1) << (Bits - 1);
  return (Value ^ Sign) - Sign;
}

void appendText(SmallVectorImpl<char> &Text, const Twine &Value) {
  SmallString<32> Storage;
  Value.toVector(Storage);
  Text.append(Storage.begin(), Storage.end());
}

uint64_t readLE(ArrayRef<uint8_t> Bytes, unsigned Offset, unsigned Width) {
  uint64_t Value = 0;
  for (unsigned I = 0; I != Width; ++I)
    Value |= uint64_t(Bytes[Offset + I]) << (I * 8);
  return Value;
}

void appendSignedImm(SmallVectorImpl<char> &Text, ArrayRef<uint8_t> Tail,
                     unsigned Width) {
  uint64_t Raw = readLE(Tail, 0, Width);
  appendText(Text, Twine(signExtend(Raw, Width * 8)));
}

void appendUnsignedImm(SmallVectorImpl<char> &Text, ArrayRef<uint8_t> Tail,
                       unsigned Width) {
  appendText(Text, Twine(readLE(Tail, 0, Width)));
}

bool matchPattern(StringRef Pattern, uint32_t Payload) {
  uint32_t Mask = 0;
  uint32_t Value = 0;
  unsigned Width = Pattern.size();
  if (Width == 0 || Width > 32 || (Width < 32 && (Payload >> Width) != 0))
    return false;
  for (unsigned I = 0; I != Width; ++I) {
    char C = Pattern[I];
    if (C != '0' && C != '1')
      continue;
    unsigned Bit = Width - I - 1;
    Mask |= 1u << Bit;
    if (C == '1')
      Value |= 1u << Bit;
  }
  return (Payload & Mask) == Value;
}

unsigned extractPatternField(StringRef Pattern, uint32_t Payload, char Field) {
  unsigned Value = 0;
  unsigned Width = Pattern.size();
  for (unsigned I = 0; I != Width; ++I) {
    if (Pattern[I] != Field)
      continue;
    unsigned Bit = Width - I - 1;
    Value = (Value << 1) | ((Payload >> Bit) & 1);
  }
  return Value;
}

bool matchPattern64(StringRef Pattern, uint64_t Payload) {
  uint64_t Mask = 0;
  uint64_t Value = 0;
  unsigned Width = Pattern.size();
  if (Width == 0 || Width > 64 || (Width < 64 && (Payload >> Width) != 0))
    return false;
  for (unsigned I = 0; I != Width; ++I) {
    char C = Pattern[I];
    if (C != '0' && C != '1')
      continue;
    unsigned Bit = Width - I - 1;
    Mask |= uint64_t(1) << Bit;
    if (C == '1')
      Value |= uint64_t(1) << Bit;
  }
  return (Payload & Mask) == Value;
}

unsigned extractPatternField64(StringRef Pattern, uint64_t Payload,
                               char Field) {
  unsigned Value = 0;
  unsigned Width = Pattern.size();
  for (unsigned I = 0; I != Width; ++I) {
    if (Pattern[I] != Field)
      continue;
    unsigned Bit = Width - I - 1;
    Value = (Value << 1) | ((Payload >> Bit) & 1);
  }
  return Value;
}

bool decodeCompactEA(uint8_t EA, ArrayRef<uint8_t> Tail, unsigned &Consumed,
                     SmallString<64> &Text);
bool decodeCompactFEA(uint8_t EA, ArrayRef<uint8_t> Tail, unsigned &Consumed,
                      SmallString<64> &Text, bool AllowImmediate);
bool isCompactEAImmediate(uint8_t EA);

bool decodeScalarTablePayload(uint64_t Payload, unsigned PatternWidth,
                              ArrayRef<uint8_t> Tail,
                              SmallString<128> &Text,
                              bool *PreserveEncodingWidth = nullptr) {
  for (const BedrockMC::ScalarEncodingForm &Form :
       BedrockMC::scalarEncodingForms()) {
    StringRef Pattern(Form.Pattern);
    if (Pattern.size() != PatternWidth || !matchPattern64(Pattern, Payload))
      continue;

    if (Tail.size() < Form.FixedPayloadBytes)
      continue;
    SmallString<48> Mnemonic(Form.Mnemonic);
    if (Form.ConditionField != '\0') {
      unsigned Condition = extractPatternField64(
          Pattern, Payload, static_cast<char>(Form.ConditionField));
      if (Condition >= 16 ||
          (Form.AllowedConditionMask & (1u << Condition)) == 0)
        continue;
      const char *Name = getFullCondName(Condition);
      if (!Name)
        continue;
      Mnemonic += Name;
    }
    if (Form.SuffixField != '\0') {
      unsigned Suffix = extractPatternField64(
          Pattern, Payload, static_cast<char>(Form.SuffixField));
      StringRef Suffixes(Form.Suffixes);
      if (Suffix >= Suffixes.size() || Suffix >= 16 ||
          Suffixes[Suffix] == '?' ||
          (Form.AllowedSuffixMask & (1u << Suffix)) == 0)
        continue;
      Mnemonic += '.';
      Mnemonic += Suffixes[Suffix];
    }

    SmallString<96> Operands;
    unsigned ExplicitValues[4] = {};
    int64_t SemanticValues[4] = {};
    SmallString<64> DecodedEAs[4];
    unsigned TailOffset = 0;
    bool Valid = true;
    for (unsigned I = 0; I != Form.OperandCount; ++I) {
      BedrockMC::ScalarOperandDesc Desc = Form.operand(I);
      if (Desc.Kind != BedrockMC::ScalarOperandKind::EA &&
          Desc.Kind != BedrockMC::ScalarOperandKind::FEA)
        continue;
      unsigned Value = extractPatternField64(Pattern, Payload, Desc.Field);
      unsigned Consumed = 0;
      bool Decoded =
          Desc.Kind == BedrockMC::ScalarOperandKind::FEA
              ? decodeCompactFEA(Value, Tail.drop_front(TailOffset), Consumed,
                                 DecodedEAs[I], Desc.AllowImmediateEA)
              : decodeCompactEA(Value, Tail.drop_front(TailOffset), Consumed,
                                DecodedEAs[I]);
      if (Decoded && Desc.Kind == BedrockMC::ScalarOperandKind::EA &&
          !Desc.AllowImmediateEA && isCompactEAImmediate(Value))
        Decoded = false;
      if (!Decoded || !Desc.allows(Value)) {
        Valid = false;
        break;
      }
      TailOffset += Consumed;
    }
    if (!Valid)
      continue;
    for (unsigned I = 0; I != Form.OperandCount; ++I) {
      BedrockMC::ScalarOperandDesc Desc = Form.operand(I);
      unsigned Value = Desc.Field == '\0'
                           ? 0
                           : extractPatternField64(Pattern, Payload, Desc.Field);
      ExplicitValues[I] = Value;
      SemanticValues[I] = Value;
      if (Desc.Field != '\0' && !Desc.allows(Value)) {
        Valid = false;
        break;
      }
      if (!Operands.empty())
        Operands += ", ";
      switch (Desc.Kind) {
      case BedrockMC::ScalarOperandKind::GPR:
        appendText(Operands, formatv("r{0}", Value).str());
        break;
      case BedrockMC::ScalarOperandKind::FPR:
        appendText(Operands, formatv("f{0}", Value).str());
        break;
      case BedrockMC::ScalarOperandKind::Segment: {
        const char *Name = getSRegName(Value);
        if (!Name) {
          Valid = false;
          break;
        }
        Operands += Name;
        break;
      }
      case BedrockMC::ScalarOperandKind::EA:
      case BedrockMC::ScalarOperandKind::FEA:
        Operands += DecodedEAs[I];
        break;
      case BedrockMC::ScalarOperandKind::Immediate:
        if (Desc.Signed) {
          SemanticValues[I] = signExtend(Value, Desc.Width);
          appendText(Operands, Twine(SemanticValues[I]));
        } else
          appendText(Operands, Twine(Value));
        break;
      case BedrockMC::ScalarOperandKind::MemoryOrder: {
        const char *Order = getMemoryOrderName(Value);
        if (!Order) {
          Valid = false;
          break;
        }
        Mnemonic += '/';
        Mnemonic += Order;
        break;
      }
      case BedrockMC::ScalarOperandKind::TailSigned:
      case BedrockMC::ScalarOperandKind::TailUnsigned: {
        unsigned Width = Desc.Width / 8;
        if (Width == 0 || TailOffset + Width > Tail.size()) {
          Valid = false;
          break;
        }
        uint64_t Raw = readLE(Tail, TailOffset, Width);
        TailOffset += Width;
        if (Desc.Kind == BedrockMC::ScalarOperandKind::TailSigned) {
          SemanticValues[I] = signExtend(Raw, Desc.Width);
          appendText(Operands, Twine(SemanticValues[I]));
        } else {
          SemanticValues[I] = Raw;
          appendText(Operands, Twine(Raw));
        }
        break;
      }
      case BedrockMC::ScalarOperandKind::RegisterSelector: {
        unsigned Width = Desc.Width / 8;
        if (Width == 0 || TailOffset + Width > Tail.size()) {
          Valid = false;
          break;
        }
        uint64_t Raw = readLE(Tail, TailOffset, Width);
        TailOffset += Width;
        SemanticValues[I] = Raw;
        appendText(Operands, Twine(Raw));
        break;
      }
      case BedrockMC::ScalarOperandKind::FixedSP:
        Operands += "sp";
        break;
      case BedrockMC::ScalarOperandKind::FixedCS:
        Operands += "cs";
        break;
      case BedrockMC::ScalarOperandKind::FixedImmediate:
        appendText(Operands, Twine(Desc.FixedValue));
        break;
      default:
        Valid = false;
        break;
      }
      if (!Valid)
        break;
    }
    if (!Valid || TailOffset != Tail.size())
      continue;
    if (Form.distinctOperandA() >= 0 && Form.distinctOperandB() >= 0 &&
        ExplicitValues[Form.distinctOperandA()] ==
            ExplicitValues[Form.distinctOperandB()])
      continue;

    Text = Mnemonic;
    if (!Operands.empty()) {
      Text += '\t';
      Text += Operands;
    }
    if (PreserveEncodingWidth) {
      auto PrimaryBytes = [](unsigned Width) -> unsigned {
        switch (Width) {
        case 7: return 1;
        case 14: return 2;
        case 18: return 3;
        case 26: return 4;
        case 34: return 5;
        case 42: return 6;
        default: return UINT_MAX;
        }
      };
      unsigned CurrentBytes = PrimaryBytes(PatternWidth) + Form.FixedPayloadBytes;
      for (const BedrockMC::ScalarEncodingForm &Candidate :
           BedrockMC::scalarEncodingForms()) {
        unsigned CandidateBytes =
            PrimaryBytes(StringRef(Candidate.Pattern).size()) +
            Candidate.FixedPayloadBytes;
        if (CandidateBytes >= CurrentBytes || Candidate.OperandCount != Form.OperandCount ||
            (Candidate.ConditionField == '\0') !=
                (Form.ConditionField == '\0'))
          continue;
        SmallString<48> CandidateMnemonic(Candidate.Mnemonic);
        if (Candidate.ConditionField != '\0') {
          unsigned CurrentCondition = extractPatternField64(
              Pattern, Payload, static_cast<char>(Form.ConditionField));
          const char *ConditionName = getFullCondName(CurrentCondition);
          if (!ConditionName ||
              (Candidate.AllowedConditionMask & (1u << CurrentCondition)) == 0)
            continue;
          CandidateMnemonic += ConditionName;
        }
        if (Candidate.SuffixField == '\0') {
          if (CandidateMnemonic != Mnemonic)
            continue;
        } else {
          CandidateMnemonic += '.';
          if (Mnemonic.size() != CandidateMnemonic.size() + 1 ||
              !Mnemonic.starts_with(CandidateMnemonic))
            continue;
          size_t CandidateSuffix = StringRef(Candidate.Suffixes).find(Mnemonic.back());
          if (CandidateSuffix == StringRef::npos || CandidateSuffix >= 16 ||
              (Candidate.AllowedSuffixMask & (1u << CandidateSuffix)) == 0)
            continue;
        }
        bool CandidateValid = true;
        for (unsigned I = 0; I != Candidate.OperandCount; ++I) {
          auto SourceDesc = Form.operand(I);
          auto CandidateDesc = Candidate.operand(I);
          bool BothTails =
              (SourceDesc.Kind == BedrockMC::ScalarOperandKind::TailSigned ||
               SourceDesc.Kind == BedrockMC::ScalarOperandKind::TailUnsigned) &&
              (CandidateDesc.Kind == BedrockMC::ScalarOperandKind::TailSigned ||
               CandidateDesc.Kind == BedrockMC::ScalarOperandKind::TailUnsigned);
          if ((!BothTails && SourceDesc.Kind != CandidateDesc.Kind) ||
              SourceDesc.FixedValue != CandidateDesc.FixedValue) {
            CandidateValid = false;
            break;
          }
          int64_t Value = SemanticValues[I];
          bool Signed = CandidateDesc.Signed ||
                        CandidateDesc.Kind ==
                            BedrockMC::ScalarOperandKind::TailSigned;
          if (CandidateDesc.Kind != BedrockMC::ScalarOperandKind::EA &&
              CandidateDesc.Kind != BedrockMC::ScalarOperandKind::FEA &&
              CandidateDesc.Width != 0 &&
              (Signed ? !isIntN(CandidateDesc.Width, Value)
                      : (Value < 0 || !isUIntN(CandidateDesc.Width, Value)))) {
            CandidateValid = false;
            break;
          }
          if (CandidateDesc.Field != '\0') {
            unsigned Encoded = static_cast<unsigned>(Value) &
                               ((1u << CandidateDesc.Width) - 1);
            if (!CandidateDesc.allows(Encoded)) {
              CandidateValid = false;
              break;
            }
          }
        }
        if (CandidateValid) {
          *PreserveEncodingWidth = true;
          break;
        }
      }
    }
    return true;
  }
  return false;
}

void appendSignedOffset(SmallVectorImpl<char> &Text, int64_t Value) {
  if (Value < 0)
    appendText(Text, formatv(" - {0}", -Value).str());
  else
    appendText(Text, formatv(" + {0}", Value).str());
}

void appendExt0Index(SmallVectorImpl<char> &Text, unsigned Mode, unsigned Reg) {
  switch (Mode) {
  case 0:
    appendText(Text, formatv("r{0}++", Reg).str());
    return;
  case 1:
    appendText(Text, formatv("--r{0}", Reg).str());
    return;
  case 2:
    appendText(Text, formatv("r{0}", Reg).str());
    return;
  default:
    llvm_unreachable("unknown Bedrock EXT0 index mode");
  }
}

[[maybe_unused]] bool decodePrefixByte(uint8_t Prefix, SmallString<32> &Text) {
  Text.clear();
  switch (Prefix) {
  case 0x00:
    return true;
  case 0x01:
    Text = "nospec";
    return true;
  case 0x02:
    Text = "saturate";
    return true;
  case 0x03:
    Text = "nontemporal";
    return true;
  case 0x08:
    Text = "u2c";
    return true;
  case 0x09:
    Text = "c2u";
    return true;
  case 0x0a:
    Text = "u2u";
    return true;
  default:
    break;
  }

  if ((Prefix & 0x80) == 0)
    return false;

  unsigned Cond = (Prefix >> 3) & 0xf;
  unsigned Reg = Prefix & 0x7;
  const char *Name = getRepCondName(Cond);
  if (!Name)
    return false;

  Text = formatv("{0} r{1}", Name, Reg).str();
  return true;
}

bool decodeCompactEA(uint8_t EA, ArrayRef<uint8_t> Tail, unsigned &Consumed,
                     SmallString<64> &Text) {
  Consumed = 0;

  if (EA <= 0x0f) {
    Text = formatv("[r{0}]", EA).str();
    return true;
  }

  if (EA >= 0x10 && EA <= 0x4f) {
    unsigned WidthCode = (EA >> 4) - 1;
    unsigned Width = 1u << WidthCode;
    if (Tail.size() < Width)
      return false;
    int64_t Disp = Width == 8 ? static_cast<int64_t>(readLE(Tail, 0, Width))
                              : signExtend(readLE(Tail, 0, Width), Width * 8);
    Text = formatv("[r{0}", EA & 0xf).str();
    appendSignedOffset(Text, Disp);
    Text += "]";
    Consumed = Width;
    return true;
  }

  if (EA >= 0x50 && EA <= 0x57) {
    unsigned Width = 1u << (EA & 0x3);
    if (Tail.size() < Width)
      return false;
    bool IsPC = EA >= 0x54;
    int64_t Disp = Width == 8 ? static_cast<int64_t>(readLE(Tail, 0, Width))
                              : signExtend(readLE(Tail, 0, Width), Width * 8);
    Text = IsPC ? "[pc" : "[sp";
    appendSignedOffset(Text, Disp);
    Text += "]";
    Consumed = Width;
    return true;
  }

  if (EA == 0x58) {
    Text = "[sp]";
    return true;
  }
  if (EA == 0x59 || EA == 0x5a) {
    unsigned Width = EA == 0x59 ? 4 : 8;
    if (Tail.size() < Width)
      return false;
    Text = "[";
    if (EA == 0x59)
      appendSignedImm(Text, Tail, Width);
    else
      appendUnsignedImm(Text, Tail, Width);
    Text += "]";
    Consumed = Width;
    return true;
  }
  if (EA >= 0x5b && EA <= 0x5e) {
    unsigned Width = 1u << (EA - 0x5b);
    if (Tail.size() < Width)
      return false;
    if (EA == 0x5e)
      appendUnsignedImm(Text, Tail, Width);
    else
      appendSignedImm(Text, Tail, Width);
    Consumed = Width;
    return true;
  }

  if (EA >= 0x5f && EA <= 0x68) {
    bool IsExt2 = EA >= 0x64;
    unsigned FamilyBase = IsExt2 ? 0x64 : 0x5f;
    unsigned DispWidth = EA == FamilyBase + 4 ? 0 : 1u << (EA - FamilyBase);
    unsigned RequiredDescriptorBytes = IsExt2 ? 2 : 1;
    if (Tail.empty())
      return false;

    auto Finish = [&](unsigned DescriptorBytes) -> bool {
      if (Tail.size() < DescriptorBytes + DispWidth)
        return false;
      if (DispWidth) {
        int64_t Disp =
            DispWidth == 8
                ? static_cast<int64_t>(readLE(Tail, DescriptorBytes, DispWidth))
                : signExtend(readLE(Tail, DescriptorBytes, DispWidth),
                             DispWidth * 8);
        appendSignedOffset(Text, Disp);
      }
      Text += "]";
      Consumed = DescriptorBytes + DispWidth;
      return true;
    };

    uint8_t D0 = Tail[0];
    if ((D0 & 0x87) == 0x84 || (D0 & 0x87) == 0x85) {
      if (RequiredDescriptorBytes != 1)
        return false;
      unsigned Base = (D0 >> 3) & 0xf;
      if ((D0 & 0x7) == 0x4)
        Text = formatv("[r{0}++", Base).str();
      else
        Text = formatv("[--r{0}", Base).str();
      return Finish(1);
    }

    if (D0 == 0x8a || D0 == 0x8b) {
      if (RequiredDescriptorBytes != 2)
        return false;
      if (Tail.size() < 2)
        return false;
      unsigned Mode = Tail[1] >> 4;
      unsigned Index = Tail[1] & 0xf;
      if (Mode > 2)
        return false;
      Text = D0 == 0x8a ? "[sp + " : "[pc + ";
      appendExt0Index(Text, Mode, Index);
      return Finish(2);
    }

    if ((D0 & 0x80) == 0) {
      if (RequiredDescriptorBytes != 1)
        return false;
      unsigned SReg = (D0 >> 4) & 0x7;
      unsigned Base = D0 & 0xf;
      const char *SRegName = getSRegName(SReg);
      if (!SRegName)
        return false;
      Text = formatv("[{0}:r{1}", SRegName, Base).str();
      return Finish(1);
    }

    unsigned SReg = (D0 >> 4) & 0x7;
    unsigned Submode = D0 & 0xf;
    const char *SRegName = getSRegName(SReg);
    if (!SRegName)
      return false;

    switch (Submode) {
    case 0:
    case 1:
    case 2: {
      if (RequiredDescriptorBytes != 2)
        return false;
      if (Tail.size() < 2)
        return false;
      unsigned Base = Tail[1] >> 4;
      unsigned Index = Tail[1] & 0xf;
      Text = formatv("[{0}:r{1} + ", SRegName, Base).str();
      appendExt0Index(Text, Submode, Index);
      return Finish(2);
    }
    case 3:
      if (RequiredDescriptorBytes != 1)
        return false;
      Text = formatv("[{0}:0", SRegName).str();
      return Finish(1);
    case 8: {
      if (RequiredDescriptorBytes != 2)
        return false;
      if (Tail.size() < 2)
        return false;
      unsigned Base = Tail[1] >> 4;
      unsigned Mode = Tail[1] & 0xf;
      if (Mode == 0)
        Text = formatv("[{0}:r{1}++", SRegName, Base).str();
      else if (Mode == 1)
        Text = formatv("[{0}:--r{1}", SRegName, Base).str();
      else
        return false;
      return Finish(2);
    }
    case 9: {
      if (RequiredDescriptorBytes != 2)
        return false;
      if (Tail.size() < 2)
        return false;
      unsigned Mode = Tail[1] >> 4;
      unsigned Index = Tail[1] & 0xf;
      if (Mode > 2)
        return false;
      Text = formatv("[{0}:0 + ", SRegName).str();
      appendExt0Index(Text, Mode, Index);
      return Finish(2);
    }
    default:
      return false;
    }
  }

  return false;
}

bool decodeCompactFEA(uint8_t EA, ArrayRef<uint8_t> Tail, unsigned &Consumed,
                      SmallString<64> &Text, bool AllowImmediate) {
  if (EA == 0x58 || EA == 0x5b || EA == 0x5c)
    return false;
  if (EA == 0x5d || EA == 0x5e) {
    if (!AllowImmediate)
      return false;
    unsigned Width = EA == 0x5d ? 4 : 8;
    if (Tail.size() < Width)
      return false;
    uint64_t Bits = readLE(Tail, 0, Width);
    Text = EA == 0x5d ? formatv("immsf({0:x8})", Bits).str()
                      : formatv("immdf({0:x16})", Bits).str();
    Consumed = Width;
    return true;
  }
  return decodeCompactEA(EA, Tail, Consumed, Text);
}

bool decodeVectorEA(uint8_t EA, ArrayRef<uint8_t> Tail, unsigned &Consumed,
                    SmallString<64> &Text) {
  if (EA != 0x58 && !(EA >= 0x5b && EA <= 0x5e))
    return decodeCompactEA(EA, Tail, Consumed, Text);

  unsigned DispWidth = EA == 0x58 ? 0 : 1u << (EA - 0x5b);
  if (Tail.size() < 1 + DispWidth)
    return false;
  unsigned Base = Tail[0] >> 4;
  unsigned Stride = Tail[0] & 0xf;
  Text = formatv("[r{0} + r{1} * lane", Base, Stride).str();
  if (DispWidth) {
    int64_t Disp = DispWidth == 8
                       ? static_cast<int64_t>(readLE(Tail, 1, DispWidth))
                       : signExtend(readLE(Tail, 1, DispWidth), DispWidth * 8);
    appendSignedOffset(Text, Disp);
  }
  Text += "]";
  Consumed = 1 + DispWidth;
  return true;
}

bool isCompactEAImmediate(uint8_t EA) { return EA >= 0x5b && EA <= 0x5e; }

} // end anonymous namespace

void BedrockMC::createRawInst(ArrayRef<uint8_t> Bytes, MCInst &Inst) {
  Inst.clear();
  Inst.setOpcode(Bedrock::RAW);
  for (uint8_t Byte : Bytes)
    Inst.addOperand(MCOperand::createImm(Byte));
}

bool BedrockMC::getRawInstBytes(const MCInst &Inst,
                                SmallVectorImpl<uint8_t> &Bytes) {
  if (Inst.getOpcode() != Bedrock::RAW && Inst.getOpcode() != Bedrock::RAW_EXPR)
    return false;

  Bytes.clear();
  unsigned FirstByteOp = 0;
  if (Inst.getOpcode() == Bedrock::RAW_EXPR) {
    if (Inst.getNumOperands() == 0 || !Inst.getOperand(0).isImm())
      return false;
    FirstByteOp = 1 + Inst.getOperand(0).getImm() * 3;
    if (FirstByteOp > Inst.getNumOperands())
      return false;
  }

  for (unsigned I = FirstByteOp; I != Inst.getNumOperands(); ++I) {
    const MCOperand &Op = Inst.getOperand(I);
    if (!Op.isImm() || Op.getImm() < 0 || Op.getImm() > 0xff)
      return false;
    Bytes.push_back(static_cast<uint8_t>(Op.getImm()));
  }
  return true;
}

bool BedrockMC::encodeExtraShort(uint8_t Payload,
                                 SmallVectorImpl<uint8_t> &Bytes) {
  if (Payload >= 0x80)
    return false;
  Bytes.push_back(Payload);
  return true;
}

bool BedrockMC::encodeShort(uint16_t Payload, SmallVectorImpl<uint8_t> &Bytes) {
  if (Payload >= (1u << 14))
    return false;
  Bytes.push_back(0x80 | ((Payload >> 8) & 0x3f));
  Bytes.push_back(Payload & 0xff);
  return true;
}

bool BedrockMC::encodeMedium(uint32_t Payload, ArrayRef<uint8_t> Tail,
                             SmallVectorImpl<uint8_t> &Bytes) {
  unsigned TotalBytes = 3 + Tail.size();
  if (Payload >= (1u << 18) || (Payload >> 14) == 0xf || TotalBytes > 18)
    return false;

  Bytes.push_back(0xc0 | ((TotalBytes - 3) << 2) | ((Payload >> 16) & 0x3));
  Bytes.push_back((Payload >> 8) & 0xff);
  Bytes.push_back(Payload & 0xff);
  Bytes.append(Tail.begin(), Tail.end());
  return true;
}

bool BedrockMC::encodeLong(uint32_t Payload, ArrayRef<uint8_t> Tail,
                           SmallVectorImpl<uint8_t> &Bytes) {
  unsigned TotalBytes = 4 + Tail.size();
  unsigned Selector6 = Payload >> 20;
  if (Payload >= (1u << 26) || Selector6 < 0x3c || Selector6 > 0x3e ||
      TotalBytes > 18)
    return false;

  Bytes.push_back(0xc0 | ((TotalBytes - 3) << 2) | ((Payload >> 24) & 0x3));
  Bytes.push_back((Payload >> 16) & 0xff);
  Bytes.push_back((Payload >> 8) & 0xff);
  Bytes.push_back(Payload & 0xff);
  Bytes.append(Tail.begin(), Tail.end());
  return true;
}

bool BedrockMC::encodeExtraLong(uint64_t Payload, ArrayRef<uint8_t> Tail,
                                SmallVectorImpl<uint8_t> &Bytes) {
  unsigned TotalBytes = 5 + Tail.size();
  unsigned Selector6 = Payload >> 28;
  if (Payload >= (uint64_t(1) << 34) || Selector6 != 0x3f || TotalBytes > 18)
    return false;

  Bytes.push_back(0xc0 | ((TotalBytes - 3) << 2) | ((Payload >> 32) & 0x3));
  Bytes.push_back((Payload >> 24) & 0xff);
  Bytes.push_back((Payload >> 16) & 0xff);
  Bytes.push_back((Payload >> 8) & 0xff);
  Bytes.push_back(Payload & 0xff);
  Bytes.append(Tail.begin(), Tail.end());
  return true;
}

bool BedrockMC::encodeXxlong(uint64_t Payload, ArrayRef<uint8_t> Tail,
                             SmallVectorImpl<uint8_t> &Bytes) {
  unsigned TotalBytes = 6 + Tail.size();
  if (Payload >= (uint64_t(1) << 42) || (Payload >> 34) != 0xff ||
      TotalBytes > 18)
    return false;

  Bytes.push_back(0xc0 | ((TotalBytes - 3) << 2) | ((Payload >> 40) & 0x3));
  Bytes.push_back((Payload >> 32) & 0xff);
  Bytes.push_back((Payload >> 24) & 0xff);
  Bytes.push_back((Payload >> 16) & 0xff);
  Bytes.push_back((Payload >> 8) & 0xff);
  Bytes.push_back(Payload & 0xff);
  Bytes.append(Tail.begin(), Tail.end());
  return true;
}

namespace {

bool decodeVectorPayload(BedrockMC::VectorEncodingClass EncodingClass,
                         uint64_t Payload, ArrayRef<uint8_t> Tail,
                         SmallString<128> &Text,
                         bool *PreserveEncodingWidth = nullptr) {
  using BedrockMC::VectorOperandKind;
  for (const BedrockMC::VectorEncodingForm &Form :
       BedrockMC::vectorEncodingForms()) {
    if (Form.encodingClass() != EncodingClass ||
        !matchPattern64(Form.Pattern, Payload))
      continue;

    unsigned Suffix = 0;
    if (Form.SuffixField != '\0') {
      Suffix = extractPatternField64(Form.Pattern, Payload, Form.SuffixField);
      if (Suffix >= 8 || (Form.AllowedSuffixMask & (1u << Suffix)) == 0 ||
          Suffix >= StringRef(Form.Suffixes).size() ||
          Form.Suffixes[Suffix] == '?')
        continue;
    }

    SmallString<48> Mnemonic(Form.Mnemonic);
    if (Form.HasCondition) {
      unsigned ConditionValue =
          extractPatternField64(Form.Pattern, Payload, 'c');
      if ((Form.AllowedConditionMask & (1u << ConditionValue)) == 0)
        continue;
      const char *Condition = getFullCondName(ConditionValue);
      if (!Condition)
        continue;
      Mnemonic += Condition;
    }
    if (Form.SuffixField != '\0') {
      Mnemonic += '.';
      Mnemonic += Form.Suffixes[Suffix];
    }

    StringRef VectorMnemonic(Form.Mnemonic);
    bool IsGather1 = VectorMnemonic.starts_with("vgather1");
    bool IsScatter1 = VectorMnemonic.starts_with("vscatter1");
    if (IsGather1 || IsScatter1) {
      StringRef Pattern(Form.Pattern);
      unsigned Predicate = extractPatternField64(Pattern, Payload, 'p');
      unsigned Vector = extractPatternField64(Pattern, Payload, 'v');
      unsigned Cursor = extractPatternField64(Pattern, Payload, 'i');
      SmallString<96> Address;
      if (Pattern.contains('s')) {
        Address = formatv("[r{0} + r{1} * r{2}]",
                          extractPatternField64(Pattern, Payload, 'b'), Cursor,
                          extractPatternField64(Pattern, Payload, 's'))
                      .str();
      } else if (!Pattern.contains('b')) {
        Address = formatv("[v{0}[r{1}]]",
                          extractPatternField64(Pattern, Payload, 'x'), Cursor)
                      .str();
      } else {
        unsigned Base = extractPatternField64(Pattern, Payload, 'b');
        unsigned VectorAddress = extractPatternField64(Pattern, Payload, 'x');
        bool Unscaled =
            Pattern == "111111110000010010zzbbbbppppiiiivvvvvxxxxx" ||
            Pattern == "111111110000011010zzbbbbppppiiiivvvvvxxxxx";
        Address =
            formatv("[r{0} + v{1}[r{2}]", Base, VectorAddress, Cursor).str();
        if (!Unscaled)
          Address += formatv(" * {0}", 1u << Suffix).str();

        bool HasTailOperand = false;
        for (unsigned I = 0; I != Form.OperandCount; ++I) {
          const BedrockMC::VectorOperandDesc Operand = Form.operand(I);
          if (Operand.Kind != VectorOperandKind::TailSigned &&
              Operand.Kind != VectorOperandKind::TailUnsigned)
            continue;
          HasTailOperand = true;
          unsigned Width = Operand.Width / 8;
          if (Tail.size() != Width)
            return false;
          uint64_t Raw = readLE(Tail, 0, Width);
          if (Operand.Kind == VectorOperandKind::TailSigned) {
            int64_t Value = signExtend(Raw, Operand.Width);
            appendSignedOffset(Address, Value);
            if (PreserveEncodingWidth) {
              for (unsigned NarrowerWidth = 1; NarrowerWidth < Width;
                   NarrowerWidth <<= 1) {
                if (isIntN(NarrowerWidth * 8, Value)) {
                  *PreserveEncodingWidth = true;
                  break;
                }
              }
            }
          } else {
            Address += formatv(" + {0}", Raw).str();
            if (PreserveEncodingWidth) {
              for (unsigned NarrowerWidth = 1; NarrowerWidth < Width;
                   NarrowerWidth <<= 1) {
                if (isUIntN(NarrowerWidth * 8, Raw)) {
                  *PreserveEncodingWidth = true;
                  break;
                }
              }
            }
          }
        }
        if (!HasTailOperand && !Tail.empty())
          return false;
        Address += ']';
      }
      if ((!Pattern.contains('b') || Pattern.contains('s')) && !Tail.empty())
        return false;
      if (IsGather1)
        Text = formatv("{0}\tp{1}, {2}, v{3}", Mnemonic, Predicate, Address,
                       Vector)
                   .str();
      else
        Text = formatv("{0}\tp{1}, v{2}, {3}", Mnemonic, Predicate, Vector,
                       Address)
                   .str();
      return true;
    }

    SmallString<96> Operands;
    unsigned TailOffset = 0;
    SmallVector<unsigned, 5> ExplicitValues;
    bool Valid = true;
    for (unsigned I = 0; I != Form.OperandCount; ++I) {
      const BedrockMC::VectorOperandDesc Operand = Form.operand(I);
      if (Operand.Kind == VectorOperandKind::Condition)
        continue;
      if (!Operands.empty())
        Operands += ", ";

      unsigned Value =
          Operand.Field == '\0'
              ? 0
              : extractPatternField64(Form.Pattern, Payload, Operand.Field);
      ExplicitValues.push_back(Value);
      switch (Operand.Kind) {
      case VectorOperandKind::None:
      case VectorOperandKind::Condition:
        llvm_unreachable("invalid explicit vector operand kind");
      case VectorOperandKind::GPR:
        appendText(Operands, formatv("r{0}", Value).str());
        break;
      case VectorOperandKind::FPR:
        appendText(Operands, formatv("f{0}", Value).str());
        break;
      case VectorOperandKind::Vector:
        appendText(Operands, formatv("v{0}", Value).str());
        break;
      case VectorOperandKind::Predicate:
        appendText(Operands, formatv("p{0}", Value).str());
        break;
      case VectorOperandKind::EA: {
        if (!Operand.AllowImmediateEA && isCompactEAImmediate(Value)) {
          Valid = false;
          break;
        }
        unsigned Consumed = 0;
        SmallString<64> EAText;
        if (!decodeCompactEA(Value, Tail.drop_front(TailOffset), Consumed,
                             EAText)) {
          Valid = false;
          break;
        }
        TailOffset += Consumed;
        Operands += EAText;
        break;
      }
      case VectorOperandKind::VEA: {
        unsigned Consumed = 0;
        SmallString<64> EAText;
        if (!decodeVectorEA(Value, Tail.drop_front(TailOffset), Consumed,
                            EAText)) {
          Valid = false;
          break;
        }
        TailOffset += Consumed;
        Operands += EAText;
        break;
      }
      case VectorOperandKind::Immediate:
        appendText(Operands, Twine(Value));
        break;
      case VectorOperandKind::TailSigned:
      case VectorOperandKind::TailUnsigned: {
        unsigned Width = Operand.Width / 8;
        if (TailOffset > Tail.size() || Tail.size() - TailOffset < Width) {
          Valid = false;
          break;
        }
        uint64_t Raw = readLE(Tail, TailOffset, Width);
        TailOffset += Width;
        if (Operand.Kind == VectorOperandKind::TailSigned)
          appendText(Operands, Twine(signExtend(Raw, Operand.Width)));
        else
          appendText(Operands, Twine(Raw));
        break;
      }
      }
      if (!Valid)
        break;
    }
    if (!Valid || TailOffset != Tail.size())
      continue;
    if (Form.distinctOperandA() >= 0 && Form.distinctOperandB() >= 0 &&
        ExplicitValues[Form.distinctOperandA()] ==
            ExplicitValues[Form.distinctOperandB()])
      continue;

    Text = Mnemonic;
    if (!Operands.empty()) {
      Text += '\t';
      Text += Operands;
    }
    return true;
  }
  return false;
}

} // end anonymous namespace

bool BedrockMC::getInstructionSize(ArrayRef<uint8_t> Bytes, uint64_t &Size) {
  if (Bytes.empty()) {
    Size = 0;
    return false;
  }

  uint8_t Byte0 = Bytes[0];
  if ((Byte0 & 0x80) == 0) {
    Size = 1;
    return true;
  }
  if ((Byte0 & 0xc0) == 0x80) {
    Size = 2;
    return Bytes.size() >= Size;
  }

  Size = 3 + ((Byte0 >> 2) & 0xf);
  return Bytes.size() >= Size;
}

static void appendLengthAnnotation(SmallString<128> &Text, uint64_t Size,
                                   ArrayRef<uint8_t> PaddingBytes) {
  Text += formatv(" : LEN {0}", Size).str();
  if (llvm::all_of(PaddingBytes, [](uint8_t Byte) { return Byte == 0; }))
    return;

  constexpr char HexDigits[] = "0123456789abcdef";
  for (uint8_t Byte : PaddingBytes) {
    Text += ", ";
    if (Byte == 0) {
      Text += "ILLEGAL";
    } else if (Byte == 1) {
      Text += "NOP";
    } else {
      char Literal[] = {'0', 'x', HexDigits[Byte >> 4],
                        HexDigits[Byte & 0xf], '\0'};
      Text += Literal;
    }
  }
}

bool BedrockMC::decodeRawInst(ArrayRef<uint8_t> Bytes, uint64_t &Size,
                              SmallString<128> &Text) {
  if (!getInstructionSize(Bytes, Size))
    return false;

  uint8_t Byte0 = Bytes[0];
  if ((Byte0 & 0x80) == 0) {
    if (!decodeScalarTablePayload(Byte0 & 0x7f, 7, {}, Text))
      return false;
    return true;
  }

  if ((Byte0 & 0xc0) == 0x80) {
    uint16_t Payload = ((Byte0 & 0x3f) << 8) | Bytes[1];
    if (!decodeScalarTablePayload(Payload, 14, {}, Text))
      return false;
    return true;
  }

  ArrayRef<uint8_t> Inst = Bytes.take_front(Size);
  if (Inst.size() < 3)
    return false;

  uint32_t FirstTen = ((Byte0 & 0x3) << 8) | Inst[1];
  uint32_t MediumPayload = (FirstTen << 8) | Inst[2];
  unsigned Selector6 = MediumPayload >> 12;

  if (isRepPayload(MediumPayload)) {
    ArrayRef<uint8_t> BodyBytes = Bytes.drop_front(Size);
    uint64_t BodySize = 0;
    SmallString<128> BodyText;
    if (BodyBytes.empty() || !decodeRawInst(BodyBytes, BodySize, BodyText))
      return false;

    constexpr StringLiteral RepPattern = "1110111110ccccrrrr";
    unsigned Cond = extractPatternField(RepPattern, MediumPayload, 'c');
    unsigned Reg = extractPatternField(RepPattern, MediumPayload, 'r');
    const char *Name = getRepCondName(Cond);
    if (!Name)
      return false;

    Text = formatv("{0}\tr{1}, ({2})", Name, Reg, BodyText).str();
    if (Size > 3)
      appendLengthAnnotation(Text, Size, Inst.drop_front(3));
    Size += BodySize;
    return true;
  }

  unsigned OpcodeLength = 3;
  if ((FirstTen >> 2) == 0xff)
    OpcodeLength = 6;
  else if (Selector6 == 0x3f)
    OpcodeLength = 5;
  else if (Selector6 >= 0x3c)
    OpcodeLength = 4;
  if (Inst.size() < OpcodeLength)
    return false;

  uint64_t Payload = MediumPayload;
  if (OpcodeLength == 4)
    Payload = (uint64_t(FirstTen) << 16) | (uint64_t(Inst[2]) << 8) |
              uint64_t(Inst[3]);
  else if (OpcodeLength == 5)
    Payload = (uint64_t(FirstTen) << 24) | (uint64_t(Inst[2]) << 16) |
              (uint64_t(Inst[3]) << 8) | uint64_t(Inst[4]);
  else if (OpcodeLength == 6)
    Payload = (uint64_t(FirstTen) << 32) | (uint64_t(Inst[2]) << 24) |
              (uint64_t(Inst[3]) << 16) | (uint64_t(Inst[4]) << 8) |
              uint64_t(Inst[5]);

  auto DecodeWithTail = [&](ArrayRef<uint8_t> Tail,
                            SmallString<128> &CandidateText,
                            bool &PreserveEncodingWidth) {
    if (OpcodeLength == 6)
      return decodeScalarTablePayload(Payload, 42, Tail, CandidateText,
                                      &PreserveEncodingWidth) ||
             decodeVectorPayload(VectorEncodingClass::Xxlong, Payload, Tail,
                                 CandidateText, &PreserveEncodingWidth);
    if (OpcodeLength == 5)
      return decodeScalarTablePayload(Payload, 34, Tail, CandidateText,
                                      &PreserveEncodingWidth) ||
             decodeVectorPayload(VectorEncodingClass::ExtraLong, Payload, Tail,
                                 CandidateText, &PreserveEncodingWidth);
    if (OpcodeLength == 4)
      return decodeScalarTablePayload(Payload, 26, Tail, CandidateText,
                                      &PreserveEncodingWidth) ||
             decodeVectorPayload(VectorEncodingClass::Long, Payload, Tail,
                                 CandidateText, &PreserveEncodingWidth);
    return decodeScalarTablePayload(Payload, 18, Tail, CandidateText,
                                    &PreserveEncodingWidth);
  };

  unsigned RequiredLength = OpcodeLength;
  bool Decoded = false;
  bool PreserveEncodingWidth = false;
  for (; RequiredLength <= Inst.size(); ++RequiredLength) {
    SmallString<128> CandidateText;
    bool CandidatePreserveEncodingWidth = false;
    ArrayRef<uint8_t> Tail =
        Inst.slice(OpcodeLength, RequiredLength - OpcodeLength);
    if (!DecodeWithTail(Tail, CandidateText, CandidatePreserveEncodingWidth))
      continue;
    Text = CandidateText;
    PreserveEncodingWidth = CandidatePreserveEncodingWidth;
    Decoded = true;
    break;
  }
  if (!Decoded)
    return false;
  if (Size > RequiredLength || PreserveEncodingWidth)
    appendLengthAnnotation(Text, Size, Inst.drop_front(RequiredLength));
  return true;
}
