//===-- BedrockAsmPrinter.cpp - Bedrock assembly printer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Bedrock.h"
#include "BedrockTargetMachine.h"
#include "MCTargetDesc/BedrockFixupKinds.h"
#include "MCTargetDesc/BedrockInstPrinter.h"
#include "MCTargetDesc/BedrockMCEncoding.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "TargetInfo/BedrockTargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {

class BedrockAsmPrinter : public AsmPrinter {
public:
  static char ID;

  BedrockAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override { return "Bedrock Assembly Printer"; }
  bool runOnMachineFunction(MachineFunction &MF) override;
  void emitInstruction(const MachineInstr *MI) override;

private:
  MCOperand lowerOperand(const MachineOperand &MO) const;
  const MCExpr *lowerSymbolOperand(const MachineOperand &MO) const;

  void emitMCInst(const MCInst &Inst);
  void emitRaw(ArrayRef<uint8_t> Bytes);
  void emitRawExpr(ArrayRef<uint8_t> Bytes, unsigned FixupOffset,
                   MCFixupKind Kind, const MCExpr *Expr);
  void emitRR(unsigned Opcode, Register DstReg, Register SrcReg);
  void emitConst(const MachineInstr *MI, bool Is64);
  void emitBinaryPseudo(const MachineInstr *MI, unsigned RealOpcode);
  void emitLongBinaryPseudo(const MachineInstr *MI, StringRef Pattern,
                            unsigned Size);
  void emitFrameAddress(const MachineInstr *MI);
  void emitAbsLoad(const MachineInstr *MI, unsigned Size, bool IsExt = false,
                   bool IsSigned = false);
  void emitAbsStore(const MachineInstr *MI, unsigned Size);
  void emitLoad(const MachineInstr *MI, unsigned Size, bool IsFrame,
                bool IsExt = false, bool IsSigned = false);
  void emitStore(const MachineInstr *MI, unsigned Size, bool IsFrame);
  void emitFpuRaw(uint16_t Primary, uint16_t Ext, ArrayRef<uint8_t> Tail);
  void emitFpuMove(Register DstReg, Register SrcReg);
  void emitFpuMove(const MachineInstr *MI);
  void emitFpuBinaryPseudo(const MachineInstr *MI, uint16_t BaseExt);
  void emitFpuConvert(const MachineInstr *MI, bool IsUnsigned);
  void emitFpuAbsLoad(const MachineInstr *MI);
  void emitFpuAbsStore(const MachineInstr *MI);
  void emitFpuLoad(const MachineInstr *MI, bool IsFrame);
  void emitFpuStore(const MachineInstr *MI, bool IsFrame);
  void emitStackAdjust(const MachineInstr *MI, bool IsDown);
  void emitBranch(const MachineInstr *MI, bool IsCond);
  void emitSetCC(const MachineInstr *MI);
  void emitCall(const MachineInstr *MI);
  void emitIndirectCall(const MachineInstr *MI);
};

} // namespace

static void appendLE(SmallVectorImpl<uint8_t> &Bytes, uint64_t Value,
                     unsigned Width) {
  for (unsigned I = 0; I != Width; ++I)
    Bytes.push_back((Value >> (I * 8)) & 0xff);
}

static bool appendSignedAuto(int64_t Value, SmallVectorImpl<uint8_t> &Tail,
                             unsigned &WidthCode) {
  if (isInt<8>(Value)) {
    WidthCode = 0;
    appendLE(Tail, static_cast<uint64_t>(Value), 1);
    return true;
  }
  if (isInt<16>(Value)) {
    WidthCode = 1;
    appendLE(Tail, static_cast<uint64_t>(Value), 2);
    return true;
  }
  if (isInt<32>(Value)) {
    WidthCode = 2;
    appendLE(Tail, static_cast<uint64_t>(Value), 4);
    return true;
  }

  WidthCode = 3;
  appendLE(Tail, static_cast<uint64_t>(Value), 8);
  return true;
}

static unsigned getGPRNo(Register Reg) {
  switch (Reg) {
  case Bedrock::R0:
    return 0;
  case Bedrock::R1:
    return 1;
  case Bedrock::R2:
    return 2;
  case Bedrock::R3:
    return 3;
  case Bedrock::R4:
    return 4;
  case Bedrock::R5:
    return 5;
  case Bedrock::R6:
    return 6;
  case Bedrock::R7:
    return 7;
  case Bedrock::R8:
    return 8;
  case Bedrock::R9:
    return 9;
  case Bedrock::R10:
    return 10;
  case Bedrock::R11:
    return 11;
  case Bedrock::R12:
    return 12;
  case Bedrock::R13:
    return 13;
  case Bedrock::R14:
    return 14;
  case Bedrock::R15:
    return 15;
  default:
    report_fatal_error("expected Bedrock GPR");
  }
}

static unsigned getFPRNo(Register Reg) {
  switch (Reg) {
  case Bedrock::F0:
    return 0;
  case Bedrock::F1:
    return 1;
  case Bedrock::F2:
    return 2;
  case Bedrock::F3:
    return 3;
  case Bedrock::F4:
    return 4;
  case Bedrock::F5:
    return 5;
  case Bedrock::F6:
    return 6;
  case Bedrock::F7:
    return 7;
  case Bedrock::F8:
    return 8;
  case Bedrock::F9:
    return 9;
  case Bedrock::F10:
    return 10;
  case Bedrock::F11:
    return 11;
  case Bedrock::F12:
    return 12;
  case Bedrock::F13:
    return 13;
  case Bedrock::F14:
    return 14;
  case Bedrock::F15:
    return 15;
  default:
    report_fatal_error("expected Bedrock FPR");
  }
}

static void appendBE16(SmallVectorImpl<uint8_t> &Bytes, uint16_t Value) {
  Bytes.push_back((Value >> 8) & 0xff);
  Bytes.push_back(Value & 0xff);
}

static uint32_t applyPattern(StringRef Pattern, uint8_t EA, unsigned Z,
                             unsigned Reg, char RegField) {
  unsigned EBits = 0;
  unsigned ZBits = 0;
  unsigned RBits = 0;
  for (char C : Pattern) {
    if (C == 'e')
      ++EBits;
    else if (C == 'z')
      ++ZBits;
    else if (C == RegField)
      ++RBits;
  }

  uint32_t Payload = 0;
  unsigned Width = Pattern.size();
  for (unsigned I = 0; I != Width; ++I) {
    unsigned Bit = Width - I - 1;
    switch (Pattern[I]) {
    case '0':
      break;
    case '1':
      Payload |= 1u << Bit;
      break;
    case 'e':
      Payload |= ((EA >> --EBits) & 1) << Bit;
      break;
    case 'z':
      Payload |= ((Z >> --ZBits) & 1) << Bit;
      break;
    case 'd':
    case 's':
      assert(Pattern[I] == RegField && "unexpected register field");
      Payload |= ((Reg >> --RBits) & 1) << Bit;
      break;
    default:
      llvm_unreachable("unexpected Bedrock pattern field");
    }
  }
  return Payload;
}

struct PatternFieldValue {
  char Field;
  unsigned Value;
};

static uint32_t applyPatternValues(StringRef Pattern,
                                   ArrayRef<PatternFieldValue> FieldValues) {
  unsigned Counts[256] = {};
  unsigned Values[256] = {};
  bool Active[256] = {};

  for (const PatternFieldValue &Field : FieldValues) {
    Active[static_cast<unsigned char>(Field.Field)] = true;
    Values[static_cast<unsigned char>(Field.Field)] = Field.Value;
  }

  for (char C : Pattern) {
    unsigned Index = static_cast<unsigned char>(C);
    if (Active[Index])
      ++Counts[Index];
  }

  uint32_t Payload = 0;
  unsigned Width = Pattern.size();
  for (unsigned I = 0; I != Width; ++I) {
    unsigned Bit = Width - I - 1;
    char C = Pattern[I];
    if (C == '0')
      continue;
    if (C == '1') {
      Payload |= 1u << Bit;
      continue;
    }

    unsigned Index = static_cast<unsigned char>(C);
    assert(Active[Index] && "missing Bedrock pattern field");
    Payload |= ((Values[Index] >> --Counts[Index]) & 1) << Bit;
  }
  return Payload;
}

static uint32_t getLeaPayload(uint8_t EA, unsigned Size, Register DstReg) {
  return applyPattern("0eeez1zdddd000eeee", EA, Size, getGPRNo(DstReg), 'd');
}

static const char *getCondSuffix(unsigned Cond) {
  switch (Cond) {
  case 0x2:
    return "eq";
  case 0x3:
    return "ne";
  case 0x4:
    return "ult";
  case 0x5:
    return "uge";
  case 0x6:
    return "mi";
  case 0x7:
    return "pl";
  case 0x8:
    return "vs";
  case 0x9:
    return "vc";
  case 0xa:
    return "ule";
  case 0xb:
    return "ugt";
  case 0xc:
    return "lt";
  case 0xd:
    return "ge";
  case 0xe:
    return "le";
  case 0xf:
    return "gt";
  default:
    report_fatal_error("invalid Bedrock condition code");
  }
}

static uint8_t getRegEA(Register Reg) { return getGPRNo(Reg); }

static void getMemEAForReg(Register BaseReg, uint8_t &EA,
                           SmallVectorImpl<uint8_t> &Tail) {
  EA = 0x10 | getGPRNo(BaseReg);
}

static void getMemEAForSP(int64_t Offset, uint8_t &EA,
                          SmallVectorImpl<uint8_t> &Tail) {
  if (Offset == 0) {
    EA = 0x69;
    return;
  }

  unsigned WidthCode;
  appendSignedAuto(Offset, Tail, WidthCode);
  EA = 0x60 + WidthCode;
}

static int64_t getFrameOffset(const MachineInstr *MI, unsigned BaseOp) {
  return MI->getOperand(BaseOp).getImm() + MI->getOperand(BaseOp + 1).getImm();
}

static uint32_t getMovPayload(bool IsLoad, unsigned Size, uint8_t EA,
                              Register Reg) {
  bool IsLQ = Size >= 2;
  StringRef Pattern;
  if (IsLoad)
    Pattern = IsLQ ? "000011zddddeeeeeee" : "000010zddddeeeeeee";
  else
    Pattern = IsLQ ? "000001zsssseeeeeee" : "000000zsssseeeeeee";
  return applyPattern(Pattern, EA, Size & 1, getGPRNo(Reg), IsLoad ? 'd' : 's');
}

static uint32_t getExtLoadPayload(unsigned Size, bool IsSigned, uint8_t EA,
                                  Register DstReg) {
  StringRef Pattern;
  if (IsSigned) {
    switch (Size) {
    case 0:
      Pattern = "1100011ddddeeeeeee";
      break;
    case 1:
      Pattern = "1100101ddddeeeeeee";
      break;
    case 2:
      Pattern = "1100111ddddeeeeeee";
      break;
    default:
      report_fatal_error("invalid Bedrock signed-ext load size");
    }
  } else {
    switch (Size) {
    case 0:
      Pattern = "1101011ddddeeeeeee";
      break;
    case 1:
      Pattern = "1101101ddddeeeeeee";
      break;
    case 2:
      Pattern = "1101111ddddeeeeeee";
      break;
    default:
      report_fatal_error("invalid Bedrock zero-ext load size");
    }
  }

  return applyPattern(Pattern, EA, 0, getGPRNo(DstReg), 'd');
}

bool BedrockAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  MF.ensureAlignment(Align(2));
  return AsmPrinter::runOnMachineFunction(MF);
}

const MCExpr *
BedrockAsmPrinter::lowerSymbolOperand(const MachineOperand &MO) const {
  const MCSymbol *Symbol = nullptr;
  int64_t Offset = 0;

  switch (MO.getType()) {
  case MachineOperand::MO_MachineBasicBlock:
    Symbol = MO.getMBB()->getSymbol();
    break;
  case MachineOperand::MO_GlobalAddress:
    Symbol = getSymbol(MO.getGlobal());
    Offset = MO.getOffset();
    break;
  case MachineOperand::MO_ExternalSymbol:
    Symbol = GetExternalSymbolSymbol(MO.getSymbolName());
    Offset = MO.getOffset();
    break;
  case MachineOperand::MO_BlockAddress:
    Symbol = GetBlockAddressSymbol(MO.getBlockAddress());
    Offset = MO.getOffset();
    break;
  case MachineOperand::MO_ConstantPoolIndex:
    Symbol = GetCPISymbol(MO.getIndex());
    Offset = MO.getOffset();
    break;
  case MachineOperand::MO_JumpTableIndex:
    Symbol = GetJTISymbol(MO.getIndex());
    break;
  default:
    llvm_unreachable("unknown Bedrock symbol operand");
  }

  const MCExpr *Expr = MCSymbolRefExpr::create(Symbol, OutContext);
  if (Offset != 0)
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(Offset, OutContext), OutContext);
  return Expr;
}

MCOperand BedrockAsmPrinter::lowerOperand(const MachineOperand &MO) const {
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    if (MO.isImplicit())
      return {};
    return MCOperand::createReg(MO.getReg());
  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(MO.getImm());
  case MachineOperand::MO_MachineBasicBlock:
  case MachineOperand::MO_GlobalAddress:
  case MachineOperand::MO_ExternalSymbol:
  case MachineOperand::MO_BlockAddress:
  case MachineOperand::MO_ConstantPoolIndex:
  case MachineOperand::MO_JumpTableIndex:
    return MCOperand::createExpr(lowerSymbolOperand(MO));
  case MachineOperand::MO_RegisterMask:
    return {};
  default:
    report_fatal_error("unknown Bedrock machine operand");
  }
}

void BedrockAsmPrinter::emitMCInst(const MCInst &Inst) {
  EmitToStreamer(*OutStreamer, Inst);
}

void BedrockAsmPrinter::emitRaw(ArrayRef<uint8_t> Bytes) {
  MCInst Inst;
  BedrockMC::createRawInst(Bytes, Inst);
  emitMCInst(Inst);
}

void BedrockAsmPrinter::emitRawExpr(ArrayRef<uint8_t> Bytes,
                                    unsigned FixupOffset, MCFixupKind Kind,
                                    const MCExpr *Expr) {
  MCInst Inst;
  Inst.setOpcode(Bedrock::RAW_EXPR);
  Inst.addOperand(MCOperand::createImm(1));
  Inst.addOperand(MCOperand::createImm(FixupOffset));
  Inst.addOperand(MCOperand::createImm(Kind));
  Inst.addOperand(MCOperand::createExpr(Expr));
  for (uint8_t Byte : Bytes)
    Inst.addOperand(MCOperand::createImm(Byte));
  emitMCInst(Inst);
}

void BedrockAsmPrinter::emitRR(unsigned Opcode, Register DstReg,
                               Register SrcReg) {
  MCInst Inst;
  Inst.setOpcode(Opcode);
  Inst.addOperand(MCOperand::createReg(DstReg));
  Inst.addOperand(MCOperand::createReg(SrcReg));
  emitMCInst(Inst);
}

void BedrockAsmPrinter::emitConst(const MachineInstr *MI, bool Is64) {
  Register DstReg = MI->getOperand(0).getReg();
  const MachineOperand &ImmOp = MI->getOperand(1);
  unsigned Size = Is64 ? 3 : 2;

  if (ImmOp.isImm()) {
    SmallVector<uint8_t, 8> Tail;
    SmallVector<uint8_t, 16> Bytes;
    unsigned WidthCode;
    appendSignedAuto(ImmOp.getImm(), Tail, WidthCode);
    uint8_t EA = 0x6c + WidthCode;
    if (!BedrockMC::encodeMedium(getLeaPayload(EA, Size, DstReg), Tail, Bytes))
      report_fatal_error("failed to encode Bedrock constant");
    emitRaw(Bytes);
    return;
  }

  const MCExpr *Expr = lowerSymbolOperand(ImmOp);
  if (OutStreamer->hasRawTextSupport()) {
    SmallString<80> Text;
    raw_svector_ostream OS(Text);
    OS << "\tlea." << (Is64 ? 'q' : 'l') << "\t";
    MAI->printExpr(OS, *Expr);
    OS << ", " << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  SmallVector<uint8_t, 4> Tail(4, 0);
  SmallVector<uint8_t, 8> Bytes;
  if (!BedrockMC::encodeMedium(getLeaPayload(0x6e, Size, DstReg), Tail, Bytes))
    report_fatal_error("failed to encode Bedrock symbolic constant");
  emitRawExpr(Bytes, 3, MCFixupKind(Bedrock::fixup_bedrock_imm32), Expr);
}

void BedrockAsmPrinter::emitBinaryPseudo(const MachineInstr *MI,
                                         unsigned RealOpcode) {
  Register DstReg = MI->getOperand(0).getReg();
  Register LHSReg = MI->getOperand(1).getReg();
  Register RHSReg = MI->getOperand(2).getReg();

  if (DstReg != LHSReg)
    emitRR(RealOpcode == Bedrock::ADDLrr || RealOpcode == Bedrock::SUBLrr ||
                   RealOpcode == Bedrock::ANDLrr ||
                   RealOpcode == Bedrock::ORLrr || RealOpcode == Bedrock::XORLrr
               ? Bedrock::MOVLrr
               : Bedrock::MOVQrr,
           DstReg, LHSReg);
  emitRR(RealOpcode, DstReg, RHSReg);
}

void BedrockAsmPrinter::emitLongBinaryPseudo(const MachineInstr *MI,
                                             StringRef Pattern, unsigned Size) {
  Register DstReg = MI->getOperand(0).getReg();
  Register LHSReg = MI->getOperand(1).getReg();
  Register RHSReg = MI->getOperand(2).getReg();

  if (DstReg != LHSReg)
    emitRR(Size == 2 ? Bedrock::MOVLrr : Bedrock::MOVQrr, DstReg, LHSReg);

  SmallVector<uint8_t, 8> Bytes;
  uint32_t Payload =
      applyPattern(Pattern, getRegEA(RHSReg), Size, getGPRNo(DstReg), 'd');
  if (!BedrockMC::encodeLong(Payload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock long binary pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitFrameAddress(const MachineInstr *MI) {
  Register DstReg = MI->getOperand(0).getReg();
  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  getMemEAForSP(getFrameOffset(MI, 1), EA, Tail);

  SmallVector<uint8_t, 16> Bytes;
  if (!BedrockMC::encodeMedium(getLeaPayload(EA, 3, DstReg), Tail, Bytes))
    report_fatal_error("failed to encode Bedrock frame address");
  emitRaw(Bytes);
}

static char getSizeSuffix(unsigned Size) {
  static const char Suffixes[] = {'b', 'w', 'l', 'q'};
  assert(Size < std::size(Suffixes) && "invalid Bedrock size suffix");
  return Suffixes[Size];
}

void BedrockAsmPrinter::emitAbsLoad(const MachineInstr *MI, unsigned Size,
                                    bool IsExt, bool IsSigned) {
  Register DstReg = MI->getOperand(0).getReg();
  const MachineOperand &Addr = MI->getOperand(1);
  const MCExpr *Expr = lowerSymbolOperand(Addr);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<96> Text;
    raw_svector_ostream OS(Text);
    if (IsExt)
      OS << "\text" << (IsSigned ? 's' : 'z') << "q." << getSizeSuffix(Size)
         << "\t[";
    else
      OS << "\tmov." << getSizeSuffix(Size) << "\t[";
    MAI->printExpr(OS, *Expr);
    OS << "], " << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  SmallVector<uint8_t, 4> Tail(4, 0);
  SmallVector<uint8_t, 8> Bytes;
  uint32_t Payload = IsExt ? getExtLoadPayload(Size, IsSigned, 0x6a, DstReg)
                           : getMovPayload(/*IsLoad=*/true, Size, 0x6a, DstReg);
  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock absolute load");
  emitRawExpr(Bytes, 3, FK_Data_4, Expr);
}

void BedrockAsmPrinter::emitAbsStore(const MachineInstr *MI, unsigned Size) {
  Register SrcReg = MI->getOperand(0).getReg();
  const MachineOperand &Addr = MI->getOperand(1);
  const MCExpr *Expr = lowerSymbolOperand(Addr);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<96> Text;
    raw_svector_ostream OS(Text);
    OS << "\tmov." << getSizeSuffix(Size) << "\t"
       << BedrockInstPrinter::getRegisterName(SrcReg) << ", [";
    MAI->printExpr(OS, *Expr);
    OS << "]";
    OutStreamer->emitRawText(OS.str());
    return;
  }

  SmallVector<uint8_t, 4> Tail(4, 0);
  SmallVector<uint8_t, 8> Bytes;
  if (!BedrockMC::encodeMedium(
          getMovPayload(/*IsLoad=*/false, Size, 0x6a, SrcReg), Tail, Bytes))
    report_fatal_error("failed to encode Bedrock absolute store");
  emitRawExpr(Bytes, 3, FK_Data_4, Expr);
}

void BedrockAsmPrinter::emitLoad(const MachineInstr *MI, unsigned Size,
                                 bool IsFrame, bool IsExt, bool IsSigned) {
  Register DstReg = MI->getOperand(0).getReg();

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  if (IsFrame)
    getMemEAForSP(getFrameOffset(MI, 1), EA, Tail);
  else
    getMemEAForReg(MI->getOperand(1).getReg(), EA, Tail);

  uint32_t Payload = IsExt ? getExtLoadPayload(Size, IsSigned, EA, DstReg)
                           : getMovPayload(/*IsLoad=*/true, Size, EA, DstReg);

  SmallVector<uint8_t, 16> Bytes;
  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock load pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitStore(const MachineInstr *MI, unsigned Size,
                                  bool IsFrame) {
  Register SrcReg = MI->getOperand(0).getReg();

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  if (IsFrame)
    getMemEAForSP(getFrameOffset(MI, 1), EA, Tail);
  else
    getMemEAForReg(MI->getOperand(1).getReg(), EA, Tail);

  SmallVector<uint8_t, 16> Bytes;
  if (!BedrockMC::encodeMedium(
          getMovPayload(/*IsLoad=*/false, Size, EA, SrcReg), Tail, Bytes))
    report_fatal_error("failed to encode Bedrock store pseudo");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitFpuRaw(uint16_t Primary, uint16_t Ext,
                                   ArrayRef<uint8_t> Tail) {
  SmallVector<uint8_t, 16> Bytes;
  appendBE16(Bytes, Primary);
  appendBE16(Bytes, Ext);
  Bytes.append(Tail.begin(), Tail.end());
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitFpuMove(Register DstReg, Register SrcReg) {
  uint16_t Ext =
      0x0400 | 0x0100 | (getFPRNo(DstReg) << 4) | getFPRNo(SrcReg);
  emitFpuRaw(0x1f65, Ext, {});
}

void BedrockAsmPrinter::emitFpuMove(const MachineInstr *MI) {
  emitFpuMove(MI->getOperand(0).getReg(), MI->getOperand(1).getReg());
}

void BedrockAsmPrinter::emitFpuBinaryPseudo(const MachineInstr *MI,
                                            uint16_t BaseExt) {
  Register DstReg = MI->getOperand(0).getReg();
  Register LHSReg = MI->getOperand(1).getReg();
  Register RHSReg = MI->getOperand(2).getReg();

  if (DstReg != LHSReg)
    emitFpuMove(DstReg, LHSReg);

  uint16_t Ext =
      BaseExt | 0x0100 | (getFPRNo(DstReg) << 4) | getFPRNo(RHSReg);
  emitFpuRaw(0x1f67, Ext, {});
}

void BedrockAsmPrinter::emitFpuConvert(const MachineInstr *MI,
                                       bool IsUnsigned) {
  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();
  uint16_t Ext =
      (IsUnsigned ? 0x0680 : 0x0280) | (getFPRNo(DstReg) << 3) | getGPRNo(SrcReg);
  emitFpuRaw(0x1f65, Ext, {});
}

void BedrockAsmPrinter::emitFpuAbsLoad(const MachineInstr *MI) {
  Register DstReg = MI->getOperand(0).getReg();
  const MachineOperand &Addr = MI->getOperand(1);
  const MCExpr *Expr = lowerSymbolOperand(Addr);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<96> Text;
    raw_svector_ostream OS(Text);
    OS << "\tFMOV.D\t[";
    MAI->printExpr(OS, *Expr);
    OS << "], " << BedrockInstPrinter::getRegisterName(DstReg);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  SmallVector<uint8_t, 8> Bytes;
  appendBE16(Bytes, 0x1f65);
  appendBE16(Bytes, 0x1800 | 0x0400 | (getFPRNo(DstReg) << 6) | 0x6a);
  Bytes.append(4, 0);
  emitRawExpr(Bytes, 4, FK_Data_4, Expr);
}

void BedrockAsmPrinter::emitFpuAbsStore(const MachineInstr *MI) {
  Register SrcReg = MI->getOperand(0).getReg();
  const MachineOperand &Addr = MI->getOperand(1);
  const MCExpr *Expr = lowerSymbolOperand(Addr);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<96> Text;
    raw_svector_ostream OS(Text);
    OS << "\tFMOV.D\t" << BedrockInstPrinter::getRegisterName(SrcReg)
       << ", [";
    MAI->printExpr(OS, *Expr);
    OS << "]";
    OutStreamer->emitRawText(OS.str());
    return;
  }

  SmallVector<uint8_t, 8> Bytes;
  appendBE16(Bytes, 0x1f65);
  appendBE16(Bytes, 0x2000 | 0x0400 | (getFPRNo(SrcReg) << 6) | 0x6a);
  Bytes.append(4, 0);
  emitRawExpr(Bytes, 4, FK_Data_4, Expr);
}

void BedrockAsmPrinter::emitFpuLoad(const MachineInstr *MI, bool IsFrame) {
  Register DstReg = MI->getOperand(0).getReg();

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  if (IsFrame)
    getMemEAForSP(getFrameOffset(MI, 1), EA, Tail);
  else
    getMemEAForReg(MI->getOperand(1).getReg(), EA, Tail);

  uint16_t Ext = 0x1800 | 0x0400 | (getFPRNo(DstReg) << 6) | EA;
  emitFpuRaw(0x1f65, Ext, Tail);
}

void BedrockAsmPrinter::emitFpuStore(const MachineInstr *MI, bool IsFrame) {
  Register SrcReg = MI->getOperand(0).getReg();

  uint8_t EA;
  SmallVector<uint8_t, 8> Tail;
  if (IsFrame)
    getMemEAForSP(getFrameOffset(MI, 1), EA, Tail);
  else
    getMemEAForReg(MI->getOperand(1).getReg(), EA, Tail);

  uint16_t Ext = 0x2000 | 0x0400 | (getFPRNo(SrcReg) << 6) | EA;
  emitFpuRaw(0x1f65, Ext, Tail);
}

void BedrockAsmPrinter::emitStackAdjust(const MachineInstr *MI, bool IsDown) {
  uint64_t Amount = MI->getOperand(0).getImm();
  if (Amount == 0)
    return;

  if (Amount <= 0xff) {
    MCInst Inst;
    Inst.setOpcode(IsDown ? Bedrock::SUBQisp : Bedrock::ADDQisp);
    Inst.addOperand(MCOperand::createImm(Amount));
    emitMCInst(Inst);
    return;
  }

  SmallVector<uint8_t, 4> Tail;
  SmallVector<uint8_t, 8> Bytes;
  uint32_t Payload;
  if (isUInt<16>(Amount)) {
    appendLE(Tail, Amount, 2);
    Payload = IsDown ? 0x2782 : 0x2780;
  } else if (isUInt<32>(Amount)) {
    appendLE(Tail, Amount, 4);
    Payload = IsDown ? 0x2783 : 0x2781;
  } else {
    report_fatal_error("Bedrock stack adjustment does not fit");
  }

  if (!BedrockMC::encodeMedium(Payload, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock stack adjustment");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitBranch(const MachineInstr *MI, bool IsCond) {
  const MachineOperand &Target = MI->getOperand(0);
  unsigned Cond = IsCond ? MI->getOperand(1).getImm() : 0;
  const MCExpr *Expr = lowerSymbolOperand(Target);

  if (OutStreamer->hasRawTextSupport()) {
    SmallString<80> Text;
    raw_svector_ostream OS(Text);
    if (IsCond)
      OS << "\tj." << getCondSuffix(Cond) << "\t";
    else
      OS << "\tjmp\t";
    MAI->printExpr(OS, *Expr);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  SmallVector<uint8_t, 4> Tail(4, 0);
  SmallVector<uint8_t, 8> Bytes;
  if (!BedrockMC::encodeMedium(0x6600 | Cond, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock branch");
  emitRawExpr(Bytes, 3, MCFixupKind(Bedrock::fixup_bedrock_brdisp32), Expr);
}

void BedrockAsmPrinter::emitSetCC(const MachineInstr *MI) {
  Register DstReg = MI->getOperand(0).getReg();
  unsigned Cond = MI->getOperand(1).getImm();
  SmallVector<uint8_t, 2> Bytes;
  uint16_t Payload = 0x2100 | (getGPRNo(DstReg) << 4) | Cond;
  Bytes.push_back((Payload >> 8) & 0x3f);
  Bytes.push_back(Payload & 0xff);
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitCall(const MachineInstr *MI) {
  const MachineOperand &Target = MI->getOperand(0);

  if (OutStreamer->hasRawTextSupport() && !Target.isImm()) {
    const MCExpr *Expr = lowerSymbolOperand(Target);
    SmallString<80> Text;
    raw_svector_ostream OS(Text);
    OS << "\tcall\t";
    MAI->printExpr(OS, *Expr);
    OutStreamer->emitRawText(OS.str());
    return;
  }

  SmallVector<uint8_t, 4> Tail;
  SmallVector<uint8_t, 8> Bytes;
  if (Target.isImm())
    appendLE(Tail, static_cast<uint64_t>(Target.getImm()), 4);
  else
    Tail.append(4, 0);
  if (!BedrockMC::encodeMedium(0xe600, Tail, Bytes))
    report_fatal_error("failed to encode Bedrock call");
  if (Target.isImm()) {
    emitRaw(Bytes);
    return;
  }

  emitRawExpr(Bytes, 3, MCFixupKind(Bedrock::fixup_bedrock_call32),
              lowerSymbolOperand(Target));
}

void BedrockAsmPrinter::emitIndirectCall(const MachineInstr *MI) {
  Register CalleeReg = MI->getOperand(0).getReg();

  SmallVector<uint8_t, 4> Bytes;
  uint32_t RdsegPayload = applyPatternValues(
      "1111101111010000100sssdddd", {{'s', 0}, {'d', getGPRNo(Bedrock::R6)}});
  if (!BedrockMC::encodeLong(RdsegPayload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock indirect call segment read");
  emitRaw(Bytes);

  Bytes.clear();
  uint32_t LCallPayload =
      applyPatternValues("111100001100101rrrreeeeeee",
                         {{'r', getGPRNo(Bedrock::R6)},
                          {'e', getRegEA(CalleeReg)}});
  if (!BedrockMC::encodeLong(LCallPayload, {}, Bytes))
    report_fatal_error("failed to encode Bedrock indirect call");
  emitRaw(Bytes);
}

void BedrockAsmPrinter::emitInstruction(const MachineInstr *MI) {
  switch (MI->getOpcode()) {
  case Bedrock::CONST32:
    emitConst(MI, /*Is64=*/false);
    return;
  case Bedrock::CONST64:
    emitConst(MI, /*Is64=*/true);
    return;
  case Bedrock::ADDL3rr:
    emitBinaryPseudo(MI, Bedrock::ADDLrr);
    return;
  case Bedrock::ADDQ3rr:
    emitBinaryPseudo(MI, Bedrock::ADDQrr);
    return;
  case Bedrock::SUBL3rr:
    emitBinaryPseudo(MI, Bedrock::SUBLrr);
    return;
  case Bedrock::SUBQ3rr:
    emitBinaryPseudo(MI, Bedrock::SUBQrr);
    return;
  case Bedrock::ANDL3rr:
    emitBinaryPseudo(MI, Bedrock::ANDLrr);
    return;
  case Bedrock::ANDQ3rr:
    emitBinaryPseudo(MI, Bedrock::ANDQrr);
    return;
  case Bedrock::ORL3rr:
    emitBinaryPseudo(MI, Bedrock::ORLrr);
    return;
  case Bedrock::ORQ3rr:
    emitBinaryPseudo(MI, Bedrock::ORQrr);
    return;
  case Bedrock::XORL3rr:
    emitBinaryPseudo(MI, Bedrock::XORLrr);
    return;
  case Bedrock::XORQ3rr:
    emitBinaryPseudo(MI, Bedrock::XORQrr);
    return;
  case Bedrock::SHLL3rr:
    emitBinaryPseudo(MI, Bedrock::SHLLrr);
    return;
  case Bedrock::SHLQ3rr:
    emitBinaryPseudo(MI, Bedrock::SHLQrr);
    return;
  case Bedrock::SHRL3rr:
    emitBinaryPseudo(MI, Bedrock::SHRLrr);
    return;
  case Bedrock::SHRQ3rr:
    emitBinaryPseudo(MI, Bedrock::SHRQrr);
    return;
  case Bedrock::SARL3rr:
    emitBinaryPseudo(MI, Bedrock::SARLrr);
    return;
  case Bedrock::SARQ3rr:
    emitBinaryPseudo(MI, Bedrock::SARQrr);
    return;
  case Bedrock::MULL3rr:
    emitLongBinaryPseudo(MI, "1111000010zz010ddddeeeeeee", 2);
    return;
  case Bedrock::MULQ3rr:
    emitLongBinaryPseudo(MI, "1111000010zz010ddddeeeeeee", 3);
    return;
  case Bedrock::DIVUL3rr:
    emitLongBinaryPseudo(MI, "1111000010zz100ddddeeeeeee", 2);
    return;
  case Bedrock::DIVUQ3rr:
    emitLongBinaryPseudo(MI, "1111000010zz100ddddeeeeeee", 3);
    return;
  case Bedrock::DIVSL3rr:
    emitLongBinaryPseudo(MI, "1111000010zz101ddddeeeeeee", 2);
    return;
  case Bedrock::DIVSQ3rr:
    emitLongBinaryPseudo(MI, "1111000010zz101ddddeeeeeee", 3);
    return;
  case Bedrock::MODUL3rr:
    emitLongBinaryPseudo(MI, "1111000010zz110ddddeeeeeee", 2);
    return;
  case Bedrock::MODUQ3rr:
    emitLongBinaryPseudo(MI, "1111000010zz110ddddeeeeeee", 3);
    return;
  case Bedrock::MODSL3rr:
    emitLongBinaryPseudo(MI, "1111000010zz111ddddeeeeeee", 2);
    return;
  case Bedrock::MODSQ3rr:
    emitLongBinaryPseudo(MI, "1111000010zz111ddddeeeeeee", 3);
    return;
  case Bedrock::FMOVDrr:
    emitFpuMove(MI);
    return;
  case Bedrock::FADDDrr:
    emitFpuBinaryPseudo(MI, 0x0200);
    return;
  case Bedrock::FSUBDrr:
    emitFpuBinaryPseudo(MI, 0xc600);
    return;
  case Bedrock::FMULDrr:
    emitFpuBinaryPseudo(MI, 0x8a00);
    return;
  case Bedrock::FDIVDrr:
    emitFpuBinaryPseudo(MI, 0x2000);
    return;
  case Bedrock::FCVTSQDrr:
    emitFpuConvert(MI, /*IsUnsigned=*/false);
    return;
  case Bedrock::FCVTUQDrr:
    emitFpuConvert(MI, /*IsUnsigned=*/true);
    return;
  case Bedrock::LEAfi:
    emitFrameAddress(MI);
    return;
  case Bedrock::LOADB_Zrr:
    emitLoad(MI, 0, /*IsFrame=*/false, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADW_Zrr:
    emitLoad(MI, 1, /*IsFrame=*/false, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADL_Zrr:
    emitLoad(MI, 2, /*IsFrame=*/false, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADB_Srr:
    emitLoad(MI, 0, /*IsFrame=*/false, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADW_Srr:
    emitLoad(MI, 1, /*IsFrame=*/false, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADL_Srr:
    emitLoad(MI, 2, /*IsFrame=*/false, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADLrr:
    emitLoad(MI, 2, /*IsFrame=*/false);
    return;
  case Bedrock::LOADQrr:
    emitLoad(MI, 3, /*IsFrame=*/false);
    return;
  case Bedrock::FLOADDrr:
    emitFpuLoad(MI, /*IsFrame=*/false);
    return;
  case Bedrock::LOADB_Zabs:
    emitAbsLoad(MI, 0, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADW_Zabs:
    emitAbsLoad(MI, 1, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADL_Zabs:
    emitAbsLoad(MI, 2, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADB_Sabs:
    emitAbsLoad(MI, 0, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADW_Sabs:
    emitAbsLoad(MI, 1, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADL_Sabs:
    emitAbsLoad(MI, 2, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADLabs:
    emitAbsLoad(MI, 2);
    return;
  case Bedrock::LOADQabs:
    emitAbsLoad(MI, 3);
    return;
  case Bedrock::FLOADDabs:
    emitFpuAbsLoad(MI);
    return;
  case Bedrock::LOADB_Zfi:
    emitLoad(MI, 0, /*IsFrame=*/true, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADW_Zfi:
    emitLoad(MI, 1, /*IsFrame=*/true, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADL_Zfi:
    emitLoad(MI, 2, /*IsFrame=*/true, /*IsExt=*/true, /*IsSigned=*/false);
    return;
  case Bedrock::LOADB_Sfi:
    emitLoad(MI, 0, /*IsFrame=*/true, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADW_Sfi:
    emitLoad(MI, 1, /*IsFrame=*/true, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADL_Sfi:
    emitLoad(MI, 2, /*IsFrame=*/true, /*IsExt=*/true, /*IsSigned=*/true);
    return;
  case Bedrock::LOADLfi:
    emitLoad(MI, 2, /*IsFrame=*/true);
    return;
  case Bedrock::LOADQfi:
    emitLoad(MI, 3, /*IsFrame=*/true);
    return;
  case Bedrock::FLOADDfi:
    emitFpuLoad(MI, /*IsFrame=*/true);
    return;
  case Bedrock::STOREBrr:
    emitStore(MI, 0, /*IsFrame=*/false);
    return;
  case Bedrock::STOREWrr:
    emitStore(MI, 1, /*IsFrame=*/false);
    return;
  case Bedrock::STORELrr:
    emitStore(MI, 2, /*IsFrame=*/false);
    return;
  case Bedrock::STOREQrr:
    emitStore(MI, 3, /*IsFrame=*/false);
    return;
  case Bedrock::FSTOREDrr:
    emitFpuStore(MI, /*IsFrame=*/false);
    return;
  case Bedrock::STOREBabs:
    emitAbsStore(MI, 0);
    return;
  case Bedrock::STOREWabs:
    emitAbsStore(MI, 1);
    return;
  case Bedrock::STORELabs:
    emitAbsStore(MI, 2);
    return;
  case Bedrock::STOREQabs:
    emitAbsStore(MI, 3);
    return;
  case Bedrock::FSTOREDabs:
    emitFpuAbsStore(MI);
    return;
  case Bedrock::STOREBfi:
    emitStore(MI, 0, /*IsFrame=*/true);
    return;
  case Bedrock::STOREWfi:
    emitStore(MI, 1, /*IsFrame=*/true);
    return;
  case Bedrock::STORELfi:
    emitStore(MI, 2, /*IsFrame=*/true);
    return;
  case Bedrock::STOREQfi:
    emitStore(MI, 3, /*IsFrame=*/true);
    return;
  case Bedrock::FSTOREDfi:
    emitFpuStore(MI, /*IsFrame=*/true);
    return;
  case Bedrock::ADJSP_DOWN:
    emitStackAdjust(MI, /*IsDown=*/true);
    return;
  case Bedrock::ADJSP_UP:
    emitStackAdjust(MI, /*IsDown=*/false);
    return;
  case Bedrock::BR:
    emitBranch(MI, /*IsCond=*/false);
    return;
  case Bedrock::BRCC:
    emitBranch(MI, /*IsCond=*/true);
    return;
  case Bedrock::SETCC:
    emitSetCC(MI);
    return;
  case Bedrock::CALL:
    emitCall(MI);
    return;
  case Bedrock::CALLr:
    emitIndirectCall(MI);
    return;
  }

  Bedrock_MC::verifyInstructionPredicates(MI->getOpcode(),
                                          getSubtargetInfo().getFeatureBits());

  MCInst Inst;
  Inst.setOpcode(MI->getOpcode());
  for (const MachineOperand &MO : MI->operands()) {
    MCOperand MCOp = lowerOperand(MO);
    if (MCOp.isValid())
      Inst.addOperand(MCOp);
  }
  emitMCInst(Inst);
}

char BedrockAsmPrinter::ID = 0;

INITIALIZE_PASS(BedrockAsmPrinter, "bedrock-asm-printer",
                "Bedrock Assembly Printer", false, false)

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeBedrockAsmPrinter() {
  RegisterAsmPrinter<BedrockAsmPrinter> X(getTheBedrockTarget());
}
