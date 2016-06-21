# Makefile.local must define the following variables
#   LIBDIR      install dir for C++ libraries
#   INCDIR      install dir for C++ headers
#   PYDIR       install dir for python/cython modules
#   CPP         g++ compiler command line
#
# See site/Makefile.local.* for examples.

include Makefile.local

INCFILES=rf_pipelines.hpp rf_pipelines_internals.hpp

all: librf_pipelines.so run-unit-tests

install: librf_pipelines.so
	cp -f $(INCFILES) $(INCDIR)/
	cp -f librf_pipelines.so $(LIBDIR)/

uninstall:
	for f in $(INCFILES); do rm -f $(INCDIR)/$$f; done
	rm -f $(LIBDIR)/librf_pipelines.so

clean:
	rm -f *~ *.o *.so

%.o: %.cpp $(INCFILES)
	$(CPP) -c -o $@ $<

librf_pipelines.so: wraparound_buf.o
	$(CPP) -shared -o $@ $<

run-unit-tests: run-unit-tests.o librf_pipelines.so
	$(CPP) -o $@ $< -lrf_pipelines
