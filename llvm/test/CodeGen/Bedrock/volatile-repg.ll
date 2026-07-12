; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s

define void @volatile_countdown(ptr %dst, i32 %n, i8 %value) {
; CHECK-LABEL: volatile_countdown:
; CHECK-NOT: repg
; CHECK: mov.b
; CHECK: djt
; CHECK: ret
entry:
  %positive = icmp sgt i32 %n, 0
  br i1 %positive, label %preheader, label %exit

preheader:
  %count = zext i32 %n to i64
  br label %loop

loop:
  %ptr = phi ptr [ %dst, %preheader ], [ %next.ptr, %loop ]
  %iv = phi i64 [ %count, %preheader ], [ %next.iv, %loop ]
  store volatile i8 %value, ptr %ptr, align 1
  %next.ptr = getelementptr i8, ptr %ptr, i64 1
  %next.iv = add nsw i64 %iv, -1
  %done = icmp eq i64 %next.iv, 0
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

define void @nonvolatile_countdown(ptr %dst, i32 %n, i8 %value) {
; CHECK-LABEL: nonvolatile_countdown:
; CHECK: repg
; CHECK: mov.b
; CHECK: }
; CHECK-NOT: djt
; CHECK: ret
entry:
  %positive = icmp sgt i32 %n, 0
  br i1 %positive, label %preheader, label %exit

preheader:
  %count = zext i32 %n to i64
  br label %loop

loop:
  %ptr = phi ptr [ %dst, %preheader ], [ %next.ptr, %loop ]
  %iv = phi i64 [ %count, %preheader ], [ %next.iv, %loop ]
  store i8 %value, ptr %ptr, align 1
  %next.ptr = getelementptr i8, ptr %ptr, i64 1
  %next.iv = add nsw i64 %iv, -1
  %done = icmp eq i64 %next.iv, 0
  br i1 %done, label %exit, label %loop

exit:
  ret void
}
