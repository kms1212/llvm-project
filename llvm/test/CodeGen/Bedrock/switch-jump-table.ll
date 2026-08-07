; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s

declare void @f0()
declare void @f1()
declare void @f2()
declare void @f3()
declare void @f4()
declare void @f5()
declare void @f6()
declare void @f7()
declare void @f8()
declare void @f9()
declare void @f10()
declare void @f11()
declare void @f12()
declare void @f13()
declare void @f14()
declare void @f15()

define void @dense_switch(i32 %x) {
; CHECK-LABEL: dense_switch:
; CHECK: cmp.l 15,
; CHECK-NOT: shl.q
; CHECK: lea.q .LJTI{{[0-9]+}}_{{[0-9]+}}, [[JT:r[0-9]+]]
; CHECK-NEXT: extsq.l [ds:[[JT]] + r0], r0
; CHECK-NEXT: add.q [[JT]], r0
; CHECK-NEXT: jmp.q r0
; CHECK: .section .rodata
; CHECK: .LJTI{{[0-9]+}}_{{[0-9]+}}:
; CHECK-NEXT: .long .LBB{{[0-9]+}}_{{[0-9]+}}-.LJTI{{[0-9]+}}_{{[0-9]+}}
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
    i32 4, label %c4
    i32 5, label %c5
    i32 6, label %c6
    i32 7, label %c7
    i32 8, label %c8
    i32 9, label %c9
    i32 10, label %c10
    i32 11, label %c11
    i32 12, label %c12
    i32 13, label %c13
    i32 14, label %c14
    i32 15, label %c15
  ]

c0:
  tail call void @f0()
  ret void
c1:
  tail call void @f1()
  ret void
c2:
  tail call void @f2()
  ret void
c3:
  tail call void @f3()
  ret void
c4:
  tail call void @f4()
  ret void
c5:
  tail call void @f5()
  ret void
c6:
  tail call void @f6()
  ret void
c7:
  tail call void @f7()
  ret void
c8:
  tail call void @f8()
  ret void
c9:
  tail call void @f9()
  ret void
c10:
  tail call void @f10()
  ret void
c11:
  tail call void @f11()
  ret void
c12:
  tail call void @f12()
  ret void
c13:
  tail call void @f13()
  ret void
c14:
  tail call void @f14()
  ret void
c15:
  tail call void @f15()
  ret void
default:
  ret void
}
