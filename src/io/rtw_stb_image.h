#ifndef RTW_STB_IMAGE_H
#define RTW_STB_IMAGE_H
//==============================================================================================
// Image loading wrapper using stb_image for texture support.
//==============================================================================================

// Disable strict warnings for this header from the Microsoft Visual C++ compiler.
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <cstdlib>
#include <iostream>
#include <string>

#include "stb_image.h"

class rtw_image {
  public:
    rtw_image() {}

    // Prevent double-free: disable copy operations since we manage raw pointers.
    rtw_image(const rtw_image&) = delete;
    rtw_image& operator=(const rtw_image&) = delete;

    // Allow move operations for efficient transfers.
    rtw_image(rtw_image&& other) noexcept
        : fdata(other.fdata),
          bdata(other.bdata),
          image_width(other.image_width),
          image_height(other.image_height),
          bytes_per_scanline(other.bytes_per_scanline) {
        other.fdata = nullptr;
        other.bdata = nullptr;
        other.image_width = 0;
        other.image_height = 0;
        other.bytes_per_scanline = 0;
    }

    rtw_image& operator=(rtw_image&& other) noexcept {
        if (this != &other) {
            // Free existing resources
            delete[] bdata;
            STBI_FREE(fdata);

            // Transfer ownership
            fdata = other.fdata;
            bdata = other.bdata;
            image_width = other.image_width;
            image_height = other.image_height;
            bytes_per_scanline = other.bytes_per_scanline;

            // Nullify source
            other.fdata = nullptr;
            other.bdata = nullptr;
            other.image_width = 0;
            other.image_height = 0;
            other.bytes_per_scanline = 0;
        }
        return *this;
    }

    rtw_image(const char* image_filename) {
        // Loads image data from the specified file. Searches for the image file
        // in various locations: current directory, images/ subdirectory, and
        // several parent directories.

        auto filename = std::string(image_filename);
        auto imagedir = getenv("RTW_IMAGES");

        // Hunt for the image file in some likely locations.
        if (imagedir && load(std::string(imagedir) + "/" + image_filename)) return;
        if (load(filename)) return;
        if (load("images/" + filename)) return;
        if (load("../images/" + filename)) return;
        if (load("../../images/" + filename)) return;
        if (load("../../../images/" + filename)) return;
        if (load("../../../../images/" + filename)) return;
        if (load("../../../../../images/" + filename)) return;
        if (load("../../../../../../images/" + filename)) return;

        std::cerr << "ERROR: Could not load image file '" << image_filename << "'.\n";
    }

    ~rtw_image() {
        delete[] bdata;
        STBI_FREE(fdata);
    }

    bool load(const std::string& filename) {
        // Loads the linear (gamma=1) image data from the given file name.
        auto n = bytes_per_pixel;
        fdata = stbi_loadf(filename.c_str(), &image_width, &image_height, &n, bytes_per_pixel);
        if (fdata == nullptr) return false;

        bytes_per_scanline = image_width * bytes_per_pixel;
        convert_to_bytes();
        return true;
    }

    int width() const { return (fdata == nullptr) ? 0 : image_width; }
    int height() const { return (fdata == nullptr) ? 0 : image_height; }

    const unsigned char* pixel_data(int x, int y) const {
        // Return the address of the three RGB bytes of the pixel at x,y.
        static unsigned char magenta[] = {255, 0, 255};
        if (bdata == nullptr) return magenta;

        x = clamp(x, 0, image_width);
        y = clamp(y, 0, image_height);

        return bdata + y * bytes_per_scanline + x * bytes_per_pixel;
    }

  private:
    const int bytes_per_pixel = 3;
    float* fdata = nullptr;
    unsigned char* bdata = nullptr;
    int image_width = 0;
    int image_height = 0;
    int bytes_per_scanline = 0;

    static int clamp(int x, int low, int high) {
        if (x < low) return low;
        if (x < high) return x;
        return high - 1;
    }

    static unsigned char float_to_byte(float value) {
        if (value <= 0.0) return 0;
        if (1.0 <= value) return 255;
        return static_cast<unsigned char>(256.0 * value);
    }

    void convert_to_bytes() {
        int total_bytes = image_width * image_height * bytes_per_pixel;
        bdata = new unsigned char[total_bytes];

        auto* bptr = bdata;
        auto* fptr = fdata;
        for (auto i = 0; i < total_bytes; i++, fptr++, bptr++) *bptr = float_to_byte(*fptr);
    }
};

// Restore MSVC compiler warnings
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
