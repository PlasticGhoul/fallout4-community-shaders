# NVIDIA Streamline for DLSS Frame Generation on the D3D12 bridge. NVIDIA's
# git repository carries no binaries, the release archive does. It is fetched
# once into extern/.downloads, checked against the SHA256 GitHub publishes for
# it, and only the headers, six runtime DLLs and three licences are extracted
# into extern/Streamline. Neither directory is tracked; the archive is 276 MB.
set(STREAMLINE_VERSION "2.14.16")
set(STREAMLINE_SHA256
    "32041e51db0cab709a271b72a352281b690c2abbd669a827809401a5dd23956b"
)
set(STREAMLINE_URL
    "https://github.com/NVIDIA-RTX/Streamline/releases/download/v${STREAMLINE_VERSION}/streamline-sdk-v${STREAMLINE_VERSION}.zip"
)
set(STREAMLINE_DIR "${CMAKE_SOURCE_DIR}/extern/Streamline")
set(STREAMLINE_ARCHIVE
    "${CMAKE_SOURCE_DIR}/extern/.downloads/streamline-sdk-v${STREAMLINE_VERSION}.zip"
)
set(STREAMLINE_STAMP "${STREAMLINE_DIR}/VERSION")

set(_streamline_present "")
if(EXISTS "${STREAMLINE_STAMP}")
    file(READ "${STREAMLINE_STAMP}" _streamline_present)
    string(STRIP "${_streamline_present}" _streamline_present)
endif()

if(NOT _streamline_present STREQUAL STREAMLINE_VERSION)
    if(EXISTS "${STREAMLINE_ARCHIVE}")
        file(SHA256 "${STREAMLINE_ARCHIVE}" _streamline_hash)
        if(NOT _streamline_hash STREQUAL STREAMLINE_SHA256)
            file(REMOVE "${STREAMLINE_ARCHIVE}")
        endif()
    endif()
    if(NOT EXISTS "${STREAMLINE_ARCHIVE}")
        message(
            STATUS
            "Downloading Streamline ${STREAMLINE_VERSION} (276 MB, once)"
        )
        file(
            DOWNLOAD "${STREAMLINE_URL}" "${STREAMLINE_ARCHIVE}"
            EXPECTED_HASH SHA256=${STREAMLINE_SHA256}
            STATUS _streamline_status
        )
        list(GET _streamline_status 0 _streamline_code)
        if(NOT _streamline_code EQUAL 0)
            file(REMOVE "${STREAMLINE_ARCHIVE}")
            message(
                FATAL_ERROR
                "Streamline download failed: ${_streamline_status}"
            )
        endif()
    endif()
    file(REMOVE_RECURSE "${STREAMLINE_DIR}")
    file(
        ARCHIVE_EXTRACT
        INPUT "${STREAMLINE_ARCHIVE}"
        DESTINATION "${STREAMLINE_DIR}"
        PATTERNS
            "include/*"
            "bin/x64/sl.interposer.dll"
            "bin/x64/sl.common.dll"
            "bin/x64/sl.dlss_g.dll"
            "bin/x64/sl.reflex.dll"
            "bin/x64/sl.pcl.dll"
            "bin/x64/nvngx_dlssg.dll"
            "bin/x64/nvngx_dlss.license.txt"
            "bin/x64/reflex.license.txt"
            "license.txt"
    )
    file(WRITE "${STREAMLINE_STAMP}" "${STREAMLINE_VERSION}\n")
endif()

if(NOT EXISTS "${STREAMLINE_DIR}/include/sl.h")
    message(
        FATAL_ERROR
        "extern/Streamline is incomplete; delete it and configure again"
    )
endif()
