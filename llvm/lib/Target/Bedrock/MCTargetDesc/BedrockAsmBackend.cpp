//===-- BedrockAsmBackend.cpp - Bedrock Assembler Backend -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockFixupKinds.h"
#include "BedrockMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

extern "C" {
#include "bedrock_asm_disasm.h"
}

using namespace llvm;

static unsigned getFixupSizeInBytes(MCFixupKind Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("invalid Bedrock fixup kind");
  case FK_Data_1:
  case Bedrock::fixup_bedrock_abs8:
    return 1;
  case FK_Data_2:
  case Bedrock::fixup_bedrock_abs16:
  case Bedrock::fixup_bedrock_pcrel16:
  case Bedrock::fixup_bedrock_word_pcrel16:
  case Bedrock::fixup_bedrock_imm16:
  case Bedrock::fixup_bedrock_disp16:
    return 2;
  case FK_Data_4:
  case Bedrock::fixup_bedrock_abs32:
  case Bedrock::fixup_bedrock_pcrel32:
  case Bedrock::fixup_bedrock_word_pcrel32:
  case Bedrock::fixup_bedrock_imm32:
  case Bedrock::fixup_bedrock_disp32:
    return 4;
  case FK_Data_8:
  case Bedrock::fixup_bedrock_abs64:
  case Bedrock::fixup_bedrock_pcrel64:
  case Bedrock::fixup_bedrock_imm64:
  case Bedrock::fixup_bedrock_disp64:
  case Bedrock::fixup_bedrock_section_rel64:
  case Bedrock::fixup_bedrock_got64:
  case Bedrock::fixup_bedrock_gotpcrel64:
  case Bedrock::fixup_bedrock_gotoff64:
  case Bedrock::fixup_bedrock_got_base_pcrel64:
  case Bedrock::fixup_bedrock_plt64:
  case Bedrock::fixup_bedrock_tls_offset64:
  case Bedrock::fixup_bedrock_tlsdesc_gotpcrel64:
    return 8;
  case Bedrock::fixup_bedrock_section_rel32:
  case Bedrock::fixup_bedrock_gotpcrel32:
  case Bedrock::fixup_bedrock_gotoff32:
  case Bedrock::fixup_bedrock_got_base_pcrel32:
  case Bedrock::fixup_bedrock_plt32:
  case Bedrock::fixup_bedrock_tls_offset32:
  case Bedrock::fixup_bedrock_tlsdesc_gotpcrel32:
    return 4;
  case Bedrock::fixup_bedrock_call_target:
  case Bedrock::fixup_bedrock_jmp_target:
  case Bedrock::fixup_bedrock_long_control_target:
  case Bedrock::fixup_bedrock_tlsdesc_call:
    return 0;
  }
}

static uint64_t adjustFixupValue(const MCFixup &Fixup, uint64_t Value,
                                 MCContext &Ctx) {
  switch (Fixup.getKind()) {
  default:
    return Value;
  case Bedrock::fixup_bedrock_word_pcrel16:
  case Bedrock::fixup_bedrock_word_pcrel32: {
    int64_t SignedValue = static_cast<int64_t>(Value);
    if ((SignedValue & 1) != 0)
      Ctx.reportError(Fixup.getLoc(), "misaligned Bedrock word-relative fixup");
    int64_t WordValue = SignedValue / 2;
    if (Fixup.getKind() == Bedrock::fixup_bedrock_word_pcrel16 &&
        !isInt<16>(WordValue))
      Ctx.reportError(Fixup.getLoc(),
                      "out of range Bedrock word-relative fixup");
    if (Fixup.getKind() == Bedrock::fixup_bedrock_word_pcrel32 &&
        !isInt<32>(WordValue))
      Ctx.reportError(Fixup.getLoc(),
                      "out of range Bedrock word-relative fixup");
    return static_cast<uint64_t>(WordValue);
  }
  }
}

static void setDeclaredWords(uint16_t &Word0, unsigned WordCount) {
  Word0 = (Word0 & ~uint16_t(BEDROCK_WORD0_LENGTH_MASK)) |
          uint16_t((WordCount - 1) << 12);
}

namespace {

class BedrockAsmBackend : public MCAsmBackend {
public:
  BedrockAsmBackend() : MCAsmBackend(llvm::endianness::little) {}

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createBedrockELFObjectWriter(/*OSABI*/ 0);
  }

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override;

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;
};

} // end anonymous namespace

MCFixupKindInfo BedrockAsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  static const MCFixupKindInfo Infos[Bedrock::NumTargetFixupKinds] = {
      {"fixup_bedrock_abs8", 0, 8, 0},
      {"fixup_bedrock_abs16", 0, 16, 0},
      {"fixup_bedrock_abs32", 0, 32, 0},
      {"fixup_bedrock_abs64", 0, 64, 0},
      {"fixup_bedrock_pcrel16", 0, 16, 0},
      {"fixup_bedrock_pcrel32", 0, 32, 0},
      {"fixup_bedrock_pcrel64", 0, 64, 0},
      {"fixup_bedrock_word_pcrel16", 0, 16, 0},
      {"fixup_bedrock_word_pcrel32", 0, 32, 0},
      {"fixup_bedrock_imm16", 0, 16, 0},
      {"fixup_bedrock_imm32", 0, 32, 0},
      {"fixup_bedrock_imm64", 0, 64, 0},
      {"fixup_bedrock_disp16", 0, 16, 0},
      {"fixup_bedrock_disp32", 0, 32, 0},
      {"fixup_bedrock_disp64", 0, 64, 0},
      {"fixup_bedrock_section_rel32", 0, 32, 0},
      {"fixup_bedrock_section_rel64", 0, 64, 0},
      {"fixup_bedrock_call_target", 0, 0, 0},
      {"fixup_bedrock_jmp_target", 0, 0, 0},
      {"fixup_bedrock_long_control_target", 0, 0, 0},
      {"fixup_bedrock_got64", 0, 64, 0},
      {"fixup_bedrock_gotpcrel32", 0, 32, 0},
      {"fixup_bedrock_gotpcrel64", 0, 64, 0},
      {"fixup_bedrock_gotoff32", 0, 32, 0},
      {"fixup_bedrock_gotoff64", 0, 64, 0},
      {"fixup_bedrock_got_base_pcrel32", 0, 32, 0},
      {"fixup_bedrock_got_base_pcrel64", 0, 64, 0},
      {"fixup_bedrock_plt32", 0, 32, 0},
      {"fixup_bedrock_plt64", 0, 64, 0},
      {"fixup_bedrock_tls_offset32", 0, 32, 0},
      {"fixup_bedrock_tls_offset64", 0, 64, 0},
      {"fixup_bedrock_tlsdesc_gotpcrel32", 0, 32, 0},
      {"fixup_bedrock_tlsdesc_gotpcrel64", 0, 64, 0},
      {"fixup_bedrock_tlsdesc_call", 0, 0, 0},
  };

  if (Kind < FirstTargetFixupKind)
    return MCAsmBackend::getFixupKindInfo(Kind);

  assert(unsigned(Kind - FirstTargetFixupKind) < Bedrock::NumTargetFixupKinds &&
         "invalid Bedrock fixup kind");
  return Infos[Kind - FirstTargetFixupKind];
}

void BedrockAsmBackend::applyFixup(const MCFragment &F, const MCFixup &Fixup,
                                   const MCValue &Target, uint8_t *Data,
                                   uint64_t Value, bool IsResolved) {
  maybeAddReloc(F, Fixup, Target, Value, IsResolved);
  if (!Value)
    return;

  MCFixupKind Kind = Fixup.getKind();
  Value = adjustFixupValue(Fixup, Value, getContext());
  unsigned NumBytes = getFixupSizeInBytes(Kind);
  for (unsigned I = 0; I != NumBytes; ++I)
    Data[I] |= uint8_t((Value >> (I * 8)) & 0xff);
}

bool BedrockAsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                     const MCSubtargetInfo *STI) const {
  (void)STI;
  const bedrock_form_desc *Nop = bedrock_find_form_by_id("NOP");
  if (Nop == nullptr)
    return false;

  uint16_t Words[BEDROCK_MAX_INSTRUCTION_WORDS] = {};
  size_t WrittenWords = 0;
  if (bedrock_encode_form_words(Nop, nullptr, 0, Words,
                                BEDROCK_MAX_INSTRUCTION_WORDS,
                                &WrittenWords) != BEDROCK_OK)
    return false;

  const uint64_t NopBytes = WrittenWords * sizeof(uint16_t);
  if (NopBytes != sizeof(uint16_t) || (Count % sizeof(uint16_t)) != 0)
    return false;

  uint64_t CountWords = Count / sizeof(uint16_t);
  for (uint64_t I = 0; I != CountWords; ++I) {
    uint64_t RemainingWords = CountWords - I;
    unsigned DeclaredWords =
        RemainingWords > BEDROCK_MAX_INSTRUCTION_WORDS
            ? BEDROCK_MAX_INSTRUCTION_WORDS
            : unsigned(RemainingWords);
    uint16_t Word = Words[0];
    setDeclaredWords(Word, DeclaredWords);
    support::endian::write<uint16_t>(OS, Word, Endian);
  }
  return true;
}

MCAsmBackend *llvm::createBedrockAsmBackend(const Target &T,
                                            const MCSubtargetInfo &STI,
                                            const MCRegisterInfo &MRI,
                                            const MCTargetOptions &Options) {
  (void)T;
  (void)MRI;
  (void)Options;
  if (!STI.getTargetTriple().isOSBinFormatELF())
    llvm_unreachable("Bedrock only supports ELF object output");
  return new BedrockAsmBackend();
}
