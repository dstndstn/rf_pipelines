import sys
import rf_pipelines

class frb_injector_transform(rf_pipelines.py_wi_transform):
    """
    This transform modifies the intensities in the pipeline by adding a simulated FRB
    (leaving weights unmodified).  For example, it can be used to add a simulated FRB
    to a "replayed" pathfinder acquisition which includes RFI.

    WARNING: the transform currently includes a 'snr' parameter which is a placeholder
    for the signal-to-noise ratio in "sigmas".  However, this is currently implemented 
    assuming that the noise variance is 1.0 in every channel!  This is a bad approximation,
    and so the true signal-to-noise ratio of the simulated pulse will be very different
    from the nominal snr specified in the constructor.  Now that variance estimation
    has been implemented in the pipeline, this can be fixed.
    """

    def __init__(self, snr, undispersed_arrival_time, dm, intrinsic_width=0.0, sm=0.0, spectral_index=0.0, sample_rms=1.0, nt_chunk=1024):
        """
        Constructor arguments
        ---------------------

           snr: Signal-to-noise ratio in "sigmas".  Currently a placeholder and not implemented correctly!
                (See note in the class docstring.)

           undispersed_arrival_time: Arrival time (in seconds) of the pulse without dispersion delay applied.
                This can be compared to other timestamps in the pipeline (e.g. the t0,t1 arguments to
                process_triggers().)

           dm: Dispersion measure in its usual units (pc cm^{-3})

           intrinsic_width: Width of the pulse in seconds, frequency-independent Gaussian profile assumed.
             Note that even a pulse with intrinsic_width=0 will have nonzero width in the channelized data, 
             due to dispersion smearing within a frequency channel.

           sm: The scattering measure, which we define to be the scattering timescale in milliseconds (not seconds!)
             at 1 GHz.  Note that 'scattering' refers to scatter-broadening of the pulse due to ISM turbulence, and
             is assumed to have an exponentially decaying profile whose timescale is proportional to frequency^(-4.4).
             (This can be compared to intrinsic_width, which has a Gaussian profile and no frequency dependence.)

           spectral_index: The spectral index beta is defined by the statement that the strength of the pulse
             is proportional to frequency^beta.

           sample_rms: The square root of the sample variance.  Currently this is assumed to be the same for
             all channels, and we have no good way of estimating its value.  This can be improved, now that
             variance estimation is implemented in the pipeline!
        """

        self.dm = dm
        self.sm = sm
        self.snr = snr
        self.spectral_index = spectral_index
        self.intrinsic_width = intrinsic_width
        self.undispersed_arrival_time = undispersed_arrival_time
        self.sample_rms = sample_rms

        self.name = 'frb_injector_transform'
        self.nfreq = 0     # to be initialized in set_stream()
        self.nt_chunk = nt_chunk
        self.nt_prepad = 0
        self.nt_postpad = 0


    def set_stream(self, stream):
        try:
            import simpulse
        except ImportError:
            raise RuntimeError("rf_pipelines: couldn't import the 'bonsai' module.  You may need to clone https://github.com/kmsmith137/simpulse and install.")

        self.nfreq = stream.nfreq
        self.freq_lo_MHz = stream.freq_lo_MHz
        self.freq_hi_MHz = stream.freq_hi_MHz
        self.dt_sample = stream.dt_sample

        # Construct a simpulse.single_pulse object, which represents a single FRB signal, to be added to the timestream in chunks.
        #
        # The mechanism implemented in simpulse for obtaining a pulse with specified signal-to-noise ratio is a little awkward!
        # First we construct the pulse with an arbitrary normalization, then we compute the signal-to-noise of the pulse given
        # its arbitrary normalization, then we adjust the normalization accordingly.

        self.pulse = simpulse.single_pulse(1024,       # number of samples used "under the hood" in the simpulse library
                                           self.nfreq,
                                           self.freq_lo_MHz,
                                           self.freq_hi_MHz,
                                           self.dm,
                                           self.sm,
                                           self.intrinsic_width,
                                           1.0,        # fluence, placeholder value to be changed below based on S/N
                                           self.spectral_index,
                                           self.undispersed_arrival_time)

        # Compute the signal-to-noise ratio of the pulse, for the arbitrary normalization specified in the constructor.
        #
        # This is the part that needs to be modified to include variance estimation!  If you look at the docstring,
        # you'll see that simpulse.single_pulse.get_signal_to_noise() has a 'sample_rms' argument and a 'channel_weights' 
        # argument.  (Note: the "rms" or "root-mean-square" is defined to be the square root of the variance.)
        #
        # Currently, we take sample_rms to be a scalar, which simpulse interprets as meaning that all frequency
        # channels have the same rms.  More generally, if sample_rms is a 1D numpy array of length nfreq, then
        # simpulse interprets it as a per-frequency rms.
        #
        # If the rms is frequency-dependent, then the weighting of the channels is also important when determining
        # the signal-to-noise.  This is specified via the 'channel_weights' argument, also an array of length nfreq,
        # to simpulse.single_pulse.get_signal_to_noise().  The best thing to use here would be a per-frequency estimate 
        # of the average value of the pipeline 'weights' array, but I propose just taking the weights to be 0.0 in
        # bad frequency channels (defined as channels with many v2 failures during variance estimation), and 1.0 in
        # non-bad channels.  Note that leaving channel_weights unspecified (or equivalently setting channel_weights=None)
        # defaults to 1/sigma_rms^2 weighting, which is inconsistent with what we're currently doing in the pipeline.
        #
        # (BTW the general issue of optimal frequency channel weighting is a nontrivial one that we'll want to revisit,
        # so the channel-weighting logic in the previous paragraph will probably be changed later!)
        
        snr0 = self.pulse.get_signal_to_noise(self.dt_sample, self.sample_rms)
        
        # The pulse has been simulated with nominal fluence 1, and its computed signal-to-noise is 'snr0'.
        # We want to end up with signal-to-noise 'snr'.  Therefore, the following fluence gives the signal-to-noise we want.
        self.pulse.fluence = self.snr / snr0

    
    def start_substream(self, isubstream, t0):
        # We keep track of the time range spanned by the stream, so that we can print a warning
        # in end_substream() if the substream doesn't span the pulse.
        self.substream_t0 = t0
        self.substream_t1 = t0


    def process_chunk(self, t0, t1, intensity, weights, pp_intensity, pp_weights):
        nt_chunk = intensity.shape[1]
        t1 = t0 + nt_chunk * self.dt_sample

        # The freq_hi_to_lo flag tells the 'simpulse' library to use the rf_pipelines
        # frequency ordering (i.e. frequencies ordered from high to low).
        self.pulse.add_to_timestream(intensity, t0, t1, freq_hi_to_lo=True)
        self.substream_t1 = t1


    def end_substream(self):
        # We print warning if the pulse isn't entirely contained in the substream,
        # since this is probably unintentional.
        (pulse_t0, pulse_t1) = self.pulse.get_endpoints()

        if (pulse_t0 >= self.substream_t0) and (pulse_t1 <= self.substream_t1):
            return

        intersection_t0 = max(pulse_t0, self.substream_t0)
        intersection_t1 = min(pulse_t1, self.substream_t1)
        intersection_dt = max(intersection_t1 - intersection_t0, 0)
        missing_frac = 1.0 - intersection_dt / (pulse_t1 - pulse_t0)

        print >>sys.stderr, ('frb_injector_transform: warning: %f percent of pulse was outside stream endpoints' % (100. * missing_frac))
