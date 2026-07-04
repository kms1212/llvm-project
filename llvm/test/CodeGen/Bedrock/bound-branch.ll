; RUN: llc -mtriple=bedrock -O1 -verify-machineinstrs < %s | FileCheck %s --check-prefix=O1
; RUN: llc -mtriple=bedrock -O0 -verify-machineinstrs < %s | FileCheck %s --check-prefix=O0

define void @u_ii(i64 %x, i64 %lo, i64 %hi, ptr %p) {
; O1-LABEL: u_ii:
; O1:       BNDUII.Q D1, D0, D2
; O1-NEXT:  JVS.W
; O0-LABEL: u_ii:
; O0-NOT:   BNDUII.Q
entry:
  %c0 = icmp uge i64 %x, %lo
  %c1 = icmp ule i64 %x, %hi
  %c = and i1 %c0, %c1
  br i1 %c, label %in, label %out

in:
  store i64 1, ptr %p
  ret void

out:
  store i64 0, ptr %p
  ret void
}

define void @u_ix(i64 %x, i64 %lo, i64 %hi, ptr %p) {
; O1-LABEL: u_ix:
; O1:       BNDUIX.Q D1, D0, D2
entry:
  %c0 = icmp uge i64 %x, %lo
  %c1 = icmp ult i64 %x, %hi
  %c = and i1 %c0, %c1
  br i1 %c, label %in, label %out

in:
  store i64 1, ptr %p
  ret void

out:
  store i64 0, ptr %p
  ret void
}

define void @u_xi(i64 %x, i64 %lo, i64 %hi, ptr %p) {
; O1-LABEL: u_xi:
; O1:       BNDUXI.Q D1, D0, D2
entry:
  %c0 = icmp ugt i64 %x, %lo
  %c1 = icmp ule i64 %x, %hi
  %c = and i1 %c0, %c1
  br i1 %c, label %in, label %out

in:
  store i64 1, ptr %p
  ret void

out:
  store i64 0, ptr %p
  ret void
}

define void @u_xx(i64 %x, i64 %lo, i64 %hi, ptr %p) {
; O1-LABEL: u_xx:
; O1:       BNDUXX.Q D1, D0, D2
entry:
  %c0 = icmp ugt i64 %x, %lo
  %c1 = icmp ult i64 %x, %hi
  %c = and i1 %c0, %c1
  br i1 %c, label %in, label %out

in:
  store i64 1, ptr %p
  ret void

out:
  store i64 0, ptr %p
  ret void
}

define void @s_ii(i64 %x, i64 %lo, i64 %hi, ptr %p) {
; O1-LABEL: s_ii:
; O1:       BNDSII.Q D1, D0, D2
entry:
  %c0 = icmp sge i64 %x, %lo
  %c1 = icmp sle i64 %x, %hi
  %c = and i1 %c0, %c1
  br i1 %c, label %in, label %out

in:
  store i64 1, ptr %p
  ret void

out:
  store i64 0, ptr %p
  ret void
}

define void @out_of_range(i64 %x, i64 %lo, i64 %hi, ptr %p) {
; O1-LABEL: out_of_range:
; O1:       BNDUII.Q D1, D0, D2
entry:
  %c0 = icmp ult i64 %x, %lo
  %c1 = icmp ugt i64 %x, %hi
  %c = or i1 %c0, %c1
  br i1 %c, label %out, label %in

in:
  store i64 1, ptr %p
  ret void

out:
  store i64 0, ptr %p
  ret void
}

define i64 @select_in_range(i64 %x, i64 %lo, i64 %hi, i64 %a, i64 %b) {
; O1-LABEL: select_in_range:
; O1:       BNDUII.Q D1, D0, D2
; O1-NEXT:  JVC.W
; O0-LABEL: select_in_range:
; O0-NOT:   BNDUII.Q
entry:
  %c0 = icmp uge i64 %x, %lo
  %c1 = icmp ule i64 %x, %hi
  %c = and i1 %c0, %c1
  %r = select i1 %c, i64 %a, i64 %b
  ret i64 %r
}

define i64 @select_out_of_range(i64 %x, i64 %lo, i64 %hi, i64 %a, i64 %b) {
; O1-LABEL: select_out_of_range:
; O1:       BNDUII.Q D1, D0, D2
; O1-NEXT:  JVS.W
entry:
  %c0 = icmp ult i64 %x, %lo
  %c1 = icmp ugt i64 %x, %hi
  %c = or i1 %c0, %c1
  %r = select i1 %c, i64 %a, i64 %b
  ret i64 %r
}
