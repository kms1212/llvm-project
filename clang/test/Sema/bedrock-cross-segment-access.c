// RUN: %clang_cc1 -triple bedrock -std=c11 -fsyntax-only -verify %s

__attribute__((cross_segment_access, pure)) int explicit_pure(void);
// expected-error@-1 {{'pure' cannot be combined with cross-segment access}}

__attribute__((const, cross_segment_access)) int explicit_const(void);
// expected-error@-1 {{'const' cannot be combined with cross-segment access}}

__attribute__((pure)) int __far far_pure(void);
// expected-error@-1 {{'pure' cannot be combined with cross-segment access}}

__attribute__((cross_segment_access)) int valid(int *);
