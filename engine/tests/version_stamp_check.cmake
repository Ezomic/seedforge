# The configure-time version stamp (SEED-691) in a throwaway repository: clean, dirty, detached and without git.
# Run with -DGIT=<git executable> -DSCRATCH=<dir> -P version_stamp_check.cmake.
include(${CMAKE_CURRENT_LIST_DIR}/../core/version_stamp.cmake)

set(root ${SCRATCH}/version_stamp)
set(repo ${root}/repo)
file(REMOVE_RECURSE ${root})
file(MAKE_DIRECTORY ${repo})
# The scratch folder sits inside the engine's own checkout, which git would otherwise find above it.
set(ENV{GIT_CEILING_DIRECTORIES} ${root})

function(git)
    execute_process(COMMAND ${GIT} ${ARGN} WORKING_DIRECTORY ${repo} RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE out)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "git ${ARGN} failed: ${out}")
    endif()
endfunction()

function(expect_stamp dir git expected)
    ichor_version_stamp(stamp ${dir} "${git}")
    if(NOT stamp STREQUAL expected)
        message(FATAL_ERROR "stamp for ${dir} is '${stamp}', expected '${expected}'")
    endif()
endfunction()

expect_stamp(${repo} ${GIT} unknown)

git(init -q)
git(config user.email stamp@example.invalid)
git(config user.name stamp)
git(config core.autocrlf false)
expect_stamp(${repo} ${GIT} unknown)

file(WRITE ${repo}/a.txt "one\n")
git(add a.txt)
git(commit -q -m one)
execute_process(COMMAND ${GIT} rev-parse --short HEAD WORKING_DIRECTORY ${repo} OUTPUT_VARIABLE first OUTPUT_STRIP_TRAILING_WHITESPACE)
expect_stamp(${repo} ${GIT} ${first})

file(WRITE ${repo}/.gitignore "build/\n")
git(add .gitignore)
git(commit -q -m ignore)
execute_process(COMMAND ${GIT} rev-parse --short HEAD WORKING_DIRECTORY ${repo} OUTPUT_VARIABLE first OUTPUT_STRIP_TRAILING_WHITESPACE)
file(WRITE ${repo}/build/out.txt "ignored\n")
expect_stamp(${repo} ${GIT} ${first})

# A new untracked source is picked up by the CONFIGURE_DEPENDS globs, so it changes the build too.
file(WRITE ${repo}/untracked.txt "new\n")
expect_stamp(${repo} ${GIT} ${first}-dirty)
file(REMOVE ${repo}/untracked.txt)
expect_stamp(${repo} ${GIT} ${first})

file(WRITE ${repo}/a.txt "two\n")
expect_stamp(${repo} ${GIT} ${first}-dirty)
git(add a.txt)
expect_stamp(${repo} ${GIT} ${first}-dirty)

git(commit -q -m two)
execute_process(COMMAND ${GIT} rev-parse --short HEAD WORKING_DIRECTORY ${repo} OUTPUT_VARIABLE second OUTPUT_STRIP_TRAILING_WHITESPACE)
if(second STREQUAL first)
    message(FATAL_ERROR "two commits share the short hash ${first}")
endif()
expect_stamp(${repo} ${GIT} ${second})

git(checkout -q --detach ${first})
expect_stamp(${repo} ${GIT} ${first})

expect_stamp(${repo} "" unknown)
expect_stamp(${repo} GIT_EXECUTABLE-NOTFOUND unknown)
expect_stamp(${repo} ${root}/no-such-git unknown)

file(MAKE_DIRECTORY ${root}/tarball)
expect_stamp(${root}/tarball ${GIT} unknown)

message(STATUS "version stamp: clean ${first}, dirty, detached and no-git cases hold")
