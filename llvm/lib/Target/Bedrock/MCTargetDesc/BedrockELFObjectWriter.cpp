//===-- BedrockELFObjectWriter.cpp - Bedrock ELF Writer -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockFixupKinds.h"
#include "BedrockMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {

class BedrockELFObjectWriter : public MCELFObjectTargetWriter {
public:
  explicit BedrockELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(/*Is64Bit*/ true, OSABI, ELF::EM_BEDROCK,
                                /*HasRelocationAddend*/ true) {}

protected:
  unsigned getRelocType(const MCFixup &Fixup, const MCValue &Target,
                        bool IsPCRel) const override;
};

static unsigned getRelocTypeForTargetFixup(MCFixupKind Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("invalid Bedrock target fixup kind");
  case Bedrock::fixup_bedrock_abs8:
    return ELF::R_BEDROCK_ABS8;
  case Bedrock::fixup_bedrock_abs16:
    return ELF::R_BEDROCK_ABS16;
  case Bedrock::fixup_bedrock_abs32:
    return ELF::R_BEDROCK_ABS32;
  case Bedrock::fixup_bedrock_abs64:
    return ELF::R_BEDROCK_ABS64;
  case Bedrock::fixup_bedrock_pcrel16:
    return ELF::R_BEDROCK_PCREL16;
  case Bedrock::fixup_bedrock_pcrel32:
    return ELF::R_BEDROCK_PCREL32;
  case Bedrock::fixup_bedrock_pcrel64:
    return ELF::R_BEDROCK_PCREL64;
  case Bedrock::fixup_bedrock_word_pcrel16:
    return ELF::R_BEDROCK_WORD_PCREL16;
  case Bedrock::fixup_bedrock_word_pcrel32:
    return ELF::R_BEDROCK_WORD_PCREL32;
  case Bedrock::fixup_bedrock_imm16:
    return ELF::R_BEDROCK_IMM16;
  case Bedrock::fixup_bedrock_imm32:
    return ELF::R_BEDROCK_IMM32;
  case Bedrock::fixup_bedrock_imm64:
    return ELF::R_BEDROCK_IMM64;
  case Bedrock::fixup_bedrock_disp16:
    return ELF::R_BEDROCK_DISP16;
  case Bedrock::fixup_bedrock_disp32:
    return ELF::R_BEDROCK_DISP32;
  case Bedrock::fixup_bedrock_disp64:
    return ELF::R_BEDROCK_DISP64;
  case Bedrock::fixup_bedrock_section_rel32:
    return ELF::R_BEDROCK_SECTION_REL32;
  case Bedrock::fixup_bedrock_section_rel64:
    return ELF::R_BEDROCK_SECTION_REL64;
  case Bedrock::fixup_bedrock_call_target:
    return ELF::R_BEDROCK_CALL_TARGET;
  case Bedrock::fixup_bedrock_jmp_target:
    return ELF::R_BEDROCK_JMP_TARGET;
  case Bedrock::fixup_bedrock_long_control_target:
    return ELF::R_BEDROCK_LONG_CONTROL_TARGET;
  case Bedrock::fixup_bedrock_got64:
    return ELF::R_BEDROCK_GOT64;
  case Bedrock::fixup_bedrock_gotpcrel32:
    return ELF::R_BEDROCK_GOTPCREL32;
  case Bedrock::fixup_bedrock_gotpcrel64:
    return ELF::R_BEDROCK_GOTPCREL64;
  case Bedrock::fixup_bedrock_gotoff32:
    return ELF::R_BEDROCK_GOTOFF32;
  case Bedrock::fixup_bedrock_gotoff64:
    return ELF::R_BEDROCK_GOTOFF64;
  case Bedrock::fixup_bedrock_got_base_pcrel32:
    return ELF::R_BEDROCK_GOT_BASE_PCREL32;
  case Bedrock::fixup_bedrock_got_base_pcrel64:
    return ELF::R_BEDROCK_GOT_BASE_PCREL64;
  case Bedrock::fixup_bedrock_plt32:
    return ELF::R_BEDROCK_PLT32;
  case Bedrock::fixup_bedrock_plt64:
    return ELF::R_BEDROCK_PLT64;
  case Bedrock::fixup_bedrock_tls_offset32:
    return ELF::R_BEDROCK_TLS_OFFSET32;
  case Bedrock::fixup_bedrock_tls_offset64:
    return ELF::R_BEDROCK_TLS_OFFSET64;
  case Bedrock::fixup_bedrock_tlsdesc_gotpcrel32:
    return ELF::R_BEDROCK_TLSDESC_GOTPCREL32;
  case Bedrock::fixup_bedrock_tlsdesc_gotpcrel64:
    return ELF::R_BEDROCK_TLSDESC_GOTPCREL64;
  case Bedrock::fixup_bedrock_tlsdesc_call:
    return ELF::R_BEDROCK_TLSDESC_CALL;
  }
}

} // end anonymous namespace

unsigned BedrockELFObjectWriter::getRelocType(const MCFixup &Fixup,
                                              const MCValue &Target,
                                              bool IsPCRel) const {
  if (Target.getSpecifier())
    return getRelocTypeForTargetFixup(
        static_cast<MCFixupKind>(Target.getSpecifier()));

  switch (Fixup.getKind()) {
  default:
    return getRelocTypeForTargetFixup(Fixup.getKind());
  case FK_Data_1:
    return ELF::R_BEDROCK_ABS8;
  case FK_Data_2:
    return IsPCRel ? ELF::R_BEDROCK_PCREL16 : ELF::R_BEDROCK_ABS16;
  case FK_Data_4:
    return IsPCRel ? ELF::R_BEDROCK_PCREL32 : ELF::R_BEDROCK_ABS32;
  case FK_Data_8:
    return IsPCRel ? ELF::R_BEDROCK_PCREL64 : ELF::R_BEDROCK_ABS64;
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createBedrockELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<BedrockELFObjectWriter>(OSABI);
}
