; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s

define void @store_zero_i64(ptr %p) optsize {
; CHECK-LABEL: store_zero_i64:
; CHECK:       CLR [A0]
; CHECK-NEXT:  RET
entry:
  store i64 0, ptr %p, align 8
  ret void
}

define void @store_zero_i64_offset(ptr %p) optsize {
; CHECK-LABEL: store_zero_i64_offset:
; CHECK:       CLR [A0 + 12]
; CHECK-NEXT:  RET
entry:
  %q = getelementptr i8, ptr %p, i64 12
  store i64 0, ptr %q, align 4
  ret void
}
