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

  class BedrockApproxUnaryIntrinsic
      : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>],
                  [IntrNoMem, IntrHasSideEffects]>;
  def int_bedrock_facosa : BedrockApproxUnaryIntrinsic;
  def int_bedrock_fasina : BedrockApproxUnaryIntrinsic;
  def int_bedrock_fatana : BedrockApproxUnaryIntrinsic;
  def int_bedrock_fatanha : BedrockApproxUnaryIntrinsic;
  def int_bedrock_fcosa : BedrockApproxUnaryIntrinsic;
  def int_bedrock_fcosha : BedrockApproxUnaryIntrinsic;
  def int_bedrock_fetoxa : BedrockApproxUnaryIntrinsic;
  def int_bedrock_fetoxm1a : BedrockApproxUnaryIntrinsic;
  def int_bedrock_flog10a : BedrockApproxUnaryIntrinsic;
  def int_bedrock_flog2a : BedrockApproxUnaryIntrinsic;
  def int_bedrock_flogna : BedrockApproxUnaryIntrinsic;
  def int_bedrock_flognp1a : BedrockApproxUnaryIntrinsic;
  def int_bedrock_fsina : BedrockApproxUnaryIntrinsic;
  def int_bedrock_fsinha : BedrockApproxUnaryIntrinsic;
  def int_bedrock_ftana : BedrockApproxUnaryIntrinsic;
  def int_bedrock_ftanha : BedrockApproxUnaryIntrinsic;
  def int_bedrock_ftentoxa : BedrockApproxUnaryIntrinsic;
  def int_bedrock_ftwotoxa : BedrockApproxUnaryIntrinsic;
  def int_bedrock_fsincosa
      : Intrinsic<[llvm_anyfloat_ty, LLVMMatchType<0>], [LLVMMatchType<0>],
                  [IntrNoMem, IntrHasSideEffects]>;

  def int_bedrock_write_status :
      Intrinsic<[], [llvm_i64_ty], [IntrNoMem, IntrHasSideEffects]>;
  def int_bedrock_read_control_register :
      Intrinsic<[llvm_i64_ty], [llvm_i32_ty],
                [IntrNoMem, IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;
  def int_bedrock_write_control_register :
      Intrinsic<[], [llvm_i32_ty, llvm_i64_ty],
                [IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;
  def int_bedrock_read_segment_register :
      Intrinsic<[llvm_i64_ty], [llvm_i32_ty],
                [IntrNoMem, IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;
  def int_bedrock_read_code_segment :
      Intrinsic<[llvm_i64_ty], [], [IntrNoMem, IntrHasSideEffects]>;
  def int_bedrock_write_segment_register :
      Intrinsic<[], [llvm_i32_ty, llvm_i64_ty],
                [IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;

  def int_bedrock_flush_dcache :
      Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty], [IntrHasSideEffects]>;
  def int_bedrock_invalidate_dcache :
      Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty], [IntrHasSideEffects]>;
  def int_bedrock_invalidate_icache :
      Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty], [IntrHasSideEffects]>;
  def int_bedrock_writeback_dcache :
      Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty], [IntrHasSideEffects]>;
  def int_bedrock_sync_cache :
      Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty], [IntrHasSideEffects]>;

  def int_bedrock_invalidate_tlb :
      Intrinsic<[], [], [IntrHasSideEffects]>;
  def int_bedrock_invalidate_page :
      Intrinsic<[], [llvm_ptr_ty], [IntrHasSideEffects]>;
  def int_bedrock_invalidate_asid :
      Intrinsic<[], [llvm_i32_ty],
                [IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;
  def int_bedrock_switch_page_table :
      Intrinsic<[], [llvm_i64_ty], [IntrHasSideEffects]>;
  def int_bedrock_switch_page_table_asid :
      Intrinsic<[], [llvm_i64_ty, llvm_i64_ty], [IntrHasSideEffects]>;
  def int_bedrock_virtual_to_physical :
      Intrinsic<[llvm_i64_ty, llvm_i64_ty], [llvm_i64_ty],
                [IntrNoMem, IntrHasSideEffects]>;
  def int_bedrock_page_table_query :
      Intrinsic<[llvm_i64_ty, llvm_i64_ty], [llvm_i32_ty, llvm_i64_ty],
                [IntrNoMem, IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;

  def int_bedrock_save_processor_state :
      Intrinsic<[], [llvm_ptr_ty], [IntrHasSideEffects]>;
  def int_bedrock_restore_processor_state :
      Intrinsic<[], [llvm_ptr_ty], [IntrHasSideEffects]>;

  // Vector memory and reduction operations whose semantics cannot be
  // represented by a target-independent scalable-vector IR operation.
  def int_bedrock_vector_strided_load :
      Intrinsic<[llvm_anyvector_ty],
                [llvm_ptr_ty, llvm_i64_ty, llvm_i64_ty],
                [IntrReadMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>]>;
  def int_bedrock_vector_strided_store :
      Intrinsic<[],
                [llvm_anyvector_ty, llvm_ptr_ty, llvm_i64_ty, llvm_i64_ty],
                [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<1>>]>;
  def int_bedrock_vector_reduce_add_i64 :
      Intrinsic<[llvm_i64_ty], [llvm_anyvector_ty], [IntrNoMem]>;
  def int_bedrock_vector_reduce_add_f64 :
      Intrinsic<[llvm_double_ty], [llvm_anyvector_ty], [IntrNoMem]>;
}
