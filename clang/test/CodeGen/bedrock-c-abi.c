// RUN: %clang_cc1 -triple bedrock-unknown-unknown -emit-llvm -O0 -o - %s \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple bedrock-unknown-unknown -S -O1 \
// RUN:   -mframe-pointer=none -o - %s | FileCheck %s --check-prefix=ASM

struct S8 {
  long a;
};

struct S16 {
  long a;
  long b;
};

struct S24 {
  long a;
  long b;
  long c;
};

struct S8 ret_s8(long a) {
  struct S8 s = {a};
  return s;
}

struct S16 ret_s16(long a, long b) {
  struct S16 s = {a, b};
  return s;
}

struct S24 ret_s24(long a, long b, long c) {
  struct S24 s = {a, b, c};
  return s;
}

long byval_s16(struct S16 s) {
  return s.a + s.b;
}

extern long use_s16(struct S16);

long call_byval(long x, long y) {
  struct S16 s = {x, y};
  return use_s16(s);
}

long many(long a0, long a1, long a2, long a3, long a4, long a5, long a6) {
  return a0 + a1 + a2 + a3 + a4 + a5 + a6;
}

typedef __builtin_va_list va_list;

int first_var(int fixed, ...) {
  va_list ap;
  __builtin_va_start(ap, fixed);
  int v = __builtin_va_arg(ap, int);
  __builtin_va_end(ap);
  return fixed + v;
}

int call_first_var(void) {
  return first_var(7, 35);
}

extern int old_add();

int call_old_add(void) {
  return old_add(7, 35);
}

float addf(float a, float b) {
  return a + b;
}

double addd(double a, double b) {
  return a + b;
}

extern double extd(double, double);

double call_extd(double a, double b) {
  return extd(a, b);
}

long double add_ld(long double a, long double b) {
  return a + b;
}

int sizeof_ld(void) {
  return sizeof(long double);
}

int alignof_ld(void) {
  return _Alignof(long double);
}

long double first_ld(int fixed, ...) {
  va_list ap;
  __builtin_va_start(ap, fixed);
  long double v = __builtin_va_arg(ap, long double);
  __builtin_va_end(ap);
  return v;
}

long double call_first_ld(long double x) {
  return first_ld(1, x);
}

__int128 add_i128(__int128 a, __int128 b) {
  return a + b;
}

extern __int128 ext_i128(__int128, __int128);

__int128 call_i128(__int128 a, __int128 b) {
  return ext_i128(a, b);
}

int alignof_i128(void) {
  return _Alignof(__int128);
}

int biggest_alignment(void) {
  return __BIGGEST_ALIGNMENT__;
}

long fixed_signed_int(int x) {
  return x;
}

long fixed_unsigned_int(unsigned x) {
  return x;
}

long first_schar(int fixed, ...) {
  va_list ap;
  __builtin_va_start(ap, fixed);
  signed char v = __builtin_va_arg(ap, int);
  __builtin_va_end(ap);
  return v;
}

long call_first_schar(void) {
  return first_schar(1, (signed char)-1);
}

// IR-LABEL: define dso_local i64 @ret_s8(i64
// IR-LABEL: define dso_local [2 x i64] @ret_s16(i64
// IR-LABEL: define dso_local void @ret_s24(ptr dead_on_unwind noalias writable sret(%struct.S24) align 16 %agg.result,
// IR-LABEL: define dso_local i64 @byval_s16(ptr noundef byval(%struct.S16) align 16 %s)
// IR: call i64 @use_s16(ptr noundef byval(%struct.S16) align 16 %s)
// IR: call i32 (i32, ...) @first_var(i32 noundef 7, i32 noundef 35)
// IR: call i32 (...) @old_add(i32 noundef 7, i32 noundef 35)
// IR-LABEL: define dso_local [2 x i64] @add_i128([2 x i64] noundef %a.coerce, [2 x i64] noundef %b.coerce)
// IR-LABEL: define dso_local [2 x i64] @call_i128([2 x i64] noundef %a.coerce, [2 x i64] noundef %b.coerce)
// IR: call [2 x i64] @ext_i128([2 x i64] noundef %{{[0-9]+}}, [2 x i64] noundef %{{[0-9]+}})
// IR-LABEL: define dso_local i32 @alignof_i128()
// IR: ret i32 16
// IR-LABEL: define dso_local i32 @biggest_alignment()
// IR: ret i32 16
// IR-LABEL: define dso_local i64 @fixed_signed_int(i32 noundef %x)
// IR-LABEL: define dso_local i64 @fixed_unsigned_int(i32 noundef %x)

// ASM-LABEL: ret_s24:
// ASM: MOV.Q A5, A0
// ASM: MOV.Q D2, [A0 + 16]
// ASM: MOV.Q D1, [A0 + 8]
// ASM: MOV.Q D0, [A0]
// ASM: RET

// ASM-LABEL: byval_s16:
// ASM: MOV.Q [A0], D1
// ASM: MOV.Q [A0 + 8], D0
// ASM: ADD.Q D1, D0
// ASM: RET

// ASM-LABEL: call_byval:
// ASM: SUB.Q 24, SP
// ASM: MOV.Q D1, [SP + 16]
// ASM: MOV.Q D0, [SP + 8]
// ASM: LEA [SP + 8], A0
// ASM: CALL use_s16@PCREL16
// ASM: ADD.Q 24, SP

// ASM-LABEL: many:
// ASM: ADD.Q [SP + 8], D0
// ASM: RET

// ASM-LABEL: first_var:
// ASM: SUB.Q 16, SP
// ASM: ADD.L [SP + 24], D0
// ASM: ADD.Q 16, SP
// ASM: RET

// ASM-LABEL: call_first_var:
// ASM: SUB.Q 24, SP
// ASM: MOV.L 35, D0
// ASM: MOV.L D0, [SP + 0]
// ASM: MOV.L 7, D0
// ASM: CALL first_var@PCREL16
// ASM: ADD.Q 24, SP

// ASM-LABEL: call_old_add:
// ASM: SUB.Q 8, SP
// ASM: MOV.L 7, D0
// ASM: MOV.L 35, D1
// ASM: CALL old_add@PCREL16
// ASM: ADD.Q 8, SP

// ASM-LABEL: addf:
// ASM: FADD.S F1, F0
// ASM: RET

// ASM-LABEL: addd:
// ASM: FADD.D F1, F0
// ASM: RET

// ASM-LABEL: call_extd:
// ASM: SUB.Q 8, SP
// ASM: CALL extd@PCREL16
// ASM: ADD.Q 8, SP

// ASM-LABEL: add_ld:
// ASM: FADD.D F1, F0
// ASM: RET

// ASM-LABEL: sizeof_ld:
// ASM: MOV.L 8, D0
// ASM: RET

// ASM-LABEL: alignof_ld:
// ASM: MOV.L 8, D0
// ASM: RET

// ASM-LABEL: first_ld:
// ASM: FMOV.D [SP + 24], F0
// ASM: RET

// ASM-LABEL: call_first_ld:
// ASM: SUB.Q 24, SP
// ASM: FMOV.D F0, [SP + 0]
// ASM: CALL first_ld@PCREL16
// ASM: ADD.Q 24, SP

// ASM-LABEL: call_i128:
// ASM: SUB.Q 8, SP
// ASM: CALL ext_i128@PCREL16
// ASM: ADD.Q 8, SP

// ASM-LABEL: alignof_i128:
// ASM: MOV.L 16, D0
// ASM: RET

// ASM-LABEL: biggest_alignment:
// ASM: MOV.L 16, D0
// ASM: RET

// ASM-LABEL: fixed_signed_int:
// ASM: EXTSQ.L D0, D0
// ASM-NEXT: RET

// ASM-LABEL: fixed_unsigned_int:
// ASM: EXTZQ.L D0, D0
// ASM-NEXT: RET

// ASM-LABEL: call_first_schar:
// ASM: SUB.Q 24, SP
// ASM: MOV.L -1, D0
// ASM: MOV.L D0, [SP + 0]
// ASM: CALL first_schar@PCREL16
// ASM: ADD.Q 24, SP
