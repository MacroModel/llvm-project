; REQUIRES: asserts
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=m3 -debug-only=wasm-reg-stackify -o /dev/null 2>&1 | FileCheck %s --check-prefix=M3

target triple = "wasm32-unknown-unknown"

define double @dot7_plus_fast(double %z, double %a0, double %b0,
                              double %a1, double %b1, double %a2,
                              double %b2, double %a3, double %b3,
                              double %a4, double %b4, double %a5,
                              double %b5, double %a6, double %b6) {
entry:
  %p0 = fmul reassoc double %a0, %b0
  %p1 = fmul reassoc double %a1, %b1
  %p2 = fmul reassoc double %a2, %b2
  %p3 = fmul reassoc double %a3, %b3
  %p4 = fmul reassoc double %a4, %b4
  %p5 = fmul reassoc double %a5, %b5
  %p6 = fmul reassoc double %a6, %b6
  %s01 = fadd reassoc double %p0, %p1
  %s012 = fadd reassoc double %s01, %p2
  %s0123 = fadd reassoc double %s012, %p3
  %s01234 = fadd reassoc double %s0123, %p4
  %s012345 = fadd reassoc double %s01234, %p5
  %s0123456 = fadd reassoc double %s012345, %p6
  %r = fadd reassoc double %s0123456, %z
  ret double %r
}

; M3-LABEL: Function: dot7_plus_fast
; M3-NOT: wasm-tune-stackify: choose-action
