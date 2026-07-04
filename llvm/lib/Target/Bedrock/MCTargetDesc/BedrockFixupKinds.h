//===-- BedrockFixupKinds.h - Bedrock Specific Fixup Entries ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_MCTARGETDESC_BEDROCKFIXUPKINDS_H
#define LLVM_LIB_TARGET_BEDROCK_MCTARGETDESC_BEDROCKFIXUPKINDS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCFixup.h"
#include <cstdint>
#include <string>

namespace llvm {
namespace Bedrock {

enum Fixups {
  fixup_bedrock_abs8 = FirstTargetFixupKind,
  fixup_bedrock_abs16,
  fixup_bedrock_abs32,
  fixup_bedrock_abs64,
  fixup_bedrock_pcrel16,
  fixup_bedrock_pcrel32,
  fixup_bedrock_pcrel64,
  fixup_bedrock_word_pcrel16,
  fixup_bedrock_word_pcrel32,
  fixup_bedrock_imm16,
  fixup_bedrock_imm32,
  fixup_bedrock_imm64,
  fixup_bedrock_disp16,
  fixup_bedrock_disp32,
  fixup_bedrock_disp64,
  fixup_bedrock_section_rel32,
  fixup_bedrock_section_rel64,
  fixup_bedrock_call_target,
  fixup_bedrock_jmp_target,
  fixup_bedrock_long_control_target,
  fixup_bedrock_got64,
  fixup_bedrock_gotpcrel32,
  fixup_bedrock_gotpcrel64,
  fixup_bedrock_gotoff32,
  fixup_bedrock_gotoff64,
  fixup_bedrock_got_base_pcrel32,
  fixup_bedrock_got_base_pcrel64,
  fixup_bedrock_plt32,
  fixup_bedrock_plt64,
  fixup_bedrock_tls_offset32,
  fixup_bedrock_tls_offset64,
  fixup_bedrock_tlsdesc_gotpcrel32,
  fixup_bedrock_tlsdesc_gotpcrel64,
  fixup_bedrock_tlsdesc_call,

  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};

inline MCFixupKind getFixupKindForRelocName(StringRef Name) {
  std::string UpperStorage = Name.upper();
  StringRef Upper(UpperStorage);
  Upper.consume_front("R_BEDROCK_");
  return StringSwitch<MCFixupKind>(Upper)
      .Case("ABS8", fixup_bedrock_abs8)
      .Case("ABS16", fixup_bedrock_abs16)
      .Case("ABS32", fixup_bedrock_abs32)
      .Case("ABS64", fixup_bedrock_abs64)
      .Case("PCREL16", fixup_bedrock_pcrel16)
      .Case("PCREL32", fixup_bedrock_pcrel32)
      .Case("PCREL64", fixup_bedrock_pcrel64)
      .Case("WORD_PCREL16", fixup_bedrock_word_pcrel16)
      .Case("WORD_PCREL32", fixup_bedrock_word_pcrel32)
      .Case("IMM16", fixup_bedrock_imm16)
      .Case("IMM32", fixup_bedrock_imm32)
      .Case("IMM64", fixup_bedrock_imm64)
      .Case("DISP16", fixup_bedrock_disp16)
      .Case("DISP32", fixup_bedrock_disp32)
      .Case("DISP64", fixup_bedrock_disp64)
      .Case("SECTION_REL32", fixup_bedrock_section_rel32)
      .Case("SECTION_REL64", fixup_bedrock_section_rel64)
      .Case("CALL_TARGET", fixup_bedrock_call_target)
      .Case("JMP_TARGET", fixup_bedrock_jmp_target)
      .Case("LONG_CONTROL_TARGET", fixup_bedrock_long_control_target)
      .Case("GOT64", fixup_bedrock_got64)
      .Case("GOT", fixup_bedrock_got64)
      .Case("GOTPCREL32", fixup_bedrock_gotpcrel32)
      .Case("GOTPCREL64", fixup_bedrock_gotpcrel64)
      .Case("GOTOFF32", fixup_bedrock_gotoff32)
      .Case("GOTOFF64", fixup_bedrock_gotoff64)
      .Case("GOT_BASE_PCREL32", fixup_bedrock_got_base_pcrel32)
      .Case("GOT_BASE_PCREL64", fixup_bedrock_got_base_pcrel64)
      .Case("PLT32", fixup_bedrock_plt32)
      .Case("PLT", fixup_bedrock_plt32)
      .Case("PLT64", fixup_bedrock_plt64)
      .Case("TLS_OFFSET32", fixup_bedrock_tls_offset32)
      .Case("TLS_OFFSET64", fixup_bedrock_tls_offset64)
      .Case("TLSDESC_GOTPCREL32", fixup_bedrock_tlsdesc_gotpcrel32)
      .Case("TLSDESC_GOTPCREL64", fixup_bedrock_tlsdesc_gotpcrel64)
      .Case("TLSDESC_CALL", fixup_bedrock_tlsdesc_call)
      .Default(FK_NONE);
}

inline unsigned getFixupWidthBits(MCFixupKind Kind) {
  switch (Kind) {
  default:
    return 0;
  case fixup_bedrock_abs8:
    return 8;
  case fixup_bedrock_abs16:
  case fixup_bedrock_pcrel16:
  case fixup_bedrock_word_pcrel16:
  case fixup_bedrock_imm16:
  case fixup_bedrock_disp16:
    return 16;
  case fixup_bedrock_abs32:
  case fixup_bedrock_pcrel32:
  case fixup_bedrock_word_pcrel32:
  case fixup_bedrock_imm32:
  case fixup_bedrock_disp32:
  case fixup_bedrock_section_rel32:
  case fixup_bedrock_gotpcrel32:
  case fixup_bedrock_gotoff32:
  case fixup_bedrock_got_base_pcrel32:
  case fixup_bedrock_plt32:
  case fixup_bedrock_tls_offset32:
  case fixup_bedrock_tlsdesc_gotpcrel32:
    return 32;
  case fixup_bedrock_abs64:
  case fixup_bedrock_pcrel64:
  case fixup_bedrock_imm64:
  case fixup_bedrock_disp64:
  case fixup_bedrock_section_rel64:
  case fixup_bedrock_got64:
  case fixup_bedrock_gotpcrel64:
  case fixup_bedrock_gotoff64:
  case fixup_bedrock_got_base_pcrel64:
  case fixup_bedrock_plt64:
  case fixup_bedrock_tls_offset64:
  case fixup_bedrock_tlsdesc_gotpcrel64:
    return 64;
  case fixup_bedrock_call_target:
  case fixup_bedrock_jmp_target:
  case fixup_bedrock_long_control_target:
  case fixup_bedrock_tlsdesc_call:
    return 0;
  }
}

inline uint64_t getRelocationPlaceholder(MCFixupKind Kind, unsigned Index = 0) {
  switch (getFixupWidthBits(Kind)) {
  default:
    return 0;
  case 8:
    return 0x40ull + Index;
  case 16:
    return 0x5200ull + Index;
  case 32:
    return 0x01020300ull + Index;
  case 64:
    return 0x0000000102030400ull + Index;
  }
}

inline bool isPCRelFixup(MCFixupKind Kind) {
  switch (Kind) {
  default:
    return false;
  case fixup_bedrock_pcrel16:
  case fixup_bedrock_pcrel32:
  case fixup_bedrock_pcrel64:
  case fixup_bedrock_word_pcrel16:
  case fixup_bedrock_word_pcrel32:
  case fixup_bedrock_gotpcrel32:
  case fixup_bedrock_gotpcrel64:
  case fixup_bedrock_got_base_pcrel32:
  case fixup_bedrock_got_base_pcrel64:
  case fixup_bedrock_plt32:
  case fixup_bedrock_tlsdesc_gotpcrel32:
  case fixup_bedrock_tlsdesc_gotpcrel64:
    return true;
  }
}

} // end namespace Bedrock
} // end namespace llvm

#endif // LLVM_LIB_TARGET_BEDROCK_MCTARGETDESC_BEDROCKFIXUPKINDS_H
