// RUN: %clang -target bedrock-unknown-unknown -c -o %t.o %s
// RUN: llvm-readobj -h -x .data %t.o | FileCheck %s --check-prefix=OBJ

char a = 1;
long b = 0x1122334455667788L;
char c = 2;

// OBJ: Machine: EM_BEDROCK
// OBJ: Hex dump of section '.data':
// OBJ-NEXT: 0x00000000 01000000 00000000 88776655 44332211
// OBJ-NEXT: 0x00000010 02
