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
};

} // namespace llvm::Bedrock

#endif
