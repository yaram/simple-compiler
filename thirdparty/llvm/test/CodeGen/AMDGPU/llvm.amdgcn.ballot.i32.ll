; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc -march=amdgcn -mcpu=gfx1010 -mattr=+wavefrontsize32,-wavefrontsize64 < %s | FileCheck %s

declare i32 @llvm.amdgcn.ballot.i32(i1)

; Test ballot(0)

define i32 @test0() {
; CHECK-LABEL: test0:
; CHECK:       ; %bb.0:
; CHECK-NEXT:    s_waitcnt vmcnt(0) expcnt(0) lgkmcnt(0)
; CHECK-NEXT:    s_waitcnt_vscnt null, 0x0
; CHECK-NEXT:    v_mov_b32_e32 v0, 0
; CHECK-NEXT:    ; implicit-def: $vcc_hi
; CHECK-NEXT:    s_setpc_b64 s[30:31]
  %ballot = call i32 @llvm.amdgcn.ballot.i32(i1 0)
  ret i32 %ballot
}

; Test ballot(1)

define i32 @test1() {
; CHECK-LABEL: test1:
; CHECK:       ; %bb.0:
; CHECK-NEXT:    s_waitcnt vmcnt(0) expcnt(0) lgkmcnt(0)
; CHECK-NEXT:    s_waitcnt_vscnt null, 0x0
; CHECK-NEXT:    v_mov_b32_e32 v0, exec_lo
; CHECK-NEXT:    ; implicit-def: $vcc_hi
; CHECK-NEXT:    s_setpc_b64 s[30:31]
  %ballot = call i32 @llvm.amdgcn.ballot.i32(i1 1)
  ret i32 %ballot
}

; Test ballot of a non-comparison operation

define i32 @test2(i32 %x) {
; CHECK-LABEL: test2:
; CHECK:       ; %bb.0:
; CHECK-NEXT:    s_waitcnt vmcnt(0) expcnt(0) lgkmcnt(0)
; CHECK-NEXT:    s_waitcnt_vscnt null, 0x0
; CHECK-NEXT:    v_and_b32_e32 v0, 1, v0
; CHECK-NEXT:    ; implicit-def: $vcc_hi
; CHECK-NEXT:    v_cmp_ne_u32_e64 s4, 0, v0
; CHECK-NEXT:    v_mov_b32_e32 v0, s4
; CHECK-NEXT:    s_setpc_b64 s[30:31]
  %trunc = trunc i32 %x to i1
  %ballot = call i32 @llvm.amdgcn.ballot.i32(i1 %trunc)
  ret i32 %ballot
}

; Test ballot of comparisons

define i32 @test3(i32 %x, i32 %y) {
; CHECK-LABEL: test3:
; CHECK:       ; %bb.0:
; CHECK-NEXT:    s_waitcnt vmcnt(0) expcnt(0) lgkmcnt(0)
; CHECK-NEXT:    s_waitcnt_vscnt null, 0x0
; CHECK-NEXT:    v_cmp_eq_u32_e64 s4, v0, v1
; CHECK-NEXT:    ; implicit-def: $vcc_hi
; CHECK-NEXT:    v_mov_b32_e32 v0, s4
; CHECK-NEXT:    s_setpc_b64 s[30:31]
  %cmp = icmp eq i32 %x, %y
  %ballot = call i32 @llvm.amdgcn.ballot.i32(i1 %cmp)
  ret i32 %ballot
}

define i32 @test4(i32 %x) {
; CHECK-LABEL: test4:
; CHECK:       ; %bb.0:
; CHECK-NEXT:    s_waitcnt vmcnt(0) expcnt(0) lgkmcnt(0)
; CHECK-NEXT:    s_waitcnt_vscnt null, 0x0
; CHECK-NEXT:    v_cmp_lt_i32_e64 s4, 0x62, v0
; CHECK-NEXT:    ; implicit-def: $vcc_hi
; CHECK-NEXT:    v_mov_b32_e32 v0, s4
; CHECK-NEXT:    s_setpc_b64 s[30:31]
  %cmp = icmp sge i32 %x, 99
  %ballot = call i32 @llvm.amdgcn.ballot.i32(i1 %cmp)
  ret i32 %ballot
}

define i32 @test5(float %x, float %y) {
; CHECK-LABEL: test5:
; CHECK:       ; %bb.0:
; CHECK-NEXT:    s_waitcnt vmcnt(0) expcnt(0) lgkmcnt(0)
; CHECK-NEXT:    s_waitcnt_vscnt null, 0x0
; CHECK-NEXT:    v_cmp_gt_f32_e64 s4, v0, v1
; CHECK-NEXT:    ; implicit-def: $vcc_hi
; CHECK-NEXT:    v_mov_b32_e32 v0, s4
; CHECK-NEXT:    s_setpc_b64 s[30:31]
  %cmp = fcmp ogt float %x, %y
  %ballot = call i32 @llvm.amdgcn.ballot.i32(i1 %cmp)
  ret i32 %ballot
}