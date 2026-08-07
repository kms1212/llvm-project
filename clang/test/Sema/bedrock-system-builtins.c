// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -std=c11 -ffreestanding -internal-isystem %S/../../lib/Headers -fsyntax-only -verify %s

#include <bedrocksystemintrin.h>

_Static_assert(__BEDROCK_SEGMENT_IMAGE(0x12345, 33, 65, 2) ==
                   UINT64_C(0x12345083),
               "");
_Static_assert(__BEDROCK_SEGMENT_IMAGE_FOR_BASE(0x12345fff, 2, 3, 0) ==
                   UINT64_C(0x12345106),
               "");
_Static_assert(__BEDROCK_SEGMENT_DISABLED == 0, "");

_Static_assert(__BEDROCK_SEG_DS == 0, "");
_Static_assert(__BEDROCK_SEG_SS == 1, "");
_Static_assert(__BEDROCK_SEG_GS0 == 2, "");
_Static_assert(__BEDROCK_SEG_GS1 == 3, "");
_Static_assert(__BEDROCK_SEG_GS2 == 4, "");
_Static_assert(__BEDROCK_SEG_GS3 == 5, "");
_Static_assert(__BEDROCK_SEG_GS4 == 6, "");
_Static_assert(__BEDROCK_SEG_GS5 == 7, "");

_Static_assert(__BEDROCK_CR_PTCR == 0x0000, "");
_Static_assert(__BEDROCK_CR_ASCR == 0x0001, "");
_Static_assert(__BEDROCK_CR_ECR == 0x0002, "");
_Static_assert(__BEDROCK_CR_SPC == 0x0100, "");
_Static_assert(__BEDROCK_CR_SCS == 0x0101, "");
_Static_assert(__BEDROCK_CR_SDS == 0x0102, "");
_Static_assert(__BEDROCK_CR_URPC == 0x0108, "");
_Static_assert(__BEDROCK_CR_URSP == 0x0109, "");
_Static_assert(__BEDROCK_CR_URCS == 0x010A, "");
_Static_assert(__BEDROCK_CR_URDS == 0x010B, "");
_Static_assert(__BEDROCK_CR_URSS == 0x010C, "");
_Static_assert(__BEDROCK_CR_URCTL == 0x010D, "");
_Static_assert(__BEDROCK_CR_EPC == 0x0110, "");
_Static_assert(__BEDROCK_CR_ECS == 0x0111, "");
_Static_assert(__BEDROCK_CR_EDS == 0x0112, "");
_Static_assert(__BEDROCK_CR_SSS == 0x0200, "");
_Static_assert(__BEDROCK_CR_SSP == 0x0201, "");
_Static_assert(__BEDROCK_CR_ISS == 0x0210, "");
_Static_assert(__BEDROCK_CR_ISP == 0x0211, "");
_Static_assert(__BEDROCK_CR_FSS == 0x0220, "");
_Static_assert(__BEDROCK_CR_FSP == 0x0221, "");
_Static_assert(__BEDROCK_CR_DSS == 0x0230, "");
_Static_assert(__BEDROCK_CR_DSP == 0x0231, "");
_Static_assert(__BEDROCK_CR_BOOTPC == 0x1000, "");
_Static_assert(__BEDROCK_CR_BOOTCFG == 0x1001, "");
_Static_assert(__BEDROCK_CR_PMC == 0x1100, "");

void bad_immediates(unsigned value, uint64_t image) {
  (void)__bedrock_read_control_register(value); // expected-error {{must be a constant integer}}
  (void)__bedrock_read_control_register(65536); // expected-error {{outside the valid range [0, 65535]}}
  __bedrock_write_control_register(value, image); // expected-error {{must be a constant integer}}
  (void)__bedrock_read_segment_register(8); // expected-error {{outside the valid range [0, 7]}}
  __bedrock_write_segment_register(8, image); // expected-error {{outside the valid range [0, 7]}}
  __bedrock_invalidate_asid(value); // expected-error {{must be a constant integer}}
  __bedrock_invalidate_asid(65536); // expected-error {{outside the valid range [0, 65535]}}
  (void)__bedrock_page_table_query(value, 0); // expected-error {{must be a constant integer}}
  (void)__bedrock_page_table_query(8, 0); // expected-error {{outside the valid range [0, 7]}}
}
