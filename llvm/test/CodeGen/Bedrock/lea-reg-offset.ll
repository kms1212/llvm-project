; RUN: llc -mtriple=bedrock < %s | FileCheck %s

define i64 @add_i64_large_offset(i64 %x) {
; CHECK-LABEL: add_i64_large_offset:
; CHECK:       lea.q [r0 + 42], r0
; CHECK-NEXT:  ret
  %y = add i64 %x, 42
  ret i64 %y
}

define i64 @add_i64_large_offset_commuted(i64 %x) {
; CHECK-LABEL: add_i64_large_offset_commuted:
; CHECK:       lea.q [r0 + 42], r0
; CHECK-NEXT:  ret
  %y = add i64 42, %x
  ret i64 %y
}

define i64 @add_i64_one_still_inc(i64 %x) {
; CHECK-LABEL: add_i64_one_still_inc:
; CHECK:       inc.q r0
; CHECK-NEXT:  ret
  %y = add i64 %x, 1
  ret i64 %y
}

define i32 @add_i32_large_offset(i32 %x) {
; CHECK-LABEL: add_i32_large_offset:
; CHECK:       add.l 42, r0
; CHECK-NEXT:  ret
  %y = add i32 %x, 42
  ret i32 %y
}

define i64 @copyback_const_after_branch(i1 %cond, i64 %x) {
; CHECK-LABEL: copyback_const_after_branch:
; CHECK:       lea.q 4660, r0
; CHECK-NEXT:  ret
entry:
  br i1 %cond, label %retx, label %retc

retx:
  ret i64 %x

retc:
  ret i64 4660
}

define i64 @copyback_lea_after_branch(i1 %cond, i64 %base) {
; CHECK-LABEL: copyback_lea_after_branch:
; CHECK:       lea.q [r1 + 4660], r0
; CHECK-NEXT:  ret
entry:
  br i1 %cond, label %retx, label %retc

retx:
  ret i64 %base

retc:
  %y = add i64 %base, 4660
  ret i64 %y
}
