#include "deep_compositor.h"

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfDeepScanLineInputFile.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfMultiPartInputFile.h>
#include <OpenEXR/ImfPartType.h>
#include <exrio/deep_reader.h>
#include <exrio/deep_writer.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

#include "deep_info.h"
#include "deep_merger.h"
#include "deep_row.h"
#include "utils.h"

namespace deep_compositor {

// Main Pipeline Function
// 1. Load deep EXR files into DeepImage objects
// 2. Merge samples across images based on depth proximity
// 3. Output merged deep EXR, flattened EXR, and PNG preview

const int NUM_CHANNELS = 6;

enum RowStates { EMPTY, LOADED, MERGED, FLATTENED, ERROR };
// Helper to group shared data passed between stages
struct PipelineContext {
    const Options& opts;
    int height;
    int width;
    int window_size;
    int num_files;
    std::vector<std::unique_ptr<DeepInfo>>& images_info;

    std::vector<std::vector<DeepRow>>& input_buffer;
    std::vector<DeepRow>& merged_buffer;
    std::vector<std::atomic<int>>& row_status;
    std::atomic<int>& loaded_scanlines;
    std::atomic<int>& current_row;
    std::vector<float>& final_image;
    exrio::DeepImage* deep_image;  // nullptr if --deep-output not requested
};

void LoaderWorker(int start_row, int end_row, PipelineContext& ctx) {
    // printf("LOADING %d , %d \n", start_row, end_row);
    fflush(stdout);
    for (int load_y = start_row; load_y < end_row; load_y++) {
        if (load_y >= ctx.height) break;

        int slot = load_y % ctx.window_size;

        // Circular buffer safety: Wait if the slot is still being processed by the writer
        if (load_y >= ctx.window_size) {
            while (ctx.row_status[load_y - ctx.window_size].load() < FLATTENED) {
                std::this_thread::yield();
            }
        }

        for (int i = 0; i < ctx.num_files; ++i) {
            Imf::DeepScanLineInputFile& file = ctx.images_info[i]->GetFile();
            DeepRow& row = ctx.input_buffer[i][slot];

            const unsigned int* tempCounts = ctx.images_info[i]->GetSampleCountsForRow(load_y);
            row.Allocate(ctx.width, tempCounts);

            size_t sampleStride = NUM_CHANNELS * sizeof(float);
            std::vector<float*> rPtrs(ctx.width), gPtrs(ctx.width), bPtrs(ctx.width),
                aPtrs(ctx.width), zPtrs(ctx.width), zbPtrs(ctx.width);

            float* currentPixelPtr = row.all_samples.get();
            for (int x = 0; x < ctx.width; ++x) {
                rPtrs[x] = currentPixelPtr + 0;
                gPtrs[x] = currentPixelPtr + 1;
                bPtrs[x] = currentPixelPtr + 2;
                aPtrs[x] = currentPixelPtr + 3;
                zPtrs[x] = currentPixelPtr + 4;
                zbPtrs[x] = currentPixelPtr + 5;
                currentPixelPtr += row.sample_counts[x] * NUM_CHANNELS;
            }

            Imf::DeepFrameBuffer frameBuffer;
            int x_min = file.header().dataWindow().min.x;
            frameBuffer.insertSampleCountSlice(Imf::Slice(
                Imf::UINT, (char*)(row.sample_counts.data() - x_min), sizeof(unsigned int), 0));

            size_t xStride = sizeof(float*);
            frameBuffer.insert(
                "R", Imf::DeepSlice(Imf::FLOAT, (char*)rPtrs.data(), xStride, 0, sampleStride));
            frameBuffer.insert(
                "G", Imf::DeepSlice(Imf::FLOAT, (char*)gPtrs.data(), xStride, 0, sampleStride));
            frameBuffer.insert(
                "B", Imf::DeepSlice(Imf::FLOAT, (char*)bPtrs.data(), xStride, 0, sampleStride));
            frameBuffer.insert(
                "A", Imf::DeepSlice(Imf::FLOAT, (char*)aPtrs.data(), xStride, 0, sampleStride));
            frameBuffer.insert(
                "Z", Imf::DeepSlice(Imf::FLOAT, (char*)zPtrs.data(), xStride, 0, sampleStride));
            frameBuffer.insert("ZBack", Imf::DeepSlice(Imf::FLOAT, (char*)zbPtrs.data(), xStride, 0,
                                                       sampleStride));

            file.setFrameBuffer(frameBuffer);
            file.readPixels(load_y, load_y);
        }

        ctx.row_status[load_y].store(LOADED);
        ctx.loaded_scanlines.fetch_add(1);
    }
}

void MergerWorker(int start_row, int end_row, PipelineContext& ctx) {
    // printf("MERGING %d , %d \n", start_row, end_row);
    fflush(stdout);
    for (int i = start_row; i < end_row; i++) {  // For loop for single threading support
        int merge_y = ctx.current_row.fetch_add(1);

        if (merge_y >= ctx.height || merge_y >= end_row) break;  // conditional to end;

        while (ctx.row_status[merge_y].load() < LOADED) {
            std::this_thread::yield();
        }

        int slot = merge_y % ctx.window_size;
        DeepRow& outputRow = ctx.merged_buffer[slot];

        int maxSamplesForPixel = 0;
        for (int i = 0; i < ctx.num_files; ++i) {
            maxSamplesForPixel += ctx.input_buffer[i][slot].total_samples_in_row;
        }

        // Safety buffer for volumetric splitting
        outputRow.Allocate(ctx.width, maxSamplesForPixel * 2);

        // One running pointer per input file to avoid O(x) prefix-sum in GetPixelData
        std::vector<const float*> runningPtrs(ctx.num_files);
        for (int i = 0; i < ctx.num_files; ++i)
            runningPtrs[i] = ctx.input_buffer[i][slot].all_samples.get();

        for (int x = 0; x < ctx.width; ++x) {
            std::vector<const float*> pixelDataPtrs;
            std::vector<unsigned int> pixelSampleCounts;

            for (int i = 0; i < ctx.num_files; ++i) {
                DeepRow& inputRow = ctx.input_buffer[i][slot];
                unsigned int cnt = inputRow.GetSampleCount(x);
                pixelDataPtrs.push_back(runningPtrs[i]);
                pixelSampleCounts.push_back(cnt);
                runningPtrs[i] += cnt * 6;
            }
            SortAndMergePixelsWithSplit(x, pixelDataPtrs, pixelSampleCounts, outputRow,
                                        ctx.opts.merge_threshold);
        }
        ctx.row_status[merge_y].store(MERGED);
    }
}

void WriterWorker(int start_row, int end_row, PipelineContext& ctx) {
    // printf("WRITING %d , %d \n", start_row, end_row);
    fflush(stdout);
    for (int write_y = start_row; write_y < end_row; write_y++) {
        if (write_y >= ctx.height || write_y >= end_row) break;

        while (ctx.row_status[write_y].load() < MERGED) {
            std::this_thread::yield();
        }

        int slot = write_y % ctx.window_size;
        const DeepRow& deepRow = ctx.merged_buffer[slot];

        if (ctx.deep_image != nullptr) {
            const float* pixelData = deepRow.all_samples.get();
            for (int x = 0; x < ctx.width; ++x) {
                unsigned int numSamples = deepRow.GetSampleCount(x);
                exrio::DeepPixel& outPixel = ctx.deep_image->pixel(x, write_y);
                for (unsigned int s = 0; s < numSamples; ++s) {
                    const float* sp = pixelData + s * 6;
                    // DeepRow layout: [R, G, B, A, Z, ZBack]
                    outPixel.addSample(exrio::DeepSample(sp[4], sp[5], sp[0], sp[1], sp[2], sp[3]));
                }
                pixelData += numSamples * 6;
            }
        }

        std::vector<float> rowRGB(ctx.width * 4);
        FlattenRow(deepRow, rowRGB);

        std::copy(rowRGB.begin(), rowRGB.end(),
                  ctx.final_image.begin() + (write_y * ctx.width * 4));

        const_cast<DeepRow&>(deepRow).Clear();
        ctx.row_status[write_y].store(FLATTENED);
    }
}

std::vector<float> ProcessAllEXR(const Options& opts, int height, int width,
                                 std::vector<std::unique_ptr<DeepInfo>>& images_info) {
    const int window_size = 48;
    int num_files = opts.input_files.size();

    std::vector<std::vector<DeepRow>> m_inputBuffer(num_files);

    for (int i = 0; i < num_files; ++i) {
        m_inputBuffer[i].resize(window_size);
    }

    std::vector<DeepRow> m_mergedBuffer;
    m_mergedBuffer.resize(window_size);

    std::vector<std::atomic<int>> row_status(height);
    for (int i = 0; i < height; ++i) row_status[i].store(EMPTY);

    std::atomic<int> loaded_scanlines{0};
    std::atomic<int> current_merge_row{0};
    std::vector<float> final_image(width * height * 4, 0.0f);

    std::unique_ptr<exrio::DeepImage> deep_image;
    if (opts.deep_output) {
        deep_image = std::make_unique<exrio::DeepImage>(width, height);
    }

    PipelineContext ctx{opts,
                        height,
                        width,
                        window_size,
                        num_files,
                        images_info,
                        m_inputBuffer,
                        m_mergedBuffer,
                        row_status,
                        loaded_scanlines,
                        current_merge_row,
                        final_image,
                        deep_image.get()};

    int n = std::max(1, (int)std::thread::hardware_concurrency());
    // Iterative loop
    if (n <= 3) {
        // printf("STARTED THIS LOOP");
        fflush(stdout);
        int iterations = (height + window_size - 1) / window_size;
        for (int i = 0; i < iterations; i++) {
            fflush(stdout);
            int pos = window_size * i;
            int end = std::min(pos + window_size, height);
            LoaderWorker(pos, end, ctx);
            MergerWorker(pos, end, ctx);
            WriterWorker(pos, end, ctx);
        }
    } else {
        std::vector<std::thread> threads;
        threads.emplace_back(LoaderWorker, 0, height, std::ref(ctx));
        for (int i = 0; i < n - 2; ++i) {
            threads.emplace_back(MergerWorker, 0, height, std::ref(ctx));
        }
        threads.emplace_back(WriterWorker, 0, height, std::ref(ctx));

        for (auto& t : threads)
            if (t.joinable()) t.join();
    }

    printf("\nPipeline complete!\n");

    if (opts.deep_output && deep_image) {
        std::string deepPath = opts.output_prefix + "_merged.exr";
        exrio::writeDeepEXR(*deep_image, deepPath);
        Log("  Wrote: " + deepPath);
    }

    return final_image;
}

}  // namespace deep_compositor
