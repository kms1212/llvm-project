//===-- BedrockRegisterInfo.td - Bedrock register defs -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class BedrockReg<bits<4> enc, int dwarf, string name, list<string> alt = []>
    : Register<name> {
  let Namespace = "Bedrock";
  let HWEncoding{3-0} = enc;
  let AltNames = alt;
  let DwarfNumbers = [dwarf];
}

class BedrockGPR<bits<4> enc, string name> : BedrockReg<enc, enc, name>;
class BedrockFPR<bits<4> enc, int dwarf, string name>
    : BedrockReg<enc, dwarf, name>;

def R0  : BedrockGPR<0,  "r0">;
def R1  : BedrockGPR<1,  "r1">;
def R2  : BedrockGPR<2,  "r2">;
def R3  : BedrockGPR<3,  "r3">;
def R4  : BedrockGPR<4,  "r4">;
def R5  : BedrockGPR<5,  "r5">;
def R6  : BedrockGPR<6,  "r6">;
def R7  : BedrockGPR<7,  "r7">;
def R8  : BedrockGPR<8,  "r8">;
def R9  : BedrockGPR<9,  "r9">;
def R10 : BedrockGPR<10, "r10">;
def R11 : BedrockGPR<11, "r11">;
def R12 : BedrockGPR<12, "r12">;
def R13 : BedrockGPR<13, "r13">;
def R14 : BedrockGPR<14, "r14">;
def R15 : BedrockGPR<15, "r15">;
def SP  : BedrockReg<0, 32, "sp">;
def PC  : BedrockReg<0, 33, "pc">;
def FLAGS : BedrockReg<0, 34, "flags">;
def STATUS : BedrockReg<0, 35, "status">;

// Segment and architectural state registers are deliberately absent from
// allocatable register classes. They are named physical resources so calls,
// far-memory pseudos, unwind information, and system intrinsics can model
// their effects without exposing them to the register allocator.
def CS  : BedrockReg<0, 36, "cs">;
def DS  : BedrockReg<1, 37, "ds">;
def SS  : BedrockReg<2, 38, "ss">;
def GS0 : BedrockReg<3, 39, "gs0">;
def GS1 : BedrockReg<4, 40, "gs1">;
def GS2 : BedrockReg<5, 41, "gs2">;
def GS3 : BedrockReg<6, 42, "gs3">;
def GS4 : BedrockReg<7, 43, "gs4">;
def FSTATUS : BedrockReg<0, 44, "fstatus">;
def FFLAGS : BedrockReg<0, 45, "fflags">;

def F0  : BedrockFPR<0,  16, "f0">;
def F1  : BedrockFPR<1,  17, "f1">;
def F2  : BedrockFPR<2,  18, "f2">;
def F3  : BedrockFPR<3,  19, "f3">;
def F4  : BedrockFPR<4,  20, "f4">;
def F5  : BedrockFPR<5,  21, "f5">;
def F6  : BedrockFPR<6,  22, "f6">;
def F7  : BedrockFPR<7,  23, "f7">;
def F8  : BedrockFPR<8,  24, "f8">;
def F9  : BedrockFPR<9,  25, "f9">;
def F10 : BedrockFPR<10, 26, "f10">;
def F11 : BedrockFPR<11, 27, "f11">;
def F12 : BedrockFPR<12, 28, "f12">;
def F13 : BedrockFPR<13, 29, "f13">;
def F14 : BedrockFPR<14, 30, "f14">;
def F15 : BedrockFPR<15, 31, "f15">;

def GPR64 : RegisterClass<"Bedrock", [i32, i64], 64, (sequence "R%u", 0, 15)> {
  let DecoderMethod = "DecodeGPR64RegisterClass";
}

def R0Only : RegisterClass<"Bedrock", [i64], 64, (add R0)>;
def R1Only : RegisterClass<"Bedrock", [i64], 64, (add R1)>;
def R2Only : RegisterClass<"Bedrock", [i64], 64, (add R2)>;
def R3Only : RegisterClass<"Bedrock", [i64], 64, (add R3)>;
def GPR64NoR0R3 :
    RegisterClass<"Bedrock", [i64], 64, (sequence "R%u", 4, 15)>;

def SPReg : RegisterClass<"Bedrock", [i64], 64, (add SP)>;

def FPR64 : RegisterClass<"Bedrock", [f32, f64], 64, (sequence "F%u", 0, 15)>;
