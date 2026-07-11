; RUN: llc -mtriple=bedrock < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O0 < %s | FileCheck %s --check-prefix=SHARED
; RUN: llc -mtriple=bedrock -filetype=obj < %s -o %t.o
; RUN: llvm-readobj --symbols %t.o | FileCheck %s --check-prefix=SIZE

define ptr @gep_i16(ptr %base, i64 %index) {
; CHECK-LABEL: gep_i16:
; CHECK:       lea.w [{{(ds:)?}}r0 + r1], r0
; CHECK-NEXT:  ret
  %p = getelementptr i16, ptr %base, i64 %index
  ret ptr %p
}

define ptr @gep_i32(ptr %base, i64 %index) {
; CHECK-LABEL: gep_i32:
; CHECK:       lea.l [{{(ds:)?}}r0 + r1], r0
; CHECK-NEXT:  ret
  %p = getelementptr i32, ptr %base, i64 %index
  ret ptr %p
}

define ptr @gep_i64(ptr %base, i64 %index) {
; CHECK-LABEL: gep_i64:
; CHECK:       lea.q [{{(ds:)?}}r0 + r1], r0
; CHECK-NEXT:  ret
  %p = getelementptr i64, ptr %base, i64 %index
  ret ptr %p
}

define ptr @gep_i32_commuted(ptr %base, i64 %index) {
; CHECK-LABEL: gep_i32_commuted:
; CHECK:       lea.l [{{(ds:)?}}r0 + r1], r0
; CHECK-NEXT:  ret
  %scaled = shl i64 %index, 2
  %base.int = ptrtoint ptr %base to i64
  %sum = add i64 %scaled, %base.int
  %result = inttoptr i64 %sum to ptr
  ret ptr %result
}

define void @shared_scaled_index(ptr %dst, ptr %src, i64 %index) {
; SHARED-LABEL: shared_scaled_index:
; SHARED-COUNT-1: shl.q 3
; SHARED-NOT:     lea.q
  %src.element = getelementptr i64, ptr %src, i64 %index
  %value = load i64, ptr %src.element, align 8
  %dst.element = getelementptr i64, ptr %dst, i64 %index
  store i64 %value, ptr %dst.element, align 8
  ret void
}

define ptr @sdiv_gep_i32(ptr %base, i64 %byte_offset) minsize optsize {
; CHECK-LABEL: sdiv_gep_i32:
; CHECK:       divs.q 4, r1
; CHECK-NOT:   sar.q
; CHECK-NOT:   and.q
; CHECK:       lea.l [{{(ds:)?}}r0 + {{r[0-9]+}}], r0
; CHECK-NEXT:  ret
; SIZE:        Name: sdiv_gep_i32
; SIZE-NEXT:   Value:
; SIZE-NEXT:   Size: 11
  %index = sdiv i64 %byte_offset, 4
  %p = getelementptr i32, ptr %base, i64 %index
  ret ptr %p
}

define i32 @pointer_integer_mix(ptr %base, ptr %end, i32 %count,
                                i64 %byte_offset) minsize optsize {
; CHECK-LABEL: pointer_integer_mix:
; CHECK-NOT:   shl.q 2
; CHECK-NOT:   and.q -4
; CHECK:       divs.q 4, r3
; CHECK-NEXT:  lea.l [{{(ds:)?}}r4 + r3], r3
; CHECK:       lea.l [{{(ds:)?}}r4 + r2], r4
entry:
  %end.int = ptrtoint ptr %end to i64
  %base.int = ptrtoint ptr %base to i64
  %byte_distance = sub i64 %end.int, %base.int
  %limit = ashr exact i64 %byte_distance, 2
  %has.work = icmp sgt i32 %count, 0
  br i1 %has.work, label %preheader, label %exit

preheader:
  %offset.index = sdiv i64 %byte_offset, 4
  %q.start = getelementptr inbounds i32, ptr %base, i64 %offset.index
  %count.ext = zext nneg i32 %count to i64
  %p.start = getelementptr inbounds nuw i32, ptr %base, i64 %count.ext
  br label %loop

loop:
  %i = phi i64 [ 0, %preheader ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %preheader ], [ %sum.next, %loop ]
  %p = phi ptr [ %p.start, %preheader ], [ %p.next, %loop ]
  %q = phi ptr [ %q.start, %preheader ], [ %q.next, %loop ]
  %in.range = icmp slt i64 %i, %limit
  %bonus = zext i1 %in.range to i32
  %sum.bonus = add i32 %sum, %bonus
  %p.value = load i32, ptr %p, align 4
  %sum.p = add i32 %sum.bonus, %p.value
  %q.value = load i32, ptr %q, align 4
  %sum.next = add i32 %sum.p, %q.value
  %p.next = getelementptr inbounds i8, ptr %p, i64 4
  %q.next = getelementptr inbounds i8, ptr %q, i64 4
  %i.next = add nuw nsw i64 %i, 1
  %done = icmp eq i64 %i.next, %count.ext
  br i1 %done, label %exit, label %loop

exit:
  %result = phi i32 [ 0, %entry ], [ %sum.next, %loop ]
  ret i32 %result
}

define ptr @expanded_sdiv_gep_i32(ptr %base, i64 %byte_offset)
    minsize optsize {
; CHECK-LABEL: expanded_sdiv_gep_i32:
; CHECK:       sar.q 63
; CHECK:       shr.q 62
; CHECK:       and.q -4
; CHECK-NOT:   divs.q
; CHECK-NOT:   lea.l
; CHECK:       ret
; SIZE:        Name: expanded_sdiv_gep_i32
; SIZE-NEXT:   Value:
; SIZE-NEXT:   Size: 19
  %sign = ashr i64 %byte_offset, 63
  %bias = lshr i64 %sign, 62
  %biased = add i64 %byte_offset, %bias
  %aligned = and i64 %biased, -4
  %base.int = ptrtoint ptr %base to i64
  %result.int = add i64 %base.int, %aligned
  %result = inttoptr i64 %result.int to ptr
  ret ptr %result
}

define i32 @sdiv_i32_oz(i32 %value) minsize optsize {
; CHECK-LABEL: sdiv_i32_oz:
; CHECK:       divs.l 4, r0
; CHECK-NEXT:  ret
; SIZE:        Name: sdiv_i32_oz
; SIZE-NEXT:   Value:
; SIZE-NEXT:   Size: 6
  %result = sdiv i32 %value, 4
  ret i32 %result
}

define i64 @sdiv_i64_oz(i64 %value) minsize optsize {
; CHECK-LABEL: sdiv_i64_oz:
; CHECK:       divs.q 4, r0
; CHECK-NEXT:  ret
; SIZE:        Name: sdiv_i64_oz
; SIZE-NEXT:   Value:
; SIZE-NEXT:   Size: 6
  %result = sdiv i64 %value, 4
  ret i64 %result
}

define i64 @sdiv_i64_speed(i64 %value) {
; CHECK-LABEL: sdiv_i64_speed:
; CHECK-NOT:   divs.q
; CHECK:       sar.q 63
; CHECK:       shr.q 62
; CHECK:       sar.q 2
; CHECK-NEXT:  ret
  %result = sdiv i64 %value, 4
  ret i64 %result
}
