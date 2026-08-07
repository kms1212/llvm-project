//===--- Bedrock.h - Declare Bedrock target feature support -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_BEDROCK_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_BEDROCK_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY BedrockTargetInfo : public TargetInfo {
  static const char *const GCCRegNames[];
  static const TargetInfo::GCCRegAlias GCCRegAliases[];
  bool HasFPU = true;
  bool HasFPTRANSA = false;

public:
  BedrockTargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    resetDataLayout();

    PointerWidth = PointerAlign = 64;
    LongWidth = LongAlign = 64;
    LongLongWidth = LongLongAlign = 64;
    Int64Type = SignedLong;
    SuitableAlign = 128;
    DefaultAlignForAttributeAligned = 128;
    IntMaxType = SignedLong;
    IntPtrType = SignedLong;
    SizeType = UnsignedLong;
    PtrDiffType = SignedLong;
    WIntType = SignedInt;
    WCharType = SignedInt;
    UseSignedCharForObjCBool = true;
    LongDoubleWidth = 64;
    LongDoubleAlign = 64;
    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
    HasFloat128 = false;
    HasIbm128 = false;
    MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 64;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override;

  bool hasFeature(StringRef Feature) const override {
    return Feature == "bedrock" || (Feature == "fpu" && HasFPU) ||
           (Feature == "fptransa" && HasFPTRANSA);
  }
  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 StringRef CPU,
                 const std::vector<std::string> &FeaturesVec) const override;
  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override;

  ArrayRef<const char *> getGCCRegNames() const override;
  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override;

  std::string_view getClobbers() const override { return ""; }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }

  bool hasBitIntType() const override { return true; }

  bool useBitFieldTypeForAllocationUnits() const override { return true; }

  bool useSignedIntForNonNegativeEnums() const override { return true; }

};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_BEDROCK_H
