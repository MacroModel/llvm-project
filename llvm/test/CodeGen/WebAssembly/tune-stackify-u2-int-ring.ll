; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=generic -asm-verbose=false | FileCheck %s --check-prefix=GENERIC
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=u2-sysv -asm-verbose=false | FileCheck %s --check-prefix=U2-SYSV
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=uwvm2 -asm-verbose=false | FileCheck %s --check-prefix=UWVM2
; RUN: llc < %s -mtriple=wasm32-unknown-unknown -mcpu=mvp -mtune=u2-aapcs64 -asm-verbose=false | FileCheck %s --check-prefix=U2-AAPCS64

target triple = "wasm32-unknown-unknown"

@U = hidden global [16 x i32] zeroinitializer, align 16

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

define i32 @delay_single_use_move_under_tree(i32 %a, i32 %b, i32 %c, i32 %d,
                                             i32 %e, i32 %f) {
entry:
  %x = add i32 %a, %b
  %t0 = add i32 %c, 1
  %t1 = add i32 %d, 2
  %t2 = mul i32 %t0, %t1
  %t3 = add i32 %e, %f
  %t4 = xor i32 %t2, %t3
  %r = add i32 %x, %t4
  ret i32 %r
}

; GENERIC-LABEL: delay_single_use_move_under_tree:
; GENERIC-NOT: local.set
; GENERIC: i32.xor
; GENERIC-NEXT: i32.add

; U2-SYSV-LABEL: delay_single_use_move_under_tree:
; U2-SYSV: i32.add
; U2-SYSV-NEXT: local.set
; U2-SYSV: i32.xor
; U2-SYSV-NEXT: local.get
; U2-SYSV-NEXT: i32.add

; UWVM2-LABEL: delay_single_use_move_under_tree:
; UWVM2: i32.add
; UWVM2-NEXT: local.set
; UWVM2: i32.xor
; UWVM2-NEXT: local.get
; UWVM2-NEXT: i32.add

; U2-AAPCS64-LABEL: delay_single_use_move_under_tree:
; U2-AAPCS64-NOT: local.set
; U2-AAPCS64: i32.xor
; U2-AAPCS64-NEXT: i32.add

define i32 @localget2_scale_shl(i32 %a, i32 %b) {
entry:
  %s = shl i32 %b, 5
  %r = add i32 %s, %a
  ret i32 %r
}

define i32 @localget2_scale_mul(i32 %a, i32 %b) {
entry:
  %m = mul i32 %b, 7
  %r = add i32 %m, %a
  ret i32 %r
}

; GENERIC-LABEL: localget2_scale_shl:
; GENERIC: local.get 1
; GENERIC-NEXT: i32.const 5
; GENERIC-NEXT: i32.shl
; GENERIC-NEXT: local.get 0
; GENERIC-NEXT: i32.add

; U2-SYSV-LABEL: localget2_scale_shl:
; U2-SYSV: local.get 0
; U2-SYSV-NEXT: local.get 1
; U2-SYSV-NEXT: i32.const 5
; U2-SYSV-NEXT: i32.shl
; U2-SYSV-NEXT: i32.add

; UWVM2-LABEL: localget2_scale_shl:
; UWVM2: local.get 0
; UWVM2-NEXT: local.get 1
; UWVM2-NEXT: i32.const 5
; UWVM2-NEXT: i32.shl
; UWVM2-NEXT: i32.add

; U2-AAPCS64-LABEL: localget2_scale_shl:
; U2-AAPCS64: local.get 0
; U2-AAPCS64-NEXT: local.get 1
; U2-AAPCS64-NEXT: i32.const 5
; U2-AAPCS64-NEXT: i32.shl
; U2-AAPCS64-NEXT: i32.add

; GENERIC-LABEL: localget2_scale_mul:
; GENERIC: local.get 1
; GENERIC-NEXT: i32.const 7
; GENERIC-NEXT: i32.mul
; GENERIC-NEXT: local.get 0
; GENERIC-NEXT: i32.add

; U2-SYSV-LABEL: localget2_scale_mul:
; U2-SYSV: local.get 0
; U2-SYSV-NEXT: local.get 1
; U2-SYSV-NEXT: i32.const 7
; U2-SYSV-NEXT: i32.mul
; U2-SYSV-NEXT: i32.add

; UWVM2-LABEL: localget2_scale_mul:
; UWVM2: local.get 0
; UWVM2-NEXT: local.get 1
; UWVM2-NEXT: i32.const 7
; UWVM2-NEXT: i32.mul
; UWVM2-NEXT: i32.add

; U2-AAPCS64-LABEL: localget2_scale_mul:
; U2-AAPCS64: local.get 0
; U2-AAPCS64-NEXT: local.get 1
; U2-AAPCS64-NEXT: i32.const 7
; U2-AAPCS64-NEXT: i32.mul
; U2-AAPCS64-NEXT: i32.add

define i32 @delay_local_under_tree() {
entry:
  %p0 = getelementptr inbounds [16 x i32], ptr @U, i32 0, i32 0
  %p7 = getelementptr inbounds [16 x i32], ptr @U, i32 0, i32 7
  %p11 = getelementptr inbounds [16 x i32], ptr @U, i32 0, i32 11
  %x = load volatile i32, ptr %p0, align 4
  %y = load volatile i32, ptr %p7, align 4
  %z = load volatile i32, ptr %p11, align 4
  %shr = lshr i32 %z, 5
  %shl = shl i32 %y, 3
  %xor = xor i32 %shr, %shl
  %mix = add i32 %x, %xor
  %r = add i32 %mix, %y
  ret i32 %r
}

; GENERIC-LABEL: delay_local_under_tree:
; GENERIC: i32.load U+28
; GENERIC-NEXT: local.tee

; U2-SYSV-LABEL: delay_local_under_tree:
; U2-SYSV: i32.load U+28
; U2-SYSV-NEXT: local.set
; U2-SYSV-NOT: local.tee
; U2-SYSV: i32.xor
; U2-SYSV-NEXT: local.get
; U2-SYSV-NEXT: i32.add
; U2-SYSV-NEXT: local.get
; U2-SYSV-NEXT: i32.add

; UWVM2-LABEL: delay_local_under_tree:
; UWVM2: i32.load U+28
; UWVM2-NEXT: local.set
; UWVM2-NOT: local.tee
; UWVM2: i32.xor
; UWVM2-NEXT: local.get
; UWVM2-NEXT: i32.add
; UWVM2-NEXT: local.get
; UWVM2-NEXT: i32.add
