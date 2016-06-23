cimport cpython
cimport rf_pipelines_pxd


cdef class wi_transform:
    cdef rf_pipelines_pxd._wi_transform *p

    def __cinit__(self):
        self.p = NULL

    def __dealloc__(self):
        del self.p


cdef class wi_stream:
    cdef rf_pipelines_pxd._wi_stream *p

    def __cinit__(self):
        self.p = NULL

    def __dealloc__(self):
        del self.p

    def get_nfreq(self):
        return self.p.get_nfreq()

    def get_nt_maxwrite(self):
        return self.p.get_nt_maxwrite()

    def get_freq_lo_MHz(self):
        return self.p.get_freq_lo_MHz()

    def get_freq_hi_MHz(self):
        return self.p.get_freq_hi_MHz()

    def get_dt_sample(self):
        return self.p.get_dt_sample()

    def clear_transforms(self):
        self.p.clear_transforms()

    def add_transform(self, wi_transform t):
        self.p.add_transform(t.p)

    def run(self):
        self.p.run()


def make_psrfits_stream(filename):
    ret = wi_stream()
    ret.p = rf_pipelines_pxd._make_psrfits_stream(filename)
    return ret

def make_simple_detrender(nt_chunk):
    ret = wi_transform()
    ret.p = rf_pipelines_pxd._make_simple_detrender(nt_chunk)
    return ret

def make_upcalling_transform(pyobj):
    ret = wi_transform()
    ret.p = rf_pipelines_pxd._make_upcalling_transform(<cpython.PyObject *> pyobj)
    return ret