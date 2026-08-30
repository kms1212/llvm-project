# RUN: split-file %s %t
# RUN: llvm-mc -triple=bedrock -mattr=+vectorfp -show-encoding %t/valid.s | FileCheck %s --check-prefix=VALID
# RUN: not llvm-mc -triple=bedrock -mattr=-vector,+fpu %t/integer.s 2>&1 | FileCheck %s --check-prefix=NOVECTOR
# RUN: not llvm-mc -triple=bedrock -mattr=+vector,+fpu,-vectorfp %t/fp.s 2>&1 | FileCheck %s --check-prefix=NOVECTORFP

# VALID: ptrue.w{{[ \t]+}}p0{{[ \t]+}}; encoding: [0xc7,0xe9,0xc4,0x00]
# VALID-NEXT: vmov.q{{[ \t]+}}p1, v2, v3{{[ \t]+}}; encoding: [0xcb,0xf7,0xad,0x84,0x43]
# VALID-NEXT: ploop.l{{[ \t]+}}r1, r2, p3, [r4]{{[ \t]+}}; encoding: [0xcf,0xfc,0x0a,0xa0,0x91,0x84]
# VALID-NEXT: vfadd.h{{[ \t]+}}p0, v1, v2

# NOVECTOR: error: instruction requires the +vector feature
# NOVECTORFP: error: instruction requires the +vectorfp feature

#--- valid.s
ptrue.w p0
vmov.q p1, v2, v3
ploop.l r1, r2, p3, [r4]
vfadd.h p0, v1, v2

#--- integer.s
vadd.q p0, v1, v2

#--- fp.s
vfadd.h p0, v1, v2
