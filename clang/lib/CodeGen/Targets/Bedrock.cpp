//===- Bedrock.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"
#include "clang/AST/Attr.h"
#include "clang/AST/RecordLayout.h"
#include "llvm/ADT/SmallPtrSet.h"

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

  bool isNoProtoCallVariadic(const CallArgList &args,
                             const FunctionNoProtoType *fnType) const override {
    return true;
  }

  unsigned getNoProtoCallRequiredArgs(
      const CallArgList &args,
      const FunctionNoProtoType *fnType) const override {
    // Bedrock's no-prototype rule puts every actual argument in the unnamed
    // 16-byte slot sequence.
    return 0;
  }

  void checkFunctionABI(CodeGenModule &CGM,
                        const FunctionDecl *Decl) const override;
  void checkFunctionCallABI(CodeGenModule &CGM, SourceLocation CallLoc,
                            const FunctionDecl *Caller,
                            const FunctionDecl *Callee, const CallArgList &Args,
                            QualType ReturnType) const override;
};

} // namespace

static bool hasNonBaselineAggregateLayout(
    ASTContext &Context, QualType Ty,
    llvm::SmallPtrSetImpl<const RecordDecl *> &Visited) {
  Ty = Ty.getCanonicalType();
  if (const auto *ArrayTy = Context.getAsArrayType(Ty))
    return hasNonBaselineAggregateLayout(Context, ArrayTy->getElementType(),
                                         Visited);

  const auto *RecordTy = Ty->getAs<RecordType>();
  if (!RecordTy)
    return false;
  const RecordDecl *Record = RecordTy->getDecl()->getDefinition();
  if (!Record || !Visited.insert(Record).second)
    return false;

  if (Record->hasAttr<PackedAttr>() || Record->hasAttr<MaxFieldAlignmentAttr>())
    return true;

  const ASTRecordLayout &Layout = Context.getASTRecordLayout(Record);
  unsigned Index = 0;
  for (const FieldDecl *Field : Record->fields()) {
    QualType FieldTy = Field->getType();
    if (Field->hasAttr<PackedAttr>())
      return true;

    if (!Field->isBitField()) {
      QualType AlignmentTy = FieldTy;
      if (const auto *ArrayTy = Context.getAsArrayType(FieldTy))
        AlignmentTy = ArrayTy->getElementType();
      unsigned NaturalAlign = Context.getTypeAlign(AlignmentTy);
      if (NaturalAlign && Layout.getFieldOffset(Index) % NaturalAlign != 0)
        return true;
    }

    if (hasNonBaselineAggregateLayout(Context, FieldTy, Visited))
      return true;
    ++Index;
  }
  return false;
}

static void diagnoseNonBaselineAggregateTypes(CodeGenModule &CGM,
                                              SourceLocation Loc,
                                              ArrayRef<QualType> Types) {
  llvm::SmallPtrSet<const Type *, 4> Diagnosed;
  for (QualType Ty : Types) {
    if (!Ty->isAggregateType() ||
        !Diagnosed.insert(Ty.getCanonicalType().getTypePtr()).second)
      continue;
    llvm::SmallPtrSet<const RecordDecl *, 4> Visited;
    if (!hasNonBaselineAggregateLayout(CGM.getContext(), Ty, Visited))
      continue;
    std::string Message = "Bedrock C ABI does not permit packed or "
                          "under-aligned aggregate type '" +
                          Ty.getAsString() +
                          "' across an external ABI boundary";
    CGM.Error(Loc, Message);
  }
}

void BedrockTargetCodeGenInfo::checkFunctionABI(
    CodeGenModule &CGM, const FunctionDecl *Decl) const {
  if (!Decl->isExternallyVisible())
    return;
  SmallVector<QualType, 8> Types;
  Types.push_back(Decl->getReturnType());
  for (const ParmVarDecl *Param : Decl->parameters())
    Types.push_back(Param->getType());
  diagnoseNonBaselineAggregateTypes(CGM, Decl->getLocation(), Types);
}

void BedrockTargetCodeGenInfo::checkFunctionCallABI(CodeGenModule &CGM,
                                                    SourceLocation CallLoc,
                                                    const FunctionDecl *Caller,
                                                    const FunctionDecl *Callee,
                                                    const CallArgList &Args,
                                                    QualType ReturnType) const {
  if (Callee && !Callee->isExternallyVisible())
    return;
  SmallVector<QualType, 8> Types;
  Types.push_back(ReturnType);
  for (const CallArg &Arg : Args)
    Types.push_back(Arg.getType());
  diagnoseNonBaselineAggregateTypes(CGM, CallLoc, Types);
}

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

  // Complex scalars use two consecutive floating-point registers. Keeping
  // the ABI form direct lets Clang expose the real and imaginary components
  // independently to the backend rather than treating the value as an
  // aggregate copy.
  if (Ty->isAnyComplexType()) {
    ABIArgInfo Info = ABIArgInfo::getDirect(CGT.ConvertType(Ty), /*Offset=*/0,
                                            /*Padding=*/nullptr,
                                            /*CanBeFlattened=*/false);
    // Preserve the FLOAT-PAIR grouping through IR argument legalization.
    Info.setInReg(true);
    return Info;
  }

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

  if (RetTy->isAnyComplexType())
    return ABIArgInfo::getDirect(CGT.ConvertType(RetTy), /*Offset=*/0,
                                 /*Padding=*/nullptr,
                                 /*CanBeFlattened=*/false);

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
