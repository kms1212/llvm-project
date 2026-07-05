; RUN: llc -mtriple=bedrock-unknown-unknown -O1 < %s | FileCheck %s
; RUN: llc -mtriple=bedrock-unknown-unknown -O2 < %s | FileCheck %s

target triple = "bedrock-unknown-unknown"

declare void @tty_put_hex8(i32)
declare void @tty_put_char(i32)

define i32 @basic_run_demo() nounwind {
; CHECK-LABEL: basic_run_demo:
; CHECK:       SUB.Q 24, SP
; CHECK:       MOV.Q D6, [SP + [[SLOT:[0-9]+]]]
; CHECK:       MOV.L 174, D6
; CHECK:       MOV.Q D6, D0
; CHECK:       CALL tty_put_hex8@PCREL16
; CHECK:       MOV.L 10, D0
; CHECK:       CALL tty_put_char@PCREL16
; CHECK:       MOV.Q D6, D0
; CHECK-NEXT:  MOV.Q [SP + [[SLOT]]], D6
; CHECK-NEXT:  ADD.Q 24, SP
; CHECK-NEXT:  RET
entry:
  tail call void @tty_put_hex8(i32 174)
  tail call void @tty_put_char(i32 10)
  ret i32 174
}
