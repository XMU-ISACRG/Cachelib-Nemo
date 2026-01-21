<p align="center">
  <img width="500" height="140" alt="CacheLib" src="website/static/img/CacheLib-Logo-Large-transp.png">
</p>

# CacheLib

Pluggable caching engine to build and scale high performance cache services. See
[www.cachelib.org](https://cachelib.org) for documentation and more information.


## What is CacheLib ?

CacheLib is a C++ library providing in-process high performance caching
mechanism. CacheLib provides a thread safe API to build high throughput,
low overhead caching services, with built-in ability to leverage
DRAM and SSD caching transparently.


## Performance benchmarking

CacheLib provides a standalone executable `CacheBench` that can be used to
evaluate the performance of heuristics and caching hardware platforms against
production workloads. Additionally `CacheBench` enables stress testing
implementation and design changes to CacheLib to catch correctness and
performance issues.

See [CacheBench](https://cachelib.org/docs/Cache_Library_User_Guides/Cachebench_Overview) for usage details
and examples.

## Versioning
CacheLib has one single version number `facebook::cachelib::kCachelibVersion` that can be located at [CacheVersion.h](https://github.com/facebook/CacheLib/blob/main/cachelib/allocator/CacheVersion.h#L31). This version number must be incremented when incompatible changes are introduced. A change is incompatible if it could cause a complication failure due to removing public API or requires dropping the cache. Details about the compatibility information when the version number increases can be found in the [changelog](https://github.com/facebook/CacheLib/blob/main/CHANGELOG.md).


## Building and installation

CacheLib provides a build script which prepares and installs all
dependencies and prerequisites, then builds CacheLib.
The build script has been tested to work on CentOS 8,
Ubuntu 18.04, and Debian 10.

```sh
git clone https://github.com/facebook/CacheLib
cd CacheLib
./contrib/build.sh -d -j -v

# The resulting library and executables:
./opt/cachelib/bin/cachebench --help
```

Re-running `./contrib/build.sh` will update CacheLib and its dependencies
to their latest versions and rebuild them.

See [build](https://cachelib.org/docs/installation/installation) for more details about
the building and installation process.


## Contributing

We'd love to have your help in making CacheLib better. If you're interested,
please read our [guide to contributing](CONTRIBUTING.md)



## License

CacheLib is *apache* licensed, as found in the [LICENSE](LICENSE) file.



## Reporting and Fixing Security Issues

Please do not open GitHub issues or pull requests - this makes the problem
immediately visible to everyone, including malicious actors. Security issues in
CacheLib can be safely reported via Facebook's Whitehat Bug Bounty program:

https://www.facebook.com/whitehat

Facebook's security team will triage your report and determine whether or not is
it eligible for a bounty under our program.

## `test.sh` Usage

The `test.sh` script is located in the root directory of the repository. It is designed to automate the following tasks:

1. Build the project (if `./contrib/build.sh` exists).
2. Reset ZNS NVMe zones and set the I/O scheduler.
3. Run the `cachebench` binary with a specified JSON configuration file and log the output.

### Options

- `-d DEVICE`    Specify the NVMe device name (default: `nvme3n2`). Updates the `nvmCachePaths` field in the JSON configuration file.
- `-p PATH`      Specify the path to the `cachebench` binary (default: `opt/cachelib/bin/cachebench`).
- `-j JSON`      Specify the path to the JSON test configuration file (default: `cachelib/cachebench/test_configs/ssd_perf/kvcache_l2_fw/fw-tiny-text.json`).
- `-l LOG`       Specify the path to the output log file (default: `/home/nemo/Nemo/cachelib/log/fw_ae.log`).
- `-t TRACE`     Specify the path to the trace file (required). Updates the `traceFileName` field in the JSON configuration file. This parameter is mandatory for the script to run correctly. Ensure the trace file exists and is accessible.

### Updated Notes

- The `-t TRACE` option is now mandatory. The script will validate the existence of the specified trace file and update the `traceFileName` field in the JSON configuration file accordingly.
- The script will also update the `nvmCachePaths` field in the JSON configuration file with the specified NVMe device name (`-d DEVICE`).
- Ensure the JSON configuration file exists at the specified path (`-j JSON`). If the file is missing, the script will exit with an error.

### Examples

```sh
# Run with defaults (same behaviour as original simple invocation)
./test.sh -d nvme3n2 -p opt/cachelib/bin/cachebench -j cachelib/cachebench/test_configs/ssd_perf/kvcache_l2_fw/fw-tiny-text.json -l /home/nemo/Nemo/cachelib/log/fw_ae.log -t /path/to/trace.csv

# Specify custom device, config file, and trace file
./test.sh -d nvme2n1 -p ./opt/cachelib/bin/cachebench -j custom_config.json -l /tmp/custom.log -t /path/to/custom_trace.csv

# Example with a different log file and trace file
./test.sh -d nvme1n1 -p ./opt/cachelib/bin/cachebench -j cachelib/cachebench/test_configs/ssd_perf/kvcache_l2_fw/fw-tiny-text.json -l /var/logs/test.log -t /var/traces/test_trace.csv
```

## Configuration Notes

All configurations mentioned in the paper can be modified directly in the JSON files provided in the repository. For example, the `fw-tiny-text.json` file contains parameters such as `nvmCachePaths` and other cache settings. Adjust these fields to match the experimental setup described in the paper.

### Adjusting FwLog/FwSet Ratios

To adjust the ratio of FwLog to FwSet in the small object cache, you can modify the `navyKangarooLogSizePct` field in the JSON configuration file. For example:

- A value of `5` means that 5% of the flash is allocated to FwLog, and 95% of the flash is allocated to FwSet.

To adjust the over-provisioning (OP) ratio of FwSet, you can modify the `navyKangarooSetOverprovisioning` field in the JSON configuration file. For example:

- A value of `0.05` means that 5% of the flash in FwSet is used as over-provisioning space.
