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
# RUN: llvm-mc -triple=bedrock -filetype=obj %s -o %t.identver.o
# RUN: %python -c "with open(r'%t.identver.o', 'r+b') as f: f.seek(6); f.write(b'\x00')"
# RUN: not ld.lld %t.identver.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=IDENTVER
# RUN: llvm-mc -triple=bedrock -filetype=obj %s -o %t.entry.o
# RUN: %python -c "with open(r'%t.entry.o', 'r+b') as f: f.seek(24); f.write(b'\x01\x00\x00\x00\x00\x00\x00\x00')"
# RUN: not ld.lld %t.entry.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=ENTRY
# RUN: llvm-mc -triple=bedrock -filetype=obj %s -o %t.ehsize.o
# RUN: %python -c "with open(r'%t.ehsize.o', 'r+b') as f: f.seek(52); f.write(b'\x00\x00')"
# RUN: not ld.lld %t.ehsize.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=EHSIZE
# RUN: llvm-mc -triple=bedrock -filetype=obj %s -o %t.phentsize.o
# RUN: %python -c "with open(r'%t.phentsize.o', 'r+b') as f: f.seek(54); f.write(b'\x00\x00')"
# RUN: not ld.lld %t.phentsize.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=PHENTSIZE
# RUN: llvm-mc -triple=bedrock -filetype=obj %s -o %t.good.o
# RUN: ld.lld %t.good.o -e 0 -o %t.good
# RUN: llvm-readelf -h -l %t.good | FileCheck %s --check-prefix=OUTPUT

# FLAGS: error: {{.*}}.flags.o: unrecognized e_flags: 1
# OSABI: error: {{.*}}.osabi.o: unrecognized ELF OSABI: 3
# ABIVER: error: {{.*}}.abiver.o: unrecognized ELF ABI version: 1
# IDENTVER: error: {{.*}}.identver.o: unrecognized ELF identification version: 0
# ENTRY: error: {{.*}}.entry.o: ET_REL e_entry must be zero
# EHSIZE: error: {{.*}}.ehsize.o: invalid e_ehsize: 0
# PHENTSIZE: error: {{.*}}.phentsize.o: invalid e_phentsize: 0

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
