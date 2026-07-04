// RUN: %clang_cc1 -triple bedrock-unknown-unknown -S -O1 \
// RUN:   -mframe-pointer=none -Wno-atomic-alignment \
// RUN:   -mllvm -verify-machineinstrs -o - %s \
// RUN:   | FileCheck %s

int load_relaxed(_Atomic int *p) {
  return __c11_atomic_load(p, __ATOMIC_RELAXED);
}

// CHECK-LABEL: load_relaxed:
// CHECK: MOV.L [A0], D0
// CHECK-NEXT: RET

int load_acquire(_Atomic int *p) {
  return __c11_atomic_load(p, __ATOMIC_ACQUIRE);
}

// CHECK-LABEL: load_acquire:
// CHECK: MOV.L [A0], D0
// CHECK-NEXT: AFENCE
// CHECK-NEXT: RET

void store_release(_Atomic int *p, int v) {
  __c11_atomic_store(p, v, __ATOMIC_RELEASE);
}

// CHECK-LABEL: store_release:
// CHECK: AFENCE
// CHECK-NEXT: MOV.L D0, [A0]
// CHECK-NEXT: RET

int add_seq(_Atomic int *p, int v) {
  return __c11_atomic_fetch_add(p, v, __ATOMIC_SEQ_CST);
}

// CHECK-LABEL: add_seq:
// CHECK: FETCHADD.L/SEQCST D0, [A0]
// CHECK-NEXT: RET

int xor_relaxed(_Atomic int *p, int v) {
  return __c11_atomic_fetch_xor(p, v, __ATOMIC_RELAXED);
}

// CHECK-LABEL: xor_relaxed:
// CHECK: FETCHXOR.L/RELAXED D0, [A0]
// CHECK-NEXT: RET

int exchange_seq(_Atomic int *p, int v) {
  return __c11_atomic_exchange(p, v, __ATOMIC_SEQ_CST);
}

// CHECK-LABEL: exchange_seq:
// CHECK: CMPXCHG.L/SEQCST
// CHECK: RET

int nand_seq(_Atomic int *p, int v) {
  return __c11_atomic_fetch_nand(p, v, __ATOMIC_SEQ_CST);
}

// CHECK-LABEL: nand_seq:
// CHECK: CMPXCHG.L/SEQCST
// CHECK: RET

int cmpx(_Atomic int *p, int *expected, int desired) {
  return __c11_atomic_compare_exchange_strong(p, expected, desired,
                                             __ATOMIC_ACQ_REL,
                                             __ATOMIC_ACQUIRE);
}

// CHECK-LABEL: cmpx:
// CHECK: MOV.L [A1],
// CHECK: CMPXCHG.L/ACQREL
// CHECK: MOV.L {{D[0-7]}}, [A1]
// CHECK: RET

int lock_free_4(void) {
  return __atomic_always_lock_free(4, 0);
}

// CHECK-LABEL: lock_free_4:
// CHECK: MOV.L 1, D0
// CHECK-NEXT: RET

int lock_free_16(void) {
  return __atomic_always_lock_free(16, 0);
}

// CHECK-LABEL: lock_free_16:
// CHECK: CLR.Q D0
// CHECK-NEXT: RET

typedef __int128 i128;

i128 load_i128(_Atomic i128 *p) {
  return __c11_atomic_load(p, __ATOMIC_SEQ_CST);
}

// CHECK-LABEL: load_i128:
// CHECK: CALL __atomic_load_16@PCREL16
// CHECK: RET

void store_i128(_Atomic i128 *p, i128 v) {
  __c11_atomic_store(p, v, __ATOMIC_SEQ_CST);
}

// CHECK-LABEL: store_i128:
// CHECK: CALL __atomic_store_16@PCREL16
// CHECK: RET

int cmpx_i128(_Atomic i128 *p, i128 *expected, i128 desired) {
  return __c11_atomic_compare_exchange_strong(p, expected, desired,
                                             __ATOMIC_ACQ_REL,
                                             __ATOMIC_ACQUIRE);
}

// CHECK-LABEL: cmpx_i128:
// CHECK: CALL __atomic_compare_exchange_16@PCREL16
// CHECK: RET
