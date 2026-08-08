; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s

target triple = "bedrock"

declare i32 @llvm.get.fpmode.i32()
declare void @llvm.set.fpmode.i32(i32)
declare void @llvm.reset.fpmode()

define i32 @get_mode() {
; CHECK-LABEL: get_mode:
; CHECK: rdfstatus{{[ \t]+}}r0
; CHECK-NEXT: ret
  %mode = call i32 @llvm.get.fpmode.i32()
  ret i32 %mode
}

define void @set_mode(i32 %mode) {
; CHECK-LABEL: set_mode:
; CHECK: and.q{{[ \t]+}}1023, r0
; CHECK-NEXT: wrfstatus{{[ \t]+}}r0
; CHECK-NEXT: ret
  call void @llvm.set.fpmode.i32(i32 %mode)
  ret void
}

define void @reset_mode() {
; CHECK-LABEL: reset_mode:
; CHECK: clr.q{{[ \t]+}}r0
; CHECK-NEXT: wrfstatus{{[ \t]+}}r0
; CHECK-NEXT: ret
  call void @llvm.reset.fpmode()
  ret void
}
