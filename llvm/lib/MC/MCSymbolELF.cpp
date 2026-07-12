//===- lib/MC/MCSymbolELF.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCSymbolELF.h"
#include "llvm/BinaryFormat/ELF.h"

namespace llvm {

namespace {
enum {
  // Shift value for STT_* flags. ELF symbol types occupy 4 bits, including
  // processor-specific values in the STT_LOPROC..STT_HIPROC range.
  ELF_STT_Shift = 0,

  // Shift value for STB_* flags. 4 possible values, 2 bits.
  ELF_STB_Shift = 4,

  // Shift value for STV_* flags. 4 possible values, 2 bits.
  ELF_STV_Shift = 6,

  // Shift value for STO_* flags. 3 bits. All the values are between 0x20 and
  // 0xe0, so we shift right by 5 before storing.
  ELF_STO_Shift = 8,

  // One bit.
  ELF_IsSignature_Shift = 11,

  // One bit.
  ELF_Weakref_Shift = 12,

  // One bit.
  ELF_BindingSet_Shift = 13,

  // One bit.
  ELF_IsMemoryTagged_Shift = 14,
};
}

void MCSymbolELF::setBinding(unsigned Binding) const {
  setIsBindingSet();
  unsigned Val;
  switch (Binding) {
  default:
    llvm_unreachable("Unsupported Binding");
  case ELF::STB_LOCAL:
    Val = 0;
    break;
  case ELF::STB_GLOBAL:
    Val = 1;
    break;
  case ELF::STB_WEAK:
    Val = 2;
    break;
  case ELF::STB_GNU_UNIQUE:
    Val = 3;
    break;
  }
  uint32_t OtherFlags = getFlags() & ~(0x3 << ELF_STB_Shift);
  setFlags(OtherFlags | (Val << ELF_STB_Shift));
}

unsigned MCSymbolELF::getBinding() const {
  if (isBindingSet()) {
    uint32_t Val = (Flags >> ELF_STB_Shift) & 3;
    switch (Val) {
    default:
      llvm_unreachable("Invalid value");
    case 0:
      return ELF::STB_LOCAL;
    case 1:
      return ELF::STB_GLOBAL;
    case 2:
      return ELF::STB_WEAK;
    case 3:
      return ELF::STB_GNU_UNIQUE;
    }
  }

  if (isDefined())
    return ELF::STB_LOCAL;
  if (isUsedInReloc())
    return ELF::STB_GLOBAL;
  if (isSignature())
    return ELF::STB_LOCAL;
  return ELF::STB_GLOBAL;
}

void MCSymbolELF::setType(unsigned Type) const {
  assert(Type <= ELF::STT_HIPROC && "unsupported ELF symbol type");
  uint32_t OtherFlags = getFlags() & ~(0xf << ELF_STT_Shift);
  setFlags(OtherFlags | (Type << ELF_STT_Shift));
}

unsigned MCSymbolELF::getType() const { return (Flags >> ELF_STT_Shift) & 0xf; }

void MCSymbolELF::setVisibility(unsigned Visibility) {
  assert(Visibility == ELF::STV_DEFAULT || Visibility == ELF::STV_INTERNAL ||
         Visibility == ELF::STV_HIDDEN || Visibility == ELF::STV_PROTECTED);

  uint32_t OtherFlags = getFlags() & ~(0x3 << ELF_STV_Shift);
  setFlags(OtherFlags | (Visibility << ELF_STV_Shift));
}

unsigned MCSymbolELF::getVisibility() const {
  unsigned Visibility = (Flags >> ELF_STV_Shift) & 3;
  return Visibility;
}

void MCSymbolELF::setOther(unsigned Other) {
  assert((Other & 0x1f) == 0);
  Other >>= 5;
  assert(Other <= 0x7);
  uint32_t OtherFlags = getFlags() & ~(0x7 << ELF_STO_Shift);
  setFlags(OtherFlags | (Other << ELF_STO_Shift));
}

unsigned MCSymbolELF::getOther() const {
  unsigned Other = (Flags >> ELF_STO_Shift) & 7;
  return Other << 5;
}

void MCSymbolELF::setIsWeakref() const {
  uint32_t OtherFlags = getFlags() & ~(0x1 << ELF_Weakref_Shift);
  setFlags(OtherFlags | (1 << ELF_Weakref_Shift));
}

bool MCSymbolELF::isWeakref() const {
  return getFlags() & (0x1 << ELF_Weakref_Shift);
}

void MCSymbolELF::setIsSignature() const {
  uint32_t OtherFlags = getFlags() & ~(0x1 << ELF_IsSignature_Shift);
  setFlags(OtherFlags | (1 << ELF_IsSignature_Shift));
}

bool MCSymbolELF::isSignature() const {
  return getFlags() & (0x1 << ELF_IsSignature_Shift);
}

void MCSymbolELF::setIsBindingSet() const {
  uint32_t OtherFlags = getFlags() & ~(0x1 << ELF_BindingSet_Shift);
  setFlags(OtherFlags | (1 << ELF_BindingSet_Shift));
}

bool MCSymbolELF::isBindingSet() const {
  return getFlags() & (0x1 << ELF_BindingSet_Shift);
}

bool MCSymbolELF::isMemtag() const {
  return getFlags() & (0x1 << ELF_IsMemoryTagged_Shift);
}

void MCSymbolELF::setMemtag(bool Tagged) {
  uint32_t OtherFlags = getFlags() & ~(1 << ELF_IsMemoryTagged_Shift);
  if (Tagged)
    setFlags(OtherFlags | (1 << ELF_IsMemoryTagged_Shift));
  else
    setFlags(OtherFlags);
}
}
