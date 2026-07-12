// RUN: %clang_cc1 -triple bedrock -std=c11 -Wno-address-of-packed-member -verify %s

typedef _Atomic(unsigned long) atomic_u64;

_Atomic(__int128) wide; // expected-error {{bedrock atomic object type '__int128' is wider than 64 bits}}

struct ThreeBytes { unsigned char bytes[3]; };
_Atomic(struct ThreeBytes) odd_size; // expected-error {{bedrock atomic object type 'struct ThreeBytes' has unsupported size 3 bytes}}

struct __attribute__((packed)) Packed {
  char prefix;
  atomic_u64 value;
};

unsigned long packed_load(struct Packed *p) {
  return __c11_atomic_load(&p->value, __ATOMIC_ACQUIRE); // expected-error {{bedrock atomic access requires 8-byte alignment; expression is only 1-byte aligned}}
}

unsigned long cast_load(char *p) {
  return __atomic_load_n((unsigned long *)(p + 1), __ATOMIC_ACQUIRE); // expected-error {{bedrock atomic access requires 8-byte alignment; expression is only 1-byte aligned}}
}

__int128 wide_builtin(__int128 *p) {
  return __atomic_load_n(p, __ATOMIC_RELAXED); // expected-error {{bedrock atomic object type '__int128' is wider than 64 bits}}
}

unsigned long integer_address(unsigned long address) {
  return __atomic_load_n((unsigned long *)address, __ATOMIC_RELAXED);
}

_Static_assert(__atomic_always_lock_free(1, 0), "byte atomics are native");
_Static_assert(__atomic_always_lock_free(2, 0), "word atomics are native");
_Static_assert(__atomic_always_lock_free(4, 0), "long atomics are native");
_Static_assert(__atomic_always_lock_free(8, 0), "quad atomics are native");
_Static_assert(!__atomic_always_lock_free(16, 0), "wide atomics are rejected");
