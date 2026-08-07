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

bool isRepPayload(uint32_t Payload) {
  return (Payload & 0x3f0f0) == 0x24000 && ((Payload >> 8) & 0xf) != 0x1;
}

bool isRepgPayload(uint32_t Payload) {
  return (Payload & 0x3fff0) == 0x2680;
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

uint16_t readBE16(ArrayRef<uint8_t> Bytes, unsigned Offset) {
  return (uint16_t(Bytes[Offset]) << 8) | Bytes[Offset + 1];
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

bool matchPattern64(StringRef Pattern, uint64_t Payload) {
  uint64_t Mask = 0;
  uint64_t Value = 0;
  unsigned Width = Pattern.size();
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

bool decodeExtraShortPayload(uint8_t Payload, SmallString<128> &Text) {
  struct FixedForm {
    uint8_t Payload;
    StringRef Mnemonic;
  };

  static const FixedForm FixedForms[] = {
      {0x00, "illegal"}, {0x01, "nop"},    {0x02, "ret"},
      {0x03, "lret"},    {0x04, "eret"},   {0x05, "syscall"},
      {0x06, "sysret"},  {0x07, "bkpt"},   {0x08, "wait"},
      {0x09, "yield"},   {0x0a, "rfence"}, {0x0b, "wfence"},
      {0x0c, "afence"},  {0x0e, "add.q\t8, sp"},
      {0x0f, "sub.q\t8, sp"},
  };

  for (const FixedForm &Form : FixedForms) {
    if (Payload == Form.Payload) {
      Text = Form.Mnemonic;
      return true;
    }
  }

  if (Payload == 0x0d) {
    Text = "push\tcs";
    return true;
  }

  if ((Payload & 0x78) == 0x10) {
    Text = formatv("pushp\t{0}", Payload & 0x7).str();
    return true;
  }
  if ((Payload & 0x78) == 0x18) {
    Text = formatv("popp\t{0}", Payload & 0x7).str();
    return true;
  }
  if ((Payload & 0x78) == 0x70) {
    Text = formatv("fpushp\t{0}", Payload & 0x7).str();
    return true;
  }
  if ((Payload & 0x78) == 0x78) {
    Text = formatv("fpopp\t{0}", Payload & 0x7).str();
    return true;
  }

  unsigned Reg = Payload & 0xf;
  switch (Payload >> 4) {
  case 0x2:
    Text = formatv("push\tr{0}", Reg).str();
    return true;
  case 0x3:
    Text = formatv("pop\tr{0}", Reg).str();
    return true;
  case 0x4:
    Text = formatv("mov.q\tr{0}, sp", Reg).str();
    return true;
  case 0x5:
    Text = formatv("mov.q\tsp, r{0}", Reg).str();
    return true;
  case 0x6:
    Text = formatv("clr.q\tr{0}", Reg).str();
    return true;
  default:
    return false;
  }
}

bool decodeShortPayload(uint16_t Payload, SmallString<128> &Text) {
  struct FixedForm {
    uint16_t Payload;
    StringRef Mnemonic;
  };

  static const FixedForm FixedForms[] = {
      {0x2049, "halt"},
      {0x204e, "reset"},
  };

  for (const FixedForm &Form : FixedForms) {
    if (Payload == Form.Payload) {
      Text = Form.Mnemonic;
      return true;
    }
  }

  if ((Payload & 0x3ff8) == 0x2280 ||
      (Payload & 0x3ff8) == 0x2288) {
    const char *SReg = getSRegName(Payload & 0x7);
    if (!SReg)
      return false;
    Text = formatv("{0}\t{1}", (Payload & 0x8) ? "pop" : "push", SReg).str();
    return true;
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
    Text = formatv("set{0}\tr{1}", CondName, Reg).str();
    return true;
  }

  struct UnaryForm {
    uint16_t Prefix;
    StringRef Mnemonic;
  };
  static const UnaryForm UnaryForms[] = {
      {0x220, "inc.l"},     {0x230, "inc.q"},     {0x221, "dec.l"},
      {0x231, "dec.q"},     {0x222, "neg.l"},     {0x232, "neg.q"},
      {0x223, "clr.l"},     {0x224, "abs.l"},
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
    Text = formatv("j{0}\t{1}", Cond, Imm8).str();
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

bool getCompactEATailBytes(uint8_t EA, unsigned &TailBytes) {
  if (EA <= 0x1f) {
    TailBytes = 0;
    return true;
  }
  if (EA >= 0x20 && EA <= 0x5f) {
    TailBytes = 1u << ((EA >> 4) - 2);
    return true;
  }
  if (EA >= 0x60 && EA <= 0x67) {
    TailBytes = 1u << (EA & 0x3);
    return true;
  }
  if (EA == 0x68 || EA == 0x69) {
    TailBytes = 0;
    return true;
  }
  if (EA == 0x6a || EA == 0x6b) {
    TailBytes = EA == 0x6a ? 4 : 8;
    return true;
  }
  if (EA >= 0x6c && EA <= 0x6f) {
    TailBytes = 1u << (EA - 0x6c);
    return true;
  }
  return false;
}

bool isCompactEAImmediate(uint8_t EA) { return EA >= 0x6c && EA <= 0x6f; }

bool isCompactEARegister(uint8_t EA) { return EA <= 0x0f || EA == 0x68; }

bool isCompactEAMemory(uint8_t EA) {
  return !isCompactEARegister(EA) && !isCompactEAImmediate(EA) && EA <= 0x74;
}

static const char *getFpuSizeSuffix(unsigned Size) {
  switch (Size) {
  case 0:
    return "S";
  case 1:
    return "D";
  default:
    return nullptr;
  }
}

[[maybe_unused]] bool getFpuRawInstSize(ArrayRef<uint8_t> Bytes,
                                        uint64_t &Size) {
  if (Bytes.size() < 2)
    return false;

  uint16_t Word0 = readBE16(Bytes, 0);
  if (Word0 != 0x1f65 && Word0 != 0x1f67)
    return false;

  Size = 4;
  if (Bytes.size() < 4)
    return false;

  uint16_t Ext = readBE16(Bytes, 2);
  uint8_t EA = 0;
  bool HasEA = false;

  if (Word0 == 0x1f65) {
    if (Ext >= 0x1800 && Ext <= 0x1fff) {
      EA = Ext & 0x3f;
      HasEA = true;
    } else if (Ext >= 0x2000 && Ext <= 0x27ff) {
      EA = Ext & 0x3f;
      HasEA = true;
    } else if ((Ext >= 0x0280 && Ext <= 0x02ff) ||
               (Ext >= 0x0400 && Ext <= 0x05ff) ||
               (Ext >= 0x0600 && Ext <= 0x06ff)) {
      HasEA = false;
    } else {
      return false;
    }
  } else if (!((Ext >= 0x0200 && Ext <= 0x03ff) ||
               (Ext >= 0x2000 && Ext <= 0x21ff) ||
               (Ext >= 0x8a00 && Ext <= 0x8bff) ||
               (Ext >= 0xc600 && Ext <= 0xc7ff))) {
    return false;
  }

  if (HasEA) {
    unsigned TailBytes = 0;
    if (!getCompactEATailBytes(EA, TailBytes))
      return false;
    Size += TailBytes;
  }

  return Bytes.size() >= Size;
}

[[maybe_unused]] bool decodeFpuRawInst(ArrayRef<uint8_t> Bytes,
                                       SmallString<128> &Text) {
  if (Bytes.size() < 4)
    return false;

  uint16_t Word0 = readBE16(Bytes, 0);
  uint16_t Ext = readBE16(Bytes, 2);

  if (Word0 == 0x1f65) {
    if (Ext >= 0x0400 && Ext <= 0x05ff) {
      const char *Suffix = getFpuSizeSuffix((Ext >> 8) & 0x1);
      unsigned Src = Ext & 0xf;
      unsigned Dst = (Ext >> 4) & 0xf;
      Text = formatv("FMOV.{0}\tf{1}, f{2}", Suffix, Src, Dst).str();
      return true;
    }

    if ((Ext >= 0x0280 && Ext <= 0x02ff) ||
        (Ext >= 0x0680 && Ext <= 0x06ff)) {
      bool IsUnsigned = Ext >= 0x0680;
      unsigned Src = Ext & 0x7;
      unsigned Dst = (Ext >> 3) & 0xf;
      Text = formatv("{0}\tr{1}, f{2}", IsUnsigned ? "FCVTU" : "FCVT", Src,
                     Dst)
                 .str();
      return true;
    }

    if (Ext >= 0x0600 && Ext <= 0x067f) {
      unsigned Dst = Ext & 0x7;
      unsigned Src = (Ext >> 3) & 0xf;
      Text = formatv("FCVT\tf{0}, r{1}", Src, Dst).str();
      return true;
    }

    if ((Ext >= 0x1800 && Ext <= 0x1fff) ||
        (Ext >= 0x2000 && Ext <= 0x27ff)) {
      bool IsLoad = Ext < 0x2000;
      const char *Suffix = getFpuSizeSuffix((Ext >> 10) & 0x1);
      unsigned Reg = (Ext >> 6) & 0xf;
      uint8_t EA = Ext & 0x3f;
      unsigned Consumed = 0;
      SmallString<64> EAText;
      if (!decodeCompactEA(EA, Bytes.drop_front(4), Consumed, EAText))
        return false;
      if (IsLoad)
        Text = formatv("FMOV.{0}\t{1}, f{2}", Suffix, EAText, Reg).str();
      else
        Text = formatv("FMOV.{0}\tf{1}, {2}", Suffix, Reg, EAText).str();
      return true;
    }
  }

  if (Word0 == 0x1f67) {
    StringRef Mnemonic;
    if (Ext >= 0x0200 && Ext <= 0x03ff)
      Mnemonic = "FADD";
    else if (Ext >= 0x2000 && Ext <= 0x21ff)
      Mnemonic = "FDIV";
    else if (Ext >= 0x8a00 && Ext <= 0x8bff)
      Mnemonic = "FMUL";
    else if (Ext >= 0xc600 && Ext <= 0xc7ff)
      Mnemonic = "FSUB";
    else
      return false;

    const char *Suffix = getFpuSizeSuffix((Ext >> 8) & 0x1);
    unsigned Src = Ext & 0xf;
    unsigned Dst = (Ext >> 4) & 0xf;
    Text = formatv("{0}.{1}\tf{2}, f{3}", Mnemonic, Suffix, Src, Dst).str();
    return true;
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

bool decodeMediumFpuRR(uint32_t Payload, SmallString<128> &Text) {
  struct Form {
    StringRef Mnemonic;
    StringRef Pattern;
  };
  static const Form Forms[] = {
      {"fmov", "100100zssss110dddd"},
      {"fadd", "100100zssss111dddd"},
      {"fsub", "100101zssss000dddd"},
      {"fmul", "100101zssss001dddd"},
      {"fdiv", "100101zssss010dddd"},
  };

  for (const Form &F : Forms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    unsigned Size = extractPatternField(F.Pattern, Payload, 'z');
    unsigned Src = extractPatternField(F.Pattern, Payload, 's');
    unsigned Dst = extractPatternField(F.Pattern, Payload, 'd');
    Text = formatv("{0}.{1}\tf{2}, f{3}", F.Mnemonic,
                   Size ? 'd' : 's', Src, Dst)
               .str();
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
  if (decodeMediumFpuRR(Payload, Text))
    return true;
  if (decodeMediumBinary(Payload, Tail, Text))
    return true;
  if (decodeMediumRRExt(Payload, Tail, Text))
    return true;

  if ((Payload & 0x3fff0) == 0x2d80) {
    Text = formatv("setf\t{0}", Payload & 0xf).str();
    return true;
  }
  if ((Payload & 0x3fff0) == 0x2580) {
    Text = formatv("clrf\t{0}", Payload & 0xf).str();
    return true;
  }

  static const char *SizeSuffixes = "bwlq";
  if (matchPattern("000010z0z01000rrrr", Payload)) {
    unsigned Z = extractPatternField("000010z0z01000rrrr", Payload, 'z');
    Text = formatv("incf.{0}\tr{1}", SizeSuffixes[Z],
                   extractPatternField("000010z0z01000rrrr", Payload, 'r'))
               .str();
    return true;
  }
  if (matchPattern("000010z0z11000rrrr", Payload)) {
    unsigned Z = extractPatternField("000010z0z11000rrrr", Payload, 'z');
    Text = formatv("decf.{0}\tr{1}", SizeSuffixes[Z],
                   extractPatternField("000010z0z11000rrrr", Payload, 'r'))
               .str();
    return true;
  }

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
    Text = formatv("j{0}\t", Cond).str();
    appendSignedImm(Text, Tail, 2);
    return true;
  }
  if ((Payload & 0x3fff0) == 0x6600) {
    const char *Cond = getCondName(Payload & 0xf);
    if (!Cond || !NeedTail(4))
      return false;
    Text = formatv("j{0}\t", Cond).str();
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
    Text = formatv("call{0}\t", Cond).str();
    appendSignedImm(Text, Tail, 2);
    return true;
  }
  if ((Payload & 0x3fff0) == 0xe600) {
    const char *Cond = getCondName(Payload & 0xf);
    if (!Cond || !NeedTail(4))
      return false;
    Text = formatv("call{0}\t", Cond).str();
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

  return false;
}

bool decodeLongPayload(uint32_t Payload, ArrayRef<uint8_t> Tail,
                       SmallString<128> &Text) {
  struct LongFMAForm {
    StringRef Mnemonic;
    StringRef Pattern;
  };
  static const LongFMAForm FMAForms[] = {
      {"fmadd", "1111010000zllllrrrr100dddd"},
      {"fmsub", "1111010000zllllrrrr101dddd"},
      {"fnmadd", "1111010000zllllrrrr110dddd"},
      {"fnmsub", "1111010000zllllrrrr111dddd"},
  };
  for (const LongFMAForm &F : FMAForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    unsigned Size = extractPatternField(F.Pattern, Payload, 'z');
    Text = formatv("{0}.{1}\tf{2}, f{3}, f{4}", F.Mnemonic,
                   Size ? 'd' : 's',
                   extractPatternField(F.Pattern, Payload, 'l'),
                   extractPatternField(F.Pattern, Payload, 'r'),
                   extractPatternField(F.Pattern, Payload, 'd'))
               .str();
    return true;
  }

  struct LongFPTRANSAForm {
    StringRef Mnemonic;
    StringRef Pattern;
  };
  static const LongFPTRANSAForm FPTRANSAForms[] = {
      {"facosa", "1111011100z0000dddd000ssss"},
      {"fasina", "1111011100z0000dddd001ssss"},
      {"fatana", "1111011100z0000dddd010ssss"},
      {"fatanha", "1111011100z0000dddd011ssss"},
      {"fcosa", "1111011100z0000dddd100ssss"},
      {"fcosha", "1111011100z0000dddd101ssss"},
      {"fetoxa", "1111011100z0000dddd110ssss"},
      {"fetoxm1a", "1111011100z0000dddd111ssss"},
      {"flog10a", "1111011100z0001dddd000ssss"},
      {"flog2a", "1111011100z0001dddd001ssss"},
      {"flogna", "1111011100z0001dddd010ssss"},
      {"flognp1a", "1111011100z0001dddd011ssss"},
      {"fsina", "1111011100z0001dddd100ssss"},
      {"fsinha", "1111011100z0001dddd110ssss"},
      {"ftana", "1111011100z0001dddd111ssss"},
      {"ftanha", "1111011100z0010dddd000ssss"},
      {"ftentoxa", "1111011100z0010dddd001ssss"},
      {"ftwotoxa", "1111011100z0010dddd010ssss"},
  };
  for (const LongFPTRANSAForm &F : FPTRANSAForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    unsigned Size = extractPatternField(F.Pattern, Payload, 'z');
    Text = formatv("{0}.{1}\tf{2}, f{3}", F.Mnemonic, Size ? 'd' : 's',
                   extractPatternField(F.Pattern, Payload, 's'),
                   extractPatternField(F.Pattern, Payload, 'd'))
               .str();
    return true;
  }

  struct LongFpuMemoryForm {
    StringRef Pattern;
    bool IsLoad;
    char RegField;
  };
  static const LongFpuMemoryForm LongFpuMemoryForms[] = {
      {"1111010101z0000ddddeeeeeee", true, 'd'},
      {"1111011000z0000sssseeeeeee", false, 's'},
  };
  for (const LongFpuMemoryForm &F : LongFpuMemoryForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    unsigned Size = extractPatternField(F.Pattern, Payload, 'z');
    unsigned Reg = extractPatternField(F.Pattern, Payload, F.RegField);
    uint8_t EA = extractPatternField(F.Pattern, Payload, 'e');
    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;
    if (F.IsLoad)
      Text = formatv("FMOV.{0}\t{1}, f{2}", Size ? 'D' : 'S', EAText, Reg)
                 .str();
    else
      Text = formatv("FMOV.{0}\tf{1}, {2}", Size ? 'D' : 'S', Reg, EAText)
                 .str();
    return true;
  }

  constexpr StringLiteral FClassPattern = "1111010111z0000ssss010dddd";
  if (matchPattern(FClassPattern, Payload)) {
    unsigned Size = extractPatternField(FClassPattern, Payload, 'z');
    unsigned Src = extractPatternField(FClassPattern, Payload, 's');
    unsigned Dst = extractPatternField(FClassPattern, Payload, 'd');
    Text = formatv("fclass.{0}\tf{1}, r{2}", Size ? 'd' : 's', Src, Dst).str();
    return true;
  }

  constexpr StringLiteral FMovCRPattern = "1111010110z01100010000dddd";
  if (matchPattern(FMovCRPattern, Payload)) {
    if (Tail.size() < 2)
      return false;
    unsigned Size = extractPatternField(FMovCRPattern, Payload, 'z');
    unsigned Dst = extractPatternField(FMovCRPattern, Payload, 'd');
    Text = formatv("fmovcr.{0}\t{1}, f{2}", Size ? 'd' : 's',
                   readLE(Tail, 0, 2), Dst)
               .str();
    return true;
  }

  enum class LongDir { RnEA, EARn };
  struct LongRegEAForm {
    StringRef Mnemonic;
    StringRef Pattern;
    StringRef Suffixes;
    LongDir Dir;
    char RegField;
    bool RequireNonRegEA;
    bool RequireMemoryEA;
    bool AllowImmediateEA;
  };

  static const LongRegEAForm LongRegEAForms[] = {
      {"adc", "1111000000zz000sssseeeeeee", "bwlq", LongDir::RnEA, 's', true,
       false, false},
      {"adc", "1111000000zz001sssseeeeeee", "bwlq", LongDir::EARn, 's', true,
       false, true},
      {"sbb", "1111000000zz010sssseeeeeee", "bwlq", LongDir::RnEA, 's', true,
       false, false},
      {"sbb", "1111000000zz011ddddeeeeeee", "bwlq", LongDir::EARn, 'd', true,
       false, true},
      {"clz", "1111000000zz100ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
       false, true},
      {"ctz", "1111000000zz101ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
       false, true},
      {"cls", "1111000000zz110ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
       false, true},
      {"cts", "1111000000zz111ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
       false, true},
      {"minu", "1111000001zz000ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
       false, true},
      {"minu", "1111000001zz001sssseeeeeee", "bwlq", LongDir::RnEA, 's', true,
       false, false},
      {"mins", "1111000001zz010ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
       false, true},
      {"mins", "1111000001zz011sssseeeeeee", "bwlq", LongDir::RnEA, 's', true,
       false, false},
      {"maxu", "1111000001zz100ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
       false, true},
      {"maxu", "1111000001zz101sssseeeeeee", "bwlq", LongDir::RnEA, 's', true,
       false, false},
      {"maxs", "1111000001zz110ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
       false, true},
      {"maxs", "1111000001zz111sssseeeeeee", "bwlq", LongDir::RnEA, 's', true,
       false, false},
      {"popcnt", "1111000010zz000ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
       false, false, true},
      {"parity", "1111000010zz001ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
       false, false, true},
      {"mul", "1111000010zz010ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
       false, true},
      {"clmul", "1111000010zz011ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
       false, false, true},
      {"divu", "1111000010zz100ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
       false, true},
      {"divs", "1111000010zz101ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
       false, true},
      {"modu", "1111000010zz110ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
       false, true},
      {"mods", "1111000010zz111ddddeeeeeee", "bwlq", LongDir::EARn, 'd', false,
       false, true},
      {"btest", "111100001100000bbbbeeeeeee", "", LongDir::RnEA, 'b', false,
       false, true},
      {"bset", "111100001100001bbbbeeeeeee", "", LongDir::RnEA, 'b', false,
       false, false},
      {"bclr", "111100001100010bbbbeeeeeee", "", LongDir::RnEA, 'b', false,
       false, false},
      {"bchg", "111100001100011bbbbeeeeeee", "", LongDir::RnEA, 'b', false,
       false, false},
      {"lcall", "111100001100100rrrreeeeeee", "", LongDir::RnEA, 'r', false,
       false, true},
      {"ljmp", "111100001100101rrrreeeeeee", "", LongDir::RnEA, 'r', false,
       false, true},
      {"clmulh.q", "111100001100110ddddeeeeeee", "", LongDir::EARn, 'd', false,
       false, true},
      {"seglea", "111100011zz0000ddddeeeeeee", "bwlq", LongDir::EARn, 'd',
       false, false, true},
      {"movnt", "1111001000zz000sssseeeeeee", "bwlq", LongDir::RnEA, 's',
       false, true, false},
  };

  for (const LongRegEAForm &F : LongRegEAForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;

    uint8_t EA = extractPatternField(F.Pattern, Payload, 'e');
    if (F.RequireNonRegEA && EA < 0x10)
      continue;
    if (F.RequireMemoryEA && !isCompactEAMemory(EA))
      continue;
    if (!F.AllowImmediateEA && isCompactEAImmediate(EA))
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

  if (matchPattern("111100101zzhhhhlllliiiiiii", Payload)) {
    unsigned Z = extractPatternField("111100101zzhhhhlllliiiiiii", Payload, 'z');
    unsigned Imm =
        extractPatternField("111100101zzhhhhlllliiiiiii", Payload, 'i');
    if (Imm >= (16u << Z))
      return false;
    Text =
        formatv("extract.{0}\t{1}, r{2}, r{3}", "bwlq"[Z], Imm,
                extractPatternField("111100101zzhhhhlllliiiiiii", Payload, 'h'),
                extractPatternField("111100101zzhhhhlllliiiiiii", Payload, 'l'))
            .str();
    return true;
  }

  struct LongCmpTestJumpForm {
    StringRef Base;
    StringRef Pattern;
    unsigned TailWidth;
  };
  static const LongCmpTestJumpForm LongCmpTestJumpForms[] = {
      {"cmpj", "111100010zzccccssss000dddd", 1},
      {"cmpj", "111100010zzccccssss001dddd", 2},
      {"testj", "111100010zzccccssss010dddd", 1},
      {"testj", "111100010zzccccssss011dddd", 2},
  };
  for (const LongCmpTestJumpForm &F : LongCmpTestJumpForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    unsigned Z = extractPatternField(F.Pattern, Payload, 'z');
    unsigned CondCode = extractPatternField(F.Pattern, Payload, 'c');
    const char *Cond = getCondName(CondCode);
    if (!Cond)
      return false;

    if (Tail.size() < F.TailWidth)
      return false;

    Text = formatv("{0}{1}.{2}\tr{3}, r{4}, ", F.Base, Cond, "bwlq"[Z],
                   extractPatternField(F.Pattern, Payload, 's'),
                   extractPatternField(F.Pattern, Payload, 'd'))
               .str();
    appendSignedImm(Text, Tail, F.TailWidth);
    return true;
  }

  struct LongControlEAForm {
    StringRef Mnemonic;
    StringRef Pattern;
  };
  static const LongControlEAForm LongControlEAForms[] = {
      {"call", "1111000011011100000eeeeeee"},
  };
  for (const LongControlEAForm &F : LongControlEAForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    uint8_t EA = extractPatternField(F.Pattern, Payload, 'e');
    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;
    Text = formatv("{0}\t{1}", F.Mnemonic, EAText).str();
    return true;
  }


  constexpr StringLiteral CallCCPattern = "111100001101110cccceeeeeee";
  if (matchPattern(CallCCPattern, Payload)) {
    unsigned CondCode = extractPatternField(CallCCPattern, Payload, 'c');
    const char *Cond = getCondName(CondCode);
    if (!Cond)
      return false;
    uint8_t EA = extractPatternField(CallCCPattern, Payload, 'e');
    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;
    Text = formatv("call{0}\t{1}", Cond, EAText).str();
    return true;
  }

  constexpr StringLiteral JmpXPattern = "1111001001z00000000eeeeeee";
  if (matchPattern(JmpXPattern, Payload)) {
    uint8_t EA = extractPatternField(JmpXPattern, Payload, 'e');
    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;
    unsigned Size = extractPatternField(JmpXPattern, Payload, 'z');
    Text = formatv("jmp.{0}\t{1}", Size ? 'q' : 'l', EAText).str();
    return true;
  }

  constexpr StringLiteral JccXPattern = "1111001001z0000cccceeeeeee";
  if (matchPattern(JccXPattern, Payload)) {
    unsigned CondCode = extractPatternField(JccXPattern, Payload, 'c');
    const char *Cond = getCondName(CondCode);
    if (!Cond)
      return false;
    uint8_t EA = extractPatternField(JccXPattern, Payload, 'e');
    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;
    unsigned Size = extractPatternField(JccXPattern, Payload, 'z');
    Text = formatv("j{0}.{1}\t{2}", Cond, Size ? 'q' : 'l', EAText).str();
    return true;
  }

  if (matchPattern("11110000111ccccrrrreeeeeee", Payload)) {
    unsigned CondCode =
        extractPatternField("11110000111ccccrrrreeeeeee", Payload, 'c');
    if (CondCode == 0x1)
      return false;
    const char *Cond = getFullCondName(CondCode);
    if (!Cond)
      return false;
    uint8_t EA = extractPatternField("11110000111ccccrrrreeeeeee", Payload, 'e');
    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;
    Text = formatv("dj{0}\tr{1}, {2}", Cond,
                   extractPatternField("11110000111ccccrrrreeeeeee", Payload,
                                       'r'),
                   EAText)
               .str();
    return true;
  }

  struct LongEAOnlyForm {
    StringRef Mnemonic;
    StringRef Pattern;
    StringRef Suffixes;
    bool RequireMemoryEA;
  };
  static const LongEAOnlyForm LongEAOnlyForms[] = {
      {"invpage", "1111101111010000010eeeeeee", "", false},
      {"flshdcache", "1111101111010000011eeeeeee", "", true},
      {"invdcache", "1111101111010000100eeeeeee", "", true},
      {"invicache", "1111101111010000101eeeeeee", "", true},
      {"prefetch", "1111101111010000110eeeeeee", "", true},
      {"synccache", "1111101111010000111eeeeeee", "", true},
      {"wrbkdcache", "1111101111010001000eeeeeee", "", true},
      {"save", "1111101111010001001eeeeeee", "", false},
      {"restore", "1111101111010001010eeeeeee", "", false},
      {"prefetchnt", "1111101111010001011eeeeeee", "", true},
  };

  for (const LongEAOnlyForm &F : LongEAOnlyForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    uint8_t EA = extractPatternField(F.Pattern, Payload, 'e');
    if (F.RequireMemoryEA && !isCompactEAMemory(EA))
      continue;

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

  struct LongEAEAForm {
    StringRef Mnemonic;
    StringRef Pattern;
    StringRef Suffixes;
    bool RequireSrcNonReg;
    bool RequireDstNonReg;
    bool RequireSrcMemory;
    bool RequireDstMemory;
    bool AllowSrcImmediate;
    bool AllowDstImmediate;
  };
  static const LongEAEAForm LongEAEAForms[] = {
      {"mov", "1111100000zzsssssssddddddd", "bwlq", true, true, false, false,
       true, false},
      {"cmp", "1111100001zzsssssssddddddd", "bwlq", true, true, false, false,
       true, true},
      {"movuc", "1111100010zzsssssssddddddd", "bwlq", false, false, true,
       false, false, false},
      {"movcu", "1111100011zzsssssssddddddd", "bwlq", false, false, false,
       true, true, false},
      {"movuu", "1111100100zzsssssssddddddd", "bwlq", false, false, true,
       true, false, false},
      {"extsw.b", "111110100000sssssssddddddd", "", true, true, false, false,
       true, false},
      {"extsq.b", "111110100001sssssssddddddd", "", true, true, false, false,
       true, false},
      {"extsq.w", "111110100010sssssssddddddd", "", true, true, false, false,
       true, false},
      {"extsq.l", "111110100011sssssssddddddd", "", true, true, false, false,
       true, false},
      {"extzw.b", "111110100100sssssssddddddd", "", true, true, false, false,
       true, false},
      {"extzq.b", "111110100101sssssssddddddd", "", true, true, false, false,
       true, false},
      {"extzq.w", "111110100110sssssssddddddd", "", true, true, false, false,
       true, false},
      {"extzq.l", "111110100111sssssssddddddd", "", true, true, false, false,
       true, false},
      {"extsl", "11111010100zsssssssddddddd", "bw", true, true, false, false,
       true, false},
      {"extzl", "11111010101zsssssssddddddd", "bw", true, true, false, false,
       true, false},
  };

  for (const LongEAEAForm &F : LongEAEAForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    uint8_t SrcEA = extractPatternField(F.Pattern, Payload, 's');
    uint8_t DstEA = extractPatternField(F.Pattern, Payload, 'd');
    if (F.RequireSrcNonReg && SrcEA < 0x10)
      continue;
    if (F.RequireDstNonReg && DstEA < 0x10)
      continue;
    if (F.RequireSrcMemory && !isCompactEAMemory(SrcEA))
      continue;
    if (F.RequireDstMemory && !isCompactEAMemory(DstEA))
      continue;
    if (!F.AllowSrcImmediate && isCompactEAImmediate(SrcEA))
      continue;
    if (!F.AllowDstImmediate && isCompactEAImmediate(DstEA))
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

  if (matchPattern("111110111101001vvvv000pppp", Payload)) {
    Text =
        formatv("vtop\tr{0}, r{1}",
                extractPatternField("111110111101001vvvv000pppp", Payload, 'v'),
                extractPatternField("111110111101001vvvv000pppp", Payload, 'p'))
            .str();
    return true;
  }
  if (matchPattern("111110111101001aaaa001pppp", Payload)) {
    Text =
        formatv("swpta\tr{0}, r{1}",
                extractPatternField("111110111101001aaaa001pppp", Payload, 'p'),
                extractPatternField("111110111101001aaaa001pppp", Payload, 'a'))
            .str();
    return true;
  }
  if (matchPattern("1111101111010001111100dddd", Payload)) {
    Text = formatv(
               "rdseg\tcs, r{0}",
               extractPatternField("1111101111010001111100dddd", Payload,
                                   'd'))
               .str();
    return true;
  }
  if (matchPattern("1111101111010000000sssdddd", Payload)) {
    unsigned SReg =
        extractPatternField("1111101111010000000sssdddd", Payload, 's');
    const char *SRegName = getSRegName(SReg);
    if (!SRegName)
      return false;
    Text =
        formatv("rdseg\t{0}, r{1}", SRegName,
                extractPatternField("1111101111010000000sssdddd", Payload, 'd'))
            .str();
    return true;
  }
  if (matchPattern("1111101111010000001sssdddd", Payload)) {
    unsigned SReg =
        extractPatternField("1111101111010000001sssdddd", Payload, 's');
    const char *SRegName = getSRegName(SReg);
    if (!SRegName)
      return false;
    Text =
        formatv("wrseg\tr{0}, {1}",
                extractPatternField("1111101111010000001sssdddd", Payload, 'd'),
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
      {"rdcr", "1111101111010001110000dddd", true, 'd'},
      {"wrcr", "1111101111010001110001ssss", false, 's'},
      {"rdpmc", "1111101111010001111010dddd", true, 'd'},
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
      {"rdflags", "1111101111010001110010dddd", 'd'},
      {"wrflags", "1111101111010001110011ssss", 's'},
      {"rdfflags", "1111101111010001110100dddd", 'd'},
      {"wrfflags", "1111101111010001110101ssss", 's'},
      {"rdstatus", "1111101111010001110110dddd", 'd'},
      {"wrstatus", "1111101111010001110111ssss", 's'},
      {"rdfstatus", "1111101111010001111000dddd", 'd'},
      {"wrfstatus", "1111101111010001111001ssss", 's'},
      {"swpt", "1111101111010001111011pppp", 'p'},
  };
  for (const LongSysRegForm &F : SysRegForms) {
    if (!matchPattern(F.Pattern, Payload))
      continue;
    Text = formatv("{0}\tr{1}", F.Mnemonic,
                   extractPatternField(F.Pattern, Payload, F.RegField))
               .str();
    return true;
  }

  if (Payload == 0x3ef4600) {
    if (Tail.size() < 2)
      return false;
    Text = formatv("invasid\t{0}", readLE(Tail, 0, 2)).str();
    return true;
  }
  if (Payload == 0x3ef4601) {
    Text = "invtlb";
    return true;
  }

  struct LongQRRForm {
    StringRef Mnemonic;
    StringRef Pattern;
  };
  static const LongQRRForm QRRForms[] = {
      {"mulhu.q", "111110111101001ssss100dddd"},
      {"mulhs.q", "111110111101001ssss101dddd"},
      {"mulhsu.q", "111110111101001ssss110dddd"},
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

bool decodeExtraLongPayload(uint64_t Payload, ArrayRef<uint8_t> Tail,
                            SmallString<128> &Text) {
  struct ExtraFMAForm {
    StringRef Mnemonic;
    StringRef Pattern;
    bool LeftEA;
  };
  static const ExtraFMAForm FMAForms[] = {
      {"fmadd", "1111110000001z00rrrr000ddddlllllll", true},
      {"fmadd", "1111110000001z00llll100ddddrrrrrrr", false},
      {"fmsub", "1111110000001z00rrrr001ddddlllllll", true},
      {"fmsub", "1111110000001z00llll101ddddrrrrrrr", false},
      {"fnmadd", "1111110000001z00rrrr010ddddlllllll", true},
      {"fnmadd", "1111110000001z00llll110ddddrrrrrrr", false},
      {"fnmsub", "1111110000001z00rrrr011ddddlllllll", true},
      {"fnmsub", "1111110000001z00llll111ddddrrrrrrr", false},
  };
  for (const ExtraFMAForm &F : FMAForms) {
    if (!matchPattern64(F.Pattern, Payload))
      continue;
    char EAField = F.LeftEA ? 'l' : 'r';
    uint8_t EA = extractPatternField64(F.Pattern, Payload, EAField);
    if (EA < 0x10)
      return false;
    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;
    unsigned Size = extractPatternField64(F.Pattern, Payload, 'z');
    unsigned Reg = extractPatternField64(F.Pattern, Payload,
                                         F.LeftEA ? 'r' : 'l');
    unsigned Dst = extractPatternField64(F.Pattern, Payload, 'd');
    if (F.LeftEA)
      Text = formatv("{0}.{1}\t{2}, f{3}, f{4}", F.Mnemonic,
                     Size ? 'd' : 's', EAText, Reg, Dst)
                 .str();
    else
      Text = formatv("{0}.{1}\tf{2}, {3}, f{4}", F.Mnemonic,
                     Size ? 'd' : 's', Reg, EAText, Dst)
                 .str();
    return true;
  }

  constexpr StringLiteral FSincosPattern =
      "1111110000001z01ssss000dddd000cccc";
  if (matchPattern64(FSincosPattern, Payload)) {
    unsigned Size = extractPatternField64(FSincosPattern, Payload, 'z');
    Text = formatv("fsincosa.{0}\tf{1}, f{2}, f{3}", Size ? 'd' : 's',
                   extractPatternField64(FSincosPattern, Payload, 's'),
                   extractPatternField64(FSincosPattern, Payload, 'd'),
                   extractPatternField64(FSincosPattern, Payload, 'c'))
               .str();
    return true;
  }

  struct ExtraMovccForm {
    StringRef Pattern;
    bool RegToEA;
  };
  static const ExtraMovccForm MovccForms[] = {
      {"111111000011zz00cccc000sssseeeeeee", true},
      {"111111000011zz00cccc001ddddeeeeeee", false},
  };
  for (const ExtraMovccForm &F : MovccForms) {
    if (!matchPattern64(F.Pattern, Payload))
      continue;

    const char *Cond = getCondName(extractPatternField64(F.Pattern, Payload, 'c'));
    if (!Cond)
      return false;

    uint8_t EA = extractPatternField64(F.Pattern, Payload, 'e');
    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;

    unsigned Z = extractPatternField64(F.Pattern, Payload, 'z');
    if (Z >= 4)
      return false;
    if (F.RegToEA) {
      unsigned Reg = extractPatternField64(F.Pattern, Payload, 's');
      Text = formatv("mov{0}.{1}\tr{2}, {3}", Cond, "bwlq"[Z], Reg, EAText)
                 .str();
    } else {
      unsigned Reg = extractPatternField64(F.Pattern, Payload, 'd');
      Text = formatv("mov{0}.{1}\t{2}, r{3}", Cond, "bwlq"[Z], EAText, Reg)
                 .str();
    }
    return true;
  }

  if (matchPattern64("111111000100cccciiii000bbbbeeeeeee", Payload)) {
    unsigned CondCode =
        extractPatternField64("111111000100cccciiii000bbbbeeeeeee", Payload,
                              'c');
    if (CondCode == 0x1)
      return false;
    const char *Cond = getFullCondName(CondCode);
    if (!Cond)
      return false;

    uint8_t EA =
        extractPatternField64("111111000100cccciiii000bbbbeeeeeee", Payload,
                              'e');
    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;

    Text =
        formatv("ij{0}\tr{1}, r{2}, {3}", Cond,
                extractPatternField64("111111000100cccciiii000bbbbeeeeeee",
                                      Payload, 'i'),
                extractPatternField64("111111000100cccciiii000bbbbeeeeeee",
                                      Payload, 'b'),
                EAText)
            .str();
    return true;
  }

  struct ExtraBndForm {
    StringRef Base;
    StringRef Pattern;
  };
  static const ExtraBndForm BndForms[] = {
      {"bndsii", "111111000001zz00llll000hhhheeeeeee"},
      {"bndsix", "111111000001zz00llll001hhhheeeeeee"},
      {"bndsxi", "111111000001zz00llll010hhhheeeeeee"},
      {"bndsxx", "111111000001zz00llll011hhhheeeeeee"},
      {"bnduii", "111111000001zz00llll100hhhheeeeeee"},
      {"bnduix", "111111000001zz00llll101hhhheeeeeee"},
      {"bnduxi", "111111000001zz00llll110hhhheeeeeee"},
      {"bnduxx", "111111000001zz00llll111hhhheeeeeee"},
  };
  for (const ExtraBndForm &F : BndForms) {
    if (!matchPattern64(F.Pattern, Payload))
      continue;

    uint8_t EA = extractPatternField64(F.Pattern, Payload, 'e');
    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;
    unsigned Z = extractPatternField64(F.Pattern, Payload, 'z');
    if (Z >= 4)
      return false;
    Text = formatv("{0}.{1}\tr{2}, {3}, r{4}", F.Base, "bwlq"[Z],
                   extractPatternField64(F.Pattern, Payload, 'l'), EAText,
                   extractPatternField64(F.Pattern, Payload, 'h'))
               .str();
    return true;
  }

  struct ExtraDivModForm {
    StringRef Base;
    StringRef Pattern;
  };
  static const ExtraDivModForm DivModForms[] = {
      {"divmodu", "111111000010zz00qqqq000rrrreeeeeee"},
      {"divmods", "111111000010zz00qqqq001rrrreeeeeee"},
  };
  for (const ExtraDivModForm &F : DivModForms) {
    if (!matchPattern64(F.Pattern, Payload))
      continue;

    uint8_t EA = extractPatternField64(F.Pattern, Payload, 'e');
    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;
    unsigned Z = extractPatternField64(F.Pattern, Payload, 'z');
    if (Z >= 4)
      return false;
    Text = formatv("{0}.{1}\t{2}, r{3}, r{4}", F.Base, "bwlq"[Z], EAText,
                   extractPatternField64(F.Pattern, Payload, 'q'),
                   extractPatternField64(F.Pattern, Payload, 'r'))
               .str();
    return true;
  }

  struct ExtraFetchForm {
    StringRef Base;
    StringRef Pattern;
  };
  static const ExtraFetchForm FetchForms[] = {
      {"fetchadd", "111111000101zz000000ooosssseeeeeee"},
      {"fetchand", "111111000101zz000001ooosssseeeeeee"},
      {"fetchor", "111111000101zz000010ooosssseeeeeee"},
      {"fetchsub", "111111000101zz000011ooosssseeeeeee"},
      {"fetchxor", "111111000101zz000100ooosssseeeeeee"},
  };
  for (const ExtraFetchForm &F : FetchForms) {
    if (!matchPattern64(F.Pattern, Payload))
      continue;

    unsigned Order = extractPatternField64(F.Pattern, Payload, 'o');
    const char *OrderName = getMemoryOrderName(Order);
    if (!OrderName)
      return false;

    uint8_t EA = extractPatternField64(F.Pattern, Payload, 'e');
    if (!isCompactEAMemory(EA))
      return false;
    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;
    unsigned Z = extractPatternField64(F.Pattern, Payload, 'z');
    if (Z >= 4)
      return false;
    Text = formatv("{0}.{1}/{2}\tr{3}, {4}", F.Base, "bwlq"[Z], OrderName,
                   extractPatternField64(F.Pattern, Payload, 's'), EAText)
               .str();
    return true;
  }

  if (matchPattern64("111111000101zz01xxxxoooddddeeeeeee", Payload)) {
    unsigned Order =
        extractPatternField64("111111000101zz01xxxxoooddddeeeeeee", Payload,
                              'o');
    const char *OrderName = getMemoryOrderName(Order);
    if (!OrderName)
      return false;

    uint8_t EA =
        extractPatternField64("111111000101zz01xxxxoooddddeeeeeee", Payload,
                              'e');
    if (!isCompactEAMemory(EA))
      return false;
    unsigned Consumed = 0;
    SmallString<64> EAText;
    if (!decodeCompactEA(EA, Tail, Consumed, EAText))
      return false;
    unsigned Z =
        extractPatternField64("111111000101zz01xxxxoooddddeeeeeee", Payload,
                              'z');
    if (Z >= 4)
      return false;
    Text =
        formatv("cmpxchg.{0}/{1}\tr{2}, r{3}, {4}", "bwlq"[Z], OrderName,
                extractPatternField64("111111000101zz01xxxxoooddddeeeeeee",
                                      Payload, 'x'),
                extractPatternField64("111111000101zz01xxxxoooddddeeeeeee",
                                      Payload, 'd'),
                EAText)
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

  Bytes.push_back(0xc0 | ((TotalBytes - 3) << 2) |
                  ((Payload >> 16) & 0x3));
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

  Bytes.push_back(0xc0 | ((TotalBytes - 3) << 2) |
                  ((Payload >> 24) & 0x3));
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
  if (Payload >= (uint64_t(1) << 34) || Selector6 != 0x3f ||
      TotalBytes > 18)
    return false;

  Bytes.push_back(0xc0 | ((TotalBytes - 3) << 2) |
                  ((Payload >> 32) & 0x3));
  Bytes.push_back((Payload >> 24) & 0xff);
  Bytes.push_back((Payload >> 16) & 0xff);
  Bytes.push_back((Payload >> 8) & 0xff);
  Bytes.push_back(Payload & 0xff);
  Bytes.append(Tail.begin(), Tail.end());
  return true;
}

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

bool BedrockMC::decodeRawInst(ArrayRef<uint8_t> Bytes, uint64_t &Size,
                              SmallString<128> &Text) {
  uint64_t FpuSize;
  if (getFpuRawInstSize(Bytes, FpuSize) &&
      decodeFpuRawInst(Bytes.take_front(FpuSize), Text)) {
    Size = FpuSize;
    return true;
  }

  if (!getInstructionSize(Bytes, Size))
    return false;

  uint8_t Byte0 = Bytes[0];
  if ((Byte0 & 0x80) == 0) {
    if (!decodeExtraShortPayload(Byte0 & 0x7f, Text))
      return false;
    return true;
  }

  if ((Byte0 & 0xc0) == 0x80) {
    uint16_t Payload = ((Byte0 & 0x3f) << 8) | Bytes[1];
    if (!decodeShortPayload(Payload, Text))
      return false;
    return true;
  }

  ArrayRef<uint8_t> Inst = Bytes.take_front(Size);
  if (Inst.size() < 3)
    return false;

  uint32_t FirstTen = ((Byte0 & 0x3) << 8) | Inst[1];
  uint32_t MediumPayload = (FirstTen << 8) | Inst[2];
  unsigned Selector6 = MediumPayload >> 12;

  if (isRepgPayload(MediumPayload) && Inst.size() >= 5) {
    uint64_t BodyBytes = readLE(Inst, 3, 2);
    if (BodyBytes != 0 && Bytes.drop_front(Size).size() >= BodyBytes) {
      ArrayRef<uint8_t> Body = Bytes.drop_front(Size).take_front(BodyBytes);
      SmallString<128> BodyText;
      uint64_t BodyOffset = 0;
      bool DecodedBody = true;
      bool FirstBodyInst = true;
      while (BodyOffset != BodyBytes) {
        uint64_t BodyInstSize = 0;
        SmallString<128> BodyInstText;
        if (!decodeRawInst(Body.drop_front(BodyOffset), BodyInstSize,
                           BodyInstText) ||
            BodyInstSize == 0 || BodyOffset + BodyInstSize > BodyBytes) {
          DecodedBody = false;
          break;
        }
        if (!FirstBodyInst)
          BodyText += "; ";
        BodyText += BodyInstText;
        FirstBodyInst = false;
        BodyOffset += BodyInstSize;
      }

      if (DecodedBody && BodyOffset == BodyBytes) {
        Text = formatv("repg\tr{0}, ", MediumPayload & 0xf).str();
        Text += "{ ";
        Text += BodyText;
        Text += " }";
        Size += BodyBytes;
        return true;
      }
    }
  }

  if (isRepPayload(MediumPayload)) {
    ArrayRef<uint8_t> BodyBytes = Bytes.drop_front(Size);
    uint64_t BodySize = 0;
    SmallString<128> BodyText;
    if (BodyBytes.empty() || !decodeRawInst(BodyBytes, BodySize, BodyText))
      return false;

    unsigned Cond = (MediumPayload >> 8) & 0xf;
    unsigned Reg = MediumPayload & 0xf;
    const char *Name = getRepCondName(Cond);
    if (!Name)
      return false;

    Text = formatv("{0}\tr{1}, ({2})", Name, Reg, BodyText).str();
    Size += BodySize;
    return true;
  }

  if (Selector6 == 0x3f) {
    if (Inst.size() < 5)
      return false;
    uint64_t ExtraLongPayload = (uint64_t(FirstTen) << 24) |
                                (uint64_t(Inst[2]) << 16) |
                                (uint64_t(Inst[3]) << 8) | uint64_t(Inst[4]);
    if (!decodeExtraLongPayload(ExtraLongPayload, Inst.drop_front(5), Text))
      return false;
    return true;
  }

  if (Selector6 >= 0x3c) {
    if (Inst.size() < 4)
      return false;
    uint32_t LongPayload = (FirstTen << 16) | (uint32_t(Inst[2]) << 8) |
                           uint32_t(Inst[3]);
    if (!decodeLongPayload(LongPayload, Inst.drop_front(4), Text))
      return false;
    return true;
  }

  if (!decodeMediumPayload(MediumPayload, Inst.drop_front(3), Text))
    return false;
  return true;
}
