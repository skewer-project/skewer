#include <session/render_session.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

void print_usage(const char* program_name) {
    std::cerr << "Usage:\n";
    std::cerr << "  " << program_name << " <scene.json> [options]\n";
    std::cerr << "\n";
    std::cerr << "Options:\n";
    std::cerr << "  --threads <n>   Override thread count\n";
    std::cerr << "  --frame <n>     Render a specific frame (default: use scene start/end)\n";
    std::cerr << "  --fps <n>       Override frame rate (default: 24.0)\n";
    std::cerr << "  --shutter <n>   Shutter duration in frames (default: 0.0, max: 1.0)\n";
    std::cerr << "\n";
    std::cerr << "Help:\n";
    std::cerr << "  " << program_name << " --help\n";
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

    std::string scene_file = argv[1];
    int thread_override = 0;
    int frame_override = -1;
    float fps_override = -1.0f;
    float shutter_override = -1.0f;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            thread_override = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--frame") == 0 && i + 1 < argc) {
            frame_override = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            fps_override = std::atof(argv[++i]);
        } else if (strcmp(argv[i], "--shutter") == 0 && i + 1 < argc) {
            shutter_override = std::atof(argv[++i]);
        }
    }

    skwr::RenderSession session;
    if (frame_override >= 0) {
        session.SetAnimationParams(frame_override, fps_override, shutter_override);
    }

    try {
        session.RenderScene(scene_file, thread_override);
    } catch (const std::exception& e) {
        std::cerr << "[Error] " << e.what() << "\n";
        return 1;
    }

    return 0;
}
