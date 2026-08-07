// REQUIRES: bedrock-registered-target
// RUN: %clang --target=bedrock-unknown-none -O0 -g -c %s -o %t.o
// RUN: llvm-dwarfdump --debug-frame %t.o | FileCheck %s --check-prefix=CFI
// RUN: llvm-readobj --sections %t.o | FileCheck %s --check-prefix=SECTIONS
// RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=ASM

int near_debug_frame(int x) {
  volatile int value = x;
  return value;
}

// CFI: Return address column: 17
// CFI: DW_CFA_def_cfa: SP +8
// CFI-NEXT: DW_CFA_offset: PC -8
// CFI: DW_CFA_def_cfa_offset: +24
// CFI: DW_CFA_def_cfa_offset: +8
// SECTIONS: Name: .debug_frame
// SECTIONS-NOT: Name: .eh_frame

// ASM-LABEL: <near_debug_frame>:
// ASM-NOT: r15
// ASM: ret
