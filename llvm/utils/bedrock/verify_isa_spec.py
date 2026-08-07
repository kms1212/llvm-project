#!/usr/bin/env python3

"""Check the LLVM Bedrock implementation against an isa-design checkout."""

from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

import yaml


class ConformanceError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ConformanceError(message)


def load_yaml(path: Path) -> dict:
    with path.open(encoding="utf-8") as stream:
        value = yaml.safe_load(stream)
    require(isinstance(value, dict), f"{path}: expected a YAML mapping")
    return value


def git_revision(path: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    require(result.returncode == 0, f"{path}: not a Git checkout")
    return result.stdout.strip()


def parse_tablegen_registers(path: Path) -> tuple[dict[str, int], dict[str, int]]:
    text = path.read_text(encoding="utf-8")
    hardware: dict[str, int] = {}
    dwarf: dict[str, int] = {}

    for match in re.finditer(
        r"^def\s+(\w+)\s*:\s*BedrockGPR<(\d+),", text, re.MULTILINE
    ):
        name, encoding = match.groups()
        hardware[name] = int(encoding)
        dwarf[name] = int(encoding)
    for match in re.finditer(
        r"^def\s+(\w+)\s*:\s*BedrockFPR<(\d+),\s*(\d+),",
        text,
        re.MULTILINE,
    ):
        name, encoding, dwarf_number = match.groups()
        hardware[name] = int(encoding)
        dwarf[name] = int(dwarf_number)
    for match in re.finditer(
        r"^def\s+(\w+)\s*:\s*BedrockReg<(\d+),\s*(\d+),",
        text,
        re.MULTILINE,
    ):
        name, encoding, dwarf_number = match.groups()
        hardware[name] = int(encoding)
        dwarf[name] = int(dwarf_number)
    return hardware, dwarf


def parse_spec_dwarf_registers(path: Path) -> dict[str, int]:
    result: dict[str, int] = {}
    row_re = re.compile(
        r"^(\d+)(?:\.\.(\d+))?\s*&\s*(.*?)\s*&\s*assigned(?:;.*?)?\\\\$"
    )
    register_re = re.compile(
        r"\\texttt\{([A-Z]+)(\d*)\}"
        r"(?:\.\.\\texttt\{([A-Z]+)(\d*)\})?"
    )
    for line in path.read_text(encoding="utf-8").splitlines():
        row = row_re.match(line)
        if not row:
            continue
        first_number, last_number, register_cell = row.groups()
        registers = register_re.fullmatch(register_cell)
        require(registers is not None, f"{path}: cannot parse DWARF row {line!r}")
        first_prefix, first_index, last_prefix, last_index = registers.groups()
        number_start = int(first_number)
        number_end = int(last_number or first_number)
        if last_prefix is None:
            require(number_start == number_end, f"{path}: malformed DWARF range")
            result[first_prefix + first_index] = number_start
            continue
        require(first_prefix == last_prefix, f"{path}: mixed register range")
        register_start = int(first_index)
        register_end = int(last_index)
        require(
            number_end - number_start == register_end - register_start,
            f"{path}: mismatched DWARF and register ranges",
        )
        for offset in range(number_end - number_start + 1):
            result[f"{first_prefix}{register_start + offset}"] = number_start + offset
    require(result, f"{path}: no assigned DWARF registers found")
    return result


def check_register_map(llvm_root: Path, isa_root: Path) -> str:
    register_defs = load_yaml(isa_root / "isa/defs/registers.yaml")["registers"]
    expected_hardware = {
        entry["name"]: int(entry["encoding"])
        for group in ("general", "segment")
        for entry in register_defs[group]["entries"]
        if "encoding" in entry
    }
    actual_hardware, actual_dwarf = parse_tablegen_registers(
        llvm_root / "llvm/lib/Target/Bedrock/BedrockRegisterInfo.td"
    )
    for name, encoding in expected_hardware.items():
        require(
            actual_hardware.get(name) == encoding,
            f"register {name}: specification encoding {encoding}, "
            f"LLVM encoding {actual_hardware.get(name)}",
        )

    expected_dwarf = parse_spec_dwarf_registers(
        isa_root / "isa/abi/bedrock-c-abi.tex"
    )
    require(
        actual_dwarf == expected_dwarf,
        "LLVM and specification DWARF register maps differ",
    )
    return (
        f"register map: {len(expected_hardware)} architectural encodings, "
        f"{len(expected_dwarf)} DWARF assignments"
    )


def clang_builtin_names(path: Path) -> set[str]:
    text = path.read_text(encoding="utf-8")
    direct = set(
        re.findall(
            r"^def\s+(\w+)\s*:\s*BedrockBuiltin<", text, re.MULTILINE
        )
    )
    unary = set(
        re.findall(
            r"^defm\s+(\w+)\s*:\s*BedrockApproxUnaryBuiltins;",
            text,
            re.MULTILINE,
        )
    )
    return direct | {f"{name}_{suffix}" for name in unary for suffix in ("f32", "f64")}


def check_builtins(llvm_root: Path, isa_root: Path, sync: dict) -> str:
    builtin_families = load_yaml(isa_root / "isa/c/target_intrinsics.yaml")[
        "builtin_families"
    ]
    specification = {
        builtin["name"]
        for family in builtin_families
        for builtin in family["builtins"]
    }
    extension = sync["fptransa_c_api"]
    suffixes = extension["suffixes"]
    allowlist = {
        f"{mnemonic}_{suffix}"
        for mnemonic in extension["unary_mnemonics"]
        for suffix in suffixes
    } | {
        f"{mnemonic}_{suffix}"
        for mnemonic in extension["paired_mnemonics"]
        for suffix in suffixes
    }
    actual = clang_builtin_names(
        llvm_root / "clang/include/clang/Basic/BuiltinsBedrock.td"
    )
    require(
        actual - allowlist == specification,
        "Clang architectural builtin set differs from target_intrinsics.yaml",
    )
    require(
        actual & allowlist == allowlist,
        "Clang FPTRANSA builtin set differs from the LLVM extension allowlist",
    )
    require(
        not specification & allowlist,
        "architectural and LLVM builtin sets overlap",
    )
    return (
        f"builtins: {len(specification)} architectural, "
        f"{len(allowlist)} LLVM FPTRANSA extensions"
    )


def check_retired_profiles(llvm_root: Path, isa_root: Path) -> str:
    require(
        not (isa_root / "isa/c/bedrockfarintrin.h").exists(),
        "isa-design unexpectedly contains the retired C far-pointer header",
    )
    require(
        not (llvm_root / "clang/lib/Headers/bedrockfarintrin.h").exists(),
        "Clang still installs the retired C far-pointer header",
    )

    forbidden = {
        "clang/include/clang/Basic/TokenKinds.def": ("__far",),
        "clang/include/clang/Basic/BuiltinsBedrock.td": ("far_pointer",),
        "llvm/include/llvm/IR/CallingConv.h": ("Bedrock_Far",),
        "llvm/include/llvm/BinaryFormat/ELFRelocs/Bedrock.def": (
            "R_BEDROCK_FAR_",
        ),
        "lld/ELF/Arch/Bedrock.cpp": ("R_BEDROCK_FAR_", ".got.far"),
    }
    for relative, tokens in forbidden.items():
        path = llvm_root / relative
        text = path.read_text(encoding="utf-8")
        for token in tokens:
            require(token not in text, f"{relative}: retired token {token!r} remains")
    return "retired profiles: C far pointers and far ELF ABI absent"


def load_spec_forms(isa_root: Path) -> tuple[dict[str, str], dict[str, dict]]:
    by_bits: dict[str, str] = {}
    by_reference: dict[str, dict] = {}
    for path in sorted((isa_root / "isa/defs").rglob("encodings.yaml")):
        relative = path.relative_to(isa_root).as_posix()
        for form in load_yaml(path).get("forms", []):
            reference = f"{relative}#{form['id']}"
            bits = str(form["bits"])
            require(bits not in by_bits, f"duplicate specification bits: {bits}")
            require(
                reference not in by_reference,
                f"duplicate specification form: {reference}",
            )
            by_bits[bits] = reference
            by_reference[reference] = form
    return by_bits, by_reference


def source_patterns(path: Path) -> set[str]:
    text = path.read_text(encoding="utf-8")
    candidates = re.findall(r'''["']([01][01A-Za-z]{7,})["']''', text)
    return {
        candidate
        for candidate in candidates
        if re.fullmatch(r"[01a-z]+", candidate)
    }


def inventory_digest(references: set[str]) -> str:
    payload = "".join(f"{reference}\n" for reference in sorted(references))
    return hashlib.sha256(payload.encode()).hexdigest()


def check_inventory(name: str, references: set[str], expected: dict) -> None:
    require(
        len(references) == expected["count"],
        f"{name} form count: expected {expected['count']}, found {len(references)}",
    )
    digest = inventory_digest(references)
    require(
        digest == expected["sha256"],
        f"{name} form inventory changed: expected {expected['sha256']}, found {digest}",
    )


def substitute_form_bits(pattern: str, values: dict[str, int]) -> str:
    result = list(pattern)
    for field in sorted(set(pattern) - {"0", "1"}):
        require(field in values, f"missing value for encoding field {field}")
        positions = [index for index, value in enumerate(result) if value == field]
        value = int(values[field])
        require(0 <= value < 1 << len(positions), f"field {field} does not fit")
        encoded = f"{value:0{len(positions)}b}"
        for position, bit in zip(positions, encoded):
            result[position] = bit
    require(
        not (set(values) - (set(pattern) - {"0", "1"})),
        "fixed encoding probe contains an unknown field",
    )
    return "".join(result)


def encode_fixed_form(form: dict, fields: dict[str, int]) -> list[int]:
    bits = substitute_form_bits(str(form["bits"]), fields)
    payload = int(bits, 2)
    if form["class"] == "extrashort":
        require(len(bits) == 7, "extra-short form must contain 7 payload bits")
        return [payload]
    require(form["class"] == "short", "fixed probe is not short or extra-short")
    require(len(bits) == 14, "short form must contain 14 payload bits")
    return [0x80 | (payload >> 8), payload & 0xFF]


def golden_encoding(test: str, assembly: str) -> list[int]:
    block = re.search(
        rf"(?m)^{re.escape(assembly)}\n"
        r"; CHECK-INST:.*\n"
        r"; CHECK-ENCODING: encoding: \[([^]]+)\]$",
        test,
    )
    require(block is not None, f"missing MC golden for {assembly!r}")
    return [int(byte.strip(), 0) for byte in block.group(1).split(",")]


def check_mc_forms(llvm_root: Path, isa_root: Path, sync: dict) -> str:
    forms_by_bits, forms_by_reference = load_spec_forms(isa_root)
    decoder_patterns = source_patterns(
        llvm_root / "llvm/lib/Target/Bedrock/MCTargetDesc/BedrockMCEncoding.cpp"
    )
    assembler_patterns = source_patterns(
        llvm_root / "llvm/lib/Target/Bedrock/AsmParser/BedrockAsmParser.cpp"
    )
    unknown = (decoder_patterns | assembler_patterns) - forms_by_bits.keys()
    require(
        not unknown,
        f"MC contains patterns absent from the specification: {sorted(unknown)}",
    )

    round_trip = {forms_by_bits[bits] for bits in decoder_patterns & assembler_patterns}
    decoder_only = {
        forms_by_bits[bits] for bits in decoder_patterns - assembler_patterns
    }
    assembler_only = {
        forms_by_bits[bits] for bits in assembler_patterns - decoder_patterns
    }
    expected = sync["mc_form_inventory"]
    check_inventory("round-trip", round_trip, expected["round_trip"])
    check_inventory("decoder-only", decoder_only, expected["decoder_only"])
    check_inventory("assembler-only", assembler_only, expected["assembler_only"])

    test = (llvm_root / "llvm/test/MC/Bedrock/revised-encodings.s").read_text(
        encoding="utf-8"
    )
    for probe in sync["fixed_encoding_probes"]:
        reference = probe["form"]
        require(reference in forms_by_reference, f"unknown fixed form {reference}")
        expected_bytes = encode_fixed_form(
            forms_by_reference[reference], probe["fields"]
        )
        actual_bytes = golden_encoding(test, probe["assembly"])
        require(
            actual_bytes == expected_bytes,
            f"{probe['assembly']}: specification bytes {expected_bytes}, "
            f"MC golden {actual_bytes}",
        )
    return (
        f"MC forms: {len(round_trip)} round-trip, {len(decoder_only)} decoder-only, "
        f"{len(sync['fixed_encoding_probes'])} fixed-width probes"
    )


def extract_integer(text: str, pattern: str, description: str) -> int:
    match = re.search(pattern, text)
    require(match is not None, f"cannot find {description} in LLD source")
    return int(match.group(1), 0)


def check_plt(llvm_root: Path, isa_root: Path) -> str:
    golden = load_yaml(isa_root / "isa/abi/plt_conformance_vectors.yaml")[
        "ordinary_plt"
    ]
    target = (llvm_root / "lld/ELF/Arch/Bedrock.cpp").read_text(encoding="utf-8")
    driver = (llvm_root / "lld/ELF/Driver.cpp").read_text(encoding="utf-8")
    synthetic = (llvm_root / "lld/ELF/SyntheticSections.cpp").read_text(
        encoding="utf-8"
    )

    require(
        extract_integer(target, r"pltHeaderSize\s*=\s*(\d+)", "PLT header size")
        == 0,
        "Bedrock must not emit PLT0",
    )
    entry_size = extract_integer(target, r"pltEntrySize\s*=\s*(\d+)", "PLT entry size")
    require(
        entry_size == golden["entry_size"],
        "LLD and specification PLT entry sizes differ",
    )
    alignment = extract_integer(
        synthetic,
        r'PltSection::PltSection\(Ctx &ctx\)[\s\S]*?SyntheticSection\(ctx, "\.plt"'
        r"[\s\S]*?\n\s*(\d+)\)",
        "PLT alignment",
    )
    require(
        alignment == golden["alignment"],
        "LLD and specification PLT alignments differ",
    )

    opcode = [
        extract_integer(
            target,
            rf"buf\[{index}\]\s*=\s*(0x[0-9a-fA-F]+)",
            f"PLT opcode byte {index}",
        )
        for index in range(4)
    ]
    require(
        opcode == golden["instruction"]["opcode_bytes"],
        "LLD and specification PLT opcodes differ",
    )
    require(
        re.search(r"memset\(buf,\s*0x01,\s*pltEntrySize\)", target) is not None,
        "Bedrock PLT padding is not initialized with one-byte NOPs",
    )
    require(
        golden["padding"]["offset"] == golden["instruction"]["total_bytes"]
        and golden["padding"]["length"] == entry_size - golden["padding"]["offset"]
        and golden["padding"]["byte"] == 1,
        "specification PLT padding vector is inconsistent",
    )
    displacement_offset = golden["instruction"]["displacement"]["offset"]
    require(
        re.search(
            rf"write64le\(buf\s*\+\s*{displacement_offset},\s*"
            r"sym\.getGotPltVA\(ctx\)\s*-\s*pltEntryAddr\)",
            target,
        )
        is not None,
        "Bedrock PLT displacement does not implement GOT(S) - entry",
    )
    require(
        "StringRef(arg->getValue()) == \"lazy\"" in driver
        and "ctx.arg.zNow = true;" in driver,
        "Bedrock does not enforce eager binding",
    )
    require(
        "dtFlags |= DF_BIND_NOW;" in synthetic and "dtFlags1 |= DF_1_NOW;" in synthetic,
        "LLD does not publish eager-binding dynamic flags",
    )
    require(
        extract_integer(
            target,
            r"gotPltHeaderEntriesNum\s*=\s*(\d+)",
            "GOTPLT header entry count",
        )
        == 3
        and "write64le(buf + 8, 0);" in target
        and "write64le(buf + 16, 0);" in target
        and re.search(
            r"void Bedrock::writeGotPlt\(.*?\{\s*.*?write64le\(buf, 0\);",
            target,
            re.DOTALL,
        ),
        "Bedrock GOTPLT reserved or unresolved entries are not zero-initialized",
    )
    return (
        f"PLT golden: {entry_size}-byte eager entries, "
        f"{len(golden['relocation_vectors'])} relocation vectors"
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--isa-root", required=True, type=Path)
    parser.add_argument(
        "--llvm-root",
        type=Path,
        default=Path(__file__).resolve().parents[3],
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    llvm_root = arguments.llvm_root.resolve()
    isa_root = arguments.isa_root.resolve()
    sync = load_yaml(Path(__file__).with_name("isa_sync.yaml"))

    try:
        revision = git_revision(isa_root)
        require(
            revision == sync["isa_revision"],
            f"isa-design revision {revision} does not match {sync['isa_revision']}",
        )
        checks = (
            check_register_map,
            check_builtins,
            check_retired_profiles,
            check_mc_forms,
            check_plt,
        )
        for check in checks:
            if check in (check_builtins, check_mc_forms):
                result = check(llvm_root, isa_root, sync)
            else:
                result = check(llvm_root, isa_root)
            print(f"PASS: {result}")
    except (ConformanceError, KeyError, TypeError, yaml.YAMLError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(f"PASS: isa-design revision {revision}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
