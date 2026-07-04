//===-- BedrockRegisterInfo.td - Bedrock Register defs -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class BedrockReg<bits<16> Num, string Name, list<string> AltNames = []>
    : Register<Name, AltNames> {
  let Namespace = "Bedrock";
  let HWEncoding = Num;
  let DwarfNumbers = [Num];
}

foreach I = 0-7 in {
  def D#I : BedrockReg<I, "D"#I>;
}

foreach I = 0-7 in {
  def A#I : BedrockReg<!add(8, I), "A"#I>;
}

def SP : BedrockReg<16, "SP">;
def PC : BedrockReg<17, "PC">;

def FLAGS : BedrockReg<18, "FLAGS">;
def STATUS : BedrockReg<19, "STATUS">;

foreach I = 0-15 in {
  def F#I : BedrockReg<!add(32, I), "F"#I>;
}

class BedrockRegClass<list<ValueType> RegTypes, int Alignment, dag RegList>
    : RegisterClass<"Bedrock", RegTypes, Alignment, RegList>;

def D8  : BedrockRegClass<[i8],  8,  (sequence "D%u", 0, 7)>;
def D16 : BedrockRegClass<[i16], 16, (sequence "D%u", 0, 7)>;
def D32 : BedrockRegClass<[i32], 32, (sequence "D%u", 0, 7)>;
def D64 : BedrockRegClass<[i64], 64, (sequence "D%u", 0, 7)>;

def A32 : BedrockRegClass<[i32], 32, (sequence "A%u", 0, 7)>;
def A64 : BedrockRegClass<[i64], 64, (sequence "A%u", 0, 7)>;
def INT64 : BedrockRegClass<[i64], 64,
  (add D0, D1, D2, D3, D4, D5, D6, D7,
       A0, A1, A2, A3, A4, A5, A6, A7)>;
def PTR64 : BedrockRegClass<[i64], 64,
  (add A0, A1, A2, A3, A4, A5, A6, A7, SP, PC)>;

def GPR8  : BedrockRegClass<[i8],  8,  (add D8)>;
def GPR16 : BedrockRegClass<[i16], 16, (add D16)>;
def GPR32 : BedrockRegClass<[i32], 32,
  (add D0, D1, D2, D3, D4, D5, D6, D7,
       A0, A1, A2, A3, A4, A5, A6, A7)>;
def GPR64 : BedrockRegClass<[i64], 64,
  (add D0, D1, D2, D3, D4, D5, A0, A1, A2, A3, A4, A5, A6, A7, D6, D7,
       SP)>;

def F32 : BedrockRegClass<[f32], 32, (sequence "F%u", 0, 15)>;
def F64 : BedrockRegClass<[f64], 64, (sequence "F%u", 0, 15)>;

let isAllocatable = 0 in {
  def SPC : BedrockRegClass<[i64], 64, (add SP)>;
  def PCC : BedrockRegClass<[i64], 64, (add PC)>;
  def FLAGSC : BedrockRegClass<[i16], 16, (add FLAGS)>;
  def STATUSC : BedrockRegClass<[i16], 16, (add STATUS)>;
}
