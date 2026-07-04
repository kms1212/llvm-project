//===-- BedrockSelectionDAGInfo.h - Bedrock SelectionDAG Info -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_BEDROCKSELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_BEDROCK_BEDROCKSELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

namespace llvm {
class BedrockSelectionDAGInfo : public SelectionDAGTargetInfo {
public:
  bool isTargetMemoryOpcode(unsigned Opcode) const override;
};
} // end namespace llvm

#endif
