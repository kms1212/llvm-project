//===-- BedrockInstPrinter.h - Convert Bedrock MCInst to asm -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_MCTARGETDESC_BEDROCKINSTPRINTER_H
#define LLVM_LIB_TARGET_BEDROCK_MCTARGETDESC_BEDROCKINSTPRINTER_H

#include "llvm/MC/MCInstPrinter.h"

namespace llvm {

class MCExpr;

class BedrockInstPrinter : public MCInstPrinter {
public:
  BedrockInstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                     const MCRegisterInfo &MRI)
      : MCInstPrinter(MAI, MII, MRI) {}

  void printRegName(raw_ostream &OS, MCRegister Reg) override;
  void printInst(const MCInst *MI, uint64_t Address, StringRef Annot,
                 const MCSubtargetInfo &STI, raw_ostream &OS) override;
  static const char *getRegisterName(MCRegister Reg);

  void printOperand(const MCInst *MI, unsigned OpNo, raw_ostream &OS);
  void printShortImmOperand(const MCInst *MI, unsigned OpNo, raw_ostream &OS);
  void printMemOperand(const MCInst *MI, unsigned OpNo, raw_ostream &OS);
  void printPostIncMemOperand(const MCInst *MI, unsigned OpNo, raw_ostream &OS);
  void printUpdateMemOperand(const MCInst *MI, unsigned OpNo, raw_ostream &OS);
  void printLeaScale4Operand(const MCInst *MI, unsigned OpNo, raw_ostream &OS);
  void printLeaScale4LOperand(const MCInst *MI, unsigned OpNo, raw_ostream &OS);
  void printIndexScale1Operand(const MCInst *MI, unsigned OpNo,
                               raw_ostream &OS);
  void printIndexScale4Operand(const MCInst *MI, unsigned OpNo,
                               raw_ostream &OS);
  void printLongIndexScale4Operand(const MCInst *MI, unsigned OpNo,
                                   raw_ostream &OS);
  void printLongIndexScale8Operand(const MCInst *MI, unsigned OpNo,
                                   raw_ostream &OS);
  void printRelocOperand(const MCExpr *Expr, StringRef Reloc,
                         raw_ostream &OS) const;
  void printCondCode(const MCInst *MI, unsigned OpNo, raw_ostream &OS);
  void printRegMask16(const MCInst *MI, unsigned OpNo, raw_ostream &OS);
  void printFRegMask16(const MCInst *MI, unsigned OpNo, raw_ostream &OS);
  void printMemoryOrder(const MCInst *MI, unsigned OpNo, raw_ostream &OS);
  void printAbs64Operand(const MCInst *MI, unsigned OpNo, raw_ostream &OS);
  void printEncodedInst(const MCInst *MI, raw_ostream &OS);

  std::pair<const char *, uint64_t>
  getMnemonic(const MCInst &MI) const override;
  bool printAliasInstr(const MCInst *MI, uint64_t Address, raw_ostream &OS);
  void printInstruction(const MCInst *MI, uint64_t Address, raw_ostream &OS);
};

} // end namespace llvm

#endif
