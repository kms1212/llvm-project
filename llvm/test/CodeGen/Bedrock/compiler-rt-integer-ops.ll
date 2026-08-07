; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs -filetype=obj < %s \
; RUN:   -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

declare i32 @llvm.ctlz.i32(i32, i1 immarg)
declare i64 @llvm.ctlz.i64(i64, i1 immarg)
declare i32 @llvm.cttz.i32(i32, i1 immarg)
declare i64 @llvm.cttz.i64(i64, i1 immarg)
declare i32 @llvm.ctpop.i32(i32)
declare i64 @llvm.ctpop.i64(i64)
declare i32 @llvm.bswap.i32(i32)
declare i64 @llvm.bswap.i64(i64)
declare i32 @llvm.fshr.i32(i32, i32, i32)
declare i64 @llvm.fshr.i64(i64, i64, i64)
declare i32 @llvm.fshl.i32(i32, i32, i32)
declare i64 @llvm.fshl.i64(i64, i64, i64)
declare i32 @llvm.clmul.i32(i32, i32)
declare i64 @llvm.clmul.i64(i64, i64)
declare i128 @llvm.clmul.i128(i128, i128)
declare {i32, i1} @llvm.sadd.with.overflow.i32(i32, i32)
declare {i64, i1} @llvm.sadd.with.overflow.i64(i64, i64)
declare {i32, i1} @llvm.ssub.with.overflow.i32(i32, i32)
declare {i64, i1} @llvm.ssub.with.overflow.i64(i64, i64)
declare {i32, i1} @llvm.uadd.with.overflow.i32(i32, i32)
declare {i64, i1} @llvm.uadd.with.overflow.i64(i64, i64)
declare {i32, i1} @llvm.usub.with.overflow.i32(i32, i32)
declare {i64, i1} @llvm.usub.with.overflow.i64(i64, i64)

define i32 @ctlz_i32(i32 %value) {
; CHECK-LABEL: ctlz_i32:
; CHECK: clz.l{{[ \t]+}}r0, r0
; OBJ-LABEL: <ctlz_i32>:
; OBJ: c7 c0 a0 00{{.*}}clz.l{{[ \t]+}}r0, r0
  %result = call i32 @llvm.ctlz.i32(i32 %value, i1 false)
  ret i32 %result
}

define i64 @ctlz_i64_zero_undef(i64 %value) {
; CHECK-LABEL: ctlz_i64_zero_undef:
; CHECK: clz.q{{[ \t]+}}r0, r0
; OBJ-LABEL: <ctlz_i64_zero_undef>:
; OBJ: c7 c0 e0 00{{.*}}clz.q{{[ \t]+}}r0, r0
  %result = call i64 @llvm.ctlz.i64(i64 %value, i1 true)
  ret i64 %result
}

define i32 @cttz_i32_zero_undef(i32 %value) {
; CHECK-LABEL: cttz_i32_zero_undef:
; CHECK: ctz.l{{[ \t]+}}r0, r0
; OBJ-LABEL: <cttz_i32_zero_undef>:
; OBJ: c7 c0 a8 00{{.*}}ctz.l{{[ \t]+}}r0, r0
  %result = call i32 @llvm.cttz.i32(i32 %value, i1 true)
  ret i32 %result
}

define i64 @cttz_i64(i64 %value) {
; CHECK-LABEL: cttz_i64:
; CHECK: ctz.q{{[ \t]+}}r0, r0
  %result = call i64 @llvm.cttz.i64(i64 %value, i1 false)
  ret i64 %result
}

define i32 @ctpop_i32(i32 %value) {
; CHECK-LABEL: ctpop_i32:
; CHECK: popcnt.l{{[ \t]+}}r0, r0
; OBJ-LABEL: <ctpop_i32>:
; OBJ: c7 c2 80 00{{.*}}popcnt.l{{[ \t]+}}r0, r0
  %result = call i32 @llvm.ctpop.i32(i32 %value)
  ret i32 %result
}

define i64 @ctpop_i64(i64 %value) {
; CHECK-LABEL: ctpop_i64:
; CHECK: popcnt.q{{[ \t]+}}r0, r0
  %result = call i64 @llvm.ctpop.i64(i64 %value)
  ret i64 %result
}

define i32 @bswap_i32(i32 %value) {
; CHECK-LABEL: bswap_i32:
; CHECK: revbyte.l{{[ \t]+}}r0
; OBJ-LABEL: <bswap_i32>:
; OBJ: a2 a0{{.*}}revbyte.l{{[ \t]+}}r0
  %result = call i32 @llvm.bswap.i32(i32 %value)
  ret i32 %result
}

define i64 @bswap_i64(i64 %value) {
; CHECK-LABEL: bswap_i64:
; CHECK: revbyte.q{{[ \t]+}}r0
; OBJ-LABEL: <bswap_i64>:
; OBJ: a2 b0{{.*}}revbyte.q{{[ \t]+}}r0
  %result = call i64 @llvm.bswap.i64(i64 %value)
  ret i64 %result
}

define i64 @count_leading_ones_i64(i64 %value) {
; CHECK-LABEL: count_leading_ones_i64:
; CHECK: cls.q{{[ \t]+}}r0, r0
  %not = xor i64 %value, -1
  %result = call i64 @llvm.ctlz.i64(i64 %not, i1 false)
  ret i64 %result
}

define i32 @parity_i32(i32 %value) {
; CHECK-LABEL: parity_i32:
; CHECK: parity.l{{[ \t]+}}r0, r0
  %count = call i32 @llvm.ctpop.i32(i32 %value)
  %result = and i32 %count, 1
  ret i32 %result
}

define i64 @parity_i64(i64 %value) {
; CHECK-LABEL: parity_i64:
; CHECK: parity.q{{[ \t]+}}r0, r0
  %count = call i64 @llvm.ctpop.i64(i64 %value)
  %result = and i64 %count, 1
  ret i64 %result
}

define i32 @count_trailing_ones_i32(i32 %value) {
; CHECK-LABEL: count_trailing_ones_i32:
; CHECK: cts.l{{[ \t]+}}r0, r0
  %not = xor i32 %value, -1
  %result = call i32 @llvm.cttz.i32(i32 %not, i1 true)
  ret i32 %result
}

define i64 @mulhu_i64(i64 %lhs, i64 %rhs) {
; CHECK-LABEL: mulhu_i64:
; CHECK: mulhu.q{{[ \t]+}}r1, r0
  %lhs.wide = zext i64 %lhs to i128
  %rhs.wide = zext i64 %rhs to i128
  %product = mul i128 %lhs.wide, %rhs.wide
  %high = lshr i128 %product, 64
  %result = trunc i128 %high to i64
  ret i64 %result
}

define i64 @mulhs_i64(i64 %lhs, i64 %rhs) {
; CHECK-LABEL: mulhs_i64:
; CHECK: mulhs.q{{[ \t]+}}r1, r0
  %lhs.wide = sext i64 %lhs to i128
  %rhs.wide = sext i64 %rhs to i128
  %product = mul i128 %lhs.wide, %rhs.wide
  %high = lshr i128 %product, 64
  %result = trunc i128 %high to i64
  ret i64 %result
}

define i64 @mulhsu_i64(i64 %lhs, i64 %rhs) {
; CHECK-LABEL: mulhsu_i64:
; CHECK: mulhsu.q{{[ \t]+}}r1, r0
; OBJ-LABEL: <mulhsu_i64>:
; OBJ: c7 ef 48 e0{{.*}}mulhsu.q{{[ \t]+}}r1, r0
  %lhs.wide = sext i64 %lhs to i128
  %rhs.wide = zext i64 %rhs to i128
  %product = mul i128 %lhs.wide, %rhs.wide
  %high = lshr i128 %product, 64
  %result = trunc i128 %high to i64
  ret i64 %result
}

define i32 @clmul_i32(i32 %lhs, i32 %rhs) {
; CHECK-LABEL: clmul_i32:
; CHECK: clmul.l{{[ \t]+}}r1, r0
; OBJ-LABEL: <clmul_i32>:
; OBJ: c7 c2 98 01{{.*}}clmul.l{{[ \t]+}}r1, r0
  %result = call i32 @llvm.clmul.i32(i32 %lhs, i32 %rhs)
  ret i32 %result
}

define i64 @clmul_i64(i64 %lhs, i64 %rhs) {
; CHECK-LABEL: clmul_i64:
; CHECK: clmul.q{{[ \t]+}}r1, r0
; OBJ-LABEL: <clmul_i64>:
; OBJ: c7 c2 d8 01{{.*}}clmul.q{{[ \t]+}}r1, r0
  %result = call i64 @llvm.clmul.i64(i64 %lhs, i64 %rhs)
  ret i64 %result
}

define i64 @clmulh_i64(i64 %lhs, i64 %rhs) {
; CHECK-LABEL: clmulh_i64:
; CHECK: clmulh.q{{[ \t]+}}r1, r0
; OBJ-LABEL: <clmulh_i64>:
; OBJ: c7 c3 30 01{{.*}}clmulh.q{{[ \t]+}}r1, r0
  %lhs.wide = zext i64 %lhs to i128
  %rhs.wide = zext i64 %rhs to i128
  %product = call i128 @llvm.clmul.i128(i128 %lhs.wide, i128 %rhs.wide)
  %high = lshr i128 %product, 64
  %result = trunc i128 %high to i64
  ret i64 %result
}

define i128 @add_i128(i128 %lhs, i128 %rhs) {
; CHECK-LABEL: add_i128:
; CHECK: clrf{{[ \t]+}}2
; CHECK-NEXT: adc.q{{[ \t]+}}r2, r0
; CHECK-NEXT: adc.q{{[ \t]+}}r3, r1
; CHECK-NOT: cmp
; CHECK-NOT: setult
; CHECK: ret
; OBJ-LABEL: <add_i128>:
; OBJ: c0 25 82{{.*}}clrf{{[ \t]+}}2
; OBJ-NEXT: {{.*}}c3 39 00{{.*}}adc.q{{[ \t]+}}r2, r0
; OBJ-NEXT: {{.*}}c3 39 81{{.*}}adc.q{{[ \t]+}}r3, r1
  %result = add i128 %lhs, %rhs
  ret i128 %result
}

define i128 @sub_i128(i128 %lhs, i128 %rhs) {
; CHECK-LABEL: sub_i128:
; CHECK: clrf{{[ \t]+}}2
; CHECK-NEXT: sbb.q{{[ \t]+}}r2, r0
; CHECK-NEXT: sbb.q{{[ \t]+}}r3, r1
; CHECK-NOT: cmp
; CHECK-NOT: setult
; CHECK: ret
; OBJ-LABEL: <sub_i128>:
; OBJ: c0 25 82{{.*}}clrf{{[ \t]+}}2
; OBJ-NEXT: {{.*}}c3 79 00{{.*}}sbb.q{{[ \t]+}}r2, r0
; OBJ-NEXT: {{.*}}c3 79 81{{.*}}sbb.q{{[ \t]+}}r3, r1
  %result = sub i128 %lhs, %rhs
  ret i128 %result
}

define {i32, i1} @saddo_i32(i32 %lhs, i32 %rhs) {
; CHECK-LABEL: saddo_i32:
; CHECK: clrf{{[ \t]+}}2
; CHECK-NEXT: adc.l{{[ \t]+}}r1, r0
; CHECK-NEXT: setvs{{[ \t]+}}r1
; OBJ-LABEL: <saddo_i32>:
; OBJ: c0 25 82{{.*}}clrf{{[ \t]+}}2
; OBJ-NEXT: {{.*}}c3 28 80{{.*}}adc.l{{[ \t]+}}r1, r0
; OBJ-NEXT: {{.*}}a1 18{{.*}}setvs{{[ \t]+}}r1
  %result = call {i32, i1} @llvm.sadd.with.overflow.i32(i32 %lhs, i32 %rhs)
  ret {i32, i1} %result
}

define {i64, i1} @saddo_i64(i64 %lhs, i64 %rhs) {
; CHECK-LABEL: saddo_i64:
; CHECK: clrf{{[ \t]+}}2
; CHECK-NEXT: adc.q{{[ \t]+}}r1, r0
; CHECK-NEXT: setvs{{[ \t]+}}r1
; OBJ-LABEL: <saddo_i64>:
; OBJ: c0 25 82{{.*}}clrf{{[ \t]+}}2
; OBJ-NEXT: {{.*}}c3 38 80{{.*}}adc.q{{[ \t]+}}r1, r0
; OBJ-NEXT: {{.*}}a1 18{{.*}}setvs{{[ \t]+}}r1
  %result = call {i64, i1} @llvm.sadd.with.overflow.i64(i64 %lhs, i64 %rhs)
  ret {i64, i1} %result
}

define {i32, i1} @ssubo_i32(i32 %lhs, i32 %rhs) {
; CHECK-LABEL: ssubo_i32:
; CHECK: clrf{{[ \t]+}}2
; CHECK-NEXT: sbb.l{{[ \t]+}}r1, r0
; CHECK-NEXT: setvs{{[ \t]+}}r1
; OBJ-LABEL: <ssubo_i32>:
; OBJ: c0 25 82{{.*}}clrf{{[ \t]+}}2
; OBJ-NEXT: {{.*}}c3 68 80{{.*}}sbb.l{{[ \t]+}}r1, r0
; OBJ-NEXT: {{.*}}a1 18{{.*}}setvs{{[ \t]+}}r1
  %result = call {i32, i1} @llvm.ssub.with.overflow.i32(i32 %lhs, i32 %rhs)
  ret {i32, i1} %result
}

define {i64, i1} @ssubo_i64(i64 %lhs, i64 %rhs) {
; CHECK-LABEL: ssubo_i64:
; CHECK: clrf{{[ \t]+}}2
; CHECK-NEXT: sbb.q{{[ \t]+}}r1, r0
; CHECK-NEXT: setvs{{[ \t]+}}r1
; OBJ-LABEL: <ssubo_i64>:
; OBJ: c0 25 82{{.*}}clrf{{[ \t]+}}2
; OBJ-NEXT: {{.*}}c3 78 80{{.*}}sbb.q{{[ \t]+}}r1, r0
; OBJ-NEXT: {{.*}}a1 18{{.*}}setvs{{[ \t]+}}r1
  %result = call {i64, i1} @llvm.ssub.with.overflow.i64(i64 %lhs, i64 %rhs)
  ret {i64, i1} %result
}

define {i32, i1} @uaddo_i32(i32 %lhs, i32 %rhs) {
; CHECK-LABEL: uaddo_i32:
; CHECK: clrf{{[ \t]+}}2
; CHECK-NEXT: adc.l{{[ \t]+}}r1, r0
; CHECK-NEXT: setult{{[ \t]+}}r1
; OBJ-LABEL: <uaddo_i32>:
; OBJ: c0 25 82{{.*}}clrf{{[ \t]+}}2
; OBJ-NEXT: {{.*}}c3 28 80{{.*}}adc.l{{[ \t]+}}r1, r0
; OBJ-NEXT: {{.*}}a1 14{{.*}}setult{{[ \t]+}}r1
  %result = call {i32, i1} @llvm.uadd.with.overflow.i32(i32 %lhs, i32 %rhs)
  ret {i32, i1} %result
}

define {i64, i1} @uaddo_i64(i64 %lhs, i64 %rhs) {
; CHECK-LABEL: uaddo_i64:
; CHECK: clrf{{[ \t]+}}2
; CHECK-NEXT: adc.q{{[ \t]+}}r1, r0
; CHECK-NEXT: setult{{[ \t]+}}r1
; OBJ-LABEL: <uaddo_i64>:
; OBJ: c0 25 82{{.*}}clrf{{[ \t]+}}2
; OBJ-NEXT: {{.*}}c3 38 80{{.*}}adc.q{{[ \t]+}}r1, r0
; OBJ-NEXT: {{.*}}a1 14{{.*}}setult{{[ \t]+}}r1
  %result = call {i64, i1} @llvm.uadd.with.overflow.i64(i64 %lhs, i64 %rhs)
  ret {i64, i1} %result
}

define {i32, i1} @usubo_i32(i32 %lhs, i32 %rhs) {
; CHECK-LABEL: usubo_i32:
; CHECK: clrf{{[ \t]+}}2
; CHECK-NEXT: sbb.l{{[ \t]+}}r1, r0
; CHECK-NEXT: setult{{[ \t]+}}r1
; OBJ-LABEL: <usubo_i32>:
; OBJ: c0 25 82{{.*}}clrf{{[ \t]+}}2
; OBJ-NEXT: {{.*}}c3 68 80{{.*}}sbb.l{{[ \t]+}}r1, r0
; OBJ-NEXT: {{.*}}a1 14{{.*}}setult{{[ \t]+}}r1
  %result = call {i32, i1} @llvm.usub.with.overflow.i32(i32 %lhs, i32 %rhs)
  ret {i32, i1} %result
}

define {i64, i1} @usubo_i64(i64 %lhs, i64 %rhs) {
; CHECK-LABEL: usubo_i64:
; CHECK: clrf{{[ \t]+}}2
; CHECK-NEXT: sbb.q{{[ \t]+}}r1, r0
; CHECK-NEXT: setult{{[ \t]+}}r1
; OBJ-LABEL: <usubo_i64>:
; OBJ: c0 25 82{{.*}}clrf{{[ \t]+}}2
; OBJ-NEXT: {{.*}}c3 78 80{{.*}}sbb.q{{[ \t]+}}r1, r0
; OBJ-NEXT: {{.*}}a1 14{{.*}}setult{{[ \t]+}}r1
  %result = call {i64, i1} @llvm.usub.with.overflow.i64(i64 %lhs, i64 %rhs)
  ret {i64, i1} %result
}

define {i32, i32} @udivrem_i32(i32 %dividend, i32 %divisor) {
; CHECK-LABEL: udivrem_i32:
; CHECK: divmodu.l{{[ \t]+}}r1, r0, [[REM:r[0-9]+]]
  %quotient = udiv i32 %dividend, %divisor
  %remainder = urem i32 %dividend, %divisor
  %pair0 = insertvalue {i32, i32} poison, i32 %quotient, 0
  %pair1 = insertvalue {i32, i32} %pair0, i32 %remainder, 1
  ret {i32, i32} %pair1
}

define {i64, i64} @sdivrem_i64(i64 %dividend, i64 %divisor) {
; CHECK-LABEL: sdivrem_i64:
; CHECK: divmods.q{{[ \t]+}}r1, r0, [[REM:r[0-9]+]]
  %quotient = sdiv i64 %dividend, %divisor
  %remainder = srem i64 %dividend, %divisor
  %pair0 = insertvalue {i64, i64} poison, i64 %quotient, 0
  %pair1 = insertvalue {i64, i64} %pair0, i64 %remainder, 1
  ret {i64, i64} %pair1
}

define i32 @fshr_i32_13(i32 %high, i32 %low) {
; CHECK-LABEL: fshr_i32_13:
; CHECK: extract.l{{[ \t]+}}13, r0, r1
  %result = call i32 @llvm.fshr.i32(i32 %high, i32 %low, i32 13)
  ret i32 %result
}

define i64 @fshr_i64_37(i64 %high, i64 %low) {
; CHECK-LABEL: fshr_i64_37:
; CHECK: extract.q{{[ \t]+}}37, r0, r1
  %result = call i64 @llvm.fshr.i64(i64 %high, i64 %low, i64 37)
  ret i64 %result
}

define i64 @fshr_i64_variable(i64 %high, i64 %low, i64 %amount) {
; CHECK-LABEL: fshr_i64_variable:
; CHECK-NOT: extract.q
; CHECK: ret
  %result = call i64 @llvm.fshr.i64(i64 %high, i64 %low, i64 %amount)
  ret i64 %result
}

define i32 @fshl_i32_13(i32 %high, i32 %low) {
; CHECK-LABEL: fshl_i32_13:
; CHECK: extract.l{{[ \t]+}}19, r0, r1
  %result = call i32 @llvm.fshl.i32(i32 %high, i32 %low, i32 13)
  ret i32 %result
}

define i64 @fshl_i64_17(i64 %high, i64 %low) {
; CHECK-LABEL: fshl_i64_17:
; CHECK: extract.q{{[ \t]+}}47, r0, r1
  %result = call i64 @llvm.fshl.i64(i64 %high, i64 %low, i64 17)
  ret i64 %result
}

define i64 @integer_bit_operations(i64 %value) {
; CHECK-LABEL: integer_bit_operations:
; CHECK: ret
  %leading = call i64 @llvm.ctlz.i64(i64 %value, i1 false)
  %trailing = call i64 @llvm.cttz.i64(i64 %value, i1 false)
  %population = call i64 @llvm.ctpop.i64(i64 %value)
  %swapped = call i64 @llvm.bswap.i64(i64 %value)
  %sum0 = add i64 %leading, %trailing
  %sum1 = add i64 %population, %swapped
  %sum2 = add i64 %sum0, %sum1
  ret i64 %sum2
}

define i128 @wide_variable_shift(i128 %value, i64 %amount) {
; CHECK-LABEL: wide_variable_shift:
; CHECK: ret
  %wide_amount = zext i64 %amount to i128
  %left = shl i128 %value, %wide_amount
  %right = lshr i128 %value, %wide_amount
  %result = xor i128 %left, %right
  ret i128 %result
}
