; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs -filetype=obj -o /dev/null < %s

declare void @syscall_put(i64)

define void @br_cg_001_syscall_pack(i64 %value) {
; CHECK-LABEL: br_cg_001_syscall_pack:
; CHECK:       AND.Q 255, D0
; CHECK-NEXT:  BSET.Q 32, D0
; CHECK-NEXT:  BSET.Q 33, D0
; CHECK-NEXT:  JMP.W syscall_put@WORD_PCREL16
entry:
  %masked = and i64 %value, 255
  %packed = or i64 %masked, 12884901888
  tail call void @syscall_put(i64 %packed)
  ret void
}

define zeroext i8 @br_cg_002_hex_digit(i32 %value) {
; CHECK-LABEL: br_cg_002_hex_digit:
; CHECK:       AND.L 15, [[NIBBLE:D[0-7]]]
; CHECK:       MOV.Q [[NIBBLE]], [[LETTER:D[0-7]]]
; CHECK-NEXT:  ADD.L 55, [[LETTER]]
; CHECK-NEXT:  MOV.Q [[NIBBLE]], [[DIGIT:D[0-7]]]
; CHECK-NEXT:  OR.L 48, [[DIGIT]]
; CHECK-NEXT:  CMP.L 10, [[NIBBLE]]
; CHECK-NEXT:  MOVULT.L [[DIGIT]], [[LETTER]]
entry:
  %nibble = and i32 %value, 15
  %is_digit = icmp ult i32 %nibble, 10
  %digit = or i32 %nibble, 48
  %letter = add nuw nsw i32 %nibble, 55
  %selected = select i1 %is_digit, i32 %digit, i32 %letter
  %trunc = trunc i32 %selected to i8
  ret i8 %trunc
}
