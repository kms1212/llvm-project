//===-- BedrockELFObjectWriter.cpp - Bedrock ELF writer -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockFixupKinds.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"

using namespace llvm;

namespace {

class BedrockELFObjectWriter : public MCELFObjectTargetWriter {
public:
  BedrockELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(/*Is64Bit=*/true, OSABI, ELF::EM_BEDROCK,
                                /*HasRelocationAddend=*/true) {}

protected:
  unsigned getRelocType(const MCFixup &Fixup, const MCValue &Target,
                        bool IsPCRel) const override {
    switch (Fixup.getKind()) {
    default:
      llvm_unreachable("invalid Bedrock fixup kind");
    case FK_Data_1:
      return ELF::R_BEDROCK_ABS8;
    case FK_Data_2:
      return ELF::R_BEDROCK_ABS16;
    case FK_Data_4:
      return ELF::R_BEDROCK_ABS32S;
    case FK_Data_8:
      return ELF::R_BEDROCK_ABS64;
    case Bedrock::fixup_bedrock_imm32:
      return ELF::R_BEDROCK_IMM32S;
    case Bedrock::fixup_bedrock_disp32:
      return ELF::R_BEDROCK_DISP32S;
    case Bedrock::fixup_bedrock_pcrel16:
      return ELF::R_BEDROCK_PCREL16S;
    case Bedrock::fixup_bedrock_pcrel32:
      return ELF::R_BEDROCK_PCREL32S;
    case Bedrock::fixup_bedrock_brdisp16:
      return ELF::R_BEDROCK_BRDISP16S;
    case Bedrock::fixup_bedrock_brdisp32:
      return ELF::R_BEDROCK_BRDISP32S;
    case Bedrock::fixup_bedrock_call16:
      return ELF::R_BEDROCK_CALL16S;
    case Bedrock::fixup_bedrock_call32:
      return ELF::R_BEDROCK_CALL32S;
    }
  }
};

} // end anonymous namespace

std::unique_ptr<MCObjectTargetWriter>
llvm::createBedrockELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<BedrockELFObjectWriter>(OSABI);
}
