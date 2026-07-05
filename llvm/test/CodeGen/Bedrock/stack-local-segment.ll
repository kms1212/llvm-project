; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=SP
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs -frame-pointer=all < %s | FileCheck %s --check-prefix=FP

target triple = "bedrock"

define signext i32 @stack_local_array(i32 signext %i, i32 signext %v) {
; SP-LABEL: stack_local_array:
; SP-NOT:   MOV.Q SP, A
; SP:       MOV.L D1, [SP + D0.L * 4]
; SP-NEXT:  MOV.L [SP + D0.L * 4 + 4], D0
; FP-LABEL: stack_local_array:
; FP:       MOV.Q SP, A7
; FP-NOT:   [A7 + D0.L * 4
; FP:       MOV.L D1, [SP + D0.L * 4 + 16]
; FP-NEXT:  MOV.L [SP + D0.L * 4 + 20], D0
entry:
  %arr = alloca [64 x i32], align 4
  %idx = sext i32 %i to i64
  %p = getelementptr inbounds [64 x i32], ptr %arr, i64 0, i64 %idx
  store i32 %v, ptr %p, align 4
  %next = add nsw i32 %i, 1
  %next64 = sext i32 %next to i64
  %q = getelementptr inbounds [64 x i32], ptr %arr, i64 0, i64 %next64
  %r = load i32, ptr %q, align 4
  ret i32 %r
}
