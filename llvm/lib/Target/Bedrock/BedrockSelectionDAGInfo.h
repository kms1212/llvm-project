//===-- BedrockSelectionDAGInfo.h - Bedrock SelectionDAG info ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_BEDROCKSELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_BEDROCK_BEDROCKSELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

#define GET_SDNODE_ENUM
#include "BedrockGenSDNodeInfo.inc"

namespace llvm {

class BedrockSelectionDAGInfo : public SelectionDAGGenTargetInfo {
public:
  BedrockSelectionDAGInfo();
  ~BedrockSelectionDAGInfo() override;

  SDValue EmitTargetCodeForMemset(
      SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst,
      SDValue Value, SDValue Size, Align Alignment, bool IsVolatile,
      bool AlwaysInline, MachinePointerInfo DstPtrInfo) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_BEDROCK_BEDROCKSELECTIONDAGINFO_H
