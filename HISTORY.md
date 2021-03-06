- Version 12:

     - Backwards-incompatible: the boolean 'noisy' argument to stream.run() has been replaced
       with an integer-valued 'verbosity' argument (see docstring or comments for details).

     - From Maya: new python bonsai_dedisperser which makes zoomable plots for the web viewer.

     - From Masoud: remove 'imitate_cpp=False' option from python RFI transforms, now that
       C++ transform logic has been validated

     - Minor internal changes reflecting API changes in bonsai v7.

- Version 11:

     - Backwards-incompatible: The 'simd_helpers' library (https://github.com/kmsmith137/simd_helpers)
       is now a prerequisite.

     - Backwards-incompatible: names of three core transforms have been changed
         - legendre_detrender -> polynomial_detrender
         - clipper_transform -> intensity_clipper
         - std_dev_filter -> std_dev_clipper

     - Assembly language kernels!  The three core transforms above now have fast implementations, 
       written in C++ with assembly language kernel "cores".  The CHIME RFI removal is now faster
       by a factor 30!

     - From Masoud: lots of tweaks to RFI removal scheme.
       Most of the CHIME RFI code has now been moved to a new repository 'ch_frb_rfi':
           https://github.com/mrafieir/ch_frb_rfi

     - From Maya: plotter_transform now has zoom levels, for use in zoomable web viewer.

- Version 10:

     - Network streams:
         - New stream class chime_network_stream which receives packetized intensity data
	 - New transform chime_packetizer which sends packetized data
	 - New example (examples/example5_network) to illustrate how these are used

     - Example scripts (examples/example5_network) illustrating python interface to
       chimefrb networking code.

     - Internal API changes which should only affect you if you're writing new streams
       (I don't think anyone is currently doing this, but let me know if you'd like more details)

- Version 9:

     - From Masoud: New transform mask_expander (masks out weights, relative to a threshold
       value in 2D, in 1D along the time axis, or in 1D along the frequency axis)

- Version 8:

     - Backwards-incompatible: jsoncpp library now required (see README.md for installation instructions)

     - Backwards-incompatible: it's now considered an error to use the same transform twice in a pipeline.
       This was necessary in order to implement automatic timing of transforms.
     
     - Backwards-incompatible: transforms must initialize self.name in their constructors.

     - Potentially backwards-incompatible: transforms which write output files should do so through
       a new interface (transform.add_file(), transform.add_plot())

     - Streams can now be written in python (not just transforms).

     - Pipelines now write a JSON file containing summary information such as which plots were written.
       This is intended to be the input to a browser-based pipeline "viewer" being written by Masoud.

     - stream.run() syntax changed in a backwards-compatible way, with optional arguments to control	
       the placement of output files, and retrieve the summary json data.

- Version 7:

     - From Alex Josephy: four new transforms
	- std_dev_filter: masks freqs whose standard deviation deviates from the others by some sigma
        - kurtosis_filter: masks channels on the basis of excess kurtosis 
	- thermal_noise_weight: optimally weight channels assuming thermal noise (from Kiyo's ch_L1Mock)
        - RC_detrender: bidirectional high-pass filter designed to remove noise source "steps"

     - From Alex Jospehy: code to group bonsai triggers to form L1 events

     - Bugfix: postpadded transforms didn't work in python (reported by Alex Josephy)

- Version 6:

     - From Masoud: New transform legendre_detrender (polynomial fitting along either time or frequency axis)

     - From Masoud: New transform clipper_transform (clips outlier intensities, relative to a variance
       estimate which can be computed either in 2D, in 1D along the time axis, or in 1D along the frequency
       axis)

- Version 5:

     - The bonsai trigger hdf5 file output can now be generated in multifile mode.  Previously
       all triggers were written to one "monster file" of unbounded size.  Multifile mode is
       needed for long-running network streams, and also makes the rf_pipelines trigger plots
       easier to interpret, since they can be generated in 1-1 correspondence with intensity
       waterfall plots.  (See example3_chime for an example)

- Version 4:

  - plotter_transform: fix bug which sometimes caused outlier intensities to be superfluously masked
  
  - plotter_transform: determine color mapping using outlier-clipped mean/variance (without this, the
    plots were sometimes misleading!)

- Version 3:

  - From Masoud: New transform badchannel_mask, which reads a text file containing a list of bad 
    frequency ranges, and masks channels which overlap (currently python-only)

- Version 2:

  - new transform chime_file_writer, which writes a stream to disk in CHIME hdf5 format.

  - fix end-of-stream bug in plotter transform
