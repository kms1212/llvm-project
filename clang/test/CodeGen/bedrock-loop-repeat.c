// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -O2 -S -o - %s | FileCheck %s

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;

#define FB_BASE 15728640ULL
#define SCREEN_WIDTH 320U
#define SCREEN_HEIGHT 200U

void clear_volatile(u32 color) {
// CHECK-LABEL: clear_volatile:
// CHECK-NOT: repg
// CHECK: mov.b
// CHECK-NEXT: ijne
// CHECK-NOT: cmp
// CHECK-NOT: jne
// CHECK: ret
  for (u32 y = 0; y < SCREEN_HEIGHT; ++y)
    for (u32 x = 0; x < SCREEN_WIDTH; ++x)
      *(volatile u8 *)(FB_BASE + (u64)y * SCREEN_WIDTH + x) = (u8)color;
}

void clear_nonvolatile(u32 color) {
// CHECK-LABEL: clear_nonvolatile:
// CHECK: lea.q 64000
// CHECK: lea.q 15728640
// CHECK: repg
// CHECK-NEXT: mov.b{{.*}}++
// CHECK-NEXT: }
// CHECK-NOT: ij
// CHECK: ret
  for (u32 y = 0; y < SCREEN_HEIGHT; ++y)
    for (u32 x = 0; x < SCREEN_WIDTH; ++x)
      *(u8 *)(FB_BASE + (u64)y * SCREEN_WIDTH + x) = (u8)color;
}
