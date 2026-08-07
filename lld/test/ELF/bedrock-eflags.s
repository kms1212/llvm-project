# REQUIRES: bedrock
# RUN: llvm-mc -triple=bedrock -filetype=obj %s -o %t.flags.o
# RUN: %python -c "with open(r'%t.flags.o', 'r+b') as f: f.seek(48); f.write(b'\x01\x00\x00\x00')"
# RUN: not ld.lld %t.flags.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=FLAGS
# RUN: llvm-mc -triple=bedrock -filetype=obj %s -o %t.osabi.o
# RUN: %python -c "with open(r'%t.osabi.o', 'r+b') as f: f.seek(7); f.write(b'\x03')"
# RUN: not ld.lld %t.osabi.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=OSABI
# RUN: llvm-mc -triple=bedrock -filetype=obj %s -o %t.abiver.o
# RUN: %python -c "with open(r'%t.abiver.o', 'r+b') as f: f.seek(8); f.write(b'\x01')"
# RUN: not ld.lld %t.abiver.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=ABIVER
# RUN: llvm-mc -triple=bedrock -filetype=obj %s -o %t.good.o
# RUN: ld.lld %t.good.o -e 0 -o %t.good
# RUN: llvm-readelf -h -l %t.good | FileCheck %s --check-prefix=OUTPUT

# FLAGS: error: {{.*}}.flags.o: unrecognized e_flags: 1
# OSABI: error: {{.*}}.osabi.o: unrecognized ELF OSABI: 3
# ABIVER: error: {{.*}}.abiver.o: unrecognized ELF ABI version: 1

# OUTPUT: Class:                             ELF64
# OUTPUT: Data:                              2's complement, little endian
# OUTPUT: OS/ABI:                            UNIX - System V
# OUTPUT: ABI Version:                       0
# OUTPUT: Machine:                           Bedrock
# OUTPUT: Flags:                             0x0
# OUTPUT: LOAD {{.*}} R   0x1000
# OUTPUT: LOAD {{.*}} R E 0x1000

.text
.byte 0
