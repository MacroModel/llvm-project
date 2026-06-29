#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SRC="$ROOT/llvm/utils/wasm-tune-bench/microbench.c"

LLVM_BUILD="${LLVM_BUILD:-$ROOT/build-wasm-tune}"
CLANG="${CLANG:-$LLVM_BUILD/bin/clang}"
WASM_LD="${WASM_LD:-/home/macromodel/Documents/tool-chain/x86_64-linux-gnu-llvm/bin/wasm-ld}"
UWVM="${UWVM:-/home/macromodel/Documents/src/uwvm2/build/linux/x86_64/release/uwvm}"
WASM3="${WASM3:-/home/macromodel/Documents/src/wasm3/build-clang-release/wasm3}"
UWVM_EXTRA_ARGS="${UWVM_EXTRA_ARGS:-}"

OUT="${OUT:-/tmp/wasm-tune-bench}"
ITER="${ITER:-200000}"
REPEAT="${REPEAT:-3}"
UWVM2_TUNE="${UWVM2_TUNE:-uwvm2}"
WASM3_TUNE="${WASM3_TUNE:-m3}"

mkdir -p "$OUT/wasm" "$OUT/log"
CSV="$OUT/results.csv"
SHAPE_CSV="$OUT/shape-summary.csv"

printf 'bench,tune,runtime,iter,repeat,min_s,avg_s,status\n' > "$CSV"
printf 'bench,tune,function,move,remat,tee,keep_local,historical_kept,profile_changed,profile_commute_tried,profile_commute_accepted,product_bank_boundary,int_mild_overflow,int_severe_overflow,fp_overflow,product_bank,m3_fp_bank,m3_distance,uwvm2_strict_fp_accum,est_local_get,est_local_set,est_tee,keep_local_boundary,delay_local_rhs_commute_tried,delay_local_rhs_commute_accepted\n' > "$SHAPE_CSV"

bench_defs=(
  "dot6_fast BENCH_DOT6 fast"
  "dot7_fast BENCH_DOT7 fast"
  "dot8_fast BENCH_DOT8 fast"
  "dot9_fast BENCH_DOT9 fast"
  "dot12_fast BENCH_DOT12 fast"
  "dot6_strict BENCH_DOT6 strict"
  "dot7_strict BENCH_DOT7 strict"
  "dot8_strict BENCH_DOT8 strict"
  "dot9_strict BENCH_DOT9 strict"
  "dot12_strict BENCH_DOT12 strict"
  "fir8_fast BENCH_FIR8 fast"
  "fir12_fast BENCH_FIR12 fast"
  "fir16_fast BENCH_FIR16 fast"
  "fft1_fast BENCH_FFT1 fast"
  "fft2_fast BENCH_FFT2 fast"
  "fft4_fast BENCH_FFT4 fast"
  "address_i32 BENCH_ADDR strict"
  "mixed_addr_fp BENCH_MIXED fast"
)

tunes=(default "$UWVM2_TUNE" "$WASM3_TUNE")
read -r -a uwvm_extra_args <<< "$UWVM_EXTRA_ARGS"

compile_wasm() {
  local bench="$1"
  local macro="$2"
  local math="$3"
  local tune="$4"
  local wasm="$5"
  local math_flags=()
  local tune_flags=()
  if [[ "$math" == "fast" ]]; then
    math_flags=(-ffast-math)
  else
    math_flags=(-fno-fast-math)
  fi
  if [[ "$tune" != "default" ]]; then
    tune_flags=("-mtune=$tune")
  fi

  "$CLANG" \
    --target=wasm32-unknown-unknown \
    -mcpu=mvp \
    "${tune_flags[@]}" \
    -O3 \
    -fno-builtin \
    -nostdlib \
    "${math_flags[@]}" \
    "-D$macro" \
    "-DITER=$ITER" \
    -fuse-ld="$WASM_LD" \
    -Wl,--no-entry \
    -Wl,--export=_start \
    -Wl,--export=bench_result_f64 \
    -Wl,--export=bench_result_u64 \
    -o "$wasm" \
    "$SRC"
}

dump_shape() {
  local bench="$1"
  local macro="$2"
  local math="$3"
  local tune="$4"
  local math_flags=()
  local tune_flags=()
  local log="$OUT/log/$bench.$tune.shape.log"
  if [[ "$math" == "fast" ]]; then
    math_flags=(-ffast-math)
  else
    math_flags=(-fno-fast-math)
  fi
  if [[ "$tune" != "default" ]]; then
    tune_flags=("-mtune=$tune")
  fi

  "$CLANG" \
    --target=wasm32-unknown-unknown \
    -mcpu=mvp \
    "${tune_flags[@]}" \
    -O3 \
    -fno-builtin \
    -nostdlib \
    "${math_flags[@]}" \
    "-D$macro" \
    "-DITER=$ITER" \
    -mllvm -debug-only=wasm-reg-stackify \
    -mllvm -wasm-tune-shape-dump \
    -S \
    -o /dev/null \
    "$SRC" >"$log" 2>&1

  awk -v bench="$bench" -v tune="$tune" -F'[ =]' '
    /^wasm-tune-shape:/ {
      delete v;
      for (i = 2; i <= NF; i += 2)
        v[$i] = $(i + 1);
      printf "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
             bench, tune, v["function"], v["move"], v["remat"], v["tee"],
             v["keep-local"], v["historical-kept"], v["profile-changed"],
             v["profile-commute-tried"], v["profile-commute-accepted"],
             v["product-bank-boundary"], v["int-mild-overflow"],
             v["int-severe-overflow"], v["fp-overflow"], v["product-bank"],
             v["m3-fp-bank"], v["m3-distance"],
             v["uwvm2-strict-fp-accum"], v["est-local-get"],
             v["est-local-set"], v["est-tee"], v["keep-local-boundary"],
             v["delay-local-rhs-commute-tried"],
             v["delay-local-rhs-commute-accepted"];
    }' "$log" >> "$SHAPE_CSV"
}

measure() {
  local bench="$1"
  local tune="$2"
  local runtime="$3"
  local wasm="$4"
  shift 4

  local times=()
  local status="ok"
  for ((run = 1; run <= REPEAT; ++run)); do
    local tfile="$OUT/log/$bench.$tune.$runtime.$run.time"
    local lfile="$OUT/log/$bench.$tune.$runtime.$run.log"
    if /usr/bin/time -f '%e' -o "$tfile" "$@" "$wasm" >"$lfile" 2>&1; then
      times+=("$(cat "$tfile")")
    else
      status="fail"
      break
    fi
  done

  if [[ "$status" != "ok" ]]; then
    printf '%s,%s,%s,%s,%s,NA,NA,%s\n' "$bench" "$tune" "$runtime" "$ITER" "$REPEAT" "$status" >> "$CSV"
    return
  fi

  awk -v bench="$bench" -v tune="$tune" -v runtime="$runtime" -v iter="$ITER" \
      -v repeat="$REPEAT" '
    BEGIN {
      min = 1e99;
      sum = 0.0;
      for (i = 1; i < ARGC; ++i) {
        t = ARGV[i] + 0.0;
        if (t < min) min = t;
        sum += t;
      }
      printf "%s,%s,%s,%s,%s,%.6f,%.6f,ok\n",
             bench, tune, runtime, iter, repeat, min, sum / (ARGC - 1);
    }' "${times[@]}" >> "$CSV"
}

for def in "${bench_defs[@]}"; do
  read -r bench macro math <<< "$def"
  for tune in "${tunes[@]}"; do
    wasm="$OUT/wasm/$bench.$tune.wasm"
    compile_wasm "$bench" "$macro" "$math" "$tune" "$wasm"
    dump_shape "$bench" "$macro" "$math" "$tune"
    "$UWVM" "${uwvm_extra_args[@]}" --mode validation "$wasm" >"$OUT/log/$bench.$tune.validation.log" 2>&1
  done
  measure "$bench" default uwvm-rint "$OUT/wasm/$bench.default.wasm" \
    "$UWVM" "${uwvm_extra_args[@]}" -Rint --
  measure "$bench" "$UWVM2_TUNE" uwvm-rint "$OUT/wasm/$bench.$UWVM2_TUNE.wasm" \
    "$UWVM" "${uwvm_extra_args[@]}" -Rint --
  measure "$bench" default wasm3 "$OUT/wasm/$bench.default.wasm" "$WASM3"
  measure "$bench" "$WASM3_TUNE" wasm3 "$OUT/wasm/$bench.$WASM3_TUNE.wasm" \
    "$WASM3"
done

printf 'results: %s\n' "$CSV"
printf 'shape: %s\n' "$SHAPE_CSV"
