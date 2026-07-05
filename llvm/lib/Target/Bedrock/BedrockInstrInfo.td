//===-- BedrockInstrInfo.td - Bedrock Instruction Definitions -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def SDT_BedrockCallSeqStart : SDCallSeqStart<[SDTCisVT<0, i64>,
                                              SDTCisVT<1, i64>]>;
def SDT_BedrockCallSeqEnd : SDCallSeqEnd<[SDTCisVT<0, i64>,
                                          SDTCisVT<1, i64>]>;
def SDT_BedrockCall : SDTypeProfile<0, -1, [SDTCisPtrTy<0>]>;
def SDT_BedrockCmp : SDTypeProfile<0, 2, [SDTCisSameAs<0, 1>]>;
def SDT_BedrockBrCC : SDTypeProfile<0, 2, [SDTCisVT<0, OtherVT>,
                                            SDTCisVT<1, i8>]>;
def SDT_BedrockSelectCC : SDTypeProfile<1, 3, [SDTCisSameAs<0, 1>,
                                                SDTCisSameAs<1, 2>,
                                                SDTCisVT<3, i8>]>;
def SDT_BedrockWrapper : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                              SDTCisPtrTy<0>]>;
def SDT_BedrockFence : SDTypeProfile<0, 0, []>;
def SDT_BedrockAtomicFetch : SDTypeProfile<1, 3, [SDTCisPtrTy<1>,
                                                  SDTCisSameAs<0, 2>,
                                                  SDTCisVT<3, i8>]>;
def SDT_BedrockAtomicCmpXchg : SDTypeProfile<1, 4, [SDTCisPtrTy<1>,
                                                    SDTCisSameAs<0, 2>,
                                                    SDTCisSameAs<2, 3>,
                                                    SDTCisVT<4, i8>]>;

def BedrockCallSeqStart : SDNode<"ISD::CALLSEQ_START",
                                 SDT_BedrockCallSeqStart,
                                 [SDNPHasChain, SDNPOutGlue]>;
def BedrockCallSeqEnd : SDNode<"ISD::CALLSEQ_END",
                               SDT_BedrockCallSeqEnd,
                               [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def BedrockCall : SDNode<"BedrockISD::CALL", SDT_BedrockCall,
                         [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                          SDNPVariadic]>;
def BedrockCall16 : SDNode<"BedrockISD::CALL16", SDT_BedrockCall,
                           [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                            SDNPVariadic]>;
def BedrockCmp : SDNode<"BedrockISD::CMP", SDT_BedrockCmp, [SDNPOutGlue]>;
def BedrockFCmp : SDNode<"BedrockISD::FCMP", SDT_BedrockCmp, [SDNPOutGlue]>;
def BedrockBrCC : SDNode<"BedrockISD::BR_CC", SDT_BedrockBrCC,
                         [SDNPHasChain, SDNPInGlue]>;
def BedrockSelectCC : SDNode<"BedrockISD::SELECT_CC", SDT_BedrockSelectCC,
                             [SDNPInGlue]>;
def BedrockRet : SDNode<"BedrockISD::RET_GLUE", SDTNone,
                        [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def BedrockWrapper : SDNode<"BedrockISD::Wrapper", SDT_BedrockWrapper>;
def BedrockAFENCE : SDNode<"BedrockISD::AFENCE", SDT_BedrockFence,
                           [SDNPHasChain, SDNPSideEffect]>;
def BedrockFetchAdd : SDNode<"BedrockISD::FETCHADD",
                             SDT_BedrockAtomicFetch,
                             [SDNPHasChain, SDNPMayLoad, SDNPMayStore,
                              SDNPMemOperand]>;
def BedrockFetchSub : SDNode<"BedrockISD::FETCHSUB",
                             SDT_BedrockAtomicFetch,
                             [SDNPHasChain, SDNPMayLoad, SDNPMayStore,
                              SDNPMemOperand]>;
def BedrockFetchAnd : SDNode<"BedrockISD::FETCHAND",
                             SDT_BedrockAtomicFetch,
                             [SDNPHasChain, SDNPMayLoad, SDNPMayStore,
                              SDNPMemOperand]>;
def BedrockFetchOr : SDNode<"BedrockISD::FETCHOR",
                            SDT_BedrockAtomicFetch,
                            [SDNPHasChain, SDNPMayLoad, SDNPMayStore,
                             SDNPMemOperand]>;
def BedrockFetchXor : SDNode<"BedrockISD::FETCHXOR",
                             SDT_BedrockAtomicFetch,
                             [SDNPHasChain, SDNPMayLoad, SDNPMayStore,
                              SDNPMemOperand]>;
def BedrockCmpXchg : SDNode<"BedrockISD::CMPXCHG",
                            SDT_BedrockAtomicCmpXchg,
                            [SDNPHasChain, SDNPMayLoad, SDNPMayStore,
                             SDNPMemOperand]>;

def brtarget : Operand<OtherVT>;
def calltarget : Operand<iPTR>;
def bedrockcc : Operand<i8> {
  let PrintMethod = "printCondCode";
}
def memory_order : Operand<i8> {
  let PrintMethod = "printMemoryOrder";
}
def abssymbol : Operand<iPTR> {
  let PrintMethod = "printAbs64Operand";
}
def shortimm32 : Operand<i32> {
  let PrintMethod = "printShortImmOperand";
}
def shortimm64 : Operand<i64> {
  let PrintMethod = "printShortImmOperand";
}
def u6imm8 : Operand<i8>, ImmLeaf<i8, [{
  return Imm >= 0 && Imm < 64 && Imm != 50 && Imm != 51 && Imm != 52;
}]>;
def u6imm16 : Operand<i16>, ImmLeaf<i16, [{
  return Imm >= 0 && Imm < 64 && Imm != 50 && Imm != 51 && Imm != 52;
}]>;
def u6imm32 : Operand<i32>, ImmLeaf<i32, [{
  return Imm >= 0 && Imm < 64 && Imm != 50 && Imm != 51 && Imm != 52;
}]>;
def u6imm64 : Operand<i64>, ImmLeaf<i64, [{
  return Imm >= 0 && Imm < 64 && Imm != 50 && Imm != 51 && Imm != 52;
}]>;
def u6nzimm8 : ImmLeaf<i8, [{
  return Imm > 0 && Imm < 64 && Imm != 50 && Imm != 51 && Imm != 52;
}]>;
def u6nzimm16 : ImmLeaf<i16, [{
  return Imm > 0 && Imm < 64 && Imm != 50 && Imm != 51 && Imm != 52;
}]>;
def u6nzimm32 : ImmLeaf<i32, [{
  return Imm > 0 && Imm < 64 && Imm != 50 && Imm != 51 && Imm != 52;
}]>;
def u6nzimm64 : ImmLeaf<i64, [{
  return Imm > 0 && Imm < 64 && Imm != 50 && Imm != 51 && Imm != 52;
}]>;
def i16nzimm : ImmLeaf<i16, [{
  return Imm != 0;
}]>;
def i32nzimm : ImmLeaf<i32, [{
  return Imm != 0;
}]>;
def u6sel8 : Operand<i8>, ImmLeaf<i8, [{
  return Imm >= 0 && Imm < 64;
}]>;
def regmask16 : Operand<i16> {
  let PrintMethod = "printRegMask16";
}
def fregmask16 : Operand<i16> {
  let PrintMethod = "printFRegMask16";
}

def memri : Operand<iPTR> {
  let MIOperandInfo = (ops PTR64:$base, i64imm:$offset);
  let PrintMethod = "printMemOperand";
}

def mempost : Operand<iPTR> {
  let MIOperandInfo = (ops PTR64:$base);
  let PrintMethod = "printPostIncMemOperand";
}

def memupd : Operand<iPTR> {
  let MIOperandInfo = (ops PTR64:$base, i64imm:$update);
  let PrintMethod = "printUpdateMemOperand";
}

def lea4addr : Operand<iPTR> {
  let MIOperandInfo = (ops PTR64:$base, D64:$index);
  let PrintMethod = "printLeaScale4Operand";
}

def lea4laddr : Operand<iPTR> {
  let MIOperandInfo = (ops PTR64:$base, D64:$index);
  let PrintMethod = "printLeaScale4LOperand";
}

def idx1addr : Operand<iPTR> {
  let MIOperandInfo = (ops PTR64:$base, D64:$index, i64imm:$offset);
  let PrintMethod = "printIndexScale1Operand";
}

def idx4addr : Operand<iPTR> {
  let MIOperandInfo = (ops PTR64:$base, D64:$index, i64imm:$offset);
  let PrintMethod = "printIndexScale4Operand";
}

def idx4laddr : Operand<iPTR> {
  let MIOperandInfo = (ops PTR64:$base, D64:$index, i64imm:$offset);
  let PrintMethod = "printLongIndexScale4Operand";
}

def idx8laddr : Operand<iPTR> {
  let MIOperandInfo = (ops PTR64:$base, D64:$index, i64imm:$offset);
  let PrintMethod = "printLongIndexScale8Operand";
}

def addr : ComplexPattern<iPTR, 2, "selectAddr", [frameindex], []>;

class BedrockInst<dag Outs, dag Ins, string Asm, list<dag> Pat = []>
    : Instruction {
  let Namespace = "Bedrock";
  let Size = 2;
  bits<16> Inst = 0;
  dag OutOperandList = Outs;
  dag InOperandList = Ins;
  string AsmString = Asm;
  list<dag> Pattern = Pat;
}

class BedrockPseudo<dag Outs, dag Ins, string Asm, list<dag> Pattern = []>
    : BedrockInst<Outs, Ins, Asm, Pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

let hasSideEffects = 1, hasNoSchedulingInfo = 1, isAsmParserOnly = 1,
    Size = 16 in
def ENCODED : BedrockInst<(outs),
                          (ins i64imm:$word_count, i64imm:$w0, i64imm:$w1,
                               i64imm:$w2, i64imm:$w3, i64imm:$w4, i64imm:$w5,
                               i64imm:$w6, i64imm:$w7),
                          "# encoded">;

let Defs = [SP], Uses = [SP], isCodeGenOnly = 1 in {
def ADJCALLSTACKDOWN : BedrockPseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                                     "# ADJCALLSTACKDOWN $amt1 $amt2",
                                     [(BedrockCallSeqStart timm:$amt1,
                                                            timm:$amt2)]>;
def ADJCALLSTACKUP : BedrockPseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                                   "# ADJCALLSTACKUP $amt1 $amt2",
                                   [(BedrockCallSeqEnd timm:$amt1,
                                                        timm:$amt2)]>;
}

def ADD64fi : BedrockPseudo<(outs GPR64:$dst), (ins memri:$addr),
                            "# ADD64fi $addr, $dst">;

multiclass MovRR<string Suffix, RegisterClass RC> {
  def rr : BedrockInst<(outs RC:$dst), (ins RC:$src),
                       "MOV." # Suffix # "\t$src, $dst">;
}

multiclass MovRI<string Suffix, RegisterClass RC, Operand ImmOp> {
  def ri : BedrockInst<(outs RC:$dst), (ins ImmOp:$imm),
                       "MOV." # Suffix # "\t$imm, $dst",
                       [(set RC:$dst, imm:$imm)]>;
}

multiclass MovRM<string Suffix, RegisterClass RC, PatFrag LoadOp> {
  let mayLoad = 1 in
  def rm : BedrockInst<(outs RC:$dst), (ins memri:$addr),
                       "MOV." # Suffix # "\t$addr, $dst",
                       [(set RC:$dst, (LoadOp addr:$addr))]>;
}

multiclass MovPostRM<string Suffix, RegisterClass RC> {
  let mayLoad = 1 in
  def postrm : BedrockInst<(outs RC:$dst), (ins mempost:$addr),
                           "MOV." # Suffix # "\t$addr, $dst">;
}

multiclass MovMR<string Suffix, RegisterClass RC> {
  let mayStore = 1 in
  def mr : BedrockInst<(outs), (ins RC:$src, memri:$addr),
                       "MOV." # Suffix # "\t$src, $addr",
                       [(store RC:$src, addr:$addr)]>;
}

multiclass MovMI<string Suffix, Operand ImmOp> {
  let mayStore = 1 in
  def mi : BedrockInst<(outs), (ins ImmOp:$src, memri:$addr),
                       "MOV." # Suffix # "\t$src, $addr">;
}

multiclass MovPostMR<string Suffix, RegisterClass RC> {
  let mayStore = 1 in
  def postmr : BedrockInst<(outs), (ins RC:$src, mempost:$addr),
                           "MOV." # Suffix # "\t$src, $addr">;
}

multiclass MovMM<string Suffix> {
  let mayLoad = 1, mayStore = 1 in {
  def mm : BedrockInst<(outs), (ins memri:$src, memri:$dst),
                       "MOV." # Suffix # "\t$src, $dst">;
  def mmpostboth : BedrockInst<(outs), (ins mempost:$src, mempost:$dst),
                               "MOV." # Suffix # "\t$src, $dst">;
  }
}

class MovCCRR<string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst),
                  (ins RC:$dst_in, RC:$src, bedrockcc:$cond),
                  "MOV$cond." # Suffix # "\t$src, $dst"> {
  let Constraints = "$dst = $dst_in";
  let Uses = [FLAGS];
  let isSelect = 1;
}

class MovCCRM<string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst),
                  (ins RC:$dst_in, memri:$addr, bedrockcc:$cond),
                  "MOV$cond." # Suffix # "\t$addr, $dst"> {
  let Constraints = "$dst = $dst_in";
  let Uses = [FLAGS];
  let mayLoad = 1;
}

class MovCCMR<string Suffix, RegisterClass RC>
    : BedrockInst<(outs), (ins RC:$src, memri:$addr, bedrockcc:$cond),
                  "MOV$cond." # Suffix # "\t$src, $addr"> {
  let Uses = [FLAGS];
  let mayStore = 1;
}

class RepMovMM<string Prefix, string Suffix>
    : BedrockInst<(outs D32:$counter),
                  (ins D32:$counter_in, memupd:$src, memupd:$dst),
                  Prefix # "\t$counter, MOV." # Suffix # "\t$src, $dst"> {
  let Constraints = "$counter = $counter_in";
  let Defs = [FLAGS];
  let hasSideEffects = 1;
  let mayLoad = 1;
  let mayStore = 1;
}

class RepMovMM64<string Prefix, string Suffix>
    : BedrockInst<(outs D64:$counter),
                  (ins D64:$counter_in, memupd:$src, memupd:$dst),
                  Prefix # "\t$counter, MOV." # Suffix # "\t$src, $dst"> {
  let Constraints = "$counter = $counter_in";
  let Defs = [FLAGS];
  let hasSideEffects = 1;
  let mayLoad = 1;
  let mayStore = 1;
}

class RepMovPostRM<string Prefix, string Suffix, RegisterClass RC>
    : BedrockInst<(outs D32:$counter, RC:$dst),
                  (ins D32:$counter_in, memupd:$addr),
                  Prefix # "\t$counter, MOV." # Suffix # "\t$addr, $dst"> {
  let Constraints = "$counter = $counter_in";
  let Defs = [FLAGS];
  let hasSideEffects = 1;
  let mayLoad = 1;
}

class RepMovPostRM64<string Prefix, string Suffix, RegisterClass RC>
    : BedrockInst<(outs D64:$counter, RC:$dst),
                  (ins D64:$counter_in, memupd:$addr),
                  Prefix # "\t$counter, MOV." # Suffix # "\t$addr, $dst"> {
  let Constraints = "$counter = $counter_in";
  let Defs = [FLAGS];
  let hasSideEffects = 1;
  let mayLoad = 1;
}

class RepMovPostMR<string Prefix, string Suffix, RegisterClass RC>
    : BedrockInst<(outs D32:$counter),
                  (ins D32:$counter_in, RC:$src, memupd:$addr),
                  Prefix # "\t$counter, MOV." # Suffix # "\t$src, $addr"> {
  let Constraints = "$counter = $counter_in";
  let Defs = [FLAGS];
  let hasSideEffects = 1;
  let mayStore = 1;
}

class RepMovPostMR64<string Prefix, string Suffix, RegisterClass RC>
    : BedrockInst<(outs D64:$counter),
                  (ins D64:$counter_in, RC:$src, memupd:$addr),
                  Prefix # "\t$counter, MOV." # Suffix # "\t$src, $addr"> {
  let Constraints = "$counter = $counter_in";
  let Defs = [FLAGS];
  let hasSideEffects = 1;
  let mayStore = 1;
}

defm MOV8  : MovRR<"B", GPR8>;
defm MOV16 : MovRR<"W", GPR16>;
defm MOV32 : MovRR<"L", GPR32>;
defm MOV64 : MovRR<"Q", GPR64>;

defm MOV8  : MovRI<"B", GPR8, i8imm>;
defm MOV16 : MovRI<"W", GPR16, i16imm>;
defm MOV32 : MovRI<"L", GPR32, shortimm32>;
defm MOV64 : MovRI<"Q", GPR64, shortimm64>;

def MOV64abs : BedrockInst<(outs GPR64:$dst), (ins abssymbol:$imm),
                           "MOV.Q\t$imm, $dst">;

defm MOV8  : MovRM<"B", GPR8, load>;
defm MOV16 : MovRM<"W", GPR16, load>;
defm MOV32 : MovRM<"L", GPR32, load>;
defm MOV64 : MovRM<"Q", GPR64, load>;

defm MOV8  : MovPostRM<"B", GPR8>;
defm MOV16 : MovPostRM<"W", GPR16>;
defm MOV32 : MovPostRM<"L", GPR32>;
defm MOV64 : MovPostRM<"Q", GPR64>;

defm MOV8  : MovMR<"B", GPR8>;
defm MOV16 : MovMR<"W", GPR16>;
defm MOV32 : MovMR<"L", GPR32>;
defm MOV64 : MovMR<"Q", GPR64>;

defm MOV8  : MovMI<"B", i8imm>;
defm MOV16 : MovMI<"W", i16imm>;
defm MOV32 : MovMI<"L", shortimm32>;
defm MOV64 : MovMI<"Q", shortimm64>;

defm MOV8  : MovPostMR<"B", GPR8>;
defm MOV16 : MovPostMR<"W", GPR16>;
defm MOV32 : MovPostMR<"L", GPR32>;
defm MOV64 : MovPostMR<"Q", GPR64>;

defm MOV8  : MovMM<"B">;
defm MOV16 : MovMM<"W">;
defm MOV32 : MovMM<"L">;
defm MOV64 : MovMM<"Q">;

class MovIdxRM<string Suffix, RegisterClass RC, Operand AddrOp>
    : BedrockInst<(outs RC:$dst), (ins AddrOp:$addr),
                  "MOV." # Suffix # "\t$addr, $dst"> {
  let mayLoad = 1;
}

class MovIdxMR<string Suffix, RegisterClass RC, Operand AddrOp>
    : BedrockInst<(outs), (ins RC:$src, AddrOp:$addr),
                  "MOV." # Suffix # "\t$src, $addr"> {
  let mayStore = 1;
}

def MOVCC8rr  : MovCCRR<"B", D8>;
def MOVCC16rr : MovCCRR<"W", D16>;
def MOVCC32rr : MovCCRR<"L", GPR32>;
def MOVCC64rr : MovCCRR<"Q", GPR64>;

def MOVCC8rm  : MovCCRM<"B", D8>;
def MOVCC16rm : MovCCRM<"W", D16>;
def MOVCC32rm : MovCCRM<"L", GPR32>;
def MOVCC64rm : MovCCRM<"Q", GPR64>;

def MOVCC8mr  : MovCCMR<"B", D8>;
def MOVCC16mr : MovCCMR<"W", D16>;
def MOVCC32mr : MovCCMR<"L", GPR32>;
def MOVCC64mr : MovCCMR<"Q", GPR64>;

def REPMOV8mmpostboth64 : RepMovMM64<"REP", "B">;
def REPMOV32mmpostboth : RepMovMM<"REP", "L">;
def REPMOV64mmpostboth : RepMovMM<"REP", "Q">;
def REPNEMOV32mmpostboth : RepMovMM<"REPNE", "L">;
def REPNEMOV32postrm : RepMovPostRM<"REPNE", "L", D32>;
def REPMOV8postmr64 : RepMovPostMR64<"REP", "B", GPR8>;
def REPMOV32postmr : RepMovPostMR<"REP", "L", GPR32>;

def MOV8idx1rm   : MovIdxRM<"B", GPR8, idx1addr>;
def MOV8idx1mr   : MovIdxMR<"B", GPR8, idx1addr>;
def MOV8idx4rm   : MovIdxRM<"B", GPR8, idx4addr>;
def MOV8idx4mr   : MovIdxMR<"B", GPR8, idx4addr>;
def MOV8idx4lrm  : MovIdxRM<"B", GPR8, idx4laddr>;
def MOV8idx4lmr  : MovIdxMR<"B", GPR8, idx4laddr>;
def MOV16idx1rm  : MovIdxRM<"W", GPR16, idx1addr>;
def MOV16idx1mr  : MovIdxMR<"W", GPR16, idx1addr>;
def MOV16idx4rm  : MovIdxRM<"W", GPR16, idx4addr>;
def MOV16idx4mr  : MovIdxMR<"W", GPR16, idx4addr>;
def MOV16idx4lrm : MovIdxRM<"W", GPR16, idx4laddr>;
def MOV16idx4lmr : MovIdxMR<"W", GPR16, idx4laddr>;
def MOV32idx1rm  : MovIdxRM<"L", GPR32, idx1addr>;
def MOV32idx1mr  : MovIdxMR<"L", GPR32, idx1addr>;
def MOV32idx4rm  : MovIdxRM<"L", GPR32, idx4addr>;
def MOV32idx4mr  : MovIdxMR<"L", GPR32, idx4addr>;
def MOV32idx4lrm : MovIdxRM<"L", GPR32, idx4laddr>;
def MOV32idx4lmr : MovIdxMR<"L", GPR32, idx4laddr>;
def MOV64idx1rm  : MovIdxRM<"Q", GPR64, idx1addr>;
def MOV64idx1mr  : MovIdxMR<"Q", GPR64, idx1addr>;
def MOV64idx4rm  : MovIdxRM<"Q", GPR64, idx4addr>;
def MOV64idx4mr  : MovIdxMR<"Q", GPR64, idx4addr>;
def MOV64idx4lrm : MovIdxRM<"Q", GPR64, idx4laddr>;
def MOV64idx4lmr : MovIdxMR<"Q", GPR64, idx4laddr>;
let mayLoad = 1, mayStore = 1 in {
def MOV32idx1mm : BedrockInst<(outs), (ins idx1addr:$src, memri:$dst),
                              "MOV.L\t$src, $dst">;
def MOV32midx1 : BedrockInst<(outs), (ins memri:$src, idx1addr:$dst),
                             "MOV.L\t$src, $dst">;
def MOV32idx4mm : BedrockInst<(outs), (ins idx4addr:$src, memri:$dst),
                              "MOV.L\t$src, $dst">;
def MOV32midx4 : BedrockInst<(outs), (ins memri:$src, idx4addr:$dst),
                             "MOV.L\t$src, $dst">;
def MOV32idx4lmm : BedrockInst<(outs), (ins idx4laddr:$src, memri:$dst),
                               "MOV.L\t$src, $dst">;
def MOV32midx4l : BedrockInst<(outs), (ins memri:$src, idx4laddr:$dst),
                              "MOV.L\t$src, $dst">;
}

let hasSideEffects = 1 in
def AFENCE : BedrockInst<(outs), (ins), "AFENCE", [(BedrockAFENCE)]>;

let hasSideEffects = 1 in
def HALT : BedrockInst<(outs), (ins), "HALT">;

def NOP : BedrockInst<(outs), (ins), "NOP">;

def CLR64r : BedrockInst<(outs INT64:$dst), (ins), "CLR.Q\t$dst">;
let mayStore = 1 in
def CLRm : BedrockInst<(outs), (ins memri:$dst), "CLR\t$dst">;

class IncDecR<string Mnemonic, string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst), (ins RC:$src),
                  Mnemonic # "." # Suffix # "\t$dst"> {
  let Constraints = "$dst = $src";
  let Defs = [FLAGS];
}

class IncDecM<string Mnemonic, string Suffix>
    : BedrockInst<(outs), (ins memri:$dst),
                  Mnemonic # "." # Suffix # "\t$dst"> {
  let mayLoad = 1;
  let mayStore = 1;
  let Defs = [FLAGS];
}

class IncDecIdxM<string Mnemonic, string Suffix, Operand AddrOp>
    : BedrockInst<(outs), (ins AddrOp:$dst),
                  Mnemonic # "." # Suffix # "\t$dst"> {
  let mayLoad = 1;
  let mayStore = 1;
  let Defs = [FLAGS];
}

def INC8r  : IncDecR<"INC", "B", GPR8>;
def INC16r : IncDecR<"INC", "W", GPR16>;
def INC32r : IncDecR<"INC", "L", GPR32>;
def INC64r : IncDecR<"INC", "Q", GPR64>;
def INC8m  : IncDecM<"INC", "B">;
def INC16m : IncDecM<"INC", "W">;
def INC32m : IncDecM<"INC", "L">;
def INC64m : IncDecM<"INC", "Q">;
def INC32idx1m  : IncDecIdxM<"INC", "L", idx1addr>;
def INC32idx4m  : IncDecIdxM<"INC", "L", idx4addr>;
def INC32idx4lm : IncDecIdxM<"INC", "L", idx4laddr>;

def DEC8r  : IncDecR<"DEC", "B", GPR8>;
def DEC16r : IncDecR<"DEC", "W", GPR16>;
def DEC32r : IncDecR<"DEC", "L", GPR32>;
def DEC64r : IncDecR<"DEC", "Q", GPR64>;
def DEC8m  : IncDecM<"DEC", "B">;
def DEC16m : IncDecM<"DEC", "W">;
def DEC32m : IncDecM<"DEC", "L">;
def DEC64m : IncDecM<"DEC", "Q">;
def DEC32idx1m  : IncDecIdxM<"DEC", "L", idx1addr>;
def DEC32idx4m  : IncDecIdxM<"DEC", "L", idx4addr>;
def DEC32idx4lm : IncDecIdxM<"DEC", "L", idx4laddr>;

class AtomicFetch<string Mnemonic, string Suffix, RegisterClass RC,
                  SDPatternOperator OpNode>
    : BedrockInst<(outs RC:$dst),
                  (ins RC:$src, memri:$addr, memory_order:$order),
                  Mnemonic # "." # Suffix # "/$order\t$src, $addr",
                  [(set RC:$dst, (OpNode addr:$addr, RC:$src, timm:$order))]> {
  let Constraints = "$dst = $src";
  let mayLoad = 1;
  let mayStore = 1;
}

def FETCHADD8  : AtomicFetch<"FETCHADD", "B", D8, BedrockFetchAdd>;
def FETCHADD16 : AtomicFetch<"FETCHADD", "W", D16, BedrockFetchAdd>;
def FETCHADD32 : AtomicFetch<"FETCHADD", "L", D32, BedrockFetchAdd>;
def FETCHADD64 : AtomicFetch<"FETCHADD", "Q", D64, BedrockFetchAdd>;
def FETCHSUB8  : AtomicFetch<"FETCHSUB", "B", D8, BedrockFetchSub>;
def FETCHSUB16 : AtomicFetch<"FETCHSUB", "W", D16, BedrockFetchSub>;
def FETCHSUB32 : AtomicFetch<"FETCHSUB", "L", D32, BedrockFetchSub>;
def FETCHSUB64 : AtomicFetch<"FETCHSUB", "Q", D64, BedrockFetchSub>;
def FETCHAND8  : AtomicFetch<"FETCHAND", "B", D8, BedrockFetchAnd>;
def FETCHAND16 : AtomicFetch<"FETCHAND", "W", D16, BedrockFetchAnd>;
def FETCHAND32 : AtomicFetch<"FETCHAND", "L", D32, BedrockFetchAnd>;
def FETCHAND64 : AtomicFetch<"FETCHAND", "Q", D64, BedrockFetchAnd>;
def FETCHOR8   : AtomicFetch<"FETCHOR", "B", D8, BedrockFetchOr>;
def FETCHOR16  : AtomicFetch<"FETCHOR", "W", D16, BedrockFetchOr>;
def FETCHOR32  : AtomicFetch<"FETCHOR", "L", D32, BedrockFetchOr>;
def FETCHOR64  : AtomicFetch<"FETCHOR", "Q", D64, BedrockFetchOr>;
def FETCHXOR8  : AtomicFetch<"FETCHXOR", "B", D8, BedrockFetchXor>;
def FETCHXOR16 : AtomicFetch<"FETCHXOR", "W", D16, BedrockFetchXor>;
def FETCHXOR32 : AtomicFetch<"FETCHXOR", "L", D32, BedrockFetchXor>;
def FETCHXOR64 : AtomicFetch<"FETCHXOR", "Q", D64, BedrockFetchXor>;

class AtomicCmpXchg<string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst),
                  (ins RC:$expected, RC:$desired, memri:$addr,
                   memory_order:$order),
                  "CMPXCHG." # Suffix # "/$order\t$expected, $desired, $addr",
                  [(set RC:$dst,
                    (BedrockCmpXchg addr:$addr, RC:$expected, RC:$desired,
                                    timm:$order))]> {
  let Constraints = "$dst = $expected";
  let mayLoad = 1;
  let mayStore = 1;
  let Defs = [FLAGS];
}

def CMPXCHG8  : AtomicCmpXchg<"B", D8>;
def CMPXCHG16 : AtomicCmpXchg<"W", D16>;
def CMPXCHG32 : AtomicCmpXchg<"L", D32>;
def CMPXCHG64 : AtomicCmpXchg<"Q", D64>;

multiclass FMovRR<string Suffix, RegisterClass RC> {
  def rr : BedrockInst<(outs RC:$dst), (ins RC:$src),
                       "FMOV." # Suffix # "\t$src, $dst">;
}

multiclass FMovRM<string Suffix, RegisterClass RC, PatFrag LoadOp> {
  let mayLoad = 1 in
  def rm : BedrockInst<(outs RC:$dst), (ins memri:$addr),
                       "FMOV." # Suffix # "\t$addr, $dst",
                       [(set RC:$dst, (LoadOp addr:$addr))]>;
}

multiclass FMovMR<string Suffix, RegisterClass RC> {
  let mayStore = 1 in
  def mr : BedrockInst<(outs), (ins RC:$src, memri:$addr),
                       "FMOV." # Suffix # "\t$src, $addr",
                       [(store RC:$src, addr:$addr)]>;
}

class FMovCCRR<string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst),
                  (ins RC:$dst_in, RC:$src, bedrockcc:$cond),
                  "FMOV$cond." # Suffix # "\t$src, $dst"> {
  let Constraints = "$dst = $dst_in";
  let Uses = [FLAGS];
  let isSelect = 1;
}

class FMovCCRM<string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst),
                  (ins RC:$dst_in, memri:$addr, bedrockcc:$cond),
                  "FMOV$cond." # Suffix # "\t$addr, $dst"> {
  let Constraints = "$dst = $dst_in";
  let Uses = [FLAGS];
  let mayLoad = 1;
}

class FMovCCMR<string Suffix, RegisterClass RC>
    : BedrockInst<(outs), (ins RC:$src, memri:$addr, bedrockcc:$cond),
                  "FMOV$cond." # Suffix # "\t$src, $addr"> {
  let Uses = [FLAGS];
  let mayStore = 1;
}

defm FMOV32 : FMovRR<"S", F32>;
defm FMOV64 : FMovRR<"D", F64>;

defm FMOV32 : FMovRM<"S", F32, load>;
defm FMOV64 : FMovRM<"D", F64, load>;

defm FMOV32 : FMovMR<"S", F32>;
defm FMOV64 : FMovMR<"D", F64>;

def FMOVCC32rr : FMovCCRR<"S", F32>;
def FMOVCC64rr : FMovCCRR<"D", F64>;
def FMOVCC32rm : FMovCCRM<"S", F32>;
def FMOVCC64rm : FMovCCRM<"D", F64>;
def FMOVCC32mr : FMovCCMR<"S", F32>;
def FMOVCC64mr : FMovCCMR<"D", F64>;

class BinRR<string Mnemonic, string Suffix, RegisterClass RC,
            SDPatternOperator OpNode, bit Commutable = 0>
    : BedrockInst<(outs RC:$dst), (ins RC:$lhs, RC:$rhs),
                  Mnemonic # "." # Suffix # "\t$rhs, $dst",
                  [(set RC:$dst, (OpNode RC:$lhs, RC:$rhs))]> {
  let Constraints = "$dst = $lhs";
  let isCommutable = Commutable;
  let Defs = [FLAGS];
}

class BinRI<string Mnemonic, string Suffix, RegisterClass RC, Operand ImmOp,
            SDPatternOperator OpNode>
    : BedrockInst<(outs RC:$dst), (ins RC:$lhs, ImmOp:$rhs),
                  Mnemonic # "." # Suffix # "\t$rhs, $dst",
                  [(set RC:$dst, (OpNode RC:$lhs, imm:$rhs))]> {
  let Constraints = "$dst = $lhs";
  let Defs = [FLAGS];
}

class BinRM<string Mnemonic, string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst), (ins RC:$lhs, memri:$rhs),
                  Mnemonic # "." # Suffix # "\t$rhs, $dst"> {
  let Constraints = "$dst = $lhs";
  let Defs = [FLAGS];
  let mayLoad = 1;
}

class BinIdxRM<string Mnemonic, string Suffix, RegisterClass RC, Operand AddrOp>
    : BedrockInst<(outs RC:$dst), (ins RC:$lhs, AddrOp:$rhs),
                  Mnemonic # "." # Suffix # "\t$rhs, $dst"> {
  let Constraints = "$dst = $lhs";
  let Defs = [FLAGS];
  let mayLoad = 1;
}

class BinPostRM<string Mnemonic, string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst), (ins RC:$lhs, mempost:$rhs),
                  Mnemonic # "." # Suffix # "\t$rhs, $dst"> {
  let Constraints = "$dst = $lhs";
  let Defs = [FLAGS];
  let mayLoad = 1;
}

class RepBinPostRM<string Prefix, string Mnemonic, string Suffix,
                   RegisterClass RC>
    : BedrockInst<(outs D32:$counter, RC:$dst),
                  (ins D32:$counter_in, RC:$lhs, memupd:$rhs),
                  Prefix # "\t$counter, " # Mnemonic # "." # Suffix #
                      "\t$rhs, $dst"> {
  let Constraints = "$counter = $counter_in, $dst = $lhs";
  let Defs = [FLAGS];
  let hasSideEffects = 1;
  let mayLoad = 1;
}

class RepCmpPostRM<string Prefix, string Suffix, RegisterClass RHSRC>
    : BedrockInst<(outs D32:$counter),
                  (ins D32:$counter_in, memupd:$lhs, RHSRC:$rhs),
                  Prefix # "\t$counter, CMP." # Suffix # "\t$lhs, $rhs"> {
  let Constraints = "$counter = $counter_in";
  let Defs = [FLAGS];
  let hasSideEffects = 1;
  let mayLoad = 1;
}

class BinMR<string Mnemonic, string Suffix, RegisterClass RC>
    : BedrockInst<(outs), (ins RC:$src, memri:$dst),
                  Mnemonic # "." # Suffix # "\t$src, $dst"> {
  let Defs = [FLAGS];
  let mayLoad = 1;
  let mayStore = 1;
}

class BinMI<string Mnemonic, string Suffix, Operand ImmOp>
    : BedrockInst<(outs), (ins ImmOp:$src, memri:$dst),
                  Mnemonic # "." # Suffix # "\t$src, $dst"> {
  let Defs = [FLAGS];
  let mayLoad = 1;
  let mayStore = 1;
}

class CarryRI<string Mnemonic, string Suffix, RegisterClass RC, Operand ImmOp>
    : BedrockInst<(outs RC:$dst), (ins RC:$lhs, ImmOp:$src),
                  Mnemonic # "." # Suffix # "\t$src, $dst"> {
  let Constraints = "$dst = $lhs";
  let Uses = [FLAGS];
  let Defs = [FLAGS];
}

class CarryMI<string Mnemonic, string Suffix, Operand ImmOp>
    : BedrockInst<(outs), (ins ImmOp:$src, memri:$dst),
                  Mnemonic # "." # Suffix # "\t$src, $dst"> {
  let mayLoad = 1;
  let mayStore = 1;
  let Uses = [FLAGS];
  let Defs = [FLAGS];
}

class FlagMI<string Mnemonic, string Suffix, Operand ImmOp>
    : BedrockInst<(outs), (ins ImmOp:$src, memri:$dst),
                  Mnemonic # "." # Suffix # "\t$src, $dst"> {
  let mayLoad = 1;
  let Defs = [FLAGS];
}

class CmpRI<string Suffix, RegisterClass RC, Operand ImmOp>
    : BedrockInst<(outs), (ins RC:$lhs, ImmOp:$rhs),
                  "CMP." # Suffix # "\t$rhs, $lhs"> {
  let Defs = [FLAGS];
}

class TestRI<string Suffix, RegisterClass RC, Operand ImmOp>
    : BedrockInst<(outs), (ins RC:$lhs, ImmOp:$rhs),
                  "TEST." # Suffix # "\t$rhs, $lhs"> {
  let Defs = [FLAGS];
}

class A32BinRR<string Mnemonic>
    : BedrockInst<(outs A32:$dst), (ins A32:$lhs, GPR32:$rhs),
                  Mnemonic # ".Q\t$rhs, $dst"> {
  let Constraints = "$dst = $lhs";
}

class A32BinRI<string Mnemonic>
    : BedrockInst<(outs A32:$dst), (ins A32:$lhs, shortimm64:$rhs),
                  Mnemonic # ".Q\t$rhs, $dst"> {
  let Constraints = "$dst = $lhs";
}

def ADD8rr  : BinRR<"ADD", "B", GPR8, add, 1>;
def ADD16rr : BinRR<"ADD", "W", GPR16, add, 1>;
def ADD32rr : BinRR<"ADD", "L", GPR32, add, 1>;
def ADD64rr : BinRR<"ADD", "Q", GPR64, add, 1>;
def ADD32ar : A32BinRR<"ADD">;

def ADD8ri  : BinRI<"ADD", "B", GPR8, i8imm, add>;
def ADD16ri : BinRI<"ADD", "W", GPR16, i16imm, add>;
def ADD32ri : BinRI<"ADD", "L", GPR32, i32imm, add>;
def ADD64ri : BinRI<"ADD", "Q", GPR64, shortimm64, add>;
def ADD32ai : A32BinRI<"ADD">;
def ADD8rm  : BinRM<"ADD", "B", GPR8>;
def ADD16rm : BinRM<"ADD", "W", GPR16>;
def ADD32rm : BinRM<"ADD", "L", D32>;
def ADD64rm : BinRM<"ADD", "Q", GPR64>;
def ADD32idx1rm  : BinIdxRM<"ADD", "L", D32, idx1addr>;
def ADD32idx4rm  : BinIdxRM<"ADD", "L", D32, idx4addr>;
def ADD32idx4lrm : BinIdxRM<"ADD", "L", D32, idx4laddr>;
def ADD8postrm  : BinPostRM<"ADD", "B", GPR8>;
def ADD16postrm : BinPostRM<"ADD", "W", GPR16>;
def ADD32postrm : BinPostRM<"ADD", "L", D32>;
def ADD64postrm : BinPostRM<"ADD", "Q", GPR64>;
def REPADD32postrm : RepBinPostRM<"REP", "ADD", "L", D32>;
def ADD8mr  : BinMR<"ADD", "B", GPR8>;
def ADD16mr : BinMR<"ADD", "W", GPR16>;
def ADD32mr : BinMR<"ADD", "L", D32>;
def ADD64mr : BinMR<"ADD", "Q", GPR64>;
def ADC8ri  : CarryRI<"ADC", "B", GPR8, u6imm8>;
def ADC16ri : CarryRI<"ADC", "W", GPR16, u6imm16>;
def ADC32ri : CarryRI<"ADC", "L", GPR32, u6imm32>;
def ADC64ri : CarryRI<"ADC", "Q", GPR64, u6imm64>;
def ADC8mi  : CarryMI<"ADC", "B", u6imm8>;
def ADC16mi : CarryMI<"ADC", "W", u6imm16>;
def ADC32mi : CarryMI<"ADC", "L", u6imm32>;
def ADC64mi : CarryMI<"ADC", "Q", u6imm64>;
def ADD8mi  : BinMI<"ADD", "B", u6imm8>;
def ADD16mi : BinMI<"ADD", "W", u6imm16>;
def ADD32mi : BinMI<"ADD", "L", u6imm32>;
def ADD64mi : BinMI<"ADD", "Q", u6imm64>;

def SUB8rr  : BinRR<"SUB", "B", GPR8, sub>;
def SUB16rr : BinRR<"SUB", "W", GPR16, sub>;
def SUB32rr : BinRR<"SUB", "L", GPR32, sub>;
def SUB64rr : BinRR<"SUB", "Q", GPR64, sub>;
def SUB32ar : A32BinRR<"SUB">;

def SUB8ri  : BinRI<"SUB", "B", GPR8, i8imm, sub>;
def SUB16ri : BinRI<"SUB", "W", GPR16, i16imm, sub>;
def SUB32ri : BinRI<"SUB", "L", GPR32, i32imm, sub>;
def SUB64ri : BinRI<"SUB", "Q", GPR64, shortimm64, sub>;
def SUB32ai : A32BinRI<"SUB">;
def SUB8rm  : BinRM<"SUB", "B", GPR8>;
def SUB16rm : BinRM<"SUB", "W", GPR16>;
def SUB32rm : BinRM<"SUB", "L", D32>;
def SUB64rm : BinRM<"SUB", "Q", GPR64>;
def SUB32idx1rm  : BinIdxRM<"SUB", "L", D32, idx1addr>;
def SUB32idx4rm  : BinIdxRM<"SUB", "L", D32, idx4addr>;
def SUB32idx4lrm : BinIdxRM<"SUB", "L", D32, idx4laddr>;
def SUB8mr  : BinMR<"SUB", "B", GPR8>;
def SUB16mr : BinMR<"SUB", "W", GPR16>;
def SUB32mr : BinMR<"SUB", "L", D32>;
def SUB64mr : BinMR<"SUB", "Q", GPR64>;
def SBB8ri  : CarryRI<"SBB", "B", GPR8, u6imm8>;
def SBB16ri : CarryRI<"SBB", "W", GPR16, u6imm16>;
def SBB32ri : CarryRI<"SBB", "L", GPR32, u6imm32>;
def SBB64ri : CarryRI<"SBB", "Q", GPR64, u6imm64>;
def SBB8mi  : CarryMI<"SBB", "B", u6imm8>;
def SBB16mi : CarryMI<"SBB", "W", u6imm16>;
def SBB32mi : CarryMI<"SBB", "L", u6imm32>;
def SBB64mi : CarryMI<"SBB", "Q", u6imm64>;
def SUB8mi  : BinMI<"SUB", "B", u6imm8>;
def SUB16mi : BinMI<"SUB", "W", u6imm16>;
def SUB32mi : BinMI<"SUB", "L", u6imm32>;
def SUB64mi : BinMI<"SUB", "Q", u6imm64>;
def SUB8postrm  : BinPostRM<"SUB", "B", GPR8>;
def SUB16postrm : BinPostRM<"SUB", "W", GPR16>;
def SUB32postrm : BinPostRM<"SUB", "L", D32>;
def SUB64postrm : BinPostRM<"SUB", "Q", GPR64>;

class ShiftRI<string Mnemonic, string Suffix, RegisterClass RC, Operand ImmOp,
              SDPatternOperator OpNode>
    : BedrockInst<(outs RC:$dst), (ins RC:$src, ImmOp:$count),
                  Mnemonic # "." # Suffix # "\t$count, $dst",
                  [(set RC:$dst, (OpNode RC:$src, ImmOp:$count))]> {
  let Constraints = "$dst = $src";
  let Defs = [FLAGS];
}

class ShiftRR<string Mnemonic, string Suffix, RegisterClass RC,
              SDPatternOperator OpNode>
    : BedrockInst<(outs RC:$dst), (ins RC:$src, GPR8:$count),
                  Mnemonic # "." # Suffix # "\t$count, $dst",
                  [(set RC:$dst, (OpNode RC:$src, GPR8:$count))]> {
  let Constraints = "$dst = $src";
  let Defs = [FLAGS];
}

class ShiftMI<string Mnemonic, string Suffix, Operand ImmOp>
    : BedrockInst<(outs), (ins ImmOp:$count, memri:$addr),
                  Mnemonic # "." # Suffix # "\t$count, $addr"> {
  let mayLoad = 1;
  let mayStore = 1;
  let Defs = [FLAGS];
}

class ShiftMR<string Mnemonic, string Suffix>
    : BedrockInst<(outs), (ins GPR8:$count, memri:$addr),
                  Mnemonic # "." # Suffix # "\t$count, $addr"> {
  let mayLoad = 1;
  let mayStore = 1;
  let Defs = [FLAGS];
}

def SHL8ri  : ShiftRI<"SHL", "B", GPR8, u6sel8, shl>;
def SHL16ri : ShiftRI<"SHL", "W", GPR16, u6sel8, shl>;
def SHL32ri : ShiftRI<"SHL", "L", GPR32, u6sel8, shl>;
def SHL64ri : ShiftRI<"SHL", "Q", GPR64, u6sel8, shl>;

def SHR8ri  : ShiftRI<"SHR", "B", GPR8, u6sel8, srl>;
def SHR16ri : ShiftRI<"SHR", "W", GPR16, u6sel8, srl>;
def SHR32ri : ShiftRI<"SHR", "L", GPR32, u6sel8, srl>;
def SHR64ri : ShiftRI<"SHR", "Q", GPR64, u6sel8, srl>;

def SAR8ri  : ShiftRI<"SAR", "B", GPR8, u6sel8, sra>;
def SAR16ri : ShiftRI<"SAR", "W", GPR16, u6sel8, sra>;
def SAR32ri : ShiftRI<"SAR", "L", GPR32, u6sel8, sra>;
def SAR64ri : ShiftRI<"SAR", "Q", GPR64, u6sel8, sra>;

def SHL8rr  : ShiftRR<"SHL", "B", GPR8, shl>;
def SHL16rr : ShiftRR<"SHL", "W", GPR16, shl>;
def SHL32rr : ShiftRR<"SHL", "L", GPR32, shl>;
def SHL64rr : ShiftRR<"SHL", "Q", GPR64, shl>;

def SHR8rr  : ShiftRR<"SHR", "B", GPR8, srl>;
def SHR16rr : ShiftRR<"SHR", "W", GPR16, srl>;
def SHR32rr : ShiftRR<"SHR", "L", GPR32, srl>;
def SHR64rr : ShiftRR<"SHR", "Q", GPR64, srl>;

def SAR8rr  : ShiftRR<"SAR", "B", GPR8, sra>;
def SAR16rr : ShiftRR<"SAR", "W", GPR16, sra>;
def SAR32rr : ShiftRR<"SAR", "L", GPR32, sra>;
def SAR64rr : ShiftRR<"SAR", "Q", GPR64, sra>;

def SHL8mi  : ShiftMI<"SHL", "B", u6sel8>;
def SHL16mi : ShiftMI<"SHL", "W", u6sel8>;
def SHL32mi : ShiftMI<"SHL", "L", u6sel8>;
def SHL64mi : ShiftMI<"SHL", "Q", u6sel8>;
def SHR8mi  : ShiftMI<"SHR", "B", u6sel8>;
def SHR16mi : ShiftMI<"SHR", "W", u6sel8>;
def SHR32mi : ShiftMI<"SHR", "L", u6sel8>;
def SHR64mi : ShiftMI<"SHR", "Q", u6sel8>;
def SAR8mi  : ShiftMI<"SAR", "B", u6sel8>;
def SAR16mi : ShiftMI<"SAR", "W", u6sel8>;
def SAR32mi : ShiftMI<"SAR", "L", u6sel8>;
def SAR64mi : ShiftMI<"SAR", "Q", u6sel8>;

def SHL8mr  : ShiftMR<"SHL", "B">;
def SHL16mr : ShiftMR<"SHL", "W">;
def SHL32mr : ShiftMR<"SHL", "L">;
def SHL64mr : ShiftMR<"SHL", "Q">;
def SHR8mr  : ShiftMR<"SHR", "B">;
def SHR16mr : ShiftMR<"SHR", "W">;
def SHR32mr : ShiftMR<"SHR", "L">;
def SHR64mr : ShiftMR<"SHR", "Q">;
def SAR8mr  : ShiftMR<"SAR", "B">;
def SAR16mr : ShiftMR<"SAR", "W">;
def SAR32mr : ShiftMR<"SAR", "L">;
def SAR64mr : ShiftMR<"SAR", "Q">;

class ExtRR<string Mnemonic, string Suffix, RegisterClass DstRC,
            RegisterClass SrcRC, SDPatternOperator OpNode>
    : BedrockInst<(outs DstRC:$dst), (ins SrcRC:$src),
                  Mnemonic # "." # Suffix # "\t$src, $dst",
                  [(set DstRC:$dst, (OpNode SrcRC:$src))]> {
  let Defs = [FLAGS];
}

class ExtRM<string Mnemonic, string Suffix, RegisterClass DstRC>
    : BedrockInst<(outs DstRC:$dst), (ins memri:$addr),
                  Mnemonic # "." # Suffix # "\t$addr, $dst"> {
  let mayLoad = 1;
  let Defs = [FLAGS];
}

class ExtMR<string Mnemonic, string Suffix, RegisterClass SrcRC>
    : BedrockInst<(outs), (ins SrcRC:$src, memri:$addr),
                  Mnemonic # "." # Suffix # "\t$src, $addr"> {
  let mayStore = 1;
  let Defs = [FLAGS];
}

def EXTZQ8rr  : ExtRR<"EXTZQ", "B", GPR64, GPR8, zext>;
def EXTZQ16rr : ExtRR<"EXTZQ", "W", GPR64, GPR16, zext>;
def EXTZQ32rr : ExtRR<"EXTZQ", "L", GPR64, GPR32, zext>;
def EXTZQ8rm  : ExtRM<"EXTZQ", "B", GPR64>;
def EXTZQ16rm : ExtRM<"EXTZQ", "W", GPR64>;
def EXTZQ32rm : ExtRM<"EXTZQ", "L", GPR64>;
def EXTZQ8mr  : ExtMR<"EXTZQ", "B", GPR8>;
def EXTZQ16mr : ExtMR<"EXTZQ", "W", GPR16>;
def EXTZQ32mr : ExtMR<"EXTZQ", "L", GPR32>;

def EXTSQ8rr  : ExtRR<"EXTSQ", "B", GPR64, GPR8, sext>;
def EXTSQ16rr : ExtRR<"EXTSQ", "W", GPR64, GPR16, sext>;
def EXTSQ32rr : ExtRR<"EXTSQ", "L", GPR64, GPR32, sext>;
def EXTSQ8rm  : ExtRM<"EXTSQ", "B", GPR64>;
def EXTSQ16rm : ExtRM<"EXTSQ", "W", GPR64>;
def EXTSQ32rm : ExtRM<"EXTSQ", "L", GPR64>;
def EXTSQ8mr  : ExtMR<"EXTSQ", "B", GPR8>;
def EXTSQ16mr : ExtMR<"EXTSQ", "W", GPR16>;
def EXTSQ32mr : ExtMR<"EXTSQ", "L", GPR32>;

def EXTZL8rr  : ExtRR<"EXTZL", "B", GPR32, GPR8, zext>;
def EXTZL16rr : ExtRR<"EXTZL", "W", GPR32, GPR16, zext>;
def EXTZL8rm  : ExtRM<"EXTZL", "B", GPR32>;
def EXTZL16rm : ExtRM<"EXTZL", "W", GPR32>;
def EXTZL8mr  : ExtMR<"EXTZL", "B", GPR8>;
def EXTZL16mr : ExtMR<"EXTZL", "W", GPR16>;

def EXTSL8rr  : ExtRR<"EXTSL", "B", GPR32, GPR8, sext>;
def EXTSL16rr : ExtRR<"EXTSL", "W", GPR32, GPR16, sext>;
def EXTSL8rm  : ExtRM<"EXTSL", "B", GPR32>;
def EXTSL16rm : ExtRM<"EXTSL", "W", GPR32>;
def EXTSL8mr  : ExtMR<"EXTSL", "B", GPR8>;
def EXTSL16mr : ExtMR<"EXTSL", "W", GPR16>;

def EXTZW8rr : ExtRR<"EXTZW", "B", GPR16, GPR8, zext>;
def EXTZW8rm : ExtRM<"EXTZW", "B", GPR16>;
def EXTZW8mr : ExtMR<"EXTZW", "B", GPR8>;

def EXTSW8rr : ExtRR<"EXTSW", "B", GPR16, GPR8, sext>;
def EXTSW8rm : ExtRM<"EXTSW", "B", GPR16>;
def EXTSW8mr : ExtMR<"EXTSW", "B", GPR8>;

def : Pat<(i32 (anyext GPR8:$src)), (EXTZL8rr GPR8:$src)>;
def : Pat<(i32 (anyext GPR16:$src)), (EXTZL16rr GPR16:$src)>;
def : Pat<(i64 (anyext GPR8:$src)), (EXTZQ8rr GPR8:$src)>;
def : Pat<(i64 (anyext GPR16:$src)), (EXTZQ16rr GPR16:$src)>;
def : Pat<(i64 (anyext GPR32:$src)), (EXTZQ32rr GPR32:$src)>;

class TruncRR<string Suffix, RegisterClass DstRC, RegisterClass SrcRC>
    : BedrockInst<(outs DstRC:$dst), (ins SrcRC:$src),
                  "MOV." # Suffix # "\t$src, $dst",
                  [(set DstRC:$dst, (trunc SrcRC:$src))]>;

def TRUNC64to32 : TruncRR<"L", GPR32, GPR64>;
def TRUNC64to16 : TruncRR<"W", GPR16, GPR64>;
def TRUNC64to8  : TruncRR<"B", GPR8, GPR64>;
def TRUNC32to16 : TruncRR<"W", GPR16, GPR32>;
def TRUNC32to8  : TruncRR<"B", GPR8, GPR32>;
def TRUNC16to8  : TruncRR<"B", GPR8, GPR16>;

def AND8rr  : BinRR<"AND", "B", GPR8, and, 1>;
def AND16rr : BinRR<"AND", "W", GPR16, and, 1>;
def AND32rr : BinRR<"AND", "L", D32, and, 1>;
def AND64rr : BinRR<"AND", "Q", GPR64, and, 1>;

def AND8ri  : BinRI<"AND", "B", GPR8, i8imm, and>;
def AND16ri : BinRI<"AND", "W", GPR16, i16imm, and>;
def AND32ri : BinRI<"AND", "L", D32, shortimm32, and>;
def AND64ri : BinRI<"AND", "Q", D64, i64imm, and>;
def AND64ai : BinRI<"AND", "Q", A64, shortimm64, and>;
def AND8rm  : BinRM<"AND", "B", GPR8>;
def AND16rm : BinRM<"AND", "W", GPR16>;
def AND32rm : BinRM<"AND", "L", D32>;
def AND64rm : BinRM<"AND", "Q", GPR64>;
def AND32idx1rm  : BinIdxRM<"AND", "L", D32, idx1addr>;
def AND32idx4rm  : BinIdxRM<"AND", "L", D32, idx4addr>;
def AND32idx4lrm : BinIdxRM<"AND", "L", D32, idx4laddr>;
def AND8mr  : BinMR<"AND", "B", GPR8>;
def AND16mr : BinMR<"AND", "W", GPR16>;
def AND32mr : BinMR<"AND", "L", D32>;
def AND64mr : BinMR<"AND", "Q", GPR64>;
def AND8mi  : BinMI<"AND", "B", u6imm8>;
def AND16mi : BinMI<"AND", "W", u6imm16>;
def AND32mi : BinMI<"AND", "L", u6imm32>;
def AND64mi : BinMI<"AND", "Q", u6imm64>;
def AND8postrm  : BinPostRM<"AND", "B", GPR8>;
def AND16postrm : BinPostRM<"AND", "W", GPR16>;
def AND32postrm : BinPostRM<"AND", "L", D32>;
def AND64postrm : BinPostRM<"AND", "Q", GPR64>;

def OR8rr  : BinRR<"OR", "B", GPR8, or, 1>;
def OR16rr : BinRR<"OR", "W", GPR16, or, 1>;
def OR32rr : BinRR<"OR", "L", D32, or, 1>;
def OR64rr : BinRR<"OR", "Q", GPR64, or, 1>;

def OR8ri  : BinRI<"OR", "B", GPR8, i8imm, or>;
def OR16ri : BinRI<"OR", "W", GPR16, i16imm, or>;
def OR32ri : BinRI<"OR", "L", D32, shortimm32, or>;
def OR64ri : BinRI<"OR", "Q", D64, i64imm, or>;
def OR64ai : BinRI<"OR", "Q", A64, shortimm64, or>;
def OR8rm  : BinRM<"OR", "B", GPR8>;
def OR16rm : BinRM<"OR", "W", GPR16>;
def OR32rm : BinRM<"OR", "L", D32>;
def OR64rm : BinRM<"OR", "Q", GPR64>;
def OR32idx1rm  : BinIdxRM<"OR", "L", D32, idx1addr>;
def OR32idx4rm  : BinIdxRM<"OR", "L", D32, idx4addr>;
def OR32idx4lrm : BinIdxRM<"OR", "L", D32, idx4laddr>;
def OR8mr  : BinMR<"OR", "B", GPR8>;
def OR16mr : BinMR<"OR", "W", GPR16>;
def OR32mr : BinMR<"OR", "L", D32>;
def OR64mr : BinMR<"OR", "Q", GPR64>;
def OR8mi  : BinMI<"OR", "B", u6imm8>;
def OR16mi : BinMI<"OR", "W", u6imm16>;
def OR32mi : BinMI<"OR", "L", u6imm32>;
def OR64mi : BinMI<"OR", "Q", u6imm64>;
def OR8postrm  : BinPostRM<"OR", "B", GPR8>;
def OR16postrm : BinPostRM<"OR", "W", GPR16>;
def OR32postrm : BinPostRM<"OR", "L", D32>;
def OR64postrm : BinPostRM<"OR", "Q", GPR64>;

def XOR8rr  : BinRR<"XOR", "B", GPR8, xor, 1>;
def XOR16rr : BinRR<"XOR", "W", GPR16, xor, 1>;
def XOR32rr : BinRR<"XOR", "L", D32, xor, 1>;
def XOR64rr : BinRR<"XOR", "Q", GPR64, xor, 1>;

def XOR8ri  : BinRI<"XOR", "B", GPR8, i8imm, xor>;
def XOR16ri : BinRI<"XOR", "W", GPR16, i16imm, xor>;
def XOR32ri : BinRI<"XOR", "L", D32, shortimm32, xor>;
def XOR64ri : BinRI<"XOR", "Q", D64, i64imm, xor>;
def XOR64ai : BinRI<"XOR", "Q", A64, shortimm64, xor>;
def XOR8rm  : BinRM<"XOR", "B", GPR8>;
def XOR16rm : BinRM<"XOR", "W", GPR16>;
def XOR32rm : BinRM<"XOR", "L", D32>;
def XOR64rm : BinRM<"XOR", "Q", GPR64>;
def XOR32idx1rm  : BinIdxRM<"XOR", "L", D32, idx1addr>;
def XOR32idx4rm  : BinIdxRM<"XOR", "L", D32, idx4addr>;
def XOR32idx4lrm : BinIdxRM<"XOR", "L", D32, idx4laddr>;
def XOR8mr  : BinMR<"XOR", "B", GPR8>;
def XOR16mr : BinMR<"XOR", "W", GPR16>;
def XOR32mr : BinMR<"XOR", "L", D32>;
def XOR64mr : BinMR<"XOR", "Q", GPR64>;
def XOR8mi  : BinMI<"XOR", "B", u6imm8>;
def XOR16mi : BinMI<"XOR", "W", u6imm16>;
def XOR32mi : BinMI<"XOR", "L", u6imm32>;
def XOR64mi : BinMI<"XOR", "Q", u6imm64>;
def XOR8postrm  : BinPostRM<"XOR", "B", GPR8>;
def XOR16postrm : BinPostRM<"XOR", "W", GPR16>;
def XOR32postrm : BinPostRM<"XOR", "L", D32>;
def XOR64postrm : BinPostRM<"XOR", "Q", GPR64>;

class UnaryRR<string Mnemonic, string Suffix, RegisterClass RC,
              SDPatternOperator OpNode>
    : BedrockInst<(outs RC:$dst), (ins RC:$src),
                  Mnemonic # "." # Suffix # "\t$src, $dst",
                  [(set RC:$dst, (OpNode RC:$src))]>;

def POPCNT8rr  : UnaryRR<"POPCNT", "B", D8, ctpop>;
def POPCNT16rr : UnaryRR<"POPCNT", "W", D16, ctpop>;
def POPCNT32rr : UnaryRR<"POPCNT", "L", D32, ctpop>;
def POPCNT64rr : UnaryRR<"POPCNT", "Q", D64, ctpop>;

def CLZ8rr  : UnaryRR<"CLZ", "B", D8, ctlz>;
def CLZ16rr : UnaryRR<"CLZ", "W", D16, ctlz>;
def CLZ32rr : UnaryRR<"CLZ", "L", D32, ctlz>;
def CLZ64rr : UnaryRR<"CLZ", "Q", D64, ctlz>;

def CTZ8rr  : UnaryRR<"CTZ", "B", D8, cttz>;
def CTZ16rr : UnaryRR<"CTZ", "W", D16, cttz>;
def CTZ32rr : UnaryRR<"CTZ", "L", D32, cttz>;
def CTZ64rr : UnaryRR<"CTZ", "Q", D64, cttz>;

def CLS8rr  : BedrockInst<(outs D8:$dst), (ins D8:$src),
                          "CLS.B\t$src, $dst">;
def CLS16rr : BedrockInst<(outs D16:$dst), (ins D16:$src),
                          "CLS.W\t$src, $dst">;
def CLS32rr : BedrockInst<(outs D32:$dst), (ins D32:$src),
                          "CLS.L\t$src, $dst">;
def CLS64rr : BedrockInst<(outs D64:$dst), (ins D64:$src),
                          "CLS.Q\t$src, $dst">;

def CTS8rr  : BedrockInst<(outs D8:$dst), (ins D8:$src),
                          "CTS.B\t$src, $dst">;
def CTS16rr : BedrockInst<(outs D16:$dst), (ins D16:$src),
                          "CTS.W\t$src, $dst">;
def CTS32rr : BedrockInst<(outs D32:$dst), (ins D32:$src),
                          "CTS.L\t$src, $dst">;
def CTS64rr : BedrockInst<(outs D64:$dst), (ins D64:$src),
                          "CTS.Q\t$src, $dst">;

def : Pat<(ctlz_zero_undef D8:$src), (CLZ8rr D8:$src)>;
def : Pat<(ctlz_zero_undef D16:$src), (CLZ16rr D16:$src)>;
def : Pat<(ctlz_zero_undef D32:$src), (CLZ32rr D32:$src)>;
def : Pat<(ctlz_zero_undef D64:$src), (CLZ64rr D64:$src)>;
def : Pat<(cttz_zero_undef D8:$src), (CTZ8rr D8:$src)>;
def : Pat<(cttz_zero_undef D16:$src), (CTZ16rr D16:$src)>;
def : Pat<(cttz_zero_undef D32:$src), (CTZ32rr D32:$src)>;
def : Pat<(cttz_zero_undef D64:$src), (CTZ64rr D64:$src)>;
def : Pat<(ctlz (not D8:$src)), (CLS8rr D8:$src)>;
def : Pat<(ctlz (not D16:$src)), (CLS16rr D16:$src)>;
def : Pat<(ctlz (not D32:$src)), (CLS32rr D32:$src)>;
def : Pat<(ctlz (not D64:$src)), (CLS64rr D64:$src)>;
def : Pat<(cttz (not D8:$src)), (CTS8rr D8:$src)>;
def : Pat<(cttz (not D16:$src)), (CTS16rr D16:$src)>;
def : Pat<(cttz (not D32:$src)), (CTS32rr D32:$src)>;
def : Pat<(cttz (not D64:$src)), (CTS64rr D64:$src)>;

class AbsRR<string Suffix, RegisterClass RC, SDPatternOperator OpNode>
    : BedrockInst<(outs RC:$dst), (ins RC:$src),
                  "ABS." # Suffix # "\t$dst",
                  [(set RC:$dst, (OpNode RC:$src))]> {
  let Constraints = "$dst = $src";
}

class MinMaxRR<string Mnemonic, string Suffix, RegisterClass RC,
               SDPatternOperator OpNode>
    : BedrockInst<(outs RC:$dst), (ins RC:$lhs, RC:$rhs),
                  Mnemonic # "." # Suffix # "\t$rhs, $dst",
                  [(set RC:$dst, (OpNode RC:$lhs, RC:$rhs))]> {
  let Constraints = "$dst = $lhs";
  let isCommutable = 1;
}

class MinMaxRRD32G<string Mnemonic, SDPatternOperator OpNode>
    : BedrockInst<(outs D32:$dst), (ins D32:$lhs, GPR32:$rhs),
                  Mnemonic # ".L\t$rhs, $dst",
                  [(set D32:$dst, (OpNode D32:$lhs, GPR32:$rhs))]> {
  let Constraints = "$dst = $lhs";
  let isCommutable = 1;
}

class MinMaxRI<string Mnemonic, string Suffix, RegisterClass RC, Operand ImmOp,
               SDPatternOperator OpNode>
    : BedrockInst<(outs RC:$dst), (ins RC:$lhs, ImmOp:$rhs),
                  Mnemonic # "." # Suffix # "\t$rhs, $dst",
                  [(set RC:$dst, (OpNode RC:$lhs, imm:$rhs))]> {
  let Constraints = "$dst = $lhs";
}

def ABS8rr  : AbsRR<"B", D8, abs>;
def ABS16rr : AbsRR<"W", D16, abs>;
def ABS32rr : AbsRR<"L", GPR32, abs>;
def ABS64rr : AbsRR<"Q", D64, abs>;
def MAXS8rr  : MinMaxRR<"MAXS", "B", D8, smax>;
def MAXS16rr : MinMaxRR<"MAXS", "W", D16, smax>;
def MAXS32rr : MinMaxRRD32G<"MAXS", smax>;
def MAXS64rr : MinMaxRR<"MAXS", "Q", D64, smax>;
def MAXU8rr  : MinMaxRR<"MAXU", "B", D8, umax>;
def MAXU16rr : MinMaxRR<"MAXU", "W", D16, umax>;
def MAXU32rr : MinMaxRRD32G<"MAXU", umax>;
def MAXU64rr : MinMaxRR<"MAXU", "Q", D64, umax>;
def MINS8rr  : MinMaxRR<"MINS", "B", D8, smin>;
def MINS16rr : MinMaxRR<"MINS", "W", D16, smin>;
def MINS32rr : MinMaxRRD32G<"MINS", smin>;
def MINS64rr : MinMaxRR<"MINS", "Q", D64, smin>;
def MINU8rr  : MinMaxRR<"MINU", "B", D8, umin>;
def MINU16rr : MinMaxRR<"MINU", "W", D16, umin>;
def MINU32rr : MinMaxRRD32G<"MINU", umin>;
def MINU64rr : MinMaxRR<"MINU", "Q", D64, umin>;

def MAXS8ri  : MinMaxRI<"MAXS", "B", D8, i8imm, smax>;
def MAXS16ri : MinMaxRI<"MAXS", "W", D16, i16imm, smax>;
def MAXS32ri : MinMaxRI<"MAXS", "L", D32, shortimm32, smax>;
def MAXS64ri : MinMaxRI<"MAXS", "Q", D64, shortimm64, smax>;
def MAXU8ri  : MinMaxRI<"MAXU", "B", D8, i8imm, umax>;
def MAXU16ri : MinMaxRI<"MAXU", "W", D16, i16imm, umax>;
def MAXU32ri : MinMaxRI<"MAXU", "L", D32, shortimm32, umax>;
def MAXU64ri : MinMaxRI<"MAXU", "Q", D64, shortimm64, umax>;
def MINS8ri  : MinMaxRI<"MINS", "B", D8, i8imm, smin>;
def MINS16ri : MinMaxRI<"MINS", "W", D16, i16imm, smin>;
def MINS32ri : MinMaxRI<"MINS", "L", D32, shortimm32, smin>;
def MINS64ri : MinMaxRI<"MINS", "Q", D64, shortimm64, smin>;
def MINU8ri  : MinMaxRI<"MINU", "B", D8, i8imm, umin>;
def MINU16ri : MinMaxRI<"MINU", "W", D16, i16imm, umin>;
def MINU32ri : MinMaxRI<"MINU", "L", D32, shortimm32, umin>;
def MINU64ri : MinMaxRI<"MINU", "Q", D64, shortimm64, umin>;

class CLMULRR<string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst), (ins RC:$lhs, RC:$rhs),
                  "CLMUL." # Suffix # "\t$rhs, $dst",
                  [(set RC:$dst, (clmul RC:$lhs, RC:$rhs))]> {
  let Constraints = "$dst = $lhs";
  let isCommutable = 1;
}

def CLMUL8rr  : CLMULRR<"B", D8>;
def CLMUL16rr : CLMULRR<"W", D16>;
def CLMUL32rr : CLMULRR<"L", D32>;
def CLMUL64rr : CLMULRR<"Q", D64>;

class BitOpRI<string Mnemonic, string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst), (ins RC:$src, u6sel8:$idx),
                  Mnemonic # "." # Suffix # "\t$idx, $dst"> {
  let Constraints = "$dst = $src";
  let Defs = [FLAGS];
}

class BitOpMI<string Mnemonic, string Suffix>
    : BedrockInst<(outs), (ins u6sel8:$idx, memri:$addr),
                  Mnemonic # "." # Suffix # "\t$idx, $addr"> {
  let mayLoad = 1;
  let mayStore = 1;
  let Defs = [FLAGS];
}

def BSET8ri  : BitOpRI<"BSET", "B", D8>;
def BSET16ri : BitOpRI<"BSET", "W", D16>;
def BSET32ri : BitOpRI<"BSET", "L", D32>;
def BSET64ri : BitOpRI<"BSET", "Q", D64>;
def BCLR8ri  : BitOpRI<"BCLR", "B", D8>;
def BCLR16ri : BitOpRI<"BCLR", "W", D16>;
def BCLR32ri : BitOpRI<"BCLR", "L", D32>;
def BCLR64ri : BitOpRI<"BCLR", "Q", D64>;
def BCHG8ri  : BitOpRI<"BCHG", "B", D8>;
def BCHG16ri : BitOpRI<"BCHG", "W", D16>;
def BCHG32ri : BitOpRI<"BCHG", "L", D32>;
def BCHG64ri : BitOpRI<"BCHG", "Q", D64>;

def BSET8mi  : BitOpMI<"BSET", "B">;
def BSET16mi : BitOpMI<"BSET", "W">;
def BSET32mi : BitOpMI<"BSET", "L">;
def BSET64mi : BitOpMI<"BSET", "Q">;
def BCLR8mi  : BitOpMI<"BCLR", "B">;
def BCLR16mi : BitOpMI<"BCLR", "W">;
def BCLR32mi : BitOpMI<"BCLR", "L">;
def BCLR64mi : BitOpMI<"BCLR", "Q">;
def BCHG8mi  : BitOpMI<"BCHG", "B">;
def BCHG16mi : BitOpMI<"BCHG", "W">;
def BCHG32mi : BitOpMI<"BCHG", "L">;
def BCHG64mi : BitOpMI<"BCHG", "Q">;

class BTestRI<string Suffix, RegisterClass RC>
    : BedrockInst<(outs), (ins RC:$src, u6sel8:$idx),
                  "BTEST." # Suffix # "\t$idx, $src"> {
  let Defs = [FLAGS];
}

def BTEST8ri  : BTestRI<"B", D8>;
def BTEST16ri : BTestRI<"W", D16>;
def BTEST32ri : BTestRI<"L", D32>;
def BTEST64ri : BTestRI<"Q", D64>;

class BndRRR<string Mnemonic, string Suffix, RegisterClass RC>
    : BedrockInst<(outs), (ins RC:$lo, RC:$value, RC:$hi),
                  Mnemonic # "." # Suffix # "\t$lo, $value, $hi"> {
  let Defs = [FLAGS];
}

def BNDUII8rrr  : BndRRR<"BNDUII", "B", D8>;
def BNDUII16rrr : BndRRR<"BNDUII", "W", D16>;
def BNDUII32rrr : BndRRR<"BNDUII", "L", D32>;
def BNDUII64rrr : BndRRR<"BNDUII", "Q", D64>;
def BNDUIX8rrr  : BndRRR<"BNDUIX", "B", D8>;
def BNDUIX16rrr : BndRRR<"BNDUIX", "W", D16>;
def BNDUIX32rrr : BndRRR<"BNDUIX", "L", D32>;
def BNDUIX64rrr : BndRRR<"BNDUIX", "Q", D64>;
def BNDUXI8rrr  : BndRRR<"BNDUXI", "B", D8>;
def BNDUXI16rrr : BndRRR<"BNDUXI", "W", D16>;
def BNDUXI32rrr : BndRRR<"BNDUXI", "L", D32>;
def BNDUXI64rrr : BndRRR<"BNDUXI", "Q", D64>;
def BNDUXX8rrr  : BndRRR<"BNDUXX", "B", D8>;
def BNDUXX16rrr : BndRRR<"BNDUXX", "W", D16>;
def BNDUXX32rrr : BndRRR<"BNDUXX", "L", D32>;
def BNDUXX64rrr : BndRRR<"BNDUXX", "Q", D64>;
def BNDSII8rrr  : BndRRR<"BNDSII", "B", D8>;
def BNDSII16rrr : BndRRR<"BNDSII", "W", D16>;
def BNDSII32rrr : BndRRR<"BNDSII", "L", D32>;
def BNDSII64rrr : BndRRR<"BNDSII", "Q", D64>;
def BNDSIX8rrr  : BndRRR<"BNDSIX", "B", D8>;
def BNDSIX16rrr : BndRRR<"BNDSIX", "W", D16>;
def BNDSIX32rrr : BndRRR<"BNDSIX", "L", D32>;
def BNDSIX64rrr : BndRRR<"BNDSIX", "Q", D64>;
def BNDSXI8rrr  : BndRRR<"BNDSXI", "B", D8>;
def BNDSXI16rrr : BndRRR<"BNDSXI", "W", D16>;
def BNDSXI32rrr : BndRRR<"BNDSXI", "L", D32>;
def BNDSXI64rrr : BndRRR<"BNDSXI", "Q", D64>;
def BNDSXX8rrr  : BndRRR<"BNDSXX", "B", D8>;
def BNDSXX16rrr : BndRRR<"BNDSXX", "W", D16>;
def BNDSXX32rrr : BndRRR<"BNDSXX", "L", D32>;
def BNDSXX64rrr : BndRRR<"BNDSXX", "Q", D64>;

let Defs = [SP], Uses = [SP], hasSideEffects = 1, mayStore = 1 in {
def PUSHD : BedrockInst<(outs), (ins D64:$reg), "PUSH\t$reg">;
def PUSHA : BedrockInst<(outs), (ins A64:$reg), "PUSH\t$reg">;
def PUSHM : BedrockInst<(outs), (ins regmask16:$regs), "PUSHM\t$regs">;
}

let Defs = [SP], Uses = [SP], hasSideEffects = 1, mayLoad = 1 in {
def POPD : BedrockInst<(outs D64:$reg), (ins), "POP\t$reg">;
def POPA : BedrockInst<(outs A64:$reg), (ins), "POP\t$reg">;
def POPM : BedrockInst<(outs), (ins regmask16:$regs), "POPM\t$regs">;
}

let Defs = [SP], Uses = [SP], hasSideEffects = 1, mayStore = 1 in
def FPUSHM : BedrockInst<(outs), (ins fregmask16:$regs), "FPUSHM\t$regs">;

let Defs = [SP], Uses = [SP], hasSideEffects = 1, mayLoad = 1 in
def FPOPM : BedrockInst<(outs), (ins fregmask16:$regs), "FPOPM\t$regs">;

class SumR<string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst), (ins regmask16:$regs),
                  "SUM." # Suffix # "\t$regs, $dst"> {
  let hasSideEffects = 1;
}

def SUM8d  : SumR<"B", D8>;
def SUM16d : SumR<"W", D16>;
def SUM32d : SumR<"L", GPR32>;
def SUM64d : SumR<"Q", D64>;
def SUM64a : SumR<"Q", A64>;

def MULU8rr  : BinRR<"MULU", "B", GPR8, mul, 1>;
def MULU16rr : BinRR<"MULU", "W", GPR16, mul, 1>;
def MULU32rr : BinRR<"MULU", "L", D32, mul, 1>;
def MULU64rr : BinRR<"MULU", "Q", GPR64, mul, 1>;
def MULU8rm  : BinRM<"MULU", "B", D8>;
def MULU16rm : BinRM<"MULU", "W", D16>;
def MULU32rm : BinRM<"MULU", "L", D32>;
def MULU64rm : BinRM<"MULU", "Q", D64>;
def MULU32idx1rm  : BinIdxRM<"MULU", "L", D32, idx1addr>;
def MULU32idx4rm  : BinIdxRM<"MULU", "L", D32, idx4addr>;
def MULU32idx4lrm : BinIdxRM<"MULU", "L", D32, idx4laddr>;
def MULU8mr  : BinMR<"MULU", "B", D8>;
def MULU16mr : BinMR<"MULU", "W", D16>;
def MULU32mr : BinMR<"MULU", "L", D32>;
def MULU64mr : BinMR<"MULU", "Q", D64>;
def MULU32ri : BedrockInst<(outs D32:$dst), (ins D32:$lhs, i32imm:$rhs),
                           "MULU.L\t$rhs, $dst"> {
  let Constraints = "$dst = $lhs";
}

class MAddRRR<string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst), (ins RC:$acc, RC:$lhs, RC:$rhs),
                  "MADD." # Suffix # "\t$lhs, $rhs, $dst"> {
  let Constraints = "$dst = $acc";
}

class MAddMRR<string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst), (ins RC:$acc, memri:$lhs, RC:$rhs),
                  "MADD." # Suffix # "\t$lhs, $rhs, $dst"> {
  let Constraints = "$dst = $acc";
  let mayLoad = 1;
}

class MAddPostMRR<string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst), (ins RC:$acc, mempost:$lhs, RC:$rhs),
                  "MADD." # Suffix # "\t$lhs, $rhs, $dst"> {
  let Constraints = "$dst = $acc";
  let mayLoad = 1;
}

def MADD8rrr  : MAddRRR<"B", D8>;
def MADD16rrr : MAddRRR<"W", D16>;
def MADD32rrr : MAddRRR<"L", D32>;
def MADD64rrr : MAddRRR<"Q", D64>;
def MADD8mrr  : MAddMRR<"B", D8>;
def MADD16mrr : MAddMRR<"W", D16>;
def MADD32mrr : MAddMRR<"L", D32>;
def MADD64mrr : MAddMRR<"Q", D64>;
def MADD8postmrr  : MAddPostMRR<"B", D8>;
def MADD16postmrr : MAddPostMRR<"W", D16>;
def MADD32postmrr : MAddPostMRR<"L", D32>;
def MADD64postmrr : MAddPostMRR<"Q", D64>;

def MULHS8rr  : BinRR<"MULHS", "B", GPR8, mulhs, 1>;
def MULHS16rr : BinRR<"MULHS", "W", GPR16, mulhs, 1>;
def MULHS32rr : BinRR<"MULHS", "L", D32, mulhs, 1>;
def MULHS64rr : BinRR<"MULHS", "Q", GPR64, mulhs, 1>;

def MULHU8rr  : BinRR<"MULHU", "B", GPR8, mulhu, 1>;
def MULHU16rr : BinRR<"MULHU", "W", GPR16, mulhu, 1>;
def MULHU32rr : BinRR<"MULHU", "L", D32, mulhu, 1>;
def MULHU64rr : BinRR<"MULHU", "Q", GPR64, mulhu, 1>;

def DIVS8rr  : BinRR<"DIVS", "B", GPR8, sdiv>;
def DIVS16rr : BinRR<"DIVS", "W", GPR16, sdiv>;
def DIVS32rr : BinRR<"DIVS", "L", D32, sdiv>;
def DIVS32ri : BinRI<"DIVS", "L", D32, i32imm, sdiv>;
def DIVS64rr : BinRR<"DIVS", "Q", GPR64, sdiv>;
def DIVS64ri : BedrockInst<(outs GPR64:$dst), (ins GPR64:$lhs, i64imm:$rhs),
                           "DIVS.Q\t$rhs, $dst"> {
  let Constraints = "$dst = $lhs";
  let Defs = [FLAGS];
}

def DIVU8rr  : BinRR<"DIVU", "B", GPR8, udiv>;
def DIVU16rr : BinRR<"DIVU", "W", GPR16, udiv>;
def DIVU32rr : BinRR<"DIVU", "L", D32, udiv>;
def DIVU32ri : BinRI<"DIVU", "L", D32, i32imm, udiv>;
def DIVU64rr : BinRR<"DIVU", "Q", GPR64, udiv>;

def MODS8rr  : BinRR<"MODS", "B", GPR8, srem>;
def MODS16rr : BinRR<"MODS", "W", GPR16, srem>;
def MODS32rr : BinRR<"MODS", "L", D32, srem>;
def MODS32ri : BinRI<"MODS", "L", D32, i32imm, srem>;
def MODS64rr : BinRR<"MODS", "Q", GPR64, srem>;

def MODU8rr  : BinRR<"MODU", "B", GPR8, urem>;
def MODU16rr : BinRR<"MODU", "W", GPR16, urem>;
def MODU32rr : BinRR<"MODU", "L", D32, urem>;
def MODU32ri : BinRI<"MODU", "L", D32, i32imm, urem>;
def MODU64rr : BinRR<"MODU", "Q", GPR64, urem>;

class DivModRR<string Mnemonic, string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$quot, RC:$rem), (ins RC:$srcquot, RC:$divisor),
                  Mnemonic # "." # Suffix # "\t$divisor, $quot, $rem"> {
  let Constraints = "$quot = $srcquot";
}

def DIVMODS8rr  : DivModRR<"DIVMODS", "B", D8>;
def DIVMODS16rr : DivModRR<"DIVMODS", "W", D16>;
def DIVMODS32rr : DivModRR<"DIVMODS", "L", D32>;
def DIVMODS64rr : DivModRR<"DIVMODS", "Q", D64>;
def DIVMODU8rr  : DivModRR<"DIVMODU", "B", D8>;
def DIVMODU16rr : DivModRR<"DIVMODU", "W", D16>;
def DIVMODU32rr : DivModRR<"DIVMODU", "L", D32>;
def DIVMODU64rr : DivModRR<"DIVMODU", "Q", D64>;

def LEAri : BedrockInst<(outs A64:$dst), (ins memri:$addr),
                        "LEA\t$addr, $dst">;
def LEA4 : BedrockInst<(outs PTR64:$dst), (ins lea4addr:$addr),
                       "LEA\t$addr, $dst">;
def LEA4L : BedrockInst<(outs PTR64:$dst), (ins lea4laddr:$addr),
                        "LEA\t$addr, $dst">;

class FBinRR<string Mnemonic, string Suffix, RegisterClass RC,
             SDPatternOperator OpNode, bit Commutable = 0>
    : BedrockInst<(outs RC:$dst), (ins RC:$lhs, RC:$rhs),
                  Mnemonic # "." # Suffix # "\t$rhs, $dst",
                  [(set RC:$dst, (OpNode RC:$lhs, RC:$rhs))]> {
  let Constraints = "$dst = $lhs";
  let isCommutable = Commutable;
}

class FBinRM<string Mnemonic, string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst), (ins RC:$lhs, memri:$addr),
                  Mnemonic # "." # Suffix # "\t$addr, $dst"> {
  let Constraints = "$dst = $lhs";
  let mayLoad = 1;
}

class FFmaRRR<string Mnemonic, string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst), (ins RC:$lhs, RC:$rhs),
                  Mnemonic # "." # Suffix # "\t$lhs, $rhs, $dst">;

class FFmaRMR<string Mnemonic, string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst), (ins RC:$lhs, memri:$rhs),
                  Mnemonic # "." # Suffix # "\t$lhs, $rhs, $dst"> {
  let mayLoad = 1;
}

class FFmaMRR<string Mnemonic, string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst), (ins memri:$lhs, RC:$rhs),
                  Mnemonic # "." # Suffix # "\t$lhs, $rhs, $dst"> {
  let mayLoad = 1;
}

class FUnaryRR<string Mnemonic, string Suffix, RegisterClass RC,
               SDPatternOperator OpNode>
    : BedrockInst<(outs RC:$dst), (ins RC:$src),
                  Mnemonic # "." # Suffix # "\t$src, $dst",
                  [(set RC:$dst, (OpNode RC:$src))]>;

class FUnaryRM<string Mnemonic, string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst), (ins memri:$addr),
                  Mnemonic # "." # Suffix # "\t$addr, $dst"> {
  let mayLoad = 1;
}

class FUnaryMR<string Mnemonic, string Suffix, RegisterClass RC>
    : BedrockInst<(outs), (ins RC:$src, memri:$addr),
                  Mnemonic # "." # Suffix # "\t$src, $addr"> {
  let mayStore = 1;
}

class FCopySignRR<string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$dst), (ins RC:$mag, RC:$sign),
                  "FCOPYSIGN." # Suffix # "\t$sign, $mag, $dst",
                  [(set RC:$dst, (fcopysign RC:$mag, RC:$sign))]>;

def FADD32rr : FBinRR<"FADD", "S", F32, fadd, 1>;
def FADD64rr : FBinRR<"FADD", "D", F64, fadd, 1>;
def FSUB32rr : FBinRR<"FSUB", "S", F32, fsub>;
def FSUB64rr : FBinRR<"FSUB", "D", F64, fsub>;
def FMUL32rr : FBinRR<"FMUL", "S", F32, fmul, 1>;
def FMUL64rr : FBinRR<"FMUL", "D", F64, fmul, 1>;
def FDIV32rr : FBinRR<"FDIV", "S", F32, fdiv>;
def FDIV64rr : FBinRR<"FDIV", "D", F64, fdiv>;
def FADD32rm : FBinRM<"FADD", "S", F32>;
def FADD64rm : FBinRM<"FADD", "D", F64>;
def FSUB32rm : FBinRM<"FSUB", "S", F32>;
def FSUB64rm : FBinRM<"FSUB", "D", F64>;
def FMUL32rm : FBinRM<"FMUL", "S", F32>;
def FMUL64rm : FBinRM<"FMUL", "D", F64>;
def FDIV32rm : FBinRM<"FDIV", "S", F32>;
def FDIV64rm : FBinRM<"FDIV", "D", F64>;
def FMADD32rrr : FFmaRRR<"FMADD", "S", F32>;
def FMADD64rrr : FFmaRRR<"FMADD", "D", F64>;
def FMADD32rmr : FFmaRMR<"FMADD", "S", F32>;
def FMADD64rmr : FFmaRMR<"FMADD", "D", F64>;
def FMADD32mrr : FFmaMRR<"FMADD", "S", F32>;
def FMADD64mrr : FFmaMRR<"FMADD", "D", F64>;
def FMSUB32rrr : FFmaRRR<"FMSUB", "S", F32>;
def FMSUB64rrr : FFmaRRR<"FMSUB", "D", F64>;
def FMSUB32rmr : FFmaRMR<"FMSUB", "S", F32>;
def FMSUB64rmr : FFmaRMR<"FMSUB", "D", F64>;
def FMSUB32mrr : FFmaMRR<"FMSUB", "S", F32>;
def FMSUB64mrr : FFmaMRR<"FMSUB", "D", F64>;
def FNMADD32rrr : FFmaRRR<"FNMADD", "S", F32>;
def FNMADD64rrr : FFmaRRR<"FNMADD", "D", F64>;
def FNMADD32rmr : FFmaRMR<"FNMADD", "S", F32>;
def FNMADD64rmr : FFmaRMR<"FNMADD", "D", F64>;
def FNMADD32mrr : FFmaMRR<"FNMADD", "S", F32>;
def FNMADD64mrr : FFmaMRR<"FNMADD", "D", F64>;
def FNMSUB32rrr : FFmaRRR<"FNMSUB", "S", F32>;
def FNMSUB64rrr : FFmaRRR<"FNMSUB", "D", F64>;
def FNMSUB32rmr : FFmaRMR<"FNMSUB", "S", F32>;
def FNMSUB64rmr : FFmaRMR<"FNMSUB", "D", F64>;
def FNMSUB32mrr : FFmaMRR<"FNMSUB", "S", F32>;
def FNMSUB64mrr : FFmaMRR<"FNMSUB", "D", F64>;
def FABS32rr : FUnaryRR<"FABS", "S", F32, fabs>;
def FABS64rr : FUnaryRR<"FABS", "D", F64, fabs>;
def FABS32rm : FUnaryRM<"FABS", "S", F32>;
def FABS64rm : FUnaryRM<"FABS", "D", F64>;
def FABS32mr : FUnaryMR<"FABS", "S", F32>;
def FABS64mr : FUnaryMR<"FABS", "D", F64>;
def FNEG32rr : FUnaryRR<"FNEG", "S", F32, fneg>;
def FNEG64rr : FUnaryRR<"FNEG", "D", F64, fneg>;
def FNEG32rm : FUnaryRM<"FNEG", "S", F32>;
def FNEG64rm : FUnaryRM<"FNEG", "D", F64>;
def FNEG32mr : FUnaryMR<"FNEG", "S", F32>;
def FNEG64mr : FUnaryMR<"FNEG", "D", F64>;
def FSQRT32rr : FUnaryRR<"FSQRT", "S", F32, fsqrt>;
def FSQRT64rr : FUnaryRR<"FSQRT", "D", F64, fsqrt>;
def FSQRT32rm : FUnaryRM<"FSQRT", "S", F32>;
def FSQRT64rm : FUnaryRM<"FSQRT", "D", F64>;
def FSQRT32mr : FUnaryMR<"FSQRT", "S", F32>;
def FSQRT64mr : FUnaryMR<"FSQRT", "D", F64>;
def FCOPYSIGN32rr : FCopySignRR<"S", F32>;
def FCOPYSIGN64rr : FCopySignRR<"D", F64>;

class FCmpRR<string Suffix, RegisterClass RC>
    : BedrockInst<(outs), (ins RC:$lhs, RC:$rhs),
                  "FCMP." # Suffix # "\t$rhs, $lhs",
                  [(BedrockFCmp RC:$lhs, RC:$rhs)]> {
  let Defs = [FLAGS];
}

class FCmpRM<string Suffix, RegisterClass RC>
    : BedrockInst<(outs), (ins RC:$lhs, memri:$addr),
                  "FCMP." # Suffix # "\t$addr, $lhs"> {
  let Defs = [FLAGS];
  let mayLoad = 1;
}

def FCMP32rr : FCmpRR<"S", F32>;
def FCMP64rr : FCmpRR<"D", F64>;
def FCMP32rm : FCmpRM<"S", F32>;
def FCMP64rm : FCmpRM<"D", F64>;

def FCLRr : BedrockInst<(outs F64:$dst), (ins), "FCLR\t$dst">;

let mayStore = 1 in
def FCLRm : BedrockInst<(outs), (ins memri:$addr), "FCLR\t$addr">;

class FIntToFP<string Mnemonic, RegisterClass DstRC, RegisterClass SrcRC,
               SDPatternOperator OpNode>
    : BedrockInst<(outs DstRC:$dst), (ins SrcRC:$src),
                  Mnemonic # "\t$src, $dst",
                  [(set DstRC:$dst, (OpNode SrcRC:$src))]>;

class FFPToInt<string Mnemonic, RegisterClass DstRC, RegisterClass SrcRC,
               SDPatternOperator OpNode>
    : BedrockInst<(outs DstRC:$dst), (ins SrcRC:$src),
                  Mnemonic # "\t$src, $dst",
                  [(set DstRC:$dst, (OpNode SrcRC:$src))]>;

def FCVTSI32toF32 : FIntToFP<"FCVT", F32, D32, sint_to_fp>;
def FCVTSI32toF64 : FIntToFP<"FCVT", F64, D32, sint_to_fp>;
def FCVTSI64toF32 : FIntToFP<"FCVT", F32, D64, sint_to_fp>;
def FCVTSI64toF64 : FIntToFP<"FCVT", F64, D64, sint_to_fp>;
def FCVTUI32toF32 : FIntToFP<"FCVTU", F32, D32, uint_to_fp>;
def FCVTUI32toF64 : FIntToFP<"FCVTU", F64, D32, uint_to_fp>;
def FCVTUI64toF32 : FIntToFP<"FCVTU", F32, D64, uint_to_fp>;
def FCVTUI64toF64 : FIntToFP<"FCVTU", F64, D64, uint_to_fp>;

def FCVTF32toSI32 : FFPToInt<"FCVT", D32, F32, fp_to_sint>;
def FCVTF64toSI32 : FFPToInt<"FCVT", D32, F64, fp_to_sint>;
def FCVTF32toSI64 : FFPToInt<"FCVT", D64, F32, fp_to_sint>;
def FCVTF64toSI64 : FFPToInt<"FCVT", D64, F64, fp_to_sint>;
def FCVTF32toUI32 : FFPToInt<"FCVTU", D32, F32, fp_to_uint>;
def FCVTF64toUI32 : FFPToInt<"FCVTU", D32, F64, fp_to_uint>;
def FCVTF32toUI64 : FFPToInt<"FCVTU", D64, F32, fp_to_uint>;
def FCVTF64toUI64 : FFPToInt<"FCVTU", D64, F64, fp_to_uint>;

def FCVT32to64 : BedrockInst<(outs F64:$dst), (ins F32:$src),
                             "FCVT\t$src, $dst",
                             [(set F64:$dst, (fpextend F32:$src))]>;
def FCVT64to32 : BedrockInst<(outs F32:$dst), (ins F64:$src),
                             "FCVT\t$src, $dst",
                             [(set F32:$dst, (fpround F64:$src))]>;
def FCVTU32to64 : BedrockInst<(outs F64:$dst), (ins F32:$src),
                              "FCVTU\t$src, $dst">;
def FCVTU64to32 : BedrockInst<(outs F32:$dst), (ins F64:$src),
                              "FCVTU\t$src, $dst">;

multiclass CmpRR<string Suffix, RegisterClass RC> {
  let Defs = [FLAGS] in
  def rr : BedrockInst<(outs), (ins RC:$lhs, RC:$rhs),
                       "CMP." # Suffix # "\t$rhs, $lhs",
                       [(BedrockCmp RC:$lhs, RC:$rhs)]>;
}

multiclass CmpRRDstD<string Suffix, RegisterClass RHSRC> {
  let Defs = [FLAGS] in
  def rr : BedrockInst<(outs), (ins D32:$lhs, RHSRC:$rhs),
                       "CMP." # Suffix # "\t$rhs, $lhs",
                       [(BedrockCmp D32:$lhs, RHSRC:$rhs)]>;
}

multiclass CmpRM<string Suffix, RegisterClass LHSRC> {
  let Defs = [FLAGS], mayLoad = 1 in
  def rm : BedrockInst<(outs), (ins LHSRC:$lhs, memri:$rhs),
                       "CMP." # Suffix # "\t$rhs, $lhs">;
}

class CmpIdxRM<string Suffix, RegisterClass LHSRC, Operand AddrOp>
    : BedrockInst<(outs), (ins LHSRC:$lhs, AddrOp:$rhs),
                  "CMP." # Suffix # "\t$rhs, $lhs"> {
  let Defs = [FLAGS];
  let mayLoad = 1;
}

multiclass CmpMR<string Suffix, RegisterClass RHSRC> {
  let Defs = [FLAGS], mayLoad = 1 in
  def mr : BedrockInst<(outs), (ins RHSRC:$rhs, memri:$lhs),
                       "CMP." # Suffix # "\t$rhs, $lhs">;
}

class CmpIdxMR<string Suffix, RegisterClass RHSRC, Operand AddrOp>
    : BedrockInst<(outs), (ins RHSRC:$rhs, AddrOp:$lhs),
                  "CMP." # Suffix # "\t$rhs, $lhs"> {
  let Defs = [FLAGS];
  let mayLoad = 1;
}

class FlagIdxMI<string Mnemonic, string Suffix, Operand ImmOp, Operand AddrOp>
    : BedrockInst<(outs), (ins ImmOp:$src, AddrOp:$dst),
                  Mnemonic # "." # Suffix # "\t$src, $dst"> {
  let Defs = [FLAGS];
  let mayLoad = 1;
}

defm CMP8  : CmpRR<"B", GPR8>;
defm CMP16 : CmpRR<"W", GPR16>;
defm CMP32 : CmpRRDstD<"L", GPR32>;
defm CMP64 : CmpRR<"Q", GPR64>;
defm CMP8  : CmpRM<"B", GPR8>;
defm CMP16 : CmpRM<"W", GPR16>;
defm CMP32 : CmpRM<"L", D32>;
defm CMP64 : CmpRM<"Q", GPR64>;
defm CMP8  : CmpMR<"B", GPR8>;
defm CMP16 : CmpMR<"W", GPR16>;
defm CMP32 : CmpMR<"L", GPR32>;
defm CMP64 : CmpMR<"Q", GPR64>;
def CMP8idx1rm   : CmpIdxRM<"B", GPR8, idx1addr>;
def CMP8idx4rm   : CmpIdxRM<"B", GPR8, idx4addr>;
def CMP8idx4lrm  : CmpIdxRM<"B", GPR8, idx4laddr>;
def CMP8idx1mr   : CmpIdxMR<"B", GPR8, idx1addr>;
def CMP8idx4mr   : CmpIdxMR<"B", GPR8, idx4addr>;
def CMP8idx4lmr  : CmpIdxMR<"B", GPR8, idx4laddr>;
def CMP16idx1rm  : CmpIdxRM<"W", GPR16, idx1addr>;
def CMP16idx4rm  : CmpIdxRM<"W", GPR16, idx4addr>;
def CMP16idx4lrm : CmpIdxRM<"W", GPR16, idx4laddr>;
def CMP16idx1mr  : CmpIdxMR<"W", GPR16, idx1addr>;
def CMP16idx4mr  : CmpIdxMR<"W", GPR16, idx4addr>;
def CMP16idx4lmr : CmpIdxMR<"W", GPR16, idx4laddr>;
def CMP32idx1rm  : CmpIdxRM<"L", D32, idx1addr>;
def CMP32idx4rm  : CmpIdxRM<"L", D32, idx4addr>;
def CMP32idx4lrm : CmpIdxRM<"L", D32, idx4laddr>;
def CMP32idx1mr  : CmpIdxMR<"L", GPR32, idx1addr>;
def CMP32idx4mr  : CmpIdxMR<"L", GPR32, idx4addr>;
def CMP32idx4lmr : CmpIdxMR<"L", GPR32, idx4laddr>;
def CMP64idx1rm  : CmpIdxRM<"Q", GPR64, idx1addr>;
def CMP64idx4rm  : CmpIdxRM<"Q", GPR64, idx4addr>;
def CMP64idx4lrm : CmpIdxRM<"Q", GPR64, idx4laddr>;
def CMP64idx1mr  : CmpIdxMR<"Q", GPR64, idx1addr>;
def CMP64idx4mr  : CmpIdxMR<"Q", GPR64, idx4addr>;
def CMP64idx4lmr : CmpIdxMR<"Q", GPR64, idx4laddr>;
def CMP8ri  : CmpRI<"B", GPR8, u6imm8>;
def CMP16ri : CmpRI<"W", GPR16, i16imm>;
def CMP32ri : CmpRI<"L", GPR32, i32imm>;
def CMP64ri : CmpRI<"Q", GPR64, u6imm64>;
def : Pat<(BedrockCmp GPR8:$lhs, u6nzimm8:$rhs),
          (CMP8ri GPR8:$lhs, u6nzimm8:$rhs)>;
def : Pat<(BedrockCmp GPR16:$lhs, i16nzimm:$rhs),
          (CMP16ri GPR16:$lhs, i16nzimm:$rhs)>;
def : Pat<(BedrockCmp GPR32:$lhs, i32nzimm:$rhs),
          (CMP32ri GPR32:$lhs, i32nzimm:$rhs)>;
def : Pat<(BedrockCmp GPR64:$lhs, u6nzimm64:$rhs),
          (CMP64ri GPR64:$lhs, u6nzimm64:$rhs)>;
def CMP8mi  : FlagMI<"CMP", "B", u6imm8>;
def CMP16mi : FlagMI<"CMP", "W", u6imm16>;
def CMP32mi : FlagMI<"CMP", "L", u6imm32>;
def CMP64mi : FlagMI<"CMP", "Q", u6imm64>;
def CMP8idx1mi   : FlagIdxMI<"CMP", "B", u6imm8, idx1addr>;
def CMP8idx4mi   : FlagIdxMI<"CMP", "B", u6imm8, idx4addr>;
def CMP8idx4lmi  : FlagIdxMI<"CMP", "B", u6imm8, idx4laddr>;
def CMP16idx1mi  : FlagIdxMI<"CMP", "W", u6imm16, idx1addr>;
def CMP16idx4mi  : FlagIdxMI<"CMP", "W", u6imm16, idx4addr>;
def CMP16idx4lmi : FlagIdxMI<"CMP", "W", u6imm16, idx4laddr>;
def CMP32idx1mi  : FlagIdxMI<"CMP", "L", u6imm32, idx1addr>;
def CMP32idx4mi  : FlagIdxMI<"CMP", "L", u6imm32, idx4addr>;
def CMP32idx4lmi : FlagIdxMI<"CMP", "L", u6imm32, idx4laddr>;
def CMP64idx1mi  : FlagIdxMI<"CMP", "Q", u6imm64, idx1addr>;
def CMP64idx4mi  : FlagIdxMI<"CMP", "Q", u6imm64, idx4addr>;
def CMP64idx4lmi : FlagIdxMI<"CMP", "Q", u6imm64, idx4laddr>;
def REPGTCMP32postrm : RepCmpPostRM<"REPGT", "L", GPR32>;

multiclass TestRR<string Suffix, RegisterClass RC> {
  let Defs = [FLAGS] in
  def rr : BedrockInst<(outs), (ins RC:$lhs, RC:$rhs),
                       "TEST." # Suffix # "\t$rhs, $lhs">;
}

multiclass TestRM<string Suffix, RegisterClass LHSRC> {
  let Defs = [FLAGS], mayLoad = 1 in
  def rm : BedrockInst<(outs), (ins LHSRC:$lhs, memri:$rhs),
                       "TEST." # Suffix # "\t$rhs, $lhs">;
}

class TestIdxRM<string Suffix, RegisterClass LHSRC, Operand AddrOp>
    : BedrockInst<(outs), (ins LHSRC:$lhs, AddrOp:$rhs),
                  "TEST." # Suffix # "\t$rhs, $lhs"> {
  let Defs = [FLAGS];
  let mayLoad = 1;
}

multiclass TestMR<string Suffix, RegisterClass RHSRC> {
  let Defs = [FLAGS], mayLoad = 1 in
  def mr : BedrockInst<(outs), (ins RHSRC:$rhs, memri:$lhs),
                       "TEST." # Suffix # "\t$rhs, $lhs">;
}

class TestIdxMR<string Suffix, RegisterClass RHSRC, Operand AddrOp>
    : BedrockInst<(outs), (ins RHSRC:$rhs, AddrOp:$lhs),
                  "TEST." # Suffix # "\t$rhs, $lhs"> {
  let Defs = [FLAGS];
  let mayLoad = 1;
}

defm TEST8  : TestRR<"B", GPR8>;
defm TEST16 : TestRR<"W", GPR16>;
defm TEST32 : TestRR<"L", D32>;
defm TEST64 : TestRR<"Q", GPR64>;
defm TEST8  : TestRM<"B", GPR8>;
defm TEST16 : TestRM<"W", GPR16>;
defm TEST32 : TestRM<"L", D32>;
defm TEST64 : TestRM<"Q", GPR64>;
defm TEST8  : TestMR<"B", GPR8>;
defm TEST16 : TestMR<"W", GPR16>;
defm TEST32 : TestMR<"L", D32>;
defm TEST64 : TestMR<"Q", GPR64>;
def TEST8idx1rm   : TestIdxRM<"B", GPR8, idx1addr>;
def TEST8idx4rm   : TestIdxRM<"B", GPR8, idx4addr>;
def TEST8idx4lrm  : TestIdxRM<"B", GPR8, idx4laddr>;
def TEST8idx1mr   : TestIdxMR<"B", GPR8, idx1addr>;
def TEST8idx4mr   : TestIdxMR<"B", GPR8, idx4addr>;
def TEST8idx4lmr  : TestIdxMR<"B", GPR8, idx4laddr>;
def TEST16idx1rm  : TestIdxRM<"W", GPR16, idx1addr>;
def TEST16idx4rm  : TestIdxRM<"W", GPR16, idx4addr>;
def TEST16idx4lrm : TestIdxRM<"W", GPR16, idx4laddr>;
def TEST16idx1mr  : TestIdxMR<"W", GPR16, idx1addr>;
def TEST16idx4mr  : TestIdxMR<"W", GPR16, idx4addr>;
def TEST16idx4lmr : TestIdxMR<"W", GPR16, idx4laddr>;
def TEST32idx1rm  : TestIdxRM<"L", D32, idx1addr>;
def TEST32idx4rm  : TestIdxRM<"L", D32, idx4addr>;
def TEST32idx4lrm : TestIdxRM<"L", D32, idx4laddr>;
def TEST32idx1mr  : TestIdxMR<"L", D32, idx1addr>;
def TEST32idx4mr  : TestIdxMR<"L", D32, idx4addr>;
def TEST32idx4lmr : TestIdxMR<"L", D32, idx4laddr>;
def TEST64idx1rm  : TestIdxRM<"Q", GPR64, idx1addr>;
def TEST64idx4rm  : TestIdxRM<"Q", GPR64, idx4addr>;
def TEST64idx4lrm : TestIdxRM<"Q", GPR64, idx4laddr>;
def TEST64idx1mr  : TestIdxMR<"Q", GPR64, idx1addr>;
def TEST64idx4mr  : TestIdxMR<"Q", GPR64, idx4addr>;
def TEST64idx4lmr : TestIdxMR<"Q", GPR64, idx4laddr>;
def TEST8ri  : TestRI<"B", GPR8, u6imm8>;
def TEST16ri : TestRI<"W", GPR16, i16imm>;
def TEST32ri : TestRI<"L", D32, i32imm>;
def TEST64ri : TestRI<"Q", GPR64, u6imm64>;
def TEST8mi  : FlagMI<"TEST", "B", u6imm8>;
def TEST16mi : FlagMI<"TEST", "W", u6imm16>;
def TEST32mi : FlagMI<"TEST", "L", u6imm32>;
def TEST64mi : FlagMI<"TEST", "Q", u6imm64>;
def TEST8idx1mi   : FlagIdxMI<"TEST", "B", u6imm8, idx1addr>;
def TEST8idx4mi   : FlagIdxMI<"TEST", "B", u6imm8, idx4addr>;
def TEST8idx4lmi  : FlagIdxMI<"TEST", "B", u6imm8, idx4laddr>;
def TEST16idx1mi  : FlagIdxMI<"TEST", "W", u6imm16, idx1addr>;
def TEST16idx4mi  : FlagIdxMI<"TEST", "W", u6imm16, idx4addr>;
def TEST16idx4lmi : FlagIdxMI<"TEST", "W", u6imm16, idx4laddr>;
def TEST32idx1mi  : FlagIdxMI<"TEST", "L", u6imm32, idx1addr>;
def TEST32idx4mi  : FlagIdxMI<"TEST", "L", u6imm32, idx4addr>;
def TEST32idx4lmi : FlagIdxMI<"TEST", "L", u6imm32, idx4laddr>;
def TEST64idx1mi  : FlagIdxMI<"TEST", "Q", u6imm64, idx1addr>;
def TEST64idx4mi  : FlagIdxMI<"TEST", "Q", u6imm64, idx4addr>;
def TEST64idx4lmi : FlagIdxMI<"TEST", "Q", u6imm64, idx4laddr>;

class SelectCC<string Suffix, RegisterClass RC>
    : BedrockPseudo<(outs RC:$dst), (ins RC:$src, RC:$src2, bedrockcc:$cond),
                    "# SELECT." # Suffix # " $src, $src2, $cond",
                    [(set RC:$dst,
                      (BedrockSelectCC RC:$src, RC:$src2, imm:$cond))]>;

let Uses = [FLAGS], usesCustomInserter = 1, isSelect = 1 in {
def SELECT8  : SelectCC<"B", GPR8>;
def SELECT16 : SelectCC<"W", GPR16>;
def SELECT32 : SelectCC<"L", GPR32>;
def SELECT64 : SelectCC<"Q", GPR64>;
def FSELECT32 : SelectCC<"S", F32>;
def FSELECT64 : SelectCC<"D", F64>;
}

let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def JMP : BedrockInst<(outs), (ins brtarget:$target),
                      "JMP.W\t$target@WORD_PCREL16",
                      [(br bb:$target)]>;

let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def JMPWpcrel : BedrockInst<(outs), (ins calltarget:$target),
                            "JMP.W\t$target@WORD_PCREL16">;

let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def JMPLpcrel : BedrockInst<(outs), (ins calltarget:$target),
                            "JMP.L\t$target@WORD_PCREL32">;

let isBranch = 1, isTerminator = 1, Uses = [FLAGS] in
def JCC : BedrockInst<(outs), (ins brtarget:$target, bedrockcc:$cond),
                      "J$cond.W\t$target@WORD_PCREL16",
                      [(BedrockBrCC bb:$target, imm:$cond)]>;

class DJccR<string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$counter),
                  (ins RC:$counter_in, brtarget:$target, bedrockcc:$cond),
                  "DJ$cond." # Suffix # "\t$counter, $target@WORD_PCREL16"> {
  let Constraints = "$counter = $counter_in";
}

class IJccR<string Suffix, RegisterClass RC>
    : BedrockInst<(outs RC:$index),
                  (ins RC:$index_in, RC:$bound, brtarget:$target,
                       bedrockcc:$cond),
                  "IJ$cond." # Suffix #
                      "\t$index, $bound, $target@WORD_PCREL16"> {
  let Constraints = "$index = $index_in";
}

let isBranch = 1, isTerminator = 1 in {
def DJCC8r  : DJccR<"B", D8>;
def DJCC16r : DJccR<"W", D16>;
def DJCC32r : DJccR<"L", D32>;
def DJCC64r : DJccR<"Q", D64>;

def IJCC32r : IJccR<"L", D32>;
def IJCC64r : IJccR<"Q", D64>;
}

let isCall = 1, Defs = [SP], Uses = [SP] in {
def CALLpcrel16 : BedrockInst<(outs), (ins calltarget:$target),
                              "CALL\t$target@PCREL16">;
def CALLpcrel : BedrockInst<(outs), (ins calltarget:$target),
                            "CALL\t$target@PCREL32">;
def CALLind : BedrockInst<(outs), (ins PTR64:$target), "CALL\t$target",
                          [(BedrockCall PTR64:$target)]>;
}

let isReturn = 1, isTerminator = 1, isBarrier = 1, Uses = [SP] in
def RET : BedrockInst<(outs), (ins), "RET", [(BedrockRet)]>;

def : Pat<(i64 (BedrockWrapper tglobaladdr:$dst)),
          (MOV64abs tglobaladdr:$dst)>;
def : Pat<(i64 (BedrockWrapper texternalsym:$dst)),
          (MOV64abs texternalsym:$dst)>;
def : Pat<(i64 (BedrockWrapper tblockaddress:$dst)),
          (MOV64abs tblockaddress:$dst)>;
def : Pat<(i64 (BedrockWrapper tconstpool:$dst)),
          (MOV64abs tconstpool:$dst)>;

def : Pat<(BedrockCall16 (i64 tglobaladdr:$dst)), (CALLpcrel16 tglobaladdr:$dst)>;
def : Pat<(BedrockCall (i64 tglobaladdr:$dst)), (CALLpcrel tglobaladdr:$dst)>;
def : Pat<(BedrockCall (i64 texternalsym:$dst)),
          (CALLpcrel texternalsym:$dst)>;
