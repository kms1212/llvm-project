# RUN: not llvm-mc -triple=bedrock -mattr=+vector,+fpu %s 2>&1 | FileCheck %s

# Vector-context selector 0x58 is VSTRIDE and cannot spell scalar [sp].
# CHECK: error: invalid operand for instruction
vmov.q p0, [sp], v1

# Vector-context selectors 0x5b..0x5e are VSTRIDE, not immediates.
# CHECK: error: invalid operand for instruction
vmov.q p0, 7, v1

# The lane marker is unavailable to scalar effective addresses.
# CHECK: error: '* lane' effective address requires a vector instruction
mov.q [r1 + r2 * lane], r3

# A VSTRIDE descriptor has exactly one base and one stride register.
# CHECK: error: expected 'lane' after '*'
vmov.q p0, [r1 + r2 * lanes], v1

# No vector instruction carries normative repeat-body eligibility metadata.
# CHECK: error: instruction is not eligible as a repeat body
rep r0, (vadd.q p0, v1, v2)
# CHECK: error: instruction is not eligible as a repeat body
repeq r0, (vmov.q p0, v1, v2)

# Integer VCMP rejects every condition outside its ten-condition domain.
# CHECK: error: invalid operand for instruction
vcmpt.q p0, v1, v2, p1
# CHECK: error: invalid operand for instruction
vcmpf.q p0, v1, v2, p1
# CHECK: error: invalid operand for instruction
vcmpmi.q p0, v1, v2, p1
# CHECK: error: invalid operand for instruction
vcmppl.q p0, v1, v2, p1
# CHECK: error: invalid operand for instruction
vcmpvs.q p0, v1, v2, p1
# CHECK: error: invalid operand for instruction
vcmpvc.q p0, v1, v2, p1

# FP VCMP rejects every condition outside its eight-condition domain.
# CHECK: error: invalid operand for instruction
vcmpt.d p0, v1, v2, p1
# CHECK: error: invalid operand for instruction
vcmpf.d p0, v1, v2, p1
# CHECK: error: invalid operand for instruction
vcmpult.d p0, v1, v2, p1
# CHECK: error: invalid operand for instruction
vcmpuge.d p0, v1, v2, p1
# CHECK: error: invalid operand for instruction
vcmpmi.d p0, v1, v2, p1
# CHECK: error: invalid operand for instruction
vcmppl.d p0, v1, v2, p1
# CHECK: error: invalid operand for instruction
vcmpule.d p0, v1, v2, p1
# CHECK: error: invalid operand for instruction
vcmpugt.d p0, v1, v2, p1
