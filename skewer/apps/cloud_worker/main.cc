/// Cloud Run / one-shot worker: reads task from env, renders, reports via gRPC, exits.

#include <coordinator_worker/worker_loop.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "session/render_session.h"

namespace {

std::optional<int> EnvInt(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) {
        return std::nullopt;
    }
    try {
        return std::stoi(v);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<float> EnvFloat(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) {
        return std::nullopt;
    }
    try {
        return std::stof(v);
    } catch (...) {
        return std::nullopt;
    }
}

bool EnvBool(const char* name, bool default_val) {
    const char* v = std::getenv(name);
    if (!v) {
        return default_val;
    }
    std::string s(v);
    if (s == "1" || s == "true" || s == "TRUE") {
        return true;
    }
    if (s == "0" || s == "false" || s == "FALSE") {
        return false;
    }
    return default_val;
}

std::string RequiredEnv(const char* name) {
    const char* v = std::getenv(name);
    if (!v) {
        return {};
    }
    return v;
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    const std::string task_id = RequiredEnv("TASK_ID");
    const std::string job_id = RequiredEnv("JOB_ID");
    const std::string scene_uri = RequiredEnv("SCENE_URI");
    const std::string output_uri = RequiredEnv("OUTPUT_URI");
    if (task_id.empty() || job_id.empty() || scene_uri.empty() || output_uri.empty()) {
        std::cerr << "[SKEWER] Missing required env: TASK_ID, JOB_ID, SCENE_URI, OUTPUT_URI\n";
        return 2;
    }

    std::string coord = coordinator_worker::DefaultCoordinatorAddr();
    if (const char* c = std::getenv("COORDINATOR_ADDR"); c && *c) {
        coord = c;
    }

    std::string worker_id = RequiredEnv("WORKER_ID");
    if (worker_id.empty()) {
        worker_id = coordinator_worker::MakeWorkerId("skewer-cloud-worker");
    }

    const auto t0 = std::chrono::steady_clock::now();

    coordinator_worker::TaskOutcome outcome;
    outcome.output_uri = output_uri;

    try {
        skwr::RenderSession session;

        int task_threads = 2;
        if (auto t = EnvInt("RENDER_THREADS")) {
            task_threads = *t;
        }
        if (auto t = EnvInt("THREADS"); t && *t > 0) {
            task_threads = *t;
        }

        session.LoadSceneFromFile(scene_uri, 0);

        if (auto w = EnvInt("WIDTH")) {
            if (*w > 0) {
                session.Options().image_config.width = *w;
            }
        }
        if (auto h = EnvInt("HEIGHT")) {
            if (*h > 0) {
                session.Options().image_config.height = *h;
            }
        }
        if (auto ms = EnvInt("MAX_SAMPLES"); ms && *ms > 0) {
            session.Options().integrator_config.max_samples = *ms;
        }

        session.Options().integrator_config.num_threads = task_threads;
        session.Options().image_config.outfile = output_uri;
        session.Options().image_config.exrfile = output_uri;
        session.Options().integrator_config.enable_deep = EnvBool("ENABLE_DEEP", false);

        if (auto nt = EnvFloat("NOISE_THRESHOLD"); nt && *nt > 0.0f) {
            session.Options().integrator_config.noise_threshold = *nt;
        }
        if (auto mn = EnvInt("MIN_SAMPLES"); mn && *mn > 0) {
            session.Options().integrator_config.min_samples = *mn;
        }
        if (auto step = EnvInt("ADAPTIVE_STEP"); step && *step > 0) {
            session.Options().integrator_config.adaptive_step = *step;
        }

        session.Render();
        session.Save();

        outcome.success = true;
    } catch (const std::exception& e) {
        outcome.success = false;
        outcome.error_message = e.what();
        std::cerr << "[SKEWER] Task failed: " << outcome.error_message << "\n";
    }

    const auto t1 = std::chrono::steady_clock::now();
    const int64_t elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    if (!coordinator_worker::ReportTaskResult(coord, worker_id, task_id, job_id, outcome,
                                              elapsed_ms)) {
        return outcome.success ? 3 : 4;
    }

    return outcome.success ? 0 : 1;
}
