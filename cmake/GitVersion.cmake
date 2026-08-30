# Re-derives the git identity and refreshes Plugin.h.
#
# Meant to be run in script mode (cmake -P) from a custom target, not included.
# The point is timing: configure_file at configure time captures whatever commit
# happened to be checked out then, and the stamp goes stale on the very next
# commit. A plugin that misreports which build is running sends anyone reading a
# crash log down the wrong path.
#
# Required definitions: SOURCE_DIR, IN_FILE, OUT_FILE, PROJECT_NAME,
# PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR, PROJECT_VERSION_PATCH.

find_package(Git QUIET)

if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --dirty --always
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

if(NOT GIT_DESCRIBE)
    set(GIT_DESCRIBE "unknown")
endif()

# configure_file leaves the output alone when the content would be identical, so
# running this before every build does not force a recompile.
configure_file("${IN_FILE}" "${OUT_FILE}" @ONLY)
