/// \file test_worker_utils.cc
/// \brief Unit tests for coordinator_worker utility functions.

#include <gtest/gtest.h>

#include <cstdlib>
#include <regex>
#include <set>
#include <string>

#include "coordinator_worker/worker_loop.h"

// ---------------------------------------------------------------------------
// DefaultCoordinatorAddr
// ---------------------------------------------------------------------------

class DefaultCoordinatorAddrTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Save original value so we can restore it after each test.
        const char* orig = std::getenv("COORDINATOR_ADDR");
        had_env_ = (orig != nullptr);
        if (had_env_) {
            saved_env_ = orig;
        }
    }

    void TearDown() override {
        if (had_env_) {
            setenv("COORDINATOR_ADDR", saved_env_.c_str(), /*overwrite=*/1);
        } else {
            unsetenv("COORDINATOR_ADDR");
        }
    }

  private:
    bool had_env_ = false;
    std::string saved_env_;
};

TEST_F(DefaultCoordinatorAddrTest, ReturnsLocalhostDefault) {
    unsetenv("COORDINATOR_ADDR");
    EXPECT_EQ(coordinator_worker::DefaultCoordinatorAddr(), "localhost:50051");
}

TEST_F(DefaultCoordinatorAddrTest, ReturnsEnvVarWhenSet) {
    setenv("COORDINATOR_ADDR", "coordinator.prod:9090", /*overwrite=*/1);
    EXPECT_EQ(coordinator_worker::DefaultCoordinatorAddr(), "coordinator.prod:9090");
}

TEST_F(DefaultCoordinatorAddrTest, ReturnsEmptyStringEnvVar) {
    // An explicitly empty env var should be returned as-is (not fall back).
    setenv("COORDINATOR_ADDR", "", /*overwrite=*/1);
    EXPECT_EQ(coordinator_worker::DefaultCoordinatorAddr(), "");
}

// ---------------------------------------------------------------------------
// MakeWorkerId
// ---------------------------------------------------------------------------

TEST(MakeWorkerIdTest, ContainsNameTag) {
    const std::string id = coordinator_worker::MakeWorkerId("skewer-worker");
    EXPECT_TRUE(id.starts_with("skewer-worker-"))
        << "ID should start with the name tag followed by a dash, got: " << id;
}

TEST(MakeWorkerIdTest, ContainsNameTagLoom) {
    const std::string id = coordinator_worker::MakeWorkerId("loom-worker");
    EXPECT_TRUE(id.starts_with("loom-worker-"))
        << "ID should start with the name tag followed by a dash, got: " << id;
}

TEST(MakeWorkerIdTest, MatchesExpectedFormat) {
    // Expected format: {tag}-{epoch}-{4-digit-random}
    const std::string id = coordinator_worker::MakeWorkerId("test-worker");
    // tag-<digits>-<4 digits>
    const std::regex pattern(R"(test-worker-\d+-\d{4})");
    EXPECT_TRUE(std::regex_match(id, pattern))
        << "ID should match '{tag}-{epoch}-{4digit}' format, got: " << id;
}

TEST(MakeWorkerIdTest, RandomSuffixInRange) {
    // The random suffix should be a 4-digit number in [1000, 9999].
    const std::string id = coordinator_worker::MakeWorkerId("w");
    // Extract the last component after the final dash.
    const auto last_dash = id.rfind('-');
    ASSERT_NE(last_dash, std::string::npos);
    const std::string suffix_str = id.substr(last_dash + 1);
    const int suffix = std::stoi(suffix_str);
    EXPECT_GE(suffix, 1000);
    EXPECT_LE(suffix, 9999);
}

TEST(MakeWorkerIdTest, SuccessiveCallsProduceDifferentIds) {
    // While not *guaranteed* to differ, a collision on two back-to-back calls is astronomically
    // unlikely.
    std::set<std::string> ids;
    constexpr int kIterations = 20;
    for (int i = 0; i < kIterations; ++i) {
        ids.insert(coordinator_worker::MakeWorkerId("uniq"));
    }

    EXPECT_GE(static_cast<int>(ids.size()), kIterations) << "Expected all IDs to be unique";
}

TEST(MakeWorkerIdTest, EmptyTagProducesValidId) {
    const std::string id = coordinator_worker::MakeWorkerId("");
    // Should start with a dash (empty tag, then dash, then epoch).
    EXPECT_FALSE(id.empty());
    EXPECT_EQ(id[0], '-') << "Empty tag should produce ID starting with '-', got: " << id;
}

// ---------------------------------------------------------------------------
// TaskOutcome defaults
// ---------------------------------------------------------------------------

TEST(TaskOutcomeTest, DefaultsToSuccess) {
    coordinator_worker::TaskOutcome outcome;
    EXPECT_TRUE(outcome.success);
    EXPECT_TRUE(outcome.error_message.empty());
    EXPECT_TRUE(outcome.output_uri.empty());
}
