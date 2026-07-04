//===-- BedrockSelectionDAGInfo.cpp - Bedrock SelectionDAG Info -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BedrockSelectionDAGInfo.h"
#include "BedrockISelLowering.h"

using namespace llvm;

bool BedrockSelectionDAGInfo::isTargetMemoryOpcode(unsigned Opcode) const {
  switch (Opcode) {
  case BedrockISD::FETCHADD:
  case BedrockISD::FETCHSUB:
  case BedrockISD::FETCHAND:
  case BedrockISD::FETCHOR:
  case BedrockISD::FETCHXOR:
  case BedrockISD::CMPXCHG:
    return true;
  default:
    return false;
  }
}
