//===-- BedrockInstPrinter.cpp - Bedrock MCInst to assembly syntax --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockInstPrinter.h"
#include "BedrockMCEncoding.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define PRINT_ALIAS_INSTR
#include "BedrockGenAsmWriter.inc"

void BedrockInstPrinter::printRegName(raw_ostream &O, MCRegister Reg) {
  O << getRegisterName(Reg);
}

void BedrockInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                   StringRef Annot, const MCSubtargetInfo &STI,
                                   raw_ostream &O) {
  SmallVector<uint8_t, 16> RawBytes;
  SmallString<128> RawText;
  uint64_t RawSize = 0;
  if (MI->getOpcode() == Bedrock::RAW || MI->getOpcode() == Bedrock::RAW_EXPR) {
    if (BedrockMC::getRawInstBytes(*MI, RawBytes) &&
        BedrockMC::decodeRawInst(RawBytes, RawSize, RawText))
      O << RawText;
    else
      O << "<unknown>";
    printAnnotation(O, Annot);
    return;
  }

  if (!printAliasInstr(MI, Address, O))
    printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}

void BedrockInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                      raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    O << getRegisterName(Op.getReg());
    return;
  }
  if (Op.isImm()) {
    O << Op.getImm();
    return;
  }
  assert(Op.isExpr() && "unknown operand kind");
  MAI.printExpr(O, *Op.getExpr());
}

void BedrockInstPrinter::printSImm8Operand(const MCInst *MI, unsigned OpNo,
                                           raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (!Op.isImm()) {
    printOperand(MI, OpNo, O);
    return;
  }

  int64_t Value = Op.getImm() & 0xff;
  if (Value & 0x80)
    Value -= 0x100;
  O << Value;
}
