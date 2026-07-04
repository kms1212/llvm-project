; RUN: llc -mtriple=bedrock -O0 -verify-machineinstrs < %s | FileCheck %s

define dso_local signext i32 @sum(ptr noundef %p, i32 noundef signext %n) noinline nounwind optnone {
entry:
  %p.addr = alloca ptr, align 8
  %n.addr = alloca i32, align 4
  %i = alloca i32, align 4
  %acc = alloca i32, align 4
  store ptr %p, ptr %p.addr, align 8
  store i32 %n, ptr %n.addr, align 4
  store i32 0, ptr %i, align 4
  store i32 0, ptr %acc, align 4
  br label %while.cond

while.cond:
  %0 = load i32, ptr %i, align 4
  %1 = load i32, ptr %n.addr, align 4
  %cmp = icmp slt i32 %0, %1
  br i1 %cmp, label %while.body, label %while.end

while.body:
  %2 = load i32, ptr %acc, align 4
  %3 = load ptr, ptr %p.addr, align 8
  %4 = load i32, ptr %i, align 4
  %idxprom = sext i32 %4 to i64
  %arrayidx = getelementptr inbounds i32, ptr %3, i64 %idxprom
  %5 = load i32, ptr %arrayidx, align 4
  %add = add nsw i32 %2, %5
  store i32 %add, ptr %acc, align 4
  %6 = load i32, ptr %i, align 4
  %add1 = add nsw i32 %6, 1
  store i32 %add1, ptr %i, align 4
  br label %while.cond

while.end:
  %7 = load i32, ptr %acc, align 4
  ret i32 %7
}

; CHECK-LABEL: sum:
; CHECK-NOT:   MOV.L D0, D0
; CHECK:       CLR.Q D0
; CHECK-NEXT:  MOV.L D0, [SP + 16]
; CHECK-NEXT:  MOV.L D0, [SP + 12]
; CHECK-NEXT: .LBB0_1:
; CHECK:       CMP.L D1, D0
; CHECK-NEXT:  JGE.W .LBB0_3@WORD_PCREL16
; CHECK-NEXT: # %bb.2:
; CHECK:       MOV.Q [SP + 24], A0
; CHECK-NEXT:  MOV.L [SP + 16], D1
; CHECK-NEXT:  MOV.L [A0 + D1.L * 4], D1
; CHECK-NEXT:  ADD.L D1, D0
; CHECK-NEXT:  MOV.L D0, [SP + 12]
; CHECK-NEXT:  INC.L [SP + 16]
; CHECK-NOT:   ADD.L 1, D0

define dso_local signext i32 @call_stack_args(i32 noundef signext %a,
                                               i32 noundef signext %b)
    noinline nounwind optnone {
entry:
  %a.addr = alloca i32, align 4
  %b.addr = alloca i32, align 4
  store i32 %a, ptr %a.addr, align 4
  store i32 %b, ptr %b.addr, align 4
  %0 = load i32, ptr %a.addr, align 4
  %1 = load i32, ptr %b.addr, align 4
  %call = call signext i32 (...) @ext_add(i32 noundef zeroext %0,
                                          i32 noundef zeroext %1)
  ret i32 %call
}

declare dso_local signext i32 @ext_add(...)

; CHECK-LABEL: call_stack_args:
; CHECK-NOT:   MOV.Q SP, A0
; CHECK:       MOV.L D0, [SP + 20]
; CHECK-NEXT:  MOV.L D1, [SP + 16]
; CHECK-NEXT:  MOV.L [SP + 20], D0
; CHECK-NEXT:  MOV.L [SP + 16], D1
; CHECK-NEXT:  CALL ext_add@PCREL16

define dso_local signext i32 @entry_arg_shuffle(i32 noundef signext %a,
                                                 i32 noundef signext %b,
                                                 i32 noundef signext %c,
                                                 i32 noundef signext %d)
    noinline nounwind optnone {
entry:
  %a.addr = alloca i32, align 4
  %b.addr = alloca i32, align 4
  %c.addr = alloca i32, align 4
  %d.addr = alloca i32, align 4
  store i32 %a, ptr %a.addr, align 4
  store i32 %b, ptr %b.addr, align 4
  store i32 %c, ptr %c.addr, align 4
  store i32 %d, ptr %d.addr, align 4
  %0 = load i32, ptr %a.addr, align 4
  %1 = load i32, ptr %b.addr, align 4
  %2 = add nsw i32 %0, %1
  %3 = load i32, ptr %c.addr, align 4
  %4 = add nsw i32 %2, %3
  %5 = load i32, ptr %d.addr, align 4
  %6 = add nsw i32 %4, %5
  ret i32 %6
}

; CHECK-LABEL: entry_arg_shuffle:
; CHECK-NOT:   MOV.Q D3, [SP + 16]
; CHECK-NOT:   MOV.Q [SP + 8], D1
; CHECK:       MOV.L D0, [SP + 12]
; CHECK-NEXT:  MOV.L D1, [SP + 8]
; CHECK-NEXT:  MOV.L D2, [SP + 4]
; CHECK-NEXT:  MOV.L D3, [SP + 0]

define dso_local signext i32 @store_reload_chain(i32 noundef signext %a,
                                                  i32 noundef signext %b,
                                                  i32 noundef signext %c)
    noinline nounwind optnone {
entry:
  %a.addr = alloca i32, align 4
  %b.addr = alloca i32, align 4
  %c.addr = alloca i32, align 4
  %tmp = alloca i32, align 4
  store i32 %a, ptr %a.addr, align 4
  store i32 %b, ptr %b.addr, align 4
  store i32 %c, ptr %c.addr, align 4
  %0 = load i32, ptr %a.addr, align 4
  %1 = load i32, ptr %b.addr, align 4
  %mul = mul nsw i32 %0, %1
  store i32 %mul, ptr %tmp, align 4
  %2 = load i32, ptr %tmp, align 4
  %3 = load i32, ptr %c.addr, align 4
  %add = add nsw i32 %2, %3
  store i32 %add, ptr %tmp, align 4
  %4 = load i32, ptr %tmp, align 4
  ret i32 %4
}

; CHECK-LABEL: store_reload_chain:
; CHECK:       MULU.L D1, D0
; CHECK-NEXT:  MOV.L D0, [SP + 0]
; CHECK-NEXT:  MOV.L [SP + 4], D1
; CHECK-NEXT:  ADD.L D1, [SP + 0]
; CHECK-NEXT:  MOV.L [SP + 0], D0

define dso_local signext i32 @memdest_add_chain(i32 noundef signext %a,
                                                 i32 noundef signext %b,
                                                 i32 noundef signext %c)
    noinline nounwind optnone {
entry:
  %a.addr = alloca i32, align 4
  %b.addr = alloca i32, align 4
  %c.addr = alloca i32, align 4
  store i32 %a, ptr %a.addr, align 4
  store i32 %b, ptr %b.addr, align 4
  store i32 %c, ptr %c.addr, align 4
  %0 = load i32, ptr %a.addr, align 4
  %1 = load i32, ptr %b.addr, align 4
  %add = add nsw i32 %0, %1
  store i32 %add, ptr %a.addr, align 4
  %2 = load i32, ptr %c.addr, align 4
  %3 = load i32, ptr %a.addr, align 4
  %add2 = add nsw i32 %2, %3
  store i32 %add2, ptr %c.addr, align 4
  %4 = load i32, ptr %c.addr, align 4
  ret i32 %4
}

; CHECK-LABEL: memdest_add_chain:
; CHECK:       MOV.L [SP + 8], D1
; CHECK-NEXT:  ADD.L D1, [SP + 12]
; CHECK-NEXT:  MOV.L [SP + 12], D1
; CHECK-NEXT:  ADD.L D1, [SP + 4]
; CHECK-NEXT:  MOV.L [SP + 4], D0

define dso_local signext i32 @memdest_sub_chain(i32 noundef signext %a,
                                                 i32 noundef signext %b)
    noinline nounwind optnone {
entry:
  %a.addr = alloca i32, align 4
  %b.addr = alloca i32, align 4
  store i32 %a, ptr %a.addr, align 4
  store i32 %b, ptr %b.addr, align 4
  %0 = load i32, ptr %a.addr, align 4
  %1 = load i32, ptr %b.addr, align 4
  %sub = sub nsw i32 %0, %1
  store i32 %sub, ptr %a.addr, align 4
  %2 = load i32, ptr %a.addr, align 4
  ret i32 %2
}

; CHECK-LABEL: memdest_sub_chain:
; CHECK:       MOV.L D1, [SP + {{[0-9]+}}]
; CHECK-NEXT:  SUB.L D1, [SP + {{[0-9]+}}]
; CHECK-NEXT:  MOV.L [SP + {{[0-9]+}}], D0

define dso_local signext i32 @memdest_or_imm(i32 noundef signext %a)
    noinline nounwind optnone {
entry:
  %a.addr = alloca i32, align 4
  store i32 %a, ptr %a.addr, align 4
  %0 = load i32, ptr %a.addr, align 4
  %or = or i32 %0, 7
  store i32 %or, ptr %a.addr, align 4
  %1 = load i32, ptr %a.addr, align 4
  ret i32 %1
}

; CHECK-LABEL: memdest_or_imm:
; CHECK:       OR.L 7, [SP + {{[0-9]+}}]
; CHECK-NEXT:  MOV.L [SP + {{[0-9]+}}], D0

define dso_local signext i32 @ret_signext_call(ptr noundef %p)
    noinline nounwind optnone {
entry:
  %call = call signext i32 @callee(ptr noundef %p)
  ret i32 %call
}

declare dso_local signext i32 @callee(ptr)

; CHECK-LABEL: ret_signext_call:
; CHECK:       CALL callee@PCREL16
; CHECK-NOT:   SHL.Q 32
; CHECK-NOT:   SAR.Q 32
; CHECK:       RET
