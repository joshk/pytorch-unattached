#pragma once

#include "../core/tensor.h"
#include "caffe2/utils/Array.h"

namespace caffe2 {

struct SigmoidOp final {
    static constexpr const char* name = "sigmoid";

    using Signature = Tensor<CPUContext>(const Tensor<CPUContext>&);

    static constexpr c10::guts::array<const char*, 1> parameter_names = {{"input"}};
};

struct SigmoidOp2 final {
    static constexpr const char* name = "sigmoid";

    using Signature = bool(const Tensor<CPUContext>&, Tensor<CPUContext>*);

    static constexpr c10::guts::array<const char*, 2> parameter_names = {{"input", "output"}};
};

}
