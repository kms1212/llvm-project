//===-- BedrockISelDAGToDAG.cpp - Bedrock DAG instruction selector --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Bedrock.h"
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
public:
  BedrockDAGToDAGISel() = delete;
  BedrockDAGToDAGISel(BedrockTargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

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

static bool selectSymbolAddress(SelectionDAG *DAG, SDValue Addr, SDLoc DL,
                                SDValue &Target) {
  if (auto *GA = dyn_cast<GlobalAddressSDNode>(Addr)) {
    Target = DAG->getTargetGlobalAddress(GA->getGlobal(), DL, MVT::i64,
                                         GA->getOffset());
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

static bool selectMaterializedSymbolAddress(SelectionDAG *DAG, SDNode *N,
                                            SDLoc DL, SDValue &Target) {
  switch (N->getOpcode()) {
  case ISD::GlobalAddress: {
    auto *GA = cast<GlobalAddressSDNode>(N);
    Target = DAG->getTargetGlobalAddress(GA->getGlobal(), DL, MVT::i64,
                                         GA->getOffset());
    return true;
  }
  case ISD::ExternalSymbol: {
    auto *ES = cast<ExternalSymbolSDNode>(N);
    Target = DAG->getTargetExternalSymbol(ES->getSymbol(), MVT::i64);
    return true;
  }
  case ISD::BlockAddress: {
    auto *BA = cast<BlockAddressSDNode>(N);
    Target = DAG->getTargetBlockAddress(BA->getBlockAddress(), MVT::i64,
                                        BA->getOffset());
    return true;
  }
  case ISD::ConstantPool: {
    auto *CP = cast<ConstantPoolSDNode>(N);
    if (CP->isMachineConstantPoolEntry())
      Target = DAG->getTargetConstantPool(CP->getMachineCPVal(), MVT::i64,
                                          CP->getAlign(), CP->getOffset());
    else
      Target = DAG->getTargetConstantPool(CP->getConstVal(), MVT::i64,
                                          CP->getAlign(), CP->getOffset());
    return true;
  }
  case ISD::JumpTable: {
    auto *JT = cast<JumpTableSDNode>(N);
    Target = DAG->getTargetJumpTable(JT->getIndex(), MVT::i64);
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
  case Bedrock::FLOADDrr:
    return Bedrock::FLOADDabs;
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
  case MVT::f64:
    return Pick(Bedrock::FSTOREDrr, Bedrock::FSTOREDfi);
  default:
    report_fatal_error("unsupported Bedrock store");
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
  case Bedrock::FSTOREDrr:
    return Bedrock::FSTOREDabs;
  default:
    llvm_unreachable("unexpected Bedrock store opcode");
  }
}

void BedrockDAGToDAGISel::Select(SDNode *N) {
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;
  }

  SDLoc DL(N);
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
      CurDAG->SelectNodeTo(N, getLoadOpcode(LD, /*IsFrame=*/true),
                           LD->getValueType(0), MVT::Other, Ops);
      return;
    }

    SDValue Target;
    if (selectSymbolAddress(CurDAG, LD->getBasePtr(), DL, Target)) {
      SDValue Ops[] = {Target, LD->getChain()};
      CurDAG->SelectNodeTo(N, getLoadAbsOpcode(LD), LD->getValueType(0),
                           MVT::Other, Ops);
      return;
    }

    SDValue Ops[] = {LD->getBasePtr(), LD->getChain()};
    CurDAG->SelectNodeTo(N, getLoadOpcode(LD, /*IsFrame=*/false),
                         LD->getValueType(0), MVT::Other, Ops);
    return;
  }

  if (auto *ST = dyn_cast<StoreSDNode>(N)) {
    SDValue FrameIndex;
    int64_t Offset = 0;
    bool IsFrame =
        selectFrameAddress(CurDAG, ST->getBasePtr(), DL, FrameIndex, Offset);
    if (IsFrame) {
      SDValue Ops[] = {
          ST->getValue(),
          FrameIndex,
          CurDAG->getTargetConstant(Offset, DL, MVT::i64),
          ST->getChain(),
      };
      CurDAG->SelectNodeTo(N, getStoreOpcode(ST, /*IsFrame=*/true), MVT::Other,
                           Ops);
      return;
    }

    SDValue Target;
    if (selectSymbolAddress(CurDAG, ST->getBasePtr(), DL, Target)) {
      SDValue Ops[] = {ST->getValue(), Target, ST->getChain()};
      CurDAG->SelectNodeTo(N, getStoreAbsOpcode(ST), MVT::Other, Ops);
      return;
    }

    SDValue Ops[] = {ST->getValue(), ST->getBasePtr(), ST->getChain()};
    CurDAG->SelectNodeTo(N, getStoreOpcode(ST, /*IsFrame=*/false), MVT::Other,
                         Ops);
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
    switch (N->getOpcode()) {
    case ISD::ADD:
    case ISD::SUB:
    case ISD::AND:
    case ISD::OR:
    case ISD::XOR:
    case ISD::SHL:
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
