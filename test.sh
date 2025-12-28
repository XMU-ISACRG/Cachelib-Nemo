######################## file's home directory ########################
cachebenchDir="opt/cachelib/bin/cachebench"
znsJsonDir="cachelib/cachebench/test_configs/ssd_perf/znskvcache"
logDir="/home/nemo/Nemo/cachelib/log" 

######################## please configure your directory ########################
# 1.device
device="nvme3n3"

# 2. json file directory
znsJsonDir="$znsJsonDir/znscache_test.json"

# 3.log file directory
logDir="$logDir/nemo-ae.log"

######################## run commands ########################
# 1.setup
cd /home/nemo/Nemo
./contrib/build.sh -d -j -v
sudo nvme zns reset-zone /dev/$device -a
sleep 10
 echo mq-deadline | sudo tee /sys/block/$device/queue/scheduler

# 2.run
sudo $cachebenchDir --json_test_config $znsJsonDir > $logDir