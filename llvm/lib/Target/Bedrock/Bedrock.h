//===-- Bedrock.h - Top-level interface for Bedrock ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_BEDROCK_H
#define LLVM_LIB_TARGET_BEDROCK_BEDROCK_H

#include "MCTargetDesc/BedrockMCTargetDesc.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/CodeGen.h"

namespace llvm {
class BedrockTargetMachine;
class FunctionPass;
class PassRegistry;

enum class BedrockPeepholeProfile {
  O0,
  O1,
  O2,
  O3,
};

FunctionPass *createBedrockISelDag(BedrockTargetMachine &TM,
                                   CodeGenOptLevel OptLevel);
FunctionPass *createBedrockBoundBranchPass();
FunctionPass *createBedrockPushPopMergePass(BedrockPeepholeProfile Profile);

void initializeBedrockAsmPrinterPass(PassRegistry &);
void initializeBedrockDAGToDAGISelLegacyPass(PassRegistry &);
void initializeBedrockBoundBranchPass(PassRegistry &);
void initializeBedrockPushPopMergePass(PassRegistry &);
} // namespace llvm

#endif
