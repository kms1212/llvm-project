//===-- BedrockFixupKinds.h - Bedrock-specific fixups ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_MCTARGETDESC_BEDROCKFIXUPKINDS_H
#define LLVM_LIB_TARGET_BEDROCK_MCTARGETDESC_BEDROCKFIXUPKINDS_H

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCFixup.h"

namespace llvm::Bedrock {

enum Fixups {
  fixup_bedrock_imm32 = FirstTargetFixupKind,
  fixup_bedrock_disp32,
  fixup_bedrock_pcrel16,
  fixup_bedrock_pcrel32,
  fixup_bedrock_brdisp16,
  fixup_bedrock_brdisp32,
  fixup_bedrock_call16,
  fixup_bedrock_call32,
  fixup_bedrock_brdisp16_local,
  fixup_bedrock_call16_local,
  fixup_bedrock_pcrel64,
  fixup_bedrock_gotpcrel32,
  fixup_bedrock_gotpcrel64,
  fixup_bedrock_plt32,
  fixup_bedrock_plt64,
  fixup_bedrock_tls_offset32,
  fixup_bedrock_tls_offset64,
  fixup_bedrock_tlsdesc_gotpcrel32,
  fixup_bedrock_tlsdesc_gotpcrel64,
  fixup_bedrock_tlsdesc_call,
  fixup_bedrock_invalid,
  NumTargetFixupKinds = fixup_bedrock_invalid - FirstTargetFixupKind,
};

enum RelocationType : unsigned {
  R_BEDROCK_NONE = ELF::R_BEDROCK_NONE,
  R_BEDROCK_ABS8 = ELF::R_BEDROCK_ABS8,
  R_BEDROCK_ABS16 = ELF::R_BEDROCK_ABS16,
  R_BEDROCK_ABS32S = ELF::R_BEDROCK_ABS32S,
  R_BEDROCK_ABS64 = ELF::R_BEDROCK_ABS64,
  R_BEDROCK_IMM32S = ELF::R_BEDROCK_IMM32S,
  R_BEDROCK_DISP32S = ELF::R_BEDROCK_DISP32S,
  R_BEDROCK_PCREL16S = ELF::R_BEDROCK_PCREL16S,
  R_BEDROCK_PCREL32S = ELF::R_BEDROCK_PCREL32S,
  R_BEDROCK_BRDISP16S = ELF::R_BEDROCK_BRDISP16S,
  R_BEDROCK_BRDISP32S = ELF::R_BEDROCK_BRDISP32S,
  R_BEDROCK_CALL16S = ELF::R_BEDROCK_CALL16S,
  R_BEDROCK_CALL32S = ELF::R_BEDROCK_CALL32S,
  R_BEDROCK_GOTPCREL32S = ELF::R_BEDROCK_GOTPCREL32S,
  R_BEDROCK_GOTPCREL64 = ELF::R_BEDROCK_GOTPCREL64,
  R_BEDROCK_PLT32S = ELF::R_BEDROCK_PLT32S,
  R_BEDROCK_PLT64 = ELF::R_BEDROCK_PLT64,
  R_BEDROCK_TLS_OFFSET32S = ELF::R_BEDROCK_TLS_OFFSET32S,
  R_BEDROCK_TLS_OFFSET64 = ELF::R_BEDROCK_TLS_OFFSET64,
  R_BEDROCK_TLSDESC_GOTPCREL32S = ELF::R_BEDROCK_TLSDESC_GOTPCREL32S,
  R_BEDROCK_TLSDESC_GOTPCREL64 = ELF::R_BEDROCK_TLSDESC_GOTPCREL64,
  R_BEDROCK_TLSDESC_CALL = ELF::R_BEDROCK_TLSDESC_CALL,
};

} // namespace llvm::Bedrock

#endif
