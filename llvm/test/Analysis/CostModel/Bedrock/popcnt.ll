; REQUIRES: bedrock-registered-target
; RUN: opt -mtriple=bedrock -passes="print<cost-model>" -cost-kind=all 2>&1 -disable-output < %s | FileCheck %s

declare i32 @llvm.ctpop.i32(i32)
declare i64 @llvm.ctpop.i64(i64)

define i32 @pop32(i32 %x) {
; CHECK-LABEL: Printing analysis 'Cost Model Analysis' for function 'pop32':
; CHECK: Cost Model: Found costs of 1 for:   %v = call i32 @llvm.ctpop.i32(i32 %x)
entry:
  %v = call i32 @llvm.ctpop.i32(i32 %x)
  ret i32 %v
}

define i64 @pop64(i64 %x) {
; CHECK-LABEL: Printing analysis 'Cost Model Analysis' for function 'pop64':
; CHECK: Cost Model: Found costs of 1 for:   %v = call i64 @llvm.ctpop.i64(i64 %x)
entry:
  %v = call i64 @llvm.ctpop.i64(i64 %x)
  ret i64 %v
}
