//===-- BedrockDisassembler.cpp - Bedrock disassembler --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/BedrockMCEncoding.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "TargetInfo/BedrockTargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoder.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::MCD;

#define DEBUG_TYPE "bedrock-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {

class BedrockDisassembler : public MCDisassembler {
public:
  BedrockDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &MI, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};

} // end anonymous namespace

static DecodeStatus DecodeGPR64RegisterClass(MCInst &MI, uint64_t RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  static const MCRegister Regs[] = {
      Bedrock::R0,  Bedrock::R1,  Bedrock::R2,  Bedrock::R3,
      Bedrock::R4,  Bedrock::R5,  Bedrock::R6,  Bedrock::R7,
      Bedrock::R8,  Bedrock::R9,  Bedrock::R10, Bedrock::R11,
      Bedrock::R12, Bedrock::R13, Bedrock::R14, Bedrock::R15,
  };

  if (RegNo >= std::size(Regs))
    return MCDisassembler::Fail;

  MI.addOperand(MCOperand::createReg(Regs[RegNo]));
  return MCDisassembler::Success;
}

#include "BedrockGenDisassemblerTables.inc"

DecodeStatus BedrockDisassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                                 ArrayRef<uint8_t> Bytes,
                                                 uint64_t Address,
                                                 raw_ostream &CStream) const {
  if (Bytes.size() < 2) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  uint64_t Word = support::endian::read16be(Bytes.data());
  if (Word & 0xc000) {
    SmallString<128> RawText;
    if (!BedrockMC::decodeRawInst(Bytes, Size, RawText)) {
      Size = 0;
      return MCDisassembler::Fail;
    }

    BedrockMC::createRawInst(Bytes.take_front(Size), MI);
    return MCDisassembler::Success;
  }

  DecodeStatus Result =
      decodeInstruction(DecoderTable16, MI, Word, Address, this, STI);
  if (Result != MCDisassembler::Fail) {
    Size = 2;
    return Result;
  }

  Size = 2;
  return MCDisassembler::Fail;
}

static MCDisassembler *createBedrockDisassembler(const Target &T,
                                                 const MCSubtargetInfo &STI,
                                                 MCContext &Ctx) {
  return new BedrockDisassembler(STI, Ctx);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeBedrockDisassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheBedrockTarget(),
                                         createBedrockDisassembler);
}
