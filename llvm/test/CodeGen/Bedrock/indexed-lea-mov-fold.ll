; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s

define i32 @indexed_load_zext_i32(ptr %base, i32 %idx) optsize {
; CHECK-LABEL: indexed_load_zext_i32:
; CHECK:       MOV.L [A0 + D0.L * 4], D0
; CHECK-NOT:   LEA
entry:
  %wide = zext i32 %idx to i64
  %ptr = getelementptr i32, ptr %base, i64 %wide
  %value = load i32, ptr %ptr, align 4
  ret i32 %value
}

define void @indexed_store_zext_i32(ptr %base, i32 %idx, i32 %value) optsize {
; CHECK-LABEL: indexed_store_zext_i32:
; CHECK:       MOV.L D1, [A0 + D0.L * 4]
; CHECK-NOT:   LEA
entry:
  %wide = zext i32 %idx to i64
  %ptr = getelementptr i32, ptr %base, i64 %wide
  store i32 %value, ptr %ptr, align 4
  ret void
}
