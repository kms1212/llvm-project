//===-- BedrockCallingConv.td - Bedrock calling conventions -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def RetCC_Bedrock : CallingConv<[
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,
  CCIfType<[f32, f64], CCAssignToReg<[F0, F1]>>,
  CCIfType<[i32, i64], CCAssignToReg<[R0, R1]>>
]>;

def CSR_Bedrock_Save : CalleeSavedRegs<
  (add R8, R9, R10, R11, R12, R13, R14, R15,
       F8, F9, F10, F11, F12, F13, F14, F15)>;

// Segment registers have no allocatable register class and therefore cannot
// enter PEI's spill list. They are nevertheless preserved by the C ABI and
// must be present in the call-preserved mask.
def CSR_Bedrock : CalleeSavedRegs<
  (add R8, R9, R10, R11, R12, R13, R14, R15,
       F8, F9, F10, F11, F12, F13, F14, F15, CS, DS, SS, GS0)>;
