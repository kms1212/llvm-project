// RUN: %clang_cc1 -triple bedrock -std=c11 -O1 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple bedrock -std=c11 -O1 -S -o - %s | FileCheck %s --check-prefix=ASM

typedef _Atomic(unsigned char) atomic_u8;
typedef _Atomic(unsigned short) atomic_u16;
typedef _Atomic(unsigned int) atomic_u32;
typedef _Atomic(unsigned long) atomic_u64;

unsigned long load_acquire(atomic_u64 *p) {
// CHECK-LABEL: define{{.*}} i64 @load_acquire
// CHECK: load atomic i64, ptr %{{.*}} acquire, align 8
// ASM-LABEL: load_acquire:
// ASM: mov.q	[r0], r0
// ASM-NEXT: afence
  return __c11_atomic_load(p, __ATOMIC_ACQUIRE);
}

void store_release(atomic_u64 *p, unsigned long value) {
// CHECK-LABEL: define{{.*}} void @store_release
// CHECK: store atomic i64 %{{.*}}, ptr %{{.*}} release, align 8
// ASM-LABEL: store_release:
// ASM: afence
// ASM-NEXT: mov.q	r1, [r0]
  __c11_atomic_store(p, value, __ATOMIC_RELEASE);
}

unsigned char add8(atomic_u8 *p, unsigned char value) {
// CHECK-LABEL: define{{.*}} i8 @add8
// CHECK: atomicrmw add ptr %{{.*}}, i8 %{{.*}} monotonic, align 1
// ASM-LABEL: add8:
// ASM: fetchadd.b/relaxed
  return __c11_atomic_fetch_add(p, value, __ATOMIC_RELAXED);
}

unsigned short sub16(atomic_u16 *p, unsigned short value) {
// CHECK-LABEL: define{{.*}} i16 @sub16
// CHECK: atomicrmw sub ptr %{{.*}}, i16 %{{.*}} acquire, align 2
// ASM-LABEL: sub16:
// ASM: fetchsub.w/acquire
  return __c11_atomic_fetch_sub(p, value, __ATOMIC_ACQUIRE);
}

unsigned int and32(atomic_u32 *p, unsigned int value) {
// CHECK-LABEL: define{{.*}} i32 @and32
// CHECK: atomicrmw and ptr %{{.*}}, i32 %{{.*}} release, align 4
// ASM-LABEL: and32:
// ASM: fetchand.l/release
  return __c11_atomic_fetch_and(p, value, __ATOMIC_RELEASE);
}

unsigned long or64(atomic_u64 *p, unsigned long value) {
// CHECK-LABEL: define{{.*}} i64 @or64
// CHECK: atomicrmw or ptr %{{.*}}, i64 %{{.*}} acq_rel, align 8
// ASM-LABEL: or64:
// ASM: fetchor.q/acqrel
  return __c11_atomic_fetch_or(p, value, __ATOMIC_ACQ_REL);
}

unsigned long xor64(atomic_u64 *p, unsigned long value) {
// CHECK-LABEL: define{{.*}} i64 @xor64
// CHECK: atomicrmw xor ptr %{{.*}}, i64 %{{.*}} seq_cst, align 8
// ASM-LABEL: xor64:
// ASM: fetchxor.q/seqcst
  return __c11_atomic_fetch_xor(p, value, __ATOMIC_SEQ_CST);
}

unsigned long exchange64(atomic_u64 *p, unsigned long value) {
// CHECK-LABEL: define{{.*}} i64 @exchange64
// CHECK: atomicrmw xchg ptr %{{.*}}, i64 %{{.*}} seq_cst, align 8
// ASM-LABEL: exchange64:
// ASM: cmpxchg.q/seqcst
  return __c11_atomic_exchange(p, value, __ATOMIC_SEQ_CST);
}

_Bool compare_exchange64(atomic_u64 *p, unsigned long *expected,
                         unsigned long desired) {
// CHECK-LABEL: define{{.*}} i1 @compare_exchange64
// CHECK: cmpxchg ptr %{{.*}}, i64 %{{.*}}, i64 %{{.*}} acq_rel acquire, align 8
// ASM-LABEL: compare_exchange64:
// ASM: cmpxchg.q/acqrel
  return __c11_atomic_compare_exchange_strong(
      p, expected, desired, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

// CHECK-NOT: __atomic_
