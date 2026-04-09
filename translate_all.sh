#!/bin/bash

dir="."
action="translate"
bench_report=""
cc="gcc"
nvcc="nvcc"
cflags="-O2 -lm"
nvccflags="-O2"
warmup=3
runs=10

# 参数解析
while [ $# -gt 0 ]; do
    case "$1" in
        -d) dir="$2"; shift 2 ;;
        translate|benchmark) action="$1"; shift ;;
        -o|--report) bench_report="$2"; shift 2 ;;
        --cc) cc="$2"; shift 2 ;;
        --nvcc) nvcc="$2"; shift 2 ;;
        --cflags) cflags="$2"; shift 2 ;;
        --nvccflags) nvccflags="$2"; shift 2 ;;
        --warmup) warmup="$2"; shift 2 ;;
        --runs) runs="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [translate|benchmark] [options]"
            echo ""
            echo "Commands:"
            echo "  translate          翻译.c为.cu (默认)"
            echo "  benchmark          编译.c和.cu并用hyperfine对比性能"
            echo ""
            echo "Options:"
            echo "  -d DIR             目标目录 (默认: .)"
            echo "  -o, --report F     benchmark报告文件 (默认: build_report/benchmark_report_{DIR}.md)"
            echo "  --cc CMD           C编译器 (默认: gcc)"
            echo "  --nvcc CMD         CUDA编译器 (默认: nvcc)"
            echo "  --cflags FLAGS     C编译选项 (默认: -O2 -lm)"
            echo "  --nvccflags FLAGS  CUDA编译选项 (默认: -O2)"
            echo "  --warmup N         hyperfine预热次数 (默认: 3)"
            echo "  --runs N           hyperfine运行次数 (默认: 10)"
            exit 0 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

do_translate() {
    local total_files=0
    local success_files=0
    local fail_files=0
    local total_ms=0

    while IFS= read -r -d '' file; do
        local cu_file="${file%.c}.cu"
        local log_file="${file%.c}.log"

        total_files=$((total_files + 1))
        echo "[$total_files] processing: $file -> $cu_file"

        local start_time=$(date +%s%N)
        ./build/bin/translate.out "$file" -rose:o "$cu_file" > "$log_file" 2>&1
        local exit_code=$?
        local end_time=$(date +%s%N)

        local elapsed_ms=$(( (end_time - start_time) / 1000000 ))
        total_ms=$((total_ms + elapsed_ms))

        if [ $exit_code -eq 0 ]; then
            success_files=$((success_files + 1))
            echo "  done in ${elapsed_ms}ms, log: $log_file"
        else
            fail_files=$((fail_files + 1))
            echo "  FAILED (exit $exit_code) in ${elapsed_ms}ms, log: $log_file"
        fi
    done < <(find "$dir" -name "*.c" -print0)

    echo ""
    echo "===== Translate Summary ====="
    echo "Total files : $total_files"
    echo "Success     : $success_files"
    echo "Failed      : $fail_files"
    echo "Total time  : ${total_ms}ms"
}

do_benchmark() {
    # 检查依赖
    for cmd in hyperfine "$cc" "$nvcc"; do
        if ! command -v "$cmd" &> /dev/null; then
            echo "Error: $cmd not found in PATH"
            exit 1
        fi
    done

    # 根据dir名生成默认报告文件名
    if [ -z "$bench_report" ]; then
        local dir_label
        dir_label="$(basename "$(realpath "$dir")")"
        bench_report="build_report/benchmark_report_${dir_label}.md"
    fi

    # 确保jq可用（表格解析需要）
    if ! command -v jq &> /dev/null; then
        echo "Error: jq not found in PATH (required for report generation)"
        exit 1
    fi

    # 确保build目录存在
    mkdir -p "$(dirname "$bench_report")"

    local total=0
    local bench_ok=0
    local bench_fail=0
    local report_abs
    report_abs="$(cd "$(dirname "$bench_report")" && pwd)/$(basename "$bench_report")"

    local dir_abs
    dir_abs="$(realpath "$dir")"

    # 写表头
    {
        echo ""
        echo "# Benchmark Report - $(date '+%Y-%m-%d %H:%M:%S')"
        echo ""
        echo "**Test Directory:** \`$dir_abs\`"
        echo ""
        echo "| C Source | CUDA Source | C Mean | C StdDev | C Min | C Max | CUDA Mean | CUDA StdDev | CUDA Min | CUDA Max | Speedup |"
        echo "|----------|------------|--------|----------|-------|-------|-----------|-------------|----------|----------|---------|"
    } >> "$bench_report"

    while IFS= read -r -d '' c_file; do
        local cu_file="${c_file%.c}.cu"

        if [ ! -f "$cu_file" ]; then
            echo "[skip] $c_file (no matching .cu)"
            continue
        fi

        total=$((total + 1))
        local base="${c_file%.c}"
        local c_bin="${base}_c_bin"
        local cu_bin="${base}_cu_bin"
        local hyperfine_json="${base}_bench.json"

        # 获取绝对路径
        local c_abs cu_abs
        c_abs="$(realpath "$c_file")"
        cu_abs="$(realpath "$cu_file")"

        echo "[$total] benchmarking: $c_abs vs $cu_abs"

        # 编译C
        echo "  compiling C:    $cc $cflags -o $c_bin $c_file"
        if ! $cc $cflags -o "$c_bin" "$c_file" 2>&1; then
            echo "  FAILED: C compile error"
            echo "| $c_abs | $cu_abs | COMPILE FAILED | - | - | - | - | - | - | - | - |" >> "$bench_report"
            bench_fail=$((bench_fail + 1))
            rm -f "$c_bin"
            continue
        fi

        # 编译CUDA
        echo "  compiling CUDA: $nvcc $nvccflags -o $cu_bin $cu_file"
        if ! $nvcc $nvccflags -o "$cu_bin" "$cu_file" 2>&1; then
            echo "  FAILED: CUDA compile error"
            echo "| $c_abs | $cu_abs | - | - | - | - | COMPILE FAILED | - | - | - | - |" >> "$bench_report"
            bench_fail=$((bench_fail + 1))
            rm -f "$c_bin" "$cu_bin"
            continue
        fi

        # hyperfine对比
        echo "  running hyperfine..."
        hyperfine \
            --warmup "$warmup" \
            --runs "$runs" \
            --export-json "$hyperfine_json" \
            --command-name "C" "$c_bin" \
            --command-name "CUDA" "$cu_bin"

        # 从json提取数据写入表格行
        if [ -f "$hyperfine_json" ]; then
            local row
            row=$(jq -r --arg csrc "$c_abs" --arg cusrc "$cu_abs" '
                def fmtu:
                    if . < 1 then  # < 1ms, show as us
                        (. * 1000 * 1000 | round) / 1000 | tostring | split(".") |
                        (if length > 1 then .[0] + "." + .[1][:3] else .[0] + ".000" end) + "us"
                    else           # >= 1ms, show as ms
                        (. * 1000 | round) / 1000 | tostring | split(".") |
                        (if length > 1 then .[0] + "." + .[1][:3] else .[0] + ".000" end) + "ms"
                    end;
                (.results[] | select(.command == "C")) as $c |
                (.results[] | select(.command == "CUDA")) as $cu |
                ($c.mean / $cu.mean) as $speedup |
                (($speedup * 100 | round) / 100 | tostring) as $sp |
                "| \($csrc) | \($cusrc) | \($c.mean*1000|fmtu) | \($c.stddev*1000|fmtu) | \($c.min*1000|fmtu) | \($c.max*1000|fmtu) | \($cu.mean*1000|fmtu) | \($cu.stddev*1000|fmtu) | \($cu.min*1000|fmtu) | \($cu.max*1000|fmtu) | \($sp)x |"
            ' "$hyperfine_json")
            echo "$row" >> "$bench_report"
        fi

        bench_ok=$((bench_ok + 1))

        # 清理编译产物
        rm -f "$c_bin" "$cu_bin"
    done < <(find "$dir" -name "*.c" -print0)

    echo "" >> "$bench_report"

    echo ""
    echo "===== Benchmark Summary ====="
    echo "Total pairs : $total"
    echo "Benchmarked : $bench_ok"
    echo "Failed      : $bench_fail"
    echo "Report      : $report_abs"
}

# 执行对应操作
case "$action" in
    translate)  do_translate ;;
    benchmark)  do_benchmark ;;
esac
