//===-- BedrockMCInstLower.cpp - Lower MachineInstr to MCInst -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockMCInstLower.h"

#include "Bedrock.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

static const MCExpr *lowerSymbolOperand(const MachineOperand &MO,
                                        MCSymbol *Symbol, MCContext &Ctx) {
  const MCExpr *Expr = MCSymbolRefExpr::create(Symbol, Ctx);
  if (MO.getOffset() == 0)
    return Expr;
  const MCExpr *Offset = MCConstantExpr::create(MO.getOffset(), Ctx);
  return MCBinaryExpr::createAdd(Expr, Offset, Ctx);
}

MCOperand BedrockMCInstLower::lowerOperand(const MachineOperand &MO) const {
  switch (MO.getType()) {
  default:
    report_fatal_error("unsupported Bedrock machine operand");
  case MachineOperand::MO_RegisterMask:
  case MachineOperand::MO_RegisterLiveOut:
    return MCOperand();
  case MachineOperand::MO_Register:
    if (!MO.getReg() || MO.isImplicit())
      return MCOperand();
    return MCOperand::createReg(MO.getReg());
  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(MO.getImm());
  case MachineOperand::MO_MachineBasicBlock:
    return MCOperand::createExpr(
        MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx));
  case MachineOperand::MO_GlobalAddress:
    return MCOperand::createExpr(
        lowerSymbolOperand(MO, Printer.getSymbol(MO.getGlobal()), Ctx));
  case MachineOperand::MO_ExternalSymbol:
    return MCOperand::createExpr(lowerSymbolOperand(
        MO, Printer.GetExternalSymbolSymbol(MO.getSymbolName()), Ctx));
  case MachineOperand::MO_BlockAddress:
    return MCOperand::createExpr(lowerSymbolOperand(
        MO, Printer.GetBlockAddressSymbol(MO.getBlockAddress()), Ctx));
  case MachineOperand::MO_ConstantPoolIndex:
    return MCOperand::createExpr(
        lowerSymbolOperand(MO, Printer.GetCPISymbol(MO.getIndex()), Ctx));
  }
}

void BedrockMCInstLower::lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());
  for (const MachineOperand &MO : MI->operands()) {
    if (MO.isReg() && MO.isImplicit())
      continue;
    MCOperand MCOp = lowerOperand(MO);
    if (MCOp.isValid())
      OutMI.addOperand(MCOp);
  }
}
