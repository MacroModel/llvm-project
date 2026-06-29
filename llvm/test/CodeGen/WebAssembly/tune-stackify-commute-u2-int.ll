; REQUIRES: asserts
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=u2-sysv -debug-only=wasm-reg-stackify -o /dev/null 2>&1 | FileCheck %s --check-prefix=U2-SYSV
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=uwvm2 -debug-only=wasm-reg-stackify -o /dev/null 2>&1 | FileCheck %s --check-prefix=UWVM2
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=u2-aapcs64 -debug-only=wasm-reg-stackify -o /dev/null 2>&1 | FileCheck %s --check-prefix=U2-AAPCS64

target triple = "wasm32-unknown-unknown"

define i32 @profile_commute_u2_mild(i32 %a, i32 %b, i32 %c, i32 %d) {
entry:
  %x = add i32 %a, %b
  %t0 = add i32 %c, 1
  %t1 = add i32 %d, 2
  %t2 = mul i32 %t0, %t1
  %t3 = add i32 %t2, %c
  %t4 = mul i32 %t3, %d
  %y = mul i32 %x, %t4
  %r = add i32 %y, %t2
  ret i32 %r
}

define i32 @profile_commute_u2_severe(i32 %a, i32 %b, i32 %c, i32 %d,
                                      i32 %e, i32 %f) {
entry:
  %x = add i32 %a, %b
  %u = add i32 %e, %f
  %t0 = add i32 %c, 1
  %t1 = add i32 %d, 2
  %t2 = mul i32 %t0, %t1
  %t3 = add i32 %t2, %c
  %t4 = mul i32 %t3, %d
  %w = mul i32 %x, %u
  %y = mul i32 %w, %t4
  %r = add i32 %y, %t2
  ret i32 %r
}

; U2-SYSV-LABEL: Function: profile_commute_u2_mild
; U2-SYSV: wasm-tune-stackify: profile-commute tune=u2-sysv
; U2-SYSV-SAME: before-peak-int=4
; U2-SYSV-SAME: after-peak-int=3
; U2-SYSV-SAME: before-overflow=1
; U2-SYSV-SAME: after-overflow=0

; U2-SYSV-LABEL: Function: profile_commute_u2_severe
; U2-SYSV: wasm-tune-stackify: profile-commute tune=u2-sysv
; U2-SYSV-SAME: before-peak-int=5
; U2-SYSV-SAME: after-peak-int=4
; U2-SYSV-SAME: before-overflow=2
; U2-SYSV-SAME: after-overflow=1

; U2-AAPCS64-NOT: profile-commute

; UWVM2-LABEL: Function: profile_commute_u2_mild
; UWVM2: wasm-tune-stackify: profile-commute tune=uwvm2
; UWVM2-SAME: before-peak-int=4
; UWVM2-SAME: after-peak-int=3
; UWVM2-SAME: before-overflow=1
; UWVM2-SAME: after-overflow=0

; UWVM2-LABEL: Function: profile_commute_u2_severe
; UWVM2: wasm-tune-stackify: profile-commute tune=uwvm2
; UWVM2-SAME: before-peak-int=5
; UWVM2-SAME: after-peak-int=4
; UWVM2-SAME: before-overflow=2
; UWVM2-SAME: after-overflow=1
