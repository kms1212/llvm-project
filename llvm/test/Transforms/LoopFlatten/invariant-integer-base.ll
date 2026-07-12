; RUN: opt -passes='loop(loop-flatten)' -S %s | FileCheck %s

define void @flatten_invariant_integer_base(i8 %value) {
; CHECK-LABEL: define void @flatten_invariant_integer_base(
; CHECK: %flatten.tripcount = mul i64 320, 200
; CHECK: %y = phi i64
; CHECK-NOT: %x = phi i64
; CHECK: %flatten.offset = add i64 15728640, %y
; CHECK: inttoptr i64 %flatten.offset to ptr
entry:
  br label %outer

outer:
  %y = phi i64 [ 0, %entry ], [ %y.next, %outer.latch ]
  %row = mul nuw nsw i64 %y, 320
  %row.base = add nuw nsw i64 %row, 15728640
  br label %inner

inner:
  %x = phi i64 [ 0, %outer ], [ %x.next, %inner ]
  %offset = add nuw nsw i64 %row.base, %x
  %ptr = inttoptr i64 %offset to ptr
  store volatile i8 %value, ptr %ptr, align 1
  %x.next = add nuw nsw i64 %x, 1
  %x.done = icmp eq i64 %x.next, 320
  br i1 %x.done, label %outer.latch, label %inner

outer.latch:
  %y.next = add nuw nsw i64 %y, 1
  %y.done = icmp eq i64 %y.next, 200
  br i1 %y.done, label %exit, label %outer

exit:
  ret void
}
