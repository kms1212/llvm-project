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

namespace BedrockCABI {
#define BEDROCK_C_CALLING_CONVENTION(STACK_POINTER, GROWTH, ENTRY_ALIGNMENT,   \
                                     FIRST_ARGUMENT_OFFSET, ARGUMENT_SLOT,     \
                                     SRET_REGISTER, RED_ZONE)                  \
  static constexpr unsigned EntryAlignment = ENTRY_ALIGNMENT;                  \
  static constexpr unsigned ArgumentSlot = ARGUMENT_SLOT;
#include "llvm/TargetParser/BedrockGenCABI.inc"
#undef BEDROCK_C_CALLING_CONVENTION
} // namespace BedrockCABI

class BedrockABIInfo : public DefaultABIInfo {
  static constexpr unsigned StackSlotSize = BedrockCABI::ArgumentSlot;
  static constexpr unsigned MaxAggregateAlign = BedrockCABI::EntryAlignment;

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

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGenModule &CGM) const override;

  void checkFunctionABI(CodeGenModule &CGM,
                        const FunctionDecl *Decl) const override;
  void checkFunctionCallABI(CodeGenModule &CGM, SourceLocation CallLoc,
                            const FunctionDecl *Caller,
                            const FunctionDecl *Callee, const CallArgList &Args,
                            QualType ReturnType) const override;
};

} // namespace

enum class BedrockAggregateLayoutIssue {
  None,
  PackedOrUnderAligned,
  OverAligned,
  NonBaselineBitField,
  EmptyOrZeroLength,
};

static BedrockAggregateLayoutIssue getNonBaselineAggregateLayoutIssue(
    ASTContext &Context, QualType Ty,
    llvm::SmallPtrSetImpl<const RecordDecl *> &Visited) {
  Ty = Ty.getCanonicalType();
  if (const auto *ArrayTy = Context.getAsArrayType(Ty)) {
    if (const auto *CAT = dyn_cast<ConstantArrayType>(ArrayTy);
        CAT && CAT->getSize().isZero())
      return BedrockAggregateLayoutIssue::EmptyOrZeroLength;
    return getNonBaselineAggregateLayoutIssue(
        Context, ArrayTy->getElementType(), Visited);
  }

  const auto *RecordTy = Ty->getAs<RecordType>();
  if (!RecordTy)
    return BedrockAggregateLayoutIssue::None;
  const RecordDecl *Record = RecordTy->getDecl()->getDefinition();
  if (!Record || !Visited.insert(Record).second)
    return BedrockAggregateLayoutIssue::None;

  if (Record->hasAttr<PackedAttr>() || Record->hasAttr<MaxFieldAlignmentAttr>())
    return BedrockAggregateLayoutIssue::PackedOrUnderAligned;
  if (isEmptyRecord(Context, Ty, /*AllowArrays=*/true))
    return BedrockAggregateLayoutIssue::EmptyOrZeroLength;
  if (Context.getTypeAlign(Ty) > 128)
    return BedrockAggregateLayoutIssue::OverAligned;

  const ASTRecordLayout &Layout = Context.getASTRecordLayout(Record);
  unsigned Index = 0;
  for (const FieldDecl *Field : Record->fields()) {
    QualType FieldTy = Field->getType();
    if (Field->hasAttr<PackedAttr>())
      return BedrockAggregateLayoutIssue::PackedOrUnderAligned;

    if (Field->isBitField()) {
      QualType BaseTy = FieldTy.getCanonicalType().getUnqualifiedType();
      if (!BaseTy->isBooleanType() && BaseTy != Context.IntTy &&
          BaseTy != Context.UnsignedIntTy)
        return BedrockAggregateLayoutIssue::NonBaselineBitField;
    }

    if (!Field->isBitField()) {
      QualType AlignmentTy = FieldTy;
      if (const auto *ArrayTy = Context.getAsArrayType(FieldTy))
        AlignmentTy = ArrayTy->getElementType();
      unsigned NaturalAlign = Context.getTypeAlign(AlignmentTy);
      if (NaturalAlign && Layout.getFieldOffset(Index) % NaturalAlign != 0)
        return BedrockAggregateLayoutIssue::PackedOrUnderAligned;
    }

    BedrockAggregateLayoutIssue Nested = getNonBaselineAggregateLayoutIssue(
        Context, FieldTy, Visited);
    if (Nested != BedrockAggregateLayoutIssue::None)
      return Nested;
    ++Index;
  }
  return BedrockAggregateLayoutIssue::None;
}

static bool containsNonBaselineEnum(
    ASTContext &Context, QualType Ty,
    llvm::SmallPtrSetImpl<const RecordDecl *> &Visited) {
  Ty = Ty.getCanonicalType().getUnqualifiedType();
  if (const auto *EnumTy = Ty->getAs<EnumType>()) {
    QualType IntegerTy = EnumTy->getDecl()->getIntegerType();
    return !IntegerTy.isNull() &&
           !Context.hasSameType(IntegerTy.getCanonicalType().getUnqualifiedType(),
                                Context.IntTy);
  }
  if (const auto *ArrayTy = Context.getAsArrayType(Ty))
    return containsNonBaselineEnum(Context, ArrayTy->getElementType(),
                                   Visited);
  if (const auto *AtomicTy = Ty->getAs<AtomicType>())
    return containsNonBaselineEnum(Context, AtomicTy->getValueType(), Visited);

  const auto *RecordTy = Ty->getAs<RecordType>();
  if (!RecordTy)
    return false;
  const RecordDecl *Record = RecordTy->getDecl()->getDefinition();
  if (!Record || !Visited.insert(Record).second)
    return false;
  return llvm::any_of(Record->fields(), [&](const FieldDecl *Field) {
    return containsNonBaselineEnum(Context, Field->getType(), Visited);
  });
}

enum class BedrockNonBaselineTypeKind {
  None,
  BitInt,
  FixedVector,
  ScalableVector,
};

static BedrockNonBaselineTypeKind getNonBaselineExtendedType(
    ASTContext &Context, QualType Ty,
    llvm::SmallPtrSetImpl<const RecordDecl *> &Visited) {
  Ty = Ty.getCanonicalType().getUnqualifiedType();
  if (Ty->isBitIntType())
    return BedrockNonBaselineTypeKind::BitInt;
  if (Ty->isSizelessVectorType())
    return BedrockNonBaselineTypeKind::ScalableVector;
  if (Ty->isVectorType())
    return BedrockNonBaselineTypeKind::FixedVector;
  if (const auto *ArrayTy = Context.getAsArrayType(Ty))
    return getNonBaselineExtendedType(Context, ArrayTy->getElementType(),
                                      Visited);
  if (const auto *AtomicTy = Ty->getAs<AtomicType>())
    return getNonBaselineExtendedType(Context, AtomicTy->getValueType(),
                                      Visited);

  const auto *RecordTy = Ty->getAs<RecordType>();
  if (!RecordTy)
    return BedrockNonBaselineTypeKind::None;
  const RecordDecl *Record = RecordTy->getDecl()->getDefinition();
  if (!Record || !Visited.insert(Record).second)
    return BedrockNonBaselineTypeKind::None;
  for (const FieldDecl *Field : Record->fields()) {
    BedrockNonBaselineTypeKind Kind = getNonBaselineExtendedType(
        Context, Field->getType(), Visited);
    if (Kind != BedrockNonBaselineTypeKind::None)
      return Kind;
  }
  return BedrockNonBaselineTypeKind::None;
}

static void diagnoseNonBaselineTypes(CodeGenModule &CGM, SourceLocation Loc,
                                     ArrayRef<QualType> Types) {
  llvm::SmallPtrSet<const Type *, 4> Diagnosed;
  for (QualType Ty : Types) {
    if (!Diagnosed.insert(Ty.getCanonicalType().getTypePtr()).second)
      continue;

    llvm::SmallPtrSet<const RecordDecl *, 4> EnumVisited;
    if (containsNonBaselineEnum(CGM.getContext(), Ty, EnumVisited)) {
      std::string Description = Ty->isEnumeralType()
                                    ? "enum type with a non-int representation"
                                    : "type containing an enum with a non-int "
                                      "representation";
      CGM.Error(Loc, "Bedrock C ABI does not permit " + Description + " '" +
                         Ty.getAsString() +
                         "' across an external ABI boundary");
      continue;
    }

    llvm::SmallPtrSet<const RecordDecl *, 4> ExtensionVisited;
    BedrockNonBaselineTypeKind ExtensionKind = getNonBaselineExtendedType(
        CGM.getContext(), Ty, ExtensionVisited);
    if (ExtensionKind == BedrockNonBaselineTypeKind::ScalableVector &&
        CGM.getTarget().hasFeature("vector"))
      ExtensionKind = BedrockNonBaselineTypeKind::None;
    if (ExtensionKind != BedrockNonBaselineTypeKind::None) {
      StringRef Description =
          ExtensionKind == BedrockNonBaselineTypeKind::BitInt
              ? "bit-precise integer type without an extension ABI"
              : ExtensionKind == BedrockNonBaselineTypeKind::FixedVector
                    ? "fixed-length vector type without an extension ABI"
                    : "scalable vector type without the vector extension";
      std::string Message = "Bedrock C ABI does not permit " +
                            Description.str() + " '" + Ty.getAsString() +
                            "' across an external ABI boundary";
      CGM.Error(Loc, Message);
      continue;
    }

    if (!Ty->isAggregateType())
      continue;

    llvm::SmallPtrSet<const RecordDecl *, 4> Visited;
    BedrockAggregateLayoutIssue Issue = getNonBaselineAggregateLayoutIssue(
        CGM.getContext(), Ty, Visited);
    if (Issue == BedrockAggregateLayoutIssue::None)
      continue;
    StringRef Description;
    switch (Issue) {
    case BedrockAggregateLayoutIssue::None:
      llvm_unreachable("non-baseline aggregate issue expected");
    case BedrockAggregateLayoutIssue::PackedOrUnderAligned:
      Description = "packed or under-aligned aggregate type";
      break;
    case BedrockAggregateLayoutIssue::OverAligned:
      Description = "over-aligned aggregate type";
      break;
    case BedrockAggregateLayoutIssue::NonBaselineBitField:
      Description = "aggregate type with a non-baseline bit-field base type";
      break;
    case BedrockAggregateLayoutIssue::EmptyOrZeroLength:
      Description = "empty or zero-length aggregate type";
      break;
    }
    std::string Message = "Bedrock C ABI does not permit " +
                          Description.str() + " '" + Ty.getAsString() +
                          "' across an external ABI boundary";
    CGM.Error(Loc, Message);
  }
}

void BedrockTargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGenModule &CGM) const {
  const auto *VD = dyn_cast_or_null<VarDecl>(D);
  if (!VD || !VD->isExternallyVisible())
    return;
  diagnoseNonBaselineTypes(CGM, VD->getLocation(), {VD->getType()});
}

void BedrockTargetCodeGenInfo::checkFunctionABI(
    CodeGenModule &CGM, const FunctionDecl *Decl) const {
  if (!Decl->isExternallyVisible())
    return;
  SmallVector<QualType, 8> Types;
  Types.push_back(Decl->getReturnType());
  for (const ParmVarDecl *Param : Decl->parameters())
    Types.push_back(Param->getType());
  diagnoseNonBaselineTypes(CGM, Decl->getLocation(), Types);
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
  diagnoseNonBaselineTypes(CGM, CallLoc, Types);
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
  if (Ty->isSizelessVectorType()) {
    const auto *BT = Ty->castAs<BuiltinType>();
    if (getContext().getBuiltinVectorTypeInfo(BT).NumVectors == 1)
      return ABIArgInfo::getDirect();
    return ABIArgInfo::getIndirect(
        CharUnits::fromQuantity(MaxAggregateAlign),
        getDataLayout().getAllocaAddrSpace(), /*ByVal=*/false);
  }
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

  if (RetTy->isSizelessVectorType()) {
    const auto *BT = RetTy->castAs<BuiltinType>();
    if (getContext().getBuiltinVectorTypeInfo(BT).NumVectors == 1)
      return ABIArgInfo::getDirect();
    return ABIArgInfo::getIndirect(
        CharUnits::fromQuantity(MaxAggregateAlign),
        getDataLayout().getAllocaAddrSpace(), /*ByVal=*/false);
  }

  if (isAggregateTypeForABI(RetTy)) {
    if (isEmptyRecord(getContext(), RetTy, /*AllowArrays=*/true))
      return ABIArgInfo::getIgnore();

    if (getContext().getTypeSize(RetTy) <= 128)
      return ABIArgInfo::getDirect(getSmallAggregateCoerceType(RetTy));

    return ABIArgInfo::getIndirect(
        CharUnits::fromQuantity(MaxAggregateAlign),
        getDataLayout().getAllocaAddrSpace(), /*ByVal=*/false);
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
  bool IsIndirect = isAggregateTypeForABI(Ty) || Ty->isSizelessVectorType();
  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, IsIndirect, TInfo,
                          CharUnits::fromQuantity(StackSlotSize),
                          /*AllowHigherAlign=*/true, Slot);
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createBedrockTargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<BedrockTargetCodeGenInfo>(CGM.getTypes());
}
