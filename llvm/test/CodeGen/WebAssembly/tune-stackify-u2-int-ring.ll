; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=generic -asm-verbose=false | FileCheck %s --check-prefix=GENERIC
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=u2-sysv -asm-verbose=false | FileCheck %s --check-prefix=U2-SYSV
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=uwvm2 -asm-verbose=false | FileCheck %s --check-prefix=UWVM2
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=u2-aapcs64 -asm-verbose=false | FileCheck %s --check-prefix=U2-AAPCS64

target triple = "wasm32-unknown-unknown"

define i32 @int_subtree(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f) {
entry:
  %ab = add i32 %a, %b
  %cd = add i32 %c, %d
  %ef = add i32 %e, %f
  %abcd = add i32 %ab, %cd
  %big = add i32 %abcd, %ef
  %r = mul i32 %big, 7
  ret i32 %r
}

; GENERIC-LABEL: int_subtree:
; GENERIC-NOT: local.set
; GENERIC: i32.mul
; GENERIC-NEXT: end_function

; U2-SYSV-LABEL: int_subtree:
; U2-SYSV-NOT: local.set
; U2-SYSV: i32.mul
; U2-SYSV-NEXT: end_function

; UWVM2-LABEL: int_subtree:
; UWVM2-NOT: local.set
; UWVM2: i32.mul
; UWVM2-NEXT: end_function

; U2-AAPCS64-LABEL: int_subtree:
; U2-AAPCS64-NOT: local.set
; U2-AAPCS64: i32.mul
; U2-AAPCS64-NEXT: end_function
