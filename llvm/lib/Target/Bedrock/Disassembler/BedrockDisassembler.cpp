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
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

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

DecodeStatus BedrockDisassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                                 ArrayRef<uint8_t> Bytes,
                                                 uint64_t Address,
                                                 raw_ostream &CStream) const {
  if (Bytes.empty()) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  SmallString<128> RawText;
  if (BedrockMC::decodeRawInst(Bytes, Size, RawText)) {
    BedrockMC::createRawInst(Bytes.take_front(Size), MI);
    return MCDisassembler::Success;
  }

  // The first byte is the authoritative Bedrock framing selector. Once the
  // raw decoder rejects that framed record as reserved or malformed, do not
  // reinterpret its leading bytes through an unrelated legacy decode table.
  Size = 0;
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
