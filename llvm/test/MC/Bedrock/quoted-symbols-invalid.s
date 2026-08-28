; REQUIRES: bedrock-registered-target
; RUN: not llvm-mc -triple=bedrock %s 2>&1 | FileCheck %s

call `bad\qescape`
; CHECK: error: invalid escape in quoted identifier

call `unterminated
; CHECK: error: unterminated quoted identifier
