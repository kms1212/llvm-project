//===-- BedrockMCAsmInfo.cpp - Bedrock asm properties ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockMCAsmInfo.h"

#include "BedrockFixupKinds.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

namespace {

const MCAsmInfo::AtSpecifier BedrockAtSpecifiers[] = {
    {Bedrock::fixup_bedrock_abs8, "ABS8"},
    {Bedrock::fixup_bedrock_abs16, "ABS16"},
    {Bedrock::fixup_bedrock_abs32, "ABS32"},
    {Bedrock::fixup_bedrock_abs64, "ABS64"},
    {Bedrock::fixup_bedrock_pcrel16, "PCREL16"},
    {Bedrock::fixup_bedrock_pcrel32, "PCREL32"},
    {Bedrock::fixup_bedrock_pcrel64, "PCREL64"},
    {Bedrock::fixup_bedrock_word_pcrel16, "WORD_PCREL16"},
    {Bedrock::fixup_bedrock_word_pcrel32, "WORD_PCREL32"},
    {Bedrock::fixup_bedrock_imm16, "IMM16"},
    {Bedrock::fixup_bedrock_imm32, "IMM32"},
    {Bedrock::fixup_bedrock_imm64, "IMM64"},
    {Bedrock::fixup_bedrock_disp16, "DISP16"},
    {Bedrock::fixup_bedrock_disp32, "DISP32"},
    {Bedrock::fixup_bedrock_disp64, "DISP64"},
    {Bedrock::fixup_bedrock_section_rel32, "SECTION_REL32"},
    {Bedrock::fixup_bedrock_section_rel64, "SECTION_REL64"},
    {Bedrock::fixup_bedrock_got64, "GOT64"},
    {Bedrock::fixup_bedrock_gotpcrel32, "GOTPCREL32"},
    {Bedrock::fixup_bedrock_gotpcrel64, "GOTPCREL64"},
    {Bedrock::fixup_bedrock_gotoff32, "GOTOFF32"},
    {Bedrock::fixup_bedrock_gotoff64, "GOTOFF64"},
    {Bedrock::fixup_bedrock_got_base_pcrel32, "GOT_BASE_PCREL32"},
    {Bedrock::fixup_bedrock_got_base_pcrel64, "GOT_BASE_PCREL64"},
    {Bedrock::fixup_bedrock_plt32, "PLT32"},
    {Bedrock::fixup_bedrock_plt64, "PLT64"},
    {Bedrock::fixup_bedrock_tls_offset32, "TLS_OFFSET32"},
    {Bedrock::fixup_bedrock_tls_offset64, "TLS_OFFSET64"},
    {Bedrock::fixup_bedrock_tlsdesc_gotpcrel32, "TLSDESC_GOTPCREL32"},
    {Bedrock::fixup_bedrock_tlsdesc_gotpcrel64, "TLSDESC_GOTPCREL64"},
};

} // end anonymous namespace

BedrockMCAsmInfo::BedrockMCAsmInfo(const Triple &TT) {
  CodePointerSize = 8;
  CalleeSaveStackSlotSize = 8;
  MinInstAlignment = 2;
  CommentString = "#";
  HasDotTypeDotSizeDirective = false;
  HasIdentDirective = false;
  PrivateGlobalPrefix = ".L";
  UsesELFSectionDirectiveForBSS = true;
  SupportsDebugInformation = true;

  initializeAtSpecifiers(BedrockAtSpecifiers);
}
