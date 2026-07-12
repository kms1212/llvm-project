// RUN: %clang --target=bedrock-unknown-none -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OMIT
// RUN: %clang --target=bedrock-unknown-none -O0 -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OMIT
// RUN: %clang --target=bedrock-unknown-none -Oz -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OMIT
// RUN: %clang --target=bedrock-unknown-none -fomit-frame-pointer -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OMIT
// RUN: %clang --target=bedrock-unknown-none -fno-omit-frame-pointer -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=KEEP

// OMIT: "-mframe-pointer=none"
// KEEP: "-mframe-pointer=all"
