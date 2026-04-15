#[=======================================================================[.rst:
FindVorbisFile
--------------

Find the VorbisFile audio library.

Imported Targets
^^^^^^^^^^^^^^^^

``VorbisFile::VorbisFile``
  The VorbisFile library, if found.

Result Variables
^^^^^^^^^^^^^^^^

``VORBISFILE_FOUND``
  True if VorbisFile was found.
``VORBISFILE_INCLUDE_DIRS``
  Include directories for VorbisFile headers.
``VORBISFILE_LIBRARIES``
  Libraries to link against for VorbisFile.

#]=======================================================================]

find_path(VORBISFILE_INCLUDE_DIR NAMES vorbis/vorbisfile.h)
find_library(VORBISFILE_LIBRARY NAMES vorbisfile libvorbisfile)
find_library(VORBIS_LIBRARY NAMES vorbis libvorbis)
find_library(OGG_LIBRARY NAMES ogg libogg)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VorbisFile
    REQUIRED_VARS VORBISFILE_LIBRARY VORBISFILE_INCLUDE_DIR)

if(VORBISFILE_FOUND)
    set(VORBISFILE_INCLUDE_DIRS "${VORBISFILE_INCLUDE_DIR}")
    set(VORBISFILE_LIBRARIES "${VORBISFILE_LIBRARY}" "${VORBIS_LIBRARY}" "${OGG_LIBRARY}")

    if(NOT TARGET VorbisFile::VorbisFile)
        add_library(VorbisFile::VorbisFile UNKNOWN IMPORTED)
        set_target_properties(VorbisFile::VorbisFile PROPERTIES
            IMPORTED_LOCATION "${VORBISFILE_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${VORBISFILE_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${VORBIS_LIBRARY};${OGG_LIBRARY}")
    endif()
endif()

mark_as_advanced(VORBISFILE_INCLUDE_DIR VORBISFILE_LIBRARY VORBIS_LIBRARY OGG_LIBRARY)
