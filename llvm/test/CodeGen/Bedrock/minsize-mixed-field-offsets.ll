; RUN: llc -mtriple=bedrock -O1 -verify-machineinstrs < %s | FileCheck %s

define i32 @mixed_field_offsets(ptr %ip, ptr %lp, i32 %n) minsize optsize {
; CHECK-LABEL: mixed_field_offsets:
; CHECK:       MOV.L [A0 + 28], D3
; CHECK-NEXT:  ADD.Q D1, [A1 + 40]
; CHECK-NOT:   MOV.Q {{.*}}, [A1 + 40]
; CHECK:       RET
entry:
  %arrayidx = getelementptr inbounds nuw i8, ptr %ip, i64 4
  %a = load i32, ptr %arrayidx, align 4, !tbaa !2
  %arrayidx1 = getelementptr inbounds nuw i8, ptr %ip, i64 28
  %b = load i32, ptr %arrayidx1, align 4, !tbaa !2
  %arrayidx2 = getelementptr inbounds nuw i8, ptr %lp, i64 16
  %x = load i64, ptr %arrayidx2, align 8, !tbaa !6
  %arrayidx3 = getelementptr inbounds nuw i8, ptr %lp, i64 40
  %y = load i64, ptr %arrayidx3, align 8, !tbaa !6
  %conv = sext i32 %a to i64
  %conv5 = sext i32 %n to i64
  %add = add nsw i64 %conv, %conv5
  %add4 = add i64 %add, %x
  %add6 = add i64 %add4, %y
  store i64 %add6, ptr %arrayidx3, align 8, !tbaa !6
  %add8 = add i32 %a, %n
  %add9 = add i32 %add8, %b
  store i32 %add9, ptr %arrayidx1, align 4, !tbaa !2
  %base = load i32, ptr %ip, align 4, !tbaa !2
  %ret = add nsw i32 %base, %add9
  ret i32 %ret
}

!llvm.module.flags = !{!0}
!llvm.errno.tbaa = !{!2}

!0 = !{i32 1, !"wchar_size", i32 4}
!2 = !{!3, !3, i64 0}
!3 = !{!"int", !4, i64 0}
!4 = !{!"omnipotent char", !5, i64 0}
!5 = !{!"Simple C/C++ TBAA"}
!6 = !{!7, !7, i64 0}
!7 = !{!"long", !4, i64 0}
