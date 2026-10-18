# NVIDIA DLSS Neural Rendering (DLSS 5) for the D3D12 bridge. No public SDK
# carries it yet: this is the package NVIDIA's RTX Remix build fetches for its
# own integration, with a signed nvngx_dlssnr.dll, the parameter header and the
# NVIDIA RTX SDKs licence. Should NR appear in a published SDK, the fetch moves
# there (F5.5c spec, R4). Fetched once into extern/.downloads, checked against
# its SHA256, and only the header, the runtime DLL and the licence are
# extracted into extern/DlssNr. Neither directory is tracked; the archive is
# 368 MB. Only the dlssnr header goes on the include path: the package's own
# nvsdk_ngx.h must not shadow the DLSS SDK's, so it is never extracted.
set(DLSSNR_VERSION "dlnr@2")
set(DLSSNR_SHA256
    "4bb8651b3ac2180f23df1f39422c489810a13d445a94dc836c876c18c93ecefe"
)
set(DLSSNR_URL
    "https://d1oqksizpb0e3d.cloudfront.net/rtx-remix-ngx_sdk_dlnr@2.7z"
)
set(DLSSNR_DIR "${CMAKE_SOURCE_DIR}/extern/DlssNr")
set(DLSSNR_ARCHIVE
    "${CMAKE_SOURCE_DIR}/extern/.downloads/rtx-remix-ngx_sdk_dlnr@2.7z"
)
set(DLSSNR_STAMP "${DLSSNR_DIR}/VERSION")

set(_dlssnr_present "")
if(EXISTS "${DLSSNR_STAMP}")
    file(READ "${DLSSNR_STAMP}" _dlssnr_present)
    string(STRIP "${_dlssnr_present}" _dlssnr_present)
endif()

if(NOT _dlssnr_present STREQUAL DLSSNR_VERSION)
    if(EXISTS "${DLSSNR_ARCHIVE}")
        file(SHA256 "${DLSSNR_ARCHIVE}" _dlssnr_hash)
        if(NOT _dlssnr_hash STREQUAL DLSSNR_SHA256)
            file(REMOVE "${DLSSNR_ARCHIVE}")
        endif()
    endif()
    if(NOT EXISTS "${DLSSNR_ARCHIVE}")
        message(
            STATUS
            "Downloading DLSS Neural Rendering (368 MB, once)"
        )
        file(
            DOWNLOAD "${DLSSNR_URL}" "${DLSSNR_ARCHIVE}"
            EXPECTED_HASH SHA256=${DLSSNR_SHA256}
            STATUS _dlssnr_status
        )
        list(GET _dlssnr_status 0 _dlssnr_code)
        if(NOT _dlssnr_code EQUAL 0)
            file(REMOVE "${DLSSNR_ARCHIVE}")
            message(
                FATAL_ERROR
                "DLSS Neural Rendering download failed: ${_dlssnr_status}"
            )
        endif()
    endif()
    file(REMOVE_RECURSE "${DLSSNR_DIR}")
    file(
        ARCHIVE_EXTRACT
        INPUT "${DLSSNR_ARCHIVE}"
        DESTINATION "${DLSSNR_DIR}"
        PATTERNS
            "include/nvsdk_ngx_defs_dlssnr.h"
            "lib/Windows_x86_64/rel/nvngx_dlssnr.dll"
            "LICENSE.txt"
    )
    file(WRITE "${DLSSNR_STAMP}" "${DLSSNR_VERSION}\n")
endif()

if(
    NOT EXISTS "${DLSSNR_DIR}/include/nvsdk_ngx_defs_dlssnr.h"
    OR NOT EXISTS "${DLSSNR_DIR}/lib/Windows_x86_64/rel/nvngx_dlssnr.dll"
)
    message(
        FATAL_ERROR
        "extern/DlssNr is incomplete; delete it and configure again"
    )
endif()
