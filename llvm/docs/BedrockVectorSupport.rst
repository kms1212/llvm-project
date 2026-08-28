===============================
Bedrock Scalable-Vector Support
===============================

This page records the compiler boundary for the initial Bedrock scalable-vector
extension.  It is an implementation support matrix, not a substitute for the
Bedrock ISA or C ABI specifications.  All entries require the ``+vector``
target feature.  With ``-vector``, scalable-vector types and operations are
rejected rather than scalarized.

Instruction selection
=====================

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - LLVM source
     - Selected instruction family
   * - Scalable integer ``add``, ``sub``, ``mul``, ``and``, ``or``, ``xor``
     - ``VADD``, ``VSUB``, ``VMUL``, ``VAND``, ``VOR``, and ``VXOR``
   * - Scalable floating ``fadd``, ``fsub``, ``fmul``, ``fdiv``
     - ``VADD``, ``VSUB``, ``VMUL``, and ``VDIV``
   * - Supported scalable ``icmp`` and ``fcmp`` conditions
     - Integer or floating ``VCMPcc``; invalid condition/domain pairs are
       rejected
   * - Scalable load and store
     - Contiguous ``VMOVZ`` and ``VMOV``
   * - ``llvm.bedrock.vector.strided.*``
     - VSTRIDE ``VMOVZ`` and ``VMOV``
   * - ``llvm.bedrock.vector.reduce.add``
     - Ordered integer or floating ``VREDADD``

The strided intrinsics take a base pointer, a byte stride, and a signed byte
displacement.  Their assembly uses ``[Rb + Rs * lane]`` with an optional
signed displacement.  The two reduction intrinsics expose the ordered
``i64`` and ``f64`` reductions.  Operations without a generic LLVM IR mapping
use these target intrinsics; the initial extension does not infer them from an
unrelated source-language operation.

The assembler and disassembler cover every normative initial-extension
encoding form, including forms which have no generic IR or C-expression
mapping.  The later-extension operations explicitly excluded by the ISA --
including scatter/gather, vector atomics, vector control flow, non-temporal and
first-fault vector memory, cryptographic operations, approximate vector
transcendentals, reassociation-permitted reduction, and implicit
multi-register destinations -- have neither an intrinsic nor instruction
selection and remain rejected.

C and ABI boundary
==================

``<bedrock_vector.h>`` exposes implementation-defined single-register scalable
vector and predicate types.  Ordinary C arithmetic and comparisons use the
generic-IR rows above.  Target-specific strided and reduction operations are
currently available at the LLVM intrinsic boundary; the header does not claim
a C builtin for an operation that Clang cannot lower.

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Boundary
     - Lowering
   * - First eight vector arguments
     - ``V0`` through ``V7``
   * - First four predicate arguments
     - ``P0`` through ``P3``
   * - Vector or predicate result
     - ``V0`` or ``P0``
   * - Exhausted direct cursor
     - Pointer to a caller-owned maximum-VLEN copy
   * - Variadic or unprototyped value
     - Pointer in the unnamed 16-byte argument slot
   * - Compound scalable argument
     - Pointer to a caller-owned indirect copy
   * - Compound scalable result
     - Caller-provided indirect result pointer
   * - Register pressure or live call
     - Maximum-VLEN spill/reload; ``P15`` preserved

Compound scalable values are not flattened into an unbounded register list.
The caller owns the indirect argument object, and the callee writes an indirect
result object supplied by the caller.  Stack storage reserves the maximum
architectural VLEN so code remains valid for every reset-stable VLEN from 128
through 2048 bits.

Assembly boundary
=================

The vector-context effective-address parser accepts VEA source and destination
operands and every VSTRIDE profile: no displacement and signed 8-, 16-, 32-,
or 64-bit displacement.  In this context selectors ``0x58`` and
``0x5b``--``0x5e`` always denote VSTRIDE forms; they never acquire the scalar
``[sp]`` or immediate meanings.  A malformed descriptor, use of ``* lane`` in
a scalar instruction, an invalid ``VCMPcc`` domain, or ``REP``/``REPcc`` with
a vector body is rejected.
