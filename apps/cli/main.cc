/**
 * #include <api/render_session.h>
#include <iostream>

int main(int argc, char* argv[]) {
    // 1. Parse Args (Hardcoded for now)
    int width = 400;
    int height = 225;
    std::string outfile = "test_render.ppm";

    // 2. Start Session
    api::RenderSession session;
    session.Init(width, height);

    // 3. Render
    session.Render();

    // 4. Save
    session.Save(outfile);

    return 0;
}



#include <iostream>
#include <string>

#include "session/render_session.h"

int main(int argc, char* argv[]) {
  // Hardcoded for Phase 1
  const int width = 400;
  const int height = 225;
  const std::string outfile = "test_render.ppm";

  skwr::RenderSession session;

  session.Init(width, height);
  session.Render();
  session.Save(outfile);

  return 0;
}
 */