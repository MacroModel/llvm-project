// This test uses '<prefix>-SAME: {{^}}' to start matching immediately where the
// previous check finished matching (specifically, caret is not treated as
// matching a start of line when used like this in FileCheck).

// RUN: not %clang_cc1 -triple wasm64--- -target-cpu not-a-cpu -fsyntax-only %s 2>&1 | FileCheck %s
// CHECK: error: unknown target CPU 'not-a-cpu'
// CHECK-NEXT: note: valid target CPU values are:
// CHECK-SAME: {{^}} mvp
// CHECK-SAME: {{^}}, bleeding-edge
// CHECK-SAME: {{^}}, generic
// CHECK-SAME: {{^}}, lime1
// CHECK-SAME: {{^}}, u2-aapcs64
// CHECK-SAME: {{^}}, u2-sysv
// CHECK-SAME: {{^}}, u2-x86_64-sysv
// CHECK-SAME: {{^}}, uwvm2
// CHECK-SAME: {{^}}, uwvm2-int
// CHECK-SAME: {{^}}, uwvm2-aapcs64
// CHECK-SAME: {{^}}, uwvm2-aarch64-aapcs64
// CHECK-SAME: {{^}}, uwvm2-sysv
// CHECK-SAME: {{^}}, uwvm2-x86_64-sysv
// CHECK-SAME: {{^}}, m3
// CHECK-SAME: {{$}}

// RUN: not %clang_cc1 -triple wasm64--- -tune-cpu not-a-cpu -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=TUNE
// TUNE: error: unknown target CPU 'not-a-cpu'
// TUNE-NEXT: note: valid target CPU values are:
// TUNE-SAME: {{^}} mvp
// TUNE-SAME: {{^}}, bleeding-edge
// TUNE-SAME: {{^}}, generic
// TUNE-SAME: {{^}}, lime1
// TUNE-SAME: {{^}}, u2-aapcs64
// TUNE-SAME: {{^}}, u2-sysv
// TUNE-SAME: {{^}}, u2-x86_64-sysv
// TUNE-SAME: {{^}}, uwvm2
// TUNE-SAME: {{^}}, uwvm2-int
// TUNE-SAME: {{^}}, uwvm2-aapcs64
// TUNE-SAME: {{^}}, uwvm2-aarch64-aapcs64
// TUNE-SAME: {{^}}, uwvm2-sysv
// TUNE-SAME: {{^}}, uwvm2-x86_64-sysv
// TUNE-SAME: {{^}}, m3
// TUNE-SAME: {{$}}
