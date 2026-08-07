// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -std=c11 -O1 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple bedrock -std=c11 -O1 -S -o - %s | FileCheck %s --check-prefix=ASM

// IR-LABEL: define{{.*}} { float, float } @return_complex_f32(
// IR-SAME: { float, float } inreg noundef returned %value.coerce)
float _Complex return_complex_f32(float _Complex value) { return value; }

// IR-LABEL: define{{.*}} { double, double } @return_complex_f64(
// IR-SAME: { double, double } inreg noundef returned %value.coerce)
double _Complex return_complex_f64(double _Complex value) { return value; }

// Bedrock long double has the binary64 representation and therefore uses the
// same FLOAT-PAIR carrier as double complex.
// IR-LABEL: define{{.*}} { double, double } @return_complex_long_double(
// IR-SAME: { double, double } inreg noundef returned %value.coerce)
long double _Complex
return_complex_long_double(long double _Complex value) {
  return value;
}

// FLOAT-PAIR does not impose even alignment: lead uses F0, small uses F1:F2,
// and wide uses F3:F4. The returned wide value moves to F0:F1.
// IR-LABEL: define{{.*}} { double, double } @mixed_complex_pairs(
// IR-SAME: float noundef %lead,
// IR-SAME: { float, float } inreg noundef %small.coerce,
// IR-SAME: { double, double } inreg noundef returned %wide.coerce)
// ASM-LABEL: mixed_complex_pairs:
// ASM: FMOV.D f4, f1
// ASM: FMOV.D f3, f0
// ASM: ret
double _Complex mixed_complex_pairs(float lead, float _Complex small,
                                    double _Complex wide) {
  return wide;
}

// With only F7 left, the complete pair uses [SP+16] and exhausts the floating
// class. The following float therefore uses [SP+32].
// ASM-LABEL: complex_pair_exhaustion:
// ASM: FMOV.D [sp + 16], f0
// ASM: ret
double complex_pair_exhaustion(double f0, double f1, double f2, double f3,
                               double f4, double f5, double f6,
                               double _Complex pair, float tail) {
  return __real__ pair;
}

// ASM-LABEL: complex_pair_exhausted_tail:
// ASM: FMOV.S [sp + 32], f0
// ASM: ret
float complex_pair_exhausted_tail(double f0, double f1, double f2, double f3,
                                  double f4, double f5, double f6,
                                  double _Complex pair, float tail) {
  return tail;
}

extern void consume_variadic(int tag, ...);

// An unnamed double complex value occupies one 16-byte slot, not two slots.
// ASM-LABEL: pass_variadic_complex:
// ASM: sub.q 24, sp
// ASM: FMOV.D f1, [{{r[0-9]+}} + 16]
// ASM: FMOV.D f0, [{{r[0-9]+}} + 8]
// ASM: call consume_variadic
// ASM: add.q 24, sp
void pass_variadic_complex(double _Complex value) {
  consume_variadic(0, value);
}
