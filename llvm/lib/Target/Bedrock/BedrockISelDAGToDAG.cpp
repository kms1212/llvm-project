//===-- BedrockISelDAGToDAG.cpp - Bedrock DAG instruction selector --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Bedrock.h"
#include "BedrockISelLowering.h"
#include "BedrockSubtarget.h"
#include "BedrockTargetMachine.h"
#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "bedrock-isel"
#define PASS_NAME "Bedrock DAG->DAG Pattern Instruction Selection"

namespace {

class BedrockDAGToDAGISel : public SelectionDAGISel {
  const BedrockSubtarget *Subtarget = nullptr;

public:
  BedrockDAGToDAGISel() = delete;
  BedrockDAGToDAGISel(BedrockTargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    Subtarget = &MF.getSubtarget<BedrockSubtarget>();
    return SelectionDAGISel::runOnMachineFunction(MF);
  }

  void Select(SDNode *N) override;

#include "BedrockGenDAGISel.inc"
};

class BedrockDAGToDAGISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;
  BedrockDAGToDAGISelLegacy(BedrockTargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISelLegacy(
            ID, std::make_unique<BedrockDAGToDAGISel>(TM, OptLevel)) {}
};

char BedrockDAGToDAGISelLegacy::ID;

} // namespace

INITIALIZE_PASS(BedrockDAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createBedrockISelDag(BedrockTargetMachine &TM,
                                         CodeGenOptLevel OptLevel) {
  return new BedrockDAGToDAGISelLegacy(TM, OptLevel);
}

static unsigned getBinaryPseudo(unsigned Opcode, MVT VT) {
  bool Is64 = VT == MVT::i64;
  switch (Opcode) {
  case ISD::ADD:
    return Is64 ? Bedrock::ADDQ3rr : Bedrock::ADDL3rr;
  case ISD::SUB:
    return Is64 ? Bedrock::SUBQ3rr : Bedrock::SUBL3rr;
  case ISD::AND:
    return Is64 ? Bedrock::ANDQ3rr : Bedrock::ANDL3rr;
  case ISD::OR:
    return Is64 ? Bedrock::ORQ3rr : Bedrock::ORL3rr;
  case ISD::XOR:
    return Is64 ? Bedrock::XORQ3rr : Bedrock::XORL3rr;
  case ISD::SHL:
    return Is64 ? Bedrock::SHLQ3rr : Bedrock::SHLL3rr;
  case ISD::ROTL:
    return Is64 ? Bedrock::ROLQ3rr : Bedrock::ROLL3rr;
  case ISD::ROTR:
    return Is64 ? Bedrock::RORQ3rr : Bedrock::RORL3rr;
  case ISD::SRL:
    return Is64 ? Bedrock::SHRQ3rr : Bedrock::SHRL3rr;
  case ISD::SRA:
    return Is64 ? Bedrock::SARQ3rr : Bedrock::SARL3rr;
  case ISD::MUL:
    return Is64 ? Bedrock::MULQ3rr : Bedrock::MULL3rr;
  case ISD::UDIV:
    return Is64 ? Bedrock::DIVUQ3rr : Bedrock::DIVUL3rr;
  case ISD::SDIV:
    return Is64 ? Bedrock::DIVSQ3rr : Bedrock::DIVSL3rr;
  case ISD::UREM:
    return Is64 ? Bedrock::MODUQ3rr : Bedrock::MODUL3rr;
  case ISD::SREM:
    return Is64 ? Bedrock::MODSQ3rr : Bedrock::MODSL3rr;
  default:
    llvm_unreachable("unexpected Bedrock binary pseudo");
  }
}

static unsigned getBinaryImmPseudo(unsigned Opcode, MVT VT) {
  bool Is64 = VT == MVT::i64;
  switch (Opcode) {
  case ISD::ADD:
    return Is64 ? Bedrock::ADDQ3ri : Bedrock::ADDL3ri;
  case ISD::SUB:
    return Is64 ? Bedrock::SUBQ3ri : Bedrock::SUBL3ri;
  case ISD::AND:
    return Is64 ? Bedrock::ANDQ3ri : Bedrock::ANDL3ri;
  case ISD::OR:
    return Is64 ? Bedrock::ORQ3ri : Bedrock::ORL3ri;
  case ISD::XOR:
    return Is64 ? Bedrock::XORQ3ri : Bedrock::XORL3ri;
  case ISD::SHL:
    return Is64 ? Bedrock::SHLQ3ri : Bedrock::SHLL3ri;
  case ISD::ROTL:
    return Is64 ? Bedrock::ROLQ3ri : Bedrock::ROLL3ri;
  case ISD::ROTR:
    return Is64 ? Bedrock::RORQ3ri : Bedrock::RORL3ri;
  case ISD::SRL:
    return Is64 ? Bedrock::SHRQ3ri : Bedrock::SHRL3ri;
  case ISD::SRA:
    return Is64 ? Bedrock::SARQ3ri : Bedrock::SARL3ri;
  case ISD::SDIV:
    return Is64 ? Bedrock::DIVSQ3ri : Bedrock::DIVSL3ri;
  default:
    llvm_unreachable("unexpected Bedrock binary immediate pseudo");
  }
}

static unsigned getIncDecPseudo(unsigned Opcode, MVT VT, int64_t Imm) {
  bool Is64 = VT == MVT::i64;
  if (Opcode == ISD::ADD) {
    if (Imm == 1)
      return Is64 ? Bedrock::INCQ3r : Bedrock::INCL3r;
    if (Imm == -1)
      return Is64 ? Bedrock::DECQ3r : Bedrock::DECL3r;
  }
  if (Opcode == ISD::SUB) {
    if (Imm == 1)
      return Is64 ? Bedrock::DECQ3r : Bedrock::DECL3r;
    if (Imm == -1)
      return Is64 ? Bedrock::INCQ3r : Bedrock::INCL3r;
  }
  return 0;
}

static unsigned getNegPseudo(MVT VT) {
  return VT == MVT::i64 ? Bedrock::NEGQ3r : Bedrock::NEGL3r;
}

static unsigned getAbsPseudo(MVT VT) {
  return VT == MVT::i64 ? Bedrock::ABSQ3r : Bedrock::ABSL3r;
}

static unsigned getNotPseudo(MVT VT) {
  return VT == MVT::i64 ? Bedrock::NOTQ3r : Bedrock::NOTL3r;
}

static unsigned getSignExtendInRegPseudo(MVT VT, MVT ExtVT) {
  if (VT == MVT::i32) {
    switch (ExtVT.SimpleTy) {
    case MVT::i8:
      return Bedrock::EXTSLBrr;
    case MVT::i16:
      return Bedrock::EXTSLWrr;
    default:
      return 0;
    }
  }

  if (VT == MVT::i64) {
    switch (ExtVT.SimpleTy) {
    case MVT::i8:
      return Bedrock::EXTSQBrr;
    case MVT::i16:
      return Bedrock::EXTSQWrr;
    case MVT::i32:
      return Bedrock::EXTSQLrr;
    default:
      return 0;
    }
  }

  return 0;
}

static unsigned getZeroExtendMaskPseudo(MVT VT, const APInt &Mask) {
  if (VT == MVT::i32) {
    if (Mask == UINT64_C(0xff))
      return Bedrock::EXTZLBrr;
    if (Mask == UINT64_C(0xffff))
      return Bedrock::EXTZLWrr;
    return 0;
  }

  if (VT == MVT::i64) {
    if (Mask == UINT64_C(0xff))
      return Bedrock::EXTZQBrr;
    if (Mask == UINT64_C(0xffff))
      return Bedrock::EXTZQWrr;
    if (Mask == UINT64_C(0xffffffff))
      return Bedrock::EXTZQLrr;
    return 0;
  }

  return 0;
}

static bool selectZeroExtendMask(SelectionDAG *DAG, SDNode *N, SDLoc DL,
                                 MVT VT) {
  if (N->getOpcode() != ISD::AND)
    return false;

  SDValue Value;
  const ConstantSDNode *Mask = nullptr;
  if ((Mask = dyn_cast<ConstantSDNode>(N->getOperand(1))))
    Value = N->getOperand(0);
  else if ((Mask = dyn_cast<ConstantSDNode>(N->getOperand(0))))
    Value = N->getOperand(1);
  else
    return false;

  unsigned Opc = getZeroExtendMaskPseudo(VT, Mask->getAPIntValue());
  if (!Opc)
    return false;

  SDValue Ops[] = {Value};
  DAG->SelectNodeTo(N, Opc, VT, Ops);
  return true;
}

static bool isCommutativeImmOpcode(unsigned Opcode) {
  switch (Opcode) {
  case ISD::ADD:
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
    return true;
  default:
    return false;
  }
}

static unsigned getMinMaxImmPseudo(unsigned Opcode, MVT VT) {
  bool Is64 = VT == MVT::i64;
  switch (Opcode) {
  case BedrockISD::SMAX:
    return Is64 ? Bedrock::MAXSQ3ri : Bedrock::MAXSL3ri;
  case BedrockISD::SMIN:
    return Is64 ? Bedrock::MINSQ3ri : Bedrock::MINSL3ri;
  case BedrockISD::UMAX:
    return Is64 ? Bedrock::MAXUQ3ri : Bedrock::MAXUL3ri;
  case BedrockISD::UMIN:
    return Is64 ? Bedrock::MINUQ3ri : Bedrock::MINUL3ri;
  default:
    llvm_unreachable("unexpected Bedrock min/max immediate pseudo");
  }
}

static bool isBedrockMinMaxOpcode(unsigned Opcode) {
  switch (Opcode) {
  case BedrockISD::SMAX:
  case BedrockISD::SMIN:
  case BedrockISD::UMAX:
  case BedrockISD::UMIN:
    return true;
  default:
    return false;
  }
}

static bool selectMinMaxImmediate(SelectionDAG *DAG, SDNode *N, SDLoc DL,
                                  MVT VT) {
  unsigned Opcode = N->getOpcode();
  if (!isBedrockMinMaxOpcode(Opcode))
    return false;

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  if (auto *CN = dyn_cast<ConstantSDNode>(RHS)) {
    SDValue Ops[] = {LHS, DAG->getTargetConstant(CN->getSExtValue(), DL,
                                                 MVT::i64)};
    DAG->SelectNodeTo(N, getMinMaxImmPseudo(Opcode, VT), VT, Ops);
    return true;
  }
  if (auto *CN = dyn_cast<ConstantSDNode>(LHS)) {
    SDValue Ops[] = {RHS, DAG->getTargetConstant(CN->getSExtValue(), DL,
                                                 MVT::i64)};
    DAG->SelectNodeTo(N, getMinMaxImmPseudo(Opcode, VT), VT, Ops);
    return true;
  }
  return false;
}

static bool isShiftOrRotateOpcode(unsigned Opcode) {
  switch (Opcode) {
  case ISD::SHL:
  case ISD::ROTL:
  case ISD::ROTR:
  case ISD::SRL:
  case ISD::SRA:
    return true;
  default:
    return false;
  }
}

static bool selectBitModifyImmediate(SelectionDAG *DAG, SDNode *N, SDLoc DL,
                                     MVT VT, SDValue Value,
                                     const ConstantSDNode *CN) {
  const APInt &Imm = CN->getAPIntValue();
  APInt BitMask = N->getOpcode() == ISD::AND ? ~Imm : Imm;
  if (BitMask.isPowerOf2() &&
      (N->getOpcode() == ISD::OR || N->getOpcode() == ISD::AND ||
       N->getOpcode() == ISD::XOR)) {
    unsigned Bit = BitMask.countr_zero();
    if (!isUInt<6>(Bit))
      return false;

    bool Is64 = VT == MVT::i64;
    unsigned Pseudo;
    switch (N->getOpcode()) {
    case ISD::OR:
      Pseudo = Is64 ? Bedrock::BSETQ3ri : Bedrock::BSETL3ri;
      break;
    case ISD::AND:
      Pseudo = Is64 ? Bedrock::BCLRQ3ri : Bedrock::BCLRL3ri;
      break;
    case ISD::XOR:
      Pseudo = Is64 ? Bedrock::BCHGQ3ri : Bedrock::BCHGL3ri;
      break;
    default:
      llvm_unreachable("unexpected Bedrock bit modification opcode");
    }
    SDValue Ops[] = {Value, DAG->getTargetConstant(Bit, DL, MVT::i64)};
    DAG->SelectNodeTo(N, Pseudo, VT, Ops);
    return true;
  }

  if (N->getOpcode() != ISD::OR || VT != MVT::i64 || Imm.popcount() != 2 ||
      isInt<32>(CN->getSExtValue()))
    return false;

  APInt Remaining = Imm;
  unsigned Bit0 = Remaining.countr_zero();
  Remaining.clearBit(Bit0);
  unsigned Bit1 = Remaining.countr_zero();
  if (!isUInt<6>(Bit0) || !isUInt<6>(Bit1))
    return false;

  SDValue Ops[] = {Value, DAG->getTargetConstant(Bit0, DL, MVT::i64),
                   DAG->getTargetConstant(Bit1, DL, MVT::i64)};
  DAG->SelectNodeTo(N, Bedrock::BSET2Q3ri, VT, Ops);
  return true;
}

static bool selectBinaryImmediate(SelectionDAG *DAG, SDNode *N, SDLoc DL,
                                  MVT VT) {
  unsigned Opcode = N->getOpcode();
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  if (auto *CN = dyn_cast<ConstantSDNode>(RHS)) {
    if (selectBitModifyImmediate(DAG, N, DL, VT, LHS, CN))
      return true;

    int64_t Imm = CN->getSExtValue();
    if (isShiftOrRotateOpcode(Opcode) && !isUInt<6>(Imm))
      return false;

    SDValue Ops[] = {LHS, DAG->getTargetConstant(Imm, DL, MVT::i64)};
    DAG->SelectNodeTo(N, getBinaryImmPseudo(Opcode, VT), VT, Ops);
    return true;
  }

  if (!isCommutativeImmOpcode(Opcode))
    return false;

  if (auto *CN = dyn_cast<ConstantSDNode>(LHS)) {
    if (selectBitModifyImmediate(DAG, N, DL, VT, RHS, CN))
      return true;

    int64_t Imm = CN->getSExtValue();
    SDValue Ops[] = {RHS, DAG->getTargetConstant(Imm, DL, MVT::i64)};
    DAG->SelectNodeTo(N, getBinaryImmPseudo(Opcode, VT), VT, Ops);
    return true;
  }

  return false;
}

static bool selectFrameAddress(SelectionDAG *DAG, SDValue Addr, SDLoc DL,
                               SDValue &FrameIndex, int64_t &Offset) {
  Offset = 0;

  auto SelectFrameIndex = [&](SDValue FI) -> bool {
    if (FI.getOpcode() != ISD::FrameIndex)
      return false;
    auto *FIN = dyn_cast<FrameIndexSDNode>(FI);
    if (!FIN)
      return false;
    FrameIndex = DAG->getTargetFrameIndex(FIN->getIndex(), MVT::i64);
    return true;
  };

  if (SelectFrameIndex(Addr))
    return true;

  if (Addr.getOpcode() != ISD::ADD)
    return false;

  SDValue LHS = Addr.getOperand(0);
  SDValue RHS = Addr.getOperand(1);
  if (auto *CN = dyn_cast<ConstantSDNode>(RHS)) {
    if (!SelectFrameIndex(LHS))
      return false;
    Offset = CN->getSExtValue();
    return true;
  }
  if (auto *CN = dyn_cast<ConstantSDNode>(LHS)) {
    if (!SelectFrameIndex(RHS))
      return false;
    Offset = CN->getSExtValue();
    return true;
  }

  return false;
}

static bool isMaterializedAddressBase(SDValue Addr) {
  switch (Addr.getOpcode()) {
  case ISD::FrameIndex:
  case ISD::GlobalAddress:
  case ISD::ExternalSymbol:
  case ISD::BlockAddress:
  case ISD::ConstantPool:
  case ISD::JumpTable:
    return true;
  default:
    return false;
  }
}

static bool selectRegOffsetAddress(SDValue Addr, SDValue &Base,
                                   int64_t &Offset) {
  if (Addr.getOpcode() != ISD::ADD)
    return false;

  SDValue LHS = Addr.getOperand(0);
  SDValue RHS = Addr.getOperand(1);
  if (auto *CN = dyn_cast<ConstantSDNode>(RHS)) {
    if (isMaterializedAddressBase(LHS))
      return false;
    Base = LHS;
    Offset = CN->getSExtValue();
    return true;
  }
  if (auto *CN = dyn_cast<ConstantSDNode>(LHS)) {
    if (isMaterializedAddressBase(RHS))
      return false;
    Base = RHS;
    Offset = CN->getSExtValue();
    return true;
  }

  return false;
}

static bool selectRegOffsetLEA(SelectionDAG *DAG, SDNode *N, SDLoc DL) {
  if (N->getOpcode() != ISD::ADD || N->getValueType(0) != MVT::i64)
    return false;

  SDValue Base;
  int64_t Offset = 0;
  if (!selectRegOffsetAddress(SDValue(N, 0), Base, Offset))
    return false;

  SDValue Ops[] = {Base, DAG->getTargetConstant(Offset, DL, MVT::i64)};
  DAG->SelectNodeTo(N, Bedrock::LEAro, MVT::i64, Ops);
  return true;
}

static bool selectScaledIndexLEA(SelectionDAG *DAG, SDNode *N, SDLoc DL) {
  if (N->getOpcode() != ISD::ADD || N->getValueType(0) != MVT::i64)
    return false;

  SDValue Base = N->getOperand(0);
  SDValue ScaledIndex = N->getOperand(1);
  if (Base.getOpcode() == ISD::SHL)
    std::swap(Base, ScaledIndex);
  if (ScaledIndex.getOpcode() != ISD::SHL)
    return false;
  if (isMaterializedAddressBase(Base))
    return false;

  auto *Scale = dyn_cast<ConstantSDNode>(ScaledIndex.getOperand(1));
  if (!Scale)
    return false;
  uint64_t ScaleAmount = Scale->getZExtValue();
  if (ScaleAmount < 1 || ScaleAmount > 3)
    return false;

  // Keep a shared scale explicit. Folding each of its address users would
  // duplicate the scale in multiple LEAs and can also hide an indexed memory
  // operand from the late folder.
  if (!ScaledIndex.hasOneUse())
    return false;

  // Leave a single-use load address in its decomposed form. The late memory
  // folder can then encode the same scale directly in the load, avoiding an
  // otherwise redundant address-producing LEA.
  if (N->hasOneUse() && N->use_begin()->getUser()->getOpcode() == ISD::LOAD)
    return false;

  SDValue Ops[] = {
      Base,
      ScaledIndex.getOperand(0),
      DAG->getTargetConstant(ScaleAmount, DL, MVT::i64),
  };
  DAG->SelectNodeTo(N, Bedrock::LEArx, MVT::i64, Ops);
  return true;
}

static unsigned getSymbolAddressFlag(const SelectionDAG &DAG,
                                     const GlobalValue *GV);

static bool selectSymbolAddress(SelectionDAG *DAG, SDValue Addr, SDLoc DL,
                                SDValue &Target) {
  if (Addr.getOpcode() == ISD::ADD) {
    SDValue Base;
    int64_t Offset = 0;
    SDValue LHS = Addr.getOperand(0);
    SDValue RHS = Addr.getOperand(1);
    if (auto *CN = dyn_cast<ConstantSDNode>(RHS)) {
      Base = LHS;
      Offset = CN->getSExtValue();
    } else if (auto *CN = dyn_cast<ConstantSDNode>(LHS)) {
      Base = RHS;
      Offset = CN->getSExtValue();
    }

    if (Base) {
      if (auto *GA = dyn_cast<GlobalAddressSDNode>(Base)) {
        Target = DAG->getTargetGlobalAddress(GA->getGlobal(), DL, MVT::i64,
                                             GA->getOffset() + Offset,
                                             getSymbolAddressFlag(
                                                 *DAG, GA->getGlobal()));
        return true;
      }
    }
  }

  if (auto *GA = dyn_cast<GlobalAddressSDNode>(Addr)) {
    Target = DAG->getTargetGlobalAddress(GA->getGlobal(), DL, MVT::i64,
                                         GA->getOffset(),
                                         getSymbolAddressFlag(
                                             *DAG, GA->getGlobal()));
    return true;
  }
  if (auto *ES = dyn_cast<ExternalSymbolSDNode>(Addr)) {
    Target = DAG->getTargetExternalSymbol(ES->getSymbol(), MVT::i64);
    return true;
  }
  if (auto *BA = dyn_cast<BlockAddressSDNode>(Addr)) {
    Target = DAG->getTargetBlockAddress(BA->getBlockAddress(), MVT::i64,
                                        BA->getOffset());
    return true;
  }
  if (auto *CP = dyn_cast<ConstantPoolSDNode>(Addr)) {
    if (CP->isMachineConstantPoolEntry())
      Target = DAG->getTargetConstantPool(CP->getMachineCPVal(), MVT::i64,
                                          CP->getAlign(), CP->getOffset());
    else
      Target = DAG->getTargetConstantPool(CP->getConstVal(), MVT::i64,
                                          CP->getAlign(), CP->getOffset());
    return true;
  }
  if (auto *JT = dyn_cast<JumpTableSDNode>(Addr)) {
    Target = DAG->getTargetJumpTable(JT->getIndex(), MVT::i64);
    return true;
  }
  return false;
}

static unsigned getSymbolAddressFlag(const SelectionDAG &DAG,
                                     const GlobalValue *GV) {
  const TargetMachine &TM = DAG.getTarget();
  CodeModel::Model CM = TM.getCodeModel();
  bool IsPIC = TM.isPositionIndependent();
  bool IsLocal = GV && GV->isDSOLocal();
  bool IsFunction = GV && GV->getValueType()->isFunctionTy();

  if (IsPIC) {
    if (!IsLocal)
      return CM == CodeModel::Large ? BedrockII::MO_GOTPCREL64
                                    : BedrockII::MO_GOTPCREL32;
    if (CM == CodeModel::Medium && !IsFunction)
      return BedrockII::MO_GOTPCREL32;
    return CM == CodeModel::Large ? BedrockII::MO_PCREL64
                                  : BedrockII::MO_PCREL32;
  }

  if (CM == CodeModel::Medium)
    return IsFunction ? BedrockII::MO_PCREL32 : BedrockII::MO_ABS64;
  if (CM == CodeModel::Large)
    return BedrockII::MO_ABS64;
  if (CM == CodeModel::Kernel)
    return IsLocal ? BedrockII::MO_PCREL32 : BedrockII::MO_ABS64;
  if (CM == CodeModel::Small)
    return BedrockII::MO_PCREL32;
  return BedrockII::MO_ABS32;
}

static unsigned getLocalAddressFlag(const SelectionDAG &DAG,
                                    bool IsCodeRelated) {
  const TargetMachine &TM = DAG.getTarget();
  CodeModel::Model CM = TM.getCodeModel();
  if (TM.isPositionIndependent())
    return CM == CodeModel::Large ? BedrockII::MO_PCREL64
                                  : BedrockII::MO_PCREL32;
  if (CM == CodeModel::Large || (CM == CodeModel::Medium && !IsCodeRelated))
    return BedrockII::MO_ABS64;
  if (CM == CodeModel::Small || CM == CodeModel::Kernel ||
      (CM == CodeModel::Medium && IsCodeRelated))
    return BedrockII::MO_PCREL32;
  return BedrockII::MO_ABS32;
}

static bool isDirectSymbolMemoryAddress(SDValue Target) {
  auto *GA = dyn_cast<GlobalAddressSDNode>(Target);
  if (!GA)
    return false;
  unsigned Flag = GA->getTargetFlags();
  return Flag == BedrockII::MO_ABS32 || Flag == BedrockII::MO_PCREL32;
}

static bool selectMaterializedSymbolAddress(SelectionDAG *DAG, SDNode *N,
                                            SDLoc DL, SDValue &Target) {
  switch (N->getOpcode()) {
  case ISD::GlobalAddress: {
    auto *GA = cast<GlobalAddressSDNode>(N);
    Target = DAG->getTargetGlobalAddress(
        GA->getGlobal(), DL, MVT::i64, GA->getOffset(),
        getSymbolAddressFlag(*DAG, GA->getGlobal()));
    return true;
  }
  case ISD::ExternalSymbol: {
    auto *ES = cast<ExternalSymbolSDNode>(N);
    CodeModel::Model CM = DAG->getTarget().getCodeModel();
    unsigned Flag = DAG->getTarget().isPositionIndependent()
                        ? (CM == CodeModel::Large
                               ? BedrockII::MO_GOTPCREL64
                               : BedrockII::MO_GOTPCREL32)
                        : (CM == CodeModel::Large || CM == CodeModel::Medium
                               ? BedrockII::MO_ABS64
                               : CM == CodeModel::Small
                                     ? BedrockII::MO_PCREL32
                                     : BedrockII::MO_ABS32);
    Target = DAG->getTargetExternalSymbol(ES->getSymbol(), MVT::i64, Flag);
    return true;
  }
  case ISD::BlockAddress: {
    auto *BA = cast<BlockAddressSDNode>(N);
    Target = DAG->getTargetBlockAddress(BA->getBlockAddress(), MVT::i64,
                                        BA->getOffset(),
                                        getLocalAddressFlag(*DAG, true));
    return true;
  }
  case ISD::ConstantPool: {
    auto *CP = cast<ConstantPoolSDNode>(N);
    if (CP->isMachineConstantPoolEntry())
      Target = DAG->getTargetConstantPool(CP->getMachineCPVal(), MVT::i64,
                                          CP->getAlign(), CP->getOffset(),
                                          getLocalAddressFlag(*DAG, false));
    else
      Target = DAG->getTargetConstantPool(CP->getConstVal(), MVT::i64,
                                          CP->getAlign(), CP->getOffset(),
                                          getLocalAddressFlag(*DAG, false));
    return true;
  }
  case ISD::JumpTable: {
    auto *JT = cast<JumpTableSDNode>(N);
    Target = DAG->getTargetJumpTable(JT->getIndex(), MVT::i64,
                                     getLocalAddressFlag(*DAG, true));
    return true;
  }
  case ISD::ADD: {
    SDValue LHS = N->getOperand(0);
    SDValue RHS = N->getOperand(1);
    auto *Offset = dyn_cast<ConstantSDNode>(RHS);
    auto *GA = dyn_cast<GlobalAddressSDNode>(LHS);
    if (!GA) {
      Offset = dyn_cast<ConstantSDNode>(LHS);
      GA = dyn_cast<GlobalAddressSDNode>(RHS);
    }
    if (!GA || !Offset)
      return false;
    Target = DAG->getTargetGlobalAddress(
        GA->getGlobal(), DL, MVT::i64,
        GA->getOffset() + Offset->getSExtValue(),
        getSymbolAddressFlag(*DAG, GA->getGlobal()));
    return true;
  }
  default:
    return false;
  }
}

static unsigned getLoadOpcode(const LoadSDNode *LD, bool IsFrame) {
  MVT MemVT = LD->getMemoryVT().getSimpleVT();
  ISD::LoadExtType ExtType = LD->getExtensionType();
  MVT VT = LD->getValueType(0).getSimpleVT();

  auto Pick = [&](unsigned RegOp, unsigned FrameOp) {
    return IsFrame ? FrameOp : RegOp;
  };

  if (ExtType == ISD::NON_EXTLOAD || ExtType == ISD::EXTLOAD) {
    if (VT == MVT::f32 && MemVT == MVT::f32)
      return Pick(Bedrock::FLOADSrr, Bedrock::FLOADSfi);
    if (VT == MVT::f64 && MemVT == MVT::f64)
      return Pick(Bedrock::FLOADDrr, Bedrock::FLOADDfi);

    switch (MemVT.SimpleTy) {
    case MVT::i8:
      return Pick(Bedrock::LOADB_Zrr, Bedrock::LOADB_Zfi);
    case MVT::i16:
      return Pick(Bedrock::LOADW_Zrr, Bedrock::LOADW_Zfi);
    case MVT::i32:
      return Pick(Bedrock::LOADLrr, Bedrock::LOADLfi);
    case MVT::i64:
      return Pick(Bedrock::LOADQrr, Bedrock::LOADQfi);
    default:
      report_fatal_error(Twine("unsupported Bedrock non-extending load memvt=") +
                         Twine(MemVT.SimpleTy) + " vt=" +
                         Twine(VT.SimpleTy));
    }
  }

  bool IsSigned = ExtType == ISD::SEXTLOAD;
  switch (MemVT.SimpleTy) {
  case MVT::i8:
    return IsSigned ? Pick(Bedrock::LOADB_Srr, Bedrock::LOADB_Sfi)
                    : Pick(Bedrock::LOADB_Zrr, Bedrock::LOADB_Zfi);
  case MVT::i16:
    return IsSigned ? Pick(Bedrock::LOADW_Srr, Bedrock::LOADW_Sfi)
                    : Pick(Bedrock::LOADW_Zrr, Bedrock::LOADW_Zfi);
  case MVT::i32:
    return IsSigned ? Pick(Bedrock::LOADL_Srr, Bedrock::LOADL_Sfi)
                    : Pick(Bedrock::LOADL_Zrr, Bedrock::LOADL_Zfi);
  default:
    report_fatal_error(Twine("unsupported Bedrock extending load memvt=") +
                       Twine(MemVT.SimpleTy) + " vt=" + Twine(VT.SimpleTy));
  }
}

static unsigned getLoadAbsOpcode(const LoadSDNode *LD) {
  switch (getLoadOpcode(LD, /*IsFrame=*/false)) {
  case Bedrock::LOADB_Zrr:
    return Bedrock::LOADB_Zabs;
  case Bedrock::LOADW_Zrr:
    return Bedrock::LOADW_Zabs;
  case Bedrock::LOADL_Zrr:
    return Bedrock::LOADL_Zabs;
  case Bedrock::LOADB_Srr:
    return Bedrock::LOADB_Sabs;
  case Bedrock::LOADW_Srr:
    return Bedrock::LOADW_Sabs;
  case Bedrock::LOADL_Srr:
    return Bedrock::LOADL_Sabs;
  case Bedrock::LOADLrr:
    return Bedrock::LOADLabs;
  case Bedrock::LOADQrr:
    return Bedrock::LOADQabs;
  case Bedrock::FLOADSrr:
    return Bedrock::FLOADSabs;
  case Bedrock::FLOADDrr:
    return Bedrock::FLOADDabs;
  default:
    llvm_unreachable("unexpected Bedrock load opcode");
  }
}

static unsigned getLoadOffsetOpcode(const LoadSDNode *LD) {
  switch (getLoadOpcode(LD, /*IsFrame=*/false)) {
  case Bedrock::LOADB_Zrr:
    return Bedrock::LOADB_Zro;
  case Bedrock::LOADW_Zrr:
    return Bedrock::LOADW_Zro;
  case Bedrock::LOADL_Zrr:
    return Bedrock::LOADL_Zro;
  case Bedrock::LOADB_Srr:
    return Bedrock::LOADB_Sro;
  case Bedrock::LOADW_Srr:
    return Bedrock::LOADW_Sro;
  case Bedrock::LOADL_Srr:
    return Bedrock::LOADL_Sro;
  case Bedrock::LOADLrr:
    return Bedrock::LOADLro;
  case Bedrock::LOADQrr:
    return Bedrock::LOADQro;
  case Bedrock::FLOADSrr:
    return Bedrock::FLOADSro;
  case Bedrock::FLOADDrr:
    return Bedrock::FLOADDro;
  default:
    llvm_unreachable("unexpected Bedrock load opcode");
  }
}

static unsigned getStoreOpcode(const StoreSDNode *ST, bool IsFrame) {
  MVT MemVT = ST->getMemoryVT().getSimpleVT();
  auto Pick = [&](unsigned RegOp, unsigned FrameOp) {
    return IsFrame ? FrameOp : RegOp;
  };

  switch (MemVT.SimpleTy) {
  case MVT::i8:
    return Pick(Bedrock::STOREBrr, Bedrock::STOREBfi);
  case MVT::i16:
    return Pick(Bedrock::STOREWrr, Bedrock::STOREWfi);
  case MVT::i32:
    return Pick(Bedrock::STORELrr, Bedrock::STORELfi);
  case MVT::i64:
    return Pick(Bedrock::STOREQrr, Bedrock::STOREQfi);
  case MVT::f32:
    return Pick(Bedrock::FSTORESrr, Bedrock::FSTORESfi);
  case MVT::f64:
    return Pick(Bedrock::FSTOREDrr, Bedrock::FSTOREDfi);
  default:
    report_fatal_error("unsupported Bedrock store");
  }
}

static bool selectStoreImmediate(const StoreSDNode *ST, int64_t &Imm) {
  auto *CN = dyn_cast<ConstantSDNode>(ST->getValue());
  if (!CN)
    return false;

  switch (ST->getMemoryVT().getSimpleVT().SimpleTy) {
  case MVT::i8:
  case MVT::i16:
  case MVT::i32:
  case MVT::i64:
    break;
  default:
    return false;
  }

  Imm = CN->getSExtValue();
  return Imm != 0;
}

static unsigned getStoreImmOpcode(const StoreSDNode *ST, bool IsFrame) {
  MVT MemVT = ST->getMemoryVT().getSimpleVT();
  auto Pick = [&](unsigned RegOp, unsigned FrameOp) {
    return IsFrame ? FrameOp : RegOp;
  };

  switch (MemVT.SimpleTy) {
  case MVT::i8:
    return Pick(Bedrock::STOREB_Immrr, Bedrock::STOREB_Immfi);
  case MVT::i16:
    return Pick(Bedrock::STOREW_Immrr, Bedrock::STOREW_Immfi);
  case MVT::i32:
    return Pick(Bedrock::STOREL_Immrr, Bedrock::STOREL_Immfi);
  case MVT::i64:
    return Pick(Bedrock::STOREQ_Immrr, Bedrock::STOREQ_Immfi);
  default:
    llvm_unreachable("unexpected Bedrock immediate store memvt");
  }
}

static unsigned getStoreOffsetOpcode(const StoreSDNode *ST) {
  switch (getStoreOpcode(ST, /*IsFrame=*/false)) {
  case Bedrock::STOREBrr:
    return Bedrock::STOREBro;
  case Bedrock::STOREWrr:
    return Bedrock::STOREWro;
  case Bedrock::STORELrr:
    return Bedrock::STORELro;
  case Bedrock::STOREQrr:
    return Bedrock::STOREQro;
  case Bedrock::FSTORESrr:
    return Bedrock::FSTORESro;
  case Bedrock::FSTOREDrr:
    return Bedrock::FSTOREDro;
  default:
    llvm_unreachable("unexpected Bedrock store opcode");
  }
}

static unsigned getStoreImmOffsetOpcode(const StoreSDNode *ST) {
  switch (ST->getMemoryVT().getSimpleVT().SimpleTy) {
  case MVT::i8:
    return Bedrock::STOREB_Immro;
  case MVT::i16:
    return Bedrock::STOREW_Immro;
  case MVT::i32:
    return Bedrock::STOREL_Immro;
  case MVT::i64:
    return Bedrock::STOREQ_Immro;
  default:
    llvm_unreachable("unexpected Bedrock immediate offset store memvt");
  }
}

static unsigned getStoreImmAbsOpcode(const StoreSDNode *ST) {
  switch (ST->getMemoryVT().getSimpleVT().SimpleTy) {
  case MVT::i8:
    return Bedrock::STOREB_Immabs;
  case MVT::i16:
    return Bedrock::STOREW_Immabs;
  case MVT::i32:
    return Bedrock::STOREL_Immabs;
  case MVT::i64:
    return Bedrock::STOREQ_Immabs;
  default:
    llvm_unreachable("unexpected Bedrock absolute immediate store memvt");
  }
}

static unsigned getStoreAbsOpcode(const StoreSDNode *ST) {
  switch (getStoreOpcode(ST, /*IsFrame=*/false)) {
  case Bedrock::STOREBrr:
    return Bedrock::STOREBabs;
  case Bedrock::STOREWrr:
    return Bedrock::STOREWabs;
  case Bedrock::STORELrr:
    return Bedrock::STORELabs;
  case Bedrock::STOREQrr:
    return Bedrock::STOREQabs;
  case Bedrock::FSTORESrr:
    return Bedrock::FSTORESabs;
  case Bedrock::FSTOREDrr:
    return Bedrock::FSTOREDabs;
  default:
    llvm_unreachable("unexpected Bedrock store opcode");
  }
}

static bool selectCompareImmediate(SelectionDAG *DAG, SDNode *N, SDLoc DL) {
  if (N->getOpcode() != BedrockISD::CMP)
    return false;

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  auto *CN = dyn_cast<ConstantSDNode>(RHS);
  if (!CN)
    return false;

  // Keep an increment-and-bound comparison in register form so the late
  // printer can select IJcc.  The constant materialization is loop invariant
  // and MachineLICM can hoist it out of the loop.
  auto IsUnitIncrement = [](SDValue V) {
    if (V.getOpcode() != ISD::ADD)
      return false;
    auto IsOne = [](SDValue Op) {
      auto *C = dyn_cast<ConstantSDNode>(Op);
      return C && C->isOne();
    };
    return IsOne(V.getOperand(0)) || IsOne(V.getOperand(1));
  };
  if (IsUnitIncrement(LHS))
    return false;

  MVT VT = LHS.getSimpleValueType();
  if (VT != MVT::i32 && VT != MVT::i64)
    return false;

  SDValue Ops[] = {LHS,
                   DAG->getTargetConstant(CN->getSExtValue(), DL, MVT::i64)};
  DAG->SelectNodeTo(N, VT == MVT::i64 ? Bedrock::CMPQri : Bedrock::CMPLri,
                    MVT::Glue, Ops);
  return true;
}

static void selectMemoryNode(SelectionDAG *DAG, SDNode *N, unsigned Opcode,
                             ArrayRef<SDValue> Ops,
                             MachineMemOperand *MemOperand) {
  SDNode *Selected = DAG->SelectNodeTo(N, Opcode, N->getVTList(), Ops);
  DAG->setNodeMemRefs(cast<MachineSDNode>(Selected), {MemOperand});
}

void BedrockDAGToDAGISel::Select(SDNode *N) {
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;
  }

  SDLoc DL(N);
  if (N->getOpcode() == BedrockISD::MEMSET) {
    SDValue Ops[] = {N->getOperand(1), N->getOperand(2), N->getOperand(3),
                     N->getOperand(0)};
    CurDAG->SelectNodeTo(N, Bedrock::REP_MEMSETB,
                         CurDAG->getVTList(MVT::i64, MVT::i64, MVT::Other),
                         Ops);
    return;
  }
  if (selectCompareImmediate(CurDAG, N, DL))
    return;
  if (N->getNumValues() > 0) {
    EVT VT = N->getValueType(0);
    if ((VT == MVT::i32 || VT == MVT::i64) &&
        selectMinMaxImmediate(CurDAG, N, DL, VT.getSimpleVT()))
      return;
  }

  if (auto *LD = dyn_cast<LoadSDNode>(N)) {
    SDValue FrameIndex;
    int64_t Offset = 0;
    bool IsFrame =
        selectFrameAddress(CurDAG, LD->getBasePtr(), DL, FrameIndex, Offset);
    if (IsFrame) {
      SDValue Ops[] = {
          FrameIndex,
          CurDAG->getTargetConstant(Offset, DL, MVT::i64),
          LD->getChain(),
      };
      selectMemoryNode(CurDAG, N, getLoadOpcode(LD, /*IsFrame=*/true), Ops,
                       LD->getMemOperand());
      return;
    }

    SDValue Target;
    if (selectSymbolAddress(CurDAG, LD->getBasePtr(), DL, Target) &&
        isDirectSymbolMemoryAddress(Target)) {
      SDValue Ops[] = {Target, LD->getChain()};
      selectMemoryNode(CurDAG, N, getLoadAbsOpcode(LD), Ops,
                       LD->getMemOperand());
      return;
    }

    SDValue Base;
    Offset = 0;
    if (selectRegOffsetAddress(LD->getBasePtr(), Base, Offset)) {
      SDValue Ops[] = {
          Base,
          CurDAG->getTargetConstant(Offset, DL, MVT::i64),
          LD->getChain(),
      };
      selectMemoryNode(CurDAG, N, getLoadOffsetOpcode(LD), Ops,
                       LD->getMemOperand());
      return;
    }

    SDValue Ops[] = {LD->getBasePtr(), LD->getChain()};
    selectMemoryNode(CurDAG, N, getLoadOpcode(LD, /*IsFrame=*/false), Ops,
                     LD->getMemOperand());
    return;
  }

  if (auto *ST = dyn_cast<StoreSDNode>(N)) {
    int64_t StoreImm = 0;
    bool HasStoreImm = selectStoreImmediate(ST, StoreImm);

    SDValue FrameIndex;
    int64_t Offset = 0;
    bool IsFrame =
        selectFrameAddress(CurDAG, ST->getBasePtr(), DL, FrameIndex, Offset);
    if (IsFrame) {
      if (HasStoreImm) {
        SDValue Ops[] = {
            CurDAG->getTargetConstant(StoreImm, DL, MVT::i64),
            FrameIndex,
            CurDAG->getTargetConstant(Offset, DL, MVT::i64),
            ST->getChain(),
        };
        selectMemoryNode(CurDAG, N,
                         getStoreImmOpcode(ST, /*IsFrame=*/true), Ops,
                         ST->getMemOperand());
        return;
      }

      SDValue Ops[] = {
          ST->getValue(),
          FrameIndex,
          CurDAG->getTargetConstant(Offset, DL, MVT::i64),
          ST->getChain(),
      };
      selectMemoryNode(CurDAG, N, getStoreOpcode(ST, /*IsFrame=*/true), Ops,
                       ST->getMemOperand());
      return;
    }

    SDValue Target;
    if (selectSymbolAddress(CurDAG, ST->getBasePtr(), DL, Target) &&
        isDirectSymbolMemoryAddress(Target)) {
      if (HasStoreImm) {
        SDValue Ops[] = {
            CurDAG->getTargetConstant(StoreImm, DL, MVT::i64),
            Target,
            ST->getChain(),
        };
        selectMemoryNode(CurDAG, N, getStoreImmAbsOpcode(ST), Ops,
                         ST->getMemOperand());
        return;
      }

      SDValue Ops[] = {ST->getValue(), Target, ST->getChain()};
      selectMemoryNode(CurDAG, N, getStoreAbsOpcode(ST), Ops,
                       ST->getMemOperand());
      return;
    }

    SDValue Base;
    Offset = 0;
    if (selectRegOffsetAddress(ST->getBasePtr(), Base, Offset)) {
      if (HasStoreImm) {
        SDValue Ops[] = {
            CurDAG->getTargetConstant(StoreImm, DL, MVT::i64),
            Base,
            CurDAG->getTargetConstant(Offset, DL, MVT::i64),
            ST->getChain(),
        };
        selectMemoryNode(CurDAG, N, getStoreImmOffsetOpcode(ST), Ops,
                         ST->getMemOperand());
        return;
      }

      SDValue Ops[] = {
          ST->getValue(),
          Base,
          CurDAG->getTargetConstant(Offset, DL, MVT::i64),
          ST->getChain(),
      };
      selectMemoryNode(CurDAG, N, getStoreOffsetOpcode(ST), Ops,
                       ST->getMemOperand());
      return;
    }

    if (HasStoreImm) {
      SDValue Ops[] = {
          CurDAG->getTargetConstant(StoreImm, DL, MVT::i64),
          ST->getBasePtr(),
          ST->getChain(),
      };
      selectMemoryNode(CurDAG, N,
                       getStoreImmOpcode(ST, /*IsFrame=*/false), Ops,
                       ST->getMemOperand());
      return;
    }

    SDValue Ops[] = {ST->getValue(), ST->getBasePtr(), ST->getChain()};
    selectMemoryNode(CurDAG, N, getStoreOpcode(ST, /*IsFrame=*/false), Ops,
                     ST->getMemOperand());
    return;
  }

  EVT VT = N->getValueType(0);
  if (VT == MVT::i64) {
    SDValue Target;
    if (selectMaterializedSymbolAddress(CurDAG, N, DL, Target)) {
      SDValue Ops[] = {Target};
      CurDAG->SelectNodeTo(N, Bedrock::CONST64, MVT::i64, Ops);
      return;
    }

    SDValue FrameIndex;
    int64_t Offset = 0;
    if (selectFrameAddress(CurDAG, SDValue(N, 0), DL, FrameIndex, Offset)) {
      SDValue Ops[] = {
          FrameIndex,
          CurDAG->getTargetConstant(Offset, DL, MVT::i64),
      };
      ReplaceNode(N, CurDAG->getMachineNode(Bedrock::LEAfi, DL, MVT::i64, Ops));
      return;
    }
  }

  if (VT == MVT::i32 || VT == MVT::i64) {
    if (N->getOpcode() == ISD::ABS) {
      SDValue Ops[] = {N->getOperand(0)};
      CurDAG->SelectNodeTo(N, getAbsPseudo(VT.getSimpleVT()), VT, Ops);
      return;
    }

    if (N->getOpcode() == ISD::SUB && isNullConstant(N->getOperand(0))) {
      SDValue Ops[] = {N->getOperand(1)};
      CurDAG->SelectNodeTo(N, getNegPseudo(VT.getSimpleVT()), VT, Ops);
      return;
    }

    if (N->getOpcode() == ISD::XOR) {
      if (isAllOnesConstant(N->getOperand(0))) {
        SDValue Ops[] = {N->getOperand(1)};
        CurDAG->SelectNodeTo(N, getNotPseudo(VT.getSimpleVT()), VT, Ops);
        return;
      }
      if (isAllOnesConstant(N->getOperand(1))) {
        SDValue Ops[] = {N->getOperand(0)};
        CurDAG->SelectNodeTo(N, getNotPseudo(VT.getSimpleVT()), VT, Ops);
        return;
      }
    }

    if (N->getOpcode() == ISD::SIGN_EXTEND_INREG) {
      auto *ExtVT = dyn_cast<VTSDNode>(N->getOperand(1));
      if (!ExtVT)
        report_fatal_error("Bedrock expected SIGN_EXTEND_INREG value type");
      if (unsigned Opc = getSignExtendInRegPseudo(
              VT.getSimpleVT(), ExtVT->getVT().getSimpleVT())) {
        SDValue Ops[] = {N->getOperand(0)};
        CurDAG->SelectNodeTo(N, Opc, VT, Ops);
        return;
      }
    }

    if (selectZeroExtendMask(CurDAG, N, DL, VT.getSimpleVT()))
      return;

    if (N->getOpcode() == ISD::ADD || N->getOpcode() == ISD::SUB) {
      SDValue LHS = N->getOperand(0);
      SDValue RHS = N->getOperand(1);
      if (auto *CN = dyn_cast<ConstantSDNode>(RHS)) {
        if (unsigned Opc = getIncDecPseudo(N->getOpcode(), VT.getSimpleVT(),
                                           CN->getSExtValue())) {
          SDValue Ops[] = {LHS};
          CurDAG->SelectNodeTo(N, Opc, VT, Ops);
          return;
        }
      }
      if (N->getOpcode() == ISD::ADD) {
        if (auto *CN = dyn_cast<ConstantSDNode>(LHS)) {
          if (unsigned Opc = getIncDecPseudo(N->getOpcode(), VT.getSimpleVT(),
                                             CN->getSExtValue())) {
            SDValue Ops[] = {RHS};
            CurDAG->SelectNodeTo(N, Opc, VT, Ops);
            return;
          }
        }
      }
    }

    if (selectScaledIndexLEA(CurDAG, N, DL))
      return;

    if (selectRegOffsetLEA(CurDAG, N, DL))
      return;

    switch (N->getOpcode()) {
    case ISD::ADD:
    case ISD::SUB:
    case ISD::AND:
    case ISD::OR:
    case ISD::XOR:
    case ISD::SHL:
    case ISD::ROTL:
    case ISD::ROTR:
    case ISD::SRL:
    case ISD::SRA:
    case ISD::SDIV:
      if (selectBinaryImmediate(CurDAG, N, DL, VT.getSimpleVT()))
        return;
      break;
    default:
      break;
    }

    switch (N->getOpcode()) {
    case ISD::ADD:
    case ISD::SUB:
    case ISD::AND:
    case ISD::OR:
    case ISD::XOR:
    case ISD::SHL:
    case ISD::ROTL:
    case ISD::ROTR:
    case ISD::SRL:
    case ISD::SRA:
    case ISD::MUL:
    case ISD::UDIV:
    case ISD::SDIV:
    case ISD::UREM:
    case ISD::SREM: {
      SDValue Ops[] = {N->getOperand(0), N->getOperand(1)};
      CurDAG->SelectNodeTo(N, getBinaryPseudo(N->getOpcode(), VT.getSimpleVT()),
                           VT, Ops);
      return;
    }
    default:
      break;
    }
  }

  SelectCode(N);
}
