// RUN: not %clang_cc1 -triple bedrock-unknown-unknown -fsyntax-only %s 2>&1 | FileCheck %s

__thread int tls_var;

// CHECK: thread-local storage is not supported for the current target
