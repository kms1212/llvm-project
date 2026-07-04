; RUN: llc -mtriple=bedrock -O0 -verify-machineinstrs < %s | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=bedrock -O1 -verify-machineinstrs < %s | FileCheck %s --check-prefix=OPT
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=OPT

define i64 @branch_smin(i64 %a, i64 %b) {
; O0-LABEL: branch_smin:
; O0:       CMP.Q D1, D0
; O0-NOT:   MINS.Q
; O0:       RET
; OPT-LABEL: branch_smin:
; OPT:       MINS.Q D1, D0
; OPT:       RET
entry:
  %cmp = icmp slt i64 %a, %b
  br i1 %cmp, label %then, label %else

then:
  br label %join

else:
  br label %join

join:
  %phi = phi i64 [ %a, %then ], [ %b, %else ]
  ret i64 %phi
}

define i64 @branch_smax(i64 %a, i64 %b) {
; OPT-LABEL: branch_smax:
; OPT:       MAXS.Q D1, D0
; OPT:       RET
entry:
  %cmp = icmp slt i64 %a, %b
  br i1 %cmp, label %then, label %else

then:
  br label %join

else:
  br label %join

join:
  %phi = phi i64 [ %b, %then ], [ %a, %else ]
  ret i64 %phi
}

define i64 @branch_umin(i64 %a, i64 %b) {
; OPT-LABEL: branch_umin:
; OPT:       MINU.Q D1, D0
; OPT:       RET
entry:
  %cmp = icmp ult i64 %a, %b
  br i1 %cmp, label %then, label %else

then:
  br label %join

else:
  br label %join

join:
  %phi = phi i64 [ %a, %then ], [ %b, %else ]
  ret i64 %phi
}

define i64 @branch_umax(i64 %a, i64 %b) {
; OPT-LABEL: branch_umax:
; OPT:       MAXU.Q D1, D0
; OPT:       RET
entry:
  %cmp = icmp ult i64 %a, %b
  br i1 %cmp, label %then, label %else

then:
  br label %join

else:
  br label %join

join:
  %phi = phi i64 [ %b, %then ], [ %a, %else ]
  ret i64 %phi
}
