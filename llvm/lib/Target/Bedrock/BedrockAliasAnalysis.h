//===- BedrockAliasAnalysis.h - Bedrock alias analysis ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BEDROCK_BEDROCKALIASANALYSIS_H
#define LLVM_LIB_TARGET_BEDROCK_BEDROCKALIASANALYSIS_H

#include "llvm/Analysis/AliasAnalysis.h"

namespace llvm {

/// Bedrock near and far pointers name overlapping canonical address ranges.
/// This analysis deliberately does not derive disjointness from address-space
/// numbers or from segment metadata.
class BedrockAAResult : public AAResultBase {
public:
  BedrockAAResult() = default;

  bool invalidate(Function &, const PreservedAnalyses &,
                  FunctionAnalysisManager::Invalidator &) {
    return false;
  }

  AliasResult alias(const MemoryLocation &, const MemoryLocation &,
                    AAQueryInfo &, const Instruction *) {
    return AliasResult::MayAlias;
  }
};

class BedrockAA : public AnalysisInfoMixin<BedrockAA> {
  friend AnalysisInfoMixin<BedrockAA>;
  static AnalysisKey Key;

public:
  using Result = BedrockAAResult;

  BedrockAAResult run(Function &, AnalysisManager<Function> &) {
    return BedrockAAResult();
  }
};

} // namespace llvm

#endif
