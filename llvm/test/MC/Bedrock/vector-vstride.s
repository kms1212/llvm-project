# RUN: llvm-mc -triple=bedrock -mattr=+vector -filetype=obj %s -o %t
# RUN: llvm-objdump -d --no-show-raw-insn %t | FileCheck %s

# CHECK: vmov.q p0, [r1 + r2 * lane], v3
vmov.q p0, [r1 + r2 * lane], v3
# CHECK-NEXT: vmov.q p0, v3, [r1 + r2 * lane - 1]
vmov.q p0, v3, [r1 + r2 * lane - 1]
# CHECK-NEXT: vmov.q p0, [r1 + r2 * lane + 128], v3
vmov.q p0, [r1 + r2 * lane + 128], v3
# CHECK-NEXT: vmov.q p0, v3, [r1 + r2 * lane - 32769]
vmov.q p0, v3, [r1 + r2 * lane - 32769]
# CHECK-NEXT: vmov.q p0, [r1 + r2 * lane + 2147483648], v3
vmov.q p0, [r1 + r2 * lane + 2147483648], v3

# A non-movement form proves that every VEA source uses vector context.
# CHECK-NEXT: vadd.l p1, [r4 + r5 * lane + 7], v6
vadd.l p1, [r4 + r5 * lane + 7], v6
