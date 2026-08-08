; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -code-model=tiny -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -code-model=tiny -O2 -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d -r --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

@src64 = external global i64
@dst64 = external global i64

define ptr @copy_word(ptr %dst, ptr %src) {
; CHECK-LABEL: copy_word:
; CHECK: mov.l [r1], [r0]
; CHECK: ret
  %v = load i32, ptr %src, align 4
  store i32 %v, ptr %dst, align 4
  ret ptr %dst
}

define ptr @copy_quad(ptr %dst, ptr %src) {
; CHECK-LABEL: copy_quad:
; CHECK: mov.q [r1], [r0]
; CHECK: ret
  %v = load i64, ptr %src, align 8
  store i64 %v, ptr %dst, align 8
  ret ptr %dst
}

define void @copy_global_quad() {
; CHECK-LABEL: copy_global_quad:
; CHECK: mov.q [0], [0]
; CHECK: ret
; OBJ-LABEL: <copy_global_quad>:
; OBJ-NEXT: {{[0-9a-f]+}}: {{.*}} mov.q {{.*}}[0], [0]
; OBJ-NEXT: R_BEDROCK_ABS32S src64
; OBJ-NEXT: R_BEDROCK_ABS32S dst64
; OBJ-NEXT: {{[0-9a-f]+}}: {{.*}} ret
  %v = load i64, ptr @src64, align 8
  store i64 %v, ptr @dst64, align 8
  ret void
}

define ptr @copy_words_postinc(ptr %dst, ptr %src, i32 %n) {
; CHECK-LABEL: copy_words_postinc:
; CHECK: mov.l [r1++], [{{r[0-9]+}}++]
; CHECK: ret
entry:
  %empty = icmp eq i32 %n, 0
  br i1 %empty, label %done, label %loop

loop:
  %d = phi ptr [ %dst, %entry ], [ %d.next, %loop ]
  %s = phi ptr [ %src, %entry ], [ %s.next, %loop ]
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %v = load i32, ptr %s, align 4
  store i32 %v, ptr %d, align 4
  %d.next = getelementptr i32, ptr %d, i64 1
  %s.next = getelementptr i32, ptr %s, i64 1
  %i.next = add i32 %i, 1
  %more = icmp ult i32 %i.next, %n
  br i1 %more, label %loop, label %done

done:
  ret ptr %dst
}

define i32 @copy_countdown(ptr %dst, ptr %src, i32 %n) {
; CHECK-LABEL: copy_countdown:
; CHECK: repgf {{r[0-9]+}}, {
; CHECK-NEXT: mov.q [r1++], [r0++]
; CHECK-NEXT: }
; CHECK-NOT: djt
; CHECK: ret
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %pre, label %zero

pre:
  %count = zext i32 %n to i64
  br label %loop

loop:
  %d = phi ptr [ %dst, %pre ], [ %d.next, %loop ]
  %s = phi ptr [ %src, %pre ], [ %s.next, %loop ]
  %c = phi i64 [ %count, %pre ], [ %next, %loop ]
  %v = load i64, ptr %s, align 8
  store i64 %v, ptr %d, align 8
  %d.next = getelementptr i64, ptr %d, i64 1
  %s.next = getelementptr i64, ptr %s, i64 1
  %next = add nsw i64 %c, -1
  %exit = icmp eq i64 %next, 0
  br i1 %exit, label %done, label %loop

done:
  ret i32 %n

zero:
  ret i32 0
}

define i32 @scan_postinc_across_exit(ptr %p, i32 %n) {
; CHECK-LABEL: scan_postinc_across_exit:
; CHECK: mov.l [{{r[0-9]+}}++], [[VALUE:r[0-9]+]]
; CHECK-NEXT: testjeq.l [[VALUE]], [[VALUE]],
; CHECK: ret
entry:
  br label %loop

loop:
  %cur = phi ptr [ %p, %entry ], [ %next, %cont ]
  %i = phi i32 [ 0, %entry ], [ %inc, %cont ]
  %v = load i32, ptr %cur, align 4
  %iszero = icmp eq i32 %v, 0
  br i1 %iszero, label %done, label %cont

cont:
  %next = getelementptr i32, ptr %cur, i64 1
  %inc = add i32 %i, 1
  %more = icmp ult i32 %inc, %n
  br i1 %more, label %loop, label %done

done:
  %res = phi i32 [ %i, %loop ], [ %inc, %cont ]
  ret i32 %res
}
