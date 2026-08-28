; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -mattr=+vector < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -mattr=+vector -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

define <vscale x 2 x i64> @ret_vec(<vscale x 2 x i64> %x) {
; CHECK-LABEL: ret_vec:
; CHECK: ret
  ret <vscale x 2 x i64> %x
}

define <vscale x 16 x i1> @ret_pred(<vscale x 16 x i1> %x) {
; CHECK-LABEL: ret_pred:
; CHECK: ret
  ret <vscale x 16 x i1> %x
}

define <vscale x 2 x i64> @ret_second_vec(<vscale x 2 x i64> %x,
                                           <vscale x 2 x i64> %y) {
; CHECK-LABEL: ret_second_vec:
; CHECK: vmov v1, v0
; CHECK: ret
  ret <vscale x 2 x i64> %y
}

define <vscale x 16 x i1> @ret_second_pred(<vscale x 16 x i1> %x,
                                            <vscale x 16 x i1> %y) {
; CHECK-LABEL: ret_second_pred:
; CHECK: pfalse p0
; CHECK-NEXT: por p1, p0
; CHECK: ret
  ret <vscale x 16 x i1> %y
}

declare <vscale x 2 x i64> @callee(<vscale x 2 x i64>,
                                    <vscale x 16 x i1>)

define <vscale x 2 x i64> @call_vec(<vscale x 2 x i64> %x,
                                    <vscale x 16 x i1> %p) {
; CHECK-LABEL: call_vec:
; CHECK: call callee
; CHECK: ret
  %r = call <vscale x 2 x i64> @callee(<vscale x 2 x i64> %x,
                                       <vscale x 16 x i1> %p)
  ret <vscale x 2 x i64> %r
}

declare void @clobber()
declare void @consume(<vscale x 2 x i64>, <vscale x 2 x i64>,
                      <vscale x 2 x i64>, <vscale x 2 x i64>,
                      <vscale x 2 x i64>, <vscale x 2 x i64>,
                      <vscale x 2 x i64>, <vscale x 2 x i64>,
                      <vscale x 16 x i1>, <vscale x 16 x i1>,
                      <vscale x 16 x i1>, <vscale x 16 x i1>)

define void @live_across_call(
    <vscale x 2 x i64> %v0, <vscale x 2 x i64> %v1,
    <vscale x 2 x i64> %v2, <vscale x 2 x i64> %v3,
    <vscale x 2 x i64> %v4, <vscale x 2 x i64> %v5,
    <vscale x 2 x i64> %v6, <vscale x 2 x i64> %v7,
    <vscale x 16 x i1> %p0, <vscale x 16 x i1> %p1,
    <vscale x 16 x i1> %p2, <vscale x 16 x i1> %p3) {
; CHECK-LABEL: live_across_call:
; CHECK: call clobber
; CHECK: call consume
; OBJ-LABEL: <live_across_call>:
; OBJ: pmov p15,
; OBJ: ptrue.b p15
; OBJ: vmov.b p15, v{{[0-9]+}},
; OBJ: vmov.b p15,
; OBJ: pmov {{.*}}, p15
; OBJ: pmov p{{[0-9]+}},
; OBJ: pmov {{.*}}, p{{[0-9]+}}
  call void @clobber()
  call void @consume(
      <vscale x 2 x i64> %v0, <vscale x 2 x i64> %v1,
      <vscale x 2 x i64> %v2, <vscale x 2 x i64> %v3,
      <vscale x 2 x i64> %v4, <vscale x 2 x i64> %v5,
      <vscale x 2 x i64> %v6, <vscale x 2 x i64> %v7,
      <vscale x 16 x i1> %p0, <vscale x 16 x i1> %p1,
      <vscale x 16 x i1> %p2, <vscale x 16 x i1> %p3)
  ret void
}

declare void @consume_nine_vectors(
    <vscale x 2 x i64>, <vscale x 2 x i64>, <vscale x 2 x i64>,
    <vscale x 2 x i64>, <vscale x 2 x i64>, <vscale x 2 x i64>,
    <vscale x 2 x i64>, <vscale x 2 x i64>, <vscale x 2 x i64>)

define void @vector_cursor_exhaustion(
    <vscale x 2 x i64> %v0, <vscale x 2 x i64> %v1,
    <vscale x 2 x i64> %v2, <vscale x 2 x i64> %v3,
    <vscale x 2 x i64> %v4, <vscale x 2 x i64> %v5,
    <vscale x 2 x i64> %v6, <vscale x 2 x i64> %v7,
    <vscale x 2 x i64> %v8) {
; CHECK-LABEL: vector_cursor_exhaustion:
; CHECK: call consume_nine_vectors
; OBJ-LABEL: <vector_cursor_exhaustion>:
; OBJ: vmov.q p7, v8, [sp + 8]
; OBJ: call
  call void @consume_nine_vectors(
      <vscale x 2 x i64> %v0, <vscale x 2 x i64> %v1,
      <vscale x 2 x i64> %v2, <vscale x 2 x i64> %v3,
      <vscale x 2 x i64> %v4, <vscale x 2 x i64> %v5,
      <vscale x 2 x i64> %v6, <vscale x 2 x i64> %v7,
      <vscale x 2 x i64> %v8)
  ret void
}

declare void @consume_five_predicates(
    <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1>,
    <vscale x 16 x i1>, <vscale x 16 x i1>)

define void @predicate_cursor_exhaustion(
    <vscale x 16 x i1> %p0, <vscale x 16 x i1> %p1,
    <vscale x 16 x i1> %p2, <vscale x 16 x i1> %p3,
    <vscale x 16 x i1> %p4) {
; CHECK-LABEL: predicate_cursor_exhaustion:
; CHECK: call consume_five_predicates
; OBJ-LABEL: <predicate_cursor_exhaustion>:
; OBJ: pmov [r0], p4
; OBJ: pmov p4, [sp + 8]
; OBJ: call
  call void @consume_five_predicates(
      <vscale x 16 x i1> %p0, <vscale x 16 x i1> %p1,
      <vscale x 16 x i1> %p2, <vscale x 16 x i1> %p3,
      <vscale x 16 x i1> %p4)
  ret void
}

declare void @vector_variadic(i64, ...)

define void @variadic_vector_pointer(<vscale x 2 x i64> %value) {
; CHECK-LABEL: variadic_vector_pointer:
; CHECK: call vector_variadic
; OBJ-LABEL: <variadic_vector_pointer>:
; OBJ: vmov.q p7, v0, [sp + 24]
; OBJ: mov.q {{.*}}, [{{.*}} + 8]
; OBJ: call
  call void (i64, ...) @vector_variadic(i64 1,
                                         <vscale x 2 x i64> %value)
  ret void
}

declare void @vector_unprototyped(...)

define void @unprototyped_vector_pointer(<vscale x 2 x i64> %value) {
; CHECK-LABEL: unprototyped_vector_pointer:
; CHECK: call vector_unprototyped
; OBJ-LABEL: <unprototyped_vector_pointer>:
; OBJ: vmov.q p7, v0, [sp + 24]
; OBJ: mov.q {{.*}}, [{{.*}} + 8]
; OBJ: call
  call void (...) @vector_unprototyped(<vscale x 2 x i64> %value)
  ret void
}
