; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s

target triple = "bedrock"

declare i32 @llvm.get.fpmode.i32()
declare void @llvm.set.fpmode.i32(i32)
declare void @llvm.reset.fpmode()
declare i32 @llvm.get.fpenv.i32()
declare void @llvm.set.fpenv.i32(i32)
declare void @llvm.reset.fpenv()

define i32 @get_mode() {
; CHECK-LABEL: get_mode:
; CHECK: rdfstatus{{[ \t]+}}r0
; CHECK-NEXT: ret
  %mode = call i32 @llvm.get.fpmode.i32()
  ret i32 %mode
}

define void @set_mode(i32 %mode) uwtable {
; CHECK-LABEL: set_mode:
; CHECK: sub.q{{[ \t]+}}16, sp
; CHECK: mov.q{{[ \t]+}}r15, [sp + 8]
; CHECK: rdfstatus{{[ \t]+}}r15
; CHECK: mov.q{{[ \t]+}}r15, [sp]
; CHECK: .cfi_offset fstatus, -24
; CHECK: and.q{{[ \t]+}}1023, r0
; CHECK-NEXT: wrfstatus{{[ \t]+}}r0
; CHECK: mov.q{{[ \t]+}}[sp], r15
; CHECK-NEXT: wrfstatus{{[ \t]+}}r15
; CHECK-NEXT: .cfi_restore fstatus
; CHECK: mov.q{{[ \t]+}}[sp + 8], r15
; CHECK: add.q{{[ \t]+}}16, sp
; CHECK-NEXT: .cfi_def_cfa_offset 8
; CHECK-NEXT: ret
  call void @llvm.set.fpmode.i32(i32 %mode)
  ret void
}

define void @reset_mode() {
; CHECK-LABEL: reset_mode:
; CHECK: clr.q{{[ \t]+}}r0
; CHECK-NEXT: wrfstatus{{[ \t]+}}r0
; CHECK: wrfstatus{{[ \t]+}}r15
; CHECK: ret
  call void @llvm.reset.fpmode()
  ret void
}

define i32 @get_environment() {
; CHECK-LABEL: get_environment:
; CHECK: rdfstatus
; CHECK: rdfflags
; CHECK: ret
  %environment = call i32 @llvm.get.fpenv.i32()
  ret i32 %environment
}

define void @set_environment(i32 %environment) {
; CHECK-LABEL: set_environment:
; CHECK: wrfflags
; CHECK: wrfstatus
; CHECK: wrfstatus{{[ \t]+}}r15
; CHECK: ret
  call void @llvm.set.fpenv.i32(i32 %environment)
  ret void
}

define void @reset_environment() {
; CHECK-LABEL: reset_environment:
; CHECK: wrfflags
; CHECK: wrfstatus
; CHECK: wrfstatus{{[ \t]+}}r15
; CHECK: ret
  call void @llvm.reset.fpenv()
  ret void
}
