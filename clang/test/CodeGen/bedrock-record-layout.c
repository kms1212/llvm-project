// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -std=c11 -fdump-record-layouts-complete -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=BEDROCK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c11 -fdump-record-layouts-complete -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=DEFAULT

typedef unsigned int UInt;

// Typedef expansion preserves the canonical base type, so these fields share
// the same 32-bit unit.
// BEDROCK-LABEL: 0 | struct SameBase
// BEDROCK: 0:0-2 |   unsigned int first
// BEDROCK-NEXT: 0:3-7 |   UInt second
// BEDROCK: [sizeof=4, align=4]
struct SameBase {
  unsigned int first : 3;
  UInt second : 5;
};

// A base-type change closes the current unit even when the next field would
// fit. The target hook is disabled by default, preserving the generic layout.
// BEDROCK-LABEL: 0 | struct ChangedBase
// BEDROCK: 0:0-2 |   unsigned int first
// BEDROCK-NEXT: 4:0-2 |   unsigned short second
// BEDROCK-NEXT: 8:0-2 |   unsigned int third
// BEDROCK: [sizeof=12, align=4]
// DEFAULT-LABEL: 0 | struct ChangedBase
// DEFAULT: 0:0-2 |   unsigned int first
// DEFAULT-NEXT: 0:3-5 |   unsigned short second
// DEFAULT-NEXT: 0:6-8 |   unsigned int third
// DEFAULT: [sizeof=4, align=4]
struct ChangedBase {
  unsigned int first : 3;
  unsigned short second : 3;
  unsigned int third : 3;
};

// A zero-width field closes the byte unit and advances to the next unsigned
// int boundary without adding storage of its own.
// BEDROCK-LABEL: 0 | struct ZeroWidth
// BEDROCK: 0:0-2 |   unsigned char first
// BEDROCK-NEXT: 4:- |   unsigned int
// BEDROCK-NEXT: 4:0-1 |   unsigned char second
// BEDROCK: [sizeof=5, align=1]
struct ZeroWidth {
  unsigned char first : 3;
  unsigned int : 0;
  unsigned char second : 2;
};

// Flexible-array alignment contributes to the record even though no element
// storage contributes to sizeof.
// BEDROCK-LABEL: 0 | struct Flexible
// BEDROCK: 0 |   char tag
// BEDROCK-NEXT: 8 |   long[] values
// BEDROCK: [sizeof=8, align=8]
struct Flexible {
  char tag;
  long values[];
};
