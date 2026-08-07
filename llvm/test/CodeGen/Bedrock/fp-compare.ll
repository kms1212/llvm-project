; REQUIRES: bedrock-registered-target
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -O2 -verify-machineinstrs -filetype=obj < %s \
; RUN:   -o %t.o
; RUN: llvm-objdump -d --triple=bedrock %t.o | FileCheck %s --check-prefix=OBJ

target triple = "bedrock"

define i64 @cmp_oeq(double %a, double %b) {
; CHECK-LABEL: cmp_oeq:
; CHECK: FCMP.D
; CHECK-NEXT: seteq
  %cmp = fcmp oeq double %a, %b
  %result = zext i1 %cmp to i64
  ret i64 %result
}

define i64 @cmp_ogt(double %a, double %b) {
; CHECK-LABEL: cmp_ogt:
; CHECK: FCMP.D
; CHECK-NEXT: setgt
  %cmp = fcmp ogt double %a, %b
  %result = zext i1 %cmp to i64
  ret i64 %result
}

define i64 @cmp_oge(double %a, double %b) {
; CHECK-LABEL: cmp_oge:
; CHECK: FCMP.D
; CHECK-NEXT: setge
  %cmp = fcmp oge double %a, %b
  %result = zext i1 %cmp to i64
  ret i64 %result
}

define i64 @cmp_olt(double %a, double %b) {
; CHECK-LABEL: cmp_olt:
; CHECK: FCMP.D
; CHECK-NEXT: setult
  %cmp = fcmp olt double %a, %b
  %result = zext i1 %cmp to i64
  ret i64 %result
}

define i64 @cmp_ole(double %a, double %b) {
; CHECK-LABEL: cmp_ole:
; CHECK: FCMP.D
; CHECK-NEXT: setule
  %cmp = fcmp ole double %a, %b
  %result = zext i1 %cmp to i64
  ret i64 %result
}

define i64 @cmp_ord(double %a, double %b) {
; CHECK-LABEL: cmp_ord:
; CHECK: FCMP.D
; CHECK-NEXT: setvc
  %cmp = fcmp ord double %a, %b
  %result = zext i1 %cmp to i64
  ret i64 %result
}

define i64 @cmp_uno(double %a, double %b) {
; CHECK-LABEL: cmp_uno:
; CHECK: FCMP.D
; CHECK-NEXT: setvs
  %cmp = fcmp uno double %a, %b
  %result = zext i1 %cmp to i64
  ret i64 %result
}

define i64 @cmp_ugt(double %a, double %b) {
; CHECK-LABEL: cmp_ugt:
; CHECK: FCMP.D
; CHECK-NEXT: setugt
  %cmp = fcmp ugt double %a, %b
  %result = zext i1 %cmp to i64
  ret i64 %result
}

define i64 @cmp_uge(double %a, double %b) {
; CHECK-LABEL: cmp_uge:
; CHECK: FCMP.D
; CHECK-NEXT: setuge
  %cmp = fcmp uge double %a, %b
  %result = zext i1 %cmp to i64
  ret i64 %result
}

define i64 @cmp_ult(double %a, double %b) {
; CHECK-LABEL: cmp_ult:
; CHECK: FCMP.D
; CHECK-NEXT: setlt
  %cmp = fcmp ult double %a, %b
  %result = zext i1 %cmp to i64
  ret i64 %result
}

define i64 @cmp_ule(double %a, double %b) {
; CHECK-LABEL: cmp_ule:
; CHECK: FCMP.D
; CHECK-NEXT: setle
  %cmp = fcmp ule double %a, %b
  %result = zext i1 %cmp to i64
  ret i64 %result
}

define i64 @cmp_une(double %a, double %b) {
; CHECK-LABEL: cmp_une:
; CHECK: FCMP.D
; CHECK-NEXT: setne
  %cmp = fcmp une double %a, %b
  %result = zext i1 %cmp to i64
  ret i64 %result
}

define i64 @cmp_one(double %a, double %b) {
; CHECK-LABEL: cmp_one:
; CHECK: FCMP.D
; CHECK-NOT: FCMP.D
; CHECK: setne
; CHECK: setvc
; CHECK: and.q
  %cmp = fcmp one double %a, %b
  %result = zext i1 %cmp to i64
  ret i64 %result
}

define i64 @cmp_ueq(double %a, double %b) {
; CHECK-LABEL: cmp_ueq:
; CHECK: FCMP.D
; CHECK-NOT: FCMP.D
; CHECK: seteq
; CHECK: setvs
; CHECK: or.q
  %cmp = fcmp ueq double %a, %b
  %result = zext i1 %cmp to i64
  ret i64 %result
}

define i64 @cmp_float(float %a, float %b) {
; CHECK-LABEL: cmp_float:
; CHECK: FCMP.S
; CHECK-NEXT: setult
  %cmp = fcmp olt float %a, %b
  %result = zext i1 %cmp to i64
  ret i64 %result
}

define double @select_olt(double %a, double %b, double %x, double %y) {
; CHECK-LABEL: select_olt:
; CHECK: FCMP.D
; CHECK-NEXT: FMOVult
; OBJ-LABEL: <select_olt>:
; OBJ: c2 28 01{{.*}}fcmp.d
; OBJ-NEXT: c7 d9 21 03{{.*}}fmovult
  %cmp = fcmp olt double %a, %b
  %result = select i1 %cmp, double %x, double %y
  ret double %result
}

define double @select_one(double %a, double %b, double %x, double %y) {
; CHECK-LABEL: select_one:
; CHECK: FCMP.D
; CHECK-NOT: FCMP.D
; CHECK: FMOVeq
; CHECK-NEXT: FMOVvs
; OBJ-LABEL: <select_one>:
; OBJ: c2 28 01{{.*}}fcmp.d
; OBJ-NEXT: c7 d9 11 82{{.*}}fmoveq
; OBJ-NEXT: c7 d9 41 82{{.*}}fmovvs
  %cmp = fcmp one double %a, %b
  %result = select i1 %cmp, double %x, double %y
  ret double %result
}

define double @select_bool(i1 %cond, double %x, double %y) {
; CHECK-LABEL: select_bool:
; CHECK: jne
; CHECK: FMOV.D
  %result = select i1 %cond, double %x, double %y
  ret double %result
}

define i1 @test_positive_zero(float %value) {
; CHECK-LABEL: test_positive_zero:
; CHECK: FTEST.S{{[ \t]+}}f0
; CHECK-NEXT: seteq
; OBJ-LABEL: <test_positive_zero>:
; OBJ: c7 d6 30 00{{.*}}ftest.s{{[ \t]+}}f0
  %result = fcmp oeq float %value, 0.0
  ret i1 %result
}

define i1 @test_negative_zero(double %value) {
; CHECK-LABEL: test_negative_zero:
; CHECK: FTEST.D{{[ \t]+}}f0
; CHECK-NEXT: setult
; OBJ-LABEL: <test_negative_zero>:
; OBJ: c7 d6 b0 00{{.*}}ftest.d{{[ \t]+}}f0
  %result = fcmp olt double %value, -0.0
  ret i1 %result
}

define double @select_float_test_double_result(float %value, double %t,
                                                double %f) {
; CHECK-LABEL: select_float_test_double_result:
; CHECK: FTEST.S{{[ \t]+}}f0
; CHECK-NEXT: FMOVult
; OBJ-LABEL: <select_float_test_double_result>:
; OBJ: c7 d6 30 00{{.*}}ftest.s{{[ \t]+}}f0
; OBJ-NEXT: {{.*}}c7 d9 20 82{{.*}}fmovult
  %condition = fcmp olt float %value, 0.0
  %result = select i1 %condition, double %t, double %f
  ret double %result
}

define float @select_double_test_float_result(double %value, float %t,
                                               float %f) {
; CHECK-LABEL: select_double_test_float_result:
; CHECK: FTEST.D{{[ \t]+}}f0
; CHECK-NEXT: FMOVgt
; OBJ-LABEL: <select_double_test_float_result>:
; OBJ: c7 d6 b0 00{{.*}}ftest.d{{[ \t]+}}f0
; OBJ-NEXT: {{.*}}c7 d9 78 82{{.*}}fmovgt
  %condition = fcmp ogt double %value, -0.0
  %result = select i1 %condition, float %t, float %f
  ret float %result
}
