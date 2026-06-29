// RUN: %clang_cc1 -triple wasm32-unknown-unknown -target-cpu mvp -tune-cpu u2-aapcs64 -emit-llvm %s -o - | FileCheck %s --check-prefix=U2-AAPCS64
// RUN: %clang_cc1 -triple wasm32-unknown-unknown -target-cpu mvp -tune-cpu u2-sysv -emit-llvm %s -o - | FileCheck %s --check-prefix=U2-SYSV
// RUN: %clang_cc1 -triple wasm32-unknown-unknown -target-cpu mvp -tune-cpu u2-x86_64-sysv -emit-llvm %s -o - | FileCheck %s --check-prefix=U2-X86-64-SYSV
// RUN: %clang_cc1 -triple wasm32-unknown-unknown -target-cpu mvp -tune-cpu uwvm2 -emit-llvm %s -o - | FileCheck %s --check-prefix=UWVM2
// RUN: %clang_cc1 -triple wasm32-unknown-unknown -target-cpu mvp -tune-cpu m3 -emit-llvm %s -o - | FileCheck %s --check-prefix=M3

void f(void) {}

// U2-AAPCS64: attributes #[[ATTR:[0-9]+]] = { {{.*}}"target-cpu"="mvp" {{.*}}"tune-cpu"="u2-aapcs64"
// U2-SYSV: attributes #[[ATTR:[0-9]+]] = { {{.*}}"target-cpu"="mvp" {{.*}}"tune-cpu"="u2-sysv"
// U2-X86-64-SYSV: attributes #[[ATTR:[0-9]+]] = { {{.*}}"target-cpu"="mvp" {{.*}}"tune-cpu"="u2-x86_64-sysv"
// UWVM2: attributes #[[ATTR:[0-9]+]] = { {{.*}}"target-cpu"="mvp" {{.*}}"tune-cpu"="uwvm2"
// M3: attributes #[[ATTR:[0-9]+]] = { {{.*}}"target-cpu"="mvp" {{.*}}"tune-cpu"="m3"
