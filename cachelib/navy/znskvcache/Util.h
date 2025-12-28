#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <chrono>
#include <ctime>
#include <sstream>

#include <folly/logging/Init.h>

#include <folly/logging/LogConfigParser.h>
#include <folly/logging/LoggerDB.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GTest.h>
#include <folly/test/TestUtils.h>
#include <folly/logging/LogHandlerFactory.h>
#include <folly/logging/LogWriter.h>
#include <folly/logging/LoggerDB.h>
#include <folly/logging/StandardLogHandler.h>
#include <folly/logging/StandardLogHandlerFactory.h>
#include <folly/logging/FileHandlerFactory.h>

namespace facebook {
namespace cachelib {
namespace navy {

std::vector<std::string> readLinesFromFile(const std::string& filename) {
    std::vector<std::string> lines;    
    std::ifstream file(filename);     
    if (!file.is_open()) {            
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return lines;
    }

    std::string line;
    while (std::getline(file, line)) { 
        lines.push_back(line);         
    }

    file.close();                      
    return lines;
}

std::string getTimestampedLogFileName(int totalIterations) {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    
    std::tm now_tm = *std::localtime(&now_c);
    char buffer[20]; 
    std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &now_tm); 
    
    return "/home/ttt/Code/CacheLib-fairywren/cachelib/traces/log/" + std::string(buffer) + "_iter" + std::to_string(totalIterations) + ".log";
}


using folly::initLogging;
using folly::LoggerDB;
using folly::parseLogConfig;

// output XLOG info to local file
void XLOGToLocalFile(std::string logFileName) {
    
    auto config = parseLogConfig(
        ".:=INFO:y;y=file:path=" + logFileName);
    
    folly::LoggerDB::get().registerHandlerFactory(std::make_unique<folly::FileHandlerFactory>());
    // folly::LoggerDB::get().registerHandlerFactory(
    // std::make_unique<folly::TestLogHandlerFactory>("file"));

    folly::LoggerDB::get().updateConfig(config);
}

} // namespace navy
} // namespace cachelib
} // namespace facebook
