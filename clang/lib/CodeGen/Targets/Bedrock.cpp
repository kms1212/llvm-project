//===- Bedrock.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

using namespace clang;
using namespace clang::CodeGen;

namespace {

class BedrockABIInfo : public DefaultABIInfo {
  static constexpr unsigned StackSlotSize = 16;
  static constexpr unsigned MaxAggregateAlign = 16;

public:
  BedrockABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  ABIArgInfo classifyArgumentType(QualType Ty) const;
  ABIArgInfo classifyReturnType(QualType RetTy) const;

  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &Arg : FI.arguments())
      Arg.info = classifyArgumentType(Arg.type);
  }

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;

private:
  ABIArgInfo getByValAggregate(QualType Ty) const;
  llvm::Type *getSmallAggregateCoerceType(QualType Ty) const;
};

class BedrockTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  BedrockTargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<BedrockABIInfo>(CGT)) {}
};

} // namespace

ABIArgInfo BedrockABIInfo::getByValAggregate(QualType Ty) const {
  unsigned TypeAlign = getContext().getTypeAlign(Ty) / 8;
  return ABIArgInfo::getIndirect(CharUnits::fromQuantity(MaxAggregateAlign),
                                 getDataLayout().getAllocaAddrSpace(),
                                 /*ByVal=*/true,
                                 /*Realign=*/TypeAlign > MaxAggregateAlign);
}

llvm::Type *BedrockABIInfo::getSmallAggregateCoerceType(QualType Ty) const {
  uint64_t Bits = getContext().getTypeSize(Ty);
  if (Bits <= 64)
    return llvm::IntegerType::get(getVMContext(), llvm::alignTo(Bits, 8));

  llvm::Type *RegTy = llvm::Type::getInt64Ty(getVMContext());
  return llvm::ArrayType::get(RegTy, 2);
}

ABIArgInfo BedrockABIInfo::classifyArgumentType(QualType Ty) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  const RecordType *RT = Ty->getAsCanonical<RecordType>();
  if (RT) {
    CGCXXABI::RecordArgABI RAA = getRecordArgABI(RT, getCXXABI());
    if (RAA == CGCXXABI::RAA_Indirect)
      return getNaturalAlignIndirect(Ty, getDataLayout().getAllocaAddrSpace(),
                                     /*ByVal=*/false);
    if (RAA == CGCXXABI::RAA_DirectInMemory)
      return getByValAggregate(Ty);
  }

  if (isAggregateTypeForABI(Ty)) {
    if (isEmptyRecord(getContext(), Ty, /*AllowArrays=*/true))
      return ABIArgInfo::getIgnore();
    return getByValAggregate(Ty);
  }

  if (const auto *ED = Ty->getAsEnumDecl())
    Ty = ED->getIntegerType();

  if (const auto *EIT = Ty->getAs<BitIntType>())
    if (EIT->getNumBits() > getContext().getTypeSize(getContext().Int128Ty))
      return getNaturalAlignIndirect(Ty, getDataLayout().getAllocaAddrSpace(),
                                     /*ByVal=*/true);

  if (Ty->isIntegralOrEnumerationType() && getContext().getTypeSize(Ty) <= 32)
    return ABIArgInfo::getExtend(Ty);

  return ABIArgInfo::getDirect();
}

ABIArgInfo BedrockABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  if (isAggregateTypeForABI(RetTy)) {
    if (isEmptyRecord(getContext(), RetTy, /*AllowArrays=*/true))
      return ABIArgInfo::getIgnore();

    if (getContext().getTypeSize(RetTy) <= 128)
      return ABIArgInfo::getDirect(getSmallAggregateCoerceType(RetTy));

    return getNaturalAlignIndirect(RetTy, getDataLayout().getAllocaAddrSpace(),
                                   /*ByVal=*/false);
  }

  if (const auto *ED = RetTy->getAsEnumDecl())
    RetTy = ED->getIntegerType();

  if (const auto *EIT = RetTy->getAs<BitIntType>())
    if (EIT->getNumBits() > getContext().getTypeSize(getContext().Int128Ty))
      return getNaturalAlignIndirect(
          RetTy, getDataLayout().getAllocaAddrSpace(), /*ByVal=*/false);

  return ABIArgInfo::getDirect();
}

RValue BedrockABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                 QualType Ty, AggValueSlot Slot) const {
  if (isEmptyRecord(getContext(), Ty, /*AllowArrays=*/true))
    return Slot.asRValue();

  TypeInfoChars TInfo = getContext().getTypeInfoInChars(Ty);
  bool IsIndirect = isAggregateTypeForABI(Ty);
  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, IsIndirect, TInfo,
                          CharUnits::fromQuantity(StackSlotSize),
                          /*AllowHigherAlign=*/true, Slot);
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createBedrockTargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<BedrockTargetCodeGenInfo>(CGM.getTypes());
}
