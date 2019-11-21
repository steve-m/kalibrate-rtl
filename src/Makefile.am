bin_PROGRAMS = kal

kal_SOURCES = \
   arfcn_freq.cc \
   c0_detect.cc	 \
   circular_buffer.cc \
   fcch_detector.cc \
   kal.cc \
   offset.cc \
   usrp_source.cc \
   util.cc\
   arfcn_freq.h \
   c0_detect.h \
   circular_buffer.h \
   fcch_detector.h \
   offset.h \
   usrp_complex.h \
   usrp_source.h \
   util.h\
   version.h

kal_CXXFLAGS = $(FFTW3_CFLAGS) $(LIBRTLSDR_CFLAGS)
kal_LDADD = $(FFTW3_LIBS) $(LIBRTLSDR_LIBS) $(LRT_FLAGS)
