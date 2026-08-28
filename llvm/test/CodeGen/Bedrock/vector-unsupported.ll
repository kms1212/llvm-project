; REQUIRES: bedrock-registered-target
; RUN: not --crash llc -mtriple=bedrock -mattr=+vector < %s 2>&1 | FileCheck %s

; Integer vector division is deliberately outside the initial extension.  It
; must not be silently scalarized or selected as a different vector operation.
define <vscale x 4 x i32> @integer_division_is_excluded(
    <vscale x 4 x i32> %lhs, <vscale x 4 x i32> %rhs) {
; CHECK: LLVM ERROR: Cannot select:{{.*}}sdiv
  %result = sdiv <vscale x 4 x i32> %lhs, %rhs
  ret <vscale x 4 x i32> %result
}
