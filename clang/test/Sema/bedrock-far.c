// RUN: %clang_cc1 -triple bedrock -std=c11 -fsyntax-only -verify %s

typedef int * __far far_int_ptr;
typedef int (* __far far_fn)(int);
typedef int (* __far far_array_ptr)[4];

int * const __far qualified_pointer;
int (* __far pointer_to_array)[4];
int * __far array_of_far_pointers[3];
int __far native_function(int);

_Static_assert(sizeof(far_int_ptr) == 16, "far pointer size");
_Static_assert(_Alignof(far_int_ptr) == 16, "far pointer alignment");
_Static_assert(sizeof(pointer_to_array) == 16, "declarator binding");
_Static_assert(sizeof(array_of_far_pointers[0]) == 16, "array binding");
_Static_assert(sizeof(far_array_ptr) == 16, "typedef binding");

far_int_ptr preserve_type(far_int_ptr p) {
  __typeof__(p) copy = p;
  return _Generic(copy, far_int_ptr: copy);
}

void preserve_pointer_qualifiers(void) {
  int * const __far p = 0;
  p = 0; // expected-error {{cannot assign to variable 'p' with const-qualified type}}
} // expected-note@-2 {{variable 'p' declared const here}}

_Atomic(int) * __far far_pointer_to_atomic;

typedef int (*near_fn)(int);
int near_target(int);
int __far far_target(int);

near_fn bad_implicit_near = far_target; // expected-error {{conversion between ordinary and far function-pointer categories is not allowed}}
far_fn bad_implicit_far = near_target; // expected-error {{conversion between ordinary and far function-pointer categories is not allowed}}
near_fn bad_cast_near(far_fn p) { return (near_fn)p; } // expected-error {{conversion between ordinary and far function-pointer categories is not allowed}}
far_fn bad_cast_far(near_fn p) { return (far_fn)p; } // expected-error {{conversion between ordinary and far function-pointer categories is not allowed}}

long bad_small_int(far_int_ptr p) { return (long)p; } // expected-error {{conversion between a Bedrock far pointer and integer type 'long' loses the far representation}}
far_int_ptr bad_small_rebuild(long v) { return (far_int_ptr)v; } // expected-error {{conversion between a Bedrock far pointer and integer type 'long' loses the far representation}}

_Atomic(far_int_ptr) bad_atomic_far; // expected-error {{a Bedrock far pointer cannot be the type of an atomic object}}
_Atomic(unsigned __int128) bad_atomic_wide; // expected-error {{bedrock atomic object type 'unsigned __int128' is wider than 64 bits}}
int __far bad_object; // expected-error {{'__far' only applies to pointer and function declarators}}

int redeclared_near(int); // expected-note {{previous declaration is here}}
int __far redeclared_near(int); // expected-error {{redeclaration changes a function between ordinary and far calling categories}}

int __far redeclared_far(int); // expected-note {{previous declaration is here}}
int redeclared_far(int); // expected-error {{redeclaration changes a function between ordinary and far calling categories}}
