; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=u2-sysv -wasm-tune-stackify=false -wasm-exec-pressure-dump -o /dev/null 2>&1 | FileCheck %s --check-prefix=U2
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=m3 -wasm-tune-stackify=false -wasm-exec-pressure-dump -o /dev/null 2>&1 | FileCheck %s --check-prefix=M3

target triple = "wasm32-unknown-unknown"

declare void @sink9(double, double, double, double, double, double, double,
                    double, double)

define void @fp_pressure_call(double %a0, double %a1, double %a2, double %a3,
                              double %a4, double %a5, double %a6, double %a7,
                              double %a8) {
entry:
  %p0 = fadd double %a0, 1.0
  %p1 = fadd double %a1, 2.0
  %p2 = fadd double %a2, 3.0
  %p3 = fadd double %a3, 4.0
  %p4 = fadd double %a4, 5.0
  %p5 = fadd double %a5, 6.0
  %p6 = fadd double %a6, 7.0
  %p7 = fadd double %a7, 8.0
  %p8 = fadd double %a8, 9.0
  call void @sink9(double %p0, double %p1, double %p2, double %p3, double %p4,
                   double %p5, double %p6, double %p7, double %p8)
  ret void
}

; U2: wasm-exec-pressure: function=fp_pressure_call tune=u2-sysv
; U2: profile: fp-ring=8 int-ring=3 register-ring=true m3-slot-provider=false
; U2: peak-fp=10 peak-int=0
; U2: cap-overflow-fp=2 cap-overflow-int=0
; U2: estimated-spills=2 estimated-fills=2
; U2: function-score-sum=85

; M3: wasm-exec-pressure: function=fp_pressure_call tune=m3
; M3: profile: fp-ring=1 int-ring=1 register-ring=false m3-slot-provider=true
; M3: peak-fp=10 peak-int=0
; M3: cap-overflow-fp=9 cap-overflow-int=0
; M3: estimated-spills=0 estimated-fills=0
; M3: function-score-sum=83
