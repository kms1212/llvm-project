// REQUIRES: bedrock-registered-target
//
// RUN: %clang --target=bedrock-unknown-none -fuse-ld=lld -nostdlib \
// RUN:   -Wl,-e,c_entry -### %s 2>&1 | FileCheck %s
//
// CHECK: "-cc1" "-triple" "bedrock-unknown-none"
// CHECK: "{{.*}}ld.lld"
// CHECK-SAME: "-Bstatic"
// CHECK-SAME: "-m" "elf64bedrock"
// CHECK-SAME: "-e" "c_entry"
// CHECK-NOT: "{{.*}}gcc"

void c_entry(void) {}
