// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -emit-llvm -o - %s | FileCheck %s

// CHECK: target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n64-S128"
// CHECK: target triple = "bedrock"
// CHECK: @indirect_function = ifunc void (), ptr @resolve_indirect_function

struct Pair {
  long a;
  long b;
};

struct Big {
  long a;
  long b;
  long c;
};

typedef union __attribute__((transparent_union)) TransparentUnion {
  long integer;
  void *pointer;
} TransparentUnion;

typedef unsigned __int128 uint128_t;

// CHECK-LABEL: define{{.*}} i32 @narrow(
// CHECK-SAME: i8 noundef signext %a,
// CHECK-SAME: i16 noundef zeroext %b,
// CHECK-SAME: i32 noundef signext %c)
int narrow(signed char a, unsigned short b, int c) {
  return a + b + c;
}

// CHECK-LABEL: define{{.*}} [2 x i64] @ret_pair(
struct Pair ret_pair(long a, long b) {
  struct Pair p = {a, b};
  return p;
}

void take_pair(struct Pair);

// CHECK-LABEL: define{{.*}} void @pass_pair(ptr noundef byval(%struct.Pair) align 16 %p)
void pass_pair(struct Pair p) {
  take_pair(p);
}
// CHECK-LABEL: declare void @take_pair(ptr noundef byval(%struct.Pair) align 16)

// CHECK-LABEL: define{{.*}} void @ret_big(ptr dead_on_unwind noalias writable sret(%struct.Big) align 16 %agg.result,
struct Big ret_big(long a, long b, long c) {
  struct Big v = {a, b, c};
  return v;
}

void take_big(struct Big);

// CHECK-LABEL: define{{.*}} void @pass_big(ptr noundef byval(%struct.Big) align 16 %b)
void pass_big(struct Big b) {
  take_big(b);
}
// CHECK-LABEL: declare void @take_big(ptr noundef byval(%struct.Big) align 16)

// A transparent union keeps its source conversion extension, but the Bedrock
// ABI still classifies every union as an INDIRECT argument.
// CHECK-LABEL: define{{.*}} i64 @pass_transparent_union(ptr noundef byval(%union.TransparentUnion) align 16 %value)
long pass_transparent_union(TransparentUnion value) {
  return value.integer;
}

// CHECK-LABEL: define{{.*}} i32 @var_int(ptr noundef %list)
// CHECK: getelementptr inbounds i8, ptr %{{.*}}, i64 16
int var_int(__builtin_va_list list) {
  return __builtin_va_arg(list, int);
}

// CHECK-LABEL: define{{.*}} i64 @mixed_register_classes(
// CHECK-SAME: i64 noundef %count,
// CHECK-SAME: double noundef %scale,
// CHECK-SAME: i128 noundef %wide,
// CHECK-SAME: ptr noundef byval(%struct.Pair) align 16 %pair,
// CHECK-SAME: float noundef %bias)
unsigned long mixed_register_classes(unsigned long count, double scale,
                                     uint128_t wide, struct Pair pair,
                                     float bias) {
  return count + (unsigned long)scale + (unsigned long)wide + pair.a +
         (unsigned long)bias;
}

// CHECK-LABEL: define{{.*}} void @sret_register_reservation(
// CHECK-SAME: ptr dead_on_unwind noalias writable sret(%struct.Big) align 16 %agg.result,
// CHECK-SAME: i64 noundef %tag,
// CHECK-SAME: i128 noundef %wide,
// CHECK-SAME: double noundef %factor)
struct Big sret_register_reservation(unsigned long tag, uint128_t wide,
                                     double factor) {
  struct Big out = {tag, (long)wide, (long)factor};
  return out;
}

// CHECK-LABEL: define{{.*}} i64 @pair_exhaustion_signature(
// CHECK-SAME: i64 noundef %a0, i64 noundef %a1, i64 noundef %a2,
// CHECK-SAME: i64 noundef %a3, i64 noundef %a4, i64 noundef %a5,
// CHECK-SAME: i64 noundef %a6, i128 noundef %wide, i64 noundef %tail)
unsigned long pair_exhaustion_signature(
    unsigned long a0, unsigned long a1, unsigned long a2, unsigned long a3,
    unsigned long a4, unsigned long a5, unsigned long a6, uint128_t wide,
    unsigned long tail) {
  return a0 + (unsigned long)wide + tail;
}

// CHECK-LABEL: define{{.*}} i128 @return_i128(i128 noundef %value)
uint128_t return_i128(uint128_t value) { return value; }

// The ELF ABI includes indirect functions in the baseline profile.
static void indirect_implementation(void) {}
static void *resolve_indirect_function(void) { return indirect_implementation; }
void indirect_function(void)
    __attribute__((ifunc("resolve_indirect_function")));

// Bedrock long double uses the base ABI's IEEE double representation and
// FLOAT register class.
// CHECK-LABEL: define{{.*}} double @return_long_double(double noundef %value)
long double return_long_double(long double value) { return value; }
