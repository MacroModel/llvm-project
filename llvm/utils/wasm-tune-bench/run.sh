#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
SRC="$ROOT/llvm/utils/wasm-tune-bench/microbench.c"

LLVM_BUILD="${LLVM_BUILD:-$ROOT/build-wasm-tune}"
CLANG="${CLANG:-$LLVM_BUILD/bin/clang}"
WASM_LD="${WASM_LD:-$LLVM_BUILD/bin/wasm-ld}"

if [[ "$(uname -s)" == "Darwin" ]]; then
  UWVM="${UWVM:-/Users/liyinan/Documents/MacroModel/src/uwvm2/build/macosx/arm64/release/uwvm}"
  WASM3="${WASM3:-/Users/liyinan/Documents/MacroModel/src/wasm3/build-clang-release/wasm3}"
  SYSROOT="${SYSROOT:-/Users/liyinan/Documents/MacroModel/src/wasi-libc/build-mvp/sysroot}"
  UWVM2_TUNE="${UWVM2_TUNE:-u2-aapcs64}"
else
  UWVM="${UWVM:-/home/macromodel/Documents/src/uwvm2/build/linux/x86_64/release/uwvm}"
  WASM3="${WASM3:-/home/macromodel/Documents/src/wasm3/build-clang-release/wasm3}"
  SYSROOT="${SYSROOT:-}"
  UWVM2_TUNE="${UWVM2_TUNE:-uwvm2}"
fi

TARGET="${TARGET:-wasm32-wasip1}"
UWVM_EXTRA_ARGS="${UWVM_EXTRA_ARGS:-}"
UWVM_RUN_ARGS="${UWVM_RUN_ARGS:--Rcc int -Rcm full --}"
UWVM_VALIDATION_ARGS="${UWVM_VALIDATION_ARGS:---mode validation}"
CLANG_EXTRA_FLAGS="${CLANG_EXTRA_FLAGS:-}"

OUT="${OUT:-/tmp/wasm-tune-bench}"
ITER="${ITER:-200000}"
REPEAT="${REPEAT:-3}"
WASM3_TUNE="${WASM3_TUNE:-m3}"
BENCH_FILTER="${BENCH_FILTER:-}"

mkdir -p "$OUT/wasm" "$OUT/log"
CSV="$OUT/results.csv"
SHAPE_CSV="$OUT/shape-summary.csv"
PAIR_CSV="$OUT/pairs.csv"

printf 'bench,tune,runtime,iter,repeat,min_s,avg_s,status\n' > "$CSV"
printf 'bench,tune,function,move,remat,tee,keep_local,historical_kept,profile_changed,profile_commute_tried,profile_commute_accepted,product_bank_boundary,int_mild_overflow,int_severe_overflow,fp_overflow,product_bank,m3_fp_bank,m3_distance,uwvm2_strict_fp_accum,uwvm2_move_int_boundary,uwvm2_tee_int_boundary,est_local_get,est_local_set,est_tee,keep_local_boundary,delay_local_rhs_commute_tried,delay_local_rhs_commute_accepted,localget2_scale_commute_tried,localget2_scale_commute_accepted\n' > "$SHAPE_CSV"
printf 'bench,tune,runtime,iter,repeat,run,default_s,tuned_s,ratio,status\n' > "$PAIR_CSV"

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
  "stencil5_fast BENCH_STENCIL5 fast"
  "stencil5_strict BENCH_STENCIL5 strict"
  "poly_fast BENCH_POLY fast"
  "poly_strict BENCH_POLY strict"
  "mat3_fast BENCH_MAT3 fast"
  "mat3_strict BENCH_MAT3 strict"
  "jacobi2d_fast BENCH_JACOBI2D fast"
  "jacobi2d_strict BENCH_JACOBI2D strict"
  "spmv4_fast BENCH_SPMV4 fast"
  "spmv4_strict BENCH_SPMV4 strict"
  "tridiag8_fast BENCH_TRIDIAG8 fast"
  "tridiag8_strict BENCH_TRIDIAG8 strict"
  "mandelbrot12_fast BENCH_MANDELBROT12 fast"
  "mandelbrot12_strict BENCH_MANDELBROT12 strict"
  "address_i32 BENCH_ADDR strict"
  "byte_hash_i32 BENCH_BYTE_HASH strict"
  "hash_lookup_i32 BENCH_HASH_LOOKUP strict"
  "hash_update_i32 BENCH_HASH_UPDATE strict"
  "wide_i64_mix BENCH_I64_MIX strict"
  "tree_walk_i32 BENCH_TREE_WALK strict"
  "particle_struct BENCH_PARTICLE fast"
  "record_struct BENCH_RECORD strict"
  "struct_scan BENCH_STRUCT_SCAN fast"
  "mixed_addr_fp BENCH_MIXED fast"
)

tunes=(default "$UWVM2_TUNE" "$WASM3_TUNE")
read -r -a uwvm_extra_args <<< "$UWVM_EXTRA_ARGS"
read -r -a uwvm_run_args <<< "$UWVM_RUN_ARGS"
read -r -a uwvm_validation_args <<< "$UWVM_VALIDATION_ARGS"
read -r -a clang_extra_flags <<< "$CLANG_EXTRA_FLAGS"

target_flags=(
  "--target=$TARGET"
  -mcpu=mvp
  -mno-bulk-memory
  -mno-bulk-memory-opt
  -mno-nontrapping-fptoint
  -mno-sign-ext
  -mno-mutable-globals
  -mno-multivalue
  -mno-reference-types
  -mno-call-indirect-overlong
)
if [[ -n "$SYSROOT" ]]; then
  target_flags+=("--sysroot=$SYSROOT")
fi

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
    "${target_flags[@]}" \
    "${tune_flags[@]}" \
    -O3 \
    -fno-builtin \
    -nostdlib \
    "${math_flags[@]}" \
    "${clang_extra_flags[@]}" \
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
    "${target_flags[@]}" \
    "${tune_flags[@]}" \
    -O3 \
    -fno-builtin \
    -nostdlib \
    "${math_flags[@]}" \
    "${clang_extra_flags[@]}" \
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
      printf "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
             bench, tune, v["function"], v["move"], v["remat"], v["tee"],
             v["keep-local"], v["historical-kept"], v["profile-changed"],
             v["profile-commute-tried"], v["profile-commute-accepted"],
             v["product-bank-boundary"], v["int-mild-overflow"],
             v["int-severe-overflow"], v["fp-overflow"], v["product-bank"],
             v["m3-fp-bank"], v["m3-distance"],
             v["uwvm2-strict-fp-accum"], v["uwvm2-move-int-boundary"],
             v["uwvm2-tee-int-boundary"], v["est-local-get"],
             v["est-local-set"], v["est-tee"], v["keep-local-boundary"],
             v["delay-local-rhs-commute-tried"],
             v["delay-local-rhs-commute-accepted"],
             v["localget2-scale-commute-tried"],
             v["localget2-scale-commute-accepted"];
    }' "$log" >> "$SHAPE_CSV"
}

run_timed() {
  local tfile="$1"
  local lfile="$2"
  shift 2

  python3 - "$tfile" "$lfile" "$@" <<'PY'
import pathlib
import subprocess
import sys
import time

tfile = pathlib.Path(sys.argv[1])
lfile = pathlib.Path(sys.argv[2])
cmd = sys.argv[3:]
start = time.perf_counter()
proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
elapsed = time.perf_counter() - start
lfile.write_bytes(proc.stdout)
tfile.write_text(f"{elapsed:.9f}\n")
sys.exit(proc.returncode)
PY
}

emit_measure_row() {
  local bench="$1"
  local tune="$2"
  local runtime="$3"
  local status="$4"
  shift 4
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
    }' "$@" >> "$CSV"
}

emit_pair_rows() {
  local bench="$1"
  local tune="$2"
  local runtime="$3"
  local status="$4"
  shift 4
  local times=("$@")

  if [[ "$status" != "ok" ]]; then
    printf '%s,%s,%s,%s,%s,NA,NA,NA,NA,%s\n' \
      "$bench" "$tune" "$runtime" "$ITER" "$REPEAT" "$status" >> "$PAIR_CSV"
    return
  fi

  local n=$((${#times[@]} / 2))
  for ((i = 0; i < n; ++i)); do
    local default_s="${times[$((i * 2))]}"
    local tuned_s="${times[$((i * 2 + 1))]}"
    awk -v bench="$bench" -v tune="$tune" -v runtime="$runtime" \
        -v iter="$ITER" -v repeat="$REPEAT" -v run="$((i + 1))" \
        -v default_s="$default_s" -v tuned_s="$tuned_s" '
      BEGIN {
        ratio = tuned_s == 0 ? 0 : default_s / tuned_s;
        printf "%s,%s,%s,%s,%s,%s,%.9f,%.9f,%.9f,ok\n",
               bench, tune, runtime, iter, repeat, run,
               default_s, tuned_s, ratio;
      }' >> "$PAIR_CSV"
  done
}

measure_pair() {
  local bench="$1"
  local tune_a="$2"
  local tune_b="$3"
  local runtime="$4"
  local wasm_a="$5"
  local wasm_b="$6"
  shift 6

  local times_a=()
  local times_b=()
  local status_a="ok"
  local status_b="ok"

  for ((run = 1; run <= REPEAT; ++run)); do
    local first="a"
    local second="b"
    if ((run % 2 == 0)); then
      first="b"
      second="a"
    fi

    for side in "$first" "$second"; do
      if [[ "$side" == "a" ]]; then
        local tfile="$OUT/log/$bench.$tune_a.$runtime.$run.time"
        local lfile="$OUT/log/$bench.$tune_a.$runtime.$run.log"
        if [[ "$status_a" == "ok" ]] &&
           run_timed "$tfile" "$lfile" "$@" "$wasm_a"; then
          times_a+=("$(cat "$tfile")")
        else
          status_a="fail"
        fi
      else
        local tfile="$OUT/log/$bench.$tune_b.$runtime.$run.time"
        local lfile="$OUT/log/$bench.$tune_b.$runtime.$run.log"
        if [[ "$status_b" == "ok" ]] &&
           run_timed "$tfile" "$lfile" "$@" "$wasm_b"; then
          times_b+=("$(cat "$tfile")")
        else
          status_b="fail"
        fi
      fi
    done

    if [[ "$status_a" != "ok" || "$status_b" != "ok" ]]; then
      break
    fi
  done

  emit_measure_row "$bench" "$tune_a" "$runtime" "$status_a" "${times_a[@]}"
  emit_measure_row "$bench" "$tune_b" "$runtime" "$status_b" "${times_b[@]}"
  if [[ "$status_a" == "ok" && "$status_b" == "ok" &&
        ${#times_a[@]} -eq ${#times_b[@]} ]]; then
    local pairs=()
    for ((i = 0; i < ${#times_a[@]}; ++i)); do
      pairs+=("${times_a[$i]}" "${times_b[$i]}")
    done
    emit_pair_rows "$bench" "$tune_b" "$runtime" "ok" "${pairs[@]}"
  else
    emit_pair_rows "$bench" "$tune_b" "$runtime" "fail"
  fi
}

for def in "${bench_defs[@]}"; do
  read -r bench macro math <<< "$def"
  if [[ -n "$BENCH_FILTER" && ! "$bench" =~ $BENCH_FILTER ]]; then
    continue
  fi
  for tune in "${tunes[@]}"; do
    wasm="$OUT/wasm/$bench.$tune.wasm"
    compile_wasm "$bench" "$macro" "$math" "$tune" "$wasm"
    dump_shape "$bench" "$macro" "$math" "$tune"
    "$UWVM" "${uwvm_extra_args[@]}" "${uwvm_validation_args[@]}" "$wasm" >"$OUT/log/$bench.$tune.validation.log" 2>&1
  done
  measure_pair "$bench" default "$UWVM2_TUNE" uwvm-rint \
    "$OUT/wasm/$bench.default.wasm" "$OUT/wasm/$bench.$UWVM2_TUNE.wasm" \
    "$UWVM" "${uwvm_extra_args[@]}" "${uwvm_run_args[@]}"
  measure_pair "$bench" default "$WASM3_TUNE" wasm3 \
    "$OUT/wasm/$bench.default.wasm" "$OUT/wasm/$bench.$WASM3_TUNE.wasm" \
    "$WASM3"
done

printf 'results: %s\n' "$CSV"
printf 'shape: %s\n' "$SHAPE_CSV"
printf 'pairs: %s\n' "$PAIR_CSV"
