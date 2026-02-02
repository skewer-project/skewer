#include <ImfArray.h>
#include <ImfHeader.h>
#include <ImfPartType.h>
#include <ImfChannelList.h>
#include <ImfDeepFrameBuffer.h>
#include <ImfDeepScanLineInputFile.h>
#include <ImfDeepScanLineOutputFile.h>

#include "ImfPixelType.h"
#include "film/image_buffer.h"
#include "deep_image.h"
#include "../samples/drawImage.h"


namespace skwr {

DeepImageBuffer ImageIO::LoadEXR(const std::string filename) {

    //
    // Read a deep image using class DeepScanLineInputFile.  Try to read one
    // channel, A, of type HALF, and one channel, Z,
    // of type FLOAT.  Store the A, and Z pixels in two
    // separate memory buffers.
    //
    //    - open the file
    //    - allocate memory for the pixels
    //    - describe the layout of the A, and Z pixel buffers
    //    - read the sample counts from the file
    //    - allocate the memory required to store the samples
    //    - read the pixels from the file
    //

    Imf::DeepScanLineInputFile file (filename.c_str());

    const Imf::Header& header = file.header ();

    //  region for which pixel data are available is defined by a second axis-parallel rectangle in pixel space, the data window.
    Imath::Box2i dataWindow = header.dataWindow();
    // defined by the positions of the pixels in the upper left and lower right corners, (x min, y min) and (x max, y max).
    Imath::Box2i displayWindow = header.displayWindow();

    int width  = dataWindow.max.x - dataWindow.min.x + 1;
    int height = dataWindow.max.y - dataWindow.min.y + 1;

    // Create 2D arrays for samples count, r, g, b, and depth
    auto sampleCount = Imf::Array2D<unsigned int>(height, width);
    auto dataZ = Imf::Array2D<float*>(height, width);
    auto dataR = Imf::Array2D<half*>(height, width);
    auto dataG = Imf::Array2D<half*>(height, width);
    auto dataB = Imf::Array2D<half*>(height, width);
    auto dataA = Imf::Array2D<half*>(height, width);

    Imf::DeepFrameBuffer frameBuffer;

    frameBuffer.insertSampleCountSlice (Imf::Slice (
        Imf_3_1::UINT,
        (char*) (&sampleCount[0][0] - dataWindow.min.x -
                 dataWindow.min.y * width),
        sizeof (unsigned int) * 1,       // xStride
        sizeof (unsigned int) * width)); // yStride

    frameBuffer.insert (
        "dataZ",
        Imf_3_1::DeepSlice (
            Imf_3_1::FLOAT,
            (char*) (&dataZ[0][0] - dataWindow.min.x -
                     dataWindow.min.y * width),

            sizeof (float*) * 1,     // xStride for pointer array
            sizeof (float*) * width, // yStride for pointer array
            sizeof (float) * 1));    // stride for Z data sample

    frameBuffer.insert (
        "dataR",
        Imf_3_1::DeepSlice (
            Imf_3_1::HALF,
            (char*) (&dataR[0][0] - dataWindow.min.x -
                     dataWindow.min.y * width),
            sizeof (half*) * 1,     // xStride for pointer array
            sizeof (half*) * width, // yStride for pointer array
            sizeof (half) * 1));    // stride for O data sample

    frameBuffer.insert (
        "dataG",
        Imf_3_1::DeepSlice (
            Imf_3_1::HALF,
            (char*) (&dataG[0][0] - dataWindow.min.x -
                     dataWindow.min.y * width),
            sizeof (half*) * 1,     // xStride for pointer array
            sizeof (half*) * width, // yStride for pointer array
            sizeof (half) * 1));    // stride for O data sample

    frameBuffer.insert (
        "dataB",
        Imf_3_1::DeepSlice (
            Imf_3_1::HALF,
            (char*) (&dataB[0][0] - dataWindow.min.x -
                     dataWindow.min.y * width),
            sizeof (half*) * 1,     // xStride for pointer array
            sizeof (half*) * width, // yStride for pointer array
            sizeof (half) * 1));    // stride for O data sample

    frameBuffer.insert (
        "dataA",
        Imf_3_1::DeepSlice (
            Imf_3_1::HALF,
            (char*) (&dataA[0][0] - dataWindow.min.x -
                     dataWindow.min.y * width),
            sizeof (half*) * 1,     // xStride for pointer array
            sizeof (half*) * width, // yStride for pointer array
            sizeof (half) * 1));    // stride for O data sample

    file.setFrameBuffer(frameBuffer);

    // reads only the table of contents for the file. It goes through the file and finds out how many samples
    // exist for every single pixel in the specified row range.
    //
    // It looks at frameBuffer and fills sampleCount with integers (num samples at [x][y])
    file.readPixelSampleCounts(dataWindow.min.y, dataWindow.max.y);

    // OpenEXR does not allocate memory for the pixel data. It assumes WE have allocated enough space.
    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width; j++)
        {
            // It allocates a raw array (buffer) big enough to hold that many samples for that specific channel.
            dataZ[i][j] = new float[sampleCount[i][j]];
            dataR[i][j] = new half[sampleCount[i][j]];
            dataG[i][j] = new half[sampleCount[i][j]];
            dataB[i][j] = new half[sampleCount[i][j]];
            dataA[i][j] = new half[sampleCount[i][j]];
        }
    }

    // This reads the actual heavy data (the float/half values).
    //
    // It looks at the frameBuffer again, and sees the DeepSlice for, say, "dataZ" pointing to your dataZ
    // array of pointers. (remember this? (char*) &dataZ - dataWindow.min.x - dataWindow.min.y * width)
    //
    // For pixel (0,0), it grabs the pointer dataZ[0][0].
    // It writes the Z-values from the file into the memory that pointer points to.
    file.readPixels(dataWindow.min.y, dataWindow.max.y);

    // Clear memory for temporary data buffers for each channel
    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width; j++)
        {
            delete[] dataZ[i][j];
            delete[] dataR[i][j];
            delete[] dataG[i][j];
            delete[] dataB[i][j];
            delete[] dataA[i][j];
        }
    }


    // TODO: Port frameBuffer to DeepImageBuffer and return
    return DeepImageBuffer(0, 0);

}

}

// OpenEXR File Layout: https://openexr.com/en/latest/OpenEXRFileLayout.html

/*
 * Strategy for `SaveEXR`:
    1. Create Temporary Pointer Arrays: Create std::vector<float*> zPtrs, std::vector<half*> rPtrs, etc., that are width * height in size.
    2. Point to your Data: Loop through your DeepImageBuffer. For each pixel, point the zPtrs[i] to &pixel.samples[0].Z.
        NOTE: DeepSample struct must be standard layout. If you have struct { float Z; Spectrum c; }, the stride for Z is
            sizeof(DeepSample).
    3. Configure FrameBuffer:
        - insert("Z", DeepSlice(FLOAT, (char*)zPtrs.data()..., sizeof(float*), sizeof(float*)*width, sizeof(DeepSample)))
        - Note the last argument!!! The sampleStride is now sizeof(DeepSample), not sizeof(float). This tells OpenEXR to skip over the Color
          bytes to find the next Z value.
 */
