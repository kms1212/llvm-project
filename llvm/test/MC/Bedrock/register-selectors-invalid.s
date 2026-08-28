; REQUIRES: bedrock-registered-target
; RUN: not llvm-mc -triple=bedrock %s 2>&1 | FileCheck %s

rdpmc ptcr, r2
; CHECK: :[[#@LINE-1]]:1: error: invalid instruction mnemonic

rdcr cycle, r2
; CHECK: :[[#@LINE-1]]:1: error: invalid instruction mnemonic
