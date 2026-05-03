#include <session/render_session.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

void print_usage(const char* program_name) {
    std::cerr << "Usage:\n";
    std::cerr << "  " << program_name
              << " <scene.json> [num_threads] [--frame N | --frames A..B] [--statics-only]\n";
    std::cerr << "\n";
    std::cerr << "Arguments:\n";
    std::cerr << "  scene.json      Path to a JSON scene configuration file (required)\n";
    std::cerr << "  num_threads     Override thread count from scene file (optional)\n";
    std::cerr << "\n";
    std::cerr << "Options:\n";
    std::cerr << "  --frame N       Render only animated layers at frame index N\n";
    std::cerr << "  --frames A..B   Render only animated layers for inclusive frame range [A, B]\n";
    std::cerr << "  --statics-only  Render only non-animated layers (one output per layer)\n";
    std::cerr << "\n";
    std::cerr << "Help:\n";
    std::cerr << "  " << program_name << " --help\n";
}

static bool parse_frames_range(const std::string& spec, std::vector<int>& out_indices,
                               std::string& err) {
    const size_t dotdot = spec.find("..");
    if (dotdot == std::string::npos) {
        err = "expected A..B after --frames";
        return false;
    }
    try {
        int a = std::stoi(spec.substr(0, dotdot));
        int b = std::stoi(spec.substr(dotdot + 2));
        if (a > b) {
            err = "frame range A..B requires A <= B";
            return false;
        }
        for (int k = a; k <= b; ++k) {
            out_indices.push_back(k);
        }
    } catch (const std::exception&) {
        err = "invalid --frames range";
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Error: missing scene file argument\n\n";
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    const std::string scene_file = argv[1];
    int thread_override = 0;
    bool thread_set = false;

    skwr::RenderCliOptions cli{};
    bool have_frame_spec = false;

    for (int i = 2; i < argc; ++i) {
        const char* arg = argv[i];
        if (strcmp(arg, "--statics-only") == 0) {
            cli.statics_only = true;
            continue;
        }
        if (strcmp(arg, "--frame") == 0) {
            if (have_frame_spec) {
                std::cerr << "Error: use only one of --frame or --frames\n";
                return 1;
            }
            if (i + 1 >= argc) {
                std::cerr << "Error: --frame requires an integer argument\n";
                return 1;
            }
            have_frame_spec = true;
            cli.only_listed_frames = true;
            cli.frame_indices.push_back(std::atoi(argv[++i]));
            continue;
        }
        if (strcmp(arg, "--frames") == 0) {
            if (have_frame_spec) {
                std::cerr << "Error: use only one of --frame or --frames\n";
                return 1;
            }
            if (i + 1 >= argc) {
                std::cerr << "Error: --frames requires A..B\n";
                return 1;
            }
            have_frame_spec = true;
            cli.only_listed_frames = true;
            std::string err;
            if (!parse_frames_range(argv[++i], cli.frame_indices, err)) {
                std::cerr << "Error: " << err << "\n";
                return 1;
            }
            continue;
        }
        if (arg[0] == '-') {
            std::cerr << "Error: unknown option \"" << arg << "\"\n";
            print_usage(argv[0]);
            return 1;
        }

        char* end = nullptr;
        long v = std::strtol(arg, &end, 10);
        if (end != arg && *end == '\0' && v >= 0) {
            if (thread_set) {
                std::cerr << "Error: unexpected extra argument \"" << arg << "\"\n";
                return 1;
            }
            thread_override = static_cast<int>(v);
            thread_set = true;
            continue;
        }

        std::cerr << "Error: unexpected argument \"" << arg << "\"\n";
        print_usage(argv[0]);
        return 1;
    }

    if (cli.statics_only && cli.only_listed_frames) {
        std::cerr << "Error: --statics-only cannot be combined with --frame or --frames\n";
        return 1;
    }

    skwr::RenderSession session;

    try {
        session.RenderScene(scene_file, cli, thread_override);
    } catch (const std::exception& e) {
        std::cerr << "[Error] " << e.what() << "\n";
        return 1;
    }

    return 0;
}
