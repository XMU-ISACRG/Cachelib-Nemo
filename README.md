# CacheLib-Nemo

This repository includes source code of Nemo: A Low-Write-Amplification Cache for Tiny Objects on Log-Structured Flash Devices, and scripts for running experiments.Nemo is a caching architecture for tiny objects, as a C++ module built on Facebook’s CacheLib engine.

## What is CacheLib ?

[CacheLib](https://github.com/facebook/CacheLib) is a C++ library providing in-process high performance caching mechanism. CacheLib provides a thread safe API to build high throughput, low overhead caching services, with built-in ability to leverage DRAM and SSD caching transparently.

## Performance benchmarking

CacheLib provides a standalone executable CacheBench that can be used to evaluate the performance of heuristics and caching hardware platforms against production workloads. Additionally CacheBench enables stress testing implementation and design changes to CacheLib to catch correctness and performance issues.

See [CacheBench](https://cachelib.org/) for usage details and examples.

## Building and running

installation:
```sh
git clone https://github.com/ae-nemo/Nemo.git
```

build:
```sh
cd Nemo
./contrib/build.sh -d -j -v
```

run:
```sh
./test.sh
```
We recommend commenting out the compilation code for related dependencies in `build.sh` after the first compilation to avoid spending a long time linking the repository each time.The same ZNS SSD device must be specified in both the `test.sh` and the `znscache_test.json`.

## Output

After executing the script, you will observe three types of log files in `cachelib/log/`: `summary.log`, `run.log`, and `progress.log`. 

The name of `summary.log` is specified in `test.sh` and it reports the workload configuration along with per-minute summaries, including the total number of operations and the hit ratio. 

Both `run.log` and `progress.log` are generated automatically with a timestamped prefix. The `run.log` file outputs customized system logs in real time, while `progress.log` records system statistics at one-minute intervals, including write amplification, read latency, and hit ratio.

## FairyWREN fixed version

We place the fixed version of FairyWREN’s source code in the FairyWREN branch, while the original FairyWREN source code is available [here](https://github.com/saramcallister/CacheLib-fairywren).

## `test.sh` Usage

The `test.sh` script is located in the root directory of the repository. It is designed to automate the following tasks:

1. Build the project (if `./contrib/build.sh` exists).
2. Reset ZNS NVMe zones and set the I/O scheduler.
3. Run the `cachebench` binary with a specified JSON configuration file and log the output.

### Options

- `-d DEVICE`    Specify the NVMe device name (default: `nvme3n3`). Updates the `nvmCachePaths` field in the JSON configuration file.
- `-p PATH`      Specify the path to the `cachebench` binary (default: `opt/cachelib/bin/cachebench`).
- `-j JSON`      Specify the path to the JSON test configuration file (default: `cachelib/cachebench/test_configs/ssd_perf/znskvcache/znscache_test.json`).
- `-l LOG`       Specify the path to the output log file (default: `cachelib/log/nemo-ae.log`).
- `-t TRACE`     Specify the path to the trace file. Updates the `traceFileName` field in the JSON configuration file.
- `-h, --help`   Display the help message.

### Notes

- All options (`-d`, `-p`, `-j`, `-l`, `-t`) are required for the script to run correctly. If any option is missing, the script will display an error and exit.
- The script will automatically run `./contrib/build.sh -d -j -v` if the file exists in the repository root. If the file is missing, the script will skip the build step and display a warning.
- The script always resets NVMe zones (`sudo nvme zns reset-zone /dev/<DEVICE> -a`) and sets the I/O scheduler to the fixed value `mq-deadline`. These actions are not configurable via command-line options to ensure consistency in the testing process.
- The script uses `sudo` to execute commands requiring elevated privileges (e.g., NVMe reset and running `cachebench`). Ensure you have the necessary permissions or run the script in an interactive terminal.

### Examples

```sh
# Run with all required options (default values shown)
./test.sh -d nvme3n3 -p opt/cachelib/bin/cachebench -j cachelib/cachebench/test_configs/ssd_perf/znskvcache/znscache_test.json -l cachelib/log/nemo-ae.log -t /mnt/zns/trace/trace/twitter/lean_trace/merge.csv

# Specify custom device, JSON config, and trace file
./test.sh -d nvme2n1 -p ./opt/cachelib/bin/cachebench -j custom_config.json -l /tmp/custom.log -t /custom/trace.csv

# Example with a different log file and trace file
./test.sh -d nvme1n1 -p ./opt/cachelib/bin/cachebench -j cachelib/cachebench/test_configs/ssd_perf/znskvcache/znscache_test.json -l /var/logs/test.log -t /mnt/zns/trace/trace/twitter/lean_trace/another_trace.csv
```
If you want a dry-run mode (show parsed args without invoking build/reset/cachebench), let us know, and we can add a `--dry-run` flag.

## Configuration Notes

All configurations mentioned in the paper can be modified directly in the JSON files provided in the repository. For example, the `znscache_test.json` file contains parameters such as `navyZNSKVCacheBloomFilterFp`, `navyZNSKVCacheBucketSize`, and other cache settings. Adjust these fields to match the experimental setup described in the paper.