#include <ATen/ATen.h>
#include <ATen/SparseTensorImpl.h>
#include <ATen/ExpandUtils.h>
#include <ATen/NativeFunctions.h>

#include <TH/THBlasUtils.h>

namespace at { namespace native {

// Just for documentary purposes
using SparseTensor = Tensor;
using LongTensor = Tensor;

// --------------------------------------------------------------------
// Utility functions
// --------------------------------------------------------------------

namespace {
  // TODO: Expose this for real in ATen, some day?
  // NB: Doesn't preserve data.
  Tensor _new_values_with_size_of(const Tensor& values, int64_t nnz) {
    if (values.dim() == 0) { // values tensor uninitialized
      return values.type().tensor({nnz});
    } else {
      std::vector<int64_t> size = values.sizes();
      size[0] = nnz;
      return values.type().tensor(size);
    }
  }

  bool _is_same_density(const SparseTensor& self, const SparseTensor& src) {
    return self._sparseDims() == src._sparseDims() && self._denseDims() == src._denseDims();
  }

  // TODO: This is a temporary stop-gap, to allow us to perform some private
  // functionality.  Our eventual plan is to fill out the PUBLIC API with
  // enough functions so that math functions don't need to rely on this.
  SparseTensorImpl* _get_sparse_impl(const SparseTensor& self) {
    if (!self.is_sparse()) AT_ERROR("_internal_get_SparseTensorImpl: not a sparse tensor");
    return static_cast<SparseTensorImpl*>(self.unsafeGetTensorImpl());
  }

  // TODO: put this into the public API
  bool isSameTensor(const Tensor& lhs, const Tensor& rhs) {
    return lhs.unsafeGetTensorImpl() == rhs.unsafeGetTensorImpl();
  }

  LongTensor _to_csr(const int64_t* indices, int64_t dim, int64_t nnz) {
    int64_t h, i, hp0, hp1;
    LongTensor csr = native::zeros({dim + 1}, kLong);

    // TODO: eliminate this conditional when zero-size dims supported correctly
    if (nnz > 0) {
      auto csr_accessor = csr.accessor<int64_t, 1>();
      // Convert the sparse matrix to CSR format
#pragma omp parallel for private(i, h, hp0, hp1) schedule(static) if (nnz > 10000)
      for (i=0; i<nnz; i++) {
        hp0 = indices[i];
        hp1 = (i+1 == nnz) ?  dim : indices[i+1];
        if (hp0 != hp1) for (h = hp0; h < hp1; h++) {
          csr_accessor[h+1] = i+1;
        }
      }
    }
    return csr;
  }

}

// --------------------------------------------------------------------
// zero_(SparseTensor)
// --------------------------------------------------------------------

// hummu hummu
SparseTensor& zero_sparse_(SparseTensor& self) {
  AT_ASSERT(self.is_sparse());

  // NB: You must use _get_sparse_impl(self)->indices()
  // and not self._indices(), because the latter will possibly
  // return a view (which means that the in-place operation will
  // not work).
  if (_get_sparse_impl(self)->indices().numel()) {
    // TODO: To be fixed when we support zero-size dims
    _get_sparse_impl(self)->indices().resize_({0});
  }

  if (_get_sparse_impl(self)->values().numel()) {
    _get_sparse_impl(self)->values().resize_({0});
  }
  _get_sparse_impl(self)->set_nnz(0);
  _get_sparse_impl(self)->set_coalesced(true); // NB: This is new
  return self;
}

// NB: Don't need zeros, zeros_like, already implemented in TensorFactories

// --------------------------------------------------------------------
// mul(SparseTensor, Scalar)
// --------------------------------------------------------------------

SparseTensor& mul_out_sparse_scalar(SparseTensor& r, const SparseTensor& t, Scalar value) {
  AT_ASSERT(r.is_sparse());
  AT_ASSERT(t.is_sparse());

  if (isSameTensor(r, t)) {
    r._values().mul_(value);
  } else {
    r.resize_as_(t);
    r._indices().resize_as_(t._indices());
    r._indices().copy_(t._indices());
    Tensor r_values = r._values(); // Sigh... needed because mul_out takes Tensor&
    at::mul_out(r_values, t._values(), value);
    _get_sparse_impl(r)->set_nnz(t._nnz());
    _get_sparse_impl(r)->set_coalesced(t.is_coalesced());
  }
  return r;
}

SparseTensor mul_sparse_scalar(const SparseTensor& t, Scalar value) {
  SparseTensor r = t.type().tensor();
  mul_out_sparse_scalar(r, t, value);
  return r;
}

SparseTensor& mul_sparse_scalar_(SparseTensor& t, Scalar v) {
  return mul_out_sparse_scalar(t, t, v);
}

// --------------------------------------------------------------------
// pow(SparseTensor, Scalar)
// --------------------------------------------------------------------

// TODO: add in-place variant

SparseTensor& pow_out_sparse_scalar(SparseTensor& r, const SparseTensor& t_, Scalar value) {
  AT_ASSERT(r.is_sparse());
  AT_ASSERT(t_.is_sparse());
  AT_CHECK(value.toDouble() != 0, "cannot raise to zeroth power on sparse tensor; it would make the result tensor dense");

  // This coalesce is why we can't easily provide an inplace variant
  SparseTensor t = t_.coalesce();

  r.resize_as_(t);
  r._indices().resize_as_(t._indices());
  r._indices().copy_(t._indices());
  Tensor r_values = r._values(); // Sigh... needed because pow_out takes Tensor&
  at::pow_out(r_values, t._values(), value);
  _get_sparse_impl(r)->set_nnz(t._nnz());
  _get_sparse_impl(r)->set_coalesced(t.is_coalesced());

  return r;
}

SparseTensor pow_sparse_scalar(const SparseTensor& t, Scalar value) {
  SparseTensor r = t.type().tensor();
  pow_out_sparse_scalar(r, t, value);
  return r;
}

// --------------------------------------------------------------------
// div(SparseTensor, Scalar)
// --------------------------------------------------------------------

SparseTensor& div_out_sparse_scalar(SparseTensor& r, const SparseTensor& t, Scalar value) {
  AT_ASSERT(r.is_sparse());
  AT_ASSERT(t.is_sparse());

  if (isSameTensor(r, t)) {
    r._values().div_(value);
  } else {
    r.resize_as_(t);
    r._indices().resize_as_(t._indices());
    r._indices().copy_(t._indices());
    Tensor r_values = r._values(); // Sigh... needed because div_out takes Tensor&
    at::div_out(r_values, t._values(), value);
    _get_sparse_impl(r)->set_nnz(t._nnz());
    _get_sparse_impl(r)->set_coalesced(t.is_coalesced());
  }
  return r;
}

SparseTensor div_sparse_scalar(const SparseTensor& t, Scalar value) {
  SparseTensor r = t.type().tensor();
  div_out_sparse_scalar(r, t, value);
  return r;
}

SparseTensor& div_sparse_scalar_(SparseTensor& t, Scalar value) {
  return div_out_sparse_scalar(t, t, value);
}

// --------------------------------------------------------------------
// norm(SparseTensor, Scalar)
// --------------------------------------------------------------------

// Only supports floating point, FYI
Tensor norm_sparse(const SparseTensor& self, Scalar value) {
  AT_ASSERT(self.is_sparse());

  return self.coalesce()._values().norm(value);
}

// --------------------------------------------------------------------
// add(SparseTensor, SparseTensor, Scalar)  [broadcasts]
// --------------------------------------------------------------------

SparseTensor& s_add_out_sparse_cpu(SparseTensor& r, const SparseTensor& t, const SparseTensor& src, Scalar value) {
  AT_ASSERT(r.is_sparse());
  AT_ASSERT(t.is_sparse());

  AT_CHECK(t.sizes().equals(src.sizes()), "cadd operands have incompatible sizes");

  if (src._nnz() == 0) {
    return raw_copy_sparse_(r, t);
  }
  if (t._nnz() == 0) {
    return mul_out_sparse_scalar(r, src, value);
  }

  AT_CHECK(_is_same_density(t, src), "cadd operands have incompatible desnitities");

  // saving those because they can be overwritten when doing in-place operations
  int64_t t_nnz = t._nnz(), s_nnz = src._nnz(), max_nnz = t_nnz + s_nnz;
  bool t_coalesced = t.is_coalesced(), s_coalesced = src.is_coalesced();
  int64_t sparseDims = src._sparseDims();
  LongTensor t_indices = t._indices();
  Tensor t_values = t._values();
  LongTensor src_indices = src._indices();
  Tensor s_values = src._values();
  LongTensor r_indices = t_indices.type().tensor({sparseDims, max_nnz});
  Tensor r_values = _new_values_with_size_of(s_values, max_nnz).zero_();
  r.resize_as_(src);
  _get_sparse_impl(r)->set_indices_and_values(r_indices, r_values);  // TODO: sigh

  int64_t blockSize = r_values.stride(0);
  int64_t cmp, d;
  int64_t r_i = 0, t_i = 0, s_i = 0;

  // NB: relies on nnz tests above
  auto t_indices_accessor = t_indices.accessor<int64_t, 2>();
  auto r_indices_accessor = r_indices.accessor<int64_t, 2>();
  auto src_indices_accessor = src_indices.accessor<int64_t, 2>();

  AT_DISPATCH_ALL_TYPES(
      t_values.type(), "cadd_sparse", [&] {
        scalar_t* t_values_ptr = t_values.data<scalar_t>();
        scalar_t* s_values_ptr = s_values.data<scalar_t>();
        scalar_t* r_values_ptr = r_values.data<scalar_t>();
        scalar_t cast_value = value.to<scalar_t>();
        while (t_i < t_nnz || s_i < s_nnz) {
          if (t_i >= t_nnz) {
            cmp = -1;
          } else if (s_i >= s_nnz) {
            cmp = 1;
          } else {
            cmp = 0;
            for (d = 0; d < sparseDims; d++) {
              if (t_indices_accessor[d][t_i] < src_indices_accessor[d][s_i]) {
                cmp = 1;
                break;
              }
              if (t_indices_accessor[d][t_i] > src_indices_accessor[d][s_i]) {
                cmp = -1;
                break;
              }
            }
          }
          if (cmp >= 0) {
            for (d = 0; d < sparseDims; d++) {
              r_indices_accessor[d][r_i] = t_indices_accessor[d][t_i];
            }
            THBlas_axpy<scalar_t>(blockSize, 1,
              t_values_ptr + t_i * blockSize, 1,
              r_values_ptr + r_i * blockSize, 1);
            t_i++;
          }
          if (cmp <= 0) {
            for (d = 0; d < sparseDims; d++) {
              r_indices_accessor[d][r_i] = src_indices_accessor[d][s_i];
            }
            THBlas_axpy<scalar_t>(blockSize, cast_value,
              s_values_ptr + s_i * blockSize, 1,
              r_values_ptr + r_i * blockSize, 1);
            s_i++;
          }
          r_i++;
        }
      }
  );

  _get_sparse_impl(r)->set_nnz(r_i);
  // TODO: I think it may be possible to track inside the loop and
  // detect when we are uncoalesced (e.g., by observing that an
  // index goes backwards) which may be more precise than using the
  // coalesced flag here.  But this is easy.
  _get_sparse_impl(r)->set_coalesced(t_coalesced && s_coalesced);

  return r;
}

SparseTensor s_add_sparse_cpu(const SparseTensor& t, const SparseTensor& src, Scalar alpha) {
  SparseTensor r = t.type().tensor();
  s_add_out_sparse_cpu(r, t, src, alpha);
  return r;
}

SparseTensor& s_add_sparse_cpu_(SparseTensor& t, const SparseTensor& src, Scalar alpha) {
  return s_add_out_sparse_cpu(t, t, src, alpha);
}

// --------------------------------------------------------------------
// add(Tensor, SparseTensorRef, Scalar)
//    formerly known as spcadd
// --------------------------------------------------------------------

template <typename scalar_t>
void add_dense_sparse_worker_cpu(Tensor& r, Scalar value, const SparseTensor& sparse, const Tensor& indices, const Tensor& values) {
  int64_t k;

  auto indices_accessor = indices.accessor<int64_t, 2>();
  auto values_accessor = values.accessor<scalar_t, 1>();

  scalar_t* r_ptr = r.data<scalar_t>();
  scalar_t cast_value = value.to<scalar_t>();

  #pragma omp parallel for private(k)
  for (k = 0; k < sparse._nnz(); k++) {
    int64_t index = r.storage_offset();
    for (int64_t d = 0; d < sparse._sparseDims(); d++) {
      index += r.stride(d) * indices_accessor[d][k];
    }
    r_ptr[index] += cast_value * values_accessor[k];
  }
}

Tensor& add_out_dense_sparse_cpu(Tensor& r, const Tensor& dense, SparseTensorRef sparse__, Scalar value) {
  AT_ASSERT(!r.is_sparse());
  AT_ASSERT(!dense.is_sparse());
  AT_ASSERT(sparse__.tref.is_sparse());

  const SparseTensor& sparse_ = sparse__.tref;
  r.resize_as_(dense);
  SparseTensor sparse = sparse_.coalesce();

  LongTensor indices = sparse._indices();
  Tensor values = sparse._values();
  int64_t nDim = dense.dim();
  int64_t nDimI = sparse._sparseDims();

  if (!isSameTensor(r, dense)) r.copy_(dense);
  if (sparse._nnz() == 0) return r;

  // accessors rely on nnz test
  if (nDim > nDimI) {
    auto indices_accessor = indices.accessor<int64_t, 2>();
    for (int64_t k = 0; k < sparse._nnz(); k++) {
      Tensor dstBuffer = r;
      for (int64_t d = 0; d < sparse._sparseDims(); d++) {
        dstBuffer = dstBuffer.select(0, indices_accessor[d][k]);
      }
      Tensor srcBuffer = values.select(0, k);
      dstBuffer.add_(srcBuffer, value);
    }
  } else {
    AT_DISPATCH_ALL_TYPES(
        values.type(), "add_dense_sparse", [&] {
          add_dense_sparse_worker_cpu<scalar_t>(r, value, sparse, indices, values);
        });
  }
  return r;
}

Tensor add_dense_sparse_cpu(const Tensor& t, SparseTensorRef src, Scalar alpha) {
  Tensor r = t.type().tensor();
  add_out_dense_sparse_cpu(r, t, src, alpha);
  return r;
}

Tensor& add_dense_sparse_cpu_(Tensor& t, SparseTensorRef src, Scalar alpha) {
  return add_out_dense_sparse_cpu(t, t, src, alpha);
}


// --------------------------------------------------------------------
// sub(SparseTensor, SparseTensor, Scalar)  [broadcasts]
// --------------------------------------------------------------------

SparseTensor& s_sub_out_sparse_cpu(SparseTensor& r, const SparseTensor& t, const SparseTensor& src, Scalar value) {
  // UGH... We're doing two dispatches on scalar type here for no good reason.
  // NB: I tried adding an operator- to Scalar, but there isn't any good way
  // to negate the tensor, because I have a TensorBase...
  AT_DISPATCH_ALL_TYPES(
      t.type(), "sub_sparse", [&] {
        scalar_t cast_value = value.to<scalar_t>();
        s_add_out_sparse_cpu(r, t, src, -cast_value);
      }
  );
  return r;
}

SparseTensor s_sub_sparse_cpu(const SparseTensor& t, const SparseTensor& src, Scalar alpha) {
  SparseTensor r = t.type().tensor();
  s_sub_out_sparse_cpu(r, t, src, alpha);
  return r;
}

SparseTensor& s_sub_sparse_cpu_(SparseTensor& t, const SparseTensor& src, Scalar alpha) {
  return s_sub_out_sparse_cpu(t, t, src, alpha);
}

// --------------------------------------------------------------------
// mul(SparseTensor, SparseTensor, Scalar)  [broadcasts]
// --------------------------------------------------------------------

SparseTensor& s_mul_out_sparse_cpu(SparseTensor& r, const SparseTensor& t_, const SparseTensor& src_) {
  AT_CHECK(t_.sizes().equals(src_.sizes()), "cmul operands have incompatible sizes");
  if (src_._nnz() == 0 || t_._nnz() == 0) {
    return r.zero_();
  }

  SparseTensor t = t_.coalesce();
  SparseTensor src = src_.coalesce();

  // saving those because they can be overwritten when doing in-place operations
  int64_t t_nnz = t._nnz(), s_nnz = src._nnz();
  int64_t max_nnz = std::min(t_nnz, s_nnz);  // multiply by zero is zero, and can be dropped
  int64_t sparseDims = src._sparseDims();
  LongTensor t_indices = t._indices();
  Tensor t_values = t._values();
  LongTensor src_indices = src._indices();
  Tensor s_values = src._values();
  LongTensor r_indices = t_indices.type().tensor({sparseDims, max_nnz});
  Tensor r_values = _new_values_with_size_of(t_values, max_nnz).zero_();
  r.resize_as_(src);
  _get_sparse_impl(r)->set_indices_and_values(r_indices, r_values);  // TODO: sigh

  int64_t match, d;
  int64_t r_i = 0, t_i = 0, s_i = 0;

  // NB: relies on nnz test above
  auto t_indices_accessor = t_indices.accessor<int64_t, 2>();
  auto r_indices_accessor = r_indices.accessor<int64_t, 2>();
  auto src_indices_accessor = src_indices.accessor<int64_t, 2>();

  // Check if we can find matching indices, and if so, write an
  // entry to the result indices vector.  Returns true if matching
  // indices were found.
  auto index_preamble = [&]() {
    match = 1;
    for (d = 0; d < sparseDims; d++) {
      if (t_indices_accessor[d][t_i] < src_indices_accessor[d][s_i]) {
        t_i++;
        match = 0;
        break;
      }
      if (t_indices_accessor[d][t_i] > src_indices_accessor[d][s_i]) {
        s_i++;
        match = 0;
        break;
      }
    }
    if (!match) return false;
    for (d = 0; d < sparseDims; d++) {
      r_indices_accessor[d][r_i] = t_indices_accessor[d][t_i];
    }
    return true;
  };

  if (t_values.dim() > 1) {
    while (t_i < t_nnz && s_i < s_nnz) {
      if (!index_preamble()) continue;
      r_values.select(0, r_i).addcmul_(t_values.select(0, t_i), s_values.select(0, s_i));
      r_i++;
      t_i++;
      s_i++;
    }
  } else {
    AT_DISPATCH_ALL_TYPES(
        r_values.type(), "mul_out_sparse", [&] {
          auto r_accessor = r_values.accessor<scalar_t, 1>();
          auto t_accessor = t_values.accessor<scalar_t, 1>();
          auto s_accessor = s_values.accessor<scalar_t, 1>();

          while (t_i < t_nnz && s_i < s_nnz) {
            if (!index_preamble()) continue;
            r_accessor[r_i] = t_accessor[t_i] * s_accessor[s_i];
            r_i++;
            t_i++;
            s_i++;
          }
        }
    );
  }

  _get_sparse_impl(r)->set_nnz(r_i);
  _get_sparse_impl(r)->set_coalesced(true);

  return r;
}

SparseTensor s_mul_sparse_cpu(const SparseTensor& t, const SparseTensor& src) {
  SparseTensor r = t.type().tensor();
  s_mul_out_sparse_cpu(r, t, src);
  return r;
}

SparseTensor& s_mul_sparse_cpu_(SparseTensor& t, const SparseTensor& src) {
  return s_mul_out_sparse_cpu(t, t, src);
}

// --------------------------------------------------------------------
// addmm(Tensor, SparseTensorRef, Tensor, Scalar, Scalar)  [broadcasts]
// --------------------------------------------------------------------

// NB: OMP pragmas have to get their own functions; can't put them in lambdas
template <typename scalar_t>
void s_addmm_out_sparse_dense_worker(int64_t nnz, int64_t dim_i, int64_t dim_j, int64_t dim_k, Tensor& r, Scalar beta, const Tensor& t, Scalar alpha, const Tensor& csr, const Tensor& indices, const Tensor& values, const Tensor& dense) {
  int64_t h, i;

  // r_ = alpha * sparse * dense
  scalar_t cast_alpha = alpha.to<scalar_t>();
  scalar_t cast_beta = beta.to<scalar_t>();
  if (cast_beta == 0) {
    r.zero_();
  } else if (cast_beta == 1) {
    if (!isSameTensor(r, t)) {
      r.copy_(t);
    }
  } else {
    at::mul_out(r, t, beta);
  }

  auto csr_accessor = csr.accessor<int64_t, 1>();
  auto indices_accessor = indices.accessor<int64_t, 2>();

  auto values_accessor = values.accessor<scalar_t, 1>();
  scalar_t* dense_ptr = dense.data<scalar_t>();
  scalar_t* r_ptr = r.data<scalar_t>();

  int64_t dense_stride0 = dense.stride(0);
  int64_t dense_stride1 = dense.stride(1);
  int64_t r_stride0 = r.stride(0);
  int64_t r_stride1 = r.stride(1);
#pragma omp parallel for private(h, i) schedule(static) if (nnz > 10000)
  for (h = 0; h < dim_i; h++) {
    int64_t i_start = csr_accessor[h];
    int64_t i_end = csr_accessor[h+1];
    for (i = i_start; i < i_end; i++) {
      scalar_t val = values_accessor[i];
      int64_t col = indices_accessor[1][i];
      if (col >= 0 && col < dim_j) {
        THBlas_axpy<scalar_t>(dim_k,
            cast_alpha * val,
            dense_ptr + col * dense_stride0, dense_stride1,
            r_ptr + h * r_stride0, r_stride1);
      } else {
        AT_ERROR("index out of bound. spmm: ", col, " not between 1 and ", dim_j);
      }
    }
  }
};

Tensor& s_addmm_out_sparse_dense_cpu(
    Tensor& r,
    const Tensor& t,
    const SparseTensor& sparse_,
    const Tensor& dense,
    Scalar beta,
    Scalar alpha
) {
  // TODO: This error message seems awfully opaque
  AT_CHECK(sparse_._sparseDims() == 2, "matrices expected, got ", sparse_._sparseDims(), "D tensor");
  AT_CHECK(sparse_._denseDims() == 0, "scalar values expected, got ", sparse_._denseDims(), "D values");
  AT_CHECK(dense.dim() == 2, "matrices expected, got ", dense.dim(), "D tensor");

  SparseTensor sparse = sparse_.coalesce();

  // ixj * jxk = ixk
  int64_t dim_i = sparse.size(0);
  int64_t dim_j = sparse.size(1);
  int64_t dim_k = dense.size(1);

  r.resize_({dim_i, dim_k});

  AT_CHECK(dense.size(0) == dim_j,
      "Argument #3 (dense): Expected dim 0 size ", dim_j, ", got ", dense.size(0));
  AT_CHECK(t.size(0) == dim_i,
      "Argument #1 (t): Expected dim 0 size ", dim_i, ", got ", t.size(0));
  AT_CHECK(t.size(1) == dim_k,
      "Argument #1 (t): Expected dim 1 size ", dim_k, ", got ", t.size(1));

  int64_t nnz        = sparse._nnz();

  if (nnz == 0) {
    at::mul_out(r, t, beta);
    return r;
  }

  LongTensor indices = sparse._indices();
  Tensor values      = sparse._values();
  LongTensor csr = _to_csr(indices.data<int64_t>(), dim_i, nnz);

  AT_DISPATCH_ALL_TYPES(
      values.type(), "addmm_sparse_dense", [&] {
        s_addmm_out_sparse_dense_worker<scalar_t>(nnz, dim_i, dim_j, dim_k, r, beta, t, alpha, csr, indices, values, dense);
      }
  );

  return r;

}

Tensor s_addmm_sparse_dense_cpu(
    const Tensor& t,
    const SparseTensor& sparse,
    const Tensor& dense,
    Scalar beta,
    Scalar alpha
) {
  Tensor r = t.type().tensor();
  s_addmm_out_sparse_dense_cpu(r, t, sparse, dense, beta, alpha);
  return r;
}

Tensor& s_addmm_sparse_dense_cpu_(
    Tensor& t,
    const SparseTensor& sparse,
    const Tensor& dense,
    Scalar beta,
    Scalar alpha
) {
  return s_addmm_out_sparse_dense_cpu(t, t, sparse, dense, beta, alpha);
}


// --------------------------------------------------------------------
// hspmm(SparseTensor mat1, Tensor mat2)
// --------------------------------------------------------------------

SparseTensor& hspmm_out_sparse_cpu(SparseTensor& r, const SparseTensor& sparse_, const Tensor& dense) {
  // TODO: Make this a real argument
  Scalar alpha = 1;
  AT_CHECK(sparse_._sparseDims() == 2,
      "Argument #2: matrices expected, got ", sparse_._sparseDims(), "D tensor");
  AT_CHECK(sparse_._denseDims() == 0,
      "Argument #2: scalar values expected, got ", sparse_._denseDims(), "D values");
  AT_CHECK(dense.dim() == 2,
      "Argument #2: matrices expected, got ", dense.dim(), "D tensor");

  int64_t m = sparse_.size(0);
  int64_t k = sparse_.size(1);
  int64_t n = dense.size(1);

  AT_CHECK(dense.size(0) == k,
      "Argument #3: Expected dim 0 size ", k, ", got ", dense.size(0));
  _get_sparse_impl(r)->raw_resize_(1, 1, {m, n});

  SparseTensor sparse = sparse_.coalesce();

  int64_t nnz = sparse._nnz();

  if (nnz == 0) {
    r.zero_();
    return r;
  }

  LongTensor indices = at::CPU(kLong).tensor({1, nnz});

  // Initialize the sparse matrix that will be used with spaddmm to send rows
  // from the dense matrix to rows of the output's value tensor
  SparseTensor newSparse = sparse.clone();
  LongTensor spIndices = newSparse._indices();
  LongTensor valueIndices = spIndices.select(0, 0);

  // Compute output indices
  auto valueIndices_accessor = valueIndices.accessor<int64_t, 1>();
  auto indices_accessor = indices.accessor<int64_t, 2>();

  int64_t i = -1, prevIdx = -1;
  for (int64_t j = 0; j < nnz; j++) {
    int64_t currIdx = valueIndices_accessor[j];
    if (currIdx != prevIdx) {
      indices_accessor[0][++i] = currIdx;
      prevIdx = currIdx;
    }
    valueIndices_accessor[j] = i;
  }
  int64_t outNnz = i + 1;
  indices.resize_({1, outNnz});
  Tensor values = dense.type().tensor({outNnz, n});
  _get_sparse_impl(newSparse)->_sizes_mut()[0] = outNnz; // TODO: use something safer

  // Compute output values tensor with sparse * dense multiplication
  s_addmm_out_sparse_dense_cpu(values, values, newSparse, dense, 0, alpha);
  _get_sparse_impl(r)->set_indices_and_values(indices, values);  // TODO: sigh

  return r;
}

SparseTensor hspmm_sparse_cpu(const SparseTensor& sparse, const Tensor& dense) {
  SparseTensor r = sparse.type().tensor();
  hspmm_out_sparse_cpu(r, sparse, dense);
  return r;
}

// --------------------------------------------------------------------
// sspaddmm
// --------------------------------------------------------------------

SparseTensor& _sspaddmm_out_cpu(
    SparseTensor& r,
    const SparseTensor& t,
    const SparseTensor& sparse_,
    const Tensor& dense,
    Scalar beta,
    Scalar alpha
) {
  AT_CHECK(sparse_._sparseDims() == 2,
      "Argument #2: matrices expected, got ", sparse_._sparseDims(), "D tensor");
  AT_CHECK(sparse_._denseDims() == 0,
      "Argument #2: scalar values expected, got ", sparse_._denseDims(), "D values");
  AT_CHECK(dense.dim() == 2,
      "Argument #2: matrices expected, got ", dense.dim(), "D tensor");

  SparseTensor sparse = sparse_.coalesce();

  // ixj * jxk = ixk
  int64_t dim_i = sparse.size(0);
  int64_t dim_j = sparse.size(1);
  int64_t dim_k = dense.size(1);

  r.sparse_raw_resize_({dim_i, dim_k}, 2, 0);

  AT_CHECK(dense.size(0) == dim_j,
      "Argument #3: Expected dim 0 size ", dim_j, ", got ", dense.size(0));
  AT_CHECK(t.size(0) == dim_i,
      "Argument #1: Expected dim 0 size ", dim_i, ", got ", t.size(0));
  AT_CHECK(t.size(1) == dim_k,
      "Argument #1: Expected dim 1 size ", dim_k, ", got ", t.size(1));

  int64_t nnz        = sparse._nnz();
  LongTensor indices = sparse._indices();
  Tensor values      = sparse._values();

  LongTensor csr = _to_csr(indices.data<int64_t>(), dim_i, nnz);

  int64_t t_nnz = t._nnz();
  int64_t r_nnz = nnz * dim_k + t_nnz;
  LongTensor newi = native::empty({2, r_nnz}, kLong);
  LongTensor newv = native::zeros({r_nnz}, values.options());

  if (t_nnz != 0) {
    LongTensor narrowi = newi.narrow(1, 0, t_nnz);
    Tensor narrowv = newv.narrow(0, 0, t_nnz);

    narrowi.copy_(t._indices());
    narrowv.copy_(t._values());
    newv.mul_(beta);
  }

  // sparse = sparse * dense
  int64_t p = t_nnz;

  auto csr_accessor = csr.accessor<int64_t, 1>();
  auto indices_accessor = indices.accessor<int64_t, 2>();
  auto newi_accessor = newi.accessor<int64_t, 2>();

  int64_t dense_stride0 = dense.stride(0);
  int64_t dense_stride1 = dense.stride(1);
  int64_t newv_stride0 = newv.stride(0);

  AT_DISPATCH_ALL_TYPES(
      values.type(), "sspmm", [&] {
        auto values_accessor = values.accessor<scalar_t, 1>();
        scalar_t* dense_ptr = dense.data<scalar_t>();
        scalar_t* newv_ptr = newv.data<scalar_t>();
        scalar_t cast_alpha = alpha.to<scalar_t>();

        for (int64_t h = 0; h < dim_i; h++) {
          int64_t i_start = csr_accessor[h];
          int64_t i_end = csr_accessor[h+1];
          for (int64_t i = i_start; i < i_end; i++) {
            scalar_t val = values_accessor[i];
            int64_t col = indices_accessor[1][i];
            if (col >= 0 && col < dim_j) {
              THBlas_axpy<scalar_t>(dim_k,
                  cast_alpha * val,
                  dense_ptr + col * dense_stride0, dense_stride1,
                  newv_ptr + p * newv_stride0, 1);
            } else {
              AT_ERROR("index out of bound. sspmm: ", col, " not between 1 and ", dim_j);
            }
          }
          // Fill up the indices with the right values
          if (i_start != i_end) {
            for (int64_t i = 0; i < dim_k; i++) {
              newi_accessor[0][p+i] = h;
              newi_accessor[1][p+i] = i;
            }
            p += dim_k;
          }
        }
      }
  );

  // to avoid a clone
  _get_sparse_impl(r)->set_indices(newi);
  _get_sparse_impl(r)->set_values(newv);
  _get_sparse_impl(r)->set_nnz(p);

  return r;
}

// sparse, sparse, sparse, dense, real, real -> sparse
Tensor& _sspaddmm_out_only_sparse(Tensor& result, const Tensor& self,
    const Tensor& mat1, const Tensor& mat2, Scalar beta, Scalar alpha) {
  AT_ERROR("tensor.sspaddmm(...) can only be called on sparse tensors");
}

// sparse, dense -> sparse
Tensor smm(const Tensor& self, const Tensor& mat2) {
  auto result = self.type().tensor();
  self.type().sspaddmm_out(result, result, self, mat2, 0.0, 1.0);
  return result;
}

// sparse, sparse, dense, real, real -> sparse
Tensor sspaddmm(const Tensor& self, const Tensor& mat1, const Tensor& mat2,
    Scalar beta, Scalar alpha) {
  auto result = self.type().tensor();
  self.type().sspaddmm_out(result, self, mat1, mat2, beta, alpha);
  return result;
}

}} // namespace at::native
