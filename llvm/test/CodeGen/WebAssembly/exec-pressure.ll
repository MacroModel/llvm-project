; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=u2-sysv -wasm-tune-stackify=false -wasm-exec-pressure-dump -o /dev/null 2>&1 | FileCheck %s --check-prefix=U2-SYSV
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=u2-aapcs64 -wasm-tune-stackify=false -wasm-exec-pressure-dump -o /dev/null 2>&1 | FileCheck %s --check-prefix=U2-AAPCS64
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=m3 -wasm-tune-stackify=false -wasm-exec-pressure-dump -o /dev/null 2>&1 | FileCheck %s --check-prefix=M3

target triple = "wasm32-unknown-unknown"

declare void @sink4(i32, i32, i32, i32)
declare void @sink3f(double, double, double)

define void @pressure_i32(i32 %a, i32 %b, i32 %c, i32 %d) {
entry:
  %x0 = add i32 %a, 1
  %x1 = add i32 %b, 2
  %x2 = add i32 %c, 3
  %x3 = add i32 %d, 4
  call void @sink4(i32 %x0, i32 %x1, i32 %x2, i32 %x3)
  ret void
}

define void @pressure_f64(double %a, double %b, double %c) {
entry:
  %x0 = fadd double %a, 1.0
  %x1 = fadd double %b, 2.0
  %x2 = fadd double %c, 3.0
  call void @sink3f(double %x0, double %x1, double %x2)
  ret void
}

; U2-SYSV: wasm-exec-pressure: function=pressure_i32 tune=u2-sysv
; U2-SYSV: profile: fp-ring=8 int-ring=3 register-ring=true m3-slot-provider=false
; U2-SYSV: peak-fp=0 peak-int=5
; U2-SYSV: cap-overflow-fp=0 cap-overflow-int=2
; U2-SYSV: estimated-spills=2 estimated-fills=2
; U2-SYSV: score=50
; U2-SYSV: function-score-sum=50

; U2-AAPCS64: wasm-exec-pressure: function=pressure_i32 tune=u2-aapcs64
; U2-AAPCS64: profile: fp-ring=8 int-ring=5 register-ring=true m3-slot-provider=false
; U2-AAPCS64: peak-fp=0 peak-int=5
; U2-AAPCS64: cap-overflow-fp=0 cap-overflow-int=0
; U2-AAPCS64: estimated-spills=0 estimated-fills=0
; U2-AAPCS64: score=30
; U2-AAPCS64: function-score-sum=30

; M3: wasm-exec-pressure: function=pressure_f64 tune=m3
; M3: profile: fp-ring=1 int-ring=1 register-ring=false m3-slot-provider=true
; M3: peak-fp=4 peak-int=0
; M3: cap-overflow-fp=3 cap-overflow-int=0
; M3: estimated-spills=0 estimated-fills=0
; M3: score=29
; M3: function-score-sum=29
