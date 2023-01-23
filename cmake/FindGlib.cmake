# FindGlib
# ---------
#
# Try to locate the GLib2 library.
# If found, this will define the following variables:
#
# ``Glib_FOUND``
# True if the GLib2 library is available
# ``Glib_INCLUDE_DIRS``
# The GLib2 include directories
# ``Glib_LIBRARIES``
# The GLib2 libraries for linking
# ``Glib_INCLUDE_DIR``
# Deprecated, use ``Glib_INCLUDE_DIRS``
# ``Glib_LIBRARY``
# Deprecated, use ``Glib_LIBRARIES``
#
# If ``Glib_FOUND`` is TRUE, it will also define the following
# imported target:
#
# ``GLIB2::GLIB2``
# The GLIB2 library
#
# Since 5.41.0.

# =============================================================================
# Copyright (c) 2008 Laurent Montel, <montel@kde.org>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the copyright
# notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution.
# 3. The name of the author may not be used to endorse or promote products
# derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# =============================================================================

find_package(PkgConfig)
pkg_check_modules(PC_Glib QUIET glib-2.0)

find_path(Glib_INCLUDE_DIRS
  NAMES glib.h
  HINTS ${PC_Glib_INCLUDEDIR}
  PATH_SUFFIXES glib-2.0)

find_library(Glib_LIBRARIES
  NAMES glib-2.0
  HINTS ${PC_Glib_LIBDIR}
)

# search the glibconfig.h include dir under the same root where the library is found
get_filename_component(glib2LibDir "${Glib_LIBRARIES}" PATH)

find_path(Glib_INTERNAL_INCLUDE_DIR glibconfig.h
  PATH_SUFFIXES glib-2.0/include
  HINTS ${PC_Glib_INCLUDEDIR} "${glib2LibDir}" ${CMAKE_SYSTEM_LIBRARY_PATH})

# not sure if this include dir is optional or required
# for now it is optional
if(Glib_INTERNAL_INCLUDE_DIR)
  list(APPEND Glib_INCLUDE_DIRS "${Glib_INTERNAL_INCLUDE_DIR}")
endif()

# Deprecated synonyms
set(Glib_INCLUDE_DIR "${Glib_INCLUDE_DIRS}")
set(Glib_LIBRARY "${Glib_LIBRARIES}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Glib   DEFAULT_MSG Glib_LIBRARIES Glib_INCLUDE_DIRS)

if(Glib_FOUND AND NOT TARGET Glib::Glib)
  add_library(Glib::Glib UNKNOWN IMPORTED)
  set_target_properties(Glib::Glib PROPERTIES
    IMPORTED_LOCATION "${Glib_LIBRARIES}"
    INTERFACE_INCLUDE_DIRECTORIES "${Glib_INCLUDE_DIRS}")
endif()

mark_as_advanced(Glib_INCLUDE_DIRS Glib_INCLUDE_DIR
  Glib_LIBRARIES Glib_LIBRARY)

include(FeatureSummary)
set_package_properties(Glib PROPERTIES
  URL "https://wiki.gnome.org/Projects/GLib"
  DESCRIPTION "Event loop and utility library")