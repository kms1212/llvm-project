# RUN: split-file %s %t
# RUN: llvm-mc -triple=bedrock -mattr=+vector,+fpu -show-encoding %t/valid.s | FileCheck %s --check-prefix=VALID
# RUN: not llvm-mc -triple=bedrock -mattr=-vector,+fpu %t/integer.s 2>&1 | FileCheck %s --check-prefix=NOVECTOR
# RUN: not llvm-mc -triple=bedrock -mattr=+vector,-fpu %t/fp.s 2>&1 | FileCheck %s --check-prefix=NOFP

# VALID: ptrue.w{{[ \t]+}}p0{{[ \t]+}}; encoding: [0xc7,0xe9,0xc4,0x00]
# VALID-NEXT: vmov.q{{[ \t]+}}p1, v2, v3{{[ \t]+}}; encoding: [0xcb,0xf7,0xad,0x84,0x43]
# VALID-NEXT: ploop.l{{[ \t]+}}r1, r2, p3, [r4]{{[ \t]+}}; encoding: [0xcf,0xfc,0x0a,0xa0,0x91,0x84]

# NOVECTOR: error: instruction requires the +vector feature
# NOFP: error: floating-point vector instruction requires the +fpu feature

#--- valid.s
ptrue.h p0
vmov.d p1, v2, v3
ploop.s r1, r2, p3, [r4]

#--- integer.s
vadd.q p0, v1, v2

#--- fp.s
vadd.h p0, v1, v2
