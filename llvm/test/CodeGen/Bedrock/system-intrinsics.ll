; RUN: llc -mtriple=bedrock -mattr=+virtaccel < %s | FileCheck %s
; RUN: llc -mtriple=bedrock -mattr=+virtaccel -filetype=obj < %s | llvm-objdump -d - | FileCheck %s --check-prefix=DIS
; RUN: llc -mtriple=bedrock -mattr=+virtaccel -stop-after=finalize-isel < %s | FileCheck %s --check-prefix=MIR

declare void @llvm.bedrock.write.status(i64)
declare i64 @llvm.bedrock.read.control.register(i32 immarg)
declare void @llvm.bedrock.write.control.register(i32 immarg, i64)
declare i64 @llvm.bedrock.read.segment.register(i32 immarg)
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
declare i64 @llvm.bedrock.encode.instruction(ptr, i64, i64, i64, i64)

; CHECK-LABEL: sysreg:
; CHECK: wrstatus
; CHECK: rdcr 4660
; CHECK: wrcr {{.*}}, 22136
; CHECK: rdseg ds
; CHECK: wrseg {{.*}}, gs4
; DIS: wrstatus
; DIS: rdcr 4660
; DIS: wrcr {{.*}}, 22136
; DIS: rdseg ds
; DIS: wrseg {{.*}}, gs4
define i64 @sysreg(i64 %value) {
  call void @llvm.bedrock.write.status(i64 %value)
  %cr = call i64 @llvm.bedrock.read.control.register(i32 4660)
  call void @llvm.bedrock.write.control.register(i32 22136, i64 %value)
  %segment = call i64 @llvm.bedrock.read.segment.register(i32 1)
  call void @llvm.bedrock.write.segment.register(i32 7, i64 %value)
  %result = add i64 %cr, %segment
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
; MIR: BEDROCK_RESTORE {{.*}}implicit-def dead $r0{{.*}}implicit-def dead $f15{{.*}}implicit-def dead $fflags
define void @state(ptr %area) {
  call void @llvm.bedrock.save.processor.state(ptr %area)
  call void @llvm.bedrock.restore.processor.state(ptr %area)
  ret void
}

; CHECK-LABEL: encode:
; CHECK: encinst
; DIS: encinst
; MIR: :r0only = COPY
; MIR: :r1only = COPY
; MIR: :r2only = COPY
; MIR: :r3only = COPY
; MIR: :r0only = BEDROCK_ENCINST
define i64 @encode(ptr %destination, ptr %descriptor) #0 {
  %control = load i64, ptr %descriptor, align 8
  %p1 = getelementptr i64, ptr %descriptor, i64 1
  %operand1 = load i64, ptr %p1, align 8
  %p2 = getelementptr i64, ptr %descriptor, i64 2
  %operand2 = load i64, ptr %p2, align 8
  %p3 = getelementptr i64, ptr %descriptor, i64 3
  %operand3 = load i64, ptr %p3, align 8
  %result = call i64 @llvm.bedrock.encode.instruction(
      ptr %destination, i64 %control, i64 %operand1, i64 %operand2,
      i64 %operand3)
  ret i64 %result
}

attributes #0 = { "target-features"="+virtaccel" }
