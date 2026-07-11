; RUN: llvm-as < %s | llvm-dis | FileCheck %s
; RUN: opt -module-summary < %s | llvm-dis | FileCheck %s --check-prefix=SUMMARY

; CHECK: define void @explicit() #0
define void @explicit() cross_segment_access {
  ret void
}

; CHECK: attributes #0 = { cross_segment_access }
; SUMMARY: funcFlags: ({{.*}}crossSegmentAccess: 1)
