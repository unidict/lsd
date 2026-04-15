#[=======================================================================[.rst:
FindUnistring
-------------

Find the GNU libunistring library.

Imported Targets
^^^^^^^^^^^^^^^^

``Unistring::Unistring``
  The libunistring library, if found.

Result Variables
^^^^^^^^^^^^^^^^

``UNISTRING_FOUND``
  True if libunistring was found.
``UNISTRING_INCLUDE_DIRS``
  Include directories for libunistring headers.
``UNISTRING_LIBRARIES``
  Libraries to link against for libunistring.

#]=======================================================================]

find_path(UNISTRING_INCLUDE_DIR NAMES unicase.h)
find_library(UNISTRING_LIBRARY NAMES unistring libunistring)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Unistring
    REQUIRED_VARS UNISTRING_LIBRARY UNISTRING_INCLUDE_DIR)

if(UNISTRING_FOUND)
    set(UNISTRING_INCLUDE_DIRS "${UNISTRING_INCLUDE_DIR}")
    set(UNISTRING_LIBRARIES "${UNISTRING_LIBRARY}")

    if(NOT TARGET Unistring::Unistring)
        add_library(Unistring::Unistring UNKNOWN IMPORTED)
        set_target_properties(Unistring::Unistring PROPERTIES
            IMPORTED_LOCATION "${UNISTRING_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${UNISTRING_INCLUDE_DIR}")
    endif()
endif()

mark_as_advanced(UNISTRING_INCLUDE_DIR UNISTRING_LIBRARY)
