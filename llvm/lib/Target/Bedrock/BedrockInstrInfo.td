//===-- BedrockInstrInfo.td - Bedrock instruction defs ------*- tablegen
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class BedrockInst<dag outs, dag ins, string asm, list<dag> pattern = []>
    : Instruction {
  let Namespace = "Bedrock";
  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = asm;
  let Pattern = pattern;
  let Size = 2;

  field bits<16> Inst;
}

let isPseudo = 1, isCodeGenOnly = 1,
    hasNoSchedulingInfo =
        1 in def RAW : BedrockInst<(outs), (ins variable_ops), "">;
let isPseudo = 1, isCodeGenOnly = 1,
    hasNoSchedulingInfo =
        1 in def RAW_EXPR : BedrockInst<(outs), (ins variable_ops), "">;

class BedrockShort<bits<14> payload, dag outs, dag ins, string asm,
                   list<dag> pattern = []>
    : BedrockInst<outs, ins, asm, pattern> {
  let Inst{15} = 0;
  let Inst{14} = 0;
  let Inst{13 -0} = payload;
}

class BedrockImmAsmOperand<string name, string predicate> : AsmOperandClass {
  let Name = name;
  let RenderMethod = "addImmOperands";
  let PredicateMethod = predicate;
}

def UImm8AsmOperand : BedrockImmAsmOperand<"UImm8", "isUImm<8>">;
def SImm8AsmOperand : BedrockImmAsmOperand<"SImm8", "isSImm<8>">;

def uimm8 : Operand<i32> { let ParserMatchClass = UImm8AsmOperand; }
def simm8 : Operand<i32> {
  let ParserMatchClass = SImm8AsmOperand;
  let PrintMethod = "printSImm8Operand";
}

class BedrockRR<bits<6> opcode, string asm>
    : BedrockInst<(outs GPR64:$rd), (ins GPR64:$rs),
                  !strconcat(asm, "\t$rs, $rd")> {
  bits<4> rd;
  bits<4> rs;

  let Inst{15} = 0;
  let Inst{14} = 0;
  let Inst{13 -8} = opcode;
  let Inst{7 -4} = rs;
  let Inst{3 -0} = rd;
}

class BedrockRegLow<bits<10> opcode, string asm>
    : BedrockInst<(outs), (ins GPR64:$r), !strconcat(asm, "\t$r")> {
  bits<4> r;

  let Inst{15} = 0;
  let Inst{14} = 0;
  let Inst{13 -4} = opcode;
  let Inst{3 -0} = r;
}

class BedrockRegSP<bits<10> opcode, string asm>
    : BedrockInst<(outs), (ins GPR64:$r), asm> {
  bits<4> r;

  let Inst{15} = 0;
  let Inst{14} = 0;
  let Inst{13 -4} = opcode;
  let Inst{3 -0} = r;
}

class BedrockSet<bits<4> cc, string asm>
    : BedrockInst<(outs), (ins GPR64:$r), !strconcat(asm, "\t$r")> {
  bits<4> r;

  let Inst{15} = 0;
  let Inst{14} = 0;
  let Inst{13 -8} = 0b100001;
  let Inst{7 -4} = r;
  let Inst{3 -0} = cc;
}

class BedrockImm8<Operand immop, bits<6> opcode, string asm>
    : BedrockInst<(outs), (ins immop:$i), asm> {
  bits<8> i;

  let Inst{15} = 0;
  let Inst{14} = 0;
  let Inst{13 -8} = opcode;
  let Inst{7 -0} = i;
}

class BedrockJcc<bits<4> cc, string suffix>
    : BedrockInst<(outs), (ins simm8:$i), !strconcat("j.", suffix, "\t$i")> {
  bits<8> i;

  let Inst{15} = 0;
  let Inst{14} = 0;
  let Inst{13 -12} = 0b11;
  let Inst{11 -8} = cc;
  let Inst{7 -0} = i;
}

class BedrockTrapcc<bits<4> cc, string suffix>
    : BedrockShort<!or(0x2050, cc), (outs), (ins), !strconcat("trap.", suffix)>;

class BedrockSetcc<bits<4> cc, string suffix>
    : BedrockSet<cc, !strconcat("set.", suffix)>;

def NOP : BedrockShort<0x2040, (outs), (ins), "nop">;
def RET : BedrockShort<0x2041, (outs), (ins), "ret">;
def LRET : BedrockShort<0x2042, (outs), (ins), "lret">;
def SYSCALL : BedrockShort<0x2043, (outs), (ins), "syscall">;
def SYSRET : BedrockShort<0x2044, (outs), (ins), "sysret">;
def IRET : BedrockShort<0x2045, (outs), (ins), "iret">;
def BKPT : BedrockShort<0x2046, (outs), (ins), "bkpt">;
def WAIT : BedrockShort<0x2047, (outs), (ins), "wait">;
def YIELD : BedrockShort<0x2048, (outs), (ins), "yield">;
def HALT : BedrockShort<0x2049, (outs), (ins), "halt">;
def ILLEGAL : BedrockShort<0x204a, (outs), (ins), "illegal">;
def RFENCE : BedrockShort<0x204b, (outs), (ins), "rfence">;
def WFENCE : BedrockShort<0x204c, (outs), (ins), "wfence">;
def AFENCE : BedrockShort<0x204d, (outs), (ins), "afence">;
def RESET : BedrockShort<0x204e, (outs), (ins), "reset">;
def TRAP : BedrockShort<0x2050, (outs), (ins), "trap">;

def MOVLrr : BedrockRR<0x00, "mov.l">;
def MOVQrr : BedrockRR<0x01, "mov.q">;
def ADDLrr : BedrockRR<0x02, "add.l">;
def ADDQrr : BedrockRR<0x03, "add.q">;
def SUBLrr : BedrockRR<0x04, "sub.l">;
def SUBQrr : BedrockRR<0x05, "sub.q">;
def CMPLrr : BedrockRR<0x06, "cmp.l">;
def CMPQrr : BedrockRR<0x07, "cmp.q">;
def ANDLrr : BedrockRR<0x08, "and.l">;
def ANDQrr : BedrockRR<0x09, "and.q">;
def ORLrr : BedrockRR<0x0a, "or.l">;
def ORQrr : BedrockRR<0x0b, "or.q">;
def XORLrr : BedrockRR<0x0c, "xor.l">;
def XORQrr : BedrockRR<0x0d, "xor.q">;
def TESTLrr : BedrockRR<0x0e, "test.l">;
def TESTQrr : BedrockRR<0x0f, "test.q">;
def XCHGLrr : BedrockRR<0x10, "xchg.l">;
def XCHGQrr : BedrockRR<0x11, "xchg.q">;
def SHRLrr : BedrockRR<0x12, "shr.l">;
def SHRQrr : BedrockRR<0x13, "shr.q">;
def SHLLrr : BedrockRR<0x14, "shl.l">;
def SHLQrr : BedrockRR<0x15, "shl.q">;
def RORLrr : BedrockRR<0x16, "ror.l">;
def RORQrr : BedrockRR<0x17, "ror.q">;
def ROLLrr : BedrockRR<0x18, "rol.l">;
def ROLQrr : BedrockRR<0x19, "rol.q">;
def SARLrr : BedrockRR<0x1a, "sar.l">;
def SARQrr : BedrockRR<0x1b, "sar.q">;
def EXTZLBrr : BedrockRR<0x1c, "extzl.b">;
def EXTZLWrr : BedrockRR<0x1d, "extzl.w">;
def EXTSLBrr : BedrockRR<0x1e, "extsl.b">;
def EXTSLWrr : BedrockRR<0x1f, "extsl.w">;
def EXTZQLrr : BedrockRR<0x28, "extzq.l">;
def EXTSQLrr : BedrockRR<0x29, "extsq.l">;

def MOVQrs : BedrockRegSP<0b1000000000, "mov.q\t$r, sp">;
def MOVQsr : BedrockRegSP<0b1000000001, "mov.q\tsp, $r">;
def PUSHr : BedrockRegLow<0b1000000010, "push">;
def POPr : BedrockRegLow<0b1000000011, "pop">;

def SETr : BedrockSet<0x0, "set">;
def INCLr : BedrockRegLow<0b1000100000, "inc.l">;
def INCQr : BedrockRegLow<0b1000110000, "inc.q">;
def DECLr : BedrockRegLow<0b1000100001, "dec.l">;
def DECQr : BedrockRegLow<0b1000110001, "dec.q">;
def NEGLr : BedrockRegLow<0b1000100010, "neg.l">;
def NEGQr : BedrockRegLow<0b1000110010, "neg.q">;
def CLRLr : BedrockRegLow<0b1000100011, "clr.l">;
def CLRQr : BedrockRegLow<0b1000110011, "clr.q">;
def ABSLr : BedrockRegLow<0b1000100100, "abs.l">;
def ABSQr : BedrockRegLow<0b1000110100, "abs.q">;
def NOTLr : BedrockRegLow<0b1000100101, "not.l">;
def NOTQr : BedrockRegLow<0b1000110101, "not.q">;
def REVBYTEWr : BedrockRegLow<0b1000101001, "revbyte.w">;
def REVBYTELr : BedrockRegLow<0b1000101010, "revbyte.l">;
def REVBYTEQr : BedrockRegLow<0b1000101011, "revbyte.q">;

def ADDQisp : BedrockImm8<uimm8, 0x2f, "add.q\t$i, sp">;
def JMPi : BedrockImm8<simm8, 0x30, "jmp\t$i">;
def SUBQisp : BedrockImm8<uimm8, 0x31, "sub.q\t$i, sp">;

def TRAP_EQ : BedrockTrapcc<0x2, "eq">;
def TRAP_NE : BedrockTrapcc<0x3, "ne">;
def TRAP_ULT : BedrockTrapcc<0x4, "ult">;
def TRAP_UGE : BedrockTrapcc<0x5, "uge">;
def TRAP_MI : BedrockTrapcc<0x6, "mi">;
def TRAP_PL : BedrockTrapcc<0x7, "pl">;
def TRAP_VS : BedrockTrapcc<0x8, "vs">;
def TRAP_VC : BedrockTrapcc<0x9, "vc">;
def TRAP_ULE : BedrockTrapcc<0xa, "ule">;
def TRAP_UGT : BedrockTrapcc<0xb, "ugt">;
def TRAP_LT : BedrockTrapcc<0xc, "lt">;
def TRAP_GE : BedrockTrapcc<0xd, "ge">;
def TRAP_LE : BedrockTrapcc<0xe, "le">;
def TRAP_GT : BedrockTrapcc<0xf, "gt">;

def SET_EQ : BedrockSetcc<0x2, "eq">;
def SET_NE : BedrockSetcc<0x3, "ne">;
def SET_ULT : BedrockSetcc<0x4, "ult">;
def SET_UGE : BedrockSetcc<0x5, "uge">;
def SET_MI : BedrockSetcc<0x6, "mi">;
def SET_PL : BedrockSetcc<0x7, "pl">;
def SET_VS : BedrockSetcc<0x8, "vs">;
def SET_VC : BedrockSetcc<0x9, "vc">;
def SET_ULE : BedrockSetcc<0xa, "ule">;
def SET_UGT : BedrockSetcc<0xb, "ugt">;
def SET_LT : BedrockSetcc<0xc, "lt">;
def SET_GE : BedrockSetcc<0xd, "ge">;
def SET_LE : BedrockSetcc<0xe, "le">;
def SET_GT : BedrockSetcc<0xf, "gt">;

def J_EQ : BedrockJcc<0x2, "eq">;
def J_NE : BedrockJcc<0x3, "ne">;
def J_ULT : BedrockJcc<0x4, "ult">;
def J_UGE : BedrockJcc<0x5, "uge">;
def J_MI : BedrockJcc<0x6, "mi">;
def J_PL : BedrockJcc<0x7, "pl">;
def J_VS : BedrockJcc<0x8, "vs">;
def J_VC : BedrockJcc<0x9, "vc">;
def J_ULE : BedrockJcc<0xa, "ule">;
def J_UGT : BedrockJcc<0xb, "ugt">;
def J_LT : BedrockJcc<0xc, "lt">;
def J_GE : BedrockJcc<0xd, "ge">;
def J_LE : BedrockJcc<0xe, "le">;
def J_GT : BedrockJcc<0xf, "gt">;
