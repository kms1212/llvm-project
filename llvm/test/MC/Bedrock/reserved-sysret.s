; REQUIRES: bedrock-registered-target
; RUN: not llvm-mc -triple=bedrock -filetype=obj %s -o /dev/null 2>&1 | FileCheck %s

sysret
; CHECK: error: invalid instruction mnemonic
; CHECK-NEXT: sysret
