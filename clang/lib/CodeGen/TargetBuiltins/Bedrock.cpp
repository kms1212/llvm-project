//===--- Bedrock.cpp - Emit LLVM code for Bedrock builtins ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/IR/IntrinsicsBedrock.h"

using namespace clang;
using namespace CodeGen;
using namespace llvm;

Value *CodeGenFunction::EmitBedrockBuiltinExpr(unsigned BuiltinID,
                                               const CallExpr *E) {
  auto EmitI64 = [&](unsigned Arg) {
    Value *V = EmitScalarExpr(E->getArg(Arg));
    return Builder.CreateZExtOrTrunc(V, Builder.getInt64Ty());
  };
  auto EmitCall = [&](Intrinsic::ID ID, ArrayRef<Value *> Args = {}) {
    return Builder.CreateCall(CGM.getIntrinsic(ID), Args);
  };
  auto TruncateResult = [&](Value *V) {
    llvm::Type *ResultTy = ConvertType(E->getType());
    return ResultTy == V->getType() ? V
                                    : Builder.CreateZExtOrTrunc(V, ResultTy);
  };

  switch (BuiltinID) {
  case Bedrock::BI__builtin_bedrock_far_ptr_init:
  case Bedrock::BI__builtin_bedrock_far_ptr_from_segment: {
    Value *Address = EmitI64(1);
    Value *Image = EmitI64(2);
    Value *Raw = Builder.CreateOr(
        Builder.CreateZExt(Address, Builder.getInt128Ty()),
        Builder.CreateShl(Builder.CreateZExt(Image, Builder.getInt128Ty()),
                          64));
    return Builder.CreateIntToPtr(Raw, ConvertType(E->getType()));
  }
  case Bedrock::BI__builtin_bedrock_far_flat_ptr_init: {
    Value *Raw = Builder.CreateZExt(EmitI64(1), Builder.getInt128Ty());
    return Builder.CreateIntToPtr(Raw, ConvertType(E->getType()));
  }
  case Bedrock::BI__builtin_bedrock_far_null:
    return ConstantPointerNull::get(
        cast<llvm::PointerType>(ConvertType(E->getType())));
  case Bedrock::BI__builtin_bedrock_far_address: {
    Value *Raw = Builder.CreatePtrToInt(EmitScalarExpr(E->getArg(0)),
                                        Builder.getInt128Ty());
    return Builder.CreateTrunc(Raw, Builder.getInt64Ty());
  }
  case Bedrock::BI__builtin_bedrock_far_same_encoding: {
    Value *Left = Builder.CreatePtrToInt(EmitScalarExpr(E->getArg(0)),
                                         Builder.getInt128Ty());
    Value *Right = Builder.CreatePtrToInt(EmitScalarExpr(E->getArg(1)),
                                          Builder.getInt128Ty());
    return Builder.CreateZExt(Builder.CreateICmpEQ(Left, Right),
                              Builder.getInt32Ty());
  }

  case Bedrock::BI__builtin_bedrock_cpuid:
    return EmitCall(Intrinsic::bedrock_cpuid, {EmitI64(0)});
  case Bedrock::BI__builtin_bedrock_read_status:
    return TruncateResult(EmitCall(Intrinsic::bedrock_read_status));
  case Bedrock::BI__builtin_bedrock_rdpmc:
    return EmitCall(Intrinsic::bedrock_rdpmc, {EmitScalarExpr(E->getArg(0))});
  case Bedrock::BI__builtin_bedrock_breakpoint:
    return EmitCall(Intrinsic::bedrock_breakpoint);
  case Bedrock::BI__builtin_bedrock_trace:
    return EmitCall(Intrinsic::bedrock_trace, {EmitScalarExpr(E->getArg(0))});
  case Bedrock::BI__builtin_bedrock_yield:
    return EmitCall(Intrinsic::bedrock_yield);
  case Bedrock::BI__builtin_bedrock_wait:
    return EmitCall(Intrinsic::bedrock_wait);
  case Bedrock::BI__builtin_bedrock_read_fence:
    return EmitCall(Intrinsic::bedrock_read_fence);
  case Bedrock::BI__builtin_bedrock_write_fence:
    return EmitCall(Intrinsic::bedrock_write_fence);
  case Bedrock::BI__builtin_bedrock_address_fence:
    return EmitCall(Intrinsic::bedrock_address_fence);

  case Bedrock::BI__builtin_bedrock_nontemporal_store_u8:
  case Bedrock::BI__builtin_bedrock_nontemporal_store_u16:
  case Bedrock::BI__builtin_bedrock_nontemporal_store_u32:
  case Bedrock::BI__builtin_bedrock_nontemporal_store_u64: {
    Intrinsic::ID ID = Intrinsic::bedrock_nontemporal_store_u8;
    if (BuiltinID == Bedrock::BI__builtin_bedrock_nontemporal_store_u16)
      ID = Intrinsic::bedrock_nontemporal_store_u16;
    else if (BuiltinID == Bedrock::BI__builtin_bedrock_nontemporal_store_u32)
      ID = Intrinsic::bedrock_nontemporal_store_u32;
    else if (BuiltinID == Bedrock::BI__builtin_bedrock_nontemporal_store_u64)
      ID = Intrinsic::bedrock_nontemporal_store_u64;
    return EmitCall(ID, {EmitScalarExpr(E->getArg(0)), EmitI64(1)});
  }

  case Bedrock::BI__builtin_bedrock_clmul_u8:
  case Bedrock::BI__builtin_bedrock_clmul_u16:
  case Bedrock::BI__builtin_bedrock_clmul_u32:
  case Bedrock::BI__builtin_bedrock_clmul_u64: {
    Intrinsic::ID ID = Intrinsic::bedrock_clmul_u8;
    if (BuiltinID == Bedrock::BI__builtin_bedrock_clmul_u16)
      ID = Intrinsic::bedrock_clmul_u16;
    else if (BuiltinID == Bedrock::BI__builtin_bedrock_clmul_u32)
      ID = Intrinsic::bedrock_clmul_u32;
    else if (BuiltinID == Bedrock::BI__builtin_bedrock_clmul_u64)
      ID = Intrinsic::bedrock_clmul_u64;
    return TruncateResult(EmitCall(ID, {EmitI64(0), EmitI64(1)}));
  }

  case Bedrock::BI__builtin_bedrock_read_fstatus:
    return TruncateResult(EmitCall(Intrinsic::bedrock_read_fstatus));
  case Bedrock::BI__builtin_bedrock_write_fstatus:
    return EmitCall(Intrinsic::bedrock_write_fstatus, {EmitI64(0)});
  case Bedrock::BI__builtin_bedrock_read_fflags:
    return TruncateResult(EmitCall(Intrinsic::bedrock_read_fflags));
  case Bedrock::BI__builtin_bedrock_write_fflags:
    return EmitCall(Intrinsic::bedrock_write_fflags, {EmitI64(0)});
  case Bedrock::BI__builtin_bedrock_fclass_f32:
  case Bedrock::BI__builtin_bedrock_fclass_f64: {
    Intrinsic::ID ID = BuiltinID == Bedrock::BI__builtin_bedrock_fclass_f32
                           ? Intrinsic::bedrock_fclass_f32
                           : Intrinsic::bedrock_fclass_f64;
    return TruncateResult(EmitCall(ID, {EmitScalarExpr(E->getArg(0))}));
  }
  }
  return nullptr;
}
