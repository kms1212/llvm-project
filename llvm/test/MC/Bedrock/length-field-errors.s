# RUN: not llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o /dev/null %s 2>&1 | FileCheck %s

LEN 0, NOP
LEN 2, AND.W 4660, D0
LEN 9, NOP

# CHECK: error: LEN word count must be in range 1..8
# CHECK: error: LEN word count must be greater than the actual instruction word count
# CHECK: error: LEN word count must be in range 1..8
