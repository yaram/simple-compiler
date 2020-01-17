#pragma once

template <typename T>
struct Result {
    bool status;

    T value;
};

template <typename T>
static Result<T> ok(T value) {
    return {
        true,
        value
    };
};

template <typename T>
static Result<T> err() {
    Result<T> result;

    result.status = false;

    return result;
};

#define expect(name, expression, ret) auto __##name##_result=expression;if(!__##name##_result.status)return ret;auto name=__##name##_result.value;