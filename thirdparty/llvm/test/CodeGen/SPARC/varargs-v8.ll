; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc < %s -mtriple=sparc -disable-sparc-leaf-proc | FileCheck %s

define i32 @test(i32 %a, i8* %va) nounwind {
; CHECK-LABEL: test:
; CHECK:       ! %bb.0: ! %entry
; CHECK-NEXT:    save %sp, -96, %sp
; CHECK-NEXT:    add %i1, 8, %i0
; CHECK-NEXT:    st %i0, [%fp+-4]
; CHECK-NEXT:    ld [%i1+4], %i0
; CHECK-NEXT:    add %i1, 12, %i2
; CHECK-NEXT:    st %i2, [%fp+-4]
; CHECK-NEXT:    ld [%i1+8], %i1
; CHECK-NEXT:    ret
; CHECK-NEXT:    restore %i1, %i0, %o0
entry:
  %va.addr = alloca i8*, align 4
  store i8* %va, i8** %va.addr, align 4
  %0 = va_arg i8** %va.addr, i64
  %conv1 = trunc i64 %0 to i32
  %1 = va_arg i8** %va.addr, i32
  %add3 = add nsw i32 %1, %conv1
  ret i32 %add3
}
