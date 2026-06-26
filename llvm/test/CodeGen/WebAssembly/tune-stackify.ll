; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=generic -asm-verbose=false | FileCheck %s --check-prefix=GENERIC
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=u2-sysv -asm-verbose=false | FileCheck %s --check-prefix=U2
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=m3 -asm-verbose=false | FileCheck %s --check-prefix=M3
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=m3 -wasm-tune-stackify=false -asm-verbose=false | FileCheck %s --check-prefix=GENERIC

target triple = "wasm32-unknown-unknown"

define double @distance_f64(double %a, double %b, double %c, double %d) {
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

; GENERIC-LABEL: distance_f64:
; GENERIC-NOT: .local
; GENERIC: local.tee
; GENERIC: end_function

; U2-LABEL: distance_f64:
; U2-NOT: .local
; U2: local.tee
; U2: end_function

; M3-LABEL: distance_f64:
; M3: .local
; M3: local.set
; M3: local.get
; M3: end_function
