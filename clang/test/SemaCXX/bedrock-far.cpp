// RUN: %clang_cc1 -triple bedrock -std=c++17 -fsyntax-only -verify %s

int * __far object_pointer; // expected-error {{'__far' attribute is not supported in C++}}
int __far function(int); // expected-error {{'__far' attribute is not supported in C++}}
