; RUN: llc -mtriple=bedrock < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -filetype=obj < %s | llvm-objdump -d - | FileCheck %s --check-prefix=DIS
; RUN: llc -mtriple=bedrock -stop-after=finalize-isel < %s | FileCheck %s --check-prefix=MIR

declare void @llvm.bedrock.write.status(i64)
declare i64 @llvm.bedrock.read.control.register(i32 immarg)
declare void @llvm.bedrock.write.control.register(i32 immarg, i64)
declare i64 @llvm.bedrock.read.segment.register(i32 immarg)
declare i64 @llvm.bedrock.read.code.segment()
declare void @llvm.bedrock.write.segment.register(i32 immarg, i64)
declare void @llvm.bedrock.flush.dcache(ptr, i64)
declare void @llvm.bedrock.invalidate.dcache(ptr, i64)
declare void @llvm.bedrock.invalidate.icache(ptr, i64)
declare void @llvm.bedrock.writeback.dcache(ptr, i64)
declare void @llvm.bedrock.sync.cache(ptr, i64)
declare void @llvm.bedrock.invalidate.tlb()
declare void @llvm.bedrock.invalidate.page(ptr)
declare void @llvm.bedrock.invalidate.asid(i32 immarg)
declare void @llvm.bedrock.switch.page.table(i64)
declare void @llvm.bedrock.switch.page.table.asid(i64, i64)
declare { i64, i64 } @llvm.bedrock.virtual.to.physical(i64)
declare { i64, i64 } @llvm.bedrock.page.table.query(i32 immarg, i64)
declare void @llvm.bedrock.save.processor.state(ptr)
declare void @llvm.bedrock.restore.processor.state(ptr)

; CHECK-LABEL: sysreg:
; CHECK: wrstatus
; CHECK: rdcr 4660
; CHECK: wrcr {{.*}}, 22136
; CHECK: rdseg ds
; CHECK: rdseg cs
; CHECK: wrseg {{.*}}, gs4
; DIS: wrstatus
; DIS: rdcr 4660
; DIS: wrcr {{.*}}, 22136
; DIS: rdseg ds
; DIS: rdseg cs
; DIS: wrseg {{.*}}, gs4
define i64 @sysreg(i64 %value) {
  call void @llvm.bedrock.write.status(i64 %value)
  %cr = call i64 @llvm.bedrock.read.control.register(i32 4660)
  call void @llvm.bedrock.write.control.register(i32 22136, i64 %value)
  %segment = call i64 @llvm.bedrock.read.segment.register(i32 0)
  %code_segment = call i64 @llvm.bedrock.read.code.segment()
  call void @llvm.bedrock.write.segment.register(i32 6, i64 %value)
  %sum = add i64 %cr, %segment
  %result = add i64 %sum, %code_segment
  ret i64 %result
}

; CHECK-LABEL: cache:
; CHECK: flshdcache
; CHECK: invdcache
; CHECK: invicache
; CHECK: wrbkdcache
; CHECK: synccache
; DIS: flshdcache
; DIS: invdcache
; DIS: invicache
; DIS: wrbkdcache
; DIS: synccache
define void @cache(ptr %address) {
  call void @llvm.bedrock.flush.dcache(ptr %address, i64 1)
  call void @llvm.bedrock.invalidate.dcache(ptr %address, i64 1)
  call void @llvm.bedrock.invalidate.icache(ptr %address, i64 1)
  call void @llvm.bedrock.writeback.dcache(ptr %address, i64 1)
  call void @llvm.bedrock.sync.cache(ptr %address, i64 1)
  ret void
}

; CHECK-LABEL: mmu:
; CHECK: invtlb
; CHECK: invpage
; CHECK: invasid 9
; CHECK: swpt
; CHECK: swpta
; CHECK: vtop
; CHECK-NEXT: rdflags
; CHECK: ptquery 3
; CHECK-NEXT: rdflags
; DIS: invtlb
; DIS: invpage
; DIS: invasid 9
; DIS: swpt
; DIS: swpta
; DIS: vtop
; DIS-NEXT: {{.*}}rdflags
; DIS: ptquery 3
; DIS-NEXT: {{.*}}rdflags
define i64 @mmu(i64 %address) {
  call void @llvm.bedrock.invalidate.tlb()
  %pointer = inttoptr i64 %address to ptr
  call void @llvm.bedrock.invalidate.page(ptr %pointer)
  call void @llvm.bedrock.invalidate.asid(i32 9)
  call void @llvm.bedrock.switch.page.table(i64 %address)
  call void @llvm.bedrock.switch.page.table.asid(i64 %address, i64 10)
  %vtop = call { i64, i64 } @llvm.bedrock.virtual.to.physical(i64 %address)
  %value1 = extractvalue { i64, i64 } %vtop, 0
  %query = call { i64, i64 } @llvm.bedrock.page.table.query(i32 3, i64 %address)
  %value2 = extractvalue { i64, i64 } %query, 0
  %result = add i64 %value1, %value2
  ret i64 %result
}

; CHECK-LABEL: state:
; CHECK: save
; CHECK: restore
; DIS: save
; DIS: restore
; MIR: BEDROCK_RESTORE
; MIR-SAME: implicit-def dead $r0
; MIR-SAME: implicit-def dead $r1
; MIR-SAME: implicit-def dead $r2
; MIR-SAME: implicit-def dead $r3
; MIR-SAME: implicit-def dead $r4
; MIR-SAME: implicit-def dead $r5
; MIR-SAME: implicit-def dead $r6
; MIR-SAME: implicit-def dead $r7
; MIR-SAME: implicit-def dead $r8
; MIR-SAME: implicit-def dead $r9
; MIR-SAME: implicit-def dead $r10
; MIR-SAME: implicit-def dead $r11
; MIR-SAME: implicit-def dead $r12
; MIR-SAME: implicit-def dead $r13
; MIR-SAME: implicit-def dead $r14
; MIR-SAME: implicit-def dead $r15
; MIR-SAME: implicit-def dead $flags
; MIR-SAME: implicit-def dead $status
; MIR-SAME: implicit-def dead $gs0
; MIR-SAME: implicit-def dead $gs1
; MIR-SAME: implicit-def dead $gs2
; MIR-SAME: implicit-def dead $gs3
; MIR-SAME: implicit-def dead $gs4
; MIR-SAME: implicit-def dead $gs5
; MIR-SAME: implicit-def dead $f0
; MIR-SAME: implicit-def dead $f1
; MIR-SAME: implicit-def dead $f2
; MIR-SAME: implicit-def dead $f3
; MIR-SAME: implicit-def dead $f4
; MIR-SAME: implicit-def dead $f5
; MIR-SAME: implicit-def dead $f6
; MIR-SAME: implicit-def dead $f7
; MIR-SAME: implicit-def dead $f8
; MIR-SAME: implicit-def dead $f9
; MIR-SAME: implicit-def dead $f10
; MIR-SAME: implicit-def dead $f11
; MIR-SAME: implicit-def dead $f12
; MIR-SAME: implicit-def dead $f13
; MIR-SAME: implicit-def dead $f14
; MIR-SAME: implicit-def dead $f15
; MIR-SAME: implicit-def dead $fstatus
; MIR-SAME: implicit-def dead $fflags
define void @state(ptr %area) {
  call void @llvm.bedrock.save.processor.state(ptr %area)
  call void @llvm.bedrock.restore.processor.state(ptr %area)
  ret void
}
