typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned long usize;
typedef unsigned char u8;

typedef struct {
  u32 Key;
  u32 Value;
  u32 Tag;
  u32 Pad;
} HashEntry;

typedef struct {
  double X;
  double Y;
  double VX;
  double VY;
  double Mass;
  u32 Id;
  u32 Flags;
} Particle;

typedef struct {
  u32 A;
  u32 B;
  u32 C;
  u32 D;
  double X;
  double Y;
} Record;

typedef struct {
  u32 Left;
  u32 Right;
  u32 Key;
  u32 Value;
  double Weight;
} TreeNode;

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

static volatile HashEntry HMap[64] = {
    {0x9e3779b9u, 0x85ebca6bu, 0x11u, 0x01u},
    {0xc2b2ae35u, 0x27d4eb2fu, 0x12u, 0x03u},
    {0x165667b1u, 0xd3a2646cu, 0x13u, 0x05u},
    {0xfd7046c5u, 0xb55a4f09u, 0x14u, 0x07u},
    {0x1234567bu, 0x31415927u, 0x15u, 0x09u},
    {0x27182818u, 0x7f4a7c15u, 0x16u, 0x0bu},
    {0x94d049bbu, 0x2545f491u, 0x17u, 0x0du},
    {0x1000003du, 0x6a09e667u, 0x18u, 0x0fu},
    {0xbb67ae85u, 0x3c6ef372u, 0x19u, 0x11u},
    {0xa54ff53au, 0x510e527fu, 0x1au, 0x13u},
    {0x9b05688cu, 0x1f83d9abu, 0x1bu, 0x15u},
    {0x5be0cd19u, 0x243f6a88u, 0x1cu, 0x17u},
    {0x85a308d3u, 0x13198a2eu, 0x1du, 0x19u},
    {0x03707344u, 0xa4093822u, 0x1eu, 0x1bu},
    {0x299f31d0u, 0x082efa98u, 0x1fu, 0x1du},
    {0xec4e6c89u, 0x452821e6u, 0x20u, 0x1fu},
};

static volatile Particle Particles[32] = {
    {0.25, 0.75, 0.010, -0.015, 1.10, 1u, 0x01u},
    {0.50, 0.25, -0.020, 0.012, 0.90, 2u, 0x03u},
    {0.80, 0.60, 0.018, 0.005, 1.30, 3u, 0x05u},
    {0.15, 0.40, -0.011, -0.009, 1.70, 4u, 0x07u},
    {0.66, 0.12, 0.009, 0.021, 0.80, 5u, 0x09u},
    {0.33, 0.88, -0.017, 0.004, 1.50, 6u, 0x0bu},
    {0.91, 0.31, 0.013, -0.016, 1.20, 7u, 0x0du},
    {0.44, 0.55, -0.006, 0.011, 1.00, 8u, 0x0fu},
};

static volatile Record Records[32] = {
    {0x9e3779b9u, 0x85ebca6bu, 0xc2b2ae35u, 0x27d4eb2fu, 0.125, 0.250},
    {0x165667b1u, 0xd3a2646cu, 0xfd7046c5u, 0xb55a4f09u, 0.375, 0.500},
    {0x1234567bu, 0x31415927u, 0x27182818u, 0x7f4a7c15u, 0.625, 0.750},
    {0x94d049bbu, 0x2545f491u, 0x1000003du, 0x6a09e667u, 0.875, 0.125},
    {0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0.250, 0.375},
    {0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u, 0x243f6a88u, 0.500, 0.625},
    {0x85a308d3u, 0x13198a2eu, 0x03707344u, 0xa4093822u, 0.750, 0.875},
    {0x299f31d0u, 0x082efa98u, 0xec4e6c89u, 0x452821e6u, 0.125, 0.500},
};

static volatile TreeNode Tree[32] = {
    {1u, 2u, 0x9e3779b9u, 0x85ebca6bu, 0.125},
    {3u, 4u, 0xc2b2ae35u, 0x27d4eb2fu, 0.250},
    {5u, 6u, 0x165667b1u, 0xd3a2646cu, 0.375},
    {7u, 8u, 0xfd7046c5u, 0xb55a4f09u, 0.500},
    {9u, 10u, 0x1234567bu, 0x31415927u, 0.625},
    {11u, 12u, 0x27182818u, 0x7f4a7c15u, 0.750},
    {13u, 14u, 0x94d049bbu, 0x2545f491u, 0.875},
    {15u, 0u, 0x1000003du, 0x6a09e667u, 1.000},
};

static volatile u8 Bytes[256] =
    "wasm tune bench byte stream: hashmap keys, parser tokens, struct fields, "
    "scientific kernels, register ring pressure, delay local windows, sysv";

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

static __attribute__((noinline)) double stencil5(usize I) {
  usize J = I & 63u;
  double L2 = A[(J + 62u) & 63u];
  double L1 = A[(J + 63u) & 63u];
  double M = A[J];
  double R1 = A[(J + 1u) & 63u];
  double R2 = A[(J + 2u) & 63u];
  double R = L2 * -0.06136 + L1 * 0.24477 + M * 0.63318 +
             R1 * 0.24477 + R2 * -0.06136;
  Scratch[(J + 13u) & 63u] = R;
  return R + C[(J + 7u) & 63u] * M;
}

static __attribute__((noinline)) double poly_horner(usize I) {
  usize J = I & 63u;
  double X = C[J] + (double)(U[(J + 9u) & 63u] & 255u) * 0.000001;
  double R = B[(J + 11u) & 63u];
  R = R * X + A[(J + 0u) & 63u];
  R = R * X + B[(J + 1u) & 63u];
  R = R * X + A[(J + 2u) & 63u];
  R = R * X + B[(J + 3u) & 63u];
  R = R * X + A[(J + 4u) & 63u];
  R = R * X + B[(J + 5u) & 63u];
  R = R * X + A[(J + 6u) & 63u];
  R = R * X + B[(J + 7u) & 63u];
  return R;
}

static __attribute__((noinline)) double mat3_kernel(usize I) {
  usize J = I & 63u;
  double A00 = A[(J + 0u) & 63u];
  double A01 = A[(J + 1u) & 63u];
  double A02 = A[(J + 2u) & 63u];
  double A10 = A[(J + 3u) & 63u];
  double A11 = A[(J + 4u) & 63u];
  double A12 = A[(J + 5u) & 63u];
  double A20 = A[(J + 6u) & 63u];
  double A21 = A[(J + 7u) & 63u];
  double A22 = A[(J + 8u) & 63u];
  double B00 = B[(J + 9u) & 63u];
  double B01 = B[(J + 10u) & 63u];
  double B02 = B[(J + 11u) & 63u];
  double B10 = B[(J + 12u) & 63u];
  double B11 = B[(J + 13u) & 63u];
  double B12 = B[(J + 14u) & 63u];
  double B20 = B[(J + 15u) & 63u];
  double B21 = B[(J + 16u) & 63u];
  double B22 = B[(J + 17u) & 63u];
  double C00 = A00 * B00 + A01 * B10 + A02 * B20;
  double C01 = A00 * B01 + A01 * B11 + A02 * B21;
  double C02 = A00 * B02 + A01 * B12 + A02 * B22;
  double C10 = A10 * B00 + A11 * B10 + A12 * B20;
  double C11 = A10 * B01 + A11 * B11 + A12 * B21;
  double C12 = A10 * B02 + A11 * B12 + A12 * B22;
  double C20 = A20 * B00 + A21 * B10 + A22 * B20;
  double C21 = A20 * B01 + A21 * B11 + A22 * B21;
  double C22 = A20 * B02 + A21 * B12 + A22 * B22;
  return C00 + C01 * C[(J + 1u) & 63u] + C02 + C10 +
         C11 * C[(J + 2u) & 63u] + C12 + C20 + C21 + C22;
}

static __attribute__((noinline)) double jacobi2d_tile(usize I) {
  usize J = I & 63u;
  double C0 = A[J];
  double N = A[(J + 56u) & 63u];
  double S = A[(J + 8u) & 63u];
  double W = A[(J + 63u) & 63u];
  double E = A[(J + 1u) & 63u];
  double NW = B[(J + 55u) & 63u];
  double NE = B[(J + 57u) & 63u];
  double SW = B[(J + 7u) & 63u];
  double SE = B[(J + 9u) & 63u];
  double R = C0 * 0.421875 + (N + S + W + E) * 0.109375 +
             (NW + NE + SW + SE) * 0.03515625;
  Scratch[(J + 19u) & 63u] = R;
  return R + C[(J + 5u) & 63u] * (C0 - R);
}

static __attribute__((noinline)) double spmv4_kernel(usize I) {
  u32 Seed = U[I & 63u] ^ (u32)I;
  double Sum = 0.0;
  for (u32 Row = 0; Row != 4u; ++Row) {
    u32 Base = (Seed + Row * 13u) & 63u;
    double R = 0.0;
    for (u32 K = 0; K != 4u; ++K) {
      u32 Col = U[(Base + K * 3u) & 63u] & 63u;
      double V = C[(Base + K * 5u) & 63u];
      R += V * A[(Col + Row) & 63u];
    }
    Sum += R * B[(Base + Row * 7u) & 63u];
  }
  return Sum;
}

static __attribute__((noinline)) double tridiag8_kernel(usize I) {
  usize J = I & 63u;
  double Cp = 0.0;
  double Dp = 0.0;
  double Acc = 0.0;
  for (u32 K = 0; K != 8u; ++K) {
    double Lo = 0.050 + C[(J + K + 3u) & 63u] * 0.125;
    double Mid = 1.750 + A[(J + K + 7u) & 63u] * 0.0625;
    double Hi = 0.075 + B[(J + K + 11u) & 63u] * 0.125;
    double RHS = A[(J + K * 2u) & 63u] + C[(J + K * 5u) & 63u];
    double Den = Mid - Lo * Cp;
    Cp = Hi / Den;
    Dp = (RHS - Lo * Dp) / Den;
    Acc += Dp;
  }
  double X = Dp;
  for (u32 K = 0; K != 4u; ++K) {
    X = Dp - Cp * X + C[(J + K * 9u) & 63u] * 0.001;
    Acc += X;
  }
  return Acc;
}

static __attribute__((noinline)) double mandelbrot12(usize I) {
  double Cr = (double)(I & 31u) * 0.078125 - 1.25;
  double Ci = (double)((I >> 5u) & 31u) * 0.078125 - 1.25;
  double Zr = Cr;
  double Zi = Ci;
  double Esc = 0.0;
  for (u32 K = 0; K != 12u; ++K) {
    double Zr2 = Zr * Zr;
    double Zi2 = Zi * Zi;
    if (Zr2 + Zi2 > 4.0) {
      Esc += (double)(K + 1u);
      break;
    }
    Zi = (Zr + Zr) * Zi + Ci;
    Zr = Zr2 - Zi2 + Cr;
    Esc += Zr + Zi;
  }
  return Esc;
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

static __attribute__((noinline)) u32 byte_hash24(usize I) {
  usize P = I & 127u;
  u32 H = 2166136261u ^ (u32)I;
  for (u32 K = 0; K != 24u; ++K) {
    u32 C0 = Bytes[(P + K * 7u) & 255u];
    H ^= C0 + (K << 8);
    H *= 16777619u;
    H ^= H >> 13;
  }
  return H;
}

static __attribute__((noinline)) u32 hashmap_lookup(usize I) {
  u32 Key = (u32)I ^ U[I & 63u];
  Key ^= Key >> 16;
  Key *= 0x85ebca6bu;
  Key ^= Key >> 13;
  Key *= 0xc2b2ae35u;
  Key ^= Key >> 16;

  u32 Pos = (Key ^ (Key >> 11)) & 63u;
  u32 Acc = 0x9e3779b9u;
  for (u32 P = 0; P != 8u; ++P) {
    const volatile HashEntry *E = &HMap[(Pos + P) & 63u];
    u32 K = E->Key;
    u32 V = E->Value;
    u32 T = E->Tag;
    u32 Hit = ((K ^ Key) & 15u) == 0u;
    Acc += (V ^ (T + P)) + (Hit ? (K + V) : (K ^ P));
    Acc = (Acc << 5) | (Acc >> 27);
  }
  return Acc ^ Key;
}

static __attribute__((noinline)) u32 hashmap_update(usize I) {
  u32 Key = (u32)I * 0x9e3779b9u + U[(I + 17u) & 63u];
  Key ^= Key >> 15;
  u32 Pos = (Key ^ (Key >> 7)) & 63u;
  volatile HashEntry *E = &HMap[Pos];
  u32 V = E->Value;
  u32 T = E->Tag;
  u32 NewV = V + (Key ^ (T * 0x1000003du));
  E->Value = NewV;
  E->Tag = T + 1u;
  return (NewV ^ E->Key) + ((NewV << 7) | (NewV >> 25));
}

static __attribute__((noinline)) u64 wide_i64_mix(usize I) {
  u64 X = ((u64)U[I & 63u] << 32) | U[(I + 19u) & 63u];
  u64 Y = ((u64)U[(I + 7u) & 63u] << 32) | U[(I + 31u) & 63u];
  X ^= Y + 0x9e3779b97f4a7c15ULL + (X << 6) + (X >> 2);
  Y ^= X * 0xbf58476d1ce4e5b9ULL;
  X += (Y << 17) ^ (Y >> 11);
  Y += (X << 23) ^ (X >> 29);
  return X ^ (Y * 0x94d049bb133111ebULL);
}

static __attribute__((noinline)) u32 tree_walk(usize I) {
  u32 Idx = ((u32)I ^ U[I & 63u]) & 7u;
  u32 Acc = 0x811c9dc5u ^ (u32)I;
  for (u32 D = 0; D != 8u; ++D) {
    const volatile TreeNode *N = &Tree[Idx];
    u32 Key = N->Key;
    u32 Val = N->Value;
    Acc ^= Key + (Val << (D & 7u));
    Acc *= 16777619u;
    Acc += (u32)(N->Weight * 4096.0) ^ U[(Idx + D * 5u) & 63u];
    Idx = (((Acc ^ Key) >> (D & 7u)) & 1u) ? N->Right : N->Left;
    Idx &= 7u;
  }
  return Acc;
}

static __attribute__((noinline)) double particle_struct(usize I) {
  usize J = I & 31u;
  volatile Particle *P = &Particles[J];
  const volatile Particle *Q = &Particles[(J + 5u) & 31u];
  double X = P->X;
  double Y = P->Y;
  double VX = P->VX;
  double VY = P->VY;
  double DX = Q->X - X + A[I & 63u] * 0.001;
  double DY = Q->Y - Y + B[(I + 3u) & 63u] * 0.001;
  double Inv = 1.0 / (DX * DX + DY * DY + 0.03125);
  double Force = Q->Mass * Inv;
  VX += DX * Force;
  VY += DY * Force;
  X += VX * 0.015625;
  Y += VY * 0.015625;
  P->X = X;
  P->Y = Y;
  P->VX = VX;
  P->VY = VY;
  return X * 0.25 + Y * 0.5 + VX * 0.125 + VY * 0.0625;
}

static __attribute__((noinline)) u32 record_struct(usize I) {
  usize J = I & 31u;
  volatile Record *R = &Records[J];
  u32 A0 = R->A;
  u32 B0 = R->B;
  u32 C0 = R->C;
  u32 D0 = R->D;
  u32 H = A0 + ((B0 << 3) ^ (C0 >> 5));
  H ^= (D0 + U[(I + 23u) & 63u]) * 0x45d9f3bu;
  H += (H << 11) ^ (H >> 7);
  double X = R->X;
  double Y = R->Y;
  R->A = H;
  R->B = B0 + H;
  R->X = X + (double)(H & 255u) * 0.00001;
  R->Y = Y + (double)((H >> 8) & 255u) * 0.00001;
  return H ^ (u32)(X * 4096.0) ^ ((u32)(Y * 4096.0) << 1);
}

static __attribute__((noinline)) double struct_aos_scan(usize I) {
  u32 H = U[I & 63u] ^ (u32)I;
  double Acc = 0.0;
  for (u32 K = 0; K != 4u; ++K) {
    usize J = (I + K * 7u) & 31u;
    volatile Record *R = &Records[J];
    volatile Particle *P = &Particles[(J + K) & 31u];
    u32 M = R->A ^ (R->B + H);
    M += (R->C << 5) ^ (R->D >> 3);
    H ^= M * 0x9e3779b9u;
    double X = P->X + R->X * 0.125;
    double Y = P->Y + R->Y * 0.250;
    double V = X * P->VX + Y * P->VY + P->Mass;
    Acc += V + (double)(H & 1023u) * 0.000001;
    R->D = H + M;
    P->Flags = (P->Flags + H) & 255u;
  }
  return Acc;
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
#elif defined(BENCH_STENCIL5)
    FSink += stencil5(I);
#elif defined(BENCH_POLY)
    FSink += poly_horner(I);
#elif defined(BENCH_MAT3)
    FSink += mat3_kernel(I);
#elif defined(BENCH_JACOBI2D)
    FSink += jacobi2d_tile(I);
#elif defined(BENCH_SPMV4)
    FSink += spmv4_kernel(I);
#elif defined(BENCH_TRIDIAG8)
    FSink += tridiag8_kernel(I);
#elif defined(BENCH_MANDELBROT12)
    FSink += mandelbrot12(I);
#elif defined(BENCH_ADDR)
    ISink += address_heavy(I);
#elif defined(BENCH_BYTE_HASH)
    ISink += byte_hash24(I);
#elif defined(BENCH_HASH_LOOKUP)
    ISink += hashmap_lookup(I);
#elif defined(BENCH_HASH_UPDATE)
    ISink += hashmap_update(I);
#elif defined(BENCH_I64_MIX)
    ISink += wide_i64_mix(I);
#elif defined(BENCH_TREE_WALK)
    ISink += tree_walk(I);
#elif defined(BENCH_PARTICLE)
    FSink += particle_struct(I);
#elif defined(BENCH_RECORD)
    ISink += record_struct(I);
#elif defined(BENCH_STRUCT_SCAN)
    FSink += struct_aos_scan(I);
#elif defined(BENCH_MIXED)
    FSink += mixed_addr_fp(I);
#else
#error "Select a BENCH_* macro"
#endif
  }
  bench_result_f64 = FSink;
  bench_result_u64 = ISink ^ (u64)(FSink * 1000000.0);
}
