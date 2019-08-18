#pragma once

template <typename T>
struct Result {
    bool status;

    T value;
};

#define expect(name, expression) auto __##name##_result=expression;if(!__##name##_result.status)return{false};auto name=__##name##_result.value;