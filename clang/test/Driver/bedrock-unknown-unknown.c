// RUN: %clang --target=bedrock-unknown-unknown -### %s -c 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ECHO
// RUN: %clang --target=bedrock-unknown-unknown -### %s -c -fno-integrated-as 2>&1 \
// RUN:   | FileCheck %s --check-prefix=EXT-AS
// RUN: %clang --target=bedrock-unknown-unknown -dM -E -x c /dev/null \
// RUN:   | FileCheck %s --check-prefix=MACROS
// RUN: %clang --target=bedrock-unknown-unknown %s -emit-llvm -S -o - \
// RUN:   | FileCheck %s
// RUN: %clang --target=bedrock-unknown-unknown -### %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LINK
// RUN: rm -rf %t.dir && mkdir -p %t.dir/lib/generic
// RUN: touch %t.dir/lib/generic/libclang_rt.builtins-bedrock.a
// RUN: %clang --target=bedrock-unknown-unknown -resource-dir %t.dir -### %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=GENERIC-RT

// ECHO: "-cc1" "-triple" "bedrock-unknown-unknown"
// ECHO: "-mframe-pointer=none"
// ECHO-NOT: "-faddrsig"
// ECHO-NOT: "bedrock-as"

// EXT-AS: "bedrock-as"

// MACROS-DAG: #define __BEDROCK__ 1
// MACROS-DAG: #define __bedrock__ 1

// LINK-NOT: "/usr/bin/gcc"
// LINK: "{{.*}}ld.lld"
// LINK-SAME: "-m" "elf64bedrock"
// LINK: {{.*}}libclang_rt.builtins{{(-bedrock)?}}.a
// LINK-NOT: "/usr/bin/gcc"

// GENERIC-RT: {{.*}}/lib/generic/libclang_rt.builtins-bedrock.a

typedef __builtin_va_list va_list;
typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;

// CHECK: @align_c = dso_local global i32 1
int align_c = __alignof(char);

// CHECK: @align_s = dso_local global i32 2
int align_s = __alignof(short);

// CHECK: @align_i = dso_local global i32 4
int align_i = __alignof(int);

// CHECK: @align_l = dso_local global i32 8
int align_l = __alignof(long);

// CHECK: @align_ll = dso_local global i32 8
int align_ll = __alignof(long long);

// CHECK: @align_p = dso_local global i32 8
int align_p = __alignof(void *);

// CHECK: @align_vl = dso_local global i32 8
int align_vl = __alignof(va_list);

// CHECK: signext i8 @check_char()
char check_char(void) { return 0; }

// CHECK: signext i16 @check_short()
short check_short(void) { return 0; }

// CHECK: i32 @check_int()
int check_int(void) { return 0; }

// CHECK: i64 @check_long()
long check_long(void) { return 0; }

// CHECK: i64 @check_longlong()
long long check_longlong(void) { return 0; }

// CHECK: zeroext i8 @check_uchar()
unsigned char check_uchar(void) { return 0; }

// CHECK: zeroext i16 @check_ushort()
unsigned short check_ushort(void) { return 0; }

// CHECK: i32 @check_uint()
unsigned int check_uint(void) { return 0; }

// CHECK: i64 @check_ulong()
unsigned long check_ulong(void) { return 0; }

// CHECK: i64 @check_ulonglong()
unsigned long long check_ulonglong(void) { return 0; }

// CHECK: i64 @check_size_t()
size_t check_size_t(void) { return 0; }

// CHECK: i64 @check_ptrdiff_t()
ptrdiff_t check_ptrdiff_t(void) { return 0; }
