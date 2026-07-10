; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O=2 -verify-machineinstrs < %s | FileCheck %s

target triple = "bedrock"

define i32 @register_pressure(ptr %p, i32 %n) minsize optsize {
; CHECK-LABEL: register_pressure:
; CHECK: add.l {{\[r[0-9]+\+\+\]}}, [[A:r[0-9]+]]
; CHECK-NEXT: inc.l [[I:r[0-9]+]]
; CHECK-NEXT: add.l [[A]], [[B:r[0-9]+]]
; CHECK-NEXT: add.l [[B]], [[C:r[0-9]+]]
; CHECK-NEXT: add.l [[C]], [[D:r[0-9]+]]
; CHECK-NEXT: add.l [[D]], [[E:r[0-9]+]]
; CHECK-NEXT: add.l [[E]], [[F:r[0-9]+]]
; CHECK-NEXT: add.l [[F]], [[G:r[0-9]+]]
; CHECK-NEXT: add.l [[G]], [[H:r[0-9]+]]
; CHECK-NOT: mov.q
; CHECK: jmp
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next.i, %body ]
  %a = phi i32 [ 1, %entry ], [ %next.a, %body ]
  %b = phi i32 [ 2, %entry ], [ %next.b, %body ]
  %c = phi i32 [ 3, %entry ], [ %next.c, %body ]
  %d = phi i32 [ 4, %entry ], [ %next.d, %body ]
  %e = phi i32 [ 5, %entry ], [ %next.e, %body ]
  %f = phi i32 [ 6, %entry ], [ %next.f, %body ]
  %g = phi i32 [ 7, %entry ], [ %next.g, %body ]
  %h = phi i32 [ 8, %entry ], [ %next.h, %body ]
  %cursor = phi ptr [ %p, %entry ], [ %next.cursor, %body ]
  %more = icmp slt i32 %i, %n
  br i1 %more, label %body, label %exit

body:
  %v = load i32, ptr %cursor, align 4
  %next.cursor = getelementptr inbounds nuw i8, ptr %cursor, i64 4
  %next.a = add nsw i32 %v, %a
  %next.b = add nsw i32 %next.a, %b
  %next.c = add nsw i32 %next.b, %c
  %next.d = add nsw i32 %next.c, %d
  %next.e = add nsw i32 %next.d, %e
  %next.f = add nsw i32 %next.e, %f
  %next.g = add nsw i32 %next.f, %g
  %next.h = add nsw i32 %next.g, %h
  %next.i = add nuw nsw i32 %i, 1
  br label %loop

exit:
  %sum0 = add nsw i32 %a, %b
  %sum1 = add nsw i32 %sum0, %c
  %sum2 = add nsw i32 %sum1, %d
  %sum3 = add nsw i32 %sum2, %e
  %sum4 = add nsw i32 %sum3, %f
  %sum5 = add nsw i32 %sum4, %g
  %sum6 = add nsw i32 %sum5, %h
  ret i32 %sum6
}
