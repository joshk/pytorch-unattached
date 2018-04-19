#pragma once

#include "c10/Tensor.h"

// TODO: This header is boilerplate, it would be best if we did not have to write it
//
// In ATen, this header was autogenerated as a way of easily finding out what the necessary
// type when implementing a function should be.  It doesn't do so well as a check that you've
// actually copied the type correctly, because C++ allows overloads, so it interprets
// a mismatched header and definition of a function as simply being separate overloads,
// and doesn't complain at you until link time.
//
// Ostensibly, we're going to be reading out the TRUE type of the function at registration
// time, so maybe all of this is not necessary, but it depends on the exact mechanics
// of the dispatcher.
namespace c10 { namespace op {

void shrink_(const Tensor& self, int64_t outer_dim_new_size);
void resize_as_(const Tensor& self, const Tensor& other);
void view_(const Tensor& self, ArrayRef<int64_t> new_sizes);
Tensor clone(const Tensor& self);

}} // namespace c10::op
