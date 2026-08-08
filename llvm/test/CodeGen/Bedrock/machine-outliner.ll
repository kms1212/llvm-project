; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O2 -enable-machine-outliner=never < %s | FileCheck %s --check-prefix=NOOUTLINE

target triple = "bedrock"

; CHECK-LABEL: f1:
; CHECK: jmp OUTLINED_FUNCTION_0
; CHECK-LABEL: f2:
; CHECK: jmp OUTLINED_FUNCTION_0
; CHECK-LABEL: f3:
; CHECK: jmp OUTLINED_FUNCTION_0
; CHECK-LABEL: f4:
; CHECK: jmp OUTLINED_FUNCTION_0
; CHECK-LABEL: OUTLINED_FUNCTION_0:
; CHECK: add.q
; CHECK: xor.q
; CHECK: sub.q
; CHECK: shl.q
; CHECK: or.q
; CHECK: ret

; NOOUTLINE-NOT: OUTLINED_FUNCTION

define i64 @f1(i64 %x, i64 %y) minsize optsize {
  %a = add i64 %x, %y
  %b = xor i64 %a, 17
  %c = sub i64 %b, %y
  %d = shl i64 %c, 3
  %e = or i64 %d, 5
  %f = xor i64 %e, %x
  ret i64 %f
}

define i64 @f2(i64 %x, i64 %y) minsize optsize {
  %a = add i64 %x, %y
  %b = xor i64 %a, 17
  %c = sub i64 %b, %y
  %d = shl i64 %c, 3
  %e = or i64 %d, 5
  %f = xor i64 %e, %x
  ret i64 %f
}

define i64 @f3(i64 %x, i64 %y) minsize optsize {
  %a = add i64 %x, %y
  %b = xor i64 %a, 17
  %c = sub i64 %b, %y
  %d = shl i64 %c, 3
  %e = or i64 %d, 5
  %f = xor i64 %e, %x
  ret i64 %f
}

define i64 @f4(i64 %x, i64 %y) minsize optsize {
  %a = add i64 %x, %y
  %b = xor i64 %a, 17
  %c = sub i64 %b, %y
  %d = shl i64 %c, 3
  %e = or i64 %d, 5
  %f = xor i64 %e, %x
  ret i64 %f
}
