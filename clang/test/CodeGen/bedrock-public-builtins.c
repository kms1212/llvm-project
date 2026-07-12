// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -std=c11 -ffreestanding -O1 -internal-isystem %S/../../lib/Headers -emit-llvm -o - %s | FileCheck %s

#include <bedrockintrin.h>

typedef int *__far far_int_ptr;

// CHECK: @far_constant ={{.*}} global ptr addrspace(1) inttoptr (i128 -136023984058069262664334284085100382328 to ptr addrspace(1)), align 16
// CHECK: @far_translated_constant ={{.*}} global ptr addrspace(1) inttoptr (i128 19342868454066287925032840 to ptr addrspace(1)), align 16
// CHECK: @far_null_constant ={{.*}} global ptr addrspace(1) null, align 16
// CHECK: @far_overflow_constant ={{.*}} global ptr addrspace(1) inttoptr (i128 -1 to ptr addrspace(1)), align 16
far_int_ptr far_constant =
    __BEDROCK_FAR_PTR_INIT(far_int_ptr, 0x1122334455667788ULL,
                          0x99aabbccddeeff02ULL);
far_int_ptr far_translated_constant = __BEDROCK_FAR_PTR_FROM_SEGMENT(
    far_int_ptr, 0x7788ULL, 0x0000000000100002ULL);
far_int_ptr far_null_constant =
    __BEDROCK_FAR_PTR_INIT(far_int_ptr, 0, 0x0000000000100002ULL);
far_int_ptr far_overflow_constant = __BEDROCK_FAR_PTR_FROM_SEGMENT(
    far_int_ptr, 0x1000ULL, 0xfffffffffffff002ULL);

// CHECK-LABEL: define{{.*}} ptr addrspace(1) @far_init_runtime
// CHECK: and i64 %image, 126
// CHECK: or i64 %image, %{{.*}}
// CHECK: icmp eq i64 %address, 0
// CHECK: select i1 %{{.*}}, ptr addrspace(1) null,
far_int_ptr far_init_runtime(uint64_t address, uint64_t image) {
  return __BEDROCK_FAR_PTR_INIT(far_int_ptr, address, image);
}

// CHECK-LABEL: define{{.*}} ptr addrspace(1) @far_from_segment_runtime
// CHECK: and i64 %image, -4096
// CHECK: call { i64, i1 } @llvm.uadd.with.overflow.i64
// CHECK: and i64 %image, 126
// CHECK: select i1 %{{.*}}, ptr addrspace(1) inttoptr (i128 -1 to ptr addrspace(1)),
// CHECK: ret ptr addrspace(1)
far_int_ptr far_from_segment_runtime(uint64_t offset, uint64_t image) {
  return __BEDROCK_FAR_PTR_FROM_SEGMENT(far_int_ptr, offset, image);
}

// CHECK-LABEL: define{{.*}} ptr addrspace(1) @far_flat_runtime
// CHECK: zext i64 %address to i128
// CHECK: inttoptr i128 %{{.*}} to ptr addrspace(1)
far_int_ptr far_flat_runtime(uint64_t address) {
  return __BEDROCK_FAR_FLAT_PTR_INIT(far_int_ptr, address);
}

// CHECK-LABEL: define{{.*}} ptr addrspace(1) @far_null_runtime
// CHECK: ret ptr addrspace(1) null
far_int_ptr far_null_runtime(void) {
  return __BEDROCK_FAR_NULL(far_int_ptr);
}

// CHECK-LABEL: define{{.*}} i32 @far_same
// CHECK: icmp eq ptr addrspace(1) %left, %right
int far_same(far_int_ptr left, far_int_ptr right) {
  return __bedrock_far_same_encoding(left, right);
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
