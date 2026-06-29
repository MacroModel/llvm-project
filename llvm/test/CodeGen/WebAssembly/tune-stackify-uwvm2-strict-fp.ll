; REQUIRES: asserts
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=uwvm2 -debug-only=wasm-reg-stackify -wasm-tune-shape-dump -o /dev/null 2>&1 | FileCheck %s

target triple = "wasm32-unknown-unknown"

@A = internal global [16 x double] zeroinitializer, align 16
@B = internal global [16 x double] zeroinitializer, align 16
@C = internal global double 0.000000e+00, align 8

define double @strict_dot6() {
entry:
  %c = load volatile double, ptr @C, align 8
  %a0p = getelementptr [16 x double], ptr @A, i32 0, i32 0
  %b0p = getelementptr [16 x double], ptr @B, i32 0, i32 1
  %a1p = getelementptr [16 x double], ptr @A, i32 0, i32 2
  %b1p = getelementptr [16 x double], ptr @B, i32 0, i32 3
  %a2p = getelementptr [16 x double], ptr @A, i32 0, i32 4
  %b2p = getelementptr [16 x double], ptr @B, i32 0, i32 5
  %a3p = getelementptr [16 x double], ptr @A, i32 0, i32 6
  %b3p = getelementptr [16 x double], ptr @B, i32 0, i32 7
  %a4p = getelementptr [16 x double], ptr @A, i32 0, i32 8
  %b4p = getelementptr [16 x double], ptr @B, i32 0, i32 9
  %a5p = getelementptr [16 x double], ptr @A, i32 0, i32 10
  %b5p = getelementptr [16 x double], ptr @B, i32 0, i32 11
  %a0 = load volatile double, ptr %a0p, align 8
  %b0 = load volatile double, ptr %b0p, align 8
  %a1 = load volatile double, ptr %a1p, align 8
  %b1 = load volatile double, ptr %b1p, align 8
  %a2 = load volatile double, ptr %a2p, align 8
  %b2 = load volatile double, ptr %b2p, align 8
  %a3 = load volatile double, ptr %a3p, align 8
  %b3 = load volatile double, ptr %b3p, align 8
  %a4 = load volatile double, ptr %a4p, align 8
  %b4 = load volatile double, ptr %b4p, align 8
  %a5 = load volatile double, ptr %a5p, align 8
  %b5 = load volatile double, ptr %b5p, align 8
  %p0 = fmul double %a0, %b0
  %p1 = fmul double %a1, %b1
  %p2 = fmul double %a2, %b2
  %p3 = fmul double %a3, %b3
  %p4 = fmul double %a4, %b4
  %p5 = fmul double %a5, %b5
  %s0 = fadd double %p0, %c
  %s1 = fadd double %p1, %s0
  %s2 = fadd double %p2, %s1
  %s3 = fadd double %p3, %s2
  %s4 = fadd double %p4, %s3
  %s5 = fadd double %p5, %s4
  ret double %s5
}

define double @fast_dot6() {
entry:
  %c = load volatile double, ptr @C, align 8
  %a0p = getelementptr [16 x double], ptr @A, i32 0, i32 0
  %b0p = getelementptr [16 x double], ptr @B, i32 0, i32 1
  %a1p = getelementptr [16 x double], ptr @A, i32 0, i32 2
  %b1p = getelementptr [16 x double], ptr @B, i32 0, i32 3
  %a2p = getelementptr [16 x double], ptr @A, i32 0, i32 4
  %b2p = getelementptr [16 x double], ptr @B, i32 0, i32 5
  %a3p = getelementptr [16 x double], ptr @A, i32 0, i32 6
  %b3p = getelementptr [16 x double], ptr @B, i32 0, i32 7
  %a4p = getelementptr [16 x double], ptr @A, i32 0, i32 8
  %b4p = getelementptr [16 x double], ptr @B, i32 0, i32 9
  %a5p = getelementptr [16 x double], ptr @A, i32 0, i32 10
  %b5p = getelementptr [16 x double], ptr @B, i32 0, i32 11
  %a0 = load volatile double, ptr %a0p, align 8
  %b0 = load volatile double, ptr %b0p, align 8
  %a1 = load volatile double, ptr %a1p, align 8
  %b1 = load volatile double, ptr %b1p, align 8
  %a2 = load volatile double, ptr %a2p, align 8
  %b2 = load volatile double, ptr %b2p, align 8
  %a3 = load volatile double, ptr %a3p, align 8
  %b3 = load volatile double, ptr %b3p, align 8
  %a4 = load volatile double, ptr %a4p, align 8
  %b4 = load volatile double, ptr %b4p, align 8
  %a5 = load volatile double, ptr %a5p, align 8
  %b5 = load volatile double, ptr %b5p, align 8
  %p0 = fmul reassoc double %a0, %b0
  %p1 = fmul reassoc double %a1, %b1
  %p2 = fmul reassoc double %a2, %b2
  %p3 = fmul reassoc double %a3, %b3
  %p4 = fmul reassoc double %a4, %b4
  %p5 = fmul reassoc double %a5, %b5
  %s0 = fadd reassoc double %p0, %c
  %s1 = fadd reassoc double %p1, %s0
  %s2 = fadd reassoc double %p2, %s1
  %s3 = fadd reassoc double %p3, %s2
  %s4 = fadd reassoc double %p4, %s3
  %s5 = fadd reassoc double %p5, %s4
  ret double %s5
}

; CHECK-LABEL: Function: strict_dot6
; CHECK: reason=uwvm2-strict-fp-accum
; CHECK: wasm-tune-shape: function=strict_dot6 tune=uwvm2
; CHECK-SAME: uwvm2-strict-fp-accum={{[1-9][0-9]*}}

; CHECK-LABEL: Function: fast_dot6
; CHECK-NOT: reason=uwvm2-strict-fp-accum
; CHECK: wasm-tune-shape: function=fast_dot6 tune=uwvm2
; CHECK-SAME: uwvm2-strict-fp-accum=0
