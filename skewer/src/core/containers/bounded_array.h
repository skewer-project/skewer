#ifndef SKWR_CORE_CONTAINERS_BOUNDED_ARRAY_H_
#define SKWR_CORE_CONTAINERS_BOUNDED_ARRAY_H_

#include <cstddef>

namespace skwr {

template <typename T, size_t MaxCapacity>
class BoundedArray {
  public:
    inline void push_back(const T& item) {
        // assert(count < MaxCapacity && "Exceeded max depth capacity!");
        if (count < MaxCapacity) {
            data[count++] = item;
        }
    }

    inline void clear() { count = 0; }

    inline size_t size() const { return count; }
    inline bool empty() const { return count == 0; }

    inline const T& operator[](size_t i) const { return data[i]; }
    inline T& operator[](size_t i) { return data[i]; }

    // Ranged-based for loop support
    inline T* begin() { return data; }
    inline T* end() { return data + count; }
    inline const T* begin() const { return data; }
    inline const T* end() const { return data + count; }

  private:
    T data[MaxCapacity];
    size_t count = 0;
};

}  // namespace skwr

#endif  // SKWR_CORE_CONTAINERS_BOUNDED_ARRAY_H_
