// Kernels defined here:
//
// _kernel_noniterative_wrms_2d(): computes weighted mean/rms of a 2D array with optional downsampling 
//    (there is also an option to write the downsampled intensity/weights to auxiliary arrays)
//
// _kernel_clip2d_iterate(): computes weighted mean/rms of a 2D array, including only elements
//    in a certain range.  (there is no downsampling option here)
//
// _kernel_intensity_mask_2d(): sets weights to zero when intensity is outside a certain range.
//    Optionally, the intensity array can be downsampled relative to the weights.
//
// _kernel_iterative_wrms_2d():
//   This is the "bottom line" routine which is wrapped by weighted_mean_and_rms().
//
// _kernel_clip_2d(): 
//   This is the "bottom line" kernel which is wrapped by the intensity_clipper(AXIS_NONE).


#ifndef _RF_PIPELINES_KERNELS_INTENSITY_CLIPPERS_HPP
#define _RF_PIPELINES_KERNELS_INTENSITY_CLIPPERS_HPP

#include "mean_variance.hpp"
#include "downsample.hpp"
#include "mask.hpp"

namespace rf_pipelines {
#if 0
}; // pacify emacs c-mode
#endif

template<typename T, unsigned int S> using simd_t = simd_helpers::simd_t<T,S>;


// -------------------------------------------------------------------------------------------------
//
// wrms kernels


// _kernel_noniterative_wrms_2d<T, S, Df, Dt, Iflag, Wflag, TwoPass>
//    (simd_t<T,S> &mean, simd_t<T,S> &rms, const T *intensity, const T *weights, 
//     int nfreq, int nt, int stride, T *ds_intensity, T *ds_weights)
//
// Computes the weighted mean and rms of a 2D strided array,
// with downsampling factors (Df,Dt) in the (frequency,time) axes.
//
// The 'mean' and 'rms' outputs are simd vectors whose elements are all equal.
// If the weighted mean and rms cannot be computed (e.g. because all weights are zero), then
// rms=0 and mean is arbitrary.  (This behavior is inherited from 'struct mean_rms_accumulator'.)
//
// As the downsampled intensity and weights arrays are computed, they are written to
// 'ds_intensity' and 'ds_weights'.  These are unstrided arrays, i.e. the row stride
// is (nt/Dt).
//
// The Iflag, Wflag template arguments will omit writing the ds_intensity, ds_weights
// arrays if set to 'false'.  In this case, passing a NULL pointer is OK.


template<typename T, unsigned int S, unsigned int Df, unsigned int Dt, bool Iflag, bool Wflag, bool TwoPass>
inline void _kernel_noniterative_wrms_2d(simd_t<T,S> &mean, simd_t<T,S> &rms, const T *intensity, const T *weights, int nfreq, int nt, int stride, T *ds_intensity, T *ds_weights)
{
    _kernel_mean_variance_2d<T,S,Df,Dt,Iflag,Wflag,TwoPass> (mean, rms, intensity, weights, nfreq, nt, stride, ds_intensity, ds_weights);
    rms = rms.sqrt();
}

template<typename T, unsigned int S, unsigned int Df, unsigned int Dt, bool Iflag, bool Wflag, bool TwoPass>
inline void _kernel_noniterative_wrms_1d_f(simd_t<T,S> &mean, simd_t<T,S> &rms, const T *intensity, const T *weights, int nfreq, int stride, T *ds_intensity, T *ds_weights)
{
    _kernel_mean_variance_1d_f<T,S,Df,Dt,Iflag,Wflag,TwoPass> (mean, rms, intensity, weights, nfreq, stride, ds_intensity, ds_weights);
    rms = rms.sqrt();
}

template<typename T, unsigned int S, unsigned int Df, unsigned int Dt, bool Iflag, bool Wflag, bool TwoPass>
inline void _kernel_noniterative_wrms_1d_t(simd_t<T,S> &mean, simd_t<T,S> &rms, const T *intensity, const T *weights, int nt, int stride, T *ds_intensity, T *ds_weights)
{
    _kernel_mean_variance_1d_t<T,S,Df,Dt,Iflag,Wflag,TwoPass> (mean, rms, intensity, weights, nt, stride, ds_intensity, ds_weights);
    rms = rms.sqrt();
}

// -------------------------------------------------------------------------------------------------
//
// iterate kernels - note no downsampling factors here
//
// Note: number of iterations is (niter-1)


template<typename T, unsigned int S, bool TwoPass>
inline void _kernel_wrms_iterate_2d(simd_t<T,S> &mean, simd_t<T,S> &rms, const T *intensity, const T *weights, int nfreq, int nt, int stride, int niter, double iter_sigma)
{
    for (int iter = 1; iter < niter; iter++) {
	simd_t<T,S> thresh = simd_t<T,S>(iter_sigma) * rms;
	_mean_variance_iterator<T,S,TwoPass> v(mean, thresh);
	_kernel_visit_2d<1,1> (v, intensity, weights, nfreq, nt, stride);
	v.get_mean_rms(mean, rms);
    }
}

// Placeholder for future expansion
template<typename T, unsigned int S, bool TwoPass>
inline void _kernel_wrms_iterate_1d_t(simd_t<T,S> &mean, simd_t<T,S> &rms, const T *intensity, const T *weights, int nt, int niter, double iter_sigma)
{
    _kernel_wrms_iterate_2d<T,S,TwoPass> (mean, rms, intensity, weights, 1, nt, 0, niter, iter_sigma);
}

template<typename T, unsigned int S, bool TwoPass>
inline void _kernel_wrms_iterate_1d_f(simd_t<T,S> &mean, simd_t<T,S> &rms, const T *intensity, const T *weights, int nfreq, int stride, int niter, double iter_sigma)
{
    for (int iter = 1; iter < niter; iter++) {
	simd_t<T,S> thresh = simd_t<T,S>(iter_sigma) * rms;
	_mean_variance_iterator<T,S,TwoPass> v(mean, thresh);
	_kernel_visit_1d_f<1,1> (v, intensity, weights, nfreq, stride);	
	v.get_mean_rms(mean, rms);
    }
}


// -------------------------------------------------------------------------------------------------
//
// masking kernels
//
// _kernel_intensity_mask_2d(): Masks all intensity samples which differ from the mean by more than 
// 'thresh'.  The intensity array can be downsampled relative to the weights array.


template<typename T, unsigned int S, unsigned int Df, unsigned int Dt>
inline void _kernel_intensity_mask_2d(T *weights, const T *ds_intensity, simd_t<T,S> mean, simd_t<T,S> thresh, int nfreq, int nt, int stride, int ds_stride)
{
    const T *ds_irow = ds_intensity;

    for (int ifreq = 0; ifreq < nfreq; ifreq += Df) {
	const T *ds_itmp = ds_irow;
	T *wrow = weights + ifreq * stride;

	for (int it = 0; it < nt; it += Dt*S) {
	    simd_t<T,S> ival = simd_t<T,S>::loadu(ds_itmp);
	    ds_itmp += S;

	    ival -= mean;
	    ival = ival.abs();

	    smask_t<T,S> valid = ival.compare_lt(thresh);
	    _kernel_mask<T,S,Df,Dt> (wrow + it, valid, stride);
	}

	ds_irow += ds_stride;
    }
}


template<typename T, unsigned int S, unsigned int Df, unsigned int Dt>
inline void _kernel_intensity_mask_1d_t(T *weights, const T *ds_intensity, simd_t<T,S> mean, simd_t<T,S> thresh, int nt, int stride, int ds_stride)
{
    _kernel_intensity_mask_2d<T,S,Df,Dt> (weights, ds_intensity, mean, thresh, Df, nt, stride, ds_stride);
}


template<typename T, unsigned int S, unsigned int Df, unsigned int Dt>
inline void _kernel_intensity_mask_1d_f(T *weights, const T *ds_intensity, simd_t<T,S> mean, simd_t<T,S> thresh, int nfreq, int stride, int ds_stride)
{
    for (int ifreq = 0; ifreq < nfreq; ifreq += Df) {
	simd_t<T,S> ival = simd_t<T,S>::loadu(ds_intensity);
	ival -= mean;
	ival = ival.abs();

	smask_t<T,S> valid = ival.compare_lt(thresh);
	_kernel_mask<T,S,Df,Dt> (weights + ifreq*stride, valid, stride);

	ds_intensity += ds_stride;
    }
}


// -------------------------------------------------------------------------------------------------
//
// _kernel_clip_2d(): 
//    This is the "bottom line" routine which is wrapped by intensity_clipper(AXIS_NONE).


// Downsampled version: ds_intensity must be non-NULL, ds_weights must be non-NULL if niter > 1.
template<typename T, unsigned int S, unsigned int Df, unsigned int Dt, bool TwoPass, typename std::enable_if<((Df>1) || (Dt>1)),int>::type = 0>
inline void _kernel_clip_2d(const T *intensity, T *weights, int nfreq, int nt, int stride, int niter, double sigma, double iter_sigma, T *ds_intensity, T *ds_weights)
{
    simd_t<T,S> mean, rms;

    if (niter == 1)
	_kernel_noniterative_wrms_2d<T,S,Df,Dt,true,false,TwoPass> (mean, rms, intensity, weights, nfreq, nt, stride, ds_intensity, ds_weights);
    else {
	_kernel_noniterative_wrms_2d<T,S,Df,Dt,true,true,TwoPass> (mean, rms, intensity, weights, nfreq, nt, stride, ds_intensity, ds_weights);
	_kernel_wrms_iterate_2d<T,S,TwoPass> (mean, rms, ds_intensity, ds_weights, nfreq/Df, nt/Dt, nt/Dt, niter, iter_sigma);
    }

    simd_t<T,S> thresh = simd_t<T,S>(sigma) * rms;
    _kernel_intensity_mask_2d<T,S,Df,Dt> (weights, ds_intensity, mean, thresh, nfreq, nt, stride, nt/Dt);
}

// Non-downsampled version: ds_intensity, ds_weights can be NULL
template<typename T, unsigned int S, unsigned int Df, unsigned int Dt, bool TwoPass, typename std::enable_if<((Df==1) && (Dt==1)),int>::type = 0>
inline void _kernel_clip_2d(const T *intensity, T *weights, int nfreq, int nt, int stride, int niter, double sigma, double iter_sigma, T *ds_intensity, T *ds_weights)
{
    simd_t<T,S> mean, rms;
    _kernel_noniterative_wrms_2d<T,S,1,1,false,false,TwoPass> (mean, rms, intensity, weights, nfreq, nt, stride, NULL, NULL);
    _kernel_wrms_iterate_2d<T,S,TwoPass> (mean, rms, intensity, weights, nfreq, nt, stride, niter, iter_sigma);

    simd_t<T,S> thresh = simd_t<T,S>(sigma) * rms;
    _kernel_intensity_mask_2d<T,S,Df,Dt> (weights, intensity, mean, thresh, nfreq, nt, stride, stride);
}


// -------------------------------------------------------------------------------------------------


template<typename T, unsigned int S, unsigned int Df, unsigned int Dt, bool TwoPass, typename std::enable_if<((Df>1) || (Dt>1)),int>::type = 0>
static void _kernel_clip_1d_t(const T *intensity, T *weights, int nfreq, int nt, int stride, int niter, double sigma, double iter_sigma, T *ds_int, T *ds_wt)
{
    simd_t<T,S> mean, rms;
    simd_t<T,S> s = sigma;

    if (niter > 1) {
	for (int ifreq = 0; ifreq < nfreq; ifreq += Df) {
	    _kernel_noniterative_wrms_1d_t<T,S,Df,Dt,true,true,TwoPass> (mean, rms, intensity + ifreq*stride, weights + ifreq*stride, nt, stride, ds_int, ds_wt);
	    _kernel_wrms_iterate_1d_t<T,S,TwoPass> (mean, rms, ds_int, ds_wt, nt/Dt, niter, iter_sigma);
	    _kernel_intensity_mask_1d_t<T,S,Df,Dt> (weights + ifreq*stride, ds_int, mean, s * rms, nt, stride, nt/Dt);
	}
    }
    else {
	for (int ifreq = 0; ifreq < nfreq; ifreq += Df) {
	    _kernel_noniterative_wrms_1d_t<T,S,Df,Dt,true,false,TwoPass> (mean, rms, intensity + ifreq*stride, weights + ifreq*stride, nt, stride, ds_int, ds_wt);
	    _kernel_intensity_mask_1d_t<T,S,Df,Dt> (weights + ifreq*stride, ds_int, mean, s * rms, nt, stride, nt/Dt);
	}
    }
}


template<typename T, unsigned int S, unsigned int Df, unsigned int Dt, bool TwoPass, typename std::enable_if<((Df==1) && (Dt==1)),int>::type = 0>
static void _kernel_clip_1d_t(const T *intensity, T *weights, int nfreq, int nt, int stride, int niter, double sigma, double iter_sigma, T *ds_int, T *ds_wt)
{
    simd_t<T,S> mean, rms;
    simd_t<T,S> s = sigma;

    for (int ifreq = 0; ifreq < nfreq; ifreq++) {
	const T *irow = intensity + ifreq * stride;
	T *wrow = weights + ifreq * stride;

	_kernel_noniterative_wrms_1d_t<T,S,1,1,false,false,TwoPass> (mean, rms, irow, wrow, nt, stride, NULL, NULL);
	_kernel_wrms_iterate_1d_t<T,S,TwoPass> (mean, rms, irow, wrow, nt, niter, iter_sigma);
	_kernel_intensity_mask_1d_t<T,S,1,1> (wrow, irow, mean, s * rms, nt, stride, stride);
    }
}


// -------------------------------------------------------------------------------------------------


template<typename T, unsigned int S, unsigned int Df, unsigned int Dt, bool TwoPass, typename std::enable_if<((Df > 1) || (Dt > 1)),int>::type = 0>
static void _kernel_clip_1d_f(const T *intensity, T *weights, int nfreq, int nt, int stride, int niter, double sigma, double iter_sigma, T *ds_int, T *ds_wt)
{
    simd_t<T,S> mean, rms;	
    simd_t<T,S> s = sigma;

    if (niter > 1) {
	for (int it = 0; it < nt; it += Dt*S) {
	    _kernel_noniterative_wrms_1d_f<T,S,Df,Dt,true,true,TwoPass> (mean, rms, intensity + it, weights + it, nfreq, stride, ds_int, ds_wt);
	    _kernel_wrms_iterate_1d_f<T,S,TwoPass> (mean, rms, ds_int, ds_wt, nfreq/Df, S, niter, iter_sigma);
	    _kernel_intensity_mask_1d_f<T,S,Df,Dt> (weights + it, ds_int, mean, s * rms, nfreq, stride, S);
	}
    }
    else {
	for (int it = 0; it < nt; it += Dt*S) {
	    _kernel_noniterative_wrms_1d_f<T,S,Df,Dt,true,false,TwoPass> (mean, rms, intensity + it, weights + it, nfreq, stride, ds_int, ds_wt);
	    _kernel_intensity_mask_1d_f<T,S,Df,Dt> (weights + it, ds_int, mean, s * rms, nfreq, stride, S);
	}
    }
}


template<typename T, unsigned int S, unsigned int Df, unsigned int Dt, bool TwoPass, typename std::enable_if<((Df == 1) && (Dt == 1)),int>::type = 0>
static void _kernel_clip_1d_f(const T *intensity, T *weights, int nfreq, int nt, int stride, int niter, double sigma, double iter_sigma, T *ds_int, T *ds_wt)
{
    simd_t<T,S> mean, rms;	
    simd_t<T,S> s = sigma;

    for (int it = 0; it < nt; it += S) {
	const T *icol = intensity + it;
	T *wcol = weights + it;
	
	_kernel_noniterative_wrms_1d_f<T,S,1,1,false,false,TwoPass> (mean, rms, icol, wcol, nfreq, stride, NULL, NULL);
	_kernel_wrms_iterate_1d_f<T,S,TwoPass> (mean, rms, icol, wcol, nfreq, stride, niter, iter_sigma);
	_kernel_intensity_mask_1d_f<T,S,1,1> (wcol, icol, mean, s * rms, nfreq, stride, stride);
    }
}


}  // namespace rf_pipelines

#endif
