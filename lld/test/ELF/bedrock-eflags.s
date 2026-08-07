# REQUIRES: bedrock
# RUN: llvm-mc -triple=bedrock -filetype=obj %s -o %t.o
# RUN: %python -c "with open(r'%t.o', 'r+b') as f: f.seek(48); f.write(b'\x01\x00\x00\x00')"
# RUN: not ld.lld %t.o -o /dev/null 2>&1 | FileCheck %s

# CHECK: error: {{.*}}.o: unrecognized e_flags: 1

.text
.byte 0
