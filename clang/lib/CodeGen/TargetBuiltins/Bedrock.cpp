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

#define BEDROCK_APPROX_CASES(NAME)                                           \
  case Bedrock::BI__builtin_bedrock_##NAME##_f32:                           \
  case Bedrock::BI__builtin_bedrock_##NAME##_f64:
    BEDROCK_APPROX_CASES(facosa)
    BEDROCK_APPROX_CASES(fasina)
    BEDROCK_APPROX_CASES(fatana)
    BEDROCK_APPROX_CASES(fatanha)
    BEDROCK_APPROX_CASES(fcosa)
    BEDROCK_APPROX_CASES(fcosha)
    BEDROCK_APPROX_CASES(fetoxa)
    BEDROCK_APPROX_CASES(fetoxm1a)
    BEDROCK_APPROX_CASES(flog10a)
    BEDROCK_APPROX_CASES(flog2a)
    BEDROCK_APPROX_CASES(flogna)
    BEDROCK_APPROX_CASES(flognp1a)
    BEDROCK_APPROX_CASES(fsina)
    BEDROCK_APPROX_CASES(fsinha)
    BEDROCK_APPROX_CASES(ftana)
    BEDROCK_APPROX_CASES(ftanha)
    BEDROCK_APPROX_CASES(ftentoxa)
    BEDROCK_APPROX_CASES(ftwotoxa) {
      Intrinsic::ID ID;
      switch (BuiltinID) {
#define BEDROCK_APPROX_ID(NAME)                                              \
  case Bedrock::BI__builtin_bedrock_##NAME##_f32:                           \
  case Bedrock::BI__builtin_bedrock_##NAME##_f64:                           \
    ID = Intrinsic::bedrock_##NAME;                                         \
    break;
        BEDROCK_APPROX_ID(facosa)
        BEDROCK_APPROX_ID(fasina)
        BEDROCK_APPROX_ID(fatana)
        BEDROCK_APPROX_ID(fatanha)
        BEDROCK_APPROX_ID(fcosa)
        BEDROCK_APPROX_ID(fcosha)
        BEDROCK_APPROX_ID(fetoxa)
        BEDROCK_APPROX_ID(fetoxm1a)
        BEDROCK_APPROX_ID(flog10a)
        BEDROCK_APPROX_ID(flog2a)
        BEDROCK_APPROX_ID(flogna)
        BEDROCK_APPROX_ID(flognp1a)
        BEDROCK_APPROX_ID(fsina)
        BEDROCK_APPROX_ID(fsinha)
        BEDROCK_APPROX_ID(ftana)
        BEDROCK_APPROX_ID(ftanha)
        BEDROCK_APPROX_ID(ftentoxa)
        BEDROCK_APPROX_ID(ftwotoxa)
#undef BEDROCK_APPROX_ID
      default:
        llvm_unreachable("unexpected Bedrock approximate builtin");
      }
      Value *Arg = EmitScalarExpr(E->getArg(0));
      return Builder.CreateCall(CGM.getIntrinsic(ID, {Arg->getType()}), {Arg});
    }
#undef BEDROCK_APPROX_CASES

  case Bedrock::BI__builtin_bedrock_fsincosa_f32:
  case Bedrock::BI__builtin_bedrock_fsincosa_f64: {
    Value *Arg = EmitScalarExpr(E->getArg(0));
    CallInst *Pair = Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::bedrock_fsincosa, {Arg->getType()}),
        {Arg});
    Builder.CreateDefaultAlignedStore(Builder.CreateExtractValue(Pair, 0),
                                      EmitScalarExpr(E->getArg(1)));
    return Builder.CreateDefaultAlignedStore(Builder.CreateExtractValue(Pair, 1),
                                             EmitScalarExpr(E->getArg(2)));
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
  case Bedrock::BI__builtin_bedrock_read_code_segment:
    return EmitCall(Intrinsic::bedrock_read_code_segment);
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
  }
  return nullptr;
}
