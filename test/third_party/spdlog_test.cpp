#include <gtest/gtest.h>

#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <sstream>
#include <string>


namespace
{

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

    EXPECT_EQ("[unit-test] [info] cache hit: key=user:1, value=42\n", testLogger.output.str());
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
        "[unit-test] [trace] trace message\n"
        "[unit-test] [debug] debug message\n"
        "[unit-test] [info] info message\n"
        "[unit-test] [warning] warn message\n"
        "[unit-test] [error] error message\n"
        "[unit-test] [critical] critical message\n",
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

    EXPECT_EQ("[unit-test] [warning] remaining items: 2\n", testLogger.output.str());
}

TEST(SpdlogTests, FiltersMessagesAtTheSinkLevel)
{
    TestLogger testLogger;
    testLogger.logger->set_level(spdlog::level::trace);
    testLogger.sink->set_level(spdlog::level::err);

    testLogger.logger->warn("warning is filtered by the sink");
    testLogger.logger->error("error is written by the sink");

    EXPECT_EQ("[unit-test] [error] error is written by the sink\n", testLogger.output.str());
}

TEST(SpdlogTests, SupportsTheDefaultLoggerApi)
{
    TestLogger testLogger;
    const auto previousLogger = spdlog::default_logger();
    spdlog::set_default_logger(testLogger.logger);

    spdlog::info("message through the default logger");
    spdlog::default_logger()->flush();

    EXPECT_EQ("[unit-test] [info] message through the default logger\n", testLogger.output.str());

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
