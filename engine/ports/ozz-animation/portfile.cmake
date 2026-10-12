vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO guillaumeblanc/ozz-animation
    REF 0.17.0
    SHA512 85e6c1b5e693c2f044815b92cb0896fb7f7d495d91cb9ff55aef4615483c33c3e4ef7232f84aa6539c92aed0e9d842fcc448712166a0eab2b252525196caff90
    HEAD_REF master
)

string(COMPARE EQUAL "${VCPKG_CRT_LINKAGE}" "dynamic" USE_DLL_RUNTIME)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -Dozz_build_tools=OFF
        -Dozz_build_fbx=OFF
        -Dozz_build_gltf=OFF
        -Dozz_build_data=OFF
        -Dozz_build_samples=OFF
        -Dozz_build_howtos=OFF
        -Dozz_build_tests=OFF
        -Dozz_build_postfix=OFF
        -Dozz_build_msvc_rt_dll=${USE_DLL_RUNTIME}
)
vcpkg_cmake_install()

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/ozz-animation-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")
