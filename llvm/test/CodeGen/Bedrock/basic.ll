; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s

@g = global i64 5

define i64 @ret42() {
; CHECK-LABEL: ret42:
; CHECK:       MOV.Q 42, D0
; CHECK-NEXT:  RET
entry:
  ret i64 42
}

define i64 @add2(i64 %a, i64 %b) {
; CHECK-LABEL: add2:
; CHECK:       ADD.Q D1, D0
; CHECK-NEXT:  RET
entry:
  %s = add i64 %a, %b
  ret i64 %s
}

declare i64 @callee(i64, i64)

define i64 @caller(i64 %a) {
; CHECK-LABEL: caller:
; CHECK:       MOV.Q 7, D1
; CHECK-NEXT:  JMP.W callee@WORD_PCREL16
entry:
  %r = call i64 @callee(i64 %a, i64 7)
  ret i64 %r
}

define i64 @load64(ptr %p) {
; CHECK-LABEL: load64:
; CHECK:       MOV.Q [A0], D0
; CHECK-NEXT:  RET
entry:
  %v = load i64, ptr %p, align 8
  ret i64 %v
}

define void @store64(ptr %p, i64 %v) {
; CHECK-LABEL: store64:
; CHECK:       MOV.Q D0, [A0]
; CHECK-NEXT:  RET
entry:
  store i64 %v, ptr %p, align 8
  ret void
}

define i64 @load_global() {
; CHECK-LABEL: load_global:
; CHECK:       MOV.Q [g@ABS64], D0
; CHECK-NEXT:  RET
entry:
  %v = load i64, ptr @g, align 8
  ret i64 %v
}

define i64 @shl3(i64 %a) {
; CHECK-LABEL: shl3:
; CHECK:       SHL.Q 3, D0
; CHECK-NEXT:  RET
entry:
  %s = shl i64 %a, 3
  ret i64 %s
}

define i64 @lshr1(i64 %a) {
; CHECK-LABEL: lshr1:
; CHECK:       SHR.Q 1, D0
; CHECK-NEXT:  RET
entry:
  %s = lshr i64 %a, 1
  ret i64 %s
}

define i64 @ashr1(i64 %a) {
; CHECK-LABEL: ashr1:
; CHECK:       SAR.Q 1, D0
; CHECK-NEXT:  RET
entry:
  %s = ashr i64 %a, 1
  ret i64 %s
}

define i64 @shl_var(i64 %a, i64 %b) {
; CHECK-LABEL: shl_var:
; CHECK:       SHL.Q D1, D0
; CHECK-NEXT:  RET
entry:
  %s = shl i64 %a, %b
  ret i64 %s
}

declare i64 @llvm.fshl.i64(i64, i64, i64)
declare i32 @llvm.fshr.i32(i32, i32, i32)

define i32 @rotl5(i32 %a) {
; CHECK-LABEL: rotl5:
; CHECK:       ROL.L 5, D0
; CHECK-NEXT:  RET
entry:
  %shl = shl i32 %a, 5
  %shr = lshr i32 %a, 27
  %r = or i32 %shl, %shr
  ret i32 %r
}

define i64 @rotl_var(i64 %a, i64 %b) {
; CHECK-LABEL: rotl_var:
; CHECK:       ROL.Q D1, D0
; CHECK-NEXT:  RET
entry:
  %r = call i64 @llvm.fshl.i64(i64 %a, i64 %a, i64 %b)
  ret i64 %r
}

define i32 @rotr_var32(i32 %a, i32 %b) {
; CHECK-LABEL: rotr_var32:
; CHECK:       ROR.L D1, D0
; CHECK-NEXT:  RET
entry:
  %r = call i32 @llvm.fshr.i32(i32 %a, i32 %a, i32 %b)
  ret i32 %r
}

define i32 @lshr_var32(i32 %a, i32 %b) {
; CHECK-LABEL: lshr_var32:
; CHECK:       SHR.L D1, D0
; CHECK-NEXT:  RET
entry:
  %s = lshr i32 %a, %b
  ret i32 %s
}

define i32 @bitops32(i32 %a, i32 %b) {
; CHECK-LABEL: bitops32:
; CHECK:       AND.L 7, D0
; CHECK-NEXT:  SHL.L 2, D0
; CHECK-NEXT:  INC.L D0
; CHECK-NEXT:  XOR.L D1, D0
; CHECK-NEXT:  RET
entry:
  %and = and i32 %a, 7
  %shl = shl i32 %and, 2
  %or = or i32 %shl, 1
  %xor = xor i32 %or, %b
  ret i32 %xor
}

define i32 @muldiv32(i32 %a, i32 %b) {
; CHECK-LABEL: muldiv32:
; CHECK:       MULU.L D1, D0
; CHECK:       MODS.L D1, D2
; CHECK:       DIVS.L D1, D0
; CHECK:       ADD.L D2, D0
; CHECK-NEXT:  RET
entry:
  %m = mul i32 %a, %b
  %d = sdiv i32 %m, %b
  %r = srem i32 %m, %b
  %s = add i32 %d, %r
  ret i32 %s
}

define i64 @muldiv64(i64 %a, i64 %b) {
; CHECK-LABEL: muldiv64:
; CHECK:       MULU.Q
; CHECK:       MODS.Q
; CHECK:       DIVU.Q
; CHECK:       RET
entry:
  %m = mul i64 %a, %b
  %d = udiv i64 %m, %b
  %r = srem i64 %m, %b
  %s = add i64 %d, %r
  ret i64 %s
}

define i32 @sdiv_const3(i32 %a) {
; CHECK-LABEL: sdiv_const3:
; CHECK:       MULHS.L
; CHECK:       RET
entry:
  %r = sdiv i32 %a, 3
  ret i32 %r
}

define i32 @urem_const5(i32 %a) {
; CHECK-LABEL: urem_const5:
; CHECK:       MULHU.L
; CHECK:       RET
entry:
  %r = urem i32 %a, 5
  ret i32 %r
}

define i64 @zext32(i32 %a) {
; CHECK-LABEL: zext32:
; CHECK:       EXTZQ.L D0, D0
; CHECK-NEXT:  RET
entry:
  %x = zext i32 %a to i64
  ret i64 %x
}

define i64 @sext32(i32 %a) {
; CHECK-LABEL: sext32:
; CHECK:       EXTSQ.L D0, D0
; CHECK-NEXT:  RET
entry:
  %x = sext i32 %a to i64
  ret i64 %x
}

define i32 @trunc32(i64 %a) {
; CHECK-LABEL: trunc32:
; CHECK:       RET
entry:
  %x = trunc i64 %a to i32
  ret i32 %x
}

define zeroext i1 @load_bool(ptr %p) {
; CHECK-LABEL: load_bool:
; CHECK:       EXTZL.B [A0], D0
; CHECK-NEXT:  RET
entry:
  %v = load i1, ptr %p, align 1
  ret i1 %v
}

define i32 @zextload8(ptr %p) {
; CHECK-LABEL: zextload8:
; CHECK:       EXTZL.B [A0], D0
; CHECK-NEXT:  RET
entry:
  %v = load i8, ptr %p, align 1
  %x = zext i8 %v to i32
  ret i32 %x
}

define i64 @sextload16(ptr %p) {
; CHECK-LABEL: sextload16:
; CHECK:       EXTSQ.W [A0], D0
; CHECK-NEXT:  RET
entry:
  %v = load i16, ptr %p, align 2
  %x = sext i16 %v to i64
  ret i64 %x
}

define void @store_bool(ptr %p, i1 zeroext %v) {
; CHECK-LABEL: store_bool:
; CHECK:       MOV.B D0, [A0]
; CHECK-NEXT:  RET
entry:
  store i1 %v, ptr %p, align 1
  ret void
}

define void @store_trunc16(ptr %p, i32 %v) {
; CHECK-LABEL: store_trunc16:
; CHECK:       MOV.W D0, [A0]
; CHECK-NEXT:  RET
entry:
  %x = trunc i32 %v to i16
  store i16 %x, ptr %p, align 2
  ret void
}

define double @fp_const() {
; CHECK-LABEL: fp_const:
; CHECK:       FMOV.D [{{.*}}@ABS64], F0
; CHECK-NEXT:  RET
entry:
  ret double 0x3ff3c083126e978d
}

declare double @llvm.fabs.f64(double)
declare double @llvm.copysign.f64(double, double)
declare float @llvm.copysign.f32(float, float)

define double @fabs64(double %x) {
; CHECK-LABEL: fabs64:
; CHECK:       FABS.D F0, F0
; CHECK-NEXT:  RET
entry:
  %r = call double @llvm.fabs.f64(double %x)
  ret double %r
}

define float @fneg32(float %x) {
; CHECK-LABEL: fneg32:
; CHECK:       FNEG.S F0, F0
; CHECK-NEXT:  RET
entry:
  %r = fneg float %x
  ret float %r
}

define double @sitofp32(i32 %x) {
; CHECK-LABEL: sitofp32:
; CHECK:       FCVT D0, F0
; CHECK-NEXT:  RET
entry:
  %r = sitofp i32 %x to double
  ret double %r
}

define i64 @fptosi64(double %x) {
; CHECK-LABEL: fptosi64:
; CHECK:       FCVT F0, D0
; CHECK-NEXT:  RET
entry:
  %r = fptosi double %x to i64
  ret i64 %r
}

define double @fpext32(float %x) {
; CHECK-LABEL: fpext32:
; CHECK:       FCVT F0, F0
; CHECK-NEXT:  RET
entry:
  %r = fpext float %x to double
  ret double %r
}

define double @fpextload32(ptr %p) {
; CHECK-LABEL: fpextload32:
; CHECK:       FMOV.S [A0], F0
; CHECK-NEXT:  FCVT F0, F0
; CHECK-NEXT:  RET
entry:
  %v = load float, ptr %p, align 4
  %r = fpext float %v to double
  ret double %r
}

define double @copysign64(double %mag, double %sign) {
; CHECK-LABEL: copysign64:
; CHECK:       FCOPYSIGN.D F1, F0, F0
; CHECK-NEXT:  RET
entry:
  %r = call double @llvm.copysign.f64(double %mag, double %sign)
  ret double %r
}

define float @copysign32(float %mag, float %sign) {
; CHECK-LABEL: copysign32:
; CHECK:       FCOPYSIGN.S F1, F0, F0
; CHECK-NEXT:  RET
entry:
  %r = call float @llvm.copysign.f32(float %mag, float %sign)
  ret float %r
}

define i64 @bitcast_f64_to_i64(double %x) {
; CHECK-LABEL: bitcast_f64_to_i64:
; CHECK:       FMOV.D F0, [SP
; CHECK:       MOV.Q [SP
; CHECK:       RET
entry:
  %r = bitcast double %x to i64
  ret i64 %r
}

define float @bitcast_i32_to_f32(i32 %x) {
; CHECK-LABEL: bitcast_i32_to_f32:
; CHECK:       MOV.L D0, [SP
; CHECK:       FMOV.S [SP
; CHECK:       RET
entry:
  %r = bitcast i32 %x to float
  ret float %r
}

define i128 @sdiv128(i128 %a, i128 %b) {
; CHECK-LABEL: sdiv128:
; CHECK:       JMP.L __divti3@WORD_PCREL32
entry:
  %r = sdiv i128 %a, %b
  ret i128 %r
}

define double @fselect64(i64 %x, double %a, double %b) {
; CHECK-LABEL: fselect64:
; CHECK:       CMP.Q
; CHECK:       FMOVEQ.D
; CHECK-NOT:   J{{[A-Z]*}}.W
; CHECK:       RET
entry:
  %cmp = icmp eq i64 %x, 0
  %r = select i1 %cmp, double %a, double %b
  ret double %r
}

define i64 @select_movcc64(i64 %a, i64 %b, i64 %x, i64 %y) {
; CHECK-LABEL: select_movcc64:
; CHECK:       CMP.Q
; CHECK:       MOVEQ.Q
; CHECK-NOT:   J{{[A-Z]*}}.W
; CHECK:       RET
entry:
  %cmp = icmp eq i64 %a, %b
  %t = xor i64 %x, 123
  %f = or i64 %y, 456
  %r = select i1 %cmp, i64 %t, i64 %f
  ret i64 %r
}

define i32 @select_movcc32(i32 %a, i32 %b, i32 %x, i32 %y) {
; CHECK-LABEL: select_movcc32:
; CHECK:       CMP.L
; CHECK:       MOVGT.L
; CHECK-NOT:   J{{[A-Z]*}}.W
; CHECK:       RET
entry:
  %cmp = icmp sgt i32 %a, %b
  %t = add i32 %x, 7
  %f = sub i32 %y, 9
  %r = select i1 %cmp, i32 %t, i32 %f
  ret i32 %r
}

define i32 @fcmp_ordered(double %a) {
; CHECK-LABEL: fcmp_ordered:
; CHECK:       FCMP.D F0, F0
; CHECK:       JVS.W
; CHECK:       RET
entry:
  %cmp = fcmp ord double %a, %a
  br i1 %cmp, label %then, label %else

then:
  ret i32 1

else:
  ret i32 0
}

define i32 @fcmp_ugt(double %a, double %b) {
; CHECK-LABEL: fcmp_ugt:
; CHECK:       FCMP.D
; CHECK:       JGE.W
; CHECK:       RET
entry:
  %cmp = fcmp ugt double %a, %b
  br i1 %cmp, label %then, label %else

then:
  ret i32 1

else:
  ret i32 0
}

define i64 @slt_branch(i64 %a, i64 %b) {
; CHECK-LABEL: slt_branch:
; CHECK:       CMP.Q D1, D0
; CHECK-NEXT:  JGE.W {{.*}}@WORD_PCREL16
; CHECK-NOT:   JMP.W
; CHECK:       RET
entry:
  %cmp = icmp slt i64 %a, %b
  br i1 %cmp, label %then, label %else

then:
  ret i64 %a

else:
  ret i64 %b
}

define i64 @ult_branch(i64 %a, i64 %b) {
; CHECK-LABEL: ult_branch:
; CHECK:       CMP.Q D1, D0
; CHECK-NEXT:  JUGE.W {{.*}}@WORD_PCREL16
; CHECK-NOT:   JMP.W
; CHECK:       RET
entry:
  %cmp = icmp ult i64 %a, %b
  br i1 %cmp, label %then, label %else

then:
  ret i64 %a

else:
  ret i64 %b
}

define i64 @select_min(i64 %a, i64 %b) {
; CHECK-LABEL: select_min:
; CHECK:       MINS.Q D1, D0
; CHECK-NEXT:  RET
entry:
  %cmp = icmp slt i64 %a, %b
  %sel = select i1 %cmp, i64 %a, i64 %b
  ret i64 %sel
}

define i64 @select_umin(i64 %a, i64 %b) {
; CHECK-LABEL: select_umin:
; CHECK:       MINU.Q D1, D0
; CHECK-NEXT:  RET
entry:
  %min = tail call i64 @llvm.umin.i64(i64 %a, i64 %b)
  ret i64 %min
}

define i32 @select_umax_i32(i32 %a, i32 %b) {
; CHECK-LABEL: select_umax_i32:
; CHECK:       MAXU.L D1, D0
; CHECK-NEXT:  RET
entry:
  %max = tail call i32 @llvm.umax.i32(i32 %a, i32 %b)
  ret i32 %max
}

define i32 @dense_switch(i32 %x, i32 %bias) {
; CHECK-LABEL: dense_switch:
; CHECK-NOT: LJTI
; CHECK:       CMP.L
; CHECK:       RET
entry:
  switch i32 %x, label %default [
    i32 0, label %case0
    i32 1, label %case1
    i32 2, label %case2
    i32 3, label %case3
    i32 4, label %case4
    i32 5, label %case5
    i32 6, label %case6
    i32 7, label %case7
  ]

case0:
  %v0 = add i32 %bias, 3
  ret i32 %v0

case1:
  %v1 = sub i32 %bias, 5
  ret i32 %v1

case2:
  %v2 = xor i32 %bias, 7
  ret i32 %v2

case3:
  %v3 = or i32 %bias, 11
  ret i32 %v3

case4:
  %v4 = and i32 %bias, 13
  ret i32 %v4

case5:
  %v5 = shl i32 %bias, 1
  ret i32 %v5

case6:
  %v6 = ashr i32 %bias, 1
  ret i32 %v6

case7:
  %v7 = mul i32 %bias, 3
  ret i32 %v7

default:
  ret i32 %bias
}

define i64 @stack_slot(i64 %a) {
; CHECK-LABEL: stack_slot:
; CHECK:       SUB.Q 16, SP
; CHECK-NEXT:  .cfi_def_cfa_offset 24
; CHECK-NEXT:  MOV.Q D0, [SP + 8]
; CHECK-NEXT:  MOV.Q [SP + 8], D0
; CHECK-NEXT:  ADD.Q 16, SP
; CHECK-NEXT:  RET
entry:
  %slot = alloca i64, align 8
  store volatile i64 %a, ptr %slot, align 8
  %v = load volatile i64, ptr %slot, align 8
  ret i64 %v
}

declare void @use_ptr(ptr)

define void @frame_addr_arg() {
; CHECK-LABEL: frame_addr_arg:
; CHECK:       SUB.Q 24, SP
; CHECK-NEXT:  .cfi_def_cfa_offset 32
; CHECK-NEXT:  LEA [SP + 16], A0
; CHECK-NEXT:  CALL use_ptr@PCREL16
; CHECK-NEXT:  ADD.Q 24, SP
; CHECK-NEXT:  RET
entry:
  %slot = alloca i64, align 8
  call void @use_ptr(ptr %slot)
  ret void
}

declare i64 @llvm.umin.i64(i64, i64)
declare i32 @llvm.umax.i32(i32, i32)
