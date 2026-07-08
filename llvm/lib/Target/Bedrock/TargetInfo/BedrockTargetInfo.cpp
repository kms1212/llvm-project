//===-- BedrockTargetInfo.cpp - Bedrock target implementation -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/BedrockTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

Target &llvm::getTheBedrockTarget() {
  static Target TheBedrockTarget;
  return TheBedrockTarget;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeBedrockTargetInfo() {
  RegisterTarget<Triple::bedrock, /*HasJIT=*/false> X(
      getTheBedrockTarget(), "bedrock", "Bedrock", "Bedrock");
}
