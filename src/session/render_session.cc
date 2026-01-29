/**
 * #include "render_session.h"
#include <iostream>

namespace api {

void RenderSession::Init(int w, int h) {
    width = w;
    height = h;
    buffer = std::make_unique<film::ImageBuffer>(width, height);
    std::cout << "Engine Initialized: " << width << "x" << height << std::endl;
}

void RenderSession::Render() {
    std::cout << "Starting Render..." << std::endl;
    // THE LOOP (Iterative)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Test Pattern: Red/Green Gradient
            float r = float(x) / (width - 1);
            float g = float(y) / (height - 1);
            buffer->SetPixel(x, y, Color3f(r, g, 0.2f));
        }
    }
    std::cout << "Render Complete." << std::endl;
}

void RenderSession::Save(const std::string& filename) {
    if (buffer) buffer->WritePPM(filename);
}

}
 */