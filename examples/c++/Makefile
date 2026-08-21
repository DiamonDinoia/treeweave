# Build the C++ examples WITHOUT CMake, FINUFFT-style.
#
#   1. cmake -S . -B build -DTREEWEAVE_BUILD_EXAMPLES=ON   # generates build/make.inc
#      cmake --build build
#   2. cd examples/c++ && make
#
# make.inc (auto-generated) sets CXX, CXXFLAGS and TREEWEAVE_INC, the -I paths
# for treeweave's header-only deps (polyfit, POET, xsimd, mdspan), wherever CMake
# fetched them. `make -n simple1d` prints the exact compile command.

include ../../build/make.inc

EXAMPLES = simple1d simple2d simple3d hankel with_options vector_output zeta_bench

all: $(EXAMPLES)

$(EXAMPLES): %: %.cpp
	$(CXX) $(CXXFLAGS) $(TREEWEAVE_INC) $< -o $@

clean:
	rm -f $(EXAMPLES)

.PHONY: all clean
