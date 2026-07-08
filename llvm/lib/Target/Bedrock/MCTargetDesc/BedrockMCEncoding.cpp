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

using namespace llvm;

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

const char *getSRegName(unsigned Reg) {
  static const char *Names[] = {"cs",  "ds",  "ss",  "gs0",
                                "gs1", "gs2", "gs3", "gs4"};
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

bool decodePrefixByte(uint8_t Prefix, SmallString<32> &Text) {
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

bool prependPrefixes(ArrayRef<uint8_t> Bytes, SmallString<128> &Text) {
  if ((Bytes[0] & 0x80) == 0)
    return true;

  SmallString<128> PrefixText;
  for (uint8_t Prefix : Bytes.slice(2, 2)) {
    SmallString<32> Slot;
    if (!decodePrefixByte(Prefix, Slot))
      return false;
    if (!Slot.empty()) {
      PrefixText += Slot;
      PrefixText += ", ";
    }
  }

  if (!PrefixText.empty()) {
    PrefixText += Text;
    Text = PrefixText;
  }
  return true;
}

bool decodeShortPayload(uint16_t Payload, SmallString<128> &Text) {
  struct FixedForm {
    uint16_t Payload;
    StringRef Mnemonic;
  };

  static const FixedForm FixedForms[] = {
      {0x2040, "nop"},     {0x2041, "ret"},     {0x2042, "lret"},
      {0x2043, "syscall"}, {0x2044, "sysret"},  {0x2045, "iret"},
      {0x2046, "bkpt"},    {0x2047, "wait"},    {0x2048, "yield"},
      {0x2049, "halt"},    {0x204a, "illegal"}, {0x204b, "rfence"},
      {0x204c, "wfence"},  {0x204d, "afence"},  {0x204e, "reset"},
      {0x2050, "trap"},
  };

  for (const FixedForm &Form : FixedForms) {
    if (Payload == Form.Payload) {
      Text = Form.Mnemonic;
      return true;
    }
  }

  static const char *RRForms[] = {
      "mov.l",   "mov.q",   "add.l",   "add.q",   "sub.l", "sub.q", "cmp.l",
      "cmp.q",   "and.l",   "and.q",   "or.l",    "or.q",  "xor.l", "xor.q",
      "test.l",  "test.q",  "xchg.l",  "xchg.q",  "shr.l", "shr.q", "shl.l",
      "shl.q",   "ror.l",   "ror.q",   "rol.l",   "rol.q", "sar.l", "sar.q",
      "extzl.b", "extzl.w", "extsl.b", "extsl.w",
  };

  unsigned Op6 = Payload >> 8;
  if (Op6 < std::size(RRForms)) {
    Text = formatv("{0}\tr{1}, r{2}", RRForms[Op6], (Payload >> 4) & 0xf,
                   Payload & 0xf)
               .str();
    return true;
  }
  if (Op6 == 0x28 || Op6 == 0x29) {
    Text = formatv("{0}\tr{1}, r{2}", Op6 == 0x28 ? "extzq.l" : "extsq.l",
                   (Payload >> 4) & 0xf, Payload & 0xf)
               .str();
    return true;
  }

  unsigned LowReg = Payload & 0xf;
  switch (Payload >> 4) {
  case 0x200:
    Text = formatv("mov.q\tr{0}, sp", LowReg).str();
    return true;
  case 0x201:
    Text = formatv("mov.q\tsp, r{0}", LowReg).str();
    return true;
  case 0x202:
    Text = formatv("push\tr{0}", LowReg).str();
    return true;
  case 0x203:
    Text = formatv("pop\tr{0}", LowReg).str();
    return true;
  default:
    break;
  }

  if ((Payload & 0x3ff0) == 0x2050) {
    const char *Cond = getCondName(Payload & 0xf);
    if (!Cond)
      return false;
    Text = formatv("trap.{0}", Cond).str();
    return true;
  }

  if ((Payload & 0x3f00) == 0x2100) {
    unsigned Reg = (Payload >> 4) & 0xf;
    unsigned Cond = Payload & 0xf;
    if (Cond == 0) {
      Text = formatv("set\tr{0}", Reg).str();
      return true;
    }
    const char *CondName = getCondName(Cond);
    if (!CondName)
      return false;
    Text = formatv("set.{0}\tr{1}", CondName, Reg).str();
    return true;
  }

  struct UnaryForm {
    uint16_t Prefix;
    StringRef Mnemonic;
  };
  static const UnaryForm UnaryForms[] = {
      {0x220, "inc.l"},     {0x230, "inc.q"},     {0x221, "dec.l"},
      {0x231, "dec.q"},     {0x222, "neg.l"},     {0x232, "neg.q"},
      {0x223, "clr.l"},     {0x233, "clr.q"},     {0x224, "abs.l"},
      {0x234, "abs.q"},     {0x225, "not.l"},     {0x235, "not.q"},
      {0x229, "revbyte.w"}, {0x22a, "revbyte.l"}, {0x22b, "revbyte.q"},
  };
  for (const UnaryForm &Form : UnaryForms) {
    if ((Payload >> 4) == Form.Prefix) {
      Text = formatv("{0}\tr{1}", Form.Mnemonic, LowReg).str();
      return true;
    }
  }

  int64_t Imm8 = signExtend(Payload & 0xff, 8);
  if (Op6 == 0x2f) {
    Text = formatv("add.q\t{0}, sp", Payload & 0xff).str();
    return true;
  }
  if (Op6 == 0x30) {
    Text = formatv("jmp\t{0}", Imm8).str();
    return true;
  }
  if (Op6 == 0x31) {
    Text = formatv("sub.q\t{0}, sp", Payload & 0xff).str();
    return true;
  }
  if (Op6 >= 0x32 && Op6 <= 0x3f) {
    const char *Cond = getCondName(Op6 & 0xf);
    if (!Cond)
      return false;
    Text = formatv("j.{0}\t{1}", Cond, Imm8).str();
    return true;
  }

  return false;
}

bool decodeCompactEA(uint8_t EA, ArrayRef<uint8_t> Tail, unsigned &Consumed,
                     SmallString<64> &Text) {
  Consumed = 0;

  if (EA <= 0x0f) {
    Text = formatv("r{0}", EA).str();
    return true;
  }

  if (EA <= 0x1f) {
    Text = formatv("[r{0}]", EA & 0xf).str();
    return true;
  }

  if (EA >= 0x20 && EA <= 0x5f) {
    unsigned WidthCode = (EA >> 4) - 2;
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

  if (EA >= 0x60 && EA <= 0x67) {
    unsigned Width = 1u << (EA & 0x3);
    if (Tail.size() < Width)
      return false;
    bool IsPC = EA >= 0x64;
    int64_t Disp = Width == 8 ? static_cast<int64_t>(readLE(Tail, 0, Width))
                              : signExtend(readLE(Tail, 0, Width), Width * 8);
    Text = IsPC ? "[pc" : "[sp";
    appendSignedOffset(Text, Disp);
    Text += "]";
    Consumed = Width;
    return true;
  }

  if (EA == 0x68) {
    Text = "sp";
    return true;
  }
  if (EA == 0x69) {
    Text = "[sp]";
    return true;
  }
  if (EA == 0x6a || EA == 0x6b) {
    unsigned Width = EA == 0x6a ? 4 : 8;
    if (Tail.size() < Width)
      return false;
    Text = "[";
    if (EA == 0x6a)
      appendSignedImm(Text, Tail, Width);
    else
      appendUnsignedImm(Text, Tail, Width);
    Text += "]";
    Consumed = Width;
    return true;
  }
  if (EA >= 0x6c && EA <= 0x6f) {
    unsigned Width = 1u << (EA - 0x6c);
    if (Tail.size() < Width)
      return false;
    if (EA == 0x6f)
      appendUnsignedImm(Text, Tail, Width);
    else
      appendSignedImm(Text, Tail, Width);
    Consumed = Width;
    return true;
  }

  if (EA >= 0x70 && EA <= 0x74) {
    unsigned DispWidth = EA == 0x74 ? 0 : 1u << (EA - 0x70);
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
      unsigned Base = (D0 >> 3) & 0xf;
      if ((D0 & 0x7) == 0x4)
        Text = formatv("[r{0}++", Base).str();
      else
        Text = formatv("[--r{0}", Base).str();
      return Finish(1);
    }

    if (D0 == 0x8a || D0 == 0x8b) {
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
      if (Tail.size() < 2)
        return false;
      unsigned Base = Tail[1] >> 4;
      unsigned Index = Tail[1] & 0xf;
      Text = formatv("[{0}:r{1} + ", SRegName, Base).str();
      appendExt0Index(Text, Submode, Index);
      return Finish(2);
    }
    case 3:
      Text = formatv("[{0}:0", SRegName).str();
      return Finish(1);
    case 8: {
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

bool decodeMediumEAUnary(uint32_t Payload, ArrayRef<uint8_t> Tail,
                         SmallString<128> &Text) {
  struct Form {
    StringRef Pattern;
    StringRef Mnemonic;
    StringRef Suffixes;
    bool RequireNonRegEA;
  };

  static const Form Forms[] = {
      {"0eee10z0000000eeee", "inc", "bw", false},
      {"0eee10z0001000eeee", "inc", "lq", true},
      {"0eee10z0010000eeee", "dec", "bw", false},
      {"0eee10z0011000eeee", "dec", "lq", true},
      {"0eee10z0100000eeee", "neg", "bw", false},
      {"0eee10z0101000eeee", "neg", "lq", true},
      {"0eee10z0110000eeee", "clr", "bw", false},
      {"0eee10z0111000eeee", "clr", "lq", true},
      {"0eee10z1000000eeee", "abs", "bw", false},
      {"0eee10z1001000eeee", "abs", "lq", true},
      {"0eee10z1010000eeee", "not", "bw", false},
      {"0eee10z1011000eeee", "not", "lq", true},
      {"0eee1001101000eeee", "revbyte", "w", true},
      {"0eee1001110000eeee", "revbyte", "l", true},
      {"0eee1001111000eeee", "revbyte", "q", true},
  };

  for (const Form &F : Forms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;

    uint8_t EA = extractPatternField(F.Pattern, Payload, 'e');
    if (F.RequireNonRegEA && EA < 0x10)
      continue;

    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;

    unsigned Z = extractPatternField(F.Pattern, Payload, 'z');
    char Suffix = F.Suffixes.size() == 1 ? F.Suffixes[0] : F.Suffixes[Z];
    Text = formatv("{0}.{1}\t{2}", F.Mnemonic, Suffix, EAText).str();
    return true;
  }

  return false;
}

bool decodeMediumBinary(uint32_t Payload, ArrayRef<uint8_t> Tail,
                        SmallString<128> &Text) {
  enum class BinaryDir { RnEA, EARn };
  struct Form {
    StringRef Base;
    StringRef Pattern;
    StringRef Suffixes;
    BinaryDir Dir;
    bool RequireNonRegEA;
    bool DisallowSPDirect;
  };

  static const Form Forms[] = {
      {"lea", "0eeez1zdddd000eeee", "bwlq", BinaryDir::EARn, false, false},
      {"mov", "000000zsssseeeeeee", "bw", BinaryDir::RnEA, false, true},
      {"mov", "000001zsssseeeeeee", "lq", BinaryDir::RnEA, true, true},
      {"mov", "000010zddddeeeeeee", "bw", BinaryDir::EARn, true, true},
      {"mov", "000011zddddeeeeeee", "lq", BinaryDir::EARn, true, true},
      {"add", "000100zsssseeeeeee", "bw", BinaryDir::RnEA, false, false},
      {"add", "000101zsssseeeeeee", "lq", BinaryDir::RnEA, true, true},
      {"add", "000110zddddeeeeeee", "bw", BinaryDir::EARn, true, false},
      {"add", "000111zddddeeeeeee", "lq", BinaryDir::EARn, true, true},
      {"sub", "001000zsssseeeeeee", "bw", BinaryDir::RnEA, false, false},
      {"sub", "001001zsssseeeeeee", "lq", BinaryDir::RnEA, true, true},
      {"sub", "001010zddddeeeeeee", "bw", BinaryDir::EARn, true, false},
      {"sub", "001011zddddeeeeeee", "lq", BinaryDir::EARn, true, true},
      {"and", "001100zsssseeeeeee", "bw", BinaryDir::RnEA, false, false},
      {"and", "001101zsssseeeeeee", "lq", BinaryDir::RnEA, true, true},
      {"and", "001110zddddeeeeeee", "bw", BinaryDir::EARn, true, false},
      {"and", "001111zddddeeeeeee", "lq", BinaryDir::EARn, true, true},
      {"or", "010000zsssseeeeeee", "bw", BinaryDir::RnEA, false, false},
      {"or", "010001zsssseeeeeee", "lq", BinaryDir::RnEA, true, true},
      {"or", "010010zddddeeeeeee", "bw", BinaryDir::EARn, true, false},
      {"or", "010011zddddeeeeeee", "lq", BinaryDir::EARn, true, true},
      {"xor", "010100zsssseeeeeee", "bw", BinaryDir::RnEA, false, false},
      {"xor", "010101zsssseeeeeee", "lq", BinaryDir::RnEA, true, true},
      {"xor", "010110zddddeeeeeee", "bw", BinaryDir::EARn, true, false},
      {"xor", "010111zddddeeeeeee", "lq", BinaryDir::EARn, true, true},
      {"test", "011000zsssseeeeeee", "bw", BinaryDir::RnEA, false, false},
      {"test", "011001zsssseeeeeee", "lq", BinaryDir::RnEA, true, true},
      {"test", "011010zddddeeeeeee", "bw", BinaryDir::EARn, true, false},
      {"test", "011011zddddeeeeeee", "lq", BinaryDir::EARn, true, true},
      {"cmp", "011100zsssseeeeeee", "bw", BinaryDir::RnEA, false, false},
      {"cmp", "011101zsssseeeeeee", "lq", BinaryDir::RnEA, true, true},
      {"cmp", "011110zddddeeeeeee", "bw", BinaryDir::EARn, true, false},
      {"cmp", "011111zddddeeeeeee", "lq", BinaryDir::EARn, true, true},
      {"xchg", "100000zsssseeeeeee", "bw", BinaryDir::RnEA, false, false},
      {"xchg", "100001zsssseeeeeee", "lq", BinaryDir::RnEA, true, true},
      {"xchg", "100010zddddeeeeeee", "bw", BinaryDir::EARn, true, false},
      {"xchg", "100011zddddeeeeeee", "lq", BinaryDir::EARn, true, true},
      {"rol", "100110zsssseeeeeee", "bw", BinaryDir::RnEA, false, false},
      {"rol", "100111zsssseeeeeee", "lq", BinaryDir::RnEA, true, false},
      {"ror", "101000zsssseeeeeee", "bw", BinaryDir::RnEA, false, false},
      {"ror", "101001zsssseeeeeee", "lq", BinaryDir::RnEA, true, false},
      {"shl", "101010zsssseeeeeee", "bw", BinaryDir::RnEA, false, false},
      {"shl", "101011zsssseeeeeee", "lq", BinaryDir::RnEA, true, false},
      {"shr", "101100zsssseeeeeee", "bw", BinaryDir::RnEA, false, false},
      {"shr", "101101zsssseeeeeee", "lq", BinaryDir::RnEA, true, false},
      {"sar", "101110zsssseeeeeee", "bw", BinaryDir::RnEA, false, false},
      {"sar", "101111zsssseeeeeee", "lq", BinaryDir::RnEA, true, false},
  };

  for (const Form &F : Forms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;

    uint8_t EA = extractPatternField(F.Pattern, Payload, 'e');
    if (F.RequireNonRegEA && EA < 0x10)
      continue;
    if (F.DisallowSPDirect && EA == 0x68)
      continue;

    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;

    unsigned Z = extractPatternField(F.Pattern, Payload, 'z');
    if (Z >= F.Suffixes.size())
      continue;
    char Suffix = F.Suffixes[Z];
    char RegField = F.Dir == BinaryDir::RnEA ? 's' : 'd';
    unsigned Reg = extractPatternField(F.Pattern, Payload, RegField);

    if (F.Dir == BinaryDir::RnEA)
      Text = formatv("{0}.{1}\tr{2}, {3}", F.Base, Suffix, Reg, EAText).str();
    else
      Text = formatv("{0}.{1}\t{2}, r{3}", F.Base, Suffix, EAText, Reg).str();
    return true;
  }

  return false;
}

bool decodeMediumRRExt(uint32_t Payload, ArrayRef<uint8_t> Tail,
                       SmallString<128> &Text) {
  struct RRForm {
    StringRef Base;
    StringRef Pattern;
  };
  static const RRForm RRForms[] = {
      {"adc", "1100zz1ssss000dddd"},
      {"sbb", "1101zz1ssss000dddd"},
  };

  for (const RRForm &F : RRForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    static const char Suffixes[] = {'b', 'w', 'l', 'q'};
    unsigned Z = extractPatternField(F.Pattern, Payload, 'z');
    unsigned S = extractPatternField(F.Pattern, Payload, 's');
    unsigned D = extractPatternField(F.Pattern, Payload, 'd');
    Text = formatv("{0}.{1}\tr{2}, r{3}", F.Base, Suffixes[Z], S, D).str();
    return true;
  }

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

  for (const ExtForm &F : ExtForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;

    uint8_t EA = extractPatternField(F.Pattern, Payload, 'e');
    if (F.RequireNonRegEA && EA < 0x10)
      continue;

    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;

    char RegField = F.Dir == ExtDir::RnEA ? 's' : 'd';
    unsigned Reg = extractPatternField(F.Pattern, Payload, RegField);
    if (F.Dir == ExtDir::RnEA)
      Text = formatv("{0}\tr{1}, {2}", F.Mnemonic, Reg, EAText).str();
    else
      Text = formatv("{0}\t{1}, r{2}", F.Mnemonic, EAText, Reg).str();
    return true;
  }

  return false;
}

bool decodeMediumPayload(uint32_t Payload, ArrayRef<uint8_t> Tail,
                         SmallString<128> &Text) {
  auto NeedTail = [&](unsigned Width) { return Tail.size() >= Width; };

  if (decodeMediumEAUnary(Payload, Tail, Text))
    return true;
  if (decodeMediumBinary(Payload, Tail, Text))
    return true;
  if (decodeMediumRRExt(Payload, Tail, Text))
    return true;

  if (Payload == 0x2600) {
    if (!NeedTail(2))
      return false;
    Text = "jmp\t";
    appendSignedImm(Text, Tail, 2);
    return true;
  }
  if (Payload == 0x6600) {
    if (!NeedTail(4))
      return false;
    Text = "jmp\t";
    appendSignedImm(Text, Tail, 4);
    return true;
  }

  if ((Payload & 0x3fff0) == 0x2600) {
    const char *Cond = getCondName(Payload & 0xf);
    if (!Cond || !NeedTail(2))
      return false;
    Text = formatv("j.{0}\t", Cond).str();
    appendSignedImm(Text, Tail, 2);
    return true;
  }
  if ((Payload & 0x3fff0) == 0x6600) {
    const char *Cond = getCondName(Payload & 0xf);
    if (!Cond || !NeedTail(4))
      return false;
    Text = formatv("j.{0}\t", Cond).str();
    appendSignedImm(Text, Tail, 4);
    return true;
  }

  if (Payload == 0xa600) {
    if (!NeedTail(2))
      return false;
    Text = "call\t";
    appendSignedImm(Text, Tail, 2);
    return true;
  }
  if (Payload == 0xe600) {
    if (!NeedTail(4))
      return false;
    Text = "call\t";
    appendSignedImm(Text, Tail, 4);
    return true;
  }

  if ((Payload & 0x3fff0) == 0xa600) {
    const char *Cond = getCondName(Payload & 0xf);
    if (!Cond || !NeedTail(2))
      return false;
    Text = formatv("call.{0}\t", Cond).str();
    appendSignedImm(Text, Tail, 2);
    return true;
  }
  if ((Payload & 0x3fff0) == 0xe600) {
    const char *Cond = getCondName(Payload & 0xf);
    if (!Cond || !NeedTail(4))
      return false;
    Text = formatv("call.{0}\t", Cond).str();
    appendSignedImm(Text, Tail, 4);
    return true;
  }

  switch (Payload) {
  case 0x2780:
    if (!NeedTail(2))
      return false;
    Text = "add.q\t";
    appendSignedImm(Text, Tail, 2);
    Text += ", sp";
    return true;
  case 0x2781:
    if (!NeedTail(4))
      return false;
    Text = "add.q\t";
    appendSignedImm(Text, Tail, 4);
    Text += ", sp";
    return true;
  case 0x2782:
    if (!NeedTail(2))
      return false;
    Text = "sub.q\t";
    appendSignedImm(Text, Tail, 2);
    Text += ", sp";
    return true;
  case 0x2783:
    if (!NeedTail(4))
      return false;
    Text = "sub.q\t";
    appendSignedImm(Text, Tail, 4);
    Text += ", sp";
    return true;
  case 0x2784:
    if (!NeedTail(2))
      return false;
    Text = "pushm\t";
    appendUnsignedImm(Text, Tail, 2);
    return true;
  case 0x2785:
    if (!NeedTail(2))
      return false;
    Text = "popm\t";
    appendUnsignedImm(Text, Tail, 2);
    return true;
  case 0x2786:
    if (!NeedTail(2))
      return false;
    Text = "trace\t";
    appendUnsignedImm(Text, Tail, 2);
    return true;
  default:
    break;
  }

  if ((Payload & 0x3fff0) == 0x2680) {
    if (!NeedTail(2))
      return false;
    Text = formatv("repg\tr{0}, ", Payload & 0xf).str();
    appendUnsignedImm(Text, Tail, 2);
    return true;
  }

  if ((Payload & 0x3fff0) == 0x2700) {
    Text = formatv("cpuid\tr{0}", Payload & 0xf).str();
    return true;
  }

  if ((Payload & 0x33ff0) == 0x12600) {
    static const char Suffixes[] = {'b', 'w', 'l', 'q'};
    unsigned Size = (Payload >> 14) & 0x3;
    if (!NeedTail(2))
      return false;
    Text = formatv("sum.{0}\t", Suffixes[Size]).str();
    appendUnsignedImm(Text, Tail, 2);
    Text += formatv(", r{0}", Payload & 0xf).str();
    return true;
  }

  return false;
}

bool decodeLongPayload(uint32_t Payload, ArrayRef<uint8_t> Tail,
                       SmallString<128> &Text) {
  enum class LongDir { RnEA, EARn };
  struct LongRegEAForm {
    StringRef Mnemonic;
    StringRef Pattern;
    StringRef Suffixes;
    LongDir Dir;
    char RegField;
    bool RequireNonRegEA;
  };

  static const LongRegEAForm LongRegEAForms[] = {
      {"adc", "1111000000zz000sssseeeeeee", "bwlq", LongDir::RnEA, 's', true},
      {"adc", "1111000000zz001sssseeeeeee", "bwlq", LongDir::EARn, 's', true},
      {"sbb", "1111000000zz010sssseeeeeee", "bwlq", LongDir::RnEA, 's', true},
      {"sbb", "1111000000zz011sssseeeeeee", "bwlq", LongDir::EARn, 's', true},
      {"clz", "1111000000zz100ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false},
      {"ctz", "1111000000zz101ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false},
      {"cls", "1111000000zz110ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false},
      {"cts", "1111000000zz111ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false},
      {"minu", "1111000001zz000ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false},
      {"minu", "1111000001zz001sssseeeeeee", "bwlq", LongDir::RnEA, 's', true},
      {"mins", "1111000001zz010ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false},
      {"mins", "1111000001zz011sssseeeeeee", "bwlq", LongDir::RnEA, 's', true},
      {"maxu", "1111000001zz100ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false},
      {"maxu", "1111000001zz101sssseeeeeee", "bwlq", LongDir::RnEA, 's', true},
      {"maxs", "1111000001zz110ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false},
      {"maxs", "1111000001zz111sssseeeeeee", "bwlq", LongDir::RnEA, 's', true},
      {"popcnt", "1111000010zz000ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
       false},
      {"parity", "1111000010zz001ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
       false},
      {"mul", "1111000010zz010ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false},
      {"clmul", "1111000010zz011ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
       false},
      {"divu", "1111000010zz100ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false},
      {"divs", "1111000010zz101ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false},
      {"modu", "1111000010zz110ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false},
      {"mods", "1111000010zz111ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false},
      {"btest", "111100001100000bbbbeeeeeee", "", LongDir::RnEA, 'b', false},
      {"bset", "111100001100001bbbbeeeeeee", "", LongDir::RnEA, 'b', false},
      {"bclr", "111100001100010bbbbeeeeeee", "", LongDir::RnEA, 'b', false},
      {"bchg", "111100001100011bbbbeeeeeee", "", LongDir::RnEA, 'b', false},
      {"lcall", "111100001100101rrrreeeeeee", "", LongDir::RnEA, 'r', false},
      {"ljmp", "111100001100110rrrreeeeeee", "", LongDir::RnEA, 'r', false},
      {"clmulh.q", "111100001100111ddddeeeeeee", "", LongDir::EARn, 'd', false},
  };

  for (const LongRegEAForm &F : LongRegEAForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;

    uint8_t EA = extractPatternField(F.Pattern, Payload, 'e');
    if (F.RequireNonRegEA && EA < 0x10)
      continue;

    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;

    StringRef Mnemonic = F.Mnemonic;
    SmallString<32> MnemonicStorage;
    if (!F.Suffixes.empty()) {
      unsigned Z = extractPatternField(F.Pattern, Payload, 'z');
      if (Z >= F.Suffixes.size())
        continue;
      MnemonicStorage = formatv("{0}.{1}", F.Mnemonic, F.Suffixes[Z]).str();
      Mnemonic = MnemonicStorage;
    }

    unsigned Reg = extractPatternField(F.Pattern, Payload, F.RegField);
    if (F.Dir == LongDir::RnEA)
      Text = formatv("{0}\tr{1}, {2}", Mnemonic, Reg, EAText).str();
    else
      Text = formatv("{0}\t{1}, r{2}", Mnemonic, EAText, Reg).str();
    return true;
  }

  if (matchPattern("111100001100100cccceeeeeee", Payload)) {
    const char *Cond = getCondName(
        extractPatternField("111100001100100cccceeeeeee", Payload, 'c'));
    if (!Cond)
      return false;
    uint8_t EA =
        extractPatternField("111100001100100cccceeeeeee", Payload, 'e');
    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;
    Text = formatv("dj.{0}\t{1}", Cond, EAText).str();
    return true;
  }

  struct LongEAOnlyForm {
    StringRef Mnemonic;
    StringRef Pattern;
    StringRef Suffixes;
  };
  static const LongEAOnlyForm LongEAOnlyForms[] = {
      {"seglea", "1111000100zz1100000eeeeeee", "bwlq"},
      {"invpage", "1111101111010000110eeeeeee", ""},
      {"flshdcache", "1111101111010000111eeeeeee", ""},
      {"invdcache", "1111101111010001000eeeeeee", ""},
      {"invicache", "1111101111010001001eeeeeee", ""},
      {"prefetch", "1111101111010001010eeeeeee", ""},
      {"synccache", "1111101111010001011eeeeeee", ""},
      {"wrbkdcache", "1111101111010001100eeeeeee", ""},
      {"save", "1111101111010001101eeeeeee", ""},
      {"restore", "1111101111010001110eeeeeee", ""},
  };

  for (const LongEAOnlyForm &F : LongEAOnlyForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    uint8_t EA = extractPatternField(F.Pattern, Payload, 'e');
    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;
    if (F.Suffixes.empty()) {
      Text = formatv("{0}\t{1}", F.Mnemonic, EAText).str();
    } else {
      unsigned Z = extractPatternField(F.Pattern, Payload, 'z');
      if (Z >= F.Suffixes.size())
        continue;
      Text = formatv("{0}.{1}\t{2}", F.Mnemonic, F.Suffixes[Z], EAText).str();
    }
    return true;
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

  for (const LongRegOnlyForm &F : LongRegOnlyForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    unsigned Z = extractPatternField(F.Pattern, Payload, 'z');
    if (Z >= F.Suffixes.size())
      continue;
    unsigned Reg = extractPatternField(F.Pattern, Payload, F.RegField);
    Text = formatv("{0}.{1}\tr{2}", F.Base, F.Suffixes[Z], Reg).str();
    return true;
  }

  struct LongEAEAForm {
    StringRef Mnemonic;
    StringRef Pattern;
    StringRef Suffixes;
  };
  static const LongEAEAForm LongEAEAForms[] = {
      {"mov", "1111100000zzsssssssddddddd", "bwlq"},
      {"cmp", "1111100001zzsssssssddddddd", "bwlq"},
      {"extsw.b", "111110100000sssssssddddddd", ""},
      {"extsq.b", "111110100001sssssssddddddd", ""},
      {"extsq.w", "111110100010sssssssddddddd", ""},
      {"extsq.l", "111110100011sssssssddddddd", ""},
      {"extzw.b", "111110100100sssssssddddddd", ""},
      {"extzq.b", "111110100101sssssssddddddd", ""},
      {"extzq.w", "111110100110sssssssddddddd", ""},
      {"extzq.l", "111110100111sssssssddddddd", ""},
      {"extsl", "11111010100zsssssssddddddd", "bw"},
      {"extzl", "11111010101zsssssssddddddd", "bw"},
  };

  for (const LongEAEAForm &F : LongEAEAForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    uint8_t SrcEA = extractPatternField(F.Pattern, Payload, 's');
    uint8_t DstEA = extractPatternField(F.Pattern, Payload, 'd');
    if (SrcEA < 0x10 || DstEA < 0x10)
      continue;

    unsigned SrcConsumed = 0;
    unsigned DstConsumed = 0;
    SmallString<64> SrcText;
    SmallString<64> DstText;
    if (!decodeCompactEA(SrcEA, Tail, SrcConsumed, SrcText) ||
        !decodeCompactEA(DstEA, Tail.drop_front(SrcConsumed), DstConsumed,
                         DstText))
      return false;

    if (F.Suffixes.empty()) {
      Text = formatv("{0}\t{1}, {2}", F.Mnemonic, SrcText, DstText).str();
    } else {
      unsigned Z = extractPatternField(F.Pattern, Payload, 'z');
      if (Z >= F.Suffixes.size())
        continue;
      Text = formatv("{0}.{1}\t{2}, {3}", F.Mnemonic, F.Suffixes[Z], SrcText,
                     DstText)
                 .str();
    }
    return true;
  }

  struct LongImmEAForm {
    StringRef Base;
    StringRef Pattern;
    StringRef Suffixes;
    unsigned ImmBits;
    bool HasRegDst;
  };
  static const LongImmEAForm LongImmEAForms[] = {
      {"rol", "1111101100zz0iiiiiieeeeeee", "bwlq", 6, false},
      {"ror", "1111101100zz1iiiiiieeeeeee", "bwlq", 6, false},
      {"shl", "1111101101zz0iiiiiieeeeeee", "bwlq", 6, false},
      {"shr", "1111101101zz1iiiiiieeeeeee", "bwlq", 6, false},
      {"sar", "1111101110zz0iiiiiieeeeeee", "bwlq", 6, false},
      {"btest", "1111101110001iiiiiieeeeeee", "", 6, false},
      {"bset", "1111101110011iiiiiieeeeeee", "", 6, false},
      {"bclr", "1111101110101iiiiiieeeeeee", "", 6, false},
      {"bchg", "1111101110111iiiiiieeeeeee", "", 6, false},
      {"ptquery", "111110111100iiiddddeeeeeee", "", 3, true},
  };

  for (const LongImmEAForm &F : LongImmEAForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    uint8_t EA = extractPatternField(F.Pattern, Payload, 'e');
    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;
    unsigned Imm = extractPatternField(F.Pattern, Payload, 'i');

    StringRef Mnemonic = F.Base;
    SmallString<32> MnemonicStorage;
    if (!F.Suffixes.empty()) {
      unsigned Z = extractPatternField(F.Pattern, Payload, 'z');
      if (Z >= F.Suffixes.size())
        continue;
      MnemonicStorage = formatv("{0}.{1}", F.Base, F.Suffixes[Z]).str();
      Mnemonic = MnemonicStorage;
    }

    if (F.HasRegDst) {
      unsigned Reg = extractPatternField(F.Pattern, Payload, 'd');
      Text = formatv("{0}\t{1}, {2}, r{3}", Mnemonic, Imm, EAText, Reg).str();
    } else {
      Text = formatv("{0}\t{1}, {2}", Mnemonic, Imm, EAText).str();
    }
    return true;
  }

  if (matchPattern("111110111101000000vvvvpppp", Payload)) {
    Text =
        formatv("vtop\tr{0}, r{1}",
                extractPatternField("111110111101000000vvvvpppp", Payload, 'v'),
                extractPatternField("111110111101000000vvvvpppp", Payload, 'p'))
            .str();
    return true;
  }
  if (matchPattern("111110111101000001ppppaaaa", Payload)) {
    Text =
        formatv("swpta\tr{0}, r{1}",
                extractPatternField("111110111101000001ppppaaaa", Payload, 'p'),
                extractPatternField("111110111101000001ppppaaaa", Payload, 'a'))
            .str();
    return true;
  }
  if (matchPattern("1111101111010000100sssdddd", Payload)) {
    unsigned SReg =
        extractPatternField("1111101111010000100sssdddd", Payload, 's');
    const char *SRegName = getSRegName(SReg);
    if (!SRegName)
      return false;
    Text =
        formatv("rdseg\t{0}, r{1}", SRegName,
                extractPatternField("1111101111010000100sssdddd", Payload, 'd'))
            .str();
    return true;
  }
  if (matchPattern("1111101111010000101sssdddd", Payload)) {
    unsigned SReg =
        extractPatternField("1111101111010000101sssdddd", Payload, 's');
    const char *SRegName = getSRegName(SReg);
    if (!SRegName)
      return false;
    Text =
        formatv("wrseg\tr{0}, {1}",
                extractPatternField("1111101111010000101sssdddd", Payload, 'd'),
                SRegName)
            .str();
    return true;
  }

  struct LongImm16RegForm {
    StringRef Mnemonic;
    StringRef Pattern;
    bool ImmFirst;
    char RegField;
  };
  static const LongImm16RegForm Imm16Forms[] = {
      {"rdcr", "1111101111010010000000dddd", true, 'd'},
      {"wrcr", "1111101111010010000001ssss", false, 's'},
      {"rdpmc", "1111101111010010001010dddd", true, 'd'},
  };
  for (const LongImm16RegForm &F : Imm16Forms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    if (Tail.size() < 2)
      return false;
    unsigned Reg = extractPatternField(F.Pattern, Payload, F.RegField);
    uint64_t Imm = readLE(Tail, 0, 2);
    if (F.ImmFirst)
      Text = formatv("{0}\t{1}, r{2}", F.Mnemonic, Imm, Reg).str();
    else
      Text = formatv("{0}\tr{1}, {2}", F.Mnemonic, Reg, Imm).str();
    return true;
  }

  struct LongSysRegForm {
    StringRef Mnemonic;
    StringRef Pattern;
    char RegField;
  };
  static const LongSysRegForm SysRegForms[] = {
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
  for (const LongSysRegForm &F : SysRegForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    Text = formatv("{0}\tr{1}", F.Mnemonic,
                   extractPatternField(F.Pattern, Payload, F.RegField))
               .str();
    return true;
  }

  if (Payload == 0x3ef4f00) {
    if (Tail.size() < 2)
      return false;
    Text = formatv("invasid\t{0}", readLE(Tail, 0, 2)).str();
    return true;
  }
  if (Payload == 0x3ef4f01) {
    Text = "invtlb";
    return true;
  }

  struct LongQRRForm {
    StringRef Mnemonic;
    StringRef Pattern;
  };
  static const LongQRRForm QRRForms[] = {
      {"mulhu.q", "111110111110000000ssssdddd"},
      {"mulhs.q", "111110111110000001ssssdddd"},
      {"mulhsu.q", "111110111110000010ssssdddd"},
  };
  for (const LongQRRForm &F : QRRForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    Text = formatv("{0}\tr{1}, r{2}", F.Mnemonic,
                   extractPatternField(F.Pattern, Payload, 's'),
                   extractPatternField(F.Pattern, Payload, 'd'))
               .str();
    return true;
  }

  return false;
}

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

bool BedrockMC::encodeMedium(uint32_t Payload, ArrayRef<uint8_t> Tail,
                             SmallVectorImpl<uint8_t> &Bytes) {
  unsigned ExtBytes = 1 + Tail.size();
  if (Payload >= (1u << 18) || ExtBytes == 0 || ExtBytes > 16)
    return false;

  Bytes.push_back(0x40 | ((ExtBytes - 1) << 2) | ((Payload >> 16) & 0x3));
  Bytes.push_back((Payload >> 8) & 0xff);
  Bytes.push_back(Payload & 0xff);
  Bytes.append(Tail.begin(), Tail.end());
  return true;
}

bool BedrockMC::encodeLong(uint32_t Payload, ArrayRef<uint8_t> Tail,
                           SmallVectorImpl<uint8_t> &Bytes) {
  unsigned ExtBytes = 2 + Tail.size();
  if (Payload >= (1u << 26) || ExtBytes > 16)
    return false;

  Bytes.push_back(0x40 | ((ExtBytes - 1) << 2) | ((Payload >> 24) & 0x3));
  Bytes.push_back((Payload >> 16) & 0xff);
  Bytes.push_back((Payload >> 8) & 0xff);
  Bytes.push_back(Payload & 0xff);
  Bytes.append(Tail.begin(), Tail.end());
  return true;
}

bool BedrockMC::getInstructionSize(ArrayRef<uint8_t> Bytes, uint64_t &Size) {
  if (Bytes.size() < 2) {
    Size = 0;
    return false;
  }

  bool HasPrefix = Bytes[0] & 0x80;
  bool IsExtended = Bytes[0] & 0x40;

  if (!IsExtended) {
    Size = 2 + (HasPrefix ? 2 : 0);
    return Bytes.size() >= Size;
  }

  unsigned ExtBytes = ((Bytes[0] >> 2) & 0xf) + 1;
  Size = 2 + (HasPrefix ? 2 : 0) + ExtBytes;
  return Bytes.size() >= Size;
}

bool BedrockMC::decodeRawInst(ArrayRef<uint8_t> Bytes, uint64_t &Size,
                              SmallString<128> &Text) {
  if (!getInstructionSize(Bytes, Size))
    return false;

  bool HasPrefix = Bytes[0] & 0x80;
  bool IsExtended = Bytes[0] & 0x40;
  if (!IsExtended) {
    uint16_t Payload = ((Bytes[0] & 0x3f) << 8) | Bytes[1];
    if (!decodeShortPayload(Payload, Text))
      return false;
    return prependPrefixes(Bytes, Text);
  }

  unsigned ExtBytes = ((Bytes[0] >> 2) & 0xf) + 1;
  unsigned ExtOffset = 2 + (HasPrefix ? 2 : 0);
  ArrayRef<uint8_t> Ext = Bytes.slice(ExtOffset, ExtBytes);
  uint32_t BasePayload = ((Bytes[0] & 0x3) << 8) | Bytes[1];
  uint32_t MediumPayload = (BasePayload << 8) | Ext[0];

  if ((MediumPayload >> 14) == 0xf) {
    if (Ext.size() < 2)
      return false;
    uint32_t LongPayload =
        (BasePayload << 16) | (uint32_t(Ext[0]) << 8) | Ext[1];
    if (!decodeLongPayload(LongPayload, Ext.drop_front(2), Text))
      return false;
    return prependPrefixes(Bytes, Text);
  }

  if (!decodeMediumPayload(MediumPayload, Ext.drop_front(), Text))
    return false;
  return prependPrefixes(Bytes, Text);
}
