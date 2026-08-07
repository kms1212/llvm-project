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

static bool needsFieldOffsetAddend(MCFixupKind Kind) {
  switch (Kind) {
  case Bedrock::fixup_bedrock_pcrel16:
  case Bedrock::fixup_bedrock_pcrel32:
  case Bedrock::fixup_bedrock_pcrel64:
  case Bedrock::fixup_bedrock_gotpcrel32:
  case Bedrock::fixup_bedrock_gotpcrel64:
  case Bedrock::fixup_bedrock_tlsdesc_gotpcrel32:
  case Bedrock::fixup_bedrock_tlsdesc_gotpcrel64:
    return true;
  default:
    return false;
  }
}

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
  auto EmitByte = [&](uint8_t Byte) {
    CB.push_back(static_cast<char>(Byte));
  };
  auto GetRegNo = [&](unsigned OpNo) -> uint8_t {
    return Ctx.getRegisterInfo()->getEncodingValue(MI.getOperand(OpNo).getReg());
  };

  switch (MI.getOpcode()) {
  case Bedrock::ILLEGAL:
    EmitByte(0x00);
    return;
  case Bedrock::NOP:
    EmitByte(0x01);
    return;
  case Bedrock::RET:
    EmitByte(0x02);
    return;
  case Bedrock::LRET:
    EmitByte(0x03);
    return;
  case Bedrock::ERET:
    EmitByte(0x04);
    return;
  case Bedrock::SYSCALL:
    EmitByte(0x05);
    return;
  case Bedrock::SYSRET:
    EmitByte(0x06);
    return;
  case Bedrock::BKPT:
    EmitByte(0x07);
    return;
  case Bedrock::WAIT:
    EmitByte(0x08);
    return;
  case Bedrock::YIELD:
    EmitByte(0x09);
    return;
  case Bedrock::RFENCE:
    EmitByte(0x0a);
    return;
  case Bedrock::WFENCE:
    EmitByte(0x0b);
    return;
  case Bedrock::AFENCE:
    EmitByte(0x0c);
    return;
  case Bedrock::ADDQisp:
    if (MI.getOperand(0).getImm() == 8) {
      EmitByte(0x0e);
      return;
    }
    break;
  case Bedrock::SUBQisp:
    if (MI.getOperand(0).getImm() == 8) {
      EmitByte(0x0f);
      return;
    }
    break;
  case Bedrock::PUSHPi:
    EmitByte(0x10 | static_cast<uint8_t>(MI.getOperand(0).getImm()));
    return;
  case Bedrock::POPPi:
    EmitByte(0x18 | static_cast<uint8_t>(MI.getOperand(0).getImm()));
    return;
  case Bedrock::FPUSHPi:
    EmitByte(0x70 | static_cast<uint8_t>(MI.getOperand(0).getImm()));
    return;
  case Bedrock::FPOPPi:
    EmitByte(0x78 | static_cast<uint8_t>(MI.getOperand(0).getImm()));
    return;
  case Bedrock::PUSHr:
    EmitByte(0x20 | GetRegNo(0));
    return;
  case Bedrock::POPr:
    EmitByte(0x30 | GetRegNo(0));
    return;
  case Bedrock::MOVQrs:
    EmitByte(0x40 | GetRegNo(0));
    return;
  case Bedrock::MOVQsr:
    EmitByte(0x50 | GetRegNo(0));
    return;
  case Bedrock::CLRQr:
    EmitByte(0x60 | GetRegNo(0));
    return;
  default:
    break;
  }

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
      const MCExpr *Expr = ExprOp.getExpr();
      if (needsFieldOffsetAddend(Kind))
        Expr = MCBinaryExpr::createAdd(
            Expr, MCConstantExpr::create(OffsetOp.getImm(), Ctx), Ctx);
      bool IsPCRel = Kind == Bedrock::fixup_bedrock_pcrel16 ||
                     Kind == Bedrock::fixup_bedrock_pcrel32 ||
                     Kind == Bedrock::fixup_bedrock_brdisp16 ||
                     Kind == Bedrock::fixup_bedrock_brdisp32 ||
                     Kind == Bedrock::fixup_bedrock_call16 ||
                     Kind == Bedrock::fixup_bedrock_call32 ||
                     Kind == Bedrock::fixup_bedrock_pcrel64 ||
                     Kind == Bedrock::fixup_bedrock_gotpcrel32 ||
                     Kind == Bedrock::fixup_bedrock_gotpcrel64 ||
                     Kind == Bedrock::fixup_bedrock_plt32 ||
                     Kind == Bedrock::fixup_bedrock_tlsdesc_gotpcrel32 ||
                     Kind == Bedrock::fixup_bedrock_tlsdesc_gotpcrel64;
      Fixups.push_back(MCFixup::create(OffsetOp.getImm(), Expr, Kind, IsPCRel));
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
