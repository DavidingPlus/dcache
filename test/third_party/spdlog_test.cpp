#include <gtest/gtest.h>

#include <spdlog/details/os.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <sstream>
#include <string>


namespace
{

    // spdlog 默认使用平台对应的换行符：Windows 为 "\r\n"，Linux 为 "\n"。测试只关心日志内容，因此统一通过 spdlog 的默认换行符构造期望值。
    std::string expectedLogLine(const char *content)
    {
        return std::string(content) + spdlog::details::os::default_eol;
    }

    struct TestLogger
    {
        std::ostringstream output;
        std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink;
        std::shared_ptr<spdlog::logger> logger;

        TestLogger()
            : sink(std::make_shared<spdlog::sinks::ostream_sink_mt>(output)),
              logger(std::make_shared<spdlog::logger>("unit-test", sink))
        {
            logger->set_pattern("[%n] [%l] %v");
        }
    };

} // namespace


TEST(SpdlogTests, UsesLoggerNamePatternAndArguments)
{
    TestLogger testLogger;

    testLogger.logger->info("cache hit: key={}, value={}", "user:1", 42);

    EXPECT_EQ(expectedLogLine("[unit-test] [info] cache hit: key=user:1, value=42"), testLogger.output.str());
}

TEST(SpdlogTests, LogsEachSeverityWithItsLevelName)
{
    TestLogger testLogger;
    testLogger.logger->set_level(spdlog::level::trace);

    testLogger.logger->trace("trace message");
    testLogger.logger->debug("debug message");
    testLogger.logger->info("info message");
    testLogger.logger->warn("warn message");
    testLogger.logger->error("error message");
    testLogger.logger->critical("critical message");

    EXPECT_EQ(
        expectedLogLine("[unit-test] [trace] trace message") + expectedLogLine("[unit-test] [debug] debug message") + expectedLogLine("[unit-test] [info] info message") + expectedLogLine("[unit-test] [warning] warn message") + expectedLogLine("[unit-test] [error] error message") + expectedLogLine("[unit-test] [critical] critical message"),
        testLogger.output.str());
}

TEST(SpdlogTests, FiltersMessagesBelowConfiguredLoggerLevel)
{
    TestLogger testLogger;
    testLogger.logger->set_level(spdlog::level::warn);

    EXPECT_FALSE(testLogger.logger->should_log(spdlog::level::info));
    EXPECT_TRUE(testLogger.logger->should_log(spdlog::level::warn));

    testLogger.logger->info("this message is filtered");
    testLogger.logger->warn("remaining items: {}", 2);

    EXPECT_EQ(expectedLogLine("[unit-test] [warning] remaining items: 2"), testLogger.output.str());
}

TEST(SpdlogTests, FiltersMessagesAtTheSinkLevel)
{
    TestLogger testLogger;
    testLogger.logger->set_level(spdlog::level::trace);
    testLogger.sink->set_level(spdlog::level::err);

    testLogger.logger->warn("warning is filtered by the sink");
    testLogger.logger->error("error is written by the sink");

    EXPECT_EQ(expectedLogLine("[unit-test] [error] error is written by the sink"), testLogger.output.str());
}

TEST(SpdlogTests, SupportsTheDefaultLoggerApi)
{
    TestLogger testLogger;
    const auto previousLogger = spdlog::default_logger();
    spdlog::set_default_logger(testLogger.logger);

    spdlog::info("message through the default logger");
    spdlog::default_logger()->flush();

    EXPECT_EQ(expectedLogLine("[unit-test] [info] message through the default logger"), testLogger.output.str());

    spdlog::set_default_logger(previousLogger);
}

TEST(SpdlogTests, PrintsExampleLogsToTerminal)
{
    const std::string loggerName = "spdlog-terminal-demo";
    spdlog::drop(loggerName);

    const auto logger = spdlog::stdout_color_mt(loggerName);
    logger->set_level(spdlog::level::trace);
    logger->set_pattern("[%n] [%^%l%$] %v");

    logger->info("info: cache connected, key={}", "user:1");
    logger->warn("warning: cache usage is {}%", 80);
    logger->error("error: key={} was not found", "missing");
    logger->flush();

    EXPECT_EQ(spdlog::level::trace, logger->level());
    spdlog::drop(loggerName);
}
