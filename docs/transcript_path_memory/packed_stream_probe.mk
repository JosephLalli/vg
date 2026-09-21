# Build only the dense PackedGraph writer probe through ./build-local.sh.

OUTPUT ?=
BDSG_ARCHIVE ?=

ifeq ($(strip $(OUTPUT)),)
$(error OUTPUT must name an absolute probe path)
endif

ifeq ($(strip $(BDSG_ARCHIVE)),)
$(error BDSG_ARCHIVE must name the pinned libbdsg.a)
endif

.PHONY: all
all: $(OUTPUT)

$(OUTPUT): docs/transcript_path_memory/packed_stream_probe.cpp \
	$(BDSG_ARCHIVE) \
	deps/libbdsg/bdsg/include/bdsg/packed_graph.hpp \
	deps/libbdsg/bdsg/include/bdsg/internal/base_packed_graph.hpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -fopenmp -O3 -std=c++17 -Wall -Wextra -Werror=return-type \
		-I. -Ideps/libbdsg/bdsg/include -Ideps/libbdsg/bdsg/deps/libhandlegraph/src/include \
		-Ideps/sdsl-lite/include $(CPPFLAGS) $< -o $@ $(LDFLAGS) \
		$(BDSG_ARCHIVE) lib/libsdsl.a lib/libdivsufsort.a lib/libdivsufsort64.a \
		-lhandlegraph -lcrypto -lpthread -ldl
