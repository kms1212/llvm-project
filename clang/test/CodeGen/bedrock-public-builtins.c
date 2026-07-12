// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -std=c11 -ffreestanding -O1 -internal-isystem %S/../../lib/Headers -emit-llvm -o - %s | FileCheck %s

#include <bedrockintrin.h>

typedef int *__far far_int_ptr;

// CHECK: @far_constant ={{.*}} global ptr addrspace(1) inttoptr (i128 {{-?[0-9]+}} to ptr addrspace(1)), align 16
far_int_ptr far_constant =
    __BEDROCK_FAR_PTR_INIT(far_int_ptr, 0x1122334455667788ULL,
                          0x99aabbccddeeff00ULL);

// CHECK-LABEL: define{{.*}} ptr addrspace(1) @far_values
// CHECK: inttoptr i128
// CHECK: ret ptr addrspace(1)
far_int_ptr far_values(uint64_t address, uint64_t image) {
  far_int_ptr a = __BEDROCK_FAR_FLAT_PTR_INIT(far_int_ptr, address);
  far_int_ptr b = __BEDROCK_FAR_PTR_FROM_SEGMENT(far_int_ptr, address, image);
  far_int_ptr n = __BEDROCK_FAR_NULL(far_int_ptr);
  return __bedrock_far_same_encoding(a, n) ? b : a;
}

// CHECK-LABEL: define{{.*}} i64 @far_address
// CHECK: ptrtoint ptr addrspace(1) %{{.*}} to i128
// CHECK: trunc i128 %{{.*}} to i64
uint64_t far_address(far_int_ptr value) {
  return __bedrock_far_address(value);
}

// CHECK-LABEL: define{{.*}} i64 @core
// CHECK: call void @llvm.bedrock.trace(i32 7)
// CHECK: call void @llvm.bedrock.breakpoint()
// CHECK: call void @llvm.bedrock.yield()
// CHECK: call void @llvm.bedrock.wait()
// CHECK: call i64 @llvm.bedrock.cpuid(i64
// CHECK: call i64 @llvm.bedrock.rdpmc(i32 9)
// CHECK: call i64 @llvm.bedrock.read.status()
uint64_t core(uint64_t value) {
  __bedrock_trace(7);
  __bedrock_breakpoint();
  __bedrock_yield();
  __bedrock_wait();
  return __bedrock_cpuid(value) + __bedrock_rdpmc(9) +
         __bedrock_read_status();
}

// CHECK-LABEL: define{{.*}} void @memory
// CHECK: call void @llvm.bedrock.read.fence()
// CHECK: call void @llvm.bedrock.write.fence()
// CHECK: call void @llvm.bedrock.address.fence()
// CHECK: call void @llvm.bedrock.nontemporal.store.u8(ptr %{{.*}}, i64
// CHECK: call void @llvm.bedrock.nontemporal.store.u16(ptr %{{.*}}, i64
// CHECK: call void @llvm.bedrock.nontemporal.store.u32(ptr %{{.*}}, i64
// CHECK: call void @llvm.bedrock.nontemporal.store.u64(ptr %{{.*}}, i64
void memory(uint8_t *p8, uint16_t *p16, uint32_t *p32, uint64_t *p64,
            uint64_t value) {
  __bedrock_read_fence();
  __bedrock_write_fence();
  __bedrock_address_fence();
  __bedrock_nontemporal_store_u8(p8, value);
  __bedrock_nontemporal_store_u16(p16, value);
  __bedrock_nontemporal_store_u32(p32, value);
  __bedrock_nontemporal_store_u64(p64, value);
}

// CHECK-LABEL: define{{.*}} i64 @integer
// CHECK: call i64 @llvm.bedrock.clmul.u8
// CHECK: call i64 @llvm.bedrock.clmul.u16
// CHECK: call i64 @llvm.bedrock.clmul.u32
// CHECK: call i64 @llvm.bedrock.clmul.u64
uint64_t integer(uint64_t a, uint64_t b) {
  return __bedrock_clmul_u8(a, b) + __bedrock_clmul_u16(a, b) +
         __bedrock_clmul_u32(a, b) + __bedrock_clmul_u64(a, b);
}

// CHECK-LABEL: define{{.*}} i16 @fpu
// CHECK: call i64 @llvm.bedrock.read.fstatus()
// CHECK: call i64 @llvm.bedrock.read.fflags()
// CHECK: call void @llvm.bedrock.write.fstatus(i64
// CHECK: call void @llvm.bedrock.write.fflags(i64
// CHECK: call i64 @llvm.bedrock.fclass.f32(float
// CHECK: call i64 @llvm.bedrock.fclass.f64(double
uint16_t fpu(float a, double b) {
  uint16_t status = __bedrock_read_fstatus();
  uint16_t flags = __bedrock_read_fflags();
  __bedrock_write_fstatus(status);
  __bedrock_write_fflags(flags);
  return __bedrock_fclass_f32(a) | __bedrock_fclass_f64(b);
}
