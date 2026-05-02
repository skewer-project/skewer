#ifndef SKWR_CORE_CONTAINERS_SMALL_VECTOR_H_
#define SKWR_CORE_CONTAINERS_SMALL_VECTOR_H_

#include <cstddef>
#include <utility>

namespace skwr {

// Hybrid container: stores up to InlineCapacity elements inline, then spills
// to a heap allocation. Designed for cases where the common case fits inline
// and the long tail is rare — e.g. per-pixel deep buckets, where the average
// pixel holds a single bucket but a few saturated pixels need many.
//
// Constraints:
//  - T must be trivially copyable / default constructible. The inline storage
//    is a plain array, and assignments use copy/move-assign rather than
//    placement-new, so non-trivial types would not be destroyed correctly.
//  - Non-copyable. Movable. The container is only intended to live inside
//    long-lived parent structures (e.g. a vector<Pixel> sized at construction).
template <typename T, std::size_t InlineCapacity>
class SmallVector {
  public:
    SmallVector() = default;
    ~SmallVector() { delete[] heap_data_; }

    SmallVector(const SmallVector&) = delete;
    SmallVector& operator=(const SmallVector&) = delete;

    SmallVector(SmallVector&& other) noexcept { steal(std::move(other)); }
    SmallVector& operator=(SmallVector&& other) noexcept {
        if (this != &other) {
            delete[] heap_data_;
            heap_data_ = nullptr;
            heap_capacity_ = 0;
            count_ = 0;
            steal(std::move(other));
        }
        return *this;
    }

    inline std::size_t size() const { return count_; }
    inline bool empty() const { return count_ == 0; }

    inline T& operator[](std::size_t i) {
        return (i < InlineCapacity) ? inline_data_[i] : heap_data_[i - InlineCapacity];
    }
    inline const T& operator[](std::size_t i) const {
        return (i < InlineCapacity) ? inline_data_[i] : heap_data_[i - InlineCapacity];
    }

    void push_back(const T& item) {
        if (count_ < InlineCapacity) {
            inline_data_[count_] = item;
        } else {
            const std::size_t heap_idx = count_ - InlineCapacity;
            if (heap_idx >= heap_capacity_) {
                grow_heap();
            }
            heap_data_[heap_idx] = item;
        }
        ++count_;
    }

    // Resets to empty AND releases the heap allocation. Callers that stream
    // through pixels (e.g. the row-by-row deep EXR writer) rely on this to
    // reclaim memory as they go.
    void clear() {
        count_ = 0;
        delete[] heap_data_;
        heap_data_ = nullptr;
        heap_capacity_ = 0;
    }

  private:
    void grow_heap() {
        const std::size_t new_cap = (heap_capacity_ == 0) ? 4 : heap_capacity_ * 2;
        T* new_data = new T[new_cap];
        for (std::size_t i = 0; i < heap_capacity_; ++i) {
            new_data[i] = std::move(heap_data_[i]);
        }
        delete[] heap_data_;
        heap_data_ = new_data;
        heap_capacity_ = new_cap;
    }

    void steal(SmallVector&& other) noexcept {
        const std::size_t inline_n =
            (other.count_ < InlineCapacity) ? other.count_ : InlineCapacity;
        for (std::size_t i = 0; i < inline_n; ++i) {
            inline_data_[i] = std::move(other.inline_data_[i]);
        }
        heap_data_ = other.heap_data_;
        heap_capacity_ = other.heap_capacity_;
        count_ = other.count_;
        other.heap_data_ = nullptr;
        other.heap_capacity_ = 0;
        other.count_ = 0;
    }

    T inline_data_[InlineCapacity]{};
    T* heap_data_ = nullptr;
    std::size_t heap_capacity_ = 0;
    std::size_t count_ = 0;
};

}  // namespace skwr

#endif  // SKWR_CORE_CONTAINERS_SMALL_VECTOR_H_
