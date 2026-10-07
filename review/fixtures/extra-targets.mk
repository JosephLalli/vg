# Load after vg's Makefile in a freshly built, exact candidate checkout.
REVIEW_FIXTURES ?= $(CURDIR)/review/fixtures
REVIEW_ID ?= candidate
REVIEW_SERIALIZATION_SOURCE ?= standalone_serialization_benchmark.cpp

.PHONY: review-serialization-benchmark review-distance-fixture review-distance-staging
review-serialization-benchmark:
	$(CXX) $(INCLUDE_FLAGS) $(CPPFLAGS) $(CXXFLAGS) -O2 -g0 -o $(REVIEW_FIXTURES)/serialization-$(REVIEW_ID) $(REVIEW_FIXTURES)/$(REVIEW_SERIALIZATION_SOURCE) $(PRE_LINK_DEPS) $(LD_LIB_DIR_FLAGS) $(LDFLAGS) $(LIB_DIR)/libvg.a $(LD_LIB_FLAGS) $(START_STATIC) $(LD_STATIC_LIB_FLAGS) $(END_STATIC) $(LD_STATIC_LIB_DEPS) $(LD_EXE_LIB_FLAGS)

review-distance-fixture:
	$(CXX) $(INCLUDE_FLAGS) $(CPPFLAGS) $(CXXFLAGS) -O2 -g0 -o $(REVIEW_FIXTURES)/distance-$(REVIEW_ID) $(REVIEW_FIXTURES)/distance_fixture.cpp $(PRE_LINK_DEPS) $(LD_LIB_DIR_FLAGS) $(LDFLAGS) $(LIB_DIR)/libvg.a $(LD_LIB_FLAGS) $(START_STATIC) $(LD_STATIC_LIB_FLAGS) $(END_STATIC) $(LD_STATIC_LIB_DEPS) $(LD_EXE_LIB_FLAGS)

review-distance-staging:
	$(CXX) $(INCLUDE_FLAGS) $(CPPFLAGS) $(CXXFLAGS) -O2 -g0 -o $(REVIEW_FIXTURES)/distance-staging $(REVIEW_FIXTURES)/distance_staging_benchmark.cpp $(PRE_LINK_DEPS) $(LD_LIB_DIR_FLAGS) $(LDFLAGS) $(LIB_DIR)/libvg.a $(LD_LIB_FLAGS) $(START_STATIC) $(LD_STATIC_LIB_FLAGS) $(END_STATIC) $(LD_STATIC_LIB_DEPS) $(LD_EXE_LIB_FLAGS)
