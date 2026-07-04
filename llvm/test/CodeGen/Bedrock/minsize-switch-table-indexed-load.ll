; RUN: llc -mtriple=bedrock -O1 -verify-machineinstrs < %s | FileCheck %s

@switch.table.switch_jump_table = private unnamed_addr constant [8 x i32] [i32 3, i32 5, i32 7, i32 11, i32 13, i32 17, i32 19, i32 23], align 4

define dso_local signext i32 @switch_jump_table(i32 noundef signext %x, i32 noundef signext %bias) minsize optsize {
; CHECK-LABEL: switch_jump_table:
; CHECK-NOT:   SHL.Q
; CHECK-NOT:   MOV.Q{{.*}}@ABS64
; CHECK:       MOV.L [PC + D0.L * 4 + .Lswitch.table.switch_jump_table@PCREL32], D0
; CHECK-NEXT:  ADD.L D1, D0
entry:
  %and = and i32 %x, 7
  %idx = zext i32 %and to i64
  %ptr = getelementptr inbounds i32, ptr @switch.table.switch_jump_table, i64 %idx
  %load = load i32, ptr %ptr, align 4
  %add = add nsw i32 %bias, %load
  ret i32 %add
}
