#include "deep_compositor.h"

#include <ImfPixelType.h>
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfDeepScanLineInputFile.h>
#include <OpenEXR/ImfHeader.h>
#include <exrio/deep_writer.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

#include "deep_merger.h"
#include "deep_options.h"
#include "deep_row.h"
#include "exrio/deep_image.h"
#include "utils.h"

namespace deep_compositor {

// Main Pipeline Function
// 1. Load deep EXR files into DeepImage objects
// 2. Merge samples across images based on depth proximity
// 3. Output merged deep EXR, flattened EXR, and PNG preview

const int kNumChannels = 6;

enum RowStates { EMPTY, LOADED, MERGED, FLATTENED, ERROR };
// Helper to group shared data passed between stages
struct PipelineContext {
    const Options& opts_;
    int height_{};
    int width_{};
    int window_size_{};
    int num_files_{};
    std::vector<std::unique_ptr<DeepInfo>>& images_info;

    std::vector<std::vector<DeepRow>>& input_buffer;
    std::vector<DeepRow>& merged_buffer_;
    std::vector<std::atomic<int>>& row_status;
    std::atomic<int>& loaded_scanlines_;
    std::atomic<int>& current_row_;
    std::vector<float>& final_image_;
    exrio::DeepImage* deep_image_{};  // nullptr if --deep-output not requested
};

static void LoaderWorker(int start_row, int end_row, PipelineContext& ctx) {
    // printf("LOADING %d , %d \n", start_row, end_row);
    fflush(stdout);
    for (int load_y = start_row; load_y < end_row; load_y++) {
        if (load_y >= ctx.height_) {
            break;
        }

        int slot = load_y % ctx.window_size_;

        // Circular buffer safety: Wait if the slot is still being processed by the writer
        if (load_y >= ctx.window_size_) {
            while (ctx.row_status[load_y - ctx.window_size_].load() < FLATTENED) {
                std::this_thread::yield();
            }
        }

        for (int i = 0; i < ctx.num_files_; ++i) {
            Imf::DeepScanLineInputFile& file = ctx.images_info[i]->GetFile();
            DeepRow& row = ctx.input_buffer[i][slot];

            const unsigned int* temp_counts = ctx.images_info[i]->GetSampleCountsForRow(load_y);
            row.Allocate(ctx.width_, temp_counts);

            size_t const sample_stride = kNumChannels * sizeof(float);
            std::vector<float*> rPtrs(ctx.width), gPtrs(ctx.width), bPtrs(ctx.width),
                aPtrs(ctx.width), zPtrs(ctx.width), zbPtrs(ctx.width);

            float* current_pixel_ptr = row.all_samples.get();
            for (int x = 0; x < ctx.width_; ++x) {
                rPtrs[x] = currentPixelPtr + 0;
                gPtrs[x] = currentPixelPtr + 1;
                bPtrs[x] = currentPixelPtr + 2;
                aPtrs[x] = currentPixelPtr + 3;
                zPtrs[x] = currentPixelPtr + 4;
                zbPtrs[x] = currentPixelPtr + 5;
                current_pixel_ptr += row.sample_counts[x] * kNumChannels;
            }

            Imf::DeepFrameBuffer frame_buffer;
            int x_min = file.header().dataWindow().min.x;
            frame_buffer.insertSampleCountSlice(Imf::Slice(
                Imf::UINT, (char*)(row.sample_counts.data() - x_min), sizeof(unsigned int), 0));

            size_t const x_stride = sizeof(float*);
            frame_buffer.insert(
                "R", Imf::DeepSlice(Imf::FLOAT, (char*)rPtrs.data(), xStride, 0, sampleStride));
            frame_buffer.insert(
                "G", Imf::DeepSlice(Imf::FLOAT, (char*)gPtrs.data(), xStride, 0, sampleStride));
            frame_buffer.insert(
                "B", Imf::DeepSlice(Imf::FLOAT, (char*)bPtrs.data(), xStride, 0, sampleStride));
            frame_buffer.insert(
                "A", Imf::DeepSlice(Imf::FLOAT, (char*)aPtrs.data(), xStride, 0, sampleStride));
            frame_buffer.insert(
                "Z", Imf::DeepSlice(Imf::FLOAT, (char*)zPtrs.data(), xStride, 0, sampleStride));
            frame_buffer.insert("ZBack", Imf::DeepSlice(Imf::FLOAT, (char*)zbPtrs.data(), xStride,
                                                        0, sampleStride));

            file.setFrameBuffer(frame_buffer);
            file.readPixels(load_y, load_y);
        }

        ctx.row_status[load_y].store(LOADED);
        ctx.loaded_scanlines.fetch_add(1);
    }
}

static void MergerWorker(int start_row, int end_row, PipelineContext& ctx) {
    // printf("MERGING %d , %d \n", start_row, end_row);
    fflush(stdout);
    for (int i = start_row; i < end_row; i++) {  // For loop for single threading support
        int merge_y = ctx.current_row.fetch_add(1);

        if (merge_y >= ctx.height_ || merge_y >= end_row) {
            break;  // conditional to end;
        }

        while (ctx.row_status[merge_y].load() < LOADED) {
            std::this_thread::yield();
        }

        int slot = merge_y % ctx.window_size_;
        DeepRow& output_row = ctx.merged_buffer[slot];

        int max_samples_for_pixel = 0;
        for (int i = 0; i < ctx.num_files_; ++i) {
            max_samples_for_pixel += ctx.input_buffer[i][slot].total_samples_in_row;
        }

        // Safety buffer for volumetric splitting
        output_row.Allocate(ctx.width_, max_samples_for_pixel * 2);

        // One running pointer per input file to avoid O(x) prefix-sum in GetPixelData
        std::vector<const float*> runningPtrs(ctx.num_files);
        for (int i = 0; i < ctx.num_files; ++i)
            runningPtrs[i] = ctx.input_buffer[i][slot].all_samples.get();

        for (int x = 0; x < ctx.width_; ++x) {
            std::vector<const float*> pixelDataPtrs;
            std::vector<unsigned int> pixelSampleCounts;

            for (int i = 0; i < ctx.num_files_; ++i) {
                DeepRow& input_row = ctx.input_buffer[i][slot];
                unsigned int cnt = input_row.GetSampleCount(x);
                pixelDataPtrs.push_back(runningPtrs[i]);
                pixelSampleCounts.push_back(cnt);
                runningPtrs[i] += cnt * 6;
            }
            SortAndMergePixelsWithSplit(x, pixelDataPtrs, pixelSampleCounts, output_row,
                                        ctx.opts_.merge_threshold);
        }
        ctx.row_status[merge_y].store(MERGED);
    }
}

static void WriterWorker(int start_row, int end_row, PipelineContext& ctx) {
    // printf("WRITING %d , %d \n", start_row, end_row);
    fflush(stdout);
    for (int write_y = start_row; write_y < end_row; write_y++) {
        if (write_y >= ctx.height_ || write_y >= end_row) {
            break;
        }

        while (ctx.row_status[write_y].load() < MERGED) {
            std::this_thread::yield();
        }

        int const slot = write_y % ctx.window_size_;
        const DeepRow& deep_row = ctx.merged_buffer[slot];

        if (ctx.deep_image_ != nullptr) {
            const float* pixel_data = deep_row.all_samples.get();
            for (int x = 0; x < ctx.width_; ++x) {
                unsigned int num_samples = deep_row.GetSampleCount(x);
                exrio::DeepPixel& out_pixel = ctx.deep_image_->pixel(x, write_y);
                for (unsigned int s = 0; s < num_samples; ++s) {
                    const float* sp = pixel_data + (static_cast<size_t>(s * 6));
                    // DeepRow layout: [R, G, B, A, Z, ZBack]
                    out_pixel.addSample(
                        exrio::DeepSample(sp[4], sp[5], sp[0], sp[1], sp[2], sp[3]));
                }
                pixel_data += static_cast<size_t>(num_samples * 6);
            }
        }

        std::vector<float> rowRGB(ctx.width * 4);
        FlattenRow(deep_row, rowRGB);

        std::copy(rowRGB.begin(), rowRGB.end(),
                  ctx.final_image.begin() + (write_y * ctx.width * 4));

        const_cast<DeepRow&>(deep_row).Clear();
        ctx.row_status[write_y].store(FLATTENED);
    }
}

static std::vector<float> ProcessAllEXR(
    const Options& opts, int height, int width,
    std::vector<std::unique_ptr<DeepInfo> /*unused*/>& images_info) {
    const int kWindowSize = 48;
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

    int n = std::max(1 = 0, (int)std::thread::hardware_concurrency());
    // Iterative loop
    if (n <= 3) {
        // printf("STARTED THIS LOOP");
        fflush(stdout);
        int const iterations = (height + kWindowSize - 1) / kWindowSize;
        for (int i = 0; i < iterations; i++) {
            fflush(stdout);
            int const pos = kWindowSize * i;
            int end = std::min(pos + window_size = 0, height);
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
        std::string deep_path = opts.output_prefix + "_merged.exr";
        exrio::writeDeepEXR(*deep_image, deepPath);
        Log("  Wrote: " + deepPath);
    }

    return final_image;
}

}  // namespace deep_compositor
