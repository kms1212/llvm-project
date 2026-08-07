; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -verify-machineinstrs -filetype=obj < %s \
; RUN:   -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

define i32 @add_imm(i32 %a) {
; CHECK-LABEL: add_imm:
; CHECK: add.l 5, r0
; CHECK: ret
  %r = add i32 %a, 5
  ret i32 %r
}

define i32 @add_lhs_imm(i32 %a) {
; CHECK-LABEL: add_lhs_imm:
; CHECK: add.l 5, r0
; CHECK: ret
  %r = add i32 5, %a
  ret i32 %r
}

define i32 @sub_imm(i32 %a) {
; CHECK-LABEL: sub_imm:
; CHECK: add.l -5, r0
; CHECK: ret
  %r = sub i32 %a, 5
  ret i32 %r
}

define i32 @and_imm(i32 %a) {
; CHECK-LABEL: and_imm:
; CHECK: and.l 999, r0
; CHECK: ret
  %r = and i32 %a, 999
  ret i32 %r
}

define i32 @or_imm(i32 %a) {
; CHECK-LABEL: or_imm:
; CHECK: or.l 5, r0
; CHECK: ret
  %r = or i32 %a, 5
  ret i32 %r
}

define i64 @or_single_bit_i64(i64 %a) {
; CHECK-LABEL: or_single_bit_i64:
; CHECK-NOT: or.q
; CHECK: bset 32, r0
; CHECK: ret
  %r = or i64 %a, 4294967296
  ret i64 %r
}

define i32 @or_single_bit_i32(i32 %a) {
; CHECK-LABEL: or_single_bit_i32:
; CHECK-NOT: or.l
; CHECK: bset 31, r0
; CHECK: ret
; OBJ-LABEL: <or_single_bit_i32>:
; OBJ: c7 ee 6f 80{{.*}}bset{{[ \t]+}}31, r0
  %r = or i32 %a, -2147483648
  ret i32 %r
}

define i64 @and_clear_single_bit_i64(i64 %a) {
; CHECK-LABEL: and_clear_single_bit_i64:
; CHECK-NOT: and.q
; CHECK: bclr 40, r0
; CHECK: ret
; OBJ-LABEL: <and_clear_single_bit_i64>:
; OBJ: c7 ee b4 00{{.*}}bclr{{[ \t]+}}40, r0
  %r = and i64 %a, -1099511627777
  ret i64 %r
}

define i32 @and_clear_single_bit_i32(i32 %a) {
; CHECK-LABEL: and_clear_single_bit_i32:
; CHECK-NOT: and.l
; CHECK: bclr 31, r0
; CHECK: ret
; OBJ-LABEL: <and_clear_single_bit_i32>:
; OBJ: c7 ee af 80{{.*}}bclr{{[ \t]+}}31, r0
  %r = and i32 %a, 2147483647
  ret i32 %r
}

define i64 @xor_single_bit_i64(i64 %a) {
; CHECK-LABEL: xor_single_bit_i64:
; CHECK-NOT: xor.q
; CHECK: bchg 40, r0
; CHECK: ret
; OBJ-LABEL: <xor_single_bit_i64>:
; OBJ: c7 ee f4 00{{.*}}bchg{{[ \t]+}}40, r0
  %r = xor i64 %a, 1099511627776
  ret i64 %r
}

define i32 @xor_single_bit_i32(i32 %a) {
; CHECK-LABEL: xor_single_bit_i32:
; CHECK-NOT: xor.l
; CHECK: bchg 31, r0
; CHECK: ret
; OBJ-LABEL: <xor_single_bit_i32>:
; OBJ: c7 ee ef 80{{.*}}bchg{{[ \t]+}}31, r0
  %r = xor i32 %a, -2147483648
  ret i32 %r
}

define i1 @test_single_bit_i64(i64 %a) {
; CHECK-LABEL: test_single_bit_i64:
; CHECK-NOT: and.q
; CHECK: btest 40, r0
; CHECK-NEXT: seteq r0
; CHECK: ret
; OBJ-LABEL: <test_single_bit_i64>:
; OBJ: c7 ee 34 00{{.*}}btest{{[ \t]+}}40, r0
; OBJ-NEXT: {{.*}}seteq{{[ \t]+}}r0
  %masked = and i64 %a, 1099511627776
  %result = icmp eq i64 %masked, 0
  ret i1 %result
}

define i1 @test_single_bit_i32(i32 %a) {
; CHECK-LABEL: test_single_bit_i32:
; CHECK-NOT: and.l
; CHECK: btest 31, r0
; CHECK-NEXT: setne r0
; CHECK: ret
; OBJ-LABEL: <test_single_bit_i32>:
; OBJ: c7 ee 2f 80{{.*}}btest{{[ \t]+}}31, r0
; OBJ-NEXT: {{.*}}setne{{[ \t]+}}r0
  %masked = and i32 %a, -2147483648
  %result = icmp ne i32 %masked, 0
  ret i1 %result
}

define i64 @or_two_large_bits_i64(i64 %a) {
; CHECK-LABEL: or_two_large_bits_i64:
; CHECK-NOT: or.q
; CHECK: bset 34, r0
; CHECK: bset 35, r0
; CHECK: ret
  %r = or i64 %a, 51539607552
  ret i64 %r
}

define i64 @or_two_small_bits_i64(i64 %a) {
; CHECK-LABEL: or_two_small_bits_i64:
; CHECK: or.q 3, r0
; CHECK: ret
  %r = or i64 %a, 3
  ret i64 %r
}

define i32 @xor_imm(i32 %a) {
; CHECK-LABEL: xor_imm:
; CHECK: xor.l 5, r0
; CHECK: ret
  %r = xor i32 %a, 5
  ret i32 %r
}

define i32 @shl_imm(i32 %a) {
; CHECK-LABEL: shl_imm:
; CHECK: shl.l 2, r0
; CHECK: ret
  %r = shl i32 %a, 2
  ret i32 %r
}

define i32 @srl_imm(i32 %a) {
; CHECK-LABEL: srl_imm:
; CHECK: shr.l 3, r0
; CHECK: ret
  %r = lshr i32 %a, 3
  ret i32 %r
}

define i32 @sra_imm(i32 %a) {
; CHECK-LABEL: sra_imm:
; CHECK: sar.l 4, r0
; CHECK: ret
  %r = ashr i32 %a, 4
  ret i32 %r
}
