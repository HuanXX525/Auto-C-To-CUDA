#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tests/run_compare.sh -d <target_dir> [options]

Options:
  -d DIR            Target directory that contains .c files to test (required)
  --recursive       Search .c files recursively under DIR
  --cc CMD          C compiler (default: gcc)
  --nvcc CMD        CUDA compiler (default: nvcc)
  --cflags FLAGS    Extra C compile flags (default: -O2)
  --nvccflags F     Extra CUDA compile flags (default: -O2)
  -h, --help        Show help

Example:
  tests/run_compare.sh -d test/induction
  tests/run_compare.sh -d tests/preprocess --recursive
EOF
}

target_dir=""
recursive=0
cc="gcc"
nvcc="nvcc"
cflags="-O2"
nvccflags="-O2"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -d)
            target_dir="${2:-}"
            shift 2
            ;;
        --recursive)
            recursive=1
            shift
            ;;
        --cc)
            cc="${2:-}"
            shift 2
            ;;
        --nvcc)
            nvcc="${2:-}"
            shift 2
            ;;
        --cflags)
            cflags="${2:-}"
            shift 2
            ;;
        --nvccflags)
            nvccflags="${2:-}"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ -z "$target_dir" ]]; then
    echo "Error: -d <target_dir> is required." >&2
    usage
    exit 1
fi

if [[ ! -d "$target_dir" ]]; then
    echo "Error: target directory does not exist: $target_dir" >&2
    exit 1
fi

translate_bin="./build/bin/translate.out"
if [[ ! -x "$translate_bin" ]]; then
    echo "Error: translate binary not found: $translate_bin" >&2
    echo "Run: make -j\$(nproc)" >&2
    exit 1
fi

if ! command -v "$cc" >/dev/null 2>&1; then
    echo "Error: C compiler not found: $cc" >&2
    exit 1
fi
if ! command -v "$nvcc" >/dev/null 2>&1; then
    echo "Error: CUDA compiler not found: $nvcc" >&2
    exit 1
fi

run_root="/tmp/c2cuda_compare_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$run_root"

find_args=("$target_dir" -type f -name "*.c")
if [[ $recursive -eq 0 ]]; then
    find_args=("$target_dir" -maxdepth 1 -type f -name "*.c")
fi

mapfile -t c_files < <(find "${find_args[@]}" | sort)
if [[ ${#c_files[@]} -eq 0 ]]; then
    echo "No .c files found in: $target_dir"
    exit 1
fi

pass=0
fail=0

echo "== C/CUDA Compare =="
echo "Target: $target_dir"
echo "Files : ${#c_files[@]}"
echo "Logs  : $run_root"
echo

for c_file in "${c_files[@]}"; do
    base_name="$(basename "${c_file%.c}")"
    cu_file="${c_file%.c}.cu"
    work_dir="$run_root/$base_name"
    mkdir -p "$work_dir"

    c_bin="$work_dir/${base_name}_c.out"
    cu_bin="$work_dir/${base_name}_cu.out"
    c_stdout="$work_dir/${base_name}.c.stdout"
    cu_stdout="$work_dir/${base_name}.cu.stdout"
    translate_log="$work_dir/translate.log"
    c_build_log="$work_dir/c_build.log"
    cu_build_log="$work_dir/cu_build.log"
    c_run_log="$work_dir/c_run.log"
    cu_run_log="$work_dir/cu_run.log"
    diff_log="$work_dir/diff.log"

    echo "[RUN] $c_file"

    if ! "$translate_bin" "$c_file" -rose:o "$cu_file" >"$translate_log" 2>&1; then
        echo "  FAIL: translate failed (see $translate_log)"
        fail=$((fail + 1))
        continue
    fi

    # Keep -lm after source to avoid link-order issues with GCC.
    if ! $cc $cflags "$c_file" -lm -o "$c_bin" >"$c_build_log" 2>&1; then
        echo "  FAIL: C compile failed (see $c_build_log)"
        fail=$((fail + 1))
        continue
    fi

    if ! $nvcc $nvccflags "$cu_file" -o "$cu_bin" >"$cu_build_log" 2>&1; then
        echo "  FAIL: CUDA compile failed (see $cu_build_log)"
        fail=$((fail + 1))
        continue
    fi

    if ! "$c_bin" >"$c_stdout" 2>"$c_run_log"; then
        echo "  FAIL: C run failed (see $c_run_log)"
        fail=$((fail + 1))
        continue
    fi

    if ! "$cu_bin" >"$cu_stdout" 2>"$cu_run_log"; then
        echo "  FAIL: CUDA run failed (see $cu_run_log)"
        fail=$((fail + 1))
        continue
    fi

    if diff -u "$c_stdout" "$cu_stdout" >"$diff_log"; then
        echo "  PASS"
        pass=$((pass + 1))
    else
        echo "  FAIL: output mismatch (see $diff_log)"
        fail=$((fail + 1))
    fi
done

echo
echo "== Summary =="
echo "PASS: $pass"
echo "FAIL: $fail"
echo "Artifacts: $run_root"

if [[ $fail -gt 0 ]]; then
    exit 2
fi
