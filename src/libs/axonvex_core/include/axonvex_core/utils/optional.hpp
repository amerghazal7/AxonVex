#pragma once

#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace axonvex {

struct nullopt_t {
    struct init {};
    constexpr explicit nullopt_t(init) {}
};
constexpr nullopt_t nullopt{nullopt_t::init{}};

template <typename T>
class optional {
  private:
    alignas(T) unsigned char storage_[sizeof(T)];
    bool engaged_{false};

    T* ptr() noexcept { return reinterpret_cast<T*>(storage_); }
    const T* ptr() const noexcept { return reinterpret_cast<const T*>(storage_); }

    void destroy() noexcept {
        if (engaged_) {
            ptr()->~T();
            engaged_ = false;
        }
    }

  public:
    optional() noexcept : engaged_(false) {}

    optional(nullopt_t) noexcept : engaged_(false) {} // NOLINT

    optional(const optional& o) : engaged_(false) {
        if (o.engaged_) {
            new (storage_) T(*o.ptr());
            engaged_ = true;
        }
    }

    optional(optional&& o) noexcept(std::is_nothrow_move_constructible<T>::value) : engaged_(false) {
        if (o.engaged_) {
            new (storage_) T(std::move(*o.ptr()));
            engaged_ = true;
            o.destroy();
        }
    }

    optional(const T& v) : engaged_(true) { new (storage_) T(v); }

    optional(T&& v) noexcept(std::is_nothrow_move_constructible<T>::value)
        : engaged_(true) {
        new (storage_) T(std::move(v));
    }

    ~optional() { destroy(); }

    optional& operator=(nullopt_t) noexcept {
        destroy();
        return *this;
    }

    optional& operator=(const optional& o) {
        if (this == &o) return *this;
        if (o.engaged_) {
            if (engaged_)
                *ptr() = *o.ptr();
            else {
                new (storage_) T(*o.ptr());
                engaged_ = true;
            }
        } else {
            destroy();
        }
        return *this;
    }

    optional& operator=(optional&& o) noexcept(
        std::is_nothrow_move_assignable<T>::value&& std::is_nothrow_move_constructible<T>::value) {
        if (this == &o) return *this;
        if (o.engaged_) {
            if (engaged_)
                *ptr() = std::move(*o.ptr());
            else {
                new (storage_) T(std::move(*o.ptr()));
                engaged_ = true;
            }
            o.destroy();
        } else {
            destroy();
        }
        return *this;
    }

    optional& operator=(const T& v) {
        if (engaged_)
            *ptr() = v;
        else {
            new (storage_) T(v);
            engaged_ = true;
        }
        return *this;
    }

    optional& operator=(T&& v) {
        if (engaged_)
            *ptr() = std::move(v);
        else {
            new (storage_) T(std::move(v));
            engaged_ = true;
        }
        return *this;
    }

    void reset() noexcept { destroy(); }

    bool has_value() const noexcept { return engaged_; }
    explicit operator bool() const noexcept { return engaged_; }

    T& operator*() noexcept { return *ptr(); }
    const T& operator*() const noexcept { return *ptr(); }

    T* operator->() noexcept { return ptr(); }
    const T* operator->() const noexcept { return ptr(); }

    T& value() {
        if (!engaged_) throw std::logic_error("optional: no value");
        return *ptr();
    }

    const T& value() const {
        if (!engaged_) throw std::logic_error("optional: no value");
        return *ptr();
    }

    template <typename U>
    T value_or(U&& default_value) const {
        return engaged_ ? *ptr() : static_cast<T>(std::forward<U>(default_value));
    }

    template <typename... Args>
    void emplace(Args&&... args) {
        destroy();
        new (storage_) T(std::forward<Args>(args)...);
        engaged_ = true;
    }
};

} // namespace axonvex
