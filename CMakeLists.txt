cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Android NDK/SDK 前置探测（必须写在 project() 之前）
#
# /opt/Qt/6.11.1/android_arm64_v8a/lib/cmake/Qt6/qt.toolchain.cmake 里记录的
# chainload 工具链路径是 Qt 打包机上的 /opt/android/android-ndk-r27c，本机不存在。
# 它一旦 chainload 失败，ANDROID 变量就不成立，下面的 QT_HOST_PATH 自动探测也随之失效，
# 于是 find_package(Qt6) 落到系统 Qt 6.10.2 上，报 "Qt6CoreTools 版本不兼容"。
#
# Qt 的工具链脚本支持用环境变量 ANDROID_NDK_ROOT 覆盖那条记录值，
# 而工具链是在 project() 里加载的，所以只能在这里先把环境变量设好。
# 外部（IDE / 命令行）已显式指定时不覆盖。
# ---------------------------------------------------------------------------
if("$ENV{ANDROID_NDK_ROOT}" STREQUAL "")
    # 候选位置按平台列出：Linux/macOS 的 ~/Android/sdk、Windows 的 %LOCALAPPDATA%。
    # 一个都找不到时保持原状，让 Qt 工具链自己报出它的那条警告。
    file(GLOB _qt_android_toolchains
        "$ENV{ANDROID_SDK_ROOT}/ndk/*/build/cmake/android.toolchain.cmake"
        "$ENV{ANDROID_HOME}/ndk/*/build/cmake/android.toolchain.cmake"
        "$ENV{HOME}/Android/Sdk/ndk/*/build/cmake/android.toolchain.cmake"
        "$ENV{HOME}/Library/Android/sdk/ndk/*/build/cmake/android.toolchain.cmake"
        "$ENV{LOCALAPPDATA}/Android/Sdk/ndk/*/build/cmake/android.toolchain.cmake"
        "$ENV{USERPROFILE}/AppData/Local/Android/Sdk/ndk/*/build/cmake/android.toolchain.cmake"
        "/opt/android/android-ndk-*/build/cmake/android.toolchain.cmake"
    )
    if(_qt_android_toolchains)
        list(SORT _qt_android_toolchains)
        list(GET _qt_android_toolchains -1 _qt_android_toolchain)
        get_filename_component(_qt_android_ndk_root "${_qt_android_toolchain}/../../.." ABSOLUTE)
        set(ENV{ANDROID_NDK_ROOT} "${_qt_android_ndk_root}")
        message(STATUS "自动探测到 Android NDK: ${_qt_android_ndk_root}")
    endif()
endif()

if("$ENV{ANDROID_SDK_ROOT}" STREQUAL "" AND NOT "$ENV{ANDROID_NDK_ROOT}" STREQUAL "")
    get_filename_component(_qt_android_sdk_root "$ENV{ANDROID_NDK_ROOT}/../.." ABSOLUTE)
    if(EXISTS "${_qt_android_sdk_root}/platform-tools")
        set(ENV{ANDROID_SDK_ROOT} "${_qt_android_sdk_root}")
        message(STATUS "自动探测到 Android SDK: ${_qt_android_sdk_root}")
    endif()
endif()

project(yiban_android_app VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Android kit 本身不含 host 端工具（moc/rcc/qmlcachegen 对应的 Qt6*Tools 包），
# 缺 QT_HOST_PATH 时 find_package(Qt6Core) 会去找桌面 Qt，版本对不上就整串失败。
# 桌面 kit 是版本目录下与 android_* 并列的兄弟目录，但名字随平台不同：
#   Linux: gcc_64 / Windows: msvc2022_64、mingw_64 / macOS: macos
# 所以从 CMAKE_TOOLCHAIN_FILE 反推 Qt 安装根，逐个试，不再写死 /opt/Qt/.../gcc_64。
if(ANDROID AND NOT QT_HOST_PATH AND NOT "$ENV{QT_HOST_PATH}" STREQUAL "")
    set(QT_HOST_PATH "$ENV{QT_HOST_PATH}" CACHE PATH "Path to host Qt installation")
endif()

# CMAKE_TOOLCHAIN_FILE 在 Windows 上可能是反斜杠路径，先统一成正斜杠再判断。
file(TO_CMAKE_PATH "${CMAKE_TOOLCHAIN_FILE}" _qt_toolchain_normalized)
if(ANDROID AND NOT QT_HOST_PATH AND _qt_toolchain_normalized MATCHES "Qt6/qt\\.toolchain\\.cmake$")
    # <Qt根>/<版本>/android_arm64_v8a/lib/cmake/Qt6/qt.toolchain.cmake → 上溯 4 层得到 kit 目录
    get_filename_component(_qt_android_kit "${_qt_toolchain_normalized}" ABSOLUTE)
    foreach(_qt_up 1 2 3 4)
        get_filename_component(_qt_android_kit "${_qt_android_kit}" DIRECTORY)
    endforeach()
    get_filename_component(_qt_version_dir "${_qt_android_kit}" DIRECTORY)

    foreach(_qt_host_suffix gcc_64 msvc2022_64 msvc2019_64 mingw_64 clang_64 macos)
        if(EXISTS "${_qt_version_dir}/${_qt_host_suffix}/lib/cmake/Qt6CoreTools/Qt6CoreToolsConfig.cmake")
            set(QT_HOST_PATH "${_qt_version_dir}/${_qt_host_suffix}" CACHE PATH "Path to host Qt installation")
            message(STATUS "自动探测到 host Qt: ${QT_HOST_PATH}")
            break()
        endif()
    endforeach()
    unset(_qt_android_kit)
    unset(_qt_version_dir)
    unset(_qt_host_suffix)
endif()
unset(_qt_toolchain_normalized)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Quick Qml)

# TextToSpeech 用于提醒/统计的语音播报。
find_package(Qt6 REQUIRED COMPONENTS Quick Qml QuickControls2 TextToSpeech)

# QTP0001 NEW：QML 模块资源前缀使用 /qt/qml（默认 QML 导入路径）。
# OLD 行为（资源放在 :/SmartGlasses/）在 Qt 6.11 上找不到 qmldir，
# 会导致 loadFromModule 报 "Module contains no type named Main"。
qt_policy(SET QTP0001 NEW)
find_package(Qt6LinguistTools QUIET)
find_package(Qt5LinguistTools QUIET)

set(TS_FILES yiban_android_app_zh_CN.ts)

set(PROJECT_SOURCES
        ${TS_FILES}
        src/main.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(yiban_android_app
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )

    # qml/ 下所有页面与组件都注册进模块；文件平铺在 qml/ 根，别名回到模块根，
    # 这样既能用 loadFromModule("SmartGlasses", "Main") 启动，也能被同一模块内的
    # 其他 QML 直接按文件名引用（无需 import 子目录）。
    # 注意：QT_RESOURCE_ALIAS 必须在 qt_add_qml_module 之前设置，
    # 否则类型会被注册为 SmartGlasses.qml.Main 而非 SmartGlasses.Main。
    file(GLOB QML_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml")
    foreach(_qml_file ${QML_FILES})
        get_filename_component(_qml_name "${_qml_file}" NAME)
        set_source_files_properties("${_qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${_qml_name}")
    endforeach()
    unset(_qml_file)
    unset(_qml_name)

    qt_add_qml_module(yiban_android_app
        URI SmartGlasses
        VERSION 1.0
        QML_FILES
            ${QML_FILES}
    )

    set_property(TARGET yiban_android_app PROPERTY QT_ANDROID_APP_NAME "一伴")

# Define target properties for Android with Qt 6 as:
    set_property(TARGET yiban_android_app APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation

    if(Qt6LinguistTools_FOUND)
        qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
    endif()
else()
    if(ANDROID)
        add_library(yiban_android_app SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(yiban_android_app
            ${PROJECT_SOURCES}
        )
    endif()

    if(Qt5LinguistTools_FOUND)
        qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
    endif()
endif()

target_link_libraries(yiban_android_app PRIVATE
    Qt${QT_VERSION_MAJOR}::Quick
    Qt${QT_VERSION_MAJOR}::Qml
    Qt${QT_VERSION_MAJOR}::QuickControls2
    Qt${QT_VERSION_MAJOR}::TextToSpeech
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.yiban_android_app)
endif()
set_target_properties(yiban_android_app PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS yiban_android_app
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(yiban_android_app)
endif()

# 可选：APK 构建完成后自动安装到已连接的 Android 设备
option(AUTO_INSTALL_APK "Automatically install the built APK on a connected device after the apk target completes" OFF)

# Qt 6.11 的 CMake 部署目标会生成 unsigned APK；QT_ANDROID_SIGN_APK
# 不是该版本的有效 target property，因此在 APK 目标完成后显式调用 apksigner。
if (ANDROID AND QT_VERSION_MAJOR EQUAL 6)
    # androiddeployqt 产出的最终 APK（Debug 为 debug 签名，Release 在下方签名后覆盖回此路径）
    set(APK_PATH "${CMAKE_BINARY_DIR}/android-build/${PROJECT_NAME}.apk")

    # 发布签名参数。此前 KEYSTORE/STORE_PASS/KEY_PASS/ALIAS 在 CMakeLists 里
    # 只有引用、从未定义，Release 配置一定 FATAL_ERROR。
    # 这份证书是测试用的自签名证书（口令公开无妨），所以直接给默认值；
    # 换证书/口令时用 -DKEYSTORE=... -DALIAS=... -DSTORE_PASS=... 覆盖。
    set(KEYSTORE "${CMAKE_CURRENT_SOURCE_DIR}/android_release.keystore"
        CACHE FILEPATH "Android release keystore (.jks/.keystore)")
    set(ALIAS "smartglasses" CACHE STRING "Android release key alias")
    set(STORE_PASS "smartglasses" CACHE STRING "Android keystore password")
    set(KEY_PASS "" CACHE STRING "Android key password (empty: same as keystore password)")
    if ("${KEY_PASS}" STREQUAL "")
        set(KEY_PASS "${STORE_PASS}")
    endif()

    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        set(SIGNED_APK "${APK_PATH}.signed")
        set(_release_sign_ready TRUE)

        # build-tools 的版本号因机器而异（Windows 上通常没有 37.0.0），
        # 所以列出已安装的所有版本、从新到旧找 apksigner，最后再退回 PATH。
        file(GLOB _build_tools_dirs "${ANDROID_SDK_ROOT}/build-tools/*")
        list(SORT _build_tools_dirs)
        list(REVERSE _build_tools_dirs)
        find_program(APKSIGNER_EXECUTABLE
            NAMES apksigner
            PATHS ${_build_tools_dirs}
            NO_DEFAULT_PATH
        )
        if (NOT APKSIGNER_EXECUTABLE)
            find_program(APKSIGNER_EXECUTABLE NAMES apksigner)
        endif()
        unset(_build_tools_dirs)

        if (NOT APKSIGNER_EXECUTABLE)
            message(WARNING "找不到 apksigner（${ANDROID_SDK_ROOT}/build-tools 和 PATH 里都没有），"
                            "Release APK 将不做发布签名")
            set(_release_sign_ready FALSE)
        elseif (WIN32 AND APKSIGNER_EXECUTABLE MATCHES "\\.bat$")
            # Windows 上 apksigner 是 .bat，CreateProcess 不能直接执行批处理，必须经 cmd /c。
            set(APKSIGNER_COMMAND cmd /c "${APKSIGNER_EXECUTABLE}")
        else()
            set(APKSIGNER_COMMAND "${APKSIGNER_EXECUTABLE}")
        endif()

        if (NOT EXISTS "${KEYSTORE}")
            message(WARNING "找不到发布签名 keystore: ${KEYSTORE}，Release APK 将不做发布签名")
            set(_release_sign_ready FALSE)
        endif()

        if (_release_sign_ready)
            add_custom_command(
                OUTPUT "${SIGNED_APK}"
                DEPENDS "${APK_PATH}"
                COMMAND ${APKSIGNER_COMMAND} sign
                    --ks "${KEYSTORE}"
                    --ks-pass "pass:${STORE_PASS}"
                    --key-pass "pass:${KEY_PASS}"
                    --ks-key-alias "${ALIAS}"
                    --out "${SIGNED_APK}" "${APK_PATH}"
                COMMAND "${CMAKE_COMMAND}" -E copy "${SIGNED_APK}" "${APK_PATH}"
                COMMAND "${CMAKE_COMMAND}" -E rm -f "${SIGNED_APK}"
                COMMENT "Signing Android APK with release keystore: ${APK_PATH}"
                VERBATIM
            )
            add_custom_target(yiban_android_app_sign_apk DEPENDS "${SIGNED_APK}")
            add_dependencies(apk yiban_android_app_sign_apk)
        endif()
        unset(_release_sign_ready)
    endif()

    # 统一在 apk 目标（Debug 打包 / Release 打包+签名）全部完成后再安装。
    # 注意不要挂到可执行目标 yiban_android_app 的 POST_BUILD 上——
    # 那会在 APK 打包完成之前就执行，导致安装的文件不存在或版本过期。
    if (AUTO_INSTALL_APK)
        # CMake 4.x 已移除 `cmake -E adb` 子命令，需直接调用 adb 可执行文件
        find_program(ADB_EXECUTABLE NAMES adb)
        if (NOT ADB_EXECUTABLE)
            message(WARNING "AUTO_INSTALL_APK is enabled but adb was not found in PATH; skipping auto-install")
        else()
            add_custom_command(
                TARGET apk POST_BUILD
                COMMAND "${ADB_EXECUTABLE}" install -r "${APK_PATH}"
                COMMENT "Installing APK on connected device"
            )
        endif()
    endif()
endif()

if (ANDROID)
    # 开启 Release 高度优化
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

    # 强制为 ARM64 启用 FP16 和 DotProduct 指令集支持
    if (${ANDROID_ABI} STREQUAL "arm64-v8a")
        add_compile_options(-march=armv8.2-a+fp16+dotprod)
        add_definitions(-DGGML_USE_NEON)
    endif()
endif()

# ----------------------------------------------------------------------------
# 单元测试
# ----------------------------------------------------------------------------
# 只在非 Android 构建中生成：测试目标是给构建机的 host Qt 跑的桌面程序，
# 不需要 Android 设备或 NDK，因此 APK 构建完全不受影响。
# 本仓库当前唯一可离线验证的逻辑是 sentencesplitter（纯自由函数，
# 不依赖 QObject / 信号槽 / 设备）——详见 tests/CMakeLists.txt。
include(CTest)
if (BUILD_TESTING AND NOT ANDROID)
    add_subdirectory(tests)
endif()