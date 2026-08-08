; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O1 -verify-machineinstrs < %s | FileCheck %s --check-prefix=O1
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=O2

define i32 @cmp_branch(i32 %a, i32 %b) {
; O1-LABEL: cmp_branch:
; O1: cmp.l r1, r0
; O1-NEXT: jge
; O1-NOT: cmpj
; O2-LABEL: cmp_branch:
; O2: cmpjge.l r1, r0,
; O2-NOT: cmp.l
entry:
  %c = icmp slt i32 %a, %b
  br i1 %c, label %t, label %f

t:
  ret i32 1

f:
  ret i32 0
}

define i32 @test_branch(i64 %x) {
; O1-LABEL: test_branch:
; O1: test.q r0, r0
; O1-NEXT: jeq
; O1-NOT: testj
; O2-LABEL: test_branch:
; O2: testjeq.q r0, r0,
; O2-NOT: test.q
entry:
  %c = icmp eq i64 %x, 0
  br i1 %c, label %t, label %f

t:
  ret i32 1

f:
  ret i32 0
}

define i32 @cmp_branch_minsize(i32 %a, i32 %b) minsize {
; O1-LABEL: cmp_branch_minsize:
; O1: cmp.l r1, r0
; O1-NEXT: jge
; O1-NOT: cmpj
; O2-LABEL: cmp_branch_minsize:
; O2: cmp.l r1, r0
; O2-NEXT: jge
; O2-NOT: cmpj
entry:
  %c = icmp slt i32 %a, %b
  br i1 %c, label %t, label %f

t:
  ret i32 1

f:
  ret i32 0
}

define i32 @cmp_branch_minsize_far(i32 %a, i32 %b) minsize {
; O1-LABEL: cmp_branch_minsize_far:
; O1: cmpjge.l r1, r0,
; O1-NOT: cmp.l
; O2-LABEL: cmp_branch_minsize_far:
; O2: cmpjge.l r1, r0,
; O2-NOT: cmp.l
entry:
  %c = icmp slt i32 %a, %b
  br i1 %c, label %t, label %f

t:
  call void asm sideeffect ".space 160", ""()
  ret i32 1

f:
  ret i32 0
}

define i32 @zero_result_precheck(ptr %p, i32 %n) {
; O1-LABEL: zero_result_precheck:
; O1: test.l r1, r1
; O1-NEXT: jle
; O1: clr.q [[ACC:r[0-9]+]]
; O1-NOT: cmp.l
; O1: repgf {{r[0-9]+}}, {
; O1-NEXT: add.l [r0++], [[ACC]]
; O1-NEXT: }
; O1-NOT: djt
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %loop.preheader, label %exit

loop.preheader:
  %count = zext i32 %n to i64
  br label %loop

loop:
  %ptr = phi ptr [ %next.ptr, %loop ], [ %p, %loop.preheader ]
  %iv = phi i64 [ %next.iv, %loop ], [ %count, %loop.preheader ]
  %acc = phi i32 [ %sum, %loop ], [ 0, %loop.preheader ]
  %value = load i32, ptr %ptr, align 4
  %sum = add i32 %value, %acc
  %next.iv = add nsw i64 %iv, -1
  %next.ptr = getelementptr i32, ptr %ptr, i64 1
  %done = icmp eq i64 %next.iv, 0
  br i1 %done, label %exit, label %loop

exit:
  %result = phi i32 [ 0, %entry ], [ %sum, %loop ]
  ret i32 %result
}

define i32 @cmp_branch_far(i32 %a, i32 %b) {
; O1-LABEL: cmp_branch_far:
; O1: cmpjge.l r1, r0,
; O1-NOT: cmp.l
; O2-LABEL: cmp_branch_far:
; O2: cmpjge.l r1, r0,
; O2-NOT: cmp.l
entry:
  %c = icmp slt i32 %a, %b
  br i1 %c, label %t, label %f

t:
  call void asm sideeffect ".space 160", ""()
  ret i32 1

f:
  ret i32 0
}
