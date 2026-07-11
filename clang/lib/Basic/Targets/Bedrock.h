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
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }

  ArrayRef<const char *> getGCCRegNames() const override;
  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override;

  std::string_view getClobbers() const override { return ""; }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }

  bool hasBitIntType() const override { return true; }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    return CC == CC_C || CC == CC_BedrockFar ? CCCR_OK : CCCR_Warning;
  }

  uint64_t getMaxPointerWidth() const override { return 128; }

protected:
  uint64_t getPointerWidthV(LangAS AS) const override {
    return getTargetAddressSpace(AS) == 1 ? 128 : PointerWidth;
  }
  uint64_t getPointerAlignV(LangAS AS) const override {
    return getTargetAddressSpace(AS) == 1 ? 128 : PointerAlign;
  }
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_BEDROCK_H
