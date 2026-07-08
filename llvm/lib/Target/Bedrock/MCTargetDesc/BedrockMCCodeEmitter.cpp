//===-- BedrockMCCodeEmitter.cpp - Bedrock machine code emitter -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockFixupKinds.h"
#include "BedrockMCEncoding.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/EndianStream.h"

using namespace llvm;

namespace llvm {

class BedrockMCCodeEmitter : public MCCodeEmitter {
  MCContext &Ctx;

  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  unsigned getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

public:
  BedrockMCCodeEmitter(MCContext &Ctx, const MCInstrInfo &MCII) : Ctx(Ctx) {}

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;
};

void BedrockMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                             SmallVectorImpl<char> &CB,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  if (MI.getOpcode() == Bedrock::RAW_EXPR) {
    unsigned NumFixups = MI.getOperand(0).getImm();
    unsigned ByteOp = 1 + NumFixups * 3;

    for (unsigned I = ByteOp; I != MI.getNumOperands(); ++I)
      CB.push_back(static_cast<char>(MI.getOperand(I).getImm()));

    for (unsigned I = 0; I != NumFixups; ++I) {
      const MCOperand &OffsetOp = MI.getOperand(1 + I * 3);
      const MCOperand &KindOp = MI.getOperand(2 + I * 3);
      const MCOperand &ExprOp = MI.getOperand(3 + I * 3);
      assert(ExprOp.isExpr() && "expected Bedrock RAW_EXPR fixup expression");
      auto Kind = static_cast<MCFixupKind>(KindOp.getImm());
      bool IsPCRel = Kind == Bedrock::fixup_bedrock_pcrel16 ||
                     Kind == Bedrock::fixup_bedrock_pcrel32 ||
                     Kind == Bedrock::fixup_bedrock_brdisp16 ||
                     Kind == Bedrock::fixup_bedrock_brdisp32 ||
                     Kind == Bedrock::fixup_bedrock_call16 ||
                     Kind == Bedrock::fixup_bedrock_call32;
      Fixups.push_back(
          MCFixup::create(OffsetOp.getImm(), ExprOp.getExpr(), Kind, IsPCRel));
    }
    return;
  }

  SmallVector<uint8_t, 16> RawBytes;
  if (BedrockMC::getRawInstBytes(MI, RawBytes)) {
    for (uint8_t Byte : RawBytes)
      CB.push_back(static_cast<char>(Byte));
    return;
  }

  uint64_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);
  support::endian::write(CB, static_cast<uint16_t>(Bits),
                         llvm::endianness::big);
}

unsigned
BedrockMCCodeEmitter::getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        const MCSubtargetInfo &STI) const {
  if (MO.isReg())
    return Ctx.getRegisterInfo()->getEncodingValue(MO.getReg());
  if (MO.isImm())
    return MO.getImm();

  assert(MO.isExpr() && "expected expression operand");
  Fixups.push_back(MCFixup::create(0, MO.getExpr(), FK_Data_2));
  return 0;
}

MCCodeEmitter *createBedrockMCCodeEmitter(const MCInstrInfo &MCII,
                                          MCContext &Ctx) {
  return new BedrockMCCodeEmitter(Ctx, MCII);
}

#include "BedrockGenMCCodeEmitter.inc"

} // end namespace llvm
