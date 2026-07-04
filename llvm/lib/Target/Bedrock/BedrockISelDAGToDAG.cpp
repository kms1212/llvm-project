//===-- BedrockISelDAGToDAG.cpp - Bedrock DAG Instruction Selector --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Bedrock.h"
#include "BedrockISelLowering.h"
#include "BedrockTargetMachine.h"
#include "llvm/ADT/APInt.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "bedrock-isel"
#define PASS_NAME "Bedrock DAG->DAG Pattern Instruction Selection"

namespace {
class BedrockDAGToDAGISel : public SelectionDAGISel {
public:
  BedrockDAGToDAGISel(BedrockTargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

  void Select(SDNode *Node) override;
  bool selectAddr(SDValue Addr, SDValue &Base, SDValue &Offset);
  bool selectBitOp(SDNode *Node);

  bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                    InlineAsm::ConstraintCode ConstraintID,
                                    std::vector<SDValue> &OutOps) override {
    return true;
  }

#include "BedrockGenDAGISel.inc"
};

class BedrockDAGToDAGISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;
  BedrockDAGToDAGISelLegacy(BedrockTargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISelLegacy(
            ID, std::make_unique<BedrockDAGToDAGISel>(TM, OptLevel)) {}

  StringRef getPassName() const override { return PASS_NAME; }
};
} // end anonymous namespace

char BedrockDAGToDAGISelLegacy::ID = 0;

INITIALIZE_PASS(BedrockDAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createBedrockISelDag(BedrockTargetMachine &TM,
                                         CodeGenOptLevel OptLevel) {
  return new BedrockDAGToDAGISelLegacy(TM, OptLevel);
}

bool BedrockDAGToDAGISel::selectAddr(SDValue Addr, SDValue &Base,
                                     SDValue &Offset) {
  SDLoc DL(Addr);
  EVT VT = Addr.getValueType();

  if (auto *FI = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base = CurDAG->getTargetFrameIndex(FI->getIndex(), VT);
    Offset = CurDAG->getTargetConstant(0, DL, VT);
    return true;
  }

  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    auto *CN = cast<ConstantSDNode>(Addr.getOperand(1));
    int64_t OffsetVal = CN->getSExtValue();
    if (isInt<32>(OffsetVal)) {
      SDValue BaseOp = Addr.getOperand(0);
      if (auto *FI = dyn_cast<FrameIndexSDNode>(BaseOp))
        Base = CurDAG->getTargetFrameIndex(FI->getIndex(), VT);
      else
        Base = BaseOp;
      Offset = CurDAG->getTargetConstant(OffsetVal, DL, VT);
      return true;
    }
  }

  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, DL, VT);
  return true;
}

static unsigned getBTestOpcode(EVT VT) {
  switch (VT.getSimpleVT().SimpleTy) {
  default:
    return 0;
  case MVT::i8:
    return Bedrock::BTEST8ri;
  case MVT::i16:
    return Bedrock::BTEST16ri;
  case MVT::i32:
    return Bedrock::BTEST32ri;
  case MVT::i64:
    return Bedrock::BTEST64ri;
  }
}

static unsigned getBndOpcode(EVT VT, unsigned Mode, bool IsSigned) {
  switch (VT.getSimpleVT().SimpleTy) {
  default:
    return 0;
  case MVT::i8:
    if (IsSigned) {
      switch (Mode) {
      default:
        return 0;
      case BedrockISD::BND_II:
        return Bedrock::BNDSII8rrr;
      case BedrockISD::BND_IX:
        return Bedrock::BNDSIX8rrr;
      case BedrockISD::BND_XI:
        return Bedrock::BNDSXI8rrr;
      case BedrockISD::BND_XX:
        return Bedrock::BNDSXX8rrr;
      }
    }
    switch (Mode) {
    default:
      return 0;
    case BedrockISD::BND_II:
      return Bedrock::BNDUII8rrr;
    case BedrockISD::BND_IX:
      return Bedrock::BNDUIX8rrr;
    case BedrockISD::BND_XI:
      return Bedrock::BNDUXI8rrr;
    case BedrockISD::BND_XX:
      return Bedrock::BNDUXX8rrr;
    }
  case MVT::i16:
    if (IsSigned) {
      switch (Mode) {
      default:
        return 0;
      case BedrockISD::BND_II:
        return Bedrock::BNDSII16rrr;
      case BedrockISD::BND_IX:
        return Bedrock::BNDSIX16rrr;
      case BedrockISD::BND_XI:
        return Bedrock::BNDSXI16rrr;
      case BedrockISD::BND_XX:
        return Bedrock::BNDSXX16rrr;
      }
    }
    switch (Mode) {
    default:
      return 0;
    case BedrockISD::BND_II:
      return Bedrock::BNDUII16rrr;
    case BedrockISD::BND_IX:
      return Bedrock::BNDUIX16rrr;
    case BedrockISD::BND_XI:
      return Bedrock::BNDUXI16rrr;
    case BedrockISD::BND_XX:
      return Bedrock::BNDUXX16rrr;
    }
  case MVT::i32:
    if (IsSigned) {
      switch (Mode) {
      default:
        return 0;
      case BedrockISD::BND_II:
        return Bedrock::BNDSII32rrr;
      case BedrockISD::BND_IX:
        return Bedrock::BNDSIX32rrr;
      case BedrockISD::BND_XI:
        return Bedrock::BNDSXI32rrr;
      case BedrockISD::BND_XX:
        return Bedrock::BNDSXX32rrr;
      }
    }
    switch (Mode) {
    default:
      return 0;
    case BedrockISD::BND_II:
      return Bedrock::BNDUII32rrr;
    case BedrockISD::BND_IX:
      return Bedrock::BNDUIX32rrr;
    case BedrockISD::BND_XI:
      return Bedrock::BNDUXI32rrr;
    case BedrockISD::BND_XX:
      return Bedrock::BNDUXX32rrr;
    }
  case MVT::i64:
    if (IsSigned) {
      switch (Mode) {
      default:
        return 0;
      case BedrockISD::BND_II:
        return Bedrock::BNDSII64rrr;
      case BedrockISD::BND_IX:
        return Bedrock::BNDSIX64rrr;
      case BedrockISD::BND_XI:
        return Bedrock::BNDSXI64rrr;
      case BedrockISD::BND_XX:
        return Bedrock::BNDSXX64rrr;
      }
    }
    switch (Mode) {
    default:
      return 0;
    case BedrockISD::BND_II:
      return Bedrock::BNDUII64rrr;
    case BedrockISD::BND_IX:
      return Bedrock::BNDUIX64rrr;
    case BedrockISD::BND_XI:
      return Bedrock::BNDUXI64rrr;
    case BedrockISD::BND_XX:
      return Bedrock::BNDUXX64rrr;
    }
  }
}

static unsigned getBitOpOpcode(unsigned Opcode, EVT VT) {
  switch (VT.getSimpleVT().SimpleTy) {
  default:
    return 0;
  case MVT::i8:
    return Opcode == ISD::OR     ? Bedrock::BSET8ri
           : Opcode == ISD::AND  ? Bedrock::BCLR8ri
           : Opcode == ISD::XOR  ? Bedrock::BCHG8ri
                                 : 0;
  case MVT::i16:
    return Opcode == ISD::OR     ? Bedrock::BSET16ri
           : Opcode == ISD::AND  ? Bedrock::BCLR16ri
           : Opcode == ISD::XOR  ? Bedrock::BCHG16ri
                                 : 0;
  case MVT::i32:
    return Opcode == ISD::OR     ? Bedrock::BSET32ri
           : Opcode == ISD::AND  ? Bedrock::BCLR32ri
           : Opcode == ISD::XOR  ? Bedrock::BCHG32ri
                                 : 0;
  case MVT::i64:
    return Opcode == ISD::OR     ? Bedrock::BSET64ri
           : Opcode == ISD::AND  ? Bedrock::BCLR64ri
           : Opcode == ISD::XOR  ? Bedrock::BCHG64ri
                                 : 0;
  }
}

bool BedrockDAGToDAGISel::selectBitOp(SDNode *Node) {
  if (OptLevel == CodeGenOptLevel::None)
    return false;

  unsigned Opcode = Node->getOpcode();
  if (Opcode != ISD::OR && Opcode != ISD::AND && Opcode != ISD::XOR)
    return false;

  EVT VT = Node->getValueType(0);
  unsigned MachineOpcode = getBitOpOpcode(Opcode, VT);
  if (MachineOpcode == 0)
    return false;

  SDValue LHS = Node->getOperand(0);
  SDValue RHS = Node->getOperand(1);
  auto *C = dyn_cast<ConstantSDNode>(RHS);
  SDValue Src = LHS;
  if (!C) {
    C = dyn_cast<ConstantSDNode>(LHS);
    Src = RHS;
  }
  if (!C)
    return false;

  unsigned Width = VT.getSizeInBits();
  APInt Mask = C->getAPIntValue();
  if (Mask.getBitWidth() != Width)
    Mask = Mask.trunc(Width);

  APInt Bit = Mask;
  if (Opcode == ISD::AND)
    Bit = ~Mask;
  if (!Bit.isPowerOf2())
    return false;

  unsigned Index = Bit.logBase2();
  if (Index >= Width || Index >= 64)
    return false;

  SDLoc DL(Node);
  SDValue TargetIndex = CurDAG->getTargetConstant(Index, DL, MVT::i8);
  CurDAG->SelectNodeTo(Node, MachineOpcode, VT, Src, TargetIndex);
  return true;
}

void BedrockDAGToDAGISel::Select(SDNode *Node) {
  unsigned Opcode = Node->getOpcode();
  if (Node->isMachineOpcode()) {
    Node->setNodeId(-1);
    return;
  }

  if (Opcode == ISD::FrameIndex) {
    SDLoc DL(Node);
    EVT VT = Node->getValueType(0);
    int FI = cast<FrameIndexSDNode>(Node)->getIndex();
    SDValue TFI = CurDAG->getTargetFrameIndex(FI, VT);
    SDValue Zero = CurDAG->getTargetConstant(0, DL, VT);
    CurDAG->SelectNodeTo(Node, Bedrock::ADD64fi, VT, TFI, Zero);
    return;
  }

  if (Opcode == BedrockISD::BTEST) {
    auto *Index = cast<ConstantSDNode>(Node->getOperand(1));
    SDLoc DL(Node);
    SDValue TargetIndex =
        CurDAG->getTargetConstant(Index->getZExtValue(), DL, MVT::i8);
    unsigned MachineOpcode = getBTestOpcode(Node->getOperand(0).getValueType());
    if (MachineOpcode != 0) {
      CurDAG->SelectNodeTo(Node, MachineOpcode, MVT::Glue, Node->getOperand(0),
                           TargetIndex);
      return;
    }
  }

  if (Opcode == BedrockISD::BND) {
    auto *Mode = cast<ConstantSDNode>(Node->getOperand(3));
    auto *Signed = cast<ConstantSDNode>(Node->getOperand(4));
    unsigned MachineOpcode =
        getBndOpcode(Node->getOperand(1).getValueType(), Mode->getZExtValue(),
                     Signed->getZExtValue() != 0);
    if (MachineOpcode != 0) {
      CurDAG->SelectNodeTo(Node, MachineOpcode, MVT::Glue, Node->getOperand(0),
                           Node->getOperand(1), Node->getOperand(2));
      return;
    }
  }

  if (selectBitOp(Node))
    return;

  SelectCode(Node);
}
