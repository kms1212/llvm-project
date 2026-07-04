; RUN: llc -mtriple=bedrock -O1 -verify-machineinstrs < %s | FileCheck %s

declare i32 @llvm.smax.i32(i32, i32)

define signext i32 @spill_heavy_loop(ptr readonly %a, ptr readonly %b,
                                     ptr readonly %c, ptr readonly %d,
                                     i32 signext %n) minsize optsize {
; CHECK-LABEL: spill_heavy_loop:
; CHECK:       PUSHM {D6,D7,A6}
; CHECK-NOT:   SUB.Q
; CHECK:       MAXS.L 0, D0
; CHECK:       TEST.L D0, D0
; CHECK-NEXT:  JEQ.W
; CHECK:       ADD.Q {{.*}}, A
; CHECK:       ADD.L [A3++], D7
; CHECK:       ADD.L [A2++], D6
; CHECK:       ADD.L [A1++], D1
; CHECK-NOT:   [SP +
; CHECK:       SUM.L {{.*}}, D0
; CHECK-NOT:   MOV.Q
; CHECK:       POPM {D6,D7,A6}
entry:
  %smax = tail call i32 @llvm.smax.i32(i32 %n, i32 0)
  br label %while.cond

while.cond:
  %count = phi i32 [ %count.next, %while.body ], [ %smax, %entry ]
  %bp = phi ptr [ %bp.next, %while.body ], [ %b, %entry ]
  %cp = phi ptr [ %cp.next, %while.body ], [ %c, %entry ]
  %dp = phi ptr [ %dp.next, %while.body ], [ %d, %entry ]
  %ap = phi ptr [ %ap.next, %while.body ], [ %a, %entry ]
  %s = phi i32 [ %s.next, %while.body ], [ 1, %entry ]
  %t = phi i32 [ %t.next, %while.body ], [ 2, %entry ]
  %u = phi i32 [ %u.next, %while.body ], [ 3, %entry ]
  %v = phi i32 [ %v.next, %while.body ], [ 4, %entry ]
  %w = phi i32 [ %w.next, %while.body ], [ 5, %entry ]
  %x = phi i32 [ %x.next, %while.body ], [ 6, %entry ]
  %y = phi i32 [ %y.next, %while.body ], [ 7, %entry ]
  %z = phi i32 [ %z.next, %while.body ], [ 8, %entry ]
  %done = icmp eq i32 %count, 0
  br i1 %done, label %while.end, label %while.body

while.body:
  %av = load i32, ptr %ap, align 4
  %s.next = add nsw i32 %av, %s
  %bv = load i32, ptr %bp, align 4
  %t.add = add nsw i32 %bv, %t
  %t.next = add nsw i32 %t.add, %s.next
  %cv = load i32, ptr %cp, align 4
  %u.add = add nsw i32 %cv, %u
  %u.next = add nsw i32 %u.add, %t.next
  %dv = load i32, ptr %dp, align 4
  %v.add = add nsw i32 %dv, %v
  %v.next = add nsw i32 %v.add, %u.next
  %w.add = add nsw i32 %s.next, %w
  %w.next = add nsw i32 %w.add, %v.next
  %x.add = add nsw i32 %t.next, %x
  %x.next = add nsw i32 %x.add, %w.next
  %y.add = add nsw i32 %u.next, %y
  %y.next = add nsw i32 %y.add, %x.next
  %z.add = add nsw i32 %v.next, %z
  %z.next = add nsw i32 %z.add, %y.next
  %ap.next = getelementptr inbounds i32, ptr %ap, i64 1
  %bp.next = getelementptr inbounds i32, ptr %bp, i64 1
  %cp.next = getelementptr inbounds i32, ptr %cp, i64 1
  %dp.next = getelementptr inbounds i32, ptr %dp, i64 1
  %count.next = add nsw i32 %count, -1
  br label %while.cond

while.end:
  %r0 = add nsw i32 %t, %s
  %r1 = add nsw i32 %r0, %u
  %r2 = add nsw i32 %r1, %v
  %r3 = add nsw i32 %r2, %w
  %r4 = add nsw i32 %r3, %x
  %r5 = add nsw i32 %r4, %y
  %r6 = add nsw i32 %r5, %z
  ret i32 %r6
}
