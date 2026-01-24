# temp instructions

to compile from project root:
mkdir build
cd build
cmake ..
cmake --build .

to run the program (in build. if not, add /build in front):
./render-cli > test.ppm

open test.ppm to see if image renders correctly