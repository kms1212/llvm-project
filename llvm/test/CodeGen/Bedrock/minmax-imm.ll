; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s

declare i32 @llvm.umax.i32(i32, i32)
declare i32 @llvm.umin.i32(i32, i32)
declare i32 @llvm.smax.i32(i32, i32)
declare i32 @llvm.smin.i32(i32, i32)

define i32 @umax_const(i32 %x) minsize optsize {
; CHECK-LABEL: umax_const:
; CHECK:       MAXU.L 1, D0
; CHECK-NEXT:  RET
  %r = call i32 @llvm.umax.i32(i32 %x, i32 1)
  ret i32 %r
}

define i32 @umin_const(i32 %x) minsize optsize {
; CHECK-LABEL: umin_const:
; CHECK:       MINU.L 6, D0
; CHECK-NEXT:  RET
  %r = call i32 @llvm.umin.i32(i32 %x, i32 6)
  ret i32 %r
}

define i32 @smax_const(i32 %x) minsize optsize {
; CHECK-LABEL: smax_const:
; CHECK:       MAXS.L -7, D0
; CHECK-NEXT:  RET
  %r = call i32 @llvm.smax.i32(i32 %x, i32 -7)
  ret i32 %r
}

define i32 @smin_const(i32 %x) minsize optsize {
; CHECK-LABEL: smin_const:
; CHECK:       MINS.L 7, D0
; CHECK-NEXT:  RET
  %r = call i32 @llvm.smin.i32(i32 %x, i32 7)
  ret i32 %r
}
