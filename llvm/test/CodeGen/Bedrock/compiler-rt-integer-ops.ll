; RUN: llc -mtriple=bedrock < %s | FileCheck %s

declare i64 @llvm.ctlz.i64(i64, i1 immarg)
declare i64 @llvm.cttz.i64(i64, i1 immarg)
declare i64 @llvm.ctpop.i64(i64)
declare i64 @llvm.bswap.i64(i64)

define i64 @integer_bit_operations(i64 %value) {
; CHECK-LABEL: integer_bit_operations:
; CHECK: ret
  %leading = call i64 @llvm.ctlz.i64(i64 %value, i1 false)
  %trailing = call i64 @llvm.cttz.i64(i64 %value, i1 false)
  %population = call i64 @llvm.ctpop.i64(i64 %value)
  %swapped = call i64 @llvm.bswap.i64(i64 %value)
  %sum0 = add i64 %leading, %trailing
  %sum1 = add i64 %population, %swapped
  %sum2 = add i64 %sum0, %sum1
  ret i64 %sum2
}

define i128 @wide_variable_shift(i128 %value, i64 %amount) {
; CHECK-LABEL: wide_variable_shift:
; CHECK: ret
  %wide_amount = zext i64 %amount to i128
  %left = shl i128 %value, %wide_amount
  %right = lshr i128 %value, %wide_amount
  %result = xor i128 %left, %right
  ret i128 %result
}
