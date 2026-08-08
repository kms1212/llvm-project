; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O2 -filetype=obj < %s | llvm-objdump -dr - | FileCheck %s --check-prefix=OBJ

define void @volatile_countdown(ptr %dst, i32 %n, i8 %value) {
; CHECK-LABEL: volatile_countdown:
; CHECK-NOT: repg
; CHECK: mov.b
; CHECK: djt
; CHECK: ret
; OBJ: cb c3 80 e4 00{{[ \t]+}}djt{{[ \t]+}}r1, [pc + 0]
; OBJ-NEXT: {{.*}}R_BEDROCK_PCREL8S{{.*}}.L{{.*}}+0x4
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

define void @volatile_countdown_medium(ptr %dst, i32 %n, i8 %value) {
; OBJ-LABEL: <volatile_countdown_medium>:
; OBJ: djt{{[ \t]+}}r1, [pc + 0]
; OBJ-NEXT: {{.*}}R_BEDROCK_PCREL16S{{.*}}.L{{.*}}+0x4
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
  call void asm sideeffect ".space 160", ""()
  %next.ptr = getelementptr i8, ptr %ptr, i64 1
  %next.iv = add nsw i64 %iv, -1
  %done = icmp eq i64 %next.iv, 0
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

define void @volatile_countdown_far(ptr %dst, i32 %n, i8 %value) {
; OBJ-LABEL: <volatile_countdown_far>:
; OBJ: djt{{[ \t]+}}r1, [pc + 0]
; OBJ-NEXT: {{.*}}R_BEDROCK_PCREL32S{{.*}}.L{{.*}}+0x4
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
  call void asm sideeffect ".space 32768", ""()
  %next.ptr = getelementptr i8, ptr %ptr, i64 1
  %next.iv = add nsw i64 %iv, -1
  %done = icmp eq i64 %next.iv, 0
  br i1 %done, label %exit, label %loop

exit:
  ret void
}

define void @nonvolatile_countdown(ptr %dst, i32 %n, i8 %value) {
; CHECK-LABEL: nonvolatile_countdown:
; CHECK: repgf
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

define void @fixed_count_copy(ptr %dst, ptr %src) minsize optsize {
; CHECK-LABEL: fixed_count_copy:
; CHECK: lea.q 16, [[COUNT:r[0-7]]]
; CHECK: repgf [[COUNT]], {
; CHECK: mov.q
; CHECK: lea.q
; CHECK: }
; CHECK-NOT: cmp.q 128
; CHECK-NOT: jne
; CHECK: ret
; OBJ-LABEL: <fixed_count_copy>:
; OBJ: repg
entry:
  br label %loop

loop:
  %offset = phi i64 [ 0, %entry ], [ %next, %loop ]
  %src.addr = getelementptr i8, ptr %src, i64 %offset
  %dst.addr = getelementptr i8, ptr %dst, i64 %offset
  %value = load i64, ptr %src.addr, align 8
  store i64 %value, ptr %dst.addr, align 8
  %next = add nuw nsw i64 %offset, 8
  %done = icmp eq i64 %next, 128
  br i1 %done, label %exit, label %loop

exit:
  ret void
}
