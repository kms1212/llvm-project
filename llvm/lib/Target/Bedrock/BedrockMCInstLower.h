//===-- BedrockMCInstLower.h - Lower MachineInstr to MCInst -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_BEDROCKMCINSTLOWER_H
#define LLVM_LIB_TARGET_BEDROCK_BEDROCKMCINSTLOWER_H

#include "llvm/CodeGen/AsmPrinter.h"

namespace llvm {
class MCOperand;

class BedrockMCInstLower {
  MCContext &Ctx;
  AsmPrinter &Printer;

public:
  BedrockMCInstLower(MCContext &Ctx, AsmPrinter &Printer)
      : Ctx(Ctx), Printer(Printer) {}

  void lower(const MachineInstr *MI, MCInst &OutMI) const;
  MCOperand lowerOperand(const MachineOperand &MO) const;
};
} // end namespace llvm

#endif
