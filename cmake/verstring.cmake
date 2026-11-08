# Copyright © 2020 Michal Schulz <michal.schulz@gmx.de>
# https://github.com/michalsc
#
# This Source Code Form is subject to the terms of the
# Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
# with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Build version string in Amiga-like manner: $VER: project_name version (build_date) git:git_hash"

function(get_verstring OUTPUT)
    find_package(Git)
    string(TIMESTAMP PROJECT_DATE "(%d.%m.%Y)")
    # Only the repository of this project: a working copy without .git of its own would otherwise get the hash of a parent repository
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --show-toplevel WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE GIT_TOP OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    get_filename_component(GIT_TOP "${GIT_TOP}" REALPATH)
    get_filename_component(SRC_TOP "${PROJECT_SOURCE_DIR}" REALPATH)
    if (NOT GIT_TOP STREQUAL SRC_TOP)
        set(${OUTPUT} "$VER: ${PROJECT_NAME} ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR} ${PROJECT_DATE} git:none" PARENT_SCOPE)
        return()
    endif()
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} status --porcelain WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE GIT_PORCELAIN OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if (GIT_HASH STREQUAL "")
        # not a git working copy (yet): say so in the version string
        set(GIT_HASH "none")
        set(GIT_PORCELAIN "")
    elseif (NOT GIT_PORCELAIN STREQUAL "")
        set(GIT_PORCELAIN ",dirty")
    endif()
    set(${OUTPUT} "$VER: ${PROJECT_NAME} ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR} ${PROJECT_DATE} git:${GIT_HASH}${GIT_PORCELAIN}" PARENT_SCOPE)
endfunction(get_verstring)
