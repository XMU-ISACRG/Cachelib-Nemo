######################## file's home directory ########################
cachebenchDir="opt/cachelib/bin/cachebench"
fwJsonDir="cachelib/cachebench/test_configs/ssd_perf/kvcache_l2_fw"
logDir="/home/nemo/Nemo/cachelib/log"

######################## please configure your directory ########################
# 1.device
device="nvme3n2"

# 2. json file directory
fwJsonDir="$fwJsonDir/fw-tiny-text.json"

# 3.log file directory
logDir="$logDir/fw_ae.log"



######################## run commands ########################
# 1.setup
cd /home/nemo/Nemo
./contrib/build.sh -d -j -v
sudo nvme zns reset-zone /dev/$device -a
sleep 10
echo mq-deadline | sudo tee /sys/block/$device/queue/scheduler


# 2.run
sudo $cachebenchDir --json_test_config $fwJsonDir > $logDir