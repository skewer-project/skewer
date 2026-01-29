#include <session/render_session.h>

#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    // Parse Args (Hardcoded for now)
    int width = 400;
    int height = 225;
    std::string outfile = "test_render.ppm";

    // Start a rendering instance (Session)
    skwr::RenderSession session;

    // session.LoadScene();

    session.SetOptions(width, height, 1);

    // Render
    session.Render();

    // Save
    session.Save(outfile);

    return 0;
}