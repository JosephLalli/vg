# Build the splice-junction survival probe through ./build-local.sh:
#   ./build-local.sh -f docs/exact_dedup_indexing_feasibility/junction_survival.mk OUTPUT=/abs/path/junction_survival

OUTPUT ?=
BDSG_ARCHIVE ?= lib/libbdsg.a

ifeq ($(strip $(OUTPUT)),)
$(error OUTPUT must name an absolute probe path)
endif

.PHONY: all
all: $(OUTPUT)

$(OUTPUT): docs/exact_dedup_indexing_feasibility/junction_survival.cpp $(BDSG_ARCHIVE)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -fopenmp -O3 -std=c++17 -Wall -Wextra -Werror=return-type \
		-I. -Ideps/libbdsg/bdsg/include -Ideps/libbdsg/bdsg/deps/libhandlegraph/src/include \
		-Ideps/sdsl-lite/include $(CPPFLAGS) $< -o $@ $(LDFLAGS) \
		$(BDSG_ARCHIVE) lib/libsdsl.a lib/libdivsufsort.a lib/libdivsufsort64.a \
		-lhandlegraph -lcrypto -lpthread -ldl
