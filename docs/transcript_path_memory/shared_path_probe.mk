# Standalone probe for the header-only shared transcript-path prototype.
# Invoke through build-local.sh so the repository toolchain contract still applies:
#   JOBS=1 ./build-local.sh -f docs/transcript_path_memory/shared_path_probe.mk OUTPUT=/absolute/path/probe

CXX ?= g++
OUTPUT ?=

ifeq ($(strip $(OUTPUT)),)
$(error OUTPUT must name an absolute probe path)
endif

.PHONY: all
all: $(OUTPUT)

$(OUTPUT): docs/transcript_path_memory/shared_path_probe.cpp src/shared_transcript_path.hpp
	mkdir -p $(dir $@)
	$(CXX) -O3 -std=c++17 -Wall -Wextra -Werror -I. $< -o $@
