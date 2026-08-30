# RUN: llvm-mc -triple=bedrock -mattr=+vectorfp -filetype=obj %s -o %t
# RUN: llvm-objdump -d --no-show-raw-insn %t | FileCheck %s

# Integer positive domain matrix (one size is sufficient because YAML-derived
# generator tests exhaustively cross every integer size with these conditions).
# CHECK: vcmpeq.q
vcmpeq.q p0, v1, v2, p1
# CHECK-NEXT: vcmpne.q
vcmpne.q p0, v1, v2, p1
# CHECK-NEXT: vcmpult.q
vcmpult.q p0, v1, v2, p1
# CHECK-NEXT: vcmpuge.q
vcmpuge.q p0, v1, v2, p1
# CHECK-NEXT: vcmpule.q
vcmpule.q p0, v1, v2, p1
# CHECK-NEXT: vcmpugt.q
vcmpugt.q p0, v1, v2, p1
# CHECK-NEXT: vcmplt.q
vcmplt.q p0, v1, v2, p1
# CHECK-NEXT: vcmpge.q
vcmpge.q p0, v1, v2, p1
# CHECK-NEXT: vcmple.q
vcmple.q p0, v1, v2, p1
# CHECK-NEXT: vcmpgt.q
vcmpgt.q p0, v1, v2, p1

# FP positive domain matrix.
# CHECK-NEXT: vfcmpeq.d
vfcmpeq.d p0, v1, v2, p1
# CHECK-NEXT: vfcmpne.d
vfcmpne.d p0, v1, v2, p1
# CHECK-NEXT: vfcmplt.d
vfcmplt.d p0, v1, v2, p1
# CHECK-NEXT: vfcmple.d
vfcmple.d p0, v1, v2, p1
# CHECK-NEXT: vfcmpge.d
vfcmpge.d p0, v1, v2, p1
# CHECK-NEXT: vfcmpgt.d
vfcmpgt.d p0, v1, v2, p1
# CHECK-NEXT: vfcmpvs.d
vfcmpvs.d p0, v1, v2, p1
# CHECK-NEXT: vfcmpvc.d
vfcmpvc.d p0, v1, v2, p1
