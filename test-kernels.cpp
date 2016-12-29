// Note: this file should only include kernel headers (kernels/*.hpp), not toplevel headers (./*.hpp)
// (This is because it has a Makefile dependency on the former but not the latter.)

#include <cassert>
#include "simd_helpers/simd_debug.hpp"
#include "kernels/polyfit.hpp"
#include "kernels/clip2d.hpp"

using namespace std;
using namespace rf_pipelines;


// -------------------------------------------------------------------------------------------------
//
// General-purpose helpers


// Cut-and-pasted from rf_pipelines_internals.hpp, since we don't want to #include it.
template<typename T>
inline T *aligned_alloc(size_t nelts)
{
    if (nelts == 0)
	return NULL;

    // align to 64-byte cache lines
    void *p = NULL;
    if (posix_memalign(&p, 64, nelts * sizeof(T)) != 0)
	throw std::runtime_error("couldn't allocate memory");

    memset(p, 0, nelts * sizeof(T));
    return reinterpret_cast<T *> (p);
}


// Generates a random number in the range [-2,2], but not too close to (+/- 1).
// This is useful when testing clippers, to avoid spurious roundoff-indiced 
// differences between reference code and fast code.
inline double clip_rand(std::mt19937 &rng)
{
    for (;;) {
	double t = std::uniform_real_distribution<>(-2.,2.)(rng);
	double u = fabs(fabs(t)-1.0);
	if (u > 1.0e-3)
	    return t;
    }
}


// Fills length-n 1D strided array with values of a randomly generated polynomial.
template<typename T>
inline void randpoly(T *dst, std::mt19937 &rng, int deg, int n, int stride)
{
    vector<T> coeffs = simd_helpers::gaussian_randvec<T> (rng, deg+1);

    for (int i = 0; i < n; i++) {
	T t = T(i) / T(n);
	T tp = 1.0;
	T y = 0.0;

	for (int p = 0; p <= deg; p++) {
	    y += coeffs[p] * tp;
	    tp *= t;
	}

	dst[i*stride] = y;
    }
}


// -------------------------------------------------------------------------------------------------


struct random_chunk {
    const int nfreq;
    const int nt;
    const int stride;
    
    float *intensity = nullptr;
    float *weights = nullptr;

    random_chunk(std::mt19937 &rng, int nfreq, int nt, int stride);
    random_chunk(std::mt19937 &rng, int nfreq, int nt);
    ~random_chunk();
    
    // noncopyable
    random_chunk(const random_chunk &) = delete;
    random_chunk &operator=(const random_chunk &) = delete;
};


random_chunk::random_chunk(std::mt19937 &rng, int nfreq_, int nt_, int stride_) :
    nfreq(nfreq_), nt(nt_), stride(stride_)
{
    assert(nfreq > 0);
    assert(nt > 0);
    assert(stride >= nt);

    intensity = aligned_alloc<float> (nfreq * stride);
    weights = aligned_alloc<float> (nfreq * stride);

    std::normal_distribution<> gdist;

    for (int i = 0; i < nfreq * stride; i++) {
	intensity[i] = gdist(rng) + 1.0;
	weights[i] = std::uniform_real_distribution<>()(rng);
    }
}


random_chunk::random_chunk(std::mt19937 &rng, int nfreq_, int nt_) :
    random_chunk(rng, nfreq_, nt_, nt_ + std::uniform_int_distribution<>(0,4)(rng))
{ }


random_chunk::~random_chunk()
{
    free(intensity);
    free(weights);
    intensity = weights = nullptr;
}


// -------------------------------------------------------------------------------------------------
//
// Test _kernel_legpoly_eval().


template<typename T>
static vector<T> reference_legpoly_eval(int npl, const vector<T> &zvec)
{
    assert(npl > 0);
    assert(zvec.size() > 0);

    int nz = zvec.size();
    vector<T> out_pl(npl * nz);

    for (int iz = 0; iz < nz; iz++)
	out_pl[iz] = 1.0;

    if (npl <= 1)
	return out_pl;

    for (int iz = 0; iz < nz; iz++)
	out_pl[nz+iz] = zvec[iz];

    for (int l = 2; l < npl; l++) {
	T a = (2*l-1) / T(l);
	T b = -(l-1) / T(l);
	
	for (int iz = 0; iz < nz; iz++)
	    out_pl[l*nz + iz] = a * zvec[iz] * out_pl[(l-1)*nz + iz] + b * out_pl[(l-2)*nz + iz];
    }

    return out_pl;
}


template<typename T, unsigned int S, unsigned int N>
static void test_legpoly_eval(std::mt19937 &rng)
{
    simd_t<T,S> z = simd_helpers::uniform_random_simd_t<T,S> (rng, -1.0, 1.0);

    simd_ntuple<T,S,N> pl;
    _kernel_legpoly_eval(pl, z);

    vector<T> pl0 = reference_legpoly_eval(N, vectorize(z));

#if 0
    for (int iz = 0; iz < S; iz++) {
	cout << z0[iz] << ": ";
	for (int l = 0; l < N; l++)
	    cout << " " << pl0[l*S+iz];
	cout << "\n";
    }
#endif

    T epsilon = simd_helpers::compare(vectorize(pl), pl0);
    assert(epsilon < 1.0e-5);
}


// -------------------------------------------------------------------------------------------------
//
// Test _kernel_detrend_t_pass1()


template<typename T>
static void reference_detrend_t_pass1(T *outm, T *outv, int npl, int nt, const T *ivec, const T *wvec)
{
    vector<T> tmp_z(nt);
    for (int it = 0; it < nt; it++)
	tmp_z[it] = 2 * (it+0.5) / T(nt) - 1;

    vector<T> tmp_pl = reference_legpoly_eval(npl, tmp_z);

    vector<T> tmp_wp(npl * nt);
    for (int l = 0; l < npl; l++)
	for (int it = 0; it < nt; it++)
	    tmp_wp[l*nt+it] = wvec[it] * tmp_pl[l*nt+it];

    for (int l = 0; l < npl; l++) {
	for (int l2 = 0; l2 <= l; l2++) {
	    T t = 0.0;
	    for (int it = 0; it < nt; it++)
		t += tmp_wp[l*nt+it] * tmp_pl[l2*nt+it];

	    outm[(l*(l+1))/2 + l2] = t;
	}

	T t = 0.0;
	for (int it = 0; it < nt; it++)
	    t += tmp_wp[l*nt+it] * ivec[it];

	outv[l] = t;
    }
}


template<typename T, unsigned int S, unsigned int N>
void test_detrend_t_pass1(std::mt19937 &rng, int nt)
{
    constexpr int NN = (N*(N+1))/2;

    vector<T> ivec = simd_helpers::uniform_randvec<T> (rng, nt, 0.0, 1.0);
    vector<T> wvec = simd_helpers::uniform_randvec<T> (rng, nt, 0.0, 1.0);

    simd_trimatrix<T,S,N> outm;
    simd_ntuple<T,S,N> outv;

    _kernel_detrend_t_pass1(outm, outv, nt, &ivec[0], &wvec[0]);

    vector<T> outm0(NN);
    vector<T> outv0(N);

    reference_detrend_t_pass1(&outm0[0], &outv0[0], N, nt, &ivec[0], &wvec[0]);

    simd_trimatrix<T,S,N> outm1;
    simd_ntuple<T,S,N> outv1;

    outm1.set1_slow(&outm0[0]);
    outv1.set1_slow(&outv0[0]);

    T epsilon_m = simd_helpers::compare(vectorize(outm), vectorize(outm1));
    T epsilon_v = simd_helpers::compare(vectorize(outv), vectorize(outv1));
    
    assert(epsilon_m < 1.0e-5);    
    assert(epsilon_v < 1.0e-5);    
}



// -------------------------------------------------------------------------------------------------
//
// Test _kernel_detrend_t_pass2()


template<typename T>
static void reference_detrend_t_pass2(T *ivec, int npl, int nt, const T *coeffs)
{
    vector<T> tmp_z(nt);
    for (int it = 0; it < nt; it++)
	tmp_z[it] = 2 * (it+0.5) / T(nt) - 1;

    vector<T> tmp_pl = reference_legpoly_eval(npl, tmp_z);

    for (int l = 0; l < npl; l++)
	for (int it = 0; it < nt; it++)
	    ivec[it] -= coeffs[l] * tmp_pl[l*nt + it];
}


template<typename T, unsigned int S, unsigned int N>
static void test_detrend_t_pass2(std::mt19937 &rng, int nt)
{
    vector<T> coeffs0 = simd_helpers::gaussian_randvec<T> (rng, N);
    vector<T> ivec = simd_helpers::gaussian_randvec<T> (rng, nt);
    vector<T> ivec2 = ivec;

    simd_ntuple<T,S,N> coeffs;
    coeffs.set1_slow(&coeffs0[0]);
    
    _kernel_detrend_t_pass2<T,S,N> (&ivec[0], nt, coeffs);
    reference_detrend_t_pass2(&ivec2[0], N, nt, &coeffs0[0]);

    T epsilon = simd_helpers::compare(ivec, ivec2);
    assert(epsilon < 1.0e-5);
}


// -------------------------------------------------------------------------------------------------
//
// Some general tests on kernel_detrend_t: 
//   "nulling": detrending a polynmomial should give zero,
//   "idempotency": detrending twice should be the same as detrending once


template<typename T, unsigned int S, unsigned int N>
void test_detrend_t_nulling(std::mt19937 &rng, int nfreq, int nt, int stride)
{
    vector<T> intensity(nfreq * stride, 0.0);
    vector<T> weights = simd_helpers::uniform_randvec<T> (rng, nfreq * stride, 0.0, 1.0);

    for (int ifreq = 0; ifreq < nfreq; ifreq++)
	randpoly(&intensity[ifreq*stride], rng, N-1, nt, 1);

    _kernel_detrend_t<T,S,N> (nfreq, nt, &intensity[0], &weights[0], stride);

    double epsilon = simd_helpers::maxabs(intensity);
    assert(epsilon < 1.0e-5);
}


template<typename T, unsigned int S, unsigned int N>
void test_detrend_t_idempotency(std::mt19937 &rng, int nfreq, int nt, int stride)
{
    vector<T> intensity = simd_helpers::uniform_randvec<T> (rng, nfreq * stride, 0.0, 1.0);
    vector<T> weights = simd_helpers::uniform_randvec<T> (rng, nfreq * stride, 0.0, 1.0);
    
    _kernel_detrend_t<T,S,N> (nfreq, nt, &intensity[0], &weights[0], stride);
    vector<T> intensity2 = intensity;
    _kernel_detrend_t<T,S,N> (nfreq, nt, &intensity2[0], &weights[0], stride);
    
    double epsilon = simd_helpers::maxdiff(intensity, intensity2);
    assert(epsilon < 1.0e-5);
}


// -------------------------------------------------------------------------------------------------


template<typename T>
static void reference_clip2d_wrms(T &mean, T &rms, const T *intensity, const T *weights, int nfreq, int nt, int stride, int nds_f, int nds_t, T *ds_int, T *ds_wt)
{
    assert(nfreq % nds_f == 0);
    assert(nt % nds_t == 0);

    // double-precision here
    double acc0 = 0.0;
    double acc1 = 0.0;
    double acc2 = 0.0;
    
    for (int ifreq = 0; ifreq < nfreq; ifreq += nds_f) {
	for (int it = 0; it < nt; it += nds_t) {
	    T wival = 0.0;
	    T wval = 0.0;

	    for (int jfreq = ifreq; jfreq < ifreq + nds_f; jfreq++) {
		for (int jt = it; jt < it + nds_t; jt++) {
		    int s = jfreq*stride + jt;
		    wival += weights[s] * intensity[s];
		    wval += weights[s];
		}
	    }

	    acc0 += double(wval);
	    acc1 += double(wival);
	    acc2 += double(wival) * double(wival) / double(wval);

	    *(ds_int++) = wival / wval;
	    *(ds_wt++) = wval;
	}
    }

    // FIXME case of invalid entries not tested
    mean = acc1/acc0;
    rms = sqrt(acc2/acc0 - mean*mean);
}


template<typename T, unsigned int S>
void test_clip2d_wrms_postmortem(int Df, int Dt, int nfreq, int nt, int stride, T ref_mean, 
				 T ref_rms, T *ref_ds_int, T *ref_ds_wt, simd_t<T,S> mean, 
				 simd_t<T,S> rms, T *ds_int, T *ds_wt)
{
    stringstream ss;
    ss << "test_clip2d_wrms failed:"
       << " T=" << simd_helpers::type_name<T>()
       << ", S=" << S << ", Df=" << Df << ", Dt=" << Dt 
       << ", nfreq=" << nfreq << ", nt=" << nt << ", stride=" << stride;

    vector<float> delta1 = vectorize(mean - simd_t<T,S> (ref_mean));
    vector<float> delta2 = vectorize(rms - simd_t<T,S> (ref_rms));    
    int n = (nfreq * nt) / (Df * Dt);

    if ((simd_helpers::maxabs(delta1) > 1.0e-3 * Df*Dt) || (simd_helpers::maxabs(delta2) > 1.0e-3 * sqrt(Df*Dt))) {
	cerr << ss.str() << "\n"
	     << "  mean: " << ref_mean << ", " << mean << "\n"
	     << "  rms: " << ref_rms << ", " << rms << "\n";
	exit(1);
    }

    if (ds_int && (simd_helpers::maxdiff(n,ref_ds_int,ds_int) > 1.0e-3 * Df*Dt)) {
	cerr << ss.str() << ": ds_int arrays differ\n";
	exit(1);
    }

    if (ds_wt && (simd_helpers::maxdiff(n,ref_ds_wt,ds_wt) > 1.0e-3 * Df*Dt)) {
	cerr << ss.str() << ": ds_wt arrays differ\n";
	exit(1);
    }
}


template<typename T, unsigned int S, unsigned int Df, unsigned int Dt>
static void test_clip2d_wrms(std::mt19937 &rng, int nfreq, int nt, int stride)
{
    assert(nfreq % Df == 0);
    assert(nt % (Dt*S) == 0);
    assert(stride >= nt);

    random_chunk rc(rng, nfreq, nt, stride);

    float ref_mean, ref_rms;
    vector<T> ref_ds_int((nfreq/Df) * (nt/Dt), -1.0);
    vector<T> ref_ds_wt((nfreq/Df) * (nt/Dt), -1.0);

    reference_clip2d_wrms(ref_mean, ref_rms, rc.intensity, rc.weights, nfreq, nt, rc.stride, Df, Dt, &ref_ds_int[0], &ref_ds_wt[0]);
    
    simd_t<T,S> mean, rms;
    vector<T> ds_int((nfreq/Df) * (nt/Dt), -1.0);
    vector<T> ds_wt((nfreq/Df) * (nt/Dt), -1.0);
    
    _kernel_clip2d_wrms<T,S,Df,Dt,false,false> (mean, rms, rc.intensity, rc.weights, nfreq, nt, rc.stride, nullptr, nullptr);
    test_clip2d_wrms_postmortem(Df, Dt, nfreq, nt, stride, ref_mean, ref_rms, &ref_ds_int[0], &ref_ds_wt[0], mean, rms, (T *)nullptr, (T *)nullptr);

    _kernel_clip2d_wrms<T,S,Df,Dt,false,true> (mean, rms, rc.intensity, rc.weights, nfreq, nt, rc.stride, nullptr, &ds_wt[0]);
    test_clip2d_wrms_postmortem(Df, Dt, nfreq, nt, stride, ref_mean, ref_rms, &ref_ds_int[0], &ref_ds_wt[0], mean, rms, (T *)nullptr, &ds_wt[0]);

    _kernel_clip2d_wrms<T,S,Df,Dt,true,false> (mean, rms, rc.intensity, rc.weights, nfreq, nt, rc.stride, &ds_int[0], nullptr);
    test_clip2d_wrms_postmortem(Df, Dt, nfreq, nt, stride, ref_mean, ref_rms, &ref_ds_int[0], &ref_ds_wt[0], mean, rms, &ds_int[0], (T *)nullptr);

    _kernel_clip2d_wrms<T,S,Df,Dt,true,true> (mean, rms, rc.intensity, rc.weights, nfreq, nt, rc.stride, &ds_int[0], &ds_wt[0]);
    test_clip2d_wrms_postmortem(Df, Dt, nfreq, nt, stride, ref_mean, ref_rms, &ref_ds_int[0], &ref_ds_wt[0], mean, rms, &ds_int[0], &ds_wt[0]);
}


template<typename T, unsigned int S, unsigned int Df, unsigned int Dt>
static void test_clip2d_wrms(std::mt19937 &rng)
{
    int nfreq = Df * std::uniform_int_distribution<>(10,20)(rng);
    int nt = Dt * S * std::uniform_int_distribution<>(10,20)(rng);
    int stride = nt + std::uniform_int_distribution<>(0,4)(rng);

    test_clip2d_wrms<T,S,Df,Dt> (rng, nfreq, nt, stride);
}


// Fixed Df, many Dt
template<typename T, unsigned int S, unsigned int Df, unsigned int MaxDt, typename std::enable_if<(MaxDt==1),int>::type = 0>
static void test_clip2d_wrms_varying_dt(std::mt19937 &rng)
{
    test_clip2d_wrms<T,S,Df,1> (rng);
}

// Fixed Df, many Dt
template<typename T, unsigned int S, unsigned int Df, unsigned int MaxDt, typename std::enable_if<(MaxDt>1),int>::type = 0>
static void test_clip2d_wrms_varying_dt(std::mt19937 &rng)
{
    test_clip2d_wrms_varying_dt<T,S,Df,MaxDt/2> (rng);
    test_clip2d_wrms<T,S,Df,MaxDt> (rng);
}

// Many Df, many Dt
template<typename T, unsigned int S, unsigned int MaxDf, unsigned int MaxDt, typename std::enable_if<(MaxDf==1),int>::type = 0>
static void test_clip2d_wrms_all(std::mt19937 &rng)
{
    test_clip2d_wrms_varying_dt<T,S,1,MaxDt> (rng);
}

// Many Df, many Dt
template<typename T, unsigned int S, unsigned int MaxDf, unsigned int MaxDt, typename std::enable_if<(MaxDf>1),int>::type = 0>
static void test_clip2d_wrms_all(std::mt19937 &rng)
{
    test_clip2d_wrms_all<T,S,MaxDf/2,MaxDt> (rng);
    test_clip2d_wrms_varying_dt<T,S,MaxDf,MaxDt> (rng);
}


// -------------------------------------------------------------------------------------------------


template<typename T>
static void reference_clip2d_mask(T *weights, const T *ds_intensity, T mean, T thresh, int nfreq, int nt, int stride, int Df, int Dt, int ds_stride)
{
    assert(nfreq % Df == 0);
    assert(nt % Dt == 0);

    int nfreq_ds = nfreq / Df;
    int nt_ds = nt / Dt;

    for (int ifreq_ds = 0; ifreq_ds < nfreq_ds; ifreq_ds++) {
	for (int it_ds = 0; it_ds < nt_ds; it_ds++) {
	    T ival = ds_intensity[ifreq_ds*ds_stride + it_ds];

	    if (fabs(ival-mean) < thresh)
		continue;

	    for (int ifreq = ifreq_ds*Df; ifreq < (ifreq_ds+1)*Df; ifreq++)
		for (int it = it_ds*Dt; it < (it_ds+1)*Dt; it++)
		    weights[ifreq*stride+it] = 0.0;
	}
    }
}


template<typename T, unsigned int S, unsigned int Df, unsigned int Dt>
static void test_clip2d_mask(std::mt19937 &rng, int nfreq, int nt, int stride, int ds_stride)
{
    assert(nfreq % Df == 0);
    assert(nt % Dt == 0);
    assert(stride >= nt);
    assert(ds_stride >= (nt/Dt));

    int nfreq_ds = nfreq / Df;
    int nt_ds = nt / Dt;

    T mean = std::uniform_real_distribution<>()(rng);
    T thresh = std::uniform_real_distribution<>()(rng);

    vector<T> ds_intensity(nfreq_ds * ds_stride, 0.0);
    vector<T> weights = simd_helpers::uniform_randvec<T> (rng, nfreq * stride, 0.0, 1.0);
    vector<T> weights2 = weights;

    for (int ifreq_ds = 0; ifreq_ds < nfreq_ds; ifreq_ds++)
	for (int it_ds = 0; it_ds < nt_ds; it_ds++)
	    ds_intensity[ifreq_ds*ds_stride + it_ds] = mean + thresh * clip_rand(rng);

    reference_clip2d_mask(&weights[0], &ds_intensity[0], mean, thresh, nfreq, nt, stride, Df, Dt, ds_stride);
    _kernel_clip2d_mask<T,S,Df,Dt> (&weights2[0], &ds_intensity[0], simd_t<T,S>(mean), simd_t<T,S>(thresh), nfreq, nt, stride, ds_stride);

    for (int ifreq = 0; ifreq < nfreq; ifreq++) {
	for (int it = 0; it < nt; it++) {
	    if (weights[ifreq*stride+it] == weights2[ifreq*stride+it])
		continue;

	    int ifreq_ds = ifreq / Df;
	    int it_ds = it / Dt;

	    cerr << "test_clip2d_mask failed:"
		 << " T=" << simd_helpers::type_name<T>() << ", S=" << S << ", Df=" << Df << ", Dt=" << Dt 
		 << ", nfreq=" << nfreq << ", nt=" << nt << ", stride=" << stride << ", ds_stride=" << ds_stride << "\n"
		 << "   at (ifreq,it)=(" << ifreq << "," << it << "): "
		 << " wt_ref=" << weights[ifreq*stride+it] 
		 << ", wt_fast=" << weights2[ifreq*stride+it] << "\n"
		 << "   mean=" << mean << ", thresh=" << thresh 
		 << ", ds_int=" << ds_intensity[ifreq_ds*ds_stride + it_ds] << "\n";

	    exit(1);
	}
    }
}


template<typename T, unsigned int S, unsigned int Df, unsigned int Dt>
static void test_clip2d_mask(std::mt19937 &rng)
{
    int nfreq = Df * std::uniform_int_distribution<>(10,20)(rng);
    int nt = Dt * S * std::uniform_int_distribution<>(10,20)(rng);
    int stride = nt + std::uniform_int_distribution<>(0,4)(rng);
    int ds_stride = (nt/Dt) + std::uniform_int_distribution<>(0,4)(rng);

    test_clip2d_mask<T,S,Df,Dt> (rng, nfreq, nt, stride, ds_stride);
}


// Fixed Df, many Dt
template<typename T, unsigned int S, unsigned int Df, unsigned int MaxDt, typename std::enable_if<(MaxDt==1),int>::type = 0>
static void test_clip2d_mask_varying_dt(std::mt19937 &rng)
{
    test_clip2d_mask<T,S,Df,1> (rng);
}

// Fixed Df, many Dt
template<typename T, unsigned int S, unsigned int Df, unsigned int MaxDt, typename std::enable_if<(MaxDt>1),int>::type = 0>
static void test_clip2d_mask_varying_dt(std::mt19937 &rng)
{
    test_clip2d_mask_varying_dt<T,S,Df,MaxDt/2> (rng);
    test_clip2d_mask<T,S,Df,MaxDt> (rng);
}

// Many Df, many Dt
template<typename T, unsigned int S, unsigned int MaxDf, unsigned int MaxDt, typename std::enable_if<(MaxDf==1),int>::type = 0>
static void test_clip2d_mask_all(std::mt19937 &rng)
{
    test_clip2d_mask_varying_dt<T,S,1,MaxDt> (rng);
}

// Many Df, many Dt
template<typename T, unsigned int S, unsigned int MaxDf, unsigned int MaxDt, typename std::enable_if<(MaxDf>1),int>::type = 0>
static void test_clip2d_mask_all(std::mt19937 &rng)
{
    test_clip2d_mask_all<T,S,MaxDf/2,MaxDt> (rng);
    test_clip2d_mask_varying_dt<T,S,MaxDf,MaxDt> (rng);
}


// -------------------------------------------------------------------------------------------------


template<typename T>
void reference_clip2d_iterate(T &out_mean, T &out_rms, const T *intensity, const T *weights, T in_mean, T in_thresh, int nfreq, int nt, int stride)
{
    // double-precision here
    double acc0 = 0.0;
    double acc1 = 0.0;
    double acc2 = 0.0;

    for (int ifreq = 0; ifreq < nfreq; ifreq++) {
	for (int it = 0; it < nt; it++) {
	    T ival = intensity[ifreq*stride + it];
	    T wval = weights[ifreq*stride + it];
	    
	    if (fabs(ival - in_mean) >= in_thresh)
		continue;

	    acc0 += double(wval);
	    acc1 += double(wval) * double(ival);
	    acc2 += double(wval) * double(ival) * double(ival);
	}
    }

    // FIXME case of invalid entries not tested
    out_mean = acc1/acc0;
    out_rms = sqrt(acc2/acc0 - out_mean*out_mean);
}


template<typename T, unsigned int S>
static void test_clip2d_iterate(std::mt19937 &rng, int nfreq, int nt, int stride)
{
    random_chunk rc(rng, nfreq, nt, stride);

    T in_mean = std::uniform_real_distribution<>()(rng);
    T in_thresh = std::uniform_real_distribution<>(1.0, 2.0)(rng);

    for (int ifreq = 0; ifreq < nfreq; ifreq++)
	for (int it = 0; it < nt; it++)
	    rc.intensity[ifreq*stride + it] = in_mean + in_thresh * clip_rand(rng);
    
    T ref_mean, ref_rms;
    reference_clip2d_iterate(ref_mean, ref_rms, rc.intensity, rc.weights, in_mean, in_thresh, nfreq, nt, stride);

    simd_t<T,S> fast_mean, fast_rms;
    _kernel_clip2d_iterate(fast_mean, fast_rms, rc.intensity, rc.weights, simd_t<T,S> (in_mean), simd_t<T,S> (in_thresh), nfreq, nt, stride);

    vector<float> delta1 = vectorize(fast_mean - simd_t<T,S> (ref_mean));
    vector<float> delta2 = vectorize(fast_rms - simd_t<T,S> (ref_rms));    

    if ((simd_helpers::maxabs(delta1) > 1.0e-3) || (simd_helpers::maxabs(delta2) > 1.0e-3)) {
	cerr << "test_clip2d_iterate failed:"
	     << " T=" << simd_helpers::type_name<T>() << ", S=" << S
	     << ", nfreq=" << nfreq << ", nt=" << nt << ", stride=" << stride << "\n"
	     << "  mean: " << ref_mean << ", " << fast_mean << "\n"
	     << "  rms: " << ref_rms << ", " << fast_rms << "\n";

	exit(1);
    }
}


template<typename T, unsigned int S>
static void test_clip2d_iterate_all(std::mt19937 &rng)
{
    for (int iter = 0; iter < 100; iter++) {
	int nfreq = std::uniform_int_distribution<>(10,20)(rng);
	int nt = S * std::uniform_int_distribution<>(10,20)(rng);
	int stride = nt + std::uniform_int_distribution<>(0,4)(rng);

	test_clip2d_iterate<T,S> (rng, nfreq, nt, stride);
    }
}


// -------------------------------------------------------------------------------------------------


template<typename T, unsigned int S, unsigned int Nmax, typename std::enable_if<(Nmax==0),int>::type = 0>
static void test_polynomial_detrenders(std::mt19937 &rng)
{
    return;
}


template<typename T, unsigned int S, unsigned int Nmax, typename std::enable_if<(Nmax>0),int>::type = 0>
static void test_polynomial_detrenders(std::mt19937 &rng)
{
    test_polynomial_detrenders<T,S,(Nmax-1)> (rng);

    for (int iter = 0; iter < 10; iter++) {
	int nfreq = std::uniform_int_distribution<>(30,100)(rng);
	int nt = S * std::uniform_int_distribution<>(10,100)(rng);
	int stride = nt + S * std::uniform_int_distribution<>(0,4)(rng);

	test_legpoly_eval<T,S,Nmax> (rng);

	test_detrend_t_pass1<T,S,Nmax> (rng, nt);
	test_detrend_t_pass2<T,S,Nmax> (rng, nt);
	test_detrend_t_nulling<T,S,Nmax> (rng, nfreq, nt, stride);
	test_detrend_t_idempotency<T,S,Nmax> (rng, nfreq, nt, stride);
    }
}


int main(int argc, char **argv)
{
    std::random_device rd;
    std::mt19937 rng(rd());

    test_polynomial_detrenders<float,8,16> (rng);
    test_clip2d_wrms_all<float,8,32,32> (rng);
    test_clip2d_mask_all<float,8,32,32> (rng);
    test_clip2d_iterate_all<float,8> (rng);

    cout << "test-kernels: all tests passed\n";
    return 0;
}
