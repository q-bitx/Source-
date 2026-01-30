// src/util/logging.cpp

#include <string>
#include <string_view>
#include <iostream>
#include <logging.h>

BCLog::Logger& LogInstance()
{
    return BCLog::g_logger;
namespace BCLog {
    enum class Level {
       Debug,
       Info,
       Warning,
        Error
    };

    enum LogFlags {
        NONE = 0
    };

    class Logger {
    public:
    void LogPrintStr(std::string_view, std::string_view, std::string_view,
                         int, LogFlags, Level) {
        }
    };

    Logger& LogInstance() {
        static Logger dummyLogger;
        return dummyLogger;
    }
}

namespace util {
    template<typename... Args>
    void LogPrintFormatInternal(std::string_view, std::string_view,
                                 int, BCLog::LogFlags, BCLog::Level,
                                 std::string_view, Args&&...) {
    }
}
