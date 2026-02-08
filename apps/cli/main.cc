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
void print_usage(const char* program_name) {
    std::cerr << "Usage: " << "\n";
    std::cerr << "       " << program_name << "\n";
    std::cerr << "       " << program_name << " --name outfile.ppm\n";
    std::cerr << "Help:  " << "\n";
    std::cerr << "       " << program_name << " --help\n";
}

struct CLIArgs {
    std::string obj_file;  // Optional OBJ to load as an object in the scene
    skwr::RenderOptions options;
};

CLIArgs ParseArgs(int argc, char* argv[]) {
    CLIArgs args;
    args.options.image_config.width = 800;
    args.options.image_config.height = 450;
    args.options.integrator_config.samples_per_pixel = 10;
    args.options.integrator_config.max_depth = 5;
    args.options.image_config.outfile = "test_render.ppm";
    args.options.integrator_type = skwr::IntegratorType::PathTrace;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            args.options.image_config.outfile = argv[++i];
        } else if (strcmp(argv[i], "--obj") == 0 && i + 1 < argc) {
            args.obj_file = argv[++i];
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            exit(1);
        }
    }

    return args;
}

int main(int argc, char* argv[]) {
    CLIArgs args = ParseArgs(argc, argv);

    skwr::RenderSession session;

    session.LoadScene(args.obj_file);

    session.SetOptions(args.options);

    session.Render();

    session.Save();

    return 0;
}
