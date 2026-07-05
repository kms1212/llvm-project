# RUN: not llvm-mc -triple=bedrock-unknown-unknown %s 2>&1 | FileCheck %s

	JMP	A0
	JMP	[A0]
	JEQ	16

# CHECK: error: JMP requires an explicit .W or .L size suffix
# CHECK: error: JMP requires an explicit .W or .L size suffix
# CHECK: error: JEQ requires an explicit .W or .L size suffix
