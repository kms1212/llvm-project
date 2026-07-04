# RUN: llvm-mc -triple=bedrock-unknown-unknown -filetype=obj -o %t.o %s
# RUN: llvm-objdump -d --no-show-raw-insn %t.o | FileCheck %s

	.text
	.globl generated_fallback
generated_fallback:
	BKPT
	RESET
	RFENCE
	WFENCE
	WAIT
	YIELD
	SYSRET
	IRET
	JMP.L	16
	CALL	0x100000000
	TRACE	0x1234
	RDSTATUS	D0
	WRSTATUS	D1
	RDCR	PTCR, D0
	WRCR	D1, ICR
	CPUID	D0
	PREFETCH	[A0]
	INVTLB
	INVASID	0x1234
	SWPT	D0
	SWPTA	D0, D1
	VTOP	[A0]
	PTATTR	[A0]
	PTQUERY	[A0]
	INVPAGE	[A0]
	INVDCACHE	[A0]
	INVICACHE	[A0]
	FLSHDCACHE	[A0]
	WRBKDCACHE	[A0]
	SYNCCACHE	[A0]
	FPUSHM	{F8-F15}
	FPOPM	{F8-F15}
	FSIN.D	F1, F0
	FCOS.S	F2, F3
	FLOG2.D	F4, F5
	FETOX.S	F6, F7
	BSET.L	D2, [A1]
	MOVSETDD	DB1, DB2, {D0-D3}
	XCHGSETDD	DB1, DB2, {D0-D3}

# CHECK-LABEL: <generated_fallback>:
# CHECK: BKPT
# CHECK: RESET
# CHECK: RFENCE
# CHECK: WFENCE
# CHECK: WAIT
# CHECK: YIELD
# CHECK: SYSRET
# CHECK: IRET
# CHECK: JMP.L{{[[:space:]]+}}0x10
# CHECK: CALL{{[[:space:]]+}}0x100000000
# CHECK: TRACE 0x1234
# CHECK: RDSTATUS.D{{[[:space:]]+}}D0
# CHECK: WRSTATUS.D{{[[:space:]]+}}D1
# CHECK: RDCR.D{{[[:space:]]+}}PTCR, D0
# CHECK: WRCR.D{{[[:space:]]+}}D1, ICR
# CHECK: CPUID.D{{[[:space:]]+}}D0
# CHECK: PREFETCH{{[[:space:]]+}}[A0]
# CHECK: INVTLB
# CHECK: INVASID 0x1234
# CHECK: SWPT.D{{[[:space:]]+}}D0
# CHECK: SWPTA{{[[:space:]]+}}D0, D1
# CHECK: VTOP{{[[:space:]]+}}[A0]
# CHECK: PTATTR{{[[:space:]]+}}[A0]
# CHECK: PTQUERY{{[[:space:]]+}}[A0]
# CHECK: INVPAGE{{[[:space:]]+}}[A0]
# CHECK: INVDCACHE{{[[:space:]]+}}[A0]
# CHECK: INVICACHE{{[[:space:]]+}}[A0]
# CHECK: FLSHDCACHE{{[[:space:]]+}}[A0]
# CHECK: WRBKDCACHE{{[[:space:]]+}}[A0]
# CHECK: SYNCCACHE{{[[:space:]]+}}[A0]
# CHECK: FPUSHM{{[[:space:]]+}}{F8-F15}
# CHECK: FPOPM{{[[:space:]]+}}{F8-F15}
# CHECK: FSIN.D{{[[:space:]]+}}F1, F0
# CHECK: FCOS.S{{[[:space:]]+}}F2, F3
# CHECK: FLOG2.D{{[[:space:]]+}}F4, F5
# CHECK: FETOX.S{{[[:space:]]+}}F6, F7
# CHECK: BSET.L{{[[:space:]]+}}D2, [A1]
# CHECK: MOVSETDD{{[[:space:]]+}}DB1, DB2, {D0-D3}
# CHECK: XCHGSETDD{{[[:space:]]+}}DB1, DB2, {D0-D3}
