; RUN: llc -mtriple=bedrock -stop-after=finalize-isel < %s | FileCheck %s

declare i64 @llvm.bedrock.read.status()
declare void @llvm.bedrock.write.status(i64)
declare i64 @llvm.bedrock.read.segment.register(i32 immarg)
declare i64 @llvm.bedrock.read.code.segment()
declare void @llvm.bedrock.write.segment.register(i32 immarg, i64)

; CHECK-LABEL: name: status_state
; CHECK: %{{[0-9]+}}:gpr64 = BEDROCK_RDSTATUS implicit $status
; CHECK: BEDROCK_WRSTATUS {{(killed )?}}%{{[0-9]+}}, implicit-def {{(dead )?}}$status
define void @status_state() {
  %status = call i64 @llvm.bedrock.read.status()
  call void @llvm.bedrock.write.status(i64 %status)
  ret void
}

; CHECK-LABEL: name: read_segment_state
; CHECK: BEDROCK_RDSEG 0, implicit $ds
; CHECK: BEDROCK_RDSEG 1, implicit $ss
; CHECK: BEDROCK_RDSEG 2, implicit $gs0
; CHECK: BEDROCK_RDSEG 3, implicit $gs1
; CHECK: BEDROCK_RDSEG 4, implicit $gs2
; CHECK: BEDROCK_RDSEG 5, implicit $gs3
; CHECK: BEDROCK_RDSEG 6, implicit $gs4
; CHECK: BEDROCK_RDSEG 7, implicit $gs5
; CHECK: BEDROCK_RDSEG_CS implicit $cs
define void @read_segment_state() {
  %ds = call i64 @llvm.bedrock.read.segment.register(i32 0)
  %ss = call i64 @llvm.bedrock.read.segment.register(i32 1)
  %gs0 = call i64 @llvm.bedrock.read.segment.register(i32 2)
  %gs1 = call i64 @llvm.bedrock.read.segment.register(i32 3)
  %gs2 = call i64 @llvm.bedrock.read.segment.register(i32 4)
  %gs3 = call i64 @llvm.bedrock.read.segment.register(i32 5)
  %gs4 = call i64 @llvm.bedrock.read.segment.register(i32 6)
  %gs5 = call i64 @llvm.bedrock.read.segment.register(i32 7)
  %cs = call i64 @llvm.bedrock.read.code.segment()
  ret void
}

; CHECK-LABEL: name: write_segment_state
; CHECK: BEDROCK_WRSEG %{{[0-9]+}}, 0, implicit-def $ds
; CHECK: BEDROCK_WRSEG %{{[0-9]+}}, 1, implicit-def $ss
; CHECK: BEDROCK_WRSEG %{{[0-9]+}}, 2, implicit-def $gs0
; CHECK: BEDROCK_WRSEG %{{[0-9]+}}, 3, implicit-def $gs1
; CHECK: BEDROCK_WRSEG %{{[0-9]+}}, 4, implicit-def $gs2
; CHECK: BEDROCK_WRSEG %{{[0-9]+}}, 5, implicit-def $gs3
; CHECK: BEDROCK_WRSEG %{{[0-9]+}}, 6, implicit-def $gs4
; CHECK: BEDROCK_WRSEG %{{[0-9]+}}, 7, implicit-def $gs5
define void @write_segment_state(i64 %value) {
  call void @llvm.bedrock.write.segment.register(i32 0, i64 %value)
  call void @llvm.bedrock.write.segment.register(i32 1, i64 %value)
  call void @llvm.bedrock.write.segment.register(i32 2, i64 %value)
  call void @llvm.bedrock.write.segment.register(i32 3, i64 %value)
  call void @llvm.bedrock.write.segment.register(i32 4, i64 %value)
  call void @llvm.bedrock.write.segment.register(i32 5, i64 %value)
  call void @llvm.bedrock.write.segment.register(i32 6, i64 %value)
  call void @llvm.bedrock.write.segment.register(i32 7, i64 %value)
  ret void
}
