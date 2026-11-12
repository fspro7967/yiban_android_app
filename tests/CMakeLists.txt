# 端上 facade 的离线单测：只依赖 host Qt，不需要 Android 设备。
#
# 这些测试锁的是"接口形状 + 状态机"，不是 UI 像素：
#   * occurrenceId / planId 编码规则
#   * 确认、推迟（3 次上限）、撤销、升级的迁移与错误码
#   * rule 校验与 createPlan/deletePlan 的返回值
#   * 事件日志：重复点按同一状态不会重复记账
# 后端（B1）替换 AppCore 内部实现后，这些用例必须原样通过。

find_package(Qt6 REQUIRED COMPONENTS Core Qml Test)

qt_add_executable(tst_app_core
    tst_app_core.cpp
    ${CMAKE_SOURCE_DIR}/src/vm/app_core.cpp
    ${CMAKE_SOURCE_DIR}/src/vm/app_core.h
    ${CMAKE_SOURCE_DIR}/src/vm/profile_vm.cpp
    ${CMAKE_SOURCE_DIR}/src/vm/profile_vm.h
)

target_include_directories(tst_app_core PRIVATE ${CMAKE_SOURCE_DIR}/src/vm)
target_link_libraries(tst_app_core PRIVATE Qt6::Core Qt6::Qml Qt6::Test)

add_test(NAME facade_app_core COMMAND tst_app_core)
set_tests_properties(facade_app_core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
