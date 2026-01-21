#!/usr/bin/env bash
# test.sh - configurable runner for cachebench tests
set -euo pipefail

echo "$(pwd)"
echo "========================"

# Defaults (match original script)
cachebenchDir="opt/cachelib/bin/cachebench"
znsJsonDir="cachelib/cachebench/test_configs/ssd_perf/znskvcache/znscache_test.json"
logDir="cachelib/log/nemo-ae.log"
device="nvme3n3"
# Always use this scheduler and always perform build/reset steps (no CLI flags)
scheduler="mq-deadline"

usage() {
	cat <<EOF
Usage: $(basename "$0") [options]

Options:
	-d DEVICE           NVMe device name (default: $device)
	-p PATH             Path to cachebench binary (default: $cachebenchDir)
	-j JSON             Path to json test config (default: $znsJsonDir)
	-l LOG              Path to output log file (default: $logDir)
	-t TRACE            Path to trace file
	(build and reset are always performed if applicable; no flags)
	-h, --help          Show this help message

Example:
	$(basename "$0") -d nvme3n3 -p ./opt/cachelib/bin/cachebench -j cachelib/cachebench/test_configs/ssd_perf/znskvcache/znscache_test.json -l cachelib/log/nemo-ae.log
EOF
}

# Parse args
if [[ $# -eq 0 ]]; then
	# no args, use defaults
	:
else
	# Use a simple parser to allow long options
	while [[ $# -gt 0 ]]; do
		case "$1" in
			-d)
				device="$2"; shift 2;;
			-p)
				cachebenchDir="$2"; shift 2;;
			-j)
				znsJsonDir="$2"; shift 2;;
			-l)
				logDir="$2"; shift 2;;
			-t)
				traceFile="$2"; shift 2;;
			-h|--help)
				usage; exit 0;;
			*)
				echo "Unknown option: $1" >&2; usage; exit 2;;
		esac
	done
fi

echo "Using configuration:"
echo "  device:        $device"
echo "  cachebench:    $cachebenchDir"
echo "  json config:   $znsJsonDir"
echo "  log file:      $logDir"
echo "  scheduler:     $scheduler"

# Validate required options
if [[ -z "$device" ]]; then
  echo "Error: Device (-d) is required." >&2
  usage
  exit 1
fi

if [[ -z "$cachebenchDir" ]]; then
  echo "Error: Cachebench path (-p) is required." >&2
  usage
  exit 1
fi

if [[ -z "$znsJsonDir" ]]; then
  echo "Error: JSON configuration file (-j) is required." >&2
  usage
  exit 1
fi

if [[ -z "$logDir" ]]; then
  echo "Error: Log file path (-l) is required." >&2
  usage
  exit 1
fi

if [[ -z "${traceFile:-}" ]]; then
  echo "Error: Trace file (-t) is required." >&2
  usage
  exit 1
fi

# Note: The device specified with the -d option will also be used to update the `nvmCachePaths` field in the JSON configuration file.

# Basic validations
if [[ -x ./contrib/build.sh || -f ./contrib/build.sh ]]; then
	echo "Found ./contrib/build.sh; will run build before test."
else
	echo "Warning: ./contrib/build.sh not found in repo root. Skipping build." >&2
fi

if [[ -z "$device" ]]; then
	echo "Device name is empty" >&2; exit 3
fi

# 1. setup (build)
if [[ -x ./contrib/build.sh || -f ./contrib/build.sh ]]; then
	echo "Running build: ./contrib/build.sh -d -j -v"
	./contrib/build.sh -d -j -v
fi

echo "Resetting NVMe zones on /dev/$device"
sudo nvme zns reset-zone /dev/$device -a
sleep 10
echo "$scheduler" | sudo tee /sys/block/$device/queue/scheduler

# Update the JSON configuration file with the specified device and trace file
if [[ -f "$znsJsonDir" ]]; then
	echo "Updating nvmCachePaths in $znsJsonDir with device: $device"
	sed -i.bak "s|\"nvmCachePaths\": \[\".*\"\]|\"nvmCachePaths\": [\"/dev/$device\"]|" "$znsJsonDir"
	if [[ -n "${traceFile:-}" ]]; then
		echo "Updating traceFileName in $znsJsonDir with trace file: $traceFile"
		sed -i.bak "s|\"traceFileName\": \".*\"|\"traceFileName\": \"$traceFile\"|" "$znsJsonDir"
	fi
else
	echo "Error: JSON configuration file not found at $znsJsonDir" >&2
	exit 5
fi

# 2. run cachebench
if [[ ! -x "$cachebenchDir" && ! -f "$cachebenchDir" ]]; then
	echo "Error: cachebench binary not found at $cachebenchDir" >&2
	echo "Either build the project or pass -p /path/to/cachebench" >&2
	exit 4
fi

echo "Running cachebench --json_test_config $znsJsonDir > $logDir"
sudo "$cachebenchDir" --json_test_config "$znsJsonDir" > "$logDir"

echo "Done. Log written to $logDir"