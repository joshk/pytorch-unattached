#include <ATen/detail/CUDAHooksInterface.h>

#include <ATen/Generator.h>

// TODO: No need to have this whole header, we can just put it all in
// the cpp file

namespace at { namespace cuda { namespace detail {

// The real implementation of CUDAHooksInterface
struct CUDAHooks : public at::CUDAHooksInterface {
  CUDAHooks(at::CUDAHooksArgs) {}
  std::unique_ptr<THCState, void(*)(THCState*)> initCUDA() const override;
  std::unique_ptr<Generator> initCUDAGenerator(Context*) const override;
  bool hasCUDA() const override;
  bool hasCuDNN() const override;
  cudaStream_t getCurrentCUDAStream(THCState*) const override;
  cudaStream_t getCurrentCUDAStreamOnDevice(THCState*, int64_t device) const override;
  struct cudaDeviceProp* getCurrentDeviceProperties(THCState*) const override;
  struct cudaDeviceProp* getDeviceProperties(THCState*, int device) const override;
  int64_t current_device() const override;
  std::unique_ptr<Allocator> newPinnedMemoryAllocator() const override;
  void registerCUDATypes(Context*) const override;
  bool compiledWithCuDNN() const override;
  bool supportsDilatedConvolutionWithCuDNN() const override;
  long versionCuDNN() const override;
  double batchnormMinEpsilonCuDNN() const override;
  int64_t cuFFTGetPlanCacheMaxSize() const override;
  void cuFFTSetPlanCacheMaxSize(int64_t max_size) const override;
  int64_t cuFFTGetPlanCacheSize() const override;
  void cuFFTClearPlanCache() const override;
  int getNumGPUs() const override;
};

}}} // at::cuda::detail
