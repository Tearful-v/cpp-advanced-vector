#pragma once
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <algorithm>
#include <utility>

template <typename T>
class RawMemory {
public:
RawMemory() noexcept = default;
explicit RawMemory(size_t capacity) : buffer_(Allocate(capacity)), capacity_(capacity) {}

RawMemory(const RawMemory&) = delete;
RawMemory& operator=(const RawMemory&) = delete;

RawMemory(RawMemory&& other) noexcept {
    buffer_ = other.buffer_;
    capacity_ = other.capacity_;
    other.buffer_ = nullptr;
    other.capacity_ = 0;
}

RawMemory& operator=(RawMemory&& other) noexcept {
    if (this != &other) {
        Deallocate(buffer_);

        buffer_ = other.buffer_;
        capacity_ = other.capacity_;

        other.buffer_ = nullptr;
        other.capacity_ = 0;
    }

    return *this;
}

~RawMemory() {
    Deallocate(buffer_);
}

T* GetAddress() noexcept {
    return buffer_;
}

const T* GetAddress() const noexcept {
    return buffer_;
}

size_t Capacity() const noexcept {
    return capacity_;
}

T& operator[](size_t index) noexcept {
    return *(buffer_ + index);
}

const T& operator[](size_t index) const noexcept {
    return *(buffer_ + index);
}

T* operator+(size_t offset) noexcept {
    return  buffer_ + offset;
}

const T* operator+(size_t offset) const noexcept {
    return buffer_ + offset;
}

T* begin() noexcept {
    return buffer_;
}

const T* begin() const noexcept {
    return buffer_;
}

T* end() noexcept {
    return buffer_ + capacity_;
}

const T* end() const noexcept {
    return buffer_ + capacity_;
}

void Swap(RawMemory& other) noexcept {
    std::swap(capacity_, other.capacity_);
    std::swap(buffer_, other.buffer_);
}

private:

static T* Allocate(size_t count) {
    return (count > 0) ? static_cast<T*>(operator new(sizeof(T) * count)) : nullptr;
}

static void Deallocate(T* buffer) noexcept {
    if (buffer) {
        operator delete(buffer);
    }
}

T* buffer_ = nullptr;
size_t capacity_ = 0;
};


template <typename T>
class Vector {
public:
    static void Destroy(T* ptr, size_t count) noexcept {
        std::destroy_n(ptr, count);
    }

    ~Vector() {
        Destroy(data_.GetAddress(), size_);
    }

    size_t GetIndex(const T* start, const T* finish) {
        return finish - start;
    }

    Vector() noexcept = default;

    explicit Vector(size_t elem_count)
        : data_(elem_count) {

        std::uninitialized_value_construct_n(
            data_.GetAddress(),
            elem_count
        );

        size_ = elem_count;
    }

    Vector(Vector&& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    Vector(const Vector& other)
        : data_(other.size_) {

        std::uninitialized_copy_n(
            other.data_.GetAddress(),
            other.size_,
            data_.GetAddress()
        );

        size_ = other.size_;
    }

    //надобность в этой функции появилась из-за желания написать общий PushBack для l и r - value
    static void UninitializedMoveOrCopy(T* from, size_t count, T* to) {
        if constexpr (
            std::is_nothrow_move_constructible_v<T>
            || !std::is_copy_constructible_v<T>
        ) {
            std::uninitialized_move_n(from, count, to);
        } else {
            std::uninitialized_copy_n(from, count, to);
        }
    }

    void Reserve(size_t capacity) {
        if (capacity <= Capacity()) {
            return;
        }

        RawMemory<T> tmp(capacity);

        UninitializedMoveOrCopy(
                data_.GetAddress(),
                Size(),
                tmp.GetAddress()
                );

        Destroy(data_.GetAddress(), size_);
        data_.Swap(tmp);
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        assert(index < size_);
        return data_[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    Vector& operator=(const Vector& other) {
        if (this != &other) {
            if (other.Size() > Capacity()) {
                Vector<T> tmp(other);
                Swap(tmp);
            } else if (other.Size() <= Capacity() && other.Size() > Size()) {

                std::copy(other.data_.begin(),
                    other.data_.begin() + Size(),
                    data_.GetAddress());

                std::uninitialized_copy(
                    other.data_.begin() + Size(),
                    other.data_.begin() + other.Size(),
                    data_.begin() + Size());

                size_ = other.size_;
            } else {
                std::copy(other.data_.begin(),
                    other.data_.begin() + other.Size(),
                    data_.GetAddress());

                Destroy(data_.GetAddress() + other.Size(),
                    Size() - other.Size());
                size_ = other.Size();
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& other) noexcept {
        if (this != &other) {
            Swap(other);
        }
        return *this;
    }

    void Resize(size_t new_size) {
        if (new_size < Size()) {
            Destroy(data_.GetAddress() + new_size,
                Size() - new_size);
        } else {

            if (new_size > Capacity()) {
                Reserve(new_size);
            }

            std::uninitialized_value_construct(
                data_.GetAddress() + Size(),
                data_.GetAddress() + new_size
            );
        }

        size_ = new_size;
    }

    bool Empty() {
        return Size() == 0;
    }

    void PopBack() noexcept {
        assert(!Empty());

        std::destroy_at(data_.GetAddress() + Size() - 1);
        --size_;
    }

    template <typename D> // общий для r-value и l-value
    void PushBack(D&& value) { //предоставляет strong гарантию
        if (Size() < Capacity()) {
            std::construct_at(
                data_.GetAddress() + Size(),
                std::forward<D>(value)
            );
            ++size_;
            return;
        }

        RawMemory<T> tmp((Size() == 0) ? 1 : Size() * 2);
        std::construct_at(
            tmp.GetAddress() + Size(),
            std::forward<D>(value)
        );

        try {
            UninitializedMoveOrCopy(
                data_.GetAddress(),
                Size(),
                tmp.GetAddress()
            );
        } catch (...) {
            std::destroy_at(tmp.GetAddress() + Size());
            throw;
        }

        Destroy(data_.GetAddress(), Size());
        data_.Swap(tmp);
        ++size_;
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        if (Size() < Capacity()) {
            std::construct_at(
                data_.GetAddress() + Size(),
                std::forward<Args>(args)...
            );
            ++size_;
            return data_[size_ - 1];
        }

        RawMemory<T> tmp((Size() == 0) ? 1 : Size() * 2);
        std::construct_at(
            tmp.GetAddress() + Size(),
            std::forward<Args>(args)...
        );

        try {
            UninitializedMoveOrCopy(
                data_.GetAddress(),
                Size(),
                tmp.GetAddress()
            );
        } catch (...) {
            std::destroy_at(tmp.GetAddress() + Size());
            throw;
        }

        Destroy(data_.GetAddress(), Size());
        data_.Swap(tmp);
        ++size_;
        return data_[size_ - 1];
    }

    using iterator = T*;
    using const_iterator = const T*;

    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_.GetAddress() + Size();
    }

    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator end() const noexcept {
        return data_.GetAddress() + Size();
    }

    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator cend() const noexcept {
        return data_.GetAddress() + Size();
    }

    template <typename U>
    iterator InsertImpl(const_iterator pos, U&& value) {
        const size_t index = GetIndex(data_.GetAddress(), pos);
        if (Size() < Capacity()) {
            T tmp(std::forward<U>(value));
            if (index == Size()) {
                new (data_.GetAddress() + Size()) T(std::move(tmp));
            } else {
                new (data_.GetAddress() + Size())
                    T(std::move(data_[Size() - 1]));

                try {
                    std::move_backward(
                        data_.GetAddress() + index,
                        data_.GetAddress() + Size() - 1,
                        data_.GetAddress() + Size()
                    );
                    data_[index] = std::move(tmp);
                } catch (...) {
                    std::destroy_at(data_.GetAddress() + Size());
                    throw;
                }
            }
            ++size_;
            return begin() + index;
        }

        RawMemory<T> tmp((Size() == 0) ? 1 : Size() * 2);
        new (tmp.GetAddress() + index)
            T(std::forward<U>(value));
        bool left_constructed = false;

        try {
            UninitializedMoveOrCopy(
                data_.GetAddress(),
                index,
                tmp.GetAddress()
            );
            left_constructed = true;

            UninitializedMoveOrCopy(
                data_.GetAddress() + index,
                Size() - index,
                tmp.GetAddress() + index + 1
            );
        } catch (...) {
            if (left_constructed) {
                std::destroy_n(
                    tmp.GetAddress(),
                    index
                );
            }

            std::destroy_at(
                tmp.GetAddress() + index
            );
            throw;
        }

        Destroy(data_.GetAddress(), Size());
        data_.Swap(tmp);
        ++size_;
        return begin() + index;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return InsertImpl(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return InsertImpl(pos, std::move(value));
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        const size_t index = GetIndex(data_.GetAddress(), pos);
        if (Size() < Capacity()) {
            T tmp(std::forward<Args>(args)...);
            if (index == Size()) {
                std::construct_at(
                    data_.GetAddress() + Size(),
                    std::move(tmp)
                );
            } else {
                std::construct_at(
                    data_.GetAddress() + Size(),
                    std::move(data_[Size() - 1])
                );

                try {
                    std::move_backward(
                        data_.GetAddress() + index,
                        data_.GetAddress() + Size() - 1,
                        data_.GetAddress() + Size()
                    );
                    data_[index] = std::move(tmp);
                } catch (...) {
                    std::destroy_at(data_.GetAddress() + Size());
                    throw;
                }
            }

            ++size_;
            return begin() + index;
        }

        RawMemory<T> tmp((Size() == 0) ? 1 : Size() * 2);
        std::construct_at(
            tmp.GetAddress() + index,
            std::forward<Args>(args)...
        );

        bool left_constructed = false;
        try {
            UninitializedMoveOrCopy(
                data_.GetAddress(),
                index,
                tmp.GetAddress()
            );
            left_constructed = true;
            UninitializedMoveOrCopy(
                data_.GetAddress() + index,
                Size() - index,
                tmp.GetAddress() + index + 1
            );
        } catch (...) {
            if (left_constructed) {
                std::destroy_n(
                    tmp.GetAddress(),
                    index
                );
            }

            std::destroy_at(
                tmp.GetAddress() + index
            );

            throw;
        }

        Destroy(data_.GetAddress(), Size());
        data_.Swap(tmp);
        ++size_;
        return begin() + index;
    }

    iterator Erase(const_iterator pos) {
        const size_t index = pos - cbegin();

        std::move(
            begin() + index + 1,
            end(),
            begin() + index
        );

        std::destroy_at(data_.GetAddress() + Size() - 1);
        --size_;

        return begin() + index;
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;
};
