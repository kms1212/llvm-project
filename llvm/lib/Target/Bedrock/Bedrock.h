//===-- Bedrock.h - Top-level interface for Bedrock ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_BEDROCK_H
#define LLVM_LIB_TARGET_BEDROCK_BEDROCK_H

#include "llvm/Target/TargetMachine.h"

namespace llvm {

class BedrockTargetMachine;
class FunctionPass;
class PassRegistry;

FunctionPass *createBedrockISelDag(BedrockTargetMachine &TM,
                                   CodeGenOptLevel OptLevel);
FunctionPass *createBedrockPreEmitPeepholePass();

void initializeBedrockAsmPrinterPass(PassRegistry &);
void initializeBedrockDAGToDAGISelLegacyPass(PassRegistry &);
void initializeBedrockPreEmitPeepholePass(PassRegistry &);

} // namespace llvm

#endif // LLVM_LIB_TARGET_BEDROCK_BEDROCK_H
