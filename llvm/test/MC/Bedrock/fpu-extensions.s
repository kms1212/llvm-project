; REQUIRES: bedrock-registered-target
; RUN: llvm-mc -triple=bedrock -mattr=+fptransa -show-encoding < %s \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING
; RUN: llvm-mc -triple=bedrock -mattr=+fptransa -show-encoding < %s \
; RUN:   | sed '/.text/d' | sed 's/.*encoding: //g' \
; RUN:   | llvm-mc -triple=bedrock -disassemble -show-encoding \
; RUN:   | FileCheck %s --check-prefixes=CHECK-INST,CHECK-ENCODING

fmadd.s f1, f2, f3
; CHECK-INST: fmadd.s	f1, f2, f3
; CHECK-ENCODING: encoding: [0xc7,0xd0,0x09,0x43]
fmadd.d [r1], f2, f3
; CHECK-INST: fmadd.d	[r1], f2, f3
; CHECK-ENCODING: encoding: [0xcb,0xf0,0x30,0x81,0x91]
fmadd.s f1, [r2], f3
; CHECK-INST: fmadd.s	f1, [r2], f3
; CHECK-ENCODING: encoding: [0xcb,0xf0,0x20,0x61,0x92]
fmsub.d f4, f5, f6
; CHECK-INST: fmsub.d	f4, f5, f6
; CHECK-ENCODING: encoding: [0xc7,0xd0,0xa2,0xd6]
fmsub.s [r3 + 8], f5, f6
; CHECK-INST: fmsub.s	[r3 + 8], f5, f6
; CHECK-ENCODING: encoding: [0xcf,0xf0,0x21,0x4b,0x23,0x08]
fmsub.d f4, [r6], f7
; CHECK-INST: fmsub.d	f4, [r6], f7
; CHECK-ENCODING: encoding: [0xcb,0xf0,0x31,0x2b,0x96]
fnmadd.s f8, f9, f10
; CHECK-INST: fnmadd.s	f8, f9, f10
; CHECK-ENCODING: encoding: [0xc7,0xd0,0x44,0xea]
fnmadd.d [r7], f9, f10
; CHECK-INST: fnmadd.d	[r7], f9, f10
; CHECK-ENCODING: encoding: [0xcb,0xf0,0x32,0x55,0x17]
fnmadd.s f8, [r10], f11
; CHECK-INST: fnmadd.s	f8, [r10], f11
; CHECK-ENCODING: encoding: [0xcb,0xf0,0x22,0x35,0x9a]
fnmsub.d f12, f13, f14
; CHECK-INST: fnmsub.d	f12, f13, f14
; CHECK-ENCODING: encoding: [0xc7,0xd0,0xe6,0xfe]
fnmsub.s [r11], f13, f14
; CHECK-INST: fnmsub.s	[r11], f13, f14
; CHECK-ENCODING: encoding: [0xcb,0xf0,0x23,0x5f,0x1b]
fnmsub.d f12, [r14], f15
; CHECK-INST: fnmsub.d	f12, [r14], f15
; CHECK-ENCODING: encoding: [0xcb,0xf0,0x33,0x3f,0x9e]

facosa.s f0, f1
; CHECK-INST: facosa.s	f0, f1
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x00,0x80]
fasina.d f1, f2
; CHECK-INST: fasina.d	f1, f2
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x81,0x11]
fatana.s f2, f3
; CHECK-INST: fatana.s	f2, f3
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x01,0xa2]
fatanha.d f3, f4
; CHECK-INST: fatanha.d	f3, f4
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x82,0x33]
fcosa.s f4, f5
; CHECK-INST: fcosa.s	f4, f5
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x02,0xc4]
fcosha.d f5, f6
; CHECK-INST: fcosha.d	f5, f6
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x83,0x55]
fetoxa.s f6, f7
; CHECK-INST: fetoxa.s	f6, f7
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x03,0xe6]
fetoxm1a.d f7, f8
; CHECK-INST: fetoxm1a.d	f7, f8
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x84,0x77]
flog10a.s f8, f9
; CHECK-INST: flog10a.s	f8, f9
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x0c,0x88]
flog2a.d f9, f10
; CHECK-INST: flog2a.d	f9, f10
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x8d,0x19]
flogna.s f10, f11
; CHECK-INST: flogna.s	f10, f11
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x0d,0xaa]
flognp1a.d f11, f12
; CHECK-INST: flognp1a.d	f11, f12
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x8e,0x3b]
fsina.s f12, f13
; CHECK-INST: fsina.s	f12, f13
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x0e,0xcc]
fsinha.d f13, f14
; CHECK-INST: fsinha.d	f13, f14
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x8f,0x6d]
ftana.s f14, f15
; CHECK-INST: ftana.s	f14, f15
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x0f,0xfe]
ftanha.d f15, f0
; CHECK-INST: ftanha.d	f15, f0
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x90,0x0f]
ftentoxa.s f0, f15
; CHECK-INST: ftentoxa.s	f0, f15
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x17,0x90]
ftwotoxa.d f15, f1
; CHECK-INST: ftwotoxa.d	f15, f1
; CHECK-ENCODING: encoding: [0xc7,0xdc,0x90,0xaf]
fsincosa.s f1, f2, f3
; CHECK-INST: fsincosa.s	f1, f2, f3
; CHECK-ENCODING: encoding: [0xcb,0xf0,0x24,0x41,0x03]
fsincosa.d f4, f5, f6
; CHECK-INST: fsincosa.d	f4, f5, f6
; CHECK-ENCODING: encoding: [0xcb,0xf0,0x35,0x02,0x86]

fpushp 0
; CHECK-INST: fpushp	0
; CHECK-ENCODING: encoding: [0x70]
fpushp 3
; CHECK-INST: fpushp	3
; CHECK-ENCODING: encoding: [0x73]
fpopp 3
; CHECK-INST: fpopp	3
; CHECK-ENCODING: encoding: [0x7b]
fpopp 0
; CHECK-INST: fpopp	0
; CHECK-ENCODING: encoding: [0x78]
