//===-- BedrockSelectionDAGInfo.cpp - Bedrock SelectionDAG info -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockSelectionDAGInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"

#define GET_SDNODE_DESC
#include "BedrockGenSDNodeInfo.inc"

using namespace llvm;

BedrockSelectionDAGInfo::BedrockSelectionDAGInfo()
    : SelectionDAGGenTargetInfo(BedrockGenSDNodeInfo) {}

BedrockSelectionDAGInfo::~BedrockSelectionDAGInfo() = default;

SDValue BedrockSelectionDAGInfo::EmitTargetCodeForMemset(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst,
    SDValue Value, SDValue Size, Align Alignment, bool IsVolatile,
    bool AlwaysInline, MachinePointerInfo DstPtrInfo) const {
  // REPG may combine or widen memory operations, so it is not a valid lowering
  // for volatile memset.  Keep small fills on the ordinary expansion path as
  // well, where the repeat setup would not pay for itself.
  auto *ConstantSize = dyn_cast<ConstantSDNode>(Size);
  if (IsVolatile || !ConstantSize)
    return {};
  if (ConstantSize->isZero())
    return Chain;
  if (!AlwaysInline && ConstantSize->getZExtValue() < 16)
    return {};

  Dst = DAG.getZExtOrTrunc(Dst, DL, MVT::i64);
  Value = DAG.getZExtOrTrunc(Value, DL, MVT::i64);
  Size = DAG.getZExtOrTrunc(Size, DL, MVT::i64);
  SDVTList VTs = DAG.getVTList(MVT::i64, MVT::i64, MVT::Other);
  SDValue Node = DAG.getNode(BedrockISD::MEMSET, DL, VTs,
                             {Chain, Dst, Value, Size});
  return Node.getValue(2);
}
