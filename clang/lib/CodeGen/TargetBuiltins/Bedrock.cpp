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
  auto ToBoundsOnlyImage = [&](Value *Image) {
    Value *HasMantissa = Builder.CreateICmpNE(
        Builder.CreateAnd(Image, Builder.getInt64(0x7e)), Builder.getInt64(0));
    return Builder.CreateOr(
        Image, Builder.CreateZExt(HasMantissa, Builder.getInt64Ty()));
  };
  auto EmitFarPointer = [&](Value *Address, Value *Image,
                            Value *ForceInvalid = nullptr) {
    Value *Raw = Builder.CreateOr(
        Builder.CreateZExt(Address, Builder.getInt128Ty()),
        Builder.CreateShl(Builder.CreateZExt(Image, Builder.getInt128Ty()),
                          64));
    Value *IsNull = Builder.CreateICmpEQ(Address, Builder.getInt64(0));
    if (ForceInvalid)
      IsNull = Builder.CreateAnd(IsNull, Builder.CreateNot(ForceInvalid));
    Raw = Builder.CreateSelect(IsNull,
                               ConstantInt::get(Builder.getInt128Ty(), 0), Raw);
    if (ForceInvalid)
      Raw = Builder.CreateSelect(
          ForceInvalid, ConstantInt::getAllOnesValue(Builder.getInt128Ty()),
          Raw);
    return Builder.CreateIntToPtr(Raw, ConvertType(E->getType()));
  };

  switch (BuiltinID) {
  case Bedrock::BI__builtin_bedrock_far_ptr_init: {
    Value *Address = EmitI64(1);
    return EmitFarPointer(Address, ToBoundsOnlyImage(EmitI64(2)));
  }
  case Bedrock::BI__builtin_bedrock_far_ptr_from_segment: {
    Value *Offset = EmitI64(1);
    Value *Image = EmitI64(2);
    Value *Base = Builder.CreateAnd(
        Image, Builder.getInt64(UINT64_C(0xfffffffffffff000)));
    Value *SumAndOverflow = Builder.CreateBinaryIntrinsic(
        Intrinsic::uadd_with_overflow, Base, Offset);
    Value *Address = Builder.CreateExtractValue(SumAndOverflow, 0);
    Value *Overflow = Builder.CreateExtractValue(SumAndOverflow, 1);
    return EmitFarPointer(Address, ToBoundsOnlyImage(Image), Overflow);
  }
  case Bedrock::BI__builtin_bedrock_far_flat_ptr_init: {
    return EmitFarPointer(EmitI64(1), Builder.getInt64(0));
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

  case Bedrock::BI__builtin_bedrock_write_status:
    return EmitCall(Intrinsic::bedrock_write_status, {EmitI64(0)});
  case Bedrock::BI__builtin_bedrock_read_control_register:
    return EmitCall(Intrinsic::bedrock_read_control_register,
                    {EmitScalarExpr(E->getArg(0))});
  case Bedrock::BI__builtin_bedrock_write_control_register:
    return EmitCall(Intrinsic::bedrock_write_control_register,
                    {EmitScalarExpr(E->getArg(0)), EmitI64(1)});
  case Bedrock::BI__builtin_bedrock_read_segment_register:
    return EmitCall(Intrinsic::bedrock_read_segment_register,
                    {EmitScalarExpr(E->getArg(0))});
  case Bedrock::BI__builtin_bedrock_write_segment_register:
    return EmitCall(Intrinsic::bedrock_write_segment_register,
                    {EmitScalarExpr(E->getArg(0)), EmitI64(1)});

  case Bedrock::BI__builtin_bedrock_flush_dcache:
  case Bedrock::BI__builtin_bedrock_invalidate_dcache:
  case Bedrock::BI__builtin_bedrock_invalidate_icache:
  case Bedrock::BI__builtin_bedrock_writeback_dcache:
  case Bedrock::BI__builtin_bedrock_sync_cache: {
    Intrinsic::ID ID = Intrinsic::bedrock_flush_dcache;
    if (BuiltinID == Bedrock::BI__builtin_bedrock_invalidate_dcache)
      ID = Intrinsic::bedrock_invalidate_dcache;
    else if (BuiltinID == Bedrock::BI__builtin_bedrock_invalidate_icache)
      ID = Intrinsic::bedrock_invalidate_icache;
    else if (BuiltinID == Bedrock::BI__builtin_bedrock_writeback_dcache)
      ID = Intrinsic::bedrock_writeback_dcache;
    else if (BuiltinID == Bedrock::BI__builtin_bedrock_sync_cache)
      ID = Intrinsic::bedrock_sync_cache;
    Value *Address = EmitScalarExpr(E->getArg(0));
    Value *Length = EmitI64(1);
    if (const auto *CI = dyn_cast<ConstantInt>(Length); CI && CI->isZero())
      return EmitCall(Intrinsic::donothing);

    BasicBlock *Preheader = Builder.GetInsertBlock();
    BasicBlock *Loop = createBasicBlock("bedrock.cache.range");
    BasicBlock *Exit = createBasicBlock("bedrock.cache.done");
    Builder.CreateCondBr(Builder.CreateICmpNE(Length, Builder.getInt64(0)),
                         Loop, Exit);

    EmitBlock(Loop);
    PHINode *CurrentAddress = Builder.CreatePHI(Address->getType(), 2);
    PHINode *Remaining = Builder.CreatePHI(Builder.getInt64Ty(), 2);
    CurrentAddress->addIncoming(Address, Preheader);
    Remaining->addIncoming(Length, Preheader);
    Value *CacheOp = EmitCall(ID, {CurrentAddress, Builder.getInt64(1)});
    Value *NextAddress = Builder.CreateGEP(Builder.getInt8Ty(), CurrentAddress,
                                           Builder.getInt64(1));
    Value *NextRemaining = Builder.CreateSub(Remaining, Builder.getInt64(1));
    Builder.CreateCondBr(
        Builder.CreateICmpNE(NextRemaining, Builder.getInt64(0)), Loop, Exit);
    CurrentAddress->addIncoming(NextAddress, Builder.GetInsertBlock());
    Remaining->addIncoming(NextRemaining, Builder.GetInsertBlock());

    EmitBlock(Exit);
    return CacheOp;
  }

  case Bedrock::BI__builtin_bedrock_invalidate_tlb:
    return EmitCall(Intrinsic::bedrock_invalidate_tlb);
  case Bedrock::BI__builtin_bedrock_invalidate_page:
    return EmitCall(Intrinsic::bedrock_invalidate_page,
                    {EmitScalarExpr(E->getArg(0))});
  case Bedrock::BI__builtin_bedrock_invalidate_asid:
    return EmitCall(Intrinsic::bedrock_invalidate_asid,
                    {EmitScalarExpr(E->getArg(0))});
  case Bedrock::BI__builtin_bedrock_switch_page_table:
    return EmitCall(Intrinsic::bedrock_switch_page_table, {EmitI64(0)});
  case Bedrock::BI__builtin_bedrock_switch_page_table_asid:
    return EmitCall(Intrinsic::bedrock_switch_page_table_asid,
                    {EmitI64(0), EmitI64(1)});
  case Bedrock::BI__builtin_bedrock_virtual_to_physical:
  case Bedrock::BI__builtin_bedrock_page_table_query: {
    CallInst *Query;
    unsigned ValuePointerArg;
    if (BuiltinID == Bedrock::BI__builtin_bedrock_virtual_to_physical) {
      Query = cast<CallInst>(
          EmitCall(Intrinsic::bedrock_virtual_to_physical, {EmitI64(0)}));
      ValuePointerArg = 1;
    } else {
      Query =
          cast<CallInst>(EmitCall(Intrinsic::bedrock_page_table_query,
                                  {EmitScalarExpr(E->getArg(0)), EmitI64(1)}));
      ValuePointerArg = 2;
    }
    Value *ValueResult = Builder.CreateExtractValue(Query, 0);
    Value *FlagsResult = Builder.CreateExtractValue(Query, 1);
    Builder.CreateDefaultAlignedStore(
        ValueResult, EmitScalarExpr(E->getArg(ValuePointerArg)));
    return Builder.CreateDefaultAlignedStore(
        Builder.CreateTrunc(FlagsResult, Builder.getInt16Ty()),
        EmitScalarExpr(E->getArg(ValuePointerArg + 1)));
  }

  case Bedrock::BI__builtin_bedrock_save_processor_state:
    return EmitCall(Intrinsic::bedrock_save_processor_state,
                    {EmitScalarExpr(E->getArg(0))});
  case Bedrock::BI__builtin_bedrock_restore_processor_state:
    return EmitCall(Intrinsic::bedrock_restore_processor_state,
                    {EmitScalarExpr(E->getArg(0))});
  case Bedrock::BI__builtin_bedrock_encode_instruction:
    return EmitCall(Intrinsic::bedrock_encode_instruction,
                    {EmitScalarExpr(E->getArg(0)), EmitI64(1), EmitI64(2),
                     EmitI64(3), EmitI64(4)});
  }
  return nullptr;
}
