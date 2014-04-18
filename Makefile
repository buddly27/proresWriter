NDKDIR ?= ...
FFMEGDIR ?= ...

MYCXX ?= g++-4.1
LINK ?= g++-4.1

CXXFLAGS ?= -g -c -DUSE_GLEW -fPIC -msse \
            -fvisibility=hidden \
            -I$(NDKDIR)/include \
            -I$(FFMEGDIR)/include \
            -I./include

LINKFLAGS ?= -L$(NDKDIR) -shared

LIBS ?= -lDDImage -lavformat -lavcodec -lswscale -lavutil

all: proresWriter.so
.PRECIOUS : proresWriter.os

proresWriter.os: src/proresWriter.cpp
	$(MYCXX) $(CXXFLAGS) -o $(@) $<
proresWriter.so: proresWriter.os
	$(LINK) $(LINKFLAGS) $(LIBS) -o $(@) $<
clean:
	rm -f *.os *.so
