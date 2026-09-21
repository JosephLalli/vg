ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SOURCE := $(dir $(lastword $(MAKEFILE_LIST)))path_graph_fingerprint.cpp
TARGET ?= $(ROOT)/tmp/transcript_memory_20260915/path_graph_fingerprint

CPPFLAGS += -I$(ROOT)/include
CXXFLAGS += -O3 -std=c++17 -fopenmp
LDFLAGS += -L$(ROOT)/lib -Wl,-rpath,$(ROOT)/lib
LDLIBS += -lbdsg -lhandlegraph -lsdsl -ldivsufsort -ldivsufsort64 -lcrypto -lzstd -lpthread

.PHONY: path_graph_fingerprint

path_graph_fingerprint: $(TARGET)

$(TARGET): $(SOURCE)

	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)
