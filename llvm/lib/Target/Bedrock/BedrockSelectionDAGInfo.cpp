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
