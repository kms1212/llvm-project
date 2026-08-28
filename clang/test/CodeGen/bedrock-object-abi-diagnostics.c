// REQUIRES: bedrock-registered-target
// RUN: not %clang_cc1 -triple bedrock -std=c11 -DTEST_PACKED -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=PACKED
// RUN: not %clang_cc1 -triple bedrock -std=c11 -DTEST_EMPTY -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=EMPTY
// RUN: not %clang_cc1 -triple bedrock -std=c11 -DTEST_ZERO_LENGTH -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=ZERO-LENGTH
// RUN: not %clang_cc1 -triple bedrock -std=c11 -DTEST_BIT_FIELD -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=BIT-FIELD
// RUN: not %clang_cc1 -triple bedrock -std=c11 -DTEST_ENUM -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=ENUM
// RUN: not %clang_cc1 -triple bedrock -std=c11 -DTEST_ATOMIC_ENUM -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=ATOMIC-ENUM
// RUN: not %clang_cc1 -triple bedrock -std=c11 -DTEST_BITINT -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=BITINT
// RUN: not %clang_cc1 -triple bedrock -std=c11 -DTEST_VECTOR -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=VECTOR

struct AttributePacked {
  char tag;
  long value;
} __attribute__((packed));

#ifdef TEST_PACKED
// PACKED: error: Bedrock C ABI does not permit packed or under-aligned aggregate type 'struct AttributePacked' across an external ABI boundary
struct AttributePacked external_packed;
#endif

struct Empty {};

#ifdef TEST_EMPTY
// EMPTY: error: Bedrock C ABI does not permit empty or zero-length aggregate type 'struct Empty' across an external ABI boundary
struct Empty external_empty;
#endif

struct ZeroLength {
  unsigned char bytes[0];
};

#ifdef TEST_ZERO_LENGTH
// ZERO-LENGTH: error: Bedrock C ABI does not permit empty or zero-length aggregate type 'struct ZeroLength' across an external ABI boundary
struct ZeroLength external_zero_length;
#endif

struct NarrowBitField {
  unsigned char value : 3;
};

#ifdef TEST_BIT_FIELD
// BIT-FIELD: error: Bedrock C ABI does not permit aggregate type with a non-baseline bit-field base type 'struct NarrowBitField' across an external ABI boundary
struct NarrowBitField external_narrow_bit_field;
#endif

enum WideEnum { WideEnumValue = 0xffffffffu };

#ifdef TEST_ENUM
// ENUM: error: Bedrock C ABI does not permit enum type with a non-int representation 'enum WideEnum' across an external ABI boundary
enum WideEnum external_wide_enum;
#endif

#ifdef TEST_ATOMIC_ENUM
// ATOMIC-ENUM: error: Bedrock C ABI does not permit type containing an enum with a non-int representation '_Atomic(enum WideEnum)' across an external ABI boundary
_Atomic(enum WideEnum) external_atomic_wide_enum;
#endif

typedef unsigned LongVector __attribute__((vector_size(32)));

struct VectorArray {
  LongVector members[2];
};

#ifdef TEST_BITINT
// BITINT: error: Bedrock C ABI does not permit bit-precise integer type without an extension ABI '_BitInt(33)' across an external ABI boundary
_BitInt(33) external_bit_int;
#endif

#ifdef TEST_VECTOR
// VECTOR: error: Bedrock C ABI does not permit fixed-length vector type without an extension ABI 'struct VectorArray' across an external ABI boundary
struct VectorArray external_vector_array;
#endif

// Non-baseline aggregates remain available to internal compiler extensions.
static struct AttributePacked internal_packed;
static struct Empty internal_empty;
static struct ZeroLength internal_zero_length;
static struct NarrowBitField internal_narrow_bit_field;
static enum WideEnum internal_wide_enum;
static _Atomic(enum WideEnum) internal_atomic_wide_enum;
static _BitInt(33) internal_bit_int;
static LongVector internal_vector;

void use_internal_objects(void) {
  (void)internal_packed;
  (void)internal_empty;
  (void)internal_zero_length;
  (void)internal_narrow_bit_field;
  (void)internal_wide_enum;
  (void)internal_atomic_wide_enum;
  (void)internal_bit_int;
  (void)internal_vector;
}
