; RUN: llc -mtriple=bedrock -O2 < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=bedrock -O2 -filetype=obj < %s -o %t
; RUN: llvm-objdump -d %t | FileCheck %s --check-prefix=OBJ

target triple = "bedrock"

define i64 @load_relaxed(ptr %p) {
; ASM-LABEL: load_relaxed:
; ASM-NOT: afence
; ASM: mov.q	[r0], r0
; ASM-NEXT: ret
  %v = load atomic i64, ptr %p monotonic, align 8
  ret i64 %v
}

define i64 @load_acquire(ptr %p) {
; ASM-LABEL: load_acquire:
; ASM: mov.q	[r0], r0
; ASM-NEXT: afence
; ASM-NEXT: ret
  %v = load atomic i64, ptr %p acquire, align 8
  ret i64 %v
}

define i64 @load_seq_cst(ptr %p) {
; ASM-LABEL: load_seq_cst:
; ASM: afence
; ASM-NEXT: mov.q	[r0], r0
; ASM-NEXT: afence
  %v = load atomic i64, ptr %p seq_cst, align 8
  ret i64 %v
}

define void @store_relaxed(ptr %p, i64 %v) {
; ASM-LABEL: store_relaxed:
; ASM-NOT: afence
; ASM: mov.q	r1, [r0]
; ASM-NEXT: ret
  store atomic i64 %v, ptr %p monotonic, align 8
  ret void
}

define void @store_release(ptr %p, i64 %v) {
; ASM-LABEL: store_release:
; ASM: afence
; ASM-NEXT: mov.q	r1, [r0]
; ASM-NEXT: ret
  store atomic i64 %v, ptr %p release, align 8
  ret void
}

define void @store_seq_cst(ptr %p, i64 %v) {
; ASM-LABEL: store_seq_cst:
; ASM: afence
; ASM-NEXT: mov.q	r1, [r0]
; ASM-NEXT: afence
  store atomic i64 %v, ptr %p seq_cst, align 8
  ret void
}

define void @thread_fences() {
; ASM-LABEL: thread_fences:
; ASM: afence
; ASM-NEXT: afence
; ASM-NEXT: afence
; ASM-NEXT: afence
  fence acquire
  fence release
  fence acq_rel
  fence seq_cst
  ret void
}

define i8 @fetch_add_b(ptr %p, i8 %v) {
; ASM-LABEL: fetch_add_b:
; ASM: fetchadd.b/relaxed
; OBJ: fetchadd.b/relaxed
  %old = atomicrmw add ptr %p, i8 %v monotonic, align 1
  ret i8 %old
}

define i16 @fetch_sub_w(ptr %p, i16 %v) {
; ASM-LABEL: fetch_sub_w:
; ASM: fetchsub.w/acquire
; OBJ: fetchsub.w/acquire
  %old = atomicrmw sub ptr %p, i16 %v acquire, align 2
  ret i16 %old
}

define i32 @fetch_and_l(ptr %p, i32 %v) {
; ASM-LABEL: fetch_and_l:
; ASM: fetchand.l/release
; OBJ: fetchand.l/release
  %old = atomicrmw and ptr %p, i32 %v release, align 4
  ret i32 %old
}

define i64 @fetch_or_q(ptr %p, i64 %v) {
; ASM-LABEL: fetch_or_q:
; ASM: fetchor.q/acqrel
; OBJ: fetchor.q/acqrel
  %old = atomicrmw or ptr %p, i64 %v acq_rel, align 8
  ret i64 %old
}

define i64 @fetch_xor_q(ptr %p, i64 %v) {
; ASM-LABEL: fetch_xor_q:
; ASM: fetchxor.q/seqcst
; OBJ: fetchxor.q/seqcst
  %old = atomicrmw xor ptr %p, i64 %v seq_cst, align 8
  ret i64 %old
}

define { i64, i1 } @compare_exchange(ptr %p, i64 %expected, i64 %desired) {
; ASM-LABEL: compare_exchange:
; ASM: cmpxchg.q/acqrel
; OBJ: cmpxchg.q/acqrel
  %result = cmpxchg ptr %p, i64 %expected, i64 %desired acq_rel acquire,
                    align 8
  ret { i64, i1 } %result
}

define i64 @exchange(ptr %p, i64 %value) {
; ASM-LABEL: exchange:
; ASM: [[LOOP:.LBB[0-9_]+]]:
; ASM: cmpxchg.q/seqcst
; ASM: jne	[[LOOP]]
  %old = atomicrmw xchg ptr %p, i64 %value seq_cst, align 8
  ret i64 %old
}
