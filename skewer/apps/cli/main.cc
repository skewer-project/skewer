#include <session/render_session.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

void print_usage(const char* program_name) {
    std::cerr << "Usage:\n";
    std::cerr << "  " << program_name << " <scene.json> [num_threads]\n";
    std::cerr << "\n";
    std::cerr << "Arguments:\n";
    std::cerr << "  scene.json    Path to a JSON scene configuration file (required)\n";
    std::cerr << "  num_threads   Override thread count from scene file (optional)\n";
    std::cerr << "\n";
    std::cerr << "Help:\n";
    std::cerr << "  " << program_name << " --help\n";
}

int main(int argc, char* argv[]) {
    // Parse positional args: <scene.json> [num_threads]
    if (argc < 2) {
        std::cerr << "Error: missing scene file argument\n\n";
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    std::string scene_file = argv[1];
    int thread_override = 0;

    if (argc >= 3) {
        thread_override = std::atoi(argv[2]);
        if (thread_override < 0) {
            std::cerr << "Error: thread count must be non-negative\n";
            return 1;
        }
    }

    skwr::RenderSession session;

    try {
        session.LoadSceneFromFile(scene_file, thread_override);
    } catch (const std::exception& e) {
        std::cerr << "[Error] Failed to load scene: " << e.what() << "\n";
        return 1;
    }

    session.Render();

    session.Save();

    return 0;
}
