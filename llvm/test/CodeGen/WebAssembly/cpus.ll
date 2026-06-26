; This tests that llc accepts all valid WebAssembly CPUs.

; RUN: llc < %s -asm-verbose=false -mtriple=wasm32-unknown-unknown -mcpu=mvp 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm64-unknown-unknown-wasm -mcpu=mvp 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm32-unknown-unknown -mcpu=generic 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm64-unknown-unknown-wasm -mcpu=generic 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm32-unknown-unknown -mcpu=lime1 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm64-unknown-unknown-wasm -mcpu=lime1 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm32-unknown-unknown -mcpu=bleeding-edge 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm64-unknown-unknown-wasm -mcpu=bleeding-edge 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm32-unknown-unknown -mcpu=u2-aapcs64 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm64-unknown-unknown-wasm -mcpu=u2-aapcs64 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm32-unknown-unknown -mcpu=u2-sysv 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm64-unknown-unknown-wasm -mcpu=u2-sysv 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm32-unknown-unknown -mcpu=u2-x86_64-sysv 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm64-unknown-unknown-wasm -mcpu=u2-x86_64-sysv 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm32-unknown-unknown -mcpu=m3 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm64-unknown-unknown-wasm -mcpu=m3 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=u2-aapcs64 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=u2-sysv 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=u2-x86_64-sysv 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=m3 2>&1 | FileCheck %s
; RUN: llc < %s -asm-verbose=false -mtriple=wasm32-unknown-unknown -mcpu=invalidcpu 2>&1 | FileCheck %s --check-prefix=INVALID
; RUN: llc < %s -asm-verbose=false -mtriple=wasm64-unknown-unknown-wasm -mcpu=invalidcpu 2>&1 | FileCheck %s --check-prefix=INVALID

; CHECK-NOT: is not a recognized processor for this target
; INVALID: {{.+}} is not a recognized processor for this target

define i32 @f(i32 %i_like_the_web) {
  ret i32 %i_like_the_web
}
