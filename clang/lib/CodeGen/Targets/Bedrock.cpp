//===- Bedrock.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"
#include "llvm/IR/DerivedTypes.h"

using namespace clang;
using namespace clang::CodeGen;

namespace {
class BedrockABIInfo : public DefaultABIInfo {
  static constexpr unsigned XLen = 64;

  CharUnits getSlotAlign() const { return CharUnits::fromQuantity(16); }

  llvm::Type *getI64Array(unsigned NumElts) const {
    return llvm::ArrayType::get(llvm::Type::getInt64Ty(getVMContext()),
                                NumElts);
  }

  llvm::Type *getTwoDRegCoerceType() const { return getI64Array(2); }

  llvm::Type *getSmallAggregateCoerceType(QualType Ty) const {
    uint64_t Size = getContext().getTypeSize(Ty);
    if (Size <= XLen)
      return llvm::IntegerType::get(getVMContext(), llvm::alignTo(Size, 8));
    return getTwoDRegCoerceType();
  }

  ABIArgInfo getIndirectByVal(QualType Ty) const {
    return ABIArgInfo::getIndirect(getSlotAlign(),
                                   getDataLayout().getAllocaAddrSpace(),
                                   /*ByVal=*/true, /*Realign=*/false);
  }

  ABIArgInfo getIndirectSRet(QualType Ty) const {
    return ABIArgInfo::getIndirect(getSlotAlign(),
                                   getDataLayout().getAllocaAddrSpace(),
                                   /*ByVal=*/false, /*Realign=*/false);
  }

public:
  BedrockABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  ABIArgInfo classifyReturnType(QualType RetTy) const {
    if (RetTy->isVoidType())
      return ABIArgInfo::getIgnore();

    if (isAggregateTypeForABI(RetTy)) {
      if (isEmptyRecord(getContext(), RetTy, true))
        return ABIArgInfo::getIgnore();

      if (getContext().getTypeSize(RetTy) <= 128)
        return ABIArgInfo::getDirect(getSmallAggregateCoerceType(RetTy), 0,
                                     nullptr, /*CanBeFlattened=*/false);

      return getIndirectSRet(RetTy);
    }

    if (const auto *ED = RetTy->getAsEnumDecl())
      RetTy = ED->getIntegerType();

    if (RetTy->isIntegralOrEnumerationType() &&
        getContext().getTypeSize(RetTy) < 32)
      return ABIArgInfo::getExtend(RetTy);

    if (RetTy->isIntegralOrEnumerationType() &&
        getContext().getTypeSize(RetTy) == 128)
      return ABIArgInfo::getDirect(getTwoDRegCoerceType());

    return DefaultABIInfo::classifyReturnType(RetTy);
  }

  ABIArgInfo classifyArgumentType(QualType Ty, bool IsFixed) const {
    Ty = useFirstFieldIfTransparentUnion(Ty);

    if (const RecordType *RT = Ty->getAsCanonical<RecordType>()) {
      CGCXXABI::RecordArgABI RAA = getRecordArgABI(RT, getCXXABI());
      if (RAA == CGCXXABI::RAA_Indirect || RAA == CGCXXABI::RAA_DirectInMemory)
        return getIndirectByVal(Ty);
    }

    if (isAggregateTypeForABI(Ty)) {
      if (isEmptyRecord(getContext(), Ty, true))
        return ABIArgInfo::getIgnore();
      return getIndirectByVal(Ty);
    }

    if (const auto *ED = Ty->getAsEnumDecl())
      Ty = ED->getIntegerType();

    if (Ty->isIntegralOrEnumerationType() && getContext().getTypeSize(Ty) < 32)
      return ABIArgInfo::getExtend(Ty);

    if (Ty->isIntegralOrEnumerationType() &&
        getContext().getTypeSize(Ty) == 128)
      return ABIArgInfo::getDirect(getTwoDRegCoerceType());

    return DefaultABIInfo::classifyArgumentType(Ty);
  }

  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());

    unsigned ArgNo = 0;
    unsigned NumFixedArgs = FI.getNumRequiredArgs();
    for (auto &I : FI.arguments())
      I.info = classifyArgumentType(I.type, ArgNo++ < NumFixedArgs);
  }

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override {
    bool IsIndirect = isAggregateTypeForABI(Ty);
    return emitVoidPtrVAArg(CGF, VAListAddr, Ty, IsIndirect,
                            getContext().getTypeInfoInChars(Ty), getSlotAlign(),
                            /*AllowHigherAlign=*/false, Slot);
  }
};

class BedrockTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  BedrockTargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<BedrockABIInfo>(CGT)) {}

  RequiredArgs
  getNoProtoCallRequiredArgs(const CallArgList &args,
                             const FunctionNoProtoType *fnType) const override {
    return RequiredArgs(0);
  }
};
} // end anonymous namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createBedrockTargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<BedrockTargetCodeGenInfo>(CGM.getTypes());
}
