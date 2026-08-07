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

def HasFPTRANSA : Predicate<"Subtarget->hasFPTRANSA()">;

def SDT_BedrockCallSeqStart : SDCallSeqStart<[SDTCisVT<0, i64>,
                                               SDTCisVT<1, i64>]>;
def SDT_BedrockCallSeqEnd : SDCallSeqEnd<[SDTCisVT<0, i64>,
                                           SDTCisVT<1, i64>]>;
def SDT_BedrockCall : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;
def SDT_BedrockMemset : SDTypeProfile<2, 3, [SDTCisVT<0, i64>,
                                                 SDTCisVT<1, i64>,
                                                 SDTCisVT<2, i64>,
                                                 SDTCisVT<3, i64>,
                                                 SDTCisVT<4, i64>]>;
def SDT_BedrockCmp : SDTypeProfile<0, 2, [SDTCisSameAs<0, 1>]>;
def SDT_BedrockFCmp : SDTypeProfile<0, 2, [SDTCisSameAs<0, 1>,
                                          SDTCisFP<0>]>;
def SDT_BedrockTest : SDTypeProfile<0, 1, [SDTCisInt<0>]>;
def SDT_BedrockBrCC : SDTypeProfile<0, 2, [SDTCisVT<0, OtherVT>,
                                           SDTCisVT<1, i32>]>;
def SDT_BedrockSetCC : SDTypeProfile<1, 1, [SDTCisVT<0, i64>,
                                            SDTCisVT<1, i32>]>;
def SDT_BedrockSelectCC : SDTypeProfile<1, 5, [SDTCisSameAs<1, 2>,
                                               SDTCisSameAs<0, 3>,
                                               SDTCisSameAs<0, 4>,
                                               SDTCisVT<5, i32>]>;
def SDT_BedrockFPSelectCC : SDTypeProfile<1, 5, [SDTCisSameAs<1, 2>,
                                                 SDTCisSameAs<0, 3>,
                                                 SDTCisSameAs<0, 4>,
                                                 SDTCisVT<5, i32>,
                                                 SDTCisFP<0>,
                                                 SDTCisFP<1>]>;
def SDT_BedrockFPSetCC : SDTypeProfile<1, 3, [SDTCisVT<0, i64>,
                                              SDTCisSameAs<1, 2>,
                                              SDTCisVT<3, i32>,
                                              SDTCisFP<1>]>;
def SDT_BedrockUnaryInt : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                               SDTCisInt<0>]>;
def SDT_BedrockBinaryInt : SDTypeProfile<1, 2, [SDTCisSameAs<0, 1>,
                                                SDTCisSameAs<0, 2>,
                                                SDTCisInt<0>]>;
def SDT_BedrockAddress : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>]>;
def SDT_BedrockTLSDesc : SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>;

def BedrockCall : SDNode<"BedrockISD::CALL", SDT_BedrockCall,
                         [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                          SDNPVariadic]>;
def BedrockCallAddress : SDNode<"BedrockISD::CALL_ADDRESS",
                                SDT_BedrockAddress>;
def BedrockTLSAddress : SDNode<"BedrockISD::TLS_ADDRESS",
                               SDT_BedrockAddress>;
def BedrockTLSDescCall
    : SDNode<"BedrockISD::TLSDESC_CALL", SDT_BedrockTLSDesc,
             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;
def BedrockTailCallCandidate
    : SDNode<"BedrockISD::TAIL_CALL_CANDIDATE", SDT_BedrockCall,
             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;
def BedrockMemset : SDNode<"BedrockISD::MEMSET", SDT_BedrockMemset,
                           [SDNPHasChain, SDNPMayStore]>;
def BedrockRetFlag : SDNode<"BedrockISD::RET_FLAG", SDTNone,
                            [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def BedrockCmp : SDNode<"BedrockISD::CMP", SDT_BedrockCmp, [SDNPOutGlue]>;
def BedrockFCmp : SDNode<"BedrockISD::FCMP", SDT_BedrockFCmp,
                         [SDNPOutGlue]>;
def BedrockTest : SDNode<"BedrockISD::TEST", SDT_BedrockTest, [SDNPOutGlue]>;
def BedrockBrCC : SDNode<"BedrockISD::BR_CC", SDT_BedrockBrCC,
                         [SDNPHasChain, SDNPInGlue]>;
def BedrockSetCC : SDNode<"BedrockISD::SET_CC", SDT_BedrockSetCC,
                          [SDNPInGlue]>;
def BedrockSelectCC : SDNode<"BedrockISD::SELECT_CC",
                             SDT_BedrockSelectCC>;
def BedrockFPSelectCC : SDNode<"BedrockISD::FP_SELECT_CC",
                               SDT_BedrockFPSelectCC>;
def BedrockFPSetCC : SDNode<"BedrockISD::FP_SET_CC", SDT_BedrockFPSetCC>;
def BedrockSMax : SDNode<"BedrockISD::SMAX", SDT_BedrockBinaryInt>;
def BedrockSMin : SDNode<"BedrockISD::SMIN", SDT_BedrockBinaryInt>;
def BedrockUMax : SDNode<"BedrockISD::UMAX", SDT_BedrockBinaryInt>;
def BedrockUMin : SDNode<"BedrockISD::UMIN", SDT_BedrockBinaryInt>;
def BedrockSMaxZero : SDNode<"BedrockISD::SMAX_ZERO", SDT_BedrockUnaryInt>;
def BedrockSMinZero : SDNode<"BedrockISD::SMIN_ZERO", SDT_BedrockUnaryInt>;
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

// Marks a counted loop whose REPG counter is intentionally reused as a
// body-local scratch register.  The late assembly printer consumes this marker
// while forming the repeat group; it never reaches the emitted instruction
// stream.
let isPseudo = 1, isCodeGenOnly = 1, hasNoSchedulingInfo = 1 in
def REPG_SCRATCH : BedrockInst<(outs), (ins GPR64:$counter), "">;

class BedrockPseudo<dag outs, dag ins, list<dag> pattern = []>
    : BedrockInst<outs, ins, "", pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
  let hasNoSchedulingInfo = 1;
}

let Constraints = "$dst_out = $dst, $count_out = $count",
    mayStore = 1, hasSideEffects = 1 in
def REP_MEMSETB : BedrockPseudo<
    (outs GPR64:$dst_out, GPR64:$count_out),
    (ins GPR64:$dst, GPR64:$value, GPR64:$count)>;

class BedrockShort<bits<14> payload, dag outs, dag ins, string asm,
                   list<dag> pattern = []>
    : BedrockInst<outs, ins, asm, pattern> {
  let Inst{15} = 1;
  let Inst{14} = 0;
  let Inst{13 -0} = payload;
}

class BedrockExtraShort<bits<7> payload, dag outs, dag ins, string asm,
                        list<dag> pattern = []>
    : BedrockInst<outs, ins, asm, pattern> {
  let Size = 1;
  let Inst{7} = 0;
  let Inst{6 -0} = payload;
}

class BedrockImmAsmOperand<string name, string predicate> : AsmOperandClass {
  let Name = name;
  let RenderMethod = "addImmOperands";
  let PredicateMethod = predicate;
}

def UImm8AsmOperand : BedrockImmAsmOperand<"UImm8", "isUImm<8>">;
def UImm3AsmOperand : BedrockImmAsmOperand<"UImm3", "isUImm<3>">;
def SImm8AsmOperand : BedrockImmAsmOperand<"SImm8", "isSImm<8>">;

def uimm3 : Operand<i32> { let ParserMatchClass = UImm3AsmOperand; }
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

  let Inst{15} = 1;
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

  let Inst{15} = 1;
  let Inst{14} = 0;
  let Inst{13 -8} = opcode;
  let Inst{7 -4} = rs;
  let Inst{3 -0} = rd;
}

class BedrockRegLow<bits<10> opcode, string asm>
    : BedrockInst<(outs), (ins GPR64:$r), !strconcat(asm, "\t$r")> {
  bits<4> r;

  let Inst{15} = 1;
  let Inst{14} = 0;
  let Inst{13 -4} = opcode;
  let Inst{3 -0} = r;
}

class BedrockRegLowOut<bits<10> opcode, string asm>
    : BedrockInst<(outs GPR64:$r), (ins), !strconcat(asm, "\t$r")> {
  bits<4> r;

  let Inst{15} = 1;
  let Inst{14} = 0;
  let Inst{13 -4} = opcode;
  let Inst{3 -0} = r;
}

class BedrockRegSP<bits<10> opcode, string asm>
    : BedrockInst<(outs), (ins GPR64:$r), asm> {
  bits<4> r;

  let Inst{15} = 1;
  let Inst{14} = 0;
  let Inst{13 -4} = opcode;
  let Inst{3 -0} = r;
}

class BedrockSPRegOut<bits<10> opcode, string asm>
    : BedrockInst<(outs GPR64:$r), (ins), asm> {
  bits<4> r;

  let Inst{15} = 1;
  let Inst{14} = 0;
  let Inst{13 -4} = opcode;
  let Inst{3 -0} = r;
}

class BedrockExtraShortRegIn<bits<3> opcode, string asm>
    : BedrockInst<(outs), (ins GPR64:$r), asm> {
  bits<4> r;

  let Size = 1;
  let Inst{7} = 0;
  let Inst{6 -4} = opcode;
  let Inst{3 -0} = r;
}

class BedrockExtraShortRegOut<bits<3> opcode, string asm>
    : BedrockInst<(outs GPR64:$r), (ins), asm> {
  bits<4> r;

  let Size = 1;
  let Inst{7} = 0;
  let Inst{6 -4} = opcode;
  let Inst{3 -0} = r;
}

class BedrockExtraShortImm3<bits<4> opcode, string asm>
    : BedrockInst<(outs), (ins uimm3:$i), !strconcat(asm, "\t$i")> {
  bits<3> i;

  let Size = 1;
  let Inst{7} = 0;
  let Inst{6 -3} = opcode;
  let Inst{2 -0} = i;
}

class BedrockSet<bits<4> cc, string asm>
    : BedrockInst<(outs), (ins GPR64:$r), !strconcat(asm, "\t$r")> {
  bits<4> r;

  let Inst{15} = 1;
  let Inst{14} = 0;
  let Inst{13 -8} = 0b100001;
  let Inst{7 -4} = r;
  let Inst{3 -0} = cc;
}

class BedrockImm8<Operand immop, bits<6> opcode, string asm>
    : BedrockInst<(outs), (ins immop:$i), asm> {
  bits<8> i;

  let Inst{15} = 1;
  let Inst{14} = 0;
  let Inst{13 -8} = opcode;
  let Inst{7 -0} = i;
}

class BedrockImm3<bits<11> prefix, string asm>
    : BedrockInst<(outs), (ins uimm3:$i), !strconcat(asm, "\t$i")> {
  bits<3> i;

  let Inst{15} = 1;
  let Inst{14} = 0;
  let Inst{13 -3} = prefix;
  let Inst{2 -0} = i;
}

class BedrockJcc<bits<4> cc, string suffix>
    : BedrockInst<(outs), (ins simm8:$i), !strconcat("j", suffix, "\t$i")> {
  bits<8> i;

  let Inst{15} = 1;
  let Inst{14} = 0;
  let Inst{13 -12} = 0b11;
  let Inst{11 -8} = cc;
  let Inst{7 -0} = i;
}

class BedrockSetcc<bits<4> cc, string suffix>
    : BedrockSet<cc, !strconcat("set", suffix)>;

def NOP : BedrockExtraShort<0x01, (outs), (ins), "nop">;
let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RET : BedrockExtraShort<0x02, (outs), (ins), "ret",
                            [(BedrockRetFlag)]>;
def LRET : BedrockExtraShort<0x03, (outs), (ins), "lret">;
def SYSCALL : BedrockExtraShort<0x05, (outs), (ins), "syscall">;
def SYSRET : BedrockExtraShort<0x06, (outs), (ins), "sysret">;
def ERET : BedrockExtraShort<0x04, (outs), (ins), "eret">;
def BKPT : BedrockExtraShort<0x07, (outs), (ins), "bkpt">;
def WAIT : BedrockExtraShort<0x08, (outs), (ins), "wait">;
def YIELD : BedrockExtraShort<0x09, (outs), (ins), "yield">;
def HALT : BedrockShort<0x2049, (outs), (ins), "halt">;
def ILLEGAL : BedrockExtraShort<0x00, (outs), (ins), "illegal">;
def RFENCE : BedrockExtraShort<0x0a, (outs), (ins), "rfence">;
def WFENCE : BedrockExtraShort<0x0b, (outs), (ins), "wfence">;
def AFENCE : BedrockExtraShort<0x0c, (outs), (ins), "afence">;
def : Pat<(atomic_fence timm, timm), (AFENCE)>;
def RESET : BedrockShort<0x204e, (outs), (ins), "reset">;

let hasSideEffects = 1 in {
def BEDROCK_CPUID : BedrockPseudo<(outs GPR64:$dst), (ins GPR64:$src),
                                  [(set i64:$dst,
                                        (int_bedrock_cpuid i64:$src))]> {
  let Constraints = "$dst = $src";
}
def BEDROCK_RDSTATUS : BedrockPseudo<(outs GPR64:$dst), (ins),
                                     [(set i64:$dst,
                                           (int_bedrock_read_status))]>;
def BEDROCK_RDPMC : BedrockPseudo<(outs GPR64:$dst), (ins i32imm:$selector),
                                  [(set i64:$dst,
                                        (int_bedrock_rdpmc timm:$selector))]>;
def BEDROCK_TRACE : BedrockPseudo<(outs), (ins i32imm:$marker),
                                  [(int_bedrock_trace timm:$marker)]>;
def BEDROCK_RDFSTATUS : BedrockPseudo<(outs GPR64:$dst), (ins),
                                      [(set i64:$dst,
                                            (int_bedrock_read_fstatus))]>;
def BEDROCK_WRFSTATUS : BedrockPseudo<(outs), (ins GPR64:$src),
                                      [(int_bedrock_write_fstatus
                                            i64:$src)]>;
def BEDROCK_RDFFLAGS : BedrockPseudo<(outs GPR64:$dst), (ins),
                                     [(set i64:$dst,
                                           (int_bedrock_read_fflags))]>;
def BEDROCK_WRFFLAGS : BedrockPseudo<(outs), (ins GPR64:$src),
                                     [(int_bedrock_write_fflags i64:$src)]>;
}

class BedrockClmulPseudo<Intrinsic intrinsic>
    : BedrockPseudo<(outs GPR64:$dst),
                    (ins GPR64:$lhs, GPR64:$rhs),
                    [(set i64:$dst, (intrinsic i64:$lhs, i64:$rhs))]> {
  let Constraints = "$dst = $lhs";
}
def BEDROCK_CLMUL_B : BedrockClmulPseudo<int_bedrock_clmul_u8>;
def BEDROCK_CLMUL_W : BedrockClmulPseudo<int_bedrock_clmul_u16>;
def BEDROCK_CLMUL_L : BedrockClmulPseudo<int_bedrock_clmul_u32>;
def BEDROCK_CLMUL_Q : BedrockClmulPseudo<int_bedrock_clmul_u64>;

class BedrockMovNTPseudo<Intrinsic intrinsic>
    : BedrockPseudo<(outs), (ins GPR64:$src, GPR64:$address),
                    [(intrinsic GPR64:$address, i64:$src)]> {
  let mayStore = 1;
}
def BEDROCK_MOVNT_B : BedrockMovNTPseudo<int_bedrock_nontemporal_store_u8>;
def BEDROCK_MOVNT_W : BedrockMovNTPseudo<int_bedrock_nontemporal_store_u16>;
def BEDROCK_MOVNT_L : BedrockMovNTPseudo<int_bedrock_nontemporal_store_u32>;
def BEDROCK_MOVNT_Q : BedrockMovNTPseudo<int_bedrock_nontemporal_store_u64>;

def BEDROCK_FCLASS_S
    : BedrockPseudo<(outs GPR64:$dst), (ins FPR64:$src),
                    [(set i64:$dst, (int_bedrock_fclass_f32 f32:$src))]>;
def BEDROCK_FCLASS_D
    : BedrockPseudo<(outs GPR64:$dst), (ins FPR64:$src),
                    [(set i64:$dst, (int_bedrock_fclass_f64 f64:$src))]>;

// The ordering is carried by the MachineMemOperand and encoded by the
// assembly printer. FETCH* overwrites its source with the old memory value;
// CMPXCHG likewise overwrites the expected-value register.
class BedrockAtomicFetchPseudo<PatFrag Op, ValueType VT>
    : BedrockPseudo<(outs GPR64:$dst),
                    (ins GPR64:$address, GPR64:$value),
                    [(set VT:$dst, (Op i64:$address, VT:$value))]> {
  let Constraints = "$dst = $value";
  let mayLoad = 1;
  let mayStore = 1;
}

class BedrockAtomicCmpXchgPseudo<PatFrag Op, ValueType VT>
    : BedrockPseudo<(outs GPR64:$dst),
                    (ins GPR64:$address, GPR64:$expected,
                         GPR64:$desired),
                    [(set VT:$dst,
                          (Op i64:$address, VT:$expected, VT:$desired))]> {
  let Constraints = "$dst = $expected";
  let mayLoad = 1;
  let mayStore = 1;
  let Defs = [FLAGS];
}

multiclass BedrockAtomicFetchPseudos<PatFrag OpB, PatFrag OpW,
                                     PatFrag OpL, PatFrag OpQ> {
  def B : BedrockAtomicFetchPseudo<OpB, i32>;
  def W : BedrockAtomicFetchPseudo<OpW, i32>;
  def L : BedrockAtomicFetchPseudo<OpL, i32>;
  def Q : BedrockAtomicFetchPseudo<OpQ, i64>;
}

defm ATOMIC_FETCHADD : BedrockAtomicFetchPseudos<
    atomic_load_add_i8, atomic_load_add_i16,
    atomic_load_add_i32, atomic_load_add_i64>;
defm ATOMIC_FETCHSUB : BedrockAtomicFetchPseudos<
    atomic_load_sub_i8, atomic_load_sub_i16,
    atomic_load_sub_i32, atomic_load_sub_i64>;
defm ATOMIC_FETCHAND : BedrockAtomicFetchPseudos<
    atomic_load_and_i8, atomic_load_and_i16,
    atomic_load_and_i32, atomic_load_and_i64>;
defm ATOMIC_FETCHOR : BedrockAtomicFetchPseudos<
    atomic_load_or_i8, atomic_load_or_i16,
    atomic_load_or_i32, atomic_load_or_i64>;
defm ATOMIC_FETCHXOR : BedrockAtomicFetchPseudos<
    atomic_load_xor_i8, atomic_load_xor_i16,
    atomic_load_xor_i32, atomic_load_xor_i64>;

def ATOMIC_CMPXCHG_B : BedrockAtomicCmpXchgPseudo<atomic_cmp_swap_i8, i32>;
def ATOMIC_CMPXCHG_W : BedrockAtomicCmpXchgPseudo<atomic_cmp_swap_i16, i32>;
def ATOMIC_CMPXCHG_L : BedrockAtomicCmpXchgPseudo<atomic_cmp_swap_i32, i32>;
def ATOMIC_CMPXCHG_Q : BedrockAtomicCmpXchgPseudo<atomic_cmp_swap_i64, i64>;

def : Pat<(int_bedrock_breakpoint), (BKPT)>;
def : Pat<(int_bedrock_yield), (YIELD)>;
def : Pat<(int_bedrock_wait), (WAIT)>;
def : Pat<(int_bedrock_read_fence), (RFENCE)>;
def : Pat<(int_bedrock_write_fence), (WFENCE)>;
def : Pat<(int_bedrock_address_fence), (AFENCE)>;

let hasSideEffects = 1 in {
def BEDROCK_WRSTATUS : BedrockPseudo<(outs), (ins GPR64:$src),
                                     [(int_bedrock_write_status i64:$src)]>;
def BEDROCK_RDCR : BedrockPseudo<(outs GPR64:$dst), (ins i32imm:$selector),
                                 [(set i64:$dst,
                                       (int_bedrock_read_control_register
                                            timm:$selector))]>;
def BEDROCK_WRCR : BedrockPseudo<(outs),
                                 (ins GPR64:$src, i32imm:$selector),
                                 [(int_bedrock_write_control_register
                                      timm:$selector, i64:$src)]>;
def BEDROCK_RDSEG : BedrockPseudo<(outs GPR64:$dst), (ins i32imm:$selector),
                                  [(set i64:$dst,
                                        (int_bedrock_read_segment_register
                                             timm:$selector))]>;
def BEDROCK_RDSEG_CS : BedrockPseudo<(outs GPR64:$dst), (ins),
                                     [(set i64:$dst,
                                           (int_bedrock_read_code_segment))]>;
def BEDROCK_WRSEG : BedrockPseudo<(outs),
                                  (ins GPR64:$src, i32imm:$selector),
                                  [(int_bedrock_write_segment_register
                                       timm:$selector, i64:$src)]>;

class BedrockCacheRangePseudo<Intrinsic intrinsic>
    : BedrockPseudo<(outs), (ins GPR64:$address, GPR64:$length),
                    [(intrinsic GPR64:$address, i64:$length)]> {
  let mayLoad = 1;
  let mayStore = 1;
}
def BEDROCK_FLSHDCACHE :
    BedrockCacheRangePseudo<int_bedrock_flush_dcache>;
def BEDROCK_INVDCACHE :
    BedrockCacheRangePseudo<int_bedrock_invalidate_dcache>;
def BEDROCK_INVICACHE :
    BedrockCacheRangePseudo<int_bedrock_invalidate_icache>;
def BEDROCK_WRBKDCACHE :
    BedrockCacheRangePseudo<int_bedrock_writeback_dcache>;
def BEDROCK_SYNCCACHE :
    BedrockCacheRangePseudo<int_bedrock_sync_cache>;

def BEDROCK_INVTLB : BedrockPseudo<(outs), (ins),
                                   [(int_bedrock_invalidate_tlb)]>;
def BEDROCK_INVPAGE :
    BedrockPseudo<(outs), (ins GPR64:$address),
                  [(int_bedrock_invalidate_page GPR64:$address)]>;
def BEDROCK_INVASID :
    BedrockPseudo<(outs), (ins i32imm:$asid),
                  [(int_bedrock_invalidate_asid timm:$asid)]>;
def BEDROCK_SWPT : BedrockPseudo<(outs), (ins GPR64:$ptcr),
                                 [(int_bedrock_switch_page_table i64:$ptcr)]>;
def BEDROCK_SWPTA :
    BedrockPseudo<(outs), (ins GPR64:$ptcr, GPR64:$asid),
                  [(int_bedrock_switch_page_table_asid i64:$ptcr,
                                                        i64:$asid)]>;
def BEDROCK_VTOP :
    BedrockPseudo<(outs GPR64:$dst, GPR64:$flags),
                  (ins GPR64:$address),
                  [(set i64:$dst, i64:$flags,
                        (int_bedrock_virtual_to_physical i64:$address))]> {
  let Constraints = "$dst = $address";
}
def BEDROCK_PTQUERY :
    BedrockPseudo<(outs GPR64:$dst, GPR64:$flags),
                  (ins GPR64:$address, i32imm:$level),
                  [(set i64:$dst, i64:$flags,
                        (int_bedrock_page_table_query timm:$level,
                                                       i64:$address))]>;

def BEDROCK_SAVE :
    BedrockPseudo<(outs), (ins GPR64:$address),
                  [(int_bedrock_save_processor_state GPR64:$address)]>;
def BEDROCK_RESTORE :
    BedrockPseudo<(outs), (ins GPR64:$address),
                  [(int_bedrock_restore_processor_state GPR64:$address)]> {
  let Defs = [R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13,
              R14, R15, FLAGS, STATUS, GS0, GS1, GS2, GS3, GS4, GS5, F0, F1, F2,
              F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15,
              FSTATUS, FFLAGS];
  let mayLoad = 1;
}

}

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
def CMPLri : BedrockPseudo<(outs), (ins GPR64:$lhs, i64imm:$imm)>;
def CMPQri : BedrockPseudo<(outs), (ins GPR64:$lhs, i64imm:$imm)>;
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
def : Pat<(BedrockTest i32:$src), (TESTLrr GPR64:$src, GPR64:$src)>;
def : Pat<(BedrockTest i64:$src), (TESTQrr GPR64:$src, GPR64:$src)>;
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

def EXTSQBrr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src)>;
def EXTSQWrr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src)>;
def EXTZQBrr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src)>;
def EXTZQWrr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src)>;

def MOVQrs : BedrockExtraShortRegIn<0b100, "mov.q\t$r, sp">;
def MOVQsr : BedrockExtraShortRegOut<0b101, "mov.q\tsp, $r">;
def PUSHr : BedrockExtraShortRegIn<0b010, "push\t$r">;
def POPr : BedrockExtraShortRegOut<0b011, "pop\t$r">;

def SETr : BedrockSet<0x0, "set">;
def INCLr : BedrockRegLow<0b1000100000, "inc.l">;
def INCQr : BedrockRegLow<0b1000110000, "inc.q">;
def DECLr : BedrockRegLow<0b1000100001, "dec.l">;
def DECQr : BedrockRegLow<0b1000110001, "dec.q">;
def NEGLr : BedrockRegLow<0b1000100010, "neg.l">;
def NEGQr : BedrockRegLow<0b1000110010, "neg.q">;
def CLRLr : BedrockRegLow<0b1000100011, "clr.l">;
def CLRQr : BedrockExtraShortRegIn<0b110, "clr.q\t$r">;
def ABSLr : BedrockRegLow<0b1000100100, "abs.l">;
def ABSQr : BedrockRegLow<0b1000110100, "abs.q">;
def NOTLr : BedrockRegLow<0b1000100101, "not.l">;
def NOTQr : BedrockRegLow<0b1000110101, "not.q">;
def REVBYTEWr : BedrockRegLow<0b1000101001, "revbyte.w">;
def REVBYTELr : BedrockRegLow<0b1000101010, "revbyte.l">;
def REVBYTEQr : BedrockRegLow<0b1000101011, "revbyte.q">;
def PUSHPi : BedrockExtraShortImm3<0b0010, "pushp">;
def POPPi : BedrockExtraShortImm3<0b0011, "popp">;
let mayStore = 1, hasSideEffects = 1, Uses = [SP], Defs = [SP] in
def FPUSHPi : BedrockExtraShortImm3<0b1110, "fpushp">;
let mayLoad = 1, hasSideEffects = 1, Uses = [SP], Defs = [SP] in
def FPOPPi : BedrockExtraShortImm3<0b1111, "fpopp">;

def ADDQisp : BedrockImm8<uimm8, 0x2f, "add.q\t$i, sp">;
def JMPi : BedrockImm8<simm8, 0x30, "jmp\t$i">;
def SUBQisp : BedrockImm8<uimm8, 0x31, "sub.q\t$i, sp">;

let isReMaterializable = 1 in {
def CONST32 : BedrockPseudo<(outs GPR64:$rd), (ins i32imm:$imm)>;
def CONST64 : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$imm)>;
}

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

let isCompare = 1, hasSideEffects = 1 in {
def FCMPSrr : BedrockPseudo<(outs), (ins FPR64:$lhs, FPR64:$rhs),
                            [(BedrockFCmp f32:$lhs, f32:$rhs)]>;
def FCMPDrr : BedrockPseudo<(outs), (ins FPR64:$lhs, FPR64:$rhs),
                            [(BedrockFCmp f64:$lhs, f64:$rhs)]>;
}

let Constraints = "$rd = $fval" in {
def FP_SELECT_CC_S
    : BedrockPseudo<(outs FPR64:$rd),
                    (ins FPR64:$lhs, FPR64:$rhs, FPR64:$tval,
                         FPR64:$fval, i32imm:$cc),
                    [(set f32:$rd,
                          (BedrockFPSelectCC f32:$lhs, f32:$rhs, f32:$tval,
                                             f32:$fval, imm:$cc))]>;
def FP_SELECT_CC_D
    : BedrockPseudo<(outs FPR64:$rd),
                    (ins FPR64:$lhs, FPR64:$rhs, FPR64:$tval,
                         FPR64:$fval, i32imm:$cc),
                    [(set f64:$rd,
                          (BedrockFPSelectCC f64:$lhs, f64:$rhs, f64:$tval,
                                             f64:$fval, imm:$cc))]>;
}

let usesCustomInserter = 1, hasSideEffects = 1 in {
def FP_SET_CC_S
    : BedrockPseudo<(outs GPR64:$rd),
                    (ins FPR64:$lhs, FPR64:$rhs, i32imm:$cc),
                    [(set i64:$rd,
                          (BedrockFPSetCC f32:$lhs, f32:$rhs, imm:$cc))]>;
def FP_SET_CC_D
    : BedrockPseudo<(outs GPR64:$rd),
                    (ins FPR64:$lhs, FPR64:$rhs, i32imm:$cc),
                    [(set i64:$rd,
                          (BedrockFPSetCC f64:$lhs, f64:$rhs, imm:$cc))]>;
}

let Constraints = "$rd = $lhs" in {
def FADDSrr : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$lhs, FPR64:$rhs),
                            [(set f32:$rd, (fadd f32:$lhs, f32:$rhs))]>;
def FADDDrr : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$lhs, FPR64:$rhs),
                            [(set f64:$rd, (fadd f64:$lhs, f64:$rhs))]>;
def FSUBSrr : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$lhs, FPR64:$rhs),
                            [(set f32:$rd, (fsub f32:$lhs, f32:$rhs))]>;
def FSUBDrr : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$lhs, FPR64:$rhs),
                            [(set f64:$rd, (fsub f64:$lhs, f64:$rhs))]>;
def FMULSrr : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$lhs, FPR64:$rhs),
                            [(set f32:$rd, (fmul f32:$lhs, f32:$rhs))]>;
def FMULDrr : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$lhs, FPR64:$rhs),
                            [(set f64:$rd, (fmul f64:$lhs, f64:$rhs))]>;
def FDIVSrr : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$lhs, FPR64:$rhs),
                            [(set f32:$rd, (fdiv f32:$lhs, f32:$rhs))]>;
def FDIVDrr : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$lhs, FPR64:$rhs),
                            [(set f64:$rd, (fdiv f64:$lhs, f64:$rhs))]>;
} // Constraints

def FCVTSQSrr : BedrockPseudo<(outs FPR64:$rd), (ins GPR64:$src),
                              [(set f32:$rd, (sint_to_fp i64:$src))]>;
def FCVTSQDrr : BedrockPseudo<(outs FPR64:$rd), (ins GPR64:$src),
                              [(set f64:$rd, (sint_to_fp i64:$src))]>;
def FCVTUQSrr : BedrockPseudo<(outs FPR64:$rd), (ins GPR64:$src),
                              [(set f32:$rd, (uint_to_fp i64:$src))]>;
def FCVTUQDrr : BedrockPseudo<(outs FPR64:$rd), (ins GPR64:$src),
                              [(set f64:$rd, (uint_to_fp i64:$src))]>;

def FCVTStoQrr : BedrockPseudo<(outs GPR64:$rd), (ins FPR64:$src),
                               [(set i64:$rd, (fp_to_sint f32:$src))]>;
def FCVTDtoQrr : BedrockPseudo<(outs GPR64:$rd), (ins FPR64:$src),
                               [(set i64:$rd, (fp_to_sint f64:$src))]>;
def FCVTUStoQrr : BedrockPseudo<(outs GPR64:$rd), (ins FPR64:$src),
                                [(set i64:$rd, (fp_to_uint f32:$src))]>;
def FCVTUDtoQrr : BedrockPseudo<(outs GPR64:$rd), (ins FPR64:$src),
                                [(set i64:$rd, (fp_to_uint f64:$src))]>;

def FCVTDtoSrr : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$src),
                               [(set f32:$rd, (fpround f64:$src))]>;
def FCVTStoDrr : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$src),
                               [(set f64:$rd, (fpextend f32:$src))]>;

def : Pat<(f64 (sint_to_fp i32:$src)), (FCVTSQDrr (EXTSQLrr GPR64:$src))>;
def : Pat<(f64 (uint_to_fp i32:$src)), (FCVTUQDrr (EXTZQLrr GPR64:$src))>;
def : Pat<(f32 (sint_to_fp i32:$src)), (FCVTSQSrr (EXTSQLrr GPR64:$src))>;
def : Pat<(f32 (uint_to_fp i32:$src)), (FCVTUQSrr (EXTZQLrr GPR64:$src))>;
def : Pat<(i32 (fp_to_sint f32:$src)), (FCVTStoQrr FPR64:$src)>;
def : Pat<(i32 (fp_to_sint f64:$src)), (FCVTDtoQrr FPR64:$src)>;
def : Pat<(i32 (fp_to_uint f32:$src)), (FCVTUStoQrr FPR64:$src)>;
def : Pat<(i32 (fp_to_uint f64:$src)), (FCVTUDtoQrr FPR64:$src)>;

multiclass BedrockFpuUnaryPseudo<SDPatternOperator Op> {
  def _S : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$src),
                         [(set f32:$rd, (Op f32:$src))]>;
  def _D : BedrockPseudo<(outs FPR64:$rd), (ins FPR64:$src),
                         [(set f64:$rd, (Op f64:$src))]>;
}

defm BEDROCK_FABS : BedrockFpuUnaryPseudo<fabs>;
defm BEDROCK_FNEG : BedrockFpuUnaryPseudo<fneg>;
defm BEDROCK_FSQRT : BedrockFpuUnaryPseudo<fsqrt>;
defm BEDROCK_FROUND : BedrockFpuUnaryPseudo<froundeven>;
defm BEDROCK_FTRUNC : BedrockFpuUnaryPseudo<ftrunc>;
defm BEDROCK_FCEIL : BedrockFpuUnaryPseudo<fceil>;
defm BEDROCK_FFLOOR : BedrockFpuUnaryPseudo<ffloor>;

multiclass BedrockFusedPseudo {
  let Constraints = "$rd = $acc", Size = 4 in {
    def _S : BedrockPseudo<(outs FPR64:$rd),
                           (ins FPR64:$lhs, FPR64:$rhs, FPR64:$acc)>;
    def _D : BedrockPseudo<(outs FPR64:$rd),
                           (ins FPR64:$lhs, FPR64:$rhs, FPR64:$acc)>;
  }
}

defm BEDROCK_FMADD : BedrockFusedPseudo;
defm BEDROCK_FMSUB : BedrockFusedPseudo;
defm BEDROCK_FNMADD : BedrockFusedPseudo;
defm BEDROCK_FNMSUB : BedrockFusedPseudo;

def : Pat<(fma f32:$lhs, f32:$rhs, f32:$acc),
          (BEDROCK_FMADD_S FPR64:$lhs, FPR64:$rhs, FPR64:$acc)>;
def : Pat<(fma f64:$lhs, f64:$rhs, f64:$acc),
          (BEDROCK_FMADD_D FPR64:$lhs, FPR64:$rhs, FPR64:$acc)>;
def : Pat<(fma f32:$lhs, f32:$rhs, (fneg f32:$acc)),
          (BEDROCK_FMSUB_S FPR64:$lhs, FPR64:$rhs, FPR64:$acc)>;
def : Pat<(fma f64:$lhs, f64:$rhs, (fneg f64:$acc)),
          (BEDROCK_FMSUB_D FPR64:$lhs, FPR64:$rhs, FPR64:$acc)>;
def : Pat<(fma (fneg f32:$lhs), f32:$rhs, f32:$acc),
          (BEDROCK_FNMADD_S FPR64:$lhs, FPR64:$rhs, FPR64:$acc)>;
def : Pat<(fma (fneg f64:$lhs), f64:$rhs, f64:$acc),
          (BEDROCK_FNMADD_D FPR64:$lhs, FPR64:$rhs, FPR64:$acc)>;
def : Pat<(fma f32:$lhs, (fneg f32:$rhs), f32:$acc),
          (BEDROCK_FNMADD_S FPR64:$lhs, FPR64:$rhs, FPR64:$acc)>;
def : Pat<(fma f64:$lhs, (fneg f64:$rhs), f64:$acc),
          (BEDROCK_FNMADD_D FPR64:$lhs, FPR64:$rhs, FPR64:$acc)>;
def : Pat<(fma (fneg f32:$lhs), f32:$rhs, (fneg f32:$acc)),
          (BEDROCK_FNMSUB_S FPR64:$lhs, FPR64:$rhs, FPR64:$acc)>;
def : Pat<(fma (fneg f64:$lhs), f64:$rhs, (fneg f64:$acc)),
          (BEDROCK_FNMSUB_D FPR64:$lhs, FPR64:$rhs, FPR64:$acc)>;
def : Pat<(fma f32:$lhs, (fneg f32:$rhs), (fneg f32:$acc)),
          (BEDROCK_FNMSUB_S FPR64:$lhs, FPR64:$rhs, FPR64:$acc)>;
def : Pat<(fma f64:$lhs, (fneg f64:$rhs), (fneg f64:$acc)),
          (BEDROCK_FNMSUB_D FPR64:$lhs, FPR64:$rhs, FPR64:$acc)>;

multiclass BedrockApproxUnaryPseudo<Intrinsic Intr> {
  let Size = 4 in {
    def _S : BedrockPseudo<(outs FPR64:$dst), (ins FPR64:$src),
                           [(set f32:$dst, (Intr f32:$src))]>;
    def _D : BedrockPseudo<(outs FPR64:$dst), (ins FPR64:$src),
                           [(set f64:$dst, (Intr f64:$src))]>;
  }
}

let Predicates = [HasFPTRANSA] in {
defm BEDROCK_FACOSA : BedrockApproxUnaryPseudo<int_bedrock_facosa>;
defm BEDROCK_FASINA : BedrockApproxUnaryPseudo<int_bedrock_fasina>;
defm BEDROCK_FATANA : BedrockApproxUnaryPseudo<int_bedrock_fatana>;
defm BEDROCK_FATANHA : BedrockApproxUnaryPseudo<int_bedrock_fatanha>;
defm BEDROCK_FCOSA : BedrockApproxUnaryPseudo<int_bedrock_fcosa>;
defm BEDROCK_FCOSHA : BedrockApproxUnaryPseudo<int_bedrock_fcosha>;
defm BEDROCK_FETOXA : BedrockApproxUnaryPseudo<int_bedrock_fetoxa>;
defm BEDROCK_FETOXM1A : BedrockApproxUnaryPseudo<int_bedrock_fetoxm1a>;
defm BEDROCK_FLOG10A : BedrockApproxUnaryPseudo<int_bedrock_flog10a>;
defm BEDROCK_FLOG2A : BedrockApproxUnaryPseudo<int_bedrock_flog2a>;
defm BEDROCK_FLOGNA : BedrockApproxUnaryPseudo<int_bedrock_flogna>;
defm BEDROCK_FLOGNP1A : BedrockApproxUnaryPseudo<int_bedrock_flognp1a>;
defm BEDROCK_FSINA : BedrockApproxUnaryPseudo<int_bedrock_fsina>;
defm BEDROCK_FSINHA : BedrockApproxUnaryPseudo<int_bedrock_fsinha>;
defm BEDROCK_FTANA : BedrockApproxUnaryPseudo<int_bedrock_ftana>;
defm BEDROCK_FTANHA : BedrockApproxUnaryPseudo<int_bedrock_ftanha>;
defm BEDROCK_FTENTOXA : BedrockApproxUnaryPseudo<int_bedrock_ftentoxa>;
defm BEDROCK_FTWOTOXA : BedrockApproxUnaryPseudo<int_bedrock_ftwotoxa>;

let Size = 5 in {
  def BEDROCK_FSINCOSA_S
      : BedrockPseudo<(outs FPR64:$sin, FPR64:$cos), (ins FPR64:$src),
                      [(set f32:$sin, f32:$cos,
                            (int_bedrock_fsincosa f32:$src))]>;
  def BEDROCK_FSINCOSA_D
      : BedrockPseudo<(outs FPR64:$sin, FPR64:$cos), (ins FPR64:$src),
                      [(set f64:$sin, f64:$cos,
                            (int_bedrock_fsincosa f64:$src))]>;
}
}

let Constraints = "$rd = $src" in {
def INCL3r : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src)>;
def INCQ3r : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src)>;
def DECL3r : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src)>;
def DECQ3r : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src)>;
def NEGL3r : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src)>;
def NEGQ3r : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src)>;
def ABSL3r : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src)>;
def ABSQ3r : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src)>;
def NOTL3r : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src)>;
def NOTQ3r : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src)>;
def SMAX_ZERO_L : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src),
                                [(set i32:$rd, (BedrockSMaxZero i32:$src))]>;
def SMAX_ZERO_Q : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src),
                                [(set i64:$rd, (BedrockSMaxZero i64:$src))]>;
def SMIN_ZERO_L : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src),
                                [(set i32:$rd, (BedrockSMinZero i32:$src))]>;
def SMIN_ZERO_Q : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$src),
                                [(set i64:$rd, (BedrockSMinZero i64:$src))]>;
}

let Constraints = "$rd = $src" in
def BSETQ3ri : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$src, i64imm:$imm)>;
let Constraints = "$rd = $src" in
def BSET2Q3ri : BedrockPseudo<(outs GPR64:$rd),
                              (ins GPR64:$src, i64imm:$imm0, i64imm:$imm1)>;

let Constraints = "$rd = $lhs" in {
let isCommutable = 1 in {
def ADDL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def ADDQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
}
def ADDL3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def ADDQ3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def SUBL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def SUBQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def SUBL3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def SUBQ3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
let isCommutable = 1 in {
def ANDL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def ANDQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
}
def ANDL3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def ANDQ3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
let isCommutable = 1 in {
def ORL3rr : BedrockPseudo<(outs GPR64:$rd),
                           (ins GPR64:$lhs, GPR64:$rhs)>;
def ORQ3rr : BedrockPseudo<(outs GPR64:$rd),
                           (ins GPR64:$lhs, GPR64:$rhs)>;
}
def ORL3ri : BedrockPseudo<(outs GPR64:$rd),
                           (ins GPR64:$lhs, i64imm:$imm)>;
def ORQ3ri : BedrockPseudo<(outs GPR64:$rd),
                           (ins GPR64:$lhs, i64imm:$imm)>;
let isCommutable = 1 in {
def XORL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def XORQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
}
def XORL3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def XORQ3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def SHLL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def SHLQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def SHLL3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def SHLQ3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def ROLL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def ROLQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def ROLB3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def ROLL3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def ROLQ3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def RORL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def RORQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def RORL3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def RORQ3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
let isCommutable = 1 in {
def MINUL3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs),
                             [(set i32:$rd,
                                   (BedrockUMin i32:$lhs, i32:$rhs))]>;
let isCommutable = 0 in
def MINUL3ri : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, i64imm:$imm)>;
def MINUQ3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs),
                             [(set i64:$rd,
                                   (BedrockUMin i64:$lhs, i64:$rhs))]>;
let isCommutable = 0 in
def MINUQ3ri : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, i64imm:$imm)>;
def MINSL3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs),
                             [(set i32:$rd,
                                   (BedrockSMin i32:$lhs, i32:$rhs))]>;
let isCommutable = 0 in
def MINSL3ri : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, i64imm:$imm)>;
def MINSQ3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs),
                             [(set i64:$rd,
                                   (BedrockSMin i64:$lhs, i64:$rhs))]>;
let isCommutable = 0 in
def MINSQ3ri : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, i64imm:$imm)>;
def MAXUL3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs),
                             [(set i32:$rd,
                                   (BedrockUMax i32:$lhs, i32:$rhs))]>;
let isCommutable = 0 in
def MAXUL3ri : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, i64imm:$imm)>;
def MAXUQ3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs),
                             [(set i64:$rd,
                                   (BedrockUMax i64:$lhs, i64:$rhs))]>;
let isCommutable = 0 in
def MAXUQ3ri : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, i64imm:$imm)>;
def MAXSL3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs),
                             [(set i32:$rd,
                                   (BedrockSMax i32:$lhs, i32:$rhs))]>;
let isCommutable = 0 in
def MAXSL3ri : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, i64imm:$imm)>;
def MAXSQ3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs),
                             [(set i64:$rd,
                                   (BedrockSMax i64:$lhs, i64:$rhs))]>;
let isCommutable = 0 in
def MAXSQ3ri : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, i64imm:$imm)>;
}
def SHRL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def SHRQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def SHRL3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def SHRQ3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def SARL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def SARQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def SARL3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def SARQ3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
let isCommutable = 1 in {
def MULL3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
def MULQ3rr : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, GPR64:$rhs)>;
}
def MULL3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def MULQ3ri : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$lhs, i64imm:$imm)>;
def DIVUL3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs)>;
def DIVUQ3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs)>;
def DIVSL3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs)>;
def DIVSQ3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs)>;
def DIVSL3ri : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, i64imm:$imm)>;
def DIVSQ3ri : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, i64imm:$imm)>;
def MODUL3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs)>;
def MODUQ3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs)>;
def MODSL3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs)>;
def MODSQ3rr : BedrockPseudo<(outs GPR64:$rd),
                             (ins GPR64:$lhs, GPR64:$rhs)>;

multiclass BedrockBinaryMemPseudos {
  def rm : BedrockPseudo<(outs GPR64:$rd),
                         (ins GPR64:$lhs, GPR64:$base)>;
  def rmx : BedrockPseudo<(outs GPR64:$rd),
                          (ins GPR64:$lhs, GPR64:$base, GPR64:$index)>;
  def rmo : BedrockPseudo<(outs GPR64:$rd),
                          (ins GPR64:$lhs, GPR64:$base, i64imm:$offset)>;
  def rmfi : BedrockPseudo<(outs GPR64:$rd),
                           (ins GPR64:$lhs, i64imm:$base, i64imm:$offset)>;
}

multiclass BedrockBinaryMemDestPseudos {
  let Constraints = "" in {
  def mr : BedrockPseudo<(outs), (ins GPR64:$src, GPR64:$base)>;
  def mro : BedrockPseudo<(outs),
                          (ins GPR64:$src, GPR64:$base, i64imm:$offset)>;
  def mfi : BedrockPseudo<(outs),
                          (ins GPR64:$src, i64imm:$base, i64imm:$offset)>;
  }
}

defm ADDL3 : BedrockBinaryMemPseudos;
defm ADDQ3 : BedrockBinaryMemPseudos;
defm SUBL3 : BedrockBinaryMemPseudos;
defm SUBQ3 : BedrockBinaryMemPseudos;
defm ANDL3 : BedrockBinaryMemPseudos;
defm ANDQ3 : BedrockBinaryMemPseudos;
defm ORL3 : BedrockBinaryMemPseudos;
defm ORQ3 : BedrockBinaryMemPseudos;
defm XORL3 : BedrockBinaryMemPseudos;
defm XORQ3 : BedrockBinaryMemPseudos;
defm MULL3 : BedrockBinaryMemPseudos;
defm MULQ3 : BedrockBinaryMemPseudos;
defm MINUL3 : BedrockBinaryMemPseudos;
defm MINUQ3 : BedrockBinaryMemPseudos;
defm MINSL3 : BedrockBinaryMemPseudos;
defm MINSQ3 : BedrockBinaryMemPseudos;
defm MAXUL3 : BedrockBinaryMemPseudos;
defm MAXUQ3 : BedrockBinaryMemPseudos;
defm MAXSL3 : BedrockBinaryMemPseudos;
defm MAXSQ3 : BedrockBinaryMemPseudos;
defm DIVUL3 : BedrockBinaryMemPseudos;
defm DIVUQ3 : BedrockBinaryMemPseudos;
defm DIVSL3 : BedrockBinaryMemPseudos;
defm DIVSQ3 : BedrockBinaryMemPseudos;
defm MODUL3 : BedrockBinaryMemPseudos;
defm MODUQ3 : BedrockBinaryMemPseudos;
defm MODSL3 : BedrockBinaryMemPseudos;
defm MODSQ3 : BedrockBinaryMemPseudos;
} // Constraints

let mayLoad = 1, mayStore = 1 in {
defm ADDL3 : BedrockBinaryMemDestPseudos;
defm ADDQ3 : BedrockBinaryMemDestPseudos;
defm SUBL3 : BedrockBinaryMemDestPseudos;
defm SUBQ3 : BedrockBinaryMemDestPseudos;
defm ANDL3 : BedrockBinaryMemDestPseudos;
defm ANDQ3 : BedrockBinaryMemDestPseudos;
defm ORL3 : BedrockBinaryMemDestPseudos;
defm ORQ3 : BedrockBinaryMemDestPseudos;
defm XORL3 : BedrockBinaryMemDestPseudos;
defm XORQ3 : BedrockBinaryMemDestPseudos;
defm MINUL3 : BedrockBinaryMemDestPseudos;
defm MINUQ3 : BedrockBinaryMemDestPseudos;
defm MINSL3 : BedrockBinaryMemDestPseudos;
defm MINSQ3 : BedrockBinaryMemDestPseudos;
defm MAXUL3 : BedrockBinaryMemDestPseudos;
defm MAXUQ3 : BedrockBinaryMemDestPseudos;
defm MAXSL3 : BedrockBinaryMemDestPseudos;
defm MAXSQ3 : BedrockBinaryMemDestPseudos;
}

multiclass BedrockCmpMemPseudos {
  def mr : BedrockPseudo<(outs), (ins GPR64:$base, GPR64:$rhs)>;
  def mxr : BedrockPseudo<(outs),
                          (ins GPR64:$base, GPR64:$index, GPR64:$rhs)>;
  def mor : BedrockPseudo<(outs),
                          (ins GPR64:$base, i64imm:$offset, GPR64:$rhs)>;
  def mabsr : BedrockPseudo<(outs), (ins i64imm:$addr, GPR64:$rhs)>;
  def mfir : BedrockPseudo<(outs),
                           (ins i64imm:$base, i64imm:$offset, GPR64:$rhs)>;
  def rm : BedrockPseudo<(outs), (ins GPR64:$lhs, GPR64:$base)>;
  def rmx : BedrockPseudo<(outs),
                          (ins GPR64:$lhs, GPR64:$base, GPR64:$index)>;
  def rmo : BedrockPseudo<(outs),
                          (ins GPR64:$lhs, GPR64:$base, i64imm:$offset)>;
  def rmabs : BedrockPseudo<(outs), (ins GPR64:$lhs, i64imm:$addr)>;
  def rmfi : BedrockPseudo<(outs),
                           (ins GPR64:$lhs, i64imm:$base, i64imm:$offset)>;
}

let mayLoad = 1 in {
defm CMPL : BedrockCmpMemPseudos;
defm CMPQ : BedrockCmpMemPseudos;

def CMPLmfmf : BedrockPseudo<(outs), (ins i64imm:$srcbase,
                                           i64imm:$srcoffset,
                                           i64imm:$dstbase,
                                           i64imm:$dstoffset)>;
def CMPQmfmf : BedrockPseudo<(outs), (ins i64imm:$srcbase,
                                           i64imm:$srcoffset,
                                           i64imm:$dstbase,
                                           i64imm:$dstoffset)>;
}

let mayLoad = 1 in {
def LOADB_Zrr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$base)>;
def LOADW_Zrr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$base)>;
def LOADL_Zrr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$base)>;
def LOADB_Srr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$base)>;
def LOADW_Srr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$base)>;
def LOADL_Srr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$base)>;
def LOADLrr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$base)>;
def LOADQrr : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$base)>;
def FLOADSrr : BedrockPseudo<(outs FPR64:$rd), (ins GPR64:$base)>;
def FLOADDrr : BedrockPseudo<(outs FPR64:$rd), (ins GPR64:$base)>;

def LOADB_Zrx : BedrockPseudo<(outs GPR64:$rd),
                              (ins GPR64:$base, GPR64:$index)>;
def LOADW_Zrx : BedrockPseudo<(outs GPR64:$rd),
                              (ins GPR64:$base, GPR64:$index)>;
def LOADL_Zrx : BedrockPseudo<(outs GPR64:$rd),
                              (ins GPR64:$base, GPR64:$index)>;
def LOADB_Srx : BedrockPseudo<(outs GPR64:$rd),
                              (ins GPR64:$base, GPR64:$index)>;
def LOADW_Srx : BedrockPseudo<(outs GPR64:$rd),
                              (ins GPR64:$base, GPR64:$index)>;
def LOADL_Srx : BedrockPseudo<(outs GPR64:$rd),
                              (ins GPR64:$base, GPR64:$index)>;
def LOADLrx : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$base, GPR64:$index)>;
def LOADQrx : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$base, GPR64:$index)>;

def LOADB_Zspx : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$index)>;
def LOADW_Zspx : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$index)>;
def LOADL_Zspx : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$index)>;
def LOADB_Sspx : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$index)>;
def LOADW_Sspx : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$index)>;
def LOADL_Sspx : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$index)>;
def LOADLspx : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$index)>;
def LOADQspx : BedrockPseudo<(outs GPR64:$rd), (ins GPR64:$index)>;

def LOADB_Zro : BedrockPseudo<(outs GPR64:$rd),
                              (ins GPR64:$base, i64imm:$offset)>;
def LOADW_Zro : BedrockPseudo<(outs GPR64:$rd),
                              (ins GPR64:$base, i64imm:$offset)>;
def LOADL_Zro : BedrockPseudo<(outs GPR64:$rd),
                              (ins GPR64:$base, i64imm:$offset)>;
def LOADB_Sro : BedrockPseudo<(outs GPR64:$rd),
                              (ins GPR64:$base, i64imm:$offset)>;
def LOADW_Sro : BedrockPseudo<(outs GPR64:$rd),
                              (ins GPR64:$base, i64imm:$offset)>;
def LOADL_Sro : BedrockPseudo<(outs GPR64:$rd),
                              (ins GPR64:$base, i64imm:$offset)>;
def LOADLro : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$base, i64imm:$offset)>;
def LOADQro : BedrockPseudo<(outs GPR64:$rd),
                            (ins GPR64:$base, i64imm:$offset)>;
def FLOADSro : BedrockPseudo<(outs FPR64:$rd),
                             (ins GPR64:$base, i64imm:$offset)>;
def FLOADDro : BedrockPseudo<(outs FPR64:$rd),
                             (ins GPR64:$base, i64imm:$offset)>;

def LOADB_Zabs : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$addr)>;
def LOADW_Zabs : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$addr)>;
def LOADL_Zabs : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$addr)>;
def LOADB_Sabs : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$addr)>;
def LOADW_Sabs : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$addr)>;
def LOADL_Sabs : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$addr)>;
def LOADLabs : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$addr)>;
def LOADQabs : BedrockPseudo<(outs GPR64:$rd), (ins i64imm:$addr)>;
def FLOADSabs : BedrockPseudo<(outs FPR64:$rd), (ins i64imm:$addr)>;
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
def FLOADSfi : BedrockPseudo<(outs FPR64:$rd),
                             (ins i64imm:$base, i64imm:$offset)>;
def FLOADDfi : BedrockPseudo<(outs FPR64:$rd),
                             (ins i64imm:$base, i64imm:$offset)>;
}

def LEAfi : BedrockPseudo<(outs GPR64:$rd),
                          (ins i64imm:$base, i64imm:$offset)>;
def LEAro : BedrockPseudo<(outs GPR64:$rd),
                          (ins GPR64:$base, i64imm:$offset)>;
def LEArx : BedrockPseudo<(outs GPR64:$rd),
                          (ins GPR64:$base, GPR64:$index, i64imm:$scale)>;

let mayLoad = 1, mayStore = 1 in {
def MOVLmmrr : BedrockPseudo<(outs), (ins GPR64:$srcbase, GPR64:$dstbase)>;
def MOVQmmrr : BedrockPseudo<(outs), (ins GPR64:$srcbase, GPR64:$dstbase)>;
def INCLm : BedrockPseudo<(outs), (ins GPR64:$base)>;
def INCQm : BedrockPseudo<(outs), (ins GPR64:$base)>;
def DECLm : BedrockPseudo<(outs), (ins GPR64:$base)>;
def DECQm : BedrockPseudo<(outs), (ins GPR64:$base)>;
def INCLmo : BedrockPseudo<(outs), (ins GPR64:$base, i64imm:$offset)>;
def INCQmo : BedrockPseudo<(outs), (ins GPR64:$base, i64imm:$offset)>;
def DECLmo : BedrockPseudo<(outs), (ins GPR64:$base, i64imm:$offset)>;
def DECQmo : BedrockPseudo<(outs), (ins GPR64:$base, i64imm:$offset)>;
def INCLfi : BedrockPseudo<(outs), (ins i64imm:$base, i64imm:$offset)>;
def INCQfi : BedrockPseudo<(outs), (ins i64imm:$base, i64imm:$offset)>;
def DECLfi : BedrockPseudo<(outs), (ins i64imm:$base, i64imm:$offset)>;
def DECQfi : BedrockPseudo<(outs), (ins i64imm:$base, i64imm:$offset)>;
def INCLabs : BedrockPseudo<(outs), (ins i64imm:$addr)>;
def INCQabs : BedrockPseudo<(outs), (ins i64imm:$addr)>;
def DECLabs : BedrockPseudo<(outs), (ins i64imm:$addr)>;
def DECQabs : BedrockPseudo<(outs), (ins i64imm:$addr)>;
}

let mayStore = 1 in {
def STOREBrr : BedrockPseudo<(outs), (ins GPR64:$src, GPR64:$base)>;
def STOREWrr : BedrockPseudo<(outs), (ins GPR64:$src, GPR64:$base)>;
def STORELrr : BedrockPseudo<(outs), (ins GPR64:$src, GPR64:$base)>;
def STOREQrr : BedrockPseudo<(outs), (ins GPR64:$src, GPR64:$base)>;
def STOREBrx : BedrockPseudo<(outs),
                             (ins GPR64:$src, GPR64:$base, GPR64:$index)>;
def STOREWrx : BedrockPseudo<(outs),
                             (ins GPR64:$src, GPR64:$base, GPR64:$index)>;
def STORELrx : BedrockPseudo<(outs),
                             (ins GPR64:$src, GPR64:$base, GPR64:$index)>;
def STOREQrx : BedrockPseudo<(outs),
                             (ins GPR64:$src, GPR64:$base, GPR64:$index)>;
def STOREBspx : BedrockPseudo<(outs), (ins GPR64:$src, GPR64:$index)>;
def STOREWspx : BedrockPseudo<(outs), (ins GPR64:$src, GPR64:$index)>;
def STORELspx : BedrockPseudo<(outs), (ins GPR64:$src, GPR64:$index)>;
def STOREQspx : BedrockPseudo<(outs), (ins GPR64:$src, GPR64:$index)>;
def FSTORESrr : BedrockPseudo<(outs), (ins FPR64:$src, GPR64:$base)>;
def FSTOREDrr : BedrockPseudo<(outs), (ins FPR64:$src, GPR64:$base)>;

def STOREB_Immrr : BedrockPseudo<(outs), (ins i64imm:$imm, GPR64:$base)>;
def STOREW_Immrr : BedrockPseudo<(outs), (ins i64imm:$imm, GPR64:$base)>;
def STOREL_Immrr : BedrockPseudo<(outs), (ins i64imm:$imm, GPR64:$base)>;
def STOREQ_Immrr : BedrockPseudo<(outs), (ins i64imm:$imm, GPR64:$base)>;

def STOREBro : BedrockPseudo<(outs),
                             (ins GPR64:$src, GPR64:$base, i64imm:$offset)>;
def STOREWro : BedrockPseudo<(outs),
                             (ins GPR64:$src, GPR64:$base, i64imm:$offset)>;
def STORELro : BedrockPseudo<(outs),
                             (ins GPR64:$src, GPR64:$base, i64imm:$offset)>;
def STOREQro : BedrockPseudo<(outs),
                             (ins GPR64:$src, GPR64:$base, i64imm:$offset)>;
def FSTORESro : BedrockPseudo<(outs),
                              (ins FPR64:$src, GPR64:$base, i64imm:$offset)>;
def FSTOREDro : BedrockPseudo<(outs),
                              (ins FPR64:$src, GPR64:$base, i64imm:$offset)>;

def STOREB_Immro : BedrockPseudo<(outs),
                                 (ins i64imm:$imm, GPR64:$base,
                                      i64imm:$offset)>;
def STOREW_Immro : BedrockPseudo<(outs),
                                 (ins i64imm:$imm, GPR64:$base,
                                      i64imm:$offset)>;
def STOREL_Immro : BedrockPseudo<(outs),
                                 (ins i64imm:$imm, GPR64:$base,
                                      i64imm:$offset)>;
def STOREQ_Immro : BedrockPseudo<(outs),
                                 (ins i64imm:$imm, GPR64:$base,
                                      i64imm:$offset)>;

def STOREBabs : BedrockPseudo<(outs), (ins GPR64:$src, i64imm:$addr)>;
def STOREWabs : BedrockPseudo<(outs), (ins GPR64:$src, i64imm:$addr)>;
def STORELabs : BedrockPseudo<(outs), (ins GPR64:$src, i64imm:$addr)>;
def STOREQabs : BedrockPseudo<(outs), (ins GPR64:$src, i64imm:$addr)>;
def FSTORESabs : BedrockPseudo<(outs), (ins FPR64:$src, i64imm:$addr)>;
def FSTOREDabs : BedrockPseudo<(outs), (ins FPR64:$src, i64imm:$addr)>;

def STOREB_Immabs : BedrockPseudo<(outs), (ins i64imm:$imm, i64imm:$addr)>;
def STOREW_Immabs : BedrockPseudo<(outs), (ins i64imm:$imm, i64imm:$addr)>;
def STOREL_Immabs : BedrockPseudo<(outs), (ins i64imm:$imm, i64imm:$addr)>;
def STOREQ_Immabs : BedrockPseudo<(outs), (ins i64imm:$imm, i64imm:$addr)>;

def STOREBfi : BedrockPseudo<(outs),
                             (ins GPR64:$src, i64imm:$base, i64imm:$offset)>;
def STOREWfi : BedrockPseudo<(outs),
                             (ins GPR64:$src, i64imm:$base, i64imm:$offset)>;
def STORELfi : BedrockPseudo<(outs),
                             (ins GPR64:$src, i64imm:$base, i64imm:$offset)>;
def STOREQfi : BedrockPseudo<(outs),
                             (ins GPR64:$src, i64imm:$base, i64imm:$offset)>;
def FSTORESfi : BedrockPseudo<(outs),
                              (ins FPR64:$src, i64imm:$base, i64imm:$offset)>;
def FSTOREDfi : BedrockPseudo<(outs),
                              (ins FPR64:$src, i64imm:$base, i64imm:$offset)>;

def STOREB_Immfi : BedrockPseudo<(outs),
                                 (ins i64imm:$imm, i64imm:$base,
                                      i64imm:$offset)>;
def STOREW_Immfi : BedrockPseudo<(outs),
                                 (ins i64imm:$imm, i64imm:$base,
                                      i64imm:$offset)>;
def STOREL_Immfi : BedrockPseudo<(outs),
                                 (ins i64imm:$imm, i64imm:$base,
                                      i64imm:$offset)>;
def STOREQ_Immfi : BedrockPseudo<(outs),
                                 (ins i64imm:$imm, i64imm:$base,
                                      i64imm:$offset)>;
}

// AtomicExpand brackets ordered loads/stores with AFENCE and reduces the
// memory operation itself to monotonic. Reuse the ordinary naturally aligned
// load/store encoding for that operation.
def : Pat<(i32 (atomic_load_azext_8 GPR64:$base)),
          (LOADB_Zrr GPR64:$base)>;
def : Pat<(i32 (atomic_load_azext_16 GPR64:$base)),
          (LOADW_Zrr GPR64:$base)>;
def : Pat<(i32 (atomic_load_nonext_32 GPR64:$base)),
          (LOADLrr GPR64:$base)>;
def : Pat<(i64 (atomic_load_nonext_64 GPR64:$base)),
          (LOADQrr GPR64:$base)>;
def : Pat<(atomic_store_8 i32:$src, GPR64:$base),
          (STOREBrr GPR64:$src, GPR64:$base)>;
def : Pat<(atomic_store_16 i32:$src, GPR64:$base),
          (STOREWrr GPR64:$src, GPR64:$base)>;
def : Pat<(atomic_store_32 i32:$src, GPR64:$base),
          (STORELrr GPR64:$src, GPR64:$base)>;
def : Pat<(atomic_store_64 i64:$src, GPR64:$base),
          (STOREQrr GPR64:$src, GPR64:$base)>;

let hasSideEffects = 1 in {
def ADJSP_DOWN : BedrockPseudo<(outs), (ins i64imm:$amount)>;
def ADJSP_UP : BedrockPseudo<(outs), (ins i64imm:$amount)>;
}

let isBranch = 1, isTerminator = 1 in {
let isBarrier = 1 in
def BR : BedrockPseudo<(outs), (ins brtarget:$dst), [(br bb:$dst)]>;
def BRCC : BedrockPseudo<(outs), (ins brtarget:$dst, i32imm:$cc),
                         [(BedrockBrCC bb:$dst, imm:$cc)]>;
let isBarrier = 1, isIndirectBranch = 1 in
def BRIND : BedrockPseudo<(outs), (ins GPR64:$dst), [(brind GPR64:$dst)]>;
}

let hasSideEffects = 1 in
def SETCC : BedrockPseudo<(outs GPR64:$rd), (ins i32imm:$cc),
                          [(set i64:$rd, (BedrockSetCC imm:$cc))]>;

let usesCustomInserter = 1 in {
def SELECT_CC_L : BedrockPseudo<(outs GPR64:$rd),
                                (ins GPR64:$lhs, GPR64:$rhs, GPR64:$tval,
                                     GPR64:$fval, i32imm:$cc),
                                [(set i32:$rd,
                                      (BedrockSelectCC i32:$lhs, i32:$rhs,
                                                       i32:$tval, i32:$fval,
                                                       imm:$cc))]>;
def SELECT_CC_Q : BedrockPseudo<(outs GPR64:$rd),
                                (ins GPR64:$lhs, GPR64:$rhs, GPR64:$tval,
                                     GPR64:$fval, i32imm:$cc),
                                [(set i64:$rd,
                                      (BedrockSelectCC i64:$lhs, i64:$rhs,
                                                       i64:$tval, i64:$fval,
                                                       imm:$cc))]>;
def SELECT_CC_S : BedrockPseudo<(outs FPR64:$rd),
                                (ins GPR64:$lhs, GPR64:$rhs, FPR64:$tval,
                                     FPR64:$fval, i32imm:$cc),
                                [(set f32:$rd,
                                      (BedrockSelectCC i64:$lhs, i64:$rhs,
                                                       f32:$tval, f32:$fval,
                                                       imm:$cc))]>;
def SELECT_CC_D : BedrockPseudo<(outs FPR64:$rd),
                                (ins GPR64:$lhs, GPR64:$rhs, FPR64:$tval,
                                     FPR64:$fval, i32imm:$cc),
                                [(set f64:$rd,
                                      (BedrockSelectCC i64:$lhs, i64:$rhs,
                                                       f64:$tval, f64:$fval,
                                                       imm:$cc))]>;
}

let isCall = 1, Defs = [R0, R1, R2, R3, R4, R5, R6, R7,
                        F0, F1, F2, F3, F4, F5, F6, F7, FLAGS,
                        GS1, GS2, GS3, GS4, GS5] in {
def CALL : BedrockPseudo<(outs), (ins i64imm:$dst)>;
def CALLr : BedrockPseudo<(outs), (ins GPR64:$dst)>;
// These retain ordinary call semantics until the late pass verifies that the
// generated epilogue restores the frame immediately before the transfer.
def CALL_TAIL : BedrockPseudo<(outs), (ins i64imm:$dst)>;
def CALLr_TAIL : BedrockPseudo<(outs), (ins GPR64:$dst)>;
}

let isCall = 1, hasSideEffects = 1,
    Defs = [R0, R1, R2, R3, R4, R5, R6, R7,
            F0, F1, F2, F3, F4, F5, F6, F7, FLAGS,
            GS1, GS2, GS3, GS4, GS5] in
def TLSDESC_CALL : BedrockPseudo<(outs), (ins i64imm:$sym)>;

let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def TAILCALL : BedrockPseudo<(outs), (ins i64imm:$dst)>;
let hasSideEffects = 1 in {
def ADJCALLSTACKDOWN : BedrockPseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                                     [(BedrockCallSeqStart timm:$amt1,
                                                            timm:$amt2)]>;
def ADJCALLSTACKUP : BedrockPseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                                   [(BedrockCallSeqEnd timm:$amt1,
                                                       timm:$amt2)]>;
}

def : Pat<(BedrockCall (i64 tglobaladdr:$dst)), (CALL tglobaladdr:$dst)>;
def : Pat<(BedrockCall (i64 texternalsym:$dst)), (CALL texternalsym:$dst)>;
def : Pat<(BedrockCall GPR64:$dst), (CALLr GPR64:$dst)>;
def : Pat<(BedrockCallAddress (i64 tglobaladdr:$dst)),
          (CONST64 tglobaladdr:$dst)>;
def : Pat<(BedrockCallAddress (i64 texternalsym:$dst)),
          (CONST64 texternalsym:$dst)>;
def : Pat<(BedrockTLSAddress (i64 tglobaltlsaddr:$sym)),
          (CONST64 tglobaltlsaddr:$sym)>;
def : Pat<(BedrockTLSDescCall (i64 tglobaltlsaddr:$sym)),
          (TLSDESC_CALL tglobaltlsaddr:$sym)>;
def : Pat<(BedrockTailCallCandidate (i64 tglobaladdr:$dst)),
          (CALL_TAIL tglobaladdr:$dst)>;
def : Pat<(BedrockTailCallCandidate (i64 texternalsym:$dst)),
          (CALL_TAIL texternalsym:$dst)>;
def : Pat<(BedrockTailCallCandidate GPR64:$dst),
          (CALLr_TAIL GPR64:$dst)>;
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
