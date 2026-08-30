; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -mattr=+vectorfp < %s | FileCheck %s
; RUN: not --crash llc -mtriple=bedrock -mattr=-vector < %s 2>&1 | FileCheck %s --check-prefix=NO-VECTOR

declare <vscale x 4 x i32> @llvm.bedrock.vector.strided.load.nxv4i32(
    ptr, i64, i64)
declare void @llvm.bedrock.vector.strided.store.nxv4i32(
    <vscale x 4 x i32>, ptr, i64, i64)
declare i64 @llvm.bedrock.vector.reduce.add.i64.nxv2i64(
    <vscale x 2 x i64>)
declare double @llvm.bedrock.vector.reduce.add.f64.nxv2f64(
    <vscale x 2 x double>)

define <vscale x 4 x i32> @strided_load(ptr %base, i64 %stride) {
; CHECK-LABEL: strided_load:
; CHECK: ptrue.b p7
; CHECK-NEXT: vmovz.l p7, [r0 + r1 * lane - 129], v0
  %result = call <vscale x 4 x i32>
      @llvm.bedrock.vector.strided.load.nxv4i32(ptr %base, i64 %stride,
                                                i64 -129)
  ret <vscale x 4 x i32> %result
}

define void @strided_store(ptr %base, i64 %stride,
                           <vscale x 4 x i32> %value) {
; CHECK-LABEL: strided_store:
; CHECK: ptrue.b p7
; CHECK-NEXT: vmov.l p7, v0, [r0 + r1 * lane + 2147483648]
  call void @llvm.bedrock.vector.strided.store.nxv4i32(
      <vscale x 4 x i32> %value, ptr %base, i64 %stride, i64 2147483648)
  ret void
}

define i64 @integer_reduction(<vscale x 2 x i64> %value) {
; CHECK-LABEL: integer_reduction:
; CHECK: ptrue.b p7
; CHECK-NEXT: vredadd.q p7, v0, r0
  %result = call i64 @llvm.bedrock.vector.reduce.add.i64.nxv2i64(
      <vscale x 2 x i64> %value)
  ret i64 %result
}

define double @floating_reduction(<vscale x 2 x double> %value) {
; CHECK-LABEL: floating_reduction:
; CHECK: ptrue.b p7
; CHECK-NEXT: vfredadd.d p7, v0, f0
  %result = call double @llvm.bedrock.vector.reduce.add.f64.nxv2f64(
      <vscale x 2 x double> %value)
  ret double %result
}

; NO-VECTOR: LLVM ERROR: Do not know how to split the result of this operator
