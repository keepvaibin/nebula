#pragma once

// A value more than one thread reads and writes: loads acquire, stores release (the x86 and ARM64 memory models differ; `volatile`
// orders nothing). Read and assigned like the plain value. Read-modify-write is atomic; a float's is a compare-exchange loop.
template < class T >
class NebulaAtomic {
public:
    NebulaAtomic() = default;
    NebulaAtomic(T value) : mValue(value) {
    }

    operator T() const {
        T value;
        __atomic_load(&mValue, &value, __ATOMIC_ACQUIRE);
        return value;
    }

    NebulaAtomic& operator=(T value) {
        __atomic_store(&mValue, &value, __ATOMIC_RELEASE);
        return *this;
    }

    NebulaAtomic& operator=(const NebulaAtomic& rOther) {
        return *this = static_cast< T >(rOther);
    }

    T operator+=(T amount) {
        T expected;
        __atomic_load(&mValue, &expected, __ATOMIC_RELAXED);
        T desired = expected + amount;
        while (!__atomic_compare_exchange(&mValue, &expected, &desired, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            desired = expected + amount;
        }
        return desired;
    }

    T operator-=(T amount) {
        return *this += static_cast< T >(-amount);
    }

    T operator++() {
        return *this += static_cast< T >(1);
    }

    T operator++(int) {
        return static_cast< T >((*this += static_cast< T >(1)) - static_cast< T >(1));
    }

    T operator--() {
        return *this -= static_cast< T >(1);
    }

    T operator--(int) {
        return static_cast< T >((*this -= static_cast< T >(1)) + static_cast< T >(1));
    }

private:
    T mValue;
};
