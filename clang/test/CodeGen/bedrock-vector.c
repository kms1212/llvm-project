// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -std=c17 -Wno-deprecated-non-prototype \
// RUN:   -target-feature +vector -ffreestanding -O1 \
// RUN:   -internal-isystem %S/../../lib/Headers -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple bedrock -std=c17 -Wno-deprecated-non-prototype \
// RUN:   -target-feature +vector -ffreestanding -O1 \
// RUN:   -internal-isystem %S/../../lib/Headers -S -o - %s \
// RUN:   | FileCheck %s --check-prefix=ASM
// RUN: not %clang_cc1 -triple bedrock -std=c17 -target-feature -vector \
// RUN:   -ffreestanding -internal-isystem %S/../../lib/Headers \
// RUN:   -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=NOVECTOR

#include <bedrock_vector.h>
#include <stdarg.h>

// IR-LABEL: define{{.*}} <vscale x 4 x i32> @add_vectors(
// IR: add <vscale x 4 x i32>
// ASM-LABEL: add_vectors:
// ASM: vadd.l
bedrock_vint32_t add_vectors(bedrock_vint32_t lhs,
                             bedrock_vint32_t rhs) {
  return lhs + rhs;
}

// Direct scalable arguments use consecutive vector-register cursors.
// IR-LABEL: define{{.*}} <vscale x 4 x i32> @return_second_vector(
// ASM-LABEL: return_second_vector:
// ASM: vmov v1, v0
bedrock_vint32_t return_second_vector(bedrock_vint32_t first,
                                      bedrock_vint32_t second) {
  return second;
}

extern bedrock_vint32x2_t consume_pair(bedrock_vint32x2_t);

// Compound scalable values use caller-owned indirect copies and indirect
// results.  This also forces two maximum-VLEN local objects through frame
// lowering rather than asking LLVM for a fixed scalable-object byte size.
// IR-LABEL: define{{.*}} void @call_pair(
// IR-SAME: ptr dead_on_unwind noalias writable{{.*}} sret({{.*}}) align 16
// IR-SAME: ptr dead_on_return noundef{{.*}} %{{.*}})
// IR: call void @consume_pair(ptr dead_on_unwind{{.*}} writable sret({{.*}}) align 16
// ASM-LABEL: call_pair:
// ASM: sub.q 1032, sp
// ASM: vlcnt.b
// ASM: call consume_pair
bedrock_vint32x2_t call_pair(bedrock_vint32x2_t value) {
  return consume_pair(value);
}

extern void consume_variadic(long, ...);

// Unnamed scalable values are represented by a pointer to a caller-owned
// copy in the 16-byte unnamed-argument slot sequence.
// IR-LABEL: define{{.*}} void @call_variadic_vector(
// IR: call void (i64, ...) @consume_variadic(i64 noundef 1, <vscale x 4 x i32>
// ASM-LABEL: call_variadic_vector:
// ASM: sub.q 600, sp
// ASM: vmov.l p7, v0, [sp + 24]
// ASM: mov.q {{.*}}, [{{.*}} + 8]
// ASM: call consume_variadic
void call_variadic_vector(bedrock_vint32_t value) {
  consume_variadic(1, value);
}

extern void consume_unprototyped();

// IR-LABEL: define{{.*}} void @call_unprototyped_vector(
// IR: call void (...) @consume_unprototyped(<vscale x 4 x i32>
// ASM-LABEL: call_unprototyped_vector:
// ASM: sub.q 600, sp
// ASM: vmov.l p7, v0, [sp + 24]
// ASM: mov.q {{.*}}, [{{.*}} + 8]
// ASM: call consume_unprototyped
void call_unprototyped_vector(bedrock_vint32_t value) {
  consume_unprototyped(value);
}

// IR-LABEL: define{{.*}} void @read_variadic_vector(
// IR: getelementptr inbounds{{.*}} i8, ptr %{{.*}}, i64 16
// IR: load ptr, ptr %{{.*}}, align 16
// IR: load <vscale x 4 x i32>, ptr %{{.*}}, align 16
void read_variadic_vector(long tag, ...) {
  va_list list;
  va_start(list, tag);
  bedrock_vint32_t value = va_arg(list, bedrock_vint32_t);
  consume_variadic(tag, value);
  va_end(list);
}

// IR: attributes #{{[0-9]+}} = {{.*}}vscale_range(1,16)

// NOVECTOR: error: "<bedrock_vector.h> requires the Bedrock vector extension"
