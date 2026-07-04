//===-- BedrockCallingConv.td - Bedrock Calling Conventions -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def RetCC_Bedrock : CallingConv<[
  CCIfPtr<CCAssignToReg<[A0]>>,
  CCIfType<[i1, i8, i16], CCPromoteToType<i64>>,
  CCIfType<[i32], CCAssignToReg<[D0]>>,
  CCIfType<[i64], CCAssignToReg<[D0, D1]>>,
  CCIfType<[f32, f64], CCAssignToReg<[F0]>>
]>;

def CC_Bedrock : CallingConv<[
  CCIfSRet<CCAssignToReg<[A5]>>,

  CCIfVarArg<CCIfArgVarArg<CCAssignToStack<16, 16>>>,

  CCIfType<[i1, i8, i16], CCPromoteToType<i64>>,
  CCIfPtr<CCAssignToReg<[A0, A1, A2, A3, A4, A5]>>,
  CCIfType<[i32], CCAssignToReg<[D0, D1, D2, D3, D4, D5]>>,
  CCIfType<[i64], CCAssignToReg<[D0, D1, D2, D3, D4, D5]>>,
  CCIfType<[f32, f64], CCAssignToReg<[F0, F1, F2, F3, F4, F5, F6, F7]>>,

  CCAssignToStack<16, 16>
]>;

def CSR_Bedrock : CalleeSavedRegs<(add D6, D7, A6, A7,
                                       F8, F9, F10, F11,
                                       F12, F13, F14, F15)>;
