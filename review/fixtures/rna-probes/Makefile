# Explicit per-source targets; rebuilding prevents stale-header measurements.
.DEFAULT_GOAL := help
.PHONY: help r03-representation-benchmark representation-benchmark translation-cache-probe
help:
	@echo "Select a target and set R03_CHECKOUT, R08_CHECKOUT or COMBINED_CHECKOUT."
r03-representation-benchmark:
	test -n "$(R03_CHECKOUT)"
	$(CXX) -I$(R03_CHECKOUT)/src -I$(R03_CHECKOUT)/include -std=c++20 -O2 -g0 r03-representation-benchmark.cpp $(R03_CHECKOUT)/lib/libvgio.a -L$(R03_CHECKOUT)/lib -lsdsl $$(pkg-config --static --libs protobuf) -pthread -o $@
representation-benchmark:
	test -n "$(R08_CHECKOUT)"
	$(CXX) -I$(R08_CHECKOUT)/src -std=c++20 -O2 -g0 representation-benchmark.cpp -o $@
translation-cache-probe:
	test -n "$(COMBINED_CHECKOUT)"
	$(CXX) -I$(COMBINED_CHECKOUT)/src -std=c++20 -O2 -g0 translation-cache-probe.cpp -o $@
