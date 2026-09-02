#!/usr/bin/env bash
# Run a reproducible GCSA2 construction benchmark under a hard cgroup ceiling.
# The internal budget is deliberately separate from MemoryMax: leave several
# GiB for vg graph loading, allocators, OpenMP, SDSL, and compression libraries.

set -euo pipefail

vg_binary=""
graph=""
mapping=""
run_directory=""
memory_limit="23G"
cgroup_limit="25G"
disk_limit="4T"
sort_run_size=""
join_partition_size=""
threads="32"
kmer_length="16"
doubling_steps="4"
sample_seconds="30"
resume=0

usage() {
    echo "usage: $0 --vg PATH --graph PATH --run-dir DIR [options]" >&2
    echo "  --mapping PATH         node mapping produced by vg prune -m" >&2
    echo "  --memory-limit SIZE    internal GCSA2 budget [23G]" >&2
    echo "  --cgroup-limit SIZE    hard systemd MemoryMax [25G]" >&2
    echo "  --disk-limit SIZE      GCSA2 disk budget [4T]" >&2
    echo "  --sort-run-size SIZE   label-sort workspace [memory limit]" >&2
    echo "  --join-partition-size SIZE join-sort workspace [memory limit]" >&2
    echo "  --threads N            construction threads [32]" >&2
    echo "  --kmer-length N        initial k-mer length [16]" >&2
    echo "  --doubling-steps N     prefix-doubling steps [4]" >&2
    echo "  --sample-seconds N     resource sampling interval [30]" >&2
    echo "  --resume               reuse committed workspace tasks" >&2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --vg) vg_binary="$2"; shift 2 ;;
        --graph) graph="$2"; shift 2 ;;
        --mapping) mapping="$2"; shift 2 ;;
        --run-dir) run_directory="$2"; shift 2 ;;
        --memory-limit) memory_limit="$2"; shift 2 ;;
        --cgroup-limit) cgroup_limit="$2"; shift 2 ;;
        --disk-limit) disk_limit="$2"; shift 2 ;;
        --sort-run-size) sort_run_size="$2"; shift 2 ;;
        --join-partition-size) join_partition_size="$2"; shift 2 ;;
        --threads) threads="$2"; shift 2 ;;
        --kmer-length) kmer_length="$2"; shift 2 ;;
        --doubling-steps) doubling_steps="$2"; shift 2 ;;
        --sample-seconds) sample_seconds="$2"; shift 2 ;;
        --resume) resume=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage; exit 2 ;;
    esac
done

if [[ -z "$vg_binary" || -z "$graph" || -z "$run_directory" ]]; then
    usage
    exit 2
fi

# These are independent GCSA2 operational caps. Defaulting them to the
# benchmark's memory ceiling makes a larger benchmark budget actually trade
# RAM for fewer merge passes; GCSA2 still reserves only the active phase.
if [[ -z "$sort_run_size" ]]; then
    sort_run_size="$memory_limit"
fi
if [[ -z "$join_partition_size" ]]; then
    join_partition_size="$memory_limit"
fi
if [[ ! -x "$vg_binary" || ! -r "$graph" || ( -n "$mapping" && ! -r "$mapping" ) ]]; then
    echo "vg, graph, or mapping is not executable/readable" >&2
    exit 2
fi
if ! systemctl --user show-environment >/dev/null 2>&1; then
    echo "a user systemd manager is required to enforce MemoryMax" >&2
    exit 2
fi

mkdir -p "$run_directory"
run_directory="$(cd "$run_directory" && pwd)"
work_directory="$run_directory/index.gcsa-work"
output_name="$run_directory/index.gcsa"
time_log="$run_directory/time.txt"
stderr_log="$run_directory/stderr.log"
stdout_log="$run_directory/stdout.log"
samples="$run_directory/resource_samples.tsv"
summary="$run_directory/summary.tsv"
command_file="$run_directory/command.txt"
unit="vg-gcsa-$USER-$(date +%Y%m%dT%H%M%S)-$$"

if [[ $resume -eq 0 && ( -e "$work_directory/build.json" || -s "$summary" ) ]]; then
    echo "run directory already contains committed work; use --resume" >&2
    exit 2
fi

command=("$vg_binary" index -p -V -g "$output_name" -k "$kmer_length"
    -X "$doubling_steps" -t "$threads"
    --gcsa-work-dir "$work_directory"
    --gcsa-memory-limit "$memory_limit"
    --gcsa-disk-limit "$disk_limit"
    --gcsa-sort-run-size "$sort_run_size"
    --gcsa-join-partition-size "$join_partition_size")
if [[ -n "$mapping" ]]; then
    command+=(-f "$mapping")
fi
if [[ $resume -eq 1 ]]; then
    command+=(--gcsa-resume)
fi
command+=("$graph")

{
    printf 'unit=%q\n' "$unit.scope"
    printf 'vg_sha256=%s\n' "$(sha256sum "$vg_binary" | awk '{print $1}')"
    printf 'command='
    printf '%q ' "${command[@]}"
    printf '\n'
} > "$command_file"
{
    printf 'kind\tpath\tbytes\tsha256\n'
    graph_bytes="$(stat -c %s "$graph")"
    graph_sha="$(sha256sum "$graph" | awk '{print $1}')"
    printf 'graph\t%s\t%s\t%s\n' "$graph" "$graph_bytes" "$graph_sha"
    if [[ -n "$mapping" ]]; then
        mapping_bytes="$(stat -c %s "$mapping")"
        mapping_sha="$(sha256sum "$mapping" | awk '{print $1}')"
        printf 'mapping\t%s\t%s\t%s\n' "$mapping" "$mapping_bytes" "$mapping_sha"
    fi
} > "$run_directory/inputs.tsv"

printf 'unix_time\trun_bytes\tmemory_current\tmemory_peak\tmem_available_kib\n' > "$samples"

runner_pid=""
cleanup_runner() {
    if [[ -n "$runner_pid" ]] && kill -0 "$runner_pid" 2>/dev/null; then
        systemctl --user kill "$unit.scope" >/dev/null 2>&1 || true
        wait "$runner_pid" 2>/dev/null || true
    fi
}
trap cleanup_runner INT TERM HUP

systemd-run --user --scope --quiet --unit="$unit" \
    -p "MemoryMax=$cgroup_limit" -p MemorySwapMax=0 \
    /usr/bin/time -v -o "$time_log" "${command[@]}" \
    >"$stdout_log" 2>"$stderr_log" &
runner_pid=$!

while kill -0 "$runner_pid" 2>/dev/null; do
    run_bytes="$(du -sb "$run_directory" 2>/dev/null | awk '{print $1}')"
    memory_current="$(systemctl --user show "$unit.scope" -p MemoryCurrent --value 2>/dev/null || true)"
    memory_peak="$(systemctl --user show "$unit.scope" -p MemoryPeak --value 2>/dev/null || true)"
    mem_available="$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)"
    printf '%s\t%s\t%s\t%s\t%s\n' "$(date +%s)" "${run_bytes:-0}" \
        "${memory_current:-0}" "${memory_peak:-0}" "${mem_available:-0}" >> "$samples"
    sleep "$sample_seconds"
done

set +e
wait "$runner_pid"
exit_code=$?
set -e
runner_pid=""

peak_run_bytes="$(awk 'NR > 1 && $2 > max {max=$2} END {print max+0}' "$samples")"
peak_cgroup_bytes="$(awk 'NR > 1 && $4 ~ /^[0-9]+$/ && $4 > max {max=$4} END {print max+0}' "$samples")"
max_rss_kib="$(awk -F: '/Maximum resident set size/ {gsub(/[[:space:]]/, "", $2); print $2}' "$time_log" 2>/dev/null || true)"
elapsed_seconds="$(awk '/Elapsed \(wall clock\) time/ {sub(/^.*\):[[:space:]]*/, ""); print}' "$time_log" 2>/dev/null || true)"
read_blocks="$(awk -F: '/File system inputs/ {gsub(/[[:space:]]/, "", $2); print $2}' "$time_log" 2>/dev/null || true)"
write_blocks="$(awk -F: '/File system outputs/ {gsub(/[[:space:]]/, "", $2); print $2}' "$time_log" 2>/dev/null || true)"
read_bytes="unknown"
write_bytes="unknown"
if [[ "$read_blocks" =~ ^[0-9]+$ ]]; then
    read_bytes=$((read_blocks * 512))
fi
if [[ "$write_blocks" =~ ^[0-9]+$ ]]; then
    write_bytes=$((write_blocks * 512))
fi

{
    printf 'metric\tvalue\n'
    printf 'exit_code\t%s\n' "$exit_code"
    printf 'internal_memory_limit\t%s\n' "$memory_limit"
    printf 'cgroup_memory_max\t%s\n' "$cgroup_limit"
    printf 'disk_limit\t%s\n' "$disk_limit"
    printf 'sort_run_size\t%s\n' "$sort_run_size"
    printf 'join_partition_size\t%s\n' "$join_partition_size"
    printf 'vg_sha256\t%s\n' "$(sha256sum "$vg_binary" | awk '{print $1}')"
    printf 'max_rss_kib\t%s\n' "${max_rss_kib:-unknown}"
    printf 'sampled_cgroup_memory_peak_bytes\t%s\n' "$peak_cgroup_bytes"
    printf 'peak_live_run_bytes\t%s\n' "$peak_run_bytes"
    printf 'elapsed_wall\t%s\n' "${elapsed_seconds:-unknown}"
    printf 'filesystem_input_blocks\t%s\n' "${read_blocks:-unknown}"
    printf 'filesystem_output_blocks\t%s\n' "${write_blocks:-unknown}"
    printf 'filesystem_input_bytes\t%s\n' "$read_bytes"
    printf 'filesystem_output_bytes\t%s\n' "$write_bytes"
} > "$summary"

echo "benchmark summary: $summary"
exit "$exit_code"
