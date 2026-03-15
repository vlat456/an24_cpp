#pragma once

#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>
#include <initializer_list>
#include <algorithm>
#include <cassert>

namespace ui {

/// Stack-allocated small vector with inline storage for N elements.
/// Falls back to heap allocation when size exceeds N.
/// Trivially-copyable types use memcpy for moves/copies; others use
/// placement new + destructor calls.
template <typename T, size_t N>
class SmallVector {
    static_assert(N > 0, "SmallVector inline capacity must be > 0");

public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    SmallVector() = default;

    SmallVector(std::initializer_list<T> il) {
        reserve(il.size());
        for (const auto& v : il) push_back(v);
    }

    ~SmallVector() {
        destroy_range(begin(), end());
        if (!is_inline()) ::operator delete(heap_);
    }

    SmallVector(const SmallVector& o) {
        reserve(o.size_);
        copy_range(o.begin(), o.end(), begin());
        size_ = o.size_;
    }

    SmallVector& operator=(const SmallVector& o) {
        if (this != &o) {
            clear();
            reserve(o.size_);
            copy_range(o.begin(), o.end(), begin());
            size_ = o.size_;
        }
        return *this;
    }

    SmallVector(SmallVector&& o) noexcept {
        move_from(o);
    }

    SmallVector& operator=(SmallVector&& o) noexcept {
        if (this != &o) {
            destroy_range(begin(), end());
            if (!is_inline()) ::operator delete(heap_);
            size_ = 0;
            cap_ = N;
            heap_ = nullptr;
            move_from(o);
        }
        return *this;
    }

    // ---- Size / capacity ----

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    size_t capacity() const { return cap_; }

    void reserve(size_t new_cap) {
        if (new_cap <= cap_) return;
        grow(new_cap);
    }

    void resize(size_t new_size) {
        if (new_size > size_) {
            reserve(new_size);
            default_init(data() + size_, data() + new_size);
            size_ = new_size;
        } else if (new_size < size_) {
            destroy_range(data() + new_size, data() + size_);
            size_ = new_size;
        }
    }

    void clear() {
        destroy_range(begin(), end());
        size_ = 0;
    }

    // ---- Element access ----

    T& operator[](size_t i) { return data()[i]; }
    const T& operator[](size_t i) const { return data()[i]; }

    T& back() { return data()[size_ - 1]; }
    const T& back() const { return data()[size_ - 1]; }

    T* data() { return is_inline() ? reinterpret_cast<T*>(&inline_[0]) : heap_; }
    const T* data() const { return is_inline() ? reinterpret_cast<const T*>(&inline_[0]) : heap_; }

    // ---- Iterators ----

    iterator begin() { return data(); }
    iterator end() { return data() + size_; }
    const_iterator begin() const { return data(); }
    const_iterator end() const { return data() + size_; }

    // ---- Modifiers ----

    void push_back(const T& v) {
        if (size_ == cap_) grow(cap_ * 2);
        new (data() + size_) T(v);
        ++size_;
    }

    void push_back(T&& v) {
        if (size_ == cap_) grow(cap_ * 2);
        new (data() + size_) T(std::move(v));
        ++size_;
    }

    template <typename... Args>
    T& emplace_back(Args&&... args) {
        if (size_ == cap_) grow(cap_ * 2);
        T* p = new (data() + size_) T(std::forward<Args>(args)...);
        ++size_;
        return *p;
    }

    iterator erase(const_iterator pos) {
        auto* p = const_cast<T*>(pos);
        p->~T();
        size_t remaining = static_cast<size_t>(end() - p - 1);
        if (remaining > 0) {
            if constexpr (std::is_trivially_copyable_v<T>) {
                std::memmove(p, p + 1, remaining * sizeof(T));
            } else {
                for (size_t i = 0; i < remaining; ++i) {
                    new (p + i) T(std::move(p[i + 1]));
                    (p + i + 1)->~T();
                }
            }
        }
        --size_;
        return p;
    }

    iterator insert(const_iterator pos, const T& v) {
        size_t idx = static_cast<size_t>(pos - begin());
        if (size_ == cap_) grow(cap_ * 2);
        T* p = data() + idx;
        // Shift elements right
        if (idx < size_) {
            new (data() + size_) T(std::move(data()[size_ - 1]));
            for (size_t i = size_ - 1; i > idx; --i) {
                data()[i] = std::move(data()[i - 1]);
            }
            *p = v;
        } else {
            new (p) T(v);
        }
        ++size_;
        return p;
    }

private:
    bool is_inline() const { return heap_ == nullptr; }

    void grow(size_t new_cap) {
        if (new_cap < N) new_cap = N;
        T* new_buf = static_cast<T*>(::operator new(new_cap * sizeof(T)));
        if constexpr (std::is_trivially_copyable_v<T>) {
            if (size_ > 0) std::memcpy(new_buf, data(), size_ * sizeof(T));
        } else {
            for (size_t i = 0; i < size_; ++i) {
                new (new_buf + i) T(std::move(data()[i]));
                data()[i].~T();
            }
        }
        if (!is_inline()) ::operator delete(heap_);
        heap_ = new_buf;
        cap_ = new_cap;
    }

    void move_from(SmallVector& o) {
        if (o.is_inline()) {
            // Source is inline — must copy/move elements into our inline storage
            if constexpr (std::is_trivially_copyable_v<T>) {
                if (o.size_ > 0) std::memcpy(&inline_[0], &o.inline_[0], o.size_ * sizeof(T));
            } else {
                for (size_t i = 0; i < o.size_; ++i) {
                    new (reinterpret_cast<T*>(&inline_[0]) + i) T(std::move(o.data()[i]));
                    o.data()[i].~T();
                }
            }
            size_ = o.size_;
            cap_ = N;
            heap_ = nullptr;
        } else {
            // Source is heap — steal the buffer
            heap_ = o.heap_;
            size_ = o.size_;
            cap_ = o.cap_;
        }
        o.heap_ = nullptr;
        o.size_ = 0;
        o.cap_ = N;
    }

    static void destroy_range(T* first, T* last) {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (T* p = first; p != last; ++p) p->~T();
        }
    }

    static void copy_range(const T* first, const T* last, T* dest) {
        if constexpr (std::is_trivially_copyable_v<T>) {
            size_t n = static_cast<size_t>(last - first);
            if (n > 0) std::memcpy(dest, first, n * sizeof(T));
        } else {
            for (; first != last; ++first, ++dest) {
                new (dest) T(*first);
            }
        }
    }

    static void default_init(T* first, T* last) {
        if constexpr (std::is_trivially_default_constructible_v<T>) {
            std::memset(first, 0, static_cast<size_t>(last - first) * sizeof(T));
        } else {
            for (; first != last; ++first) new (first) T();
        }
    }

    alignas(T) char inline_[N * sizeof(T)]{};
    T* heap_ = nullptr;
    size_t size_ = 0;
    size_t cap_ = N;
};

} // namespace ui
