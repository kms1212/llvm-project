//===--- Bedrock.cpp - Implement Bedrock target feature support -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Bedrock.h"
#include "clang/Basic/MacroBuilder.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/ADT/STLExtras.h"

using namespace clang;
using namespace clang::targets;

static constexpr int NumBuiltins =
    clang::Bedrock::LastTSBuiltin - Builtin::FirstTSBuiltin;

#define GET_BUILTIN_STR_TABLE
#include "clang/Basic/BuiltinsBedrock.inc"
#undef GET_BUILTIN_STR_TABLE

static constexpr Builtin::Info BuiltinInfos[] = {
#define GET_BUILTIN_INFOS
#include "clang/Basic/BuiltinsBedrock.inc"
#undef GET_BUILTIN_INFOS
};
static_assert(std::size(BuiltinInfos) == NumBuiltins);

llvm::SmallVector<Builtin::InfosShard>
BedrockTargetInfo::getTargetBuiltins() const {
  return {{&BuiltinStrings, BuiltinInfos}};
}

bool BedrockTargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags, StringRef CPU,
    const std::vector<std::string> &FeaturesVec) const {
  std::vector<std::string> EffectiveFeatures = FeaturesVec;
  if (llvm::none_of(FeaturesVec, [](StringRef Feature) {
        return Feature == "+fpu" || Feature == "-fpu";
      }))
    EffectiveFeatures.emplace_back("+fpu");
  if (llvm::none_of(FeaturesVec, [](StringRef Feature) {
        return Feature == "+vector" || Feature == "-vector";
      }))
    EffectiveFeatures.emplace_back("+vector");
  return TargetInfo::initFeatureMap(Features, Diags, CPU, EffectiveFeatures);
}

bool BedrockTargetInfo::handleTargetFeatures(std::vector<std::string> &Features,
                                             DiagnosticsEngine &) {
  for (const std::string &Feature : Features) {
    if (Feature == "+fpu")
      HasFPU = true;
    else if (Feature == "-fpu")
      HasFPU = false;
    else if (Feature == "+fptransa")
      HasFPTRANSA = true;
    else if (Feature == "-fptransa")
      HasFPTRANSA = false;
    else if (Feature == "+vector")
      HasVector = true;
    else if (Feature == "-vector")
      HasVector = false;
  }
  if (HasFPTRANSA)
    HasFPU = true;
  return true;
}

const char *const BedrockTargetInfo::GCCRegNames[] = {
    "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",  "r8",  "r9", "r10",
    "r11", "r12", "r13", "r14", "r15", "f0",  "f1",  "f2",  "f3",  "f4", "f5",
    "f6",  "f7",  "f8",  "f9",  "f10", "f11", "f12", "f13", "f14", "f15",
    "v0",  "v1",  "v2",  "v3",  "v4",  "v5",  "v6",  "v7",  "v8",  "v9",
    "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19",
    "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29",
    "v30", "v31", "p0",  "p1",  "p2",  "p3",  "p4",  "p5",  "p6",  "p7",
    "p8",  "p9",  "p10", "p11", "p12", "p13", "p14", "p15"};

ArrayRef<const char *> BedrockTargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

const TargetInfo::GCCRegAlias BedrockTargetInfo::GCCRegAliases[] = {
    {{"fp"}, "r15"},
};

ArrayRef<TargetInfo::GCCRegAlias> BedrockTargetInfo::getGCCRegAliases() const {
  return llvm::ArrayRef(GCCRegAliases);
}

void BedrockTargetInfo::getTargetDefines(const LangOptions &Opts,
                                         MacroBuilder &Builder) const {
  Builder.defineMacro("__bedrock__");
  Builder.defineMacro("__BEDROCK__");
  Builder.defineMacro("__bedrock_c_abi", "1");
  if (HasVector)
    Builder.defineMacro("__bedrock_vector__", "1");
}

bool BedrockTargetInfo::validateAsmConstraint(
    const char *&Name, TargetInfo::ConstraintInfo &Info) const {
  switch (*Name) {
  default:
    return false;
  case 'r':
    Info.setAllowsRegister();
    return true;
  case 'm':
    Info.setAllowsMemory();
    return true;
  }
}
