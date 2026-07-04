; RUN: llc -mtriple=bedrock -O1 -stop-after=bedrock-push-pop-merge < %s | FileCheck %s

declare i32 @llvm.smax.i32(i32, i32)

define signext i32 @minsize_stack_const_mul(ptr readonly %p, i32 signext %n,
                                            i32 signext %d) minsize optsize {
; CHECK-LABEL: name: minsize_stack_const_mul
; CHECK-LABEL: bb.0.entry:
; CHECK-NOT:   MOV32mr {{.*}}$sp
; CHECK:       MOV64rr $d0
; CHECK-LABEL: bb.2.while.body:
; CHECK:       DIVMODS32rr
; CHECK-NOT:   MOV32rm $sp
; CHECK-NOT:   MADD32rrr
; CHECK:       MULU32ri {{.*}}, 5
; CHECK:       $d0 = ADD32rr $d0
; CHECK-NEXT:  $d0 = ADD32rr $d0
; CHECK-NEXT:  $d0 = ADD32rr $d0
; CHECK-NOT:   SHL32ri {{.*}}, 2
; CHECK-NOT:   MOV32rm $sp
; CHECK-LABEL: bb.3.while.end:
entry:
  %smax = tail call i32 @llvm.smax.i32(i32 %n, i32 0)
  %wide.trip.count = zext nneg i32 %smax to i64
  br label %while.cond

while.cond:
  %iv.count = phi i64 [ %iv.count.next, %while.body ], [ %wide.trip.count, %entry ]
  %ptr.iv = phi ptr [ %ptr.next, %while.body ], [ %p, %entry ]
  %i = phi i32 [ %i.next, %while.body ], [ 0, %entry ]
  %acc = phi i32 [ %acc.next, %while.body ], [ 0, %entry ]
  %done = icmp eq i64 %iv.count, 0
  br i1 %done, label %while.end, label %while.body

while.body:
  %loaded = load i32, ptr %ptr.iv, align 4
  %sum = add i32 %i, %loaded
  %frozen = freeze i32 %sum
  %q = sdiv i32 %frozen, %d
  %qprod = mul i32 %q, %d
  %rem = sub i32 %frozen, %qprod
  %q3 = mul nsw i32 %q, 3
  %acc.q = add nsw i32 %q3, %acc
  %r5 = mul nsw i32 %rem, 5
  %acc.next = add nsw i32 %acc.q, %r5
  %i.next = add nuw i32 %i, 1
  %ptr.next = getelementptr i8, ptr %ptr.iv, i64 4
  %iv.count.next = add nsw i64 %iv.count, -1
  br label %while.cond

while.end:
  ret i32 %acc
}
