#include <exrio/deep_image.h>
#include <exrio/deep_writer.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <vector>

#include "film/image_buffer.h"
#include "io/image_io.h"

namespace skwr {

class ImageIOTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Create a small 4x4 deep buffer
        int w = 4;
        int h = 4;

        Imf::Array2D<unsigned int> sampleCounts(h, w);
        size_t totalSamples = 0;

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                // Varying sample counts: some pixels have 0, 1, or 2 samples
                unsigned int count = (x + y) % 3;
                sampleCounts[y][x] = count;
                totalSamples += count;
            }
        }

        expectedBuffer = std::make_unique<DeepImageBuffer>(w, h, totalSamples, sampleCounts);

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                unsigned int count = sampleCounts[y][x];
                if (count > 0) {
                    std::vector<DeepSample> samples;
                    for (unsigned int i = 0; i < count; ++i) {
                        DeepSample s;
                        s.z_front = 10.0f + i * 5.0f;
                        s.z_back = 12.0f + i * 5.0f;
                        s.r = static_cast<float>(x) / w, s.g = static_cast<float>(y) / h,
                        s.b = static_cast<float>(i) / count, s.alpha = 0.5f;
                        samples.push_back(s);
                    }
                    expectedBuffer->SetPixel(x, y, samples);
                }
            }
        }
    }

    std::unique_ptr<DeepImageBuffer> expectedBuffer;
    const std::string testFilename = "test_output.exr";
};

TEST_F(ImageIOTest, SaveAndLoadEXR) {
    // Convert expectedBuffer to deep_compositor::DeepImage for writing
    int w = expectedBuffer->GetWidth();
    int h = expectedBuffer->GetHeight();
    deep_compositor::DeepImage img(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            DeepPixelView pv = expectedBuffer->GetPixel(x, y);
            for (size_t i = 0; i < pv.count; ++i) {
                img.pixel(x, y).addSample(deep_compositor::DeepSample(
                    pv[i].z_front, pv[i].z_back, pv[i].r, pv[i].g, pv[i].b, pv[i].alpha));
            }
        }
    }

    // Save via exrio
    ASSERT_NO_THROW(deep_compositor::writeDeepEXR(img, testFilename));

    // Load it back
    DeepImageBuffer loadedBuffer = ImageIO::LoadEXR(testFilename);

    // Verify metadata
    EXPECT_EQ(loadedBuffer.GetWidth(), expectedBuffer->GetWidth());
    EXPECT_EQ(loadedBuffer.GetHeight(), expectedBuffer->GetHeight());

    // Verify pixel data
    for (int y = 0; y < expectedBuffer->GetHeight(); ++y) {
        for (int x = 0; x < expectedBuffer->GetWidth(); ++x) {
            DeepPixelView expectedPixel = expectedBuffer->GetPixel(x, y);
            DeepPixelView loadedPixel = loadedBuffer.GetPixel(x, y);

            ASSERT_EQ(loadedPixel.count, expectedPixel.count) << "Mismatch at " << x << "," << y;

            for (size_t i = 0; i < expectedPixel.count; ++i) {
                EXPECT_FLOAT_EQ(loadedPixel[i].z_front, expectedPixel[i].z_front);
                EXPECT_FLOAT_EQ(loadedPixel[i].z_back, expectedPixel[i].z_back);
                EXPECT_FLOAT_EQ(loadedPixel[i].alpha, expectedPixel[i].alpha);
                EXPECT_FLOAT_EQ(loadedPixel[i].r, expectedPixel[i].r);
                EXPECT_FLOAT_EQ(loadedPixel[i].g, expectedPixel[i].g);
                EXPECT_FLOAT_EQ(loadedPixel[i].b, expectedPixel[i].b);
            }
        }
    }

    // Cleanup
    // if (std::filesystem::exists(testFilename)) {
    //     std::filesystem::remove(testFilename);
    // }
}

}  // namespace skwr
