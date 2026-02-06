#include <session/render_session.h>

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

skwr::RenderOptions ParseArgs(int argc, char* argv[]) {
    skwr::RenderOptions options;

    // Parse Args
    bool help = (argc == 2 && strcmp(argv[1], "--help"));
    bool bad_args = (argc != 3 && argc != 1);
    if (bad_args || help) {
        print_usage(argv[0]);
        exit(1);  // maybe replace with try-catch
    }

    std::string outfile = "test_render.ppm";
    if (argc == 3 && strcmp(argv[1], "--name") == 0)  // make name argument
        outfile = argv[2];

    options.image_config.width = 800;
    options.image_config.height = 450;
    options.integrator_config.samples_per_pixel = 10;
    options.integrator_config.max_depth = 5;
    options.image_config.outfile = outfile;
    options.integrator_type = skwr::IntegratorType::PathTrace;
    return options;  // pass by copy back to main
}

int main(int argc, char* argv[]) {
    // Create configuration
    skwr::RenderOptions options = ParseArgs(argc, argv);

    // Start a rendering instance (Session)
    skwr::RenderSession session;

    session.LoadScene("temp");

    session.SetOptions(options);

    // Render
    session.Render();

    // Save
    session.Save();

    return 0;
}
