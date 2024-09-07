#pragma once

template <typename T>
struct Result {
    bool status;

    T value;
};

template<>
struct Result<void> {
    bool status;
};

template <typename T>
inline Result<T> ok(T value) {
    Result<T> result {};
    result.status = true;
    result.value = value;
    return result;
}

inline Result<void> ok() {
    Result<void> result {};
    result.status = true;
    return result;
}

struct ResultErrorHelper {
    template <typename T>
    inline operator Result<T>() {
        Result<T> result {};
        result.status = false;
        return result;
    }
};

inline ResultErrorHelper err() {
    ResultErrorHelper helper {};
    return helper;
}

#define expect(name, expression) auto __##name##_result=(expression);if(!__##name##_result.status)return err();auto name=__##name##_result.value
#define expect_void(expression) auto __void_result=(expression);if(!__void_result.status)return err()