// RUN: %clang --target=bedrock-unknown-none -### -c %s -mcmodel=low 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LOW
// RUN: %clang --target=bedrock-unknown-none -### -c %s -mcmodel=small 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SMALL
// RUN: %clang --target=bedrock-unknown-none -### -c %s -mcmodel=medium 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MEDIUM
// RUN: %clang --target=bedrock-unknown-none -### -c %s -mcmodel=high 2>&1 \
// RUN:   | FileCheck %s --check-prefix=HIGH
// RUN: %clang --target=bedrock-unknown-none -### -c %s -mcmodel=large 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LARGE
// RUN: not %clang --target=bedrock-unknown-none -### -c %s -mcmodel=kernel \
// RUN:   2>&1 | FileCheck %s --check-prefix=INVALID
// RUN: not %clang --target=bedrock-unknown-none -### -c %s -fPIC \
// RUN:   -mcmodel=low 2>&1 | FileCheck %s --check-prefix=STATIC-ONLY
// RUN: not %clang --target=bedrock-unknown-none -### -c %s -fPIC \
// RUN:   -mcmodel=high 2>&1 | FileCheck %s --check-prefix=STATIC-ONLY

// LOW: "-mcmodel=tiny"
// SMALL: "-mcmodel=small"
// MEDIUM: "-mcmodel=medium"
// HIGH: "-mcmodel=kernel"
// LARGE: "-mcmodel=large"
// INVALID: error: unsupported argument 'kernel' to option '-mcmodel='
// STATIC-ONLY: error: invalid argument '{{.*}}' only allowed with '-fno-pic'
