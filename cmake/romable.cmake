# Copyright © 2020 Michal Schulz <michal.schulz@gmx.de>
# https://github.com/michalsc
#
# This Source Code Form is subject to the terms of the
# Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
# with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Run after the link (cmake -DOBJDUMP=... -DFILE=... -P): the library must be able to run from the ROM, so it has no writable data. The
# linked file must hold one section, .text: a .data or a .bss means a global that is written, and that fails the build.
execute_process(COMMAND ${OBJDUMP} -h ${FILE} OUTPUT_VARIABLE SECTIONS RESULT_VARIABLE RESULT)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "romable: ${OBJDUMP} -h ${FILE} failed")
endif()
string(REGEX MATCHALL "\n +[0-9]+ [.][a-z]+" FOUND "${SECTIONS}")
foreach(LINE ${FOUND})
    string(REGEX REPLACE "^\n +[0-9]+ " "" NAME "${LINE}")
    if(NOT NAME STREQUAL ".text")
        message(FATAL_ERROR "romable: ${FILE} has a section ${NAME}: writable data is not allowed (state goes into the base)")
    endif()
endforeach()
