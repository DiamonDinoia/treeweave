# Build demo.cpp against an extracted treeweave headers bundle, no CMake.
#
#   1. tar xzf treeweave-<ver>-cxx-headers.tar.gz   # -> ./include/...
#   2. make                                         # this file
#
# Point TREEWEAVE_INC elsewhere if the bundle lives somewhere else:
#   make TREEWEAVE_INC=-I/path/to/include

CXX          ?= g++
CXXFLAGS     ?= -O3 -march=native
TREEWEAVE_INC ?= -Iinclude

# -std=c++20 is required and kept out of the overridable CXXFLAGS on purpose: an
# inherited CXXFLAGS (common on HPC modules) would otherwise clobber it via ?=.
demo: demo.cpp
	$(CXX) -std=c++20 $(CXXFLAGS) $(TREEWEAVE_INC) $< -o $@

clean:
	rm -f demo

.PHONY: clean
