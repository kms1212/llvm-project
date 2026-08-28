# RUN: llvm-mc -triple=bedrock -mattr=+vector,+fpu -filetype=obj %s -o %t
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
# CHECK-NEXT: vcmpeq.d
vcmpeq.d p0, v1, v2, p1
# CHECK-NEXT: vcmpne.d
vcmpne.d p0, v1, v2, p1
# CHECK-NEXT: vcmplt.d
vcmplt.d p0, v1, v2, p1
# CHECK-NEXT: vcmple.d
vcmple.d p0, v1, v2, p1
# CHECK-NEXT: vcmpge.d
vcmpge.d p0, v1, v2, p1
# CHECK-NEXT: vcmpgt.d
vcmpgt.d p0, v1, v2, p1
# CHECK-NEXT: vcmpvs.d
vcmpvs.d p0, v1, v2, p1
# CHECK-NEXT: vcmpvc.d
vcmpvc.d p0, v1, v2, p1
