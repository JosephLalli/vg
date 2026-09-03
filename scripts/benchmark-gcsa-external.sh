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
process_workers="1"
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
    echo "  --process-workers N    independent join partition workers [1]" >&2
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
        --process-workers) process_workers="$2"; shift 2 ;;
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

# Bind executable provenance before launching. The path may be atomically
# replaced by an independent build while this long-running process continues
# to execute the old inode; recomputing the hash at shutdown would then record
# a binary that never ran.
vg_sha256="$(sha256sum "$vg_binary" | awk '{print $1}')"

mkdir -p "$run_directory"
run_directory="$(cd "$run_directory" && pwd)"
work_directory="$run_directory/index.gcsa-work"
temp_directory="$run_directory/vg-tmp"
output_name="$run_directory/index.gcsa"

# A resumed benchmark is a new measured attempt over the same immutable
# workspace frontier. Never truncate the evidence from the attempt that was
# interrupted: select the first unused deterministic resume suffix instead.
attempt_suffix=""
if [[ $resume -eq 1 && -e "$run_directory/command.txt" ]]; then
    attempt_number=1
    while :; do
        printf -v attempt_suffix '.resume-%02d' "$attempt_number"
        [[ ! -e "$run_directory/command${attempt_suffix}.txt" ]] && break
        ((attempt_number += 1))
    done
fi

time_log="$run_directory/time${attempt_suffix}.txt"
stderr_log="$run_directory/stderr${attempt_suffix}.log"
stdout_log="$run_directory/stdout${attempt_suffix}.log"
samples="$run_directory/resource_samples${attempt_suffix}.tsv"
summary="$run_directory/summary${attempt_suffix}.tsv"
phase_summary="$run_directory/phase_summary${attempt_suffix}.tsv"
command_file="$run_directory/command${attempt_suffix}.txt"
inputs_file="$run_directory/inputs${attempt_suffix}.tsv"
unit="vg-gcsa-$USER-$(date +%Y%m%dT%H%M%S)-$$"

if [[ $resume -eq 0 && ( -e "$work_directory/build.json" || -s "$summary" ) ]]; then
    echo "run directory already contains committed work; use --resume" >&2
    exit 2
fi

# vg first enumerates de Bruijn records before GCSA2 can commit them into the
# durable workspace. Keep that spool on the benchmark filesystem so it is
# included in peak-live-disk accounting and cannot silently fill /tmp.
mkdir -p "$temp_directory"
command=("$vg_binary" index -b "$temp_directory" -p -V -g "$output_name" -k "$kmer_length"
    -X "$doubling_steps" -t "$threads"
    --gcsa-work-dir "$work_directory"
    --gcsa-memory-limit "$memory_limit"
    --gcsa-disk-limit "$disk_limit"
    --gcsa-sort-run-size "$sort_run_size"
    --gcsa-join-partition-size "$join_partition_size"
    --gcsa-process-workers "$process_workers")
if [[ -n "$mapping" ]]; then
    command+=(-f "$mapping")
fi
if [[ $resume -eq 1 ]]; then
    command+=(--gcsa-resume)
fi
command+=("$graph")

{
    printf 'unit=%q\n' "$unit.scope"
    printf 'vg_sha256=%s\n' "$vg_sha256"
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
} > "$inputs_file"

printf 'unix_time\tphase\trun_bytes\tmemory_current\tmemory_peak\tmemory_anon\tmemory_file\tmemory_file_dirty\tcpu_usage_usec\tio_read_bytes\tio_write_bytes\ttasks_current\tmem_available_kib\n' > "$samples"

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
    tasks_current="$(systemctl --user show "$unit.scope" -p TasksCurrent --value 2>/dev/null || true)"
    control_group="$(systemctl --user show "$unit.scope" -p ControlGroup --value 2>/dev/null || true)"
    cgroup_directory="/sys/fs/cgroup${control_group:-/__not_running__}"
    memory_anon=0
    memory_file=0
    memory_file_dirty=0
    cpu_usage_usec=0
    io_read_bytes=0
    io_write_bytes=0
    if [[ -r "$cgroup_directory/memory.stat" ]]; then
        read -r memory_anon memory_file memory_file_dirty < <(
            awk '
                $1 == "anon" { anon = $2 }
                $1 == "file" { file = $2 }
                $1 == "file_dirty" { dirty = $2 }
                END { print anon + 0, file + 0, dirty + 0 }
            ' "$cgroup_directory/memory.stat"
        )
    fi
    if [[ -r "$cgroup_directory/cpu.stat" ]]; then
        cpu_usage_usec="$(awk '$1 == "usage_usec" { print $2 + 0 }' "$cgroup_directory/cpu.stat")"
    fi
    if [[ -r "$cgroup_directory/io.stat" ]]; then
        read -r io_read_bytes io_write_bytes < <(
            awk '
                {
                    for(i = 2; i <= NF; i++) {
                        split($i, value, "=")
                        if(value[1] == "rbytes") { reads += value[2] }
                        if(value[1] == "wbytes") { writes += value[2] }
                    }
                }
                END { print reads + 0, writes + 0 }
            ' "$cgroup_directory/io.stat"
        )
    fi
    # Phase names come from the existing stable progress messages. Sampling
    # them here keeps instrumentation observational and lets one benchmark
    # distinguish CPU-bound scans from I/O-bound merges without changing the
    # index format or construction schedule.
    phase="$(awk '
        /Validating and restoring durable kmer files/ { phase = "restore" }
        /Building the GCSA2 index/ { phase = "preprocess" }
        /GCSA::GCSA\(\): Restoring checkpoint/ { phase = "restore" }
        /Prefix-doubling from path length/ { phase = "prefix" }
        /GCSA::GCSA\(\): Step [0-9]+/ {
            for(i = 1; i <= NF; i++) {
                if($i == "Step") {
                    step = $(i + 1); gsub(/[^0-9]/, "", step)
                    phase = "step-" step
                }
            }
        }
        /GCSA::GCSA\(\): Merging the paths/ { phase = "merge" }
        /GCSA::GCSA\(\): Building the index/ { phase = "index" }
        /Saving GCSA to/ { phase = "serialize" }
        /Verifying the index/ { phase = "verify" }
        END { print (phase == "" ? "startup" : phase) }
    ' "$stderr_log" 2>/dev/null || printf 'startup')"
    mem_available="$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$(date +%s)" "${phase:-startup}" "${run_bytes:-0}" \
        "${memory_current:-0}" "${memory_peak:-0}" "${memory_anon:-0}" \
        "${memory_file:-0}" "${memory_file_dirty:-0}" "${cpu_usage_usec:-0}" \
        "${io_read_bytes:-0}" "${io_write_bytes:-0}" "${tasks_current:-0}" \
        "${mem_available:-0}" >> "$samples"
    sleep "$sample_seconds"
done

set +e
wait "$runner_pid"
exit_code=$?
set -e
runner_pid=""

peak_run_bytes="$(awk 'NR > 1 && $3 > max {max=$3} END {print max+0}' "$samples")"
peak_cgroup_bytes="$(awk 'NR > 1 && $5 ~ /^[0-9]+$/ && $5 > max {max=$5} END {print max+0}' "$samples")"
peak_anon_bytes="$(awk 'NR > 1 && $6 > max {max=$6} END {print max+0}' "$samples")"
peak_file_bytes="$(awk 'NR > 1 && $7 > max {max=$7} END {print max+0}' "$samples")"
peak_file_dirty_bytes="$(awk 'NR > 1 && $8 > max {max=$8} END {print max+0}' "$samples")"
cgroup_cpu_usage_usec="$(awk 'NR > 1 && $9 ~ /^[0-9]+$/ {last=$9} END {print last+0}' "$samples")"
cgroup_io_read_bytes="$(awk 'NR > 1 && $10 ~ /^[0-9]+$/ {last=$10} END {print last+0}' "$samples")"
cgroup_io_write_bytes="$(awk 'NR > 1 && $11 ~ /^[0-9]+$/ {last=$11} END {print last+0}' "$samples")"

# Summarize contiguous construction phases from the cgroup counters. The first
# and last sample in a phase bracket the measured interval, so short phases may
# have a zero sampled span; whole-run /usr/bin/time remains the exact authority.
awk -F '\t' '
    BEGIN { OFS = "\t" }
    NR == 1 { next }
    {
        phase = $2
        if(!(phase in seen)) {
            seen[phase] = 1
            order[++phases] = phase
            first_time[phase] = $1
            first_cpu[phase] = $9
            first_read[phase] = $10
            first_write[phase] = $11
        }
        last_time[phase] = $1
        last_cpu[phase] = $9
        last_read[phase] = $10
        last_write[phase] = $11
        samples[phase]++
        if($3 > peak_run[phase]) { peak_run[phase] = $3 }
        if($4 ~ /^[0-9]+$/ && $4 > peak_memory[phase]) { peak_memory[phase] = $4 }
        if($6 > peak_anon[phase]) { peak_anon[phase] = $6 }
        if($7 > peak_file[phase]) { peak_file[phase] = $7 }
        if($8 > peak_dirty[phase]) { peak_dirty[phase] = $8 }
    }
    END {
        print "phase", "samples", "sampled_span_seconds", "peak_run_bytes", \
            "peak_memory_current", "peak_memory_anon", "peak_memory_file", \
            "peak_memory_file_dirty", "cpu_usage_delta_usec", \
            "io_read_delta_bytes", "io_write_delta_bytes"
        for(i = 1; i <= phases; i++) {
            phase = order[i]
            printf "%s\t%d\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\n", \
                phase, samples[phase], last_time[phase] - first_time[phase], \
                peak_run[phase], peak_memory[phase], peak_anon[phase], \
                peak_file[phase], peak_dirty[phase], \
                last_cpu[phase] - first_cpu[phase], \
                last_read[phase] - first_read[phase], \
                last_write[phase] - first_write[phase]
        }
    }
' "$samples" > "$phase_summary"

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
    printf 'process_workers\t%s\n' "$process_workers"
    printf 'vg_sha256\t%s\n' "$vg_sha256"
    printf 'max_rss_kib\t%s\n' "${max_rss_kib:-unknown}"
    printf 'sampled_cgroup_memory_peak_bytes\t%s\n' "$peak_cgroup_bytes"
    printf 'sampled_cgroup_anon_peak_bytes\t%s\n' "$peak_anon_bytes"
    printf 'sampled_cgroup_file_peak_bytes\t%s\n' "$peak_file_bytes"
    printf 'sampled_cgroup_file_dirty_peak_bytes\t%s\n' "$peak_file_dirty_bytes"
    printf 'sampled_cgroup_cpu_usage_usec\t%s\n' "$cgroup_cpu_usage_usec"
    printf 'sampled_cgroup_io_read_bytes\t%s\n' "$cgroup_io_read_bytes"
    printf 'sampled_cgroup_io_write_bytes\t%s\n' "$cgroup_io_write_bytes"
    printf 'peak_live_run_bytes\t%s\n' "$peak_run_bytes"
    printf 'elapsed_wall\t%s\n' "${elapsed_seconds:-unknown}"
    printf 'filesystem_input_blocks\t%s\n' "${read_blocks:-unknown}"
    printf 'filesystem_output_blocks\t%s\n' "${write_blocks:-unknown}"
    printf 'filesystem_input_bytes\t%s\n' "$read_bytes"
    printf 'filesystem_output_bytes\t%s\n' "$write_bytes"
} > "$summary"

echo "benchmark summary: $summary"
exit "$exit_code"
