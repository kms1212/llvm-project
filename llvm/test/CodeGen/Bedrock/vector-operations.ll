; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -mattr=+vectorfp < %s | FileCheck %s
; RUN: not --crash llc -mtriple=bedrock -mattr=-vector < %s 2>&1 | FileCheck %s --check-prefix=NO-VECTOR
; RUN: not --crash llc -mtriple=bedrock -mattr=+vector,-vectorfp < %s 2>&1 | FileCheck %s --check-prefix=NO-VECTORFP

declare i64 @llvm.vscale.i64()

define i64 @vector_byte_length() {
; CHECK-LABEL: vector_byte_length:
; CHECK: vlcnt.b{{[ \t]+}}r0
; CHECK-NOT: rdvl
  %vscale = call i64 @llvm.vscale.i64()
  %bytes = mul i64 %vscale, 16
  ret i64 %bytes
}

define <vscale x 4 x i32> @integer_add(<vscale x 4 x i32> %lhs,
                                       <vscale x 4 x i32> %rhs) {
; CHECK-LABEL: integer_add:
; CHECK: ptrue.b p7
; CHECK-NEXT: vadd.l p7, v1, v0
  %result = add <vscale x 4 x i32> %lhs, %rhs
  ret <vscale x 4 x i32> %result
}

define <vscale x 4 x float> @floating_add(<vscale x 4 x float> %lhs,
                                          <vscale x 4 x float> %rhs) {
; CHECK-LABEL: floating_add:
; CHECK: ptrue.b p7
; CHECK-NEXT: vfadd.s p7, v1, v0
  %result = fadd <vscale x 4 x float> %lhs, %rhs
  ret <vscale x 4 x float> %result
}

define <vscale x 4 x i1> @integer_compare(<vscale x 4 x i32> %lhs,
                                          <vscale x 4 x i32> %rhs) {
; CHECK-LABEL: integer_compare:
; CHECK: ptrue.b p7
; CHECK-NEXT: vcmpult.l p7, v0, v1, p0
  %result = icmp ult <vscale x 4 x i32> %lhs, %rhs
  ret <vscale x 4 x i1> %result
}

define <vscale x 4 x i1> @floating_compare(<vscale x 4 x float> %lhs,
                                           <vscale x 4 x float> %rhs) {
; CHECK-LABEL: floating_compare:
; CHECK: ptrue.b p7
; CHECK-NEXT: vfcmplt.s p7, v0, v1, p0
  %result = fcmp olt <vscale x 4 x float> %lhs, %rhs
  ret <vscale x 4 x i1> %result
}

define <vscale x 2 x i64> @contiguous_load(ptr %address) {
; CHECK-LABEL: contiguous_load:
; CHECK: ptrue.b p7
; CHECK-NEXT: vmovz.q p7, [r0], v0
  %result = load <vscale x 2 x i64>, ptr %address, align 16
  ret <vscale x 2 x i64> %result
}

define void @contiguous_store(ptr %address, <vscale x 2 x i64> %value) {
; CHECK-LABEL: contiguous_store:
; CHECK: ptrue.b p7
; CHECK-NEXT: vmov.q p7, v0, [r0]
  store <vscale x 2 x i64> %value, ptr %address, align 16
  ret void
}

; NO-VECTOR: LLVM ERROR: Don't know how to legalize this scalable vector type
; NO-VECTORFP: LLVM ERROR: Don't know how to legalize this scalable vector type
