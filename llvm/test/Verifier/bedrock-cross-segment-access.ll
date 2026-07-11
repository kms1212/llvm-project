; RUN: not llvm-as < %s 2>&1 | FileCheck %s

; CHECK: Attribute 'cross_segment_access' requires unrestricted memory effects!
define void @invalid() cross_segment_access memory(read) {
  ret void
}
