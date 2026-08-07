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
#ifndef TEST_CALL
void exported_nonbaseline_aggregates(struct AttributePacked attribute,
                                     struct PragmaPacked pragma,
                                     struct FieldPacked field) {}
#endif

// Packed aggregates remain usable within one translation unit when they do
// not cross an external ABI boundary.
static struct AttributePacked
internal_packed(struct AttributePacked value) {
  return value;
}

#ifdef TEST_CALL
extern void consume_attribute_packed(struct AttributePacked);

// CALL: error: Bedrock C ABI does not permit packed or under-aligned aggregate type 'struct AttributePacked' across an external ABI boundary
void call_external_boundary(void) {
  struct AttributePacked value = {0};
  consume_attribute_packed(value);
}
#endif

struct ValidBoundaries {
  unsigned char first : 3;
  unsigned int : 0;
  long values[];
};

// Zero-width bit-fields and correctly aligned flexible arrays are baseline
// aggregate features and must not trigger the packed/unaligned diagnostic.
void exported_valid_boundaries(struct ValidBoundaries *value) {
  struct AttributePacked local = {0};
  (void)value;
  (void)internal_packed(local);
}
