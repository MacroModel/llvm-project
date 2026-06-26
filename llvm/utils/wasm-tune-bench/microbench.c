typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned long usize;

#ifndef ITER
#define ITER 200000u
#endif

static volatile double A[64] = {
    1.0001, 1.0002, 1.0003, 1.0004, 1.0005, 1.0006, 1.0007, 1.0008,
    1.0009, 1.0010, 1.0011, 1.0012, 1.0013, 1.0014, 1.0015, 1.0016,
    1.0017, 1.0018, 1.0019, 1.0020, 1.0021, 1.0022, 1.0023, 1.0024,
    1.0025, 1.0026, 1.0027, 1.0028, 1.0029, 1.0030, 1.0031, 1.0032,
    1.0033, 1.0034, 1.0035, 1.0036, 1.0037, 1.0038, 1.0039, 1.0040,
    1.0041, 1.0042, 1.0043, 1.0044, 1.0045, 1.0046, 1.0047, 1.0048,
    1.0049, 1.0050, 1.0051, 1.0052, 1.0053, 1.0054, 1.0055, 1.0056,
    1.0057, 1.0058, 1.0059, 1.0060, 1.0061, 1.0062, 1.0063, 1.0064};

static volatile double B[64] = {
    0.9991, 0.9992, 0.9993, 0.9994, 0.9995, 0.9996, 0.9997, 0.9998,
    0.9999, 1.0000, 1.0001, 1.0002, 1.0003, 1.0004, 1.0005, 1.0006,
    1.0007, 1.0008, 1.0009, 1.0010, 1.0011, 1.0012, 1.0013, 1.0014,
    1.0015, 1.0016, 1.0017, 1.0018, 1.0019, 1.0020, 1.0021, 1.0022,
    1.0023, 1.0024, 1.0025, 1.0026, 1.0027, 1.0028, 1.0029, 1.0030,
    1.0031, 1.0032, 1.0033, 1.0034, 1.0035, 1.0036, 1.0037, 1.0038,
    1.0039, 1.0040, 1.0041, 1.0042, 1.0043, 1.0044, 1.0045, 1.0046,
    1.0047, 1.0048, 1.0049, 1.0050, 1.0051, 1.0052, 1.0053, 1.0054};

static volatile double C[64] = {
    0.1001, 0.1002, 0.1003, 0.1004, 0.1005, 0.1006, 0.1007, 0.1008,
    0.1009, 0.1010, 0.1011, 0.1012, 0.1013, 0.1014, 0.1015, 0.1016,
    0.1017, 0.1018, 0.1019, 0.1020, 0.1021, 0.1022, 0.1023, 0.1024,
    0.1025, 0.1026, 0.1027, 0.1028, 0.1029, 0.1030, 0.1031, 0.1032,
    0.1033, 0.1034, 0.1035, 0.1036, 0.1037, 0.1038, 0.1039, 0.1040,
    0.1041, 0.1042, 0.1043, 0.1044, 0.1045, 0.1046, 0.1047, 0.1048,
    0.1049, 0.1050, 0.1051, 0.1052, 0.1053, 0.1054, 0.1055, 0.1056,
    0.1057, 0.1058, 0.1059, 0.1060, 0.1061, 0.1062, 0.1063, 0.1064};

static volatile u32 U[64] = {
    0x9e3779b9u, 0x85ebca6bu, 0xc2b2ae35u, 0x27d4eb2fu, 0x165667b1u,
    0xd3a2646cu, 0xfd7046c5u, 0xb55a4f09u, 0x1234567bu, 0x31415927u,
    0x27182818u, 0x7f4a7c15u, 0x94d049bbu, 0x2545f491u, 0x1000003du,
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu,
    0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u, 0x243f6a88u, 0x85a308d3u,
    0x13198a2eu, 0x03707344u, 0xa4093822u, 0x299f31d0u, 0x082efa98u,
    0xec4e6c89u, 0x452821e6u, 0x38d01377u, 0xbe5466cfu, 0x34e90c6cu,
    0xc0ac29b7u, 0xc97c50ddu, 0x3f84d5b5u, 0xb5470917u, 0x9216d5d9u,
    0x8979fb1bu, 0xd1310ba6u, 0x98dfb5acu, 0x2ffd72dbu, 0xd01adfb7u,
    0xb8e1afedu, 0x6a267e96u, 0xba7c9045u, 0xf12c7f99u, 0x24a19947u,
    0xb3916cf7u, 0x0801f2e2u, 0x858efc16u, 0x636920d8u, 0x71574e69u,
    0xa458fea3u, 0xf4933d7eu, 0x0d95748fu, 0x728eb658u, 0x718bcd58u,
    0x82154aeeu, 0x7b54a41du, 0xc25a59b5u, 0x9c30d539u};

static volatile double Scratch[64];
volatile double bench_result_f64;
volatile u64 bench_result_u64;

static __attribute__((noinline)) double dot_n(usize I, unsigned N) {
  usize J = I & 63u;
  double R = C[J];
  if (N > 0)
    R += A[(J + 0u) & 63u] * B[(J + 1u) & 63u];
  if (N > 1)
    R += A[(J + 2u) & 63u] * B[(J + 3u) & 63u];
  if (N > 2)
    R += A[(J + 4u) & 63u] * B[(J + 5u) & 63u];
  if (N > 3)
    R += A[(J + 6u) & 63u] * B[(J + 7u) & 63u];
  if (N > 4)
    R += A[(J + 8u) & 63u] * B[(J + 9u) & 63u];
  if (N > 5)
    R += A[(J + 10u) & 63u] * B[(J + 11u) & 63u];
  if (N > 6)
    R += A[(J + 12u) & 63u] * B[(J + 13u) & 63u];
  if (N > 7)
    R += A[(J + 14u) & 63u] * B[(J + 15u) & 63u];
  if (N > 8)
    R += A[(J + 16u) & 63u] * B[(J + 17u) & 63u];
  if (N > 9)
    R += A[(J + 18u) & 63u] * B[(J + 19u) & 63u];
  if (N > 10)
    R += A[(J + 20u) & 63u] * B[(J + 21u) & 63u];
  if (N > 11)
    R += A[(J + 22u) & 63u] * B[(J + 23u) & 63u];
  return R;
}

static __attribute__((noinline)) double fir_n(usize I, unsigned N) {
  usize J = I & 63u;
  double R = C[(J + 31u) & 63u];
  if (N > 0)
    R += A[(J + 0u) & 63u] * C[0];
  if (N > 1)
    R += A[(J + 1u) & 63u] * C[1];
  if (N > 2)
    R += A[(J + 2u) & 63u] * C[2];
  if (N > 3)
    R += A[(J + 3u) & 63u] * C[3];
  if (N > 4)
    R += A[(J + 4u) & 63u] * C[4];
  if (N > 5)
    R += A[(J + 5u) & 63u] * C[5];
  if (N > 6)
    R += A[(J + 6u) & 63u] * C[6];
  if (N > 7)
    R += A[(J + 7u) & 63u] * C[7];
  if (N > 8)
    R += A[(J + 8u) & 63u] * C[8];
  if (N > 9)
    R += A[(J + 9u) & 63u] * C[9];
  if (N > 10)
    R += A[(J + 10u) & 63u] * C[10];
  if (N > 11)
    R += A[(J + 11u) & 63u] * C[11];
  if (N > 12)
    R += A[(J + 12u) & 63u] * C[12];
  if (N > 13)
    R += A[(J + 13u) & 63u] * C[13];
  if (N > 14)
    R += A[(J + 14u) & 63u] * C[14];
  if (N > 15)
    R += A[(J + 15u) & 63u] * C[15];
  return R;
}

static __attribute__((noinline)) double fft_butterfly(usize I, unsigned N) {
  double Acc = 0.0;
  for (unsigned K = 0; K < N; ++K) {
    usize J = (I + K * 8u) & 63u;
    double Ar = A[J];
    double Ai = B[(J + 1u) & 63u];
    double Br = A[(J + 2u) & 63u];
    double Bi = B[(J + 3u) & 63u];
    double Wr = C[(J + 4u) & 63u];
    double Wi = C[(J + 5u) & 63u];
    double Tr = Wr * Br - Wi * Bi;
    double Ti = Wr * Bi + Wi * Br;
    double Or0 = Ar + Tr;
    double Oi0 = Ai + Ti;
    double Or1 = Ar - Tr;
    double Oi1 = Ai - Ti;
    Acc += Or0 * Oi0 + Or1 * Oi1;
  }
  return Acc;
}

static __attribute__((noinline)) u32 address_heavy(usize I) {
  u32 X = U[I & 63u];
  u32 Y = U[(I * 3u + 7u) & 63u];
  u32 Z = U[(I * 5u + 11u) & 63u];
  X += (Y << 3) ^ (Z >> 5);
  Y += (X * 33u) ^ (Z + 0x9e3779b9u);
  Z += (Y << 7) ^ (X >> 9);
  X = (X + Y) ^ (Z * 17u);
  Y = (Y + Z) ^ (X * 31u);
  Z = (Z + X) ^ (Y * 13u);
  return X + (Y << 1) + (Z << 2);
}

static __attribute__((noinline)) double mixed_addr_fp(usize I) {
  u32 H = address_heavy(I);
  usize J = H & 63u;
  double R = A[J] * B[(J + 17u) & 63u] + A[(J + 3u) & 63u] * C[(J + 29u) & 63u];
  Scratch[(J + 11u) & 63u] = R;
  return R + Scratch[(J + 11u) & 63u];
}

__attribute__((export_name("_start"))) void _start(void) {
  double FSink = 0.0;
  u64 ISink = 0;
  for (usize I = 0; I < (usize)ITER; ++I) {
#if defined(BENCH_DOT6)
    FSink += dot_n(I, 6);
#elif defined(BENCH_DOT7)
    FSink += dot_n(I, 7);
#elif defined(BENCH_DOT8)
    FSink += dot_n(I, 8);
#elif defined(BENCH_DOT9)
    FSink += dot_n(I, 9);
#elif defined(BENCH_DOT12)
    FSink += dot_n(I, 12);
#elif defined(BENCH_FIR8)
    FSink += fir_n(I, 8);
#elif defined(BENCH_FIR12)
    FSink += fir_n(I, 12);
#elif defined(BENCH_FIR16)
    FSink += fir_n(I, 16);
#elif defined(BENCH_FFT1)
    FSink += fft_butterfly(I, 1);
#elif defined(BENCH_FFT2)
    FSink += fft_butterfly(I, 2);
#elif defined(BENCH_FFT4)
    FSink += fft_butterfly(I, 4);
#elif defined(BENCH_ADDR)
    ISink += address_heavy(I);
#elif defined(BENCH_MIXED)
    FSink += mixed_addr_fp(I);
#else
#error "Select a BENCH_* macro"
#endif
  }
  bench_result_f64 = FSink;
  bench_result_u64 = ISink ^ (u64)(FSink * 1000000.0);
}
