#include <session/render_session.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "session/render_options.h"

/**
 * These are temporary parsing and help functions which
 * should be refactored when implementing a more robust
 * parser.
 */
static void PrintUsage(const char* program_name) {
    std::cerr << "Usage:\n";
    std::cerr << "  " << program_name << " [--obj model.obj] [--name outfile.ppm] [--threads N]\n";
    std::cerr << "Help:\n";
    std::cerr << "  " << program_name << " --help\n";
}

struct CLIArgs {
    std::string obj_file_;  // Optional OBJ to load as an object in the scene
    skwr::RenderOptions options_;
};

static auto ParseArgs(int argc, char* argv[]) -> CLIArgs {
    CLIArgs args;
    args.options.image_config.width = 800;
    args.options.image_config.height = 450;
    args.options.integrator_config.samples_per_pixel = 100;
    args.options.integrator_config.max_depth = 10;
    args.options.image_config.outfile = "test_render.ppm";
    args.options.integrator_type = skwr::IntegratorType::PathTrace;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            PrintUsage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            args.options.image_config.outfile = argv[++i];
        } else if (strcmp(argv[i], "--obj") == 0 && i + 1 < argc) {
            args.obj_file = argv[++i];
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            args.options.integrator_config.num_threads = std::atoi(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            PrintUsage(argv[0]);
            exit(1);
        }
    }

    return args;
}

auto main(int argc, char* argv[]) -> int {
    CLIArgs const args = ParseArgs(argc, argv);

    skwr::RenderSession session;

    session.LoadScene(args.obj_file);

    session.SetOptions(args.options);

    skwr::RenderSession::Render();

    skwr::RenderSession::Save();

    return 0;
}
