#include <session/render_session.h>

#include <iostream>
#include <string>

void print_usage(const char* program_name)
{
    std::cerr << "Usage: " << "\n";
    std::cerr << "       " << program_name << "\n";
    std::cerr << "       " << program_name << " --name outfile.ppm\n";
    std::cerr << "Help:  " << "\n";
    std::cerr << "       " << program_name << " --help\n";
}

int main(int argc, char *argv[])
{
    // Parse Args
    bool help = (argc == 2 && strcmp(argv[1], "--help"));
    bool bad_args = (argc != 3 && argc != 1);
    if ( bad_args || help )
    {
        print_usage(argv[0]);
        return 1;
    }

    std::string outfile = "test_render.ppm";
    if (argc == 3 && strcmp(argv[1], "--name") == 0) // make name argument
        outfile = argv[2];

    int width = 400;
    int height = 225;

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
