// REQUIRES: bedrock-registered-target
// RUN: not %clang_cc1 -triple bedrock -std=c11 -ferror-limit 0 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s
// RUN: not %clang_cc1 -triple bedrock -std=c11 -DTEST_CALL -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=CALL

struct AttributePacked {
  char tag;
  long value;
} __attribute__((packed));

// CHECK: error: Bedrock C ABI does not permit packed or under-aligned aggregate type 'struct AttributePacked' across an external ABI boundary

#pragma pack(push, 1)
struct PragmaPacked {
  char tag;
  long value;
};
#pragma pack(pop)

// CHECK: error: Bedrock C ABI does not permit packed or under-aligned aggregate type 'struct PragmaPacked' across an external ABI boundary

struct FieldPacked {
  char tag;
  long value __attribute__((packed));
};

// CHECK: error: Bedrock C ABI does not permit packed or under-aligned aggregate type 'struct FieldPacked' across an external ABI boundary

struct Empty {};

// CHECK: error: Bedrock C ABI does not permit empty or zero-length aggregate type 'struct Empty' across an external ABI boundary

struct ZeroLength {
  unsigned char bytes[0];
};

// CHECK: error: Bedrock C ABI does not permit empty or zero-length aggregate type 'struct ZeroLength' across an external ABI boundary

struct OverAligned {
  long value;
} __attribute__((aligned(32)));

// CHECK: error: Bedrock C ABI does not permit over-aligned aggregate type 'struct OverAligned' across an external ABI boundary

struct NarrowBitField {
  unsigned char value : 3;
};

// CHECK: error: Bedrock C ABI does not permit aggregate type with a non-baseline bit-field base type 'struct NarrowBitField' across an external ABI boundary

enum WideEnum { WideEnumValue = 0xffffffffu };

// CHECK: error: Bedrock C ABI does not permit enum type with a non-int representation 'enum WideEnum' across an external ABI boundary

struct WideEnumMember {
  enum WideEnum value;
};

// CHECK: error: Bedrock C ABI does not permit type containing an enum with a non-int representation 'struct WideEnumMember' across an external ABI boundary
#ifndef TEST_CALL
void exported_nonbaseline_aggregates(struct AttributePacked attribute,
                                     struct PragmaPacked pragma,
                                     struct FieldPacked field,
                                     struct Empty empty,
                                     struct ZeroLength zero_length,
                                     struct OverAligned over_aligned,
                                     struct NarrowBitField narrow_bit_field,
                                     enum WideEnum wide_enum,
                                     struct WideEnumMember wide_member) {}
#endif

// Packed aggregates remain usable within one translation unit when they do
// not cross an external ABI boundary.
static struct AttributePacked
internal_packed(struct AttributePacked value) {
  return value;
}

#ifdef TEST_CALL
extern void consume_attribute_packed(struct AttributePacked);
extern void consume_wide_enum(enum WideEnum);

// CALL: error: Bedrock C ABI does not permit packed or under-aligned aggregate type 'struct AttributePacked' across an external ABI boundary
// CALL: error: Bedrock C ABI does not permit enum type with a non-int representation 'enum WideEnum' across an external ABI boundary
void call_external_boundary(void) {
  struct AttributePacked value = {0};
  consume_attribute_packed(value);
  consume_wide_enum(WideEnumValue);
}
#endif

struct ValidBoundaries {
  unsigned int first : 3;
  unsigned int : 0;
  long values[];
};

// Zero-width bit-fields and correctly aligned flexible arrays are baseline
// aggregate features and must not trigger the packed/unaligned diagnostic.
void exported_valid_boundaries(struct ValidBoundaries *value) {
  struct AttributePacked local = {0};
  enum WideEnum internal_wide = WideEnumValue;
  struct WideEnumMember internal_member = {internal_wide};
  (void)value;
  (void)internal_packed(local);
  (void)internal_member;
}
