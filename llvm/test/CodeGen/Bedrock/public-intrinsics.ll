; RUN: llc -mtriple=bedrock < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -filetype=obj < %s | llvm-objdump -d - | FileCheck %s --check-prefix=DIS

declare i64 @llvm.bedrock.cpuid(i64)
declare i64 @llvm.bedrock.read.status()
declare i64 @llvm.bedrock.rdpmc(i32 immarg)
declare void @llvm.bedrock.breakpoint()
declare void @llvm.bedrock.trace(i32 immarg)
declare void @llvm.bedrock.yield()
declare void @llvm.bedrock.wait()
declare void @llvm.bedrock.read.fence()
declare void @llvm.bedrock.write.fence()
declare void @llvm.bedrock.address.fence()
declare void @llvm.bedrock.nontemporal.store.u8(ptr, i64)
declare void @llvm.bedrock.nontemporal.store.u16(ptr, i64)
declare void @llvm.bedrock.nontemporal.store.u32(ptr, i64)
declare void @llvm.bedrock.nontemporal.store.u64(ptr, i64)
declare i64 @llvm.bedrock.clmul.u8(i64, i64)
declare i64 @llvm.bedrock.clmul.u16(i64, i64)
declare i64 @llvm.bedrock.clmul.u32(i64, i64)
declare i64 @llvm.bedrock.clmul.u64(i64, i64)
declare i64 @llvm.bedrock.read.fstatus()
declare void @llvm.bedrock.write.fstatus(i64)
declare i64 @llvm.bedrock.read.fflags()
declare void @llvm.bedrock.write.fflags(i64)
declare i64 @llvm.bedrock.fclass.f32(float)
declare i64 @llvm.bedrock.fclass.f64(double)

; CHECK-LABEL: core:
; CHECK: trace 4660
; CHECK: bkpt
; CHECK: yield
; CHECK: wait
; CHECK: cpuid
; CHECK: rdpmc 22136
; CHECK: rdstatus
; DIS: trace 4660
; DIS: bkpt
; DIS: yield
; DIS: wait
; DIS: cpuid
; DIS: rdpmc 22136
; DIS: rdstatus
define i64 @core(i64 %selector) {
  call void @llvm.bedrock.trace(i32 4660)
  call void @llvm.bedrock.breakpoint()
  call void @llvm.bedrock.yield()
  call void @llvm.bedrock.wait()
  %cpuid = call i64 @llvm.bedrock.cpuid(i64 %selector)
  %pmc = call i64 @llvm.bedrock.rdpmc(i32 22136)
  %status = call i64 @llvm.bedrock.read.status()
  %a = add i64 %cpuid, %pmc
  %b = add i64 %a, %status
  ret i64 %b
}

; CHECK-LABEL: memory:
; CHECK: rfence
; CHECK: wfence
; CHECK: afence
; CHECK: movnt.b
; CHECK: movnt.w
; CHECK: movnt.l
; CHECK: movnt.q
; DIS: rfence
; DIS: wfence
; DIS: afence
; DIS: movnt.b
; DIS: movnt.w
; DIS: movnt.l
; DIS: movnt.q
define void @memory(ptr %p8, ptr %p16, ptr %p32, ptr %p64, i64 %value) {
  call void @llvm.bedrock.read.fence()
  call void @llvm.bedrock.write.fence()
  call void @llvm.bedrock.address.fence()
  call void @llvm.bedrock.nontemporal.store.u8(ptr %p8, i64 %value)
  call void @llvm.bedrock.nontemporal.store.u16(ptr %p16, i64 %value)
  call void @llvm.bedrock.nontemporal.store.u32(ptr %p32, i64 %value)
  call void @llvm.bedrock.nontemporal.store.u64(ptr %p64, i64 %value)
  ret void
}

; CHECK-LABEL: integer:
; CHECK-DAG: clmul.b
; CHECK-DAG: clmul.w
; CHECK-DAG: clmul.l
; CHECK-DAG: clmul.q
; DIS-DAG: clmul.b
; DIS-DAG: clmul.w
; DIS-DAG: clmul.l
; DIS-DAG: clmul.q
define i64 @integer(i64 %a, i64 %b) {
  %b8 = call i64 @llvm.bedrock.clmul.u8(i64 %a, i64 %b)
  %b16 = call i64 @llvm.bedrock.clmul.u16(i64 %a, i64 %b)
  %b32 = call i64 @llvm.bedrock.clmul.u32(i64 %a, i64 %b)
  %b64 = call i64 @llvm.bedrock.clmul.u64(i64 %a, i64 %b)
  %x = xor i64 %b8, %b16
  %y = xor i64 %b32, %b64
  %z = xor i64 %x, %y
  ret i64 %z
}

; CHECK-LABEL: fpu:
; CHECK-DAG: rdfstatus
; CHECK-DAG: rdfflags
; CHECK-DAG: wrfstatus
; CHECK-DAG: wrfflags
; CHECK-DAG: fclass.s
; CHECK-DAG: fclass.d
; DIS-DAG: rdfstatus
; DIS-DAG: rdfflags
; DIS-DAG: wrfstatus
; DIS-DAG: wrfflags
; DIS-DAG: fclass.s
; DIS-DAG: fclass.d
define i64 @fpu(float %single, double %double) #0 {
  %status = call i64 @llvm.bedrock.read.fstatus()
  %flags = call i64 @llvm.bedrock.read.fflags()
  call void @llvm.bedrock.write.fstatus(i64 %status)
  call void @llvm.bedrock.write.fflags(i64 %flags)
  %a = call i64 @llvm.bedrock.fclass.f32(float %single)
  %b = call i64 @llvm.bedrock.fclass.f64(double %double)
  %c = or i64 %a, %b
  ret i64 %c
}

attributes #0 = { "target-features"="+fpu" }
