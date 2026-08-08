//===-- BedrockAsmBackend.cpp - Bedrock assembler backend -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockFixupKinds.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Endian.h"

using namespace llvm;

namespace {

bool isNextIPRelativeFixup(MCFixupKind Kind) {
  return Kind == Bedrock::fixup_bedrock_brdisp16 ||
         Kind == Bedrock::fixup_bedrock_brdisp32 ||
         Kind == Bedrock::fixup_bedrock_call16 ||
         Kind == Bedrock::fixup_bedrock_call32;
}

class BedrockAsmBackend : public MCAsmBackend {
public:
  BedrockAsmBackend() : MCAsmBackend(llvm::endianness::little) {}
  ~BedrockAsmBackend() override = default;

  std::optional<MCFixupKind> getFixupKind(StringRef Name) const override {
    unsigned Type = StringSwitch<unsigned>(Name)
#define ELF_RELOC(X, Y) .Case(#X, Y)
#include "llvm/BinaryFormat/ELFRelocs/Bedrock.def"
#undef ELF_RELOC
                        .Default(-1u);
    if (Type != -1u)
      return static_cast<MCFixupKind>(FirstLiteralRelocationKind + Type);
    return MCAsmBackend::getFixupKind(Name);
  }

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    static const MCFixupKindInfo Infos[Bedrock::NumTargetFixupKinds] = {
        {"fixup_bedrock_imm32", 0, 32, 0},
        {"fixup_bedrock_disp32", 0, 32, 0},
        {"fixup_bedrock_pcrel16", 0, 16, 0},
        {"fixup_bedrock_pcrel32", 0, 32, 0},
        {"fixup_bedrock_brdisp16", 0, 16, 0},
        {"fixup_bedrock_brdisp32", 0, 32, 0},
        {"fixup_bedrock_call16", 0, 16, 0},
        {"fixup_bedrock_call32", 0, 32, 0},
        {"fixup_bedrock_pcrel64", 0, 64, 0},
        {"fixup_bedrock_gotpcrel32", 0, 32, 0},
        {"fixup_bedrock_gotpcrel64", 0, 64, 0},
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

    assert(unsigned(Kind - FirstTargetFixupKind) <
               Bedrock::NumTargetFixupKinds &&
           "invalid Bedrock fixup kind");
    return Infos[Kind - FirstTargetFixupKind];
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override {
    // Preserve symbolic long calls so the linker can select the short form
    // after final layout.
    if (IsResolved && Fixup.getKind() == Bedrock::fixup_bedrock_call32 &&
        Target.getAddSym())
      IsResolved = false;
    maybeAddReloc(F, Fixup, Target, Value, IsResolved);
    if (IsResolved && isNextIPRelativeFixup(Fixup.getKind())) {
      MCFixupKindInfo Info = getFixupKindInfo(Fixup.getKind());
      Value -= Info.TargetSize / 8;
    }

    if (!Value)
      return;

    MCFixupKindInfo Info = getFixupKindInfo(Fixup.getKind());
    Value <<= Info.TargetOffset;

    unsigned NumBytes = alignTo(Info.TargetSize + Info.TargetOffset, 8) / 8;
    for (unsigned I = 0; I != NumBytes; ++I)
      Data[I] |= uint8_t((Value >> (I * 8)) & 0xff);
  }

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createBedrockELFObjectWriter(ELF::ELFOSABI_NONE);
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override {
    while (Count--)
      OS.write("\x01", 1);

    return true;
  }
};

} // end anonymous namespace

MCAsmBackend *llvm::createBedrockAsmBackend(const Target &T,
                                            const MCSubtargetInfo &STI,
                                            const MCRegisterInfo &MRI,
                                            const MCTargetOptions &Options) {
  return new BedrockAsmBackend();
}
