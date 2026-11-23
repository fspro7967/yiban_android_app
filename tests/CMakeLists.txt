# 端上 facade 的离线单测：只依赖 host Qt，不需要 Android 设备。
#
# 这些测试锁的是"接口形状 + 状态机"，不是 UI 像素：
#   * occurrenceId / planId 编码规则
#   * 确认、推迟（3 次上限）、撤销、升级的迁移与错误码
#   * rule 校验与 createPlan/deletePlan 的返回值
#   * 事件日志：重复点按同一状态不会重复记账
# 后端（B1）替换 AppCore 内部实现后，这些用例必须原样通过。

find_package(Qt6 REQUIRED COMPONENTS Core Qml Test Sql)

qt_add_executable(tst_app_core
    tst_app_core.cpp
    ${CMAKE_SOURCE_DIR}/src/vm/app_core.cpp
    ${CMAKE_SOURCE_DIR}/src/vm/app_core.h
    ${CMAKE_SOURCE_DIR}/src/vm/profile_vm.cpp
    ${CMAKE_SOURCE_DIR}/src/vm/profile_vm.h
)

target_include_directories(tst_app_core PRIVATE ${CMAKE_SOURCE_DIR}/src/vm)
target_link_libraries(tst_app_core PRIVATE Qt6::Core Qt6::Qml Qt6::Test Qt6::Sql)

add_test(NAME facade_app_core COMMAND tst_app_core)
# 用构建目录里的独立库文件，别碰用户 %APPDATA% 下的真实数据；
# 每条用例自己会 resetStorage()，但文件位置仍要隔离。
set_tests_properties(facade_app_core PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;YIBAN_APP_DB=${CMAKE_CURRENT_BINARY_DIR}/tst_app_core.sqlite")

# 管理后台"测试连接"的探测逻辑：用本机假服务器覆盖各种应答，不需要外网。
find_package(Qt6 REQUIRED COMPONENTS Network)

qt_add_executable(tst_connection_probe
    tst_connection_probe.cpp
    ${CMAKE_SOURCE_DIR}/src/comm/connection_probe.cpp
    ${CMAKE_SOURCE_DIR}/src/comm/connection_probe.h
)

target_include_directories(tst_connection_probe PRIVATE ${CMAKE_SOURCE_DIR}/src/comm)
target_link_libraries(tst_connection_probe PRIVATE Qt6::Core Qt6::Network Qt6::Test)

add_test(NAME connection_probe COMMAND tst_connection_probe)
set_tests_properties(connection_probe PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# 外设板链路的分行与解析（一行一条 JSON）：只测 DeviceLink 基类，不需要蓝牙。
qt_add_executable(tst_device_link
    tst_device_link.cpp
    ${CMAKE_SOURCE_DIR}/src/comm/device_link.cpp
    ${CMAKE_SOURCE_DIR}/src/comm/device_link.h
)

target_include_directories(tst_device_link PRIVATE ${CMAKE_SOURCE_DIR}/src/comm)
target_link_libraries(tst_device_link PRIVATE Qt6::Core Qt6::Test)

add_test(NAME device_link COMMAND tst_device_link)
set_tests_properties(device_link PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# B2：平台层与设置持久化。桌面构建下平台层必须是安全空实现，
# 这条用例就是防止某次改动把它变成"在 PC 上直接崩"。
qt_add_executable(tst_platform
    tst_platform.cpp
    ${CMAKE_SOURCE_DIR}/src/comm/app_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/comm/app_settings.h
    ${CMAKE_SOURCE_DIR}/src/platform/platform.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/platform.h
)

target_include_directories(tst_platform PRIVATE
    ${CMAKE_SOURCE_DIR}/src/comm
    ${CMAKE_SOURCE_DIR}/src/platform
)
target_link_libraries(tst_platform PRIVATE Qt6::Core Qt6::Test)

add_test(NAME platform_bridge COMMAND tst_platform)

# B2：同步链路（SyncVM + AppCore 队列接口）。假 HttpClient 回放 canned
# 响应，不联网；锁的是规范 §8/§9 的上传/拉取/游标语义。
qt_add_executable(tst_sync
    tst_sync.cpp
    ${CMAKE_SOURCE_DIR}/src/vm/app_core.cpp
    ${CMAKE_SOURCE_DIR}/src/vm/app_core.h
    ${CMAKE_SOURCE_DIR}/src/vm/sync_vm.cpp
    ${CMAKE_SOURCE_DIR}/src/vm/sync_vm.h
    ${CMAKE_SOURCE_DIR}/src/vm/profile_vm.cpp
    ${CMAKE_SOURCE_DIR}/src/vm/profile_vm.h
    ${CMAKE_SOURCE_DIR}/src/vm/facade_errors.h
    ${CMAKE_SOURCE_DIR}/src/comm/app_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/comm/app_settings.h
    ${CMAKE_SOURCE_DIR}/src/comm/http_client.cpp
    ${CMAKE_SOURCE_DIR}/src/comm/http_client.h
)

target_include_directories(tst_sync PRIVATE
    ${CMAKE_SOURCE_DIR}/src/vm
    ${CMAKE_SOURCE_DIR}/src/comm
)
target_link_libraries(tst_sync PRIVATE Qt6::Core Qt6::Qml Qt6::Network Qt6::Test Qt6::Sql)

add_test(NAME sync_flow COMMAND tst_sync)
# AppCore 落 SQLite 之后（S1）同步测试也会开库：同样隔离到构建目录。
set_tests_properties(sync_flow PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;YIBAN_APP_DB=${CMAKE_CURRENT_BINARY_DIR}/tst_sync.sqlite")
