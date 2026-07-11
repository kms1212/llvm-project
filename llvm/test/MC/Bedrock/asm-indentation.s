# REQUIRES: bedrock-registered-target
# RUN: llvm-mc -triple=bedrock < %s | FileCheck %s --strict-whitespace

lea.l 3, r2
# CHECK: {{^	lea\.l	3, r2$}}
