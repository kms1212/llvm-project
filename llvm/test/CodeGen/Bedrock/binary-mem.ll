; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -code-model=tiny -O2 -verify-machineinstrs < %s | FileCheck %s

declare i32 @llvm.smax.i32(i32, i32)

@g32 = external global i32
@g64 = external global i64

define i32 @add_load_rhs(ptr %p, i32 %a) {
; CHECK-LABEL: add_load_rhs:
; CHECK: add.l [r0], r1
; CHECK: mov.q r1, r0
; CHECK: ret
  %v = load i32, ptr %p, align 4
  %r = add i32 %a, %v
  ret i32 %r
}

define i32 @add_two_loads(ptr %p, ptr %q, i32 %a) {
; CHECK-LABEL: add_two_loads:
; CHECK: add.l [r0], r2
; CHECK-NEXT: add.l [r1], r2
; CHECK-NEXT: mov.q r2, r0
; CHECK-NEXT: ret
  %x = load i32, ptr %p, align 4
  %y = load i32, ptr %q, align 4
  %s1 = add i32 %a, %x
  %s2 = add i32 %s1, %y
  ret i32 %s2
}

define i32 @mul_load_rhs(ptr %p, i32 %a) {
; CHECK-LABEL: mul_load_rhs:
; CHECK: mul.l [r0], r1
; CHECK: mov.q r1, r0
; CHECK: ret
  %v = load i32, ptr %p, align 4
  %r = mul i32 %a, %v
  ret i32 %r
}

define i32 @smax_load_rhs(ptr %p, i32 %a) {
; CHECK-LABEL: smax_load_rhs:
; CHECK: maxs.l [r0], r1
; CHECK: mov.q r1, r0
; CHECK: ret
  %v = load i32, ptr %p, align 4
  %r = call i32 @llvm.smax.i32(i32 %a, i32 %v)
  ret i32 %r
}

define i32 @smax_load_lhs_safe(ptr %p, i32 %a) {
; CHECK-LABEL: smax_load_lhs_safe:
; CHECK: maxs.l [r0], r1
; CHECK-NEXT: add.l r1, r0
; CHECK: ret
  %v = load i32, ptr %p, align 4
  %m = call i32 @llvm.smax.i32(i32 %v, i32 %a)
  %pi = ptrtoint ptr %p to i64
  %lo = trunc i64 %pi to i32
  %r = add i32 %m, %lo
  ret i32 %r
}

define i32 @mul_load_lhs_safe(ptr %p, i32 %a) {
; CHECK-LABEL: mul_load_lhs_safe:
; CHECK: mul.l [r0], r1
; CHECK-NEXT: add.l r1, r0
; CHECK: ret
  %v = load i32, ptr %p, align 4
  %m = mul i32 %v, %a
  %pi = ptrtoint ptr %p to i64
  %lo = trunc i64 %pi to i32
  %r = add i32 %m, %lo
  ret i32 %r
}

define i32 @mul_load_lhs_offset(ptr %p, i32 %a) {
; CHECK-LABEL: mul_load_lhs_offset:
; CHECK: mul.l [r0 + 4], r1
; CHECK-NEXT: add.l r1, r0
; CHECK: ret
  %q = getelementptr i8, ptr %p, i64 4
  %v = load i32, ptr %q, align 4
  %m = mul i32 %v, %a
  %pi = ptrtoint ptr %p to i64
  %lo = trunc i64 %pi to i32
  %r = add i32 %m, %lo
  ret i32 %r
}

define i32 @add_load_offset_rhs(ptr %p, i32 %a) {
; CHECK-LABEL: add_load_offset_rhs:
; CHECK: add.l [r0 + 4], r1
; CHECK: mov.q r1, r0
; CHECK: ret
  %q = getelementptr i8, ptr %p, i64 4
  %v = load i32, ptr %q, align 4
  %r = add i32 %a, %v
  ret i32 %r
}

define void @add_mem_dest(ptr %p, i32 %a) {
; CHECK-LABEL: add_mem_dest:
; CHECK: add.l r1, [r0]
; CHECK: ret
  %v = load i32, ptr %p, align 4
  %r = add i32 %v, %a
  store i32 %r, ptr %p, align 4
  ret void
}

define void @add_mem_source_store_offset(ptr %p, i64 %a, i64 %b) {
; CHECK-LABEL: add_mem_source_store_offset:
; CHECK: add.q r{{[0-9]+}}, [r0 + 40]
; CHECK-NOT: mov.q {{.*}}, [r0 + 40]
; CHECK: ret
  %sum = add i64 %a, %b
  %q = getelementptr i8, ptr %p, i64 40
  %v = load i64, ptr %q, align 8
  %r = add i64 %sum, %v
  store i64 %r, ptr %q, align 8
  ret void
}

define i32 @add_mem_source_store_across_load(ptr %ip, ptr %lp, i32 %n) {
; CHECK-LABEL: add_mem_source_store_across_load:
; CHECK: add.q [r1 + 16], r[[ACC:[0-9]+]]
; CHECK-NEXT: mov.l [r0 + 28], r{{[0-9]+}}
; CHECK-NEXT: add.q r[[ACC]], [r1 + 40]
; CHECK-NOT: mov.q {{.*}}, [r1 + 40]
; CHECK: ret
  %ip1 = getelementptr i32, ptr %ip, i64 1
  %a = load i32, ptr %ip1, align 4
  %as = sext i32 %a to i64
  %ns = sext i32 %n to i64
  %lp2 = getelementptr i64, ptr %lp, i64 2
  %x = load i64, ptr %lp2, align 8
  %sum0 = add nsw i64 %as, %ns
  %sum1 = add i64 %sum0, %x
  %lp5 = getelementptr i64, ptr %lp, i64 5
  %y = load i64, ptr %lp5, align 8
  %sum2 = add i64 %sum1, %y
  %ip7 = getelementptr i32, ptr %ip, i64 7
  %b = load i32, ptr %ip7, align 4
  store i64 %sum2, ptr %lp5, align 8
  %sum32a = add i32 %a, %n
  %sum32b = add i32 %sum32a, %b
  store i32 %sum32b, ptr %ip7, align 4
  %z = load i32, ptr %ip, align 4
  %ret = add i32 %z, %sum32b
  ret i32 %ret
}

define i64 @add_mem_source_store_return(ptr %p, i64 %a, i64 %b) {
; CHECK-LABEL: add_mem_source_store_return:
; CHECK: add.q [r0 + 40], r{{[0-9]+}}
; CHECK: mov.q r{{[0-9]+}}, [r0 + 40]
; CHECK: ret
  %sum = add i64 %a, %b
  %q = getelementptr i8, ptr %p, i64 40
  %v = load i64, ptr %q, align 8
  %r = add i64 %sum, %v
  store i64 %r, ptr %q, align 8
  ret i64 %r
}

define void @or_mem_dest_offset(ptr %p, i32 %a) {
; CHECK-LABEL: or_mem_dest_offset:
; CHECK: or.l r1, [r0 + 4]
; CHECK: ret
  %q = getelementptr i8, ptr %p, i64 4
  %v = load i32, ptr %q, align 4
  %r = or i32 %v, %a
  store i32 %r, ptr %q, align 4
  ret void
}

define i32 @cmp_load_lhs(ptr %p, i32 %a) {
; CHECK-LABEL: cmp_load_lhs:
; CHECK: cmp.l r1, [r0]
; CHECK: setlt r0
; CHECK: ret
  %v = load i32, ptr %p, align 4
  %c = icmp slt i32 %v, %a
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @cmp_load_rhs(ptr %p, i32 %a) {
; CHECK-LABEL: cmp_load_rhs:
; CHECK: cmp.l [r0], r1
; CHECK: setlt r0
; CHECK: ret
  %v = load i32, ptr %p, align 4
  %c = icmp slt i32 %a, %v
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @cmp_abs_lhs(i32 %a) {
; CHECK-LABEL: cmp_abs_lhs:
; CHECK-NOT: mov.l [g32]
; CHECK: cmp.l r0, [g32]
; CHECK: setlt r0
; CHECK: ret
  %v = load i32, ptr @g32, align 4
  %c = icmp slt i32 %v, %a
  %r = zext i1 %c to i32
  ret i32 %r
}

define i64 @cmp_abs_rhs(i64 %a) {
; CHECK-LABEL: cmp_abs_rhs:
; CHECK-NOT: mov.q [g64]
; CHECK: cmp.q [g64], r0
; CHECK: setult r0
; CHECK: ret
  %v = load i64, ptr @g64, align 8
  %c = icmp ult i64 %a, %v
  %r = zext i1 %c to i64
  ret i64 %r
}

define i32 @cmp_abs_imm_one() {
; CHECK-LABEL: cmp_abs_imm_one:
; CHECK-NOT: mov.l [g32]
; CHECK: set r0
; CHECK: cmp.l r0, [g32]
; CHECK: seteq r0
; CHECK: ret
  %v = load i32, ptr @g32, align 4
  %c = icmp eq i32 %v, 1
  %r = zext i1 %c to i32
  ret i32 %r
}

define i32 @sum_postinc_alu(ptr %p, i32 %n) {
; CHECK-LABEL: sum_postinc_alu:
; CHECK: add.l [r0++],
; CHECK: ret
entry:
  %empty = icmp eq i32 %n, 0
  br i1 %empty, label %done, label %loop

loop:
  %it = phi ptr [ %p, %entry ], [ %next, %loop ]
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  %v = load i32, ptr %it, align 4
  %acc.next = add i32 %acc, %v
  %next = getelementptr i32, ptr %it, i64 1
  %i.next = add i32 %i, 1
  %more = icmp ult i32 %i.next, %n
  br i1 %more, label %loop, label %done

done:
  %result = phi i32 [ 0, %entry ], [ %acc.next, %loop ]
  ret i32 %result
}

define i32 @scale_store_postinc(ptr %dst, ptr %src, i32 %n) {
; CHECK-LABEL: scale_store_postinc:
; CHECK: mov.l [r1++],
; CHECK: mov.l {{.*}}, [r0++]
; CHECK: ret
entry:
  %empty = icmp eq i32 %n, 0
  br i1 %empty, label %done, label %loop

loop:
  %dst.it = phi ptr [ %dst, %entry ], [ %dst.next, %loop ]
  %src.it = phi ptr [ %src, %entry ], [ %src.next, %loop ]
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %v = load i32, ptr %src.it, align 4
  %sh = shl i32 %v, 2
  %out = or i32 %sh, 1
  store i32 %out, ptr %dst.it, align 4
  %dst.next = getelementptr i32, ptr %dst.it, i64 1
  %src.next = getelementptr i32, ptr %src.it, i64 1
  %i.next = add i32 %i, 1
  %more = icmp ult i32 %i.next, %n
  br i1 %more, label %loop, label %done

done:
  %result = phi i32 [ 0, %entry ], [ %n, %loop ]
  ret i32 %result
}

define i32 @load_postinc_keep_base(ptr %p, ptr %slot) {
; CHECK-LABEL: load_postinc_keep_base:
; CHECK: mov.l [r0++],
; CHECK: mov.q r0, [r1]
; CHECK: ret
  %v = load i32, ptr %p, align 4
  %next = getelementptr i32, ptr %p, i64 1
  store ptr %next, ptr %slot, align 8
  ret i32 %v
}
