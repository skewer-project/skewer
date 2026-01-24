gemini:

This folder is for "Modules" and "Toolchains." It keeps your root CMakeLists.txt clean.

What goes in here:

Find Packages: If you use a library that CMake doesn't know (like a specific version of OpenEXR or TBB), you write a FindOpenEXR.cmake file here.

Sanitizers: A file like Sanitizers.cmake to easily enable AddressSanitizer (ASan) to catch memory leaks.

Compiler Flags: A file CompilerSettings.cmake to strictly set warnings (-Wall -Wextra -Werror) for all files.

Action: Leave it empty for now. When you add OpenEXR later, you might need to drop a file in here if CMake can't find it automatically.