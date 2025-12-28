# Nemo

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