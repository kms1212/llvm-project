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

def SDT_BedrockCallSeqStart : SDCallSeqStart<[SDTCisVT<0, i64>,
                                               SDTCisVT<1, i64>]>;
def SDT_BedrockCallSeqEnd : SDCallSeqEnd<[SDTCisVT<0, i64>,
                                           SDTCisVT<1, i64>]>;
def SDT_BedrockCall : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;
def SDT_BedrockCmp : SDTypeProfile<0, 2, [SDTCisSameAs<0, 1>]>;
def SDT_BedrockBrCC : SDTypeProfile<0, 2, [SDTCisVT<0, OtherVT>,
                                           SDTCisVT<1, i32>]>;
def SDT_BedrockSetCC : SDTypeProfile<1, 1, [SDTCisVT<0, i64>,
                                            SDTCisVT<1, i32>]>;

def BedrockCall : SDNode<"BedrockISD::CALL", SDT_BedrockCall,
                         [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                          SDNPVariadic]>;
def BedrockRetFlag : SDNode<"BedrockISD::RET_FLAG", SDTNone,
                            [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def BedrockCmp : SDNode<"BedrockISD::CMP", SDT_BedrockCmp, [SDNPOutGlue]>;
def BedrockBrCC : SDNode<"BedrockISD::BR_CC", SDT_BedrockBrCC,
                         [SDNPHasChain, SDNPInGlue]>;
def BedrockSetCC : SDNode<"BedrockISD::SET_CC", SDT_BedrockSetCC,
                          [SDNPInGlue]>;
def BedrockCallSeqStart : SDNode<"ISD::CALLSEQ_START",
                                 SDT_BedrockCallSeqStart,
                                 [SDNPHasChain, SDNPOutGlue]>;
def BedrockCallSeqEnd : SDNode<"ISD::CALLSEQ_END", SDT_BedrockCallSeqEnd,
                               [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

let isPseudo = 1, isCodeGenOnly = 1,
    hasNoSchedulingInfo =
        1 in def RAW : BedrockInst<(outs), (ins variable_ops), "">;
let isPseudo = 1, isCodeGenOnly = 1,
    hasNoSchedulingInfo =
        1 in def RAW_EXPR : BedrockInst<(outs), (ins variable_ops), "">;

class BedrockPseudo<dag outs, dag ins, list<dag> pattern = []>
    : BedrockInst<outs, ins, "", pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
  let hasNoSchedulingInfo = 1;
}

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
def brtarget : Operand<OtherVT>;

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

class BedrockRRNoOut<bits<6> opcode, string asm, list<dag> pattern = []>
    : BedrockInst<(outs), (ins GPR64:$rs, GPR64:$rd),
                  !strconcat(asm, "\t$rs, $rd"), pattern> {
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
let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RET : BedrockShort<0x2041, (outs), (ins), "ret", [(BedrockRetFlag)]>;
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
let isCompare = 1, hasSideEffects = 1 in {
def CMPLrr : BedrockRRNoOut<0x06, "cmp.l",
                            [(BedrockCmp i32:$rd, i32:$rs)]>;
def CMPQrr : BedrockRRNoOut<0x07, "cmp.q",
                            [(BedrockCmp i64:$rd, i64:$rs)]>;
}
def ANDLrr : BedrockRR<0x08, "and.l">;
def ANDQrr : BedrockRR<0x09, "and.q">;
def ORLrr : BedrockRR<0x0a, "or.l">;
def ORQrr : BedrockRR<0x0b, "or.q">;
def XORLrr : BedrockRR<0x0c, "xor.l">;
def XORQrr : BedrockRR<0x0d, "xor.q">;
let isCompare = 1, hasSideEffects = 1 in {
def TESTLrr : BedrockRRNoOut<0x0e, "test.l">;
def TESTQrr : BedrockRRNoOut<0x0f, "test.q">;
}
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

def CONST32 : BedrockPseudo<(outs GPR64:$rd), (ins i32imm:$imm)>;
def CONST64 : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$imm)>;

def : Pat<(i32 imm:$imm), (CONST32 imm:$imm)>;
def : Pat<(i64 imm:$imm), (CONST64 imm:$imm)>;
def : Pat<(i64 tglobaladdr:$addr), (CONST64 tglobaladdr:$addr)>;
def : Pat<(i64 texternalsym:$addr), (CONST64 texternalsym:$addr)>;
def : Pat<(i64 tblockaddress:$addr), (CONST64 tblockaddress:$addr)>;
def : Pat<(i64 tconstpool:$addr), (CONST64 tconstpool:$addr)>;
def : Pat<(i64 tjumptable:$addr), (CONST64 tjumptable:$addr)>;

def : Pat<(i64 (sext i32:$src)), (EXTSQLrr GPR64:$src)>;
def : Pat<(i64 (zext i32:$src)), (EXTZQLrr GPR64:$src)>;
def : Pat<(i64 (anyext i32:$src)), (COPY_TO_REGCLASS $src, GPR64)>;
def : Pat<(i32 (trunc i64:$src)), (COPY_TO_REGCLASS $src, GPR64)>;

def FMOVDrr : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$src)>;

let Constraints = "$rd = $lhs" in {
def FADDDrr : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$lhs, FPR64:$rhs),
                            [(set f64:$rd, (fadd f64:$lhs, f64:$rhs))]>;
def FSUBDrr : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$lhs, FPR64:$rhs),
                            [(set f64:$rd, (fsub f64:$lhs, f64:$rhs))]>;
def FMULDrr : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$lhs, FPR64:$rhs),
                            [(set f64:$rd, (fmul f64:$lhs, f64:$rhs))]>;
def FDIVDrr : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$lhs, FPR64:$rhs),
                            [(set f64:$rd, (fdiv f64:$lhs, f64:$rhs))]>;
} // Constraints

def FCVTSQDrr : BedrockPseudo<(outs FPR64:$rd), (ins GPR64:$src),
                              [(set f64:$rd, (sint_to_fp i64:$src))]>;
def FCVTUQDrr : BedrockPseudo<(outs FPR64:$rd), (ins GPR64:$src),
                              [(set f64:$rd, (uint_to_fp i64:$src))]>;

def : Pat<(f64 (sint_to_fp i32:$src)), (FCVTSQDrr (EXTSQLrr GPR64:$src))>;
def : Pat<(f64 (uint_to_fp i32:$src)), (FCVTUQDrr (EXTZQLrr GPR64:$src))>;

let Constraints = "$rd = $lhs" in {
def ADDL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def ADDQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def SUBL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def SUBQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def ANDL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def ANDQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def ORL3rr : BedrockPseudo<(outs GPR64:$rd),
                           (ins GPR64:$lhs, GPR64:$rhs)>;
def ORQ3rr : BedrockPseudo<(outs GPR64:$rd),
                           (ins GPR64:$lhs, GPR64:$rhs)>;
def XORL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def XORQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def SHLL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def SHLQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def SHRL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def SHRQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def SARL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def SARQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def MULL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def MULQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def DIVUL3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs)>;
def DIVUQ3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs)>;
def DIVSL3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs)>;
def DIVSQ3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs)>;
def MODUL3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs)>;
def MODUQ3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs)>;
def MODSL3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs)>;
def MODSQ3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs)>;
} // Constraints

let mayLoad = 1 in {
def LOADB_Zrr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$base)>;
def LOADW_Zrr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$base)>;
def LOADL_Zrr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$base)>;
def LOADB_Srr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$base)>;
def LOADW_Srr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$base)>;
def LOADL_Srr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$base)>;
def LOADLrr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$base)>;
def LOADQrr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$base)>;
def FLOADDrr : BedrockPseudo<(outs FPR64:$rd), (ins GPR64:$base)>;

def LOADB_Zabs : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$addr)>;
def LOADW_Zabs : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$addr)>;
def LOADL_Zabs : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$addr)>;
def LOADB_Sabs : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$addr)>;
def LOADW_Sabs : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$addr)>;
def LOADL_Sabs : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$addr)>;
def LOADLabs : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$addr)>;
def LOADQabs : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$addr)>;
def FLOADDabs : BedrockPseudo<(outs FPR64:$rd), (ins i64imm:$addr)>;

def LOADB_Zfi : BedrockPseudo<(outs GPR64:$rd),
                              (ins i64imm:$base, i64imm:$offset)>;
def LOADW_Zfi : BedrockPseudo<(outs GPR64:$rd),
                              (ins i64imm:$base, i64imm:$offset)>;
def LOADL_Zfi : BedrockPseudo<(outs GPR64:$rd),
                              (ins i64imm:$base, i64imm:$offset)>;
def LOADB_Sfi : BedrockPseudo<(outs GPR64:$rd),
                              (ins i64imm:$base, i64imm:$offset)>;
def LOADW_Sfi : BedrockPseudo<(outs GPR64:$rd),
                              (ins i64imm:$base, i64imm:$offset)>;
def LOADL_Sfi : BedrockPseudo<(outs GPR64:$rd),
                              (ins i64imm:$base, i64imm:$offset)>;
def LOADLfi : BedrockPseudo<(outs GPR64:$rd),
                            (ins i64imm:$base, i64imm:$offset)>;
def LOADQfi : BedrockPseudo<(outs GPR64:$rd),
                            (ins i64imm:$base, i64imm:$offset)>;
def FLOADDfi : BedrockPseudo<(outs FPR64:$rd),
                             (ins i64imm:$base, i64imm:$offset)>;
}

def LEAfi : BedrockPseudo<(outs GPR64:$rd),
                          (ins i64imm:$base, i64imm:$offset)>;

let mayStore = 1 in {
def STOREBrr : BedrockPseudo<(outs), (ins GPR64:$src, GPR64:$base)>;
def STOREWrr : BedrockPseudo<(outs), (ins GPR64:$src, GPR64:$base)>;
def STORELrr : BedrockPseudo<(outs), (ins GPR64:$src, GPR64:$base)>;
def STOREQrr : BedrockPseudo<(outs), (ins GPR64:$src, GPR64:$base)>;
def FSTOREDrr : BedrockPseudo<(outs), (ins FPR64:$src, GPR64:$base)>;

def STOREBabs : BedrockPseudo<(outs), (ins GPR64:$src, i64imm:$addr)>;
def STOREWabs : BedrockPseudo<(outs), (ins GPR64:$src, i64imm:$addr)>;
def STORELabs : BedrockPseudo<(outs), (ins GPR64:$src, i64imm:$addr)>;
def STOREQabs : BedrockPseudo<(outs), (ins GPR64:$src, i64imm:$addr)>;
def FSTOREDabs : BedrockPseudo<(outs), (ins FPR64:$src, i64imm:$addr)>;

def STOREBfi : BedrockPseudo<(outs),
                             (ins GPR64:$src, i64imm:$base, i64imm:$offset)>;
def STOREWfi : BedrockPseudo<(outs),
                             (ins GPR64:$src, i64imm:$base, i64imm:$offset)>;
def STORELfi : BedrockPseudo<(outs),
                             (ins GPR64:$src, i64imm:$base, i64imm:$offset)>;
def STOREQfi : BedrockPseudo<(outs),
                             (ins GPR64:$src, i64imm:$base, i64imm:$offset)>;
def FSTOREDfi : BedrockPseudo<(outs),
                              (ins FPR64:$src, i64imm:$base, i64imm:$offset)>;
}

let hasSideEffects = 1 in {
def ADJSP_DOWN : BedrockPseudo<(outs), (ins i64imm:$amount)>;
def ADJSP_UP : BedrockPseudo<(outs), (ins i64imm:$amount)>;
}

let isBranch = 1, isTerminator = 1 in {
let isBarrier = 1 in
def BR : BedrockPseudo<(outs), (ins brtarget:$dst), [(br bb:$dst)]>;
def BRCC : BedrockPseudo<(outs), (ins brtarget:$dst, i32imm:$cc),
                         [(BedrockBrCC bb:$dst, imm:$cc)]>;
}

def SETCC : BedrockPseudo<(outs GPR64:$rd), (ins i32imm:$cc),
                          [(set i64:$rd, (BedrockSetCC imm:$cc))]>;

let isCall = 1, Defs = [R0, R1, R2, R3, R4, R5, R6, R7,
                        F0, F1, F2, F3, F4, F5, F6, F7] in {
def CALL : BedrockPseudo<(outs), (ins i64imm:$dst)>;
def CALLr : BedrockPseudo<(outs), (ins GPR64:$dst)>;
}

def ADJCALLSTACKDOWN : BedrockPseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                                     [(BedrockCallSeqStart timm:$amt1,
                                                            timm:$amt2)]>;
def ADJCALLSTACKUP : BedrockPseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                                   [(BedrockCallSeqEnd timm:$amt1,
                                                       timm:$amt2)]>;

def : Pat<(BedrockCall (i64 tglobaladdr:$dst)), (CALL tglobaladdr:$dst)>;
def : Pat<(BedrockCall (i64 texternalsym:$dst)), (CALL texternalsym:$dst)>;
def : Pat<(BedrockCall GPR64:$dst), (CALLr GPR64:$dst)>;

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
