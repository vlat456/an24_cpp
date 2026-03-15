# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/vladimir/an24_cpp/_deps/nfde-src")
  file(MAKE_DIRECTORY "/Users/vladimir/an24_cpp/_deps/nfde-src")
endif()
file(MAKE_DIRECTORY
  "/Users/vladimir/an24_cpp/_deps/nfde-build"
  "/Users/vladimir/an24_cpp/_deps/nfde-subbuild/nfde-populate-prefix"
  "/Users/vladimir/an24_cpp/_deps/nfde-subbuild/nfde-populate-prefix/tmp"
  "/Users/vladimir/an24_cpp/_deps/nfde-subbuild/nfde-populate-prefix/src/nfde-populate-stamp"
  "/Users/vladimir/an24_cpp/_deps/nfde-subbuild/nfde-populate-prefix/src"
  "/Users/vladimir/an24_cpp/_deps/nfde-subbuild/nfde-populate-prefix/src/nfde-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/vladimir/an24_cpp/_deps/nfde-subbuild/nfde-populate-prefix/src/nfde-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/vladimir/an24_cpp/_deps/nfde-subbuild/nfde-populate-prefix/src/nfde-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
