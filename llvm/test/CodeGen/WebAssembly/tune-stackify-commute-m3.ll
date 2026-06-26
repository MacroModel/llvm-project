; REQUIRES: asserts
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=m3 -debug-only=wasm-reg-stackify -o /dev/null 2>&1 | FileCheck %s --check-prefix=M3
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=generic -debug-only=wasm-reg-stackify -o /dev/null 2>&1 | FileCheck %s --check-prefix=GENERIC
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=m3 -wasm-tune-stackify=false -debug-only=wasm-reg-stackify -o /dev/null 2>&1 | FileCheck %s --check-prefix=DISABLED

target triple = "wasm32-unknown-unknown"

define double @profile_commute_m3(double %a, double %b, double %c, double %d) {
entry:
  %x = fadd double %a, %b
  %t0 = fadd double %c, 1.0
  %t1 = fadd double %d, 2.0
  %t2 = fmul double %t0, %t1
  %t3 = fadd double %t2, %c
  %t4 = fmul double %t3, %d
  %y = fmul double %x, %t4
  %r = fadd double %y, %t2
  ret double %r
}

; M3: wasm-tune-stackify: profile-commute tune=m3
; M3-SAME: before-peak-fp=4
; M3-SAME: after-peak-fp=3
; M3-SAME: before-overflow=3
; M3-SAME: after-overflow=2

; GENERIC-NOT: profile-commute
; DISABLED-NOT: profile-commute
