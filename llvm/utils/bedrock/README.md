# Bedrock ISA synchronization checks

`verify_isa_spec.py` compares the in-tree Bedrock implementation with the
authoritative `isa-design` checkout pinned by `isa_sync.yaml`.

Run it from an LLVM checkout with:

```console
python3 llvm/utils/bedrock/verify_isa_spec.py \
  --isa-root /path/to/isa-design
```

The check covers the architectural and DWARF register maps, specification
target builtins plus the explicit LLVM FPTRANSA extension allowlist, every
symbolic MC form implemented by the assembler or decoder, selected fixed-width
encoding probes, the absence of the retired C far-pointer and far ELF profiles,
and the eager PLT golden vector. A specification update must update the revision
and inventory in `isa_sync.yaml` in the same change as the corresponding
implementation.
