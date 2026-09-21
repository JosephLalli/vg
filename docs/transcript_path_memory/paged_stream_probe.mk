# Standalone probe for generated PagedVector serialization.
# Build through the repository wrapper:
#   JOBS=1 ./build-local.sh -f docs/transcript_path_memory/paged_stream_probe.mk OUTPUT=/absolute/path/paged_stream_probe

CXX ?= g++
OUTPUT ?=

ifeq ($(strip $(OUTPUT)),)
$(error OUTPUT must name an absolute probe path)
endif

.PHONY: all
all: $(OUTPUT)

$(OUTPUT): docs/transcript_path_memory/paged_stream_probe.cpp deps/libbdsg/bdsg/include/bdsg/internal/packed_structs.hpp
	mkdir -p $(dir $@)
	$(CXX) -O2 -std=c++17 -Wall -Wextra -Werror \
		-Ideps/libbdsg/bdsg/include -Ideps/sdsl-lite/include $< \
		-Llib -lsdsl -ldivsufsort -ldivsufsort64 -o $@
