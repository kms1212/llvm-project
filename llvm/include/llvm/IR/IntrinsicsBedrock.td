//===- IntrinsicsBedrock.td - Bedrock intrinsics -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "bedrock" in {
  def int_bedrock_cpuid :
      Intrinsic<[llvm_i64_ty], [llvm_i64_ty], [IntrNoMem]>;
  def int_bedrock_read_status :
      Intrinsic<[llvm_i64_ty], [], [IntrNoMem, IntrHasSideEffects]>;
  def int_bedrock_rdpmc :
      Intrinsic<[llvm_i64_ty], [llvm_i32_ty],
                [IntrNoMem, IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;
  def int_bedrock_breakpoint :
      Intrinsic<[], [], [IntrNoMem, IntrHasSideEffects]>;
  def int_bedrock_trace :
      Intrinsic<[], [llvm_i32_ty],
                [IntrNoMem, IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;
  def int_bedrock_yield :
      Intrinsic<[], [], [IntrNoMem, IntrHasSideEffects]>;
  def int_bedrock_wait :
      Intrinsic<[], [], [IntrNoMem, IntrHasSideEffects, IntrConvergent]>;

  def int_bedrock_read_fence :
      Intrinsic<[], [], [IntrReadMem, IntrHasSideEffects]>;
  def int_bedrock_write_fence :
      Intrinsic<[], [], [IntrWriteMem, IntrHasSideEffects]>;
  def int_bedrock_address_fence
      : Intrinsic<[], [], [IntrHasSideEffects]>;

  def int_bedrock_nontemporal_store_u8
      : Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty],
                  [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>]>;
  def int_bedrock_nontemporal_store_u16
      : Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty],
                  [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>]>;
  def int_bedrock_nontemporal_store_u32
      : Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty],
                  [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>]>;
  def int_bedrock_nontemporal_store_u64
      : Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty],
                  [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>]>;

  def int_bedrock_clmul_u8 :
      Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty], [IntrNoMem]>;
  def int_bedrock_clmul_u16 :
      Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty], [IntrNoMem]>;
  def int_bedrock_clmul_u32 :
      Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty], [IntrNoMem]>;
  def int_bedrock_clmul_u64 :
      Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty], [IntrNoMem]>;

  def int_bedrock_read_fstatus
      : Intrinsic<[llvm_i64_ty], [], [IntrNoMem, IntrHasSideEffects]>;
  def int_bedrock_write_fstatus
      : Intrinsic<[], [llvm_i64_ty], [IntrNoMem, IntrHasSideEffects]>;
  def int_bedrock_read_fflags
      : Intrinsic<[llvm_i64_ty], [], [IntrNoMem, IntrHasSideEffects]>;
  def int_bedrock_write_fflags
      : Intrinsic<[], [llvm_i64_ty], [IntrNoMem, IntrHasSideEffects]>;
  def int_bedrock_fclass_f32 :
      Intrinsic<[llvm_i64_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_bedrock_fclass_f64 :
      Intrinsic<[llvm_i64_ty], [llvm_double_ty], [IntrNoMem]>;
}
