#include "deep_compositor.h"
#include "deep_volume.h"
#include "utils.h"

#include "indicators.h"
#include "deep_row.h"
#include "deep_merger.h"
#include "deep_info.h"

#include <exrio/deep_reader.h>
#include <OpenEXR/ImfDeepScanLineInputFile.h>
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfMultiPartInputFile.h>
#include <ImfDeepScanLineInputFile.h> // OpenEXR

#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>

#include <algorithm>
#include <stdexcept>


using namespace indicators;

namespace deep_compositor {


// Main Pipeline Function
// 1. Load deep EXR files into DeepImage objects
// 2. Merge samples across images based on depth proximity
// 3. Output merged deep EXR, flattened EXR, and PNG preview


enum RowStates {
    EMPTY,
    LOADED,
    MERGED,
    FLATTENED,
    ERROR
};




std::vector<float> processAllEXR(const Options& opts, int height, int width, std::vector<std::unique_ptr<DeepInfo>>& imagesInfo) {
    


    // ========================================================================
    // Key State Variables - validate files and load metadata
    // ========================================================================
    int numFiles = opts.inputFiles.size();



    // const int chunkSize = 16;
    const int windowSize = 48;
    
    std::atomic<int> loaded_scanlines{0};

    std::vector<std::vector<DeepRow>> m_inputBuffer;
    std::vector<DeepRow> m_mergedBuffer;


    // Initialize all row statuses to EMPTY (0)
    std::vector<std::atomic<int>> rowStatus(height);
    for (int i = 0; i < height; ++i) {
        rowStatus[i].store(EMPTY);
    }

    m_mergedBuffer.resize(windowSize); // One slot per scanline in the sliding window

    m_inputBuffer.resize(numFiles);
    for (int i = 0; i < numFiles; ++i) {
        // Resize the inner vector to the size of the sliding window
        m_inputBuffer[i].resize(windowSize);
    }



    

    // ========================================================================
    // Stage 1 LOAD - Load lines in chunks of 16
    // ========================================================================
    
    show_console_cursor(false);


    auto loader_worker = [&]() {
        // printf("Loading EXR data in chunks of %d scanlines...\n", chunkSize);
        int load_y = 0;
        while (load_y < height) {
            int slot = load_y % windowSize;

            // Yeild if y-windowSize hasn't been merged and written yet, meaning the merger worker hasn't caught up to the loader
        
            //  Prevent processing more than 16 files
            if (load_y >= windowSize) {
                while (rowStatus[load_y - windowSize].load() < FLATTENED) {
                    std::this_thread::yield(); 
                }
            }

            // printf("IDX: %d, Loading row %d...\n", logstate.load(), load_y);
            // 2. LOAD CHUNK FROM EACH FILE
            for (int i = 0; i < numFiles; ++i) {
                Imf::DeepScanLineInputFile& file = imagesInfo[i]->getFile(); // The Imf::DeepScanLineInputFile 
                DeepRow& row = m_inputBuffer[i][slot]; // Gets the index where we will write data
              
                // Loads row and gets the numbers corresponding to how many samples are in each pixel of that row
                const unsigned int* tempCounts = imagesInfo[i]->getSampleCountsForRow(load_y);
                // printf("IDX: %d, Loading Row: %d File: %d sample counts: %zu\n", logstate.load(), load_y, i, row.currentCapacity);

                if (tempCounts == nullptr) {
                    // printf("ERROR: rowCounts is NULL! Expect strange behaviour.\n");
                }
                // Custom allocation at the rowCounts pointer
                row.allocate(width, tempCounts); // Allocates space based on how big that row is

                // char* permanentCountPtr = (char*)row.sampleCounts.data();
                // char* basePtr = (char*)(row.allSamples);  // Location of block of memor
                
                size_t sampleStride = 6 * sizeof(float);
                size_t xStride = 0; // Since we're using a single contiguous block, xStride is 0
                // size_t yStride = 0; // Since we're using a single contiguous block, yStride is 0


                std::vector<float*> rPtrs(width), gPtrs(width), bPtrs(width), 
                aPtrs(width), zPtrs(width), zbPtrs(width);

                float* currentPixelPtr = row.allSamples;
                for (int x = 0; x < width; ++x) {
                    rPtrs[x]  = currentPixelPtr + 0; // Points to R
                    gPtrs[x]  = currentPixelPtr + 1; // Points to G
                    bPtrs[x]  = currentPixelPtr + 2; // Points to B
                    aPtrs[x]  = currentPixelPtr + 3; // Points to A
                    zPtrs[x]  = currentPixelPtr + 4; // Points to Z
                    zbPtrs[x] = currentPixelPtr + 5; // Points to ZBack
                    
                    // Move to the next pixel: jump by (samples in this pixel * 6 channels)
                    currentPixelPtr += row.sampleCounts[x] * 6;
                }


                std::vector<float*> pixelPointers(width);
                Imf::DeepFrameBuffer frameBuffer; 
                int x_min = file.header().dataWindow().min.x;


                frameBuffer.insertSampleCountSlice(Imf::Slice(
                    Imf::UINT,
                    (char*)(row.sampleCounts.data() - x_min), 
                    sizeof(unsigned int), // xStride: move to next int
                    0                     // yStride: 0
                ));

                // char* basePointers = (char*)pixelPointers.data();


                xStride = sizeof(float*);

                frameBuffer.insert("R", Imf::DeepSlice(Imf::FLOAT, (char*)rPtrs.data(),  xStride, 0, sampleStride));
                frameBuffer.insert("G", Imf::DeepSlice(Imf::FLOAT, (char*)gPtrs.data(),  xStride, 0, sampleStride));
                frameBuffer.insert("B", Imf::DeepSlice(Imf::FLOAT, (char*)bPtrs.data(),  xStride, 0, sampleStride));
                frameBuffer.insert("A", Imf::DeepSlice(Imf::FLOAT, (char*)aPtrs.data(),  xStride, 0, sampleStride));
                frameBuffer.insert("Z", Imf::DeepSlice(Imf::FLOAT, (char*)zPtrs.data(),  xStride, 0, sampleStride));
                frameBuffer.insert("ZBack", Imf::DeepSlice(Imf::FLOAT, (char*)zbPtrs.data(), xStride, 0, sampleStride));

                // printf("Setting frame buffer ");
                // printf("Attempting to read RGBAZ... \n");
                file.setFrameBuffer(frameBuffer);
                file.readPixels(load_y, load_y); 
                
            }

            // std::this_thread::sleep_for(std::chrono::milliseconds(500));
            // Update status after N rows
            rowStatus[load_y].store(LOADED);  // Update status to Loaded
            loaded_scanlines.fetch_add(1); 
            // printf("(IDX: %d) Finished loading row %d ...\n", logstate.load(), load_y);
            // Increment outer loop
            load_y++;

            //
            // if (load_y % (height / 10) == 0) {
            //     loadBar.set_progress(load_y/ (height / 10));
            // }
        }

    };
     // ========================================================================
    // Stage 2 Merge - Merge lines in parallel as they are loaded
    // ========================================================================

    std::atomic<int> currentRow{0};

    auto merger_worker = [&]() {
        int merge_y = 0;
   
        // printf("Merging scanlines in parallel...\n");
        while (merge_y < height) {
            merge_y = currentRow.fetch_add(1); // Atomically get the next row to merge

            if (merge_y >= height) {
                break; // No more rows to merge
            }
            // Atomic "Grab" - Thread-safe row selection

            // Hard Coded safety in case it catches up. 
            while (rowStatus[merge_y].load() < LOADED) { // Wait until the loader has loaded this row
                std::this_thread::yield();
            }

            int slot = merge_y % windowSize;
  
            DeepRow& outputRow = m_mergedBuffer[slot];
            
            int totalPossibleSamplesInRow = 0;
            int maxSamplesForPixel = 0;

            for (int i = 0; i < numFiles; ++i) {
                // Worst case: Volumetric splitting could potentially double samples, 
                // but let's start with the sum of all inputs.
                maxSamplesForPixel += m_inputBuffer[i][slot].totalSamplesInRow;
            }

            // Safety buffer for volumetric splitting (e.g., 2x)
            totalPossibleSamplesInRow += (maxSamplesForPixel * 1);  

            // Now allocate the merged row once
            m_mergedBuffer[slot].allocate(width, totalPossibleSamplesInRow);
            // printf("Allocated output row with capacity for %d samples\n", totalPossibleSamplesInRow);


            // Process scanline x at a time to keep memory usage low and allow for early merging
            for (int x = 0; x < width; ++x) {
                std::vector<const float*> pixelDataPtrs;
                std::vector<unsigned int> pixelSampleCounts;

                for (int i = 0; i < numFiles; ++i) {
                    DeepRow& inputRow = m_inputBuffer[i][slot];
                    pixelDataPtrs.push_back(inputRow.getPixelData(x));  // 
                    pixelSampleCounts.push_back(inputRow.getSampleCount(x));
                }
 
                // Merge pixels
                
                sortAndMergePixelsWithSplit(x, pixelDataPtrs, pixelSampleCounts, outputRow);
                
            }
            

            rowStatus[merge_y].store(MERGED); // Mark as Merged

        }

    };



    // ========================================================================
    // Stage 3 Write - Save lines in chunks of 16
    // ========================================================================

    BlockProgressBar writeBar{option::BarWidth{80},
                         option::Start{"["},
                         option::End{"]"},
                         option::ShowPercentage{true},
                         option::ShowElapsedTime{true},
                         option::ShowRemainingTime{true},
                         option::MaxProgress{10}};

    std::vector<float> finalImage(width * height * 4, 0.0f) ; // RGBA output buffer

    printf("Making an image of size %d x %d \n", width, height);
    auto writer_worker = [&]() {

        int write_y = 0;

        while (write_y < height) {

            // Ensure Merger has finished this row
            while (rowStatus[write_y].load() < MERGED) {
                std::this_thread::yield();
            }

            int slot = write_y % windowSize;
            const DeepRow& deepRow = m_mergedBuffer[slot];

            // Convert merged deep data to flat RGBA
            std::vector<float> rowRGB(width * 4);
            flattenRow(deepRow, rowRGB);

            std::copy(rowRGB.begin(), rowRGB.end(), finalImage.begin() + (write_y * width * 4)); // 4 channels (RGBA)


            const_cast<DeepRow&>(deepRow).clear(); 
            // Update status and progress bar

            rowStatus[write_y].store(FLATTENED); // Set back to Loaded so loader can reuse the slot for the next row
            write_y++;

            
            if (write_y % (height / 10) == 0) {
                writeBar.set_progress(write_y/ (height / 10));
            }
        }

        // Return the final flattened image data as a vector of floats (RGB interleaved)
        
    };

    



    int n = std::thread::hardware_concurrency();

    
    std::vector<std::thread> threads;
    threads.emplace_back(loader_worker);
    for (int i = 0; i < n - 2; ++i) {
        threads.emplace_back(merger_worker);
    }
    threads.emplace_back(writer_worker);

    // Wait for all threads to complete
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    printf("\nPipeline complete!\n");
    show_console_cursor(true);
    return finalImage;



}


bool validateDimensions(const std::vector<DeepImage>& inputs) {
    if (inputs.empty()) {
        return true;
    }
    
    int width = inputs[0].width();
    int height = inputs[0].height();
    
    for (size_t i = 1; i < inputs.size(); ++i) {
        if (inputs[i].width() != width || inputs[i].height() != height) {
            return false;
        }
    }
    
    return true;
}

bool validateDimensions(const std::vector<const DeepImage*>& inputs) {
    if (inputs.empty()) {
        return true;
    }
    
    int width = inputs[0]->width();
    int height = inputs[0]->height();
    
    for (size_t i = 1; i < inputs.size(); ++i) {
        if (inputs[i]->width() != width || inputs[i]->height() != height) {
            return false;
        }
    }
    
    return true;
}

// DeepPixel mergePixels(const std::vector<const DeepPixel*>& pixels,
//                       float mergeThreshold) {
//     return mergePixelsVolumetric(pixels, mergeThreshold);
// }

// DeepImage deepMerge(const std::vector<DeepImage>& inputs,
//                     const CompositorOptions& options,
//                     CompositorStats* stats, const std::vector<float>& zOffsets) {
//     // Convert to pointer version
//     std::vector<const DeepImage*> ptrs;
//     ptrs.reserve(inputs.size());
//     for (const auto& img : inputs) {
//         ptrs.push_back(&img);
//     }
    
//     return deepMerge(ptrs, options, stats, zOffsets);
// }

// DeepImage deepMerge(const std::vector<const DeepImage*>& inputs,
//                     const CompositorOptions& options,
//                     CompositorStats* stats, const std::vector<float>& zOffsets) {

//     DeepImage result(0, 0);
    
  

//     return result;
// }

} // namespace deep_compositor
