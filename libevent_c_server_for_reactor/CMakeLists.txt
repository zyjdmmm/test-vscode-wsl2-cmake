project (libevent_c_server_for_reactor)


#-------------------------------------------#
# 查找系统库
# 
#-------------------------------------------#
# 注意：pkg_check_modules是必须的(qt中用findxxxmodules.cmake已经提前写好了)
# 依赖 pkg-config 工具（pkg-config.exe这个查找包的工具通常由mingw64自带）
find_package(PkgConfig REQUIRED) 


# 注意：不能写多条pkg_check_modules，否则会覆盖之前的结果，必须一次性列出所有模块以免被覆盖
pkg_check_modules(${PROJECT_NAME}_DEPS REQUIRED # 在这里添加需要的系统库
    cairo
    gtk+-3.0
    glib-2.0
    openssl
    libevent
    libevent_pthreads
    )

#-------------------------------------------#
# 链接系统库
# 
#-------------------------------------------#

# 创建可执行文件目标(必须先于target_link_libraries生成)
add_executable(${PROJECT_NAME} main.cpp)

# 配置头文件目录（自动提取 GTK3 和 Cairo 的所有头文件路径，无需手动写）
target_include_directories(${PROJECT_NAME} PRIVATE ${${PROJECT_NAME}_DEPS_INCLUDE_DIRS})

# 链接核心库及所有依赖（自动链接 GTK3、Cairo 及其附属库，无需单独配置）
target_link_libraries(${PROJECT_NAME} PRIVATE ${${PROJECT_NAME}_DEPS_LIBRARIES})


##-------------------------------------------#
## 编译完成后无脑自动拷贝本项目中资源文件夹resource到exe的同级目录
## 注意：这个方法更好，有些老方法编译完成后执行(如:POST_BUILD),如果你的代码没有修改(或许你此时改了resource下的资源文件如.css)不会触发拷贝,这就很坑
##
##-------------------------------------------#
#
## 如果要修改resource目录名称，修改以下两行即可
#set(RESOURCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resource(auto_copy_after_compile_by_cmake)")# 源目录
#set(RESOURCE_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/resource(auto_copy_after_compile_by_cmake)")#目的目录
#
## 你可以把这个当成一个新项目CopyResource，你可以看到vscode的cmake插件侧边显示这是“实用工具”(不是真的项目，只是个用于拷贝的工具而已)
#add_custom_target(
#    ${PROJECT_NAME}_CopyResource                  # 自定义工具名称，注意：CopyResource必须加上前缀${PROJECT_NAME}_，因为cmake要求所有子项目不能存在多个同名CopyResource工具，你真的是有那个大病cmake,这不相当于子项目污染全局了么
#    ALL                           				  # 标记为ALL目标，执行`cmake --build .`时自动构建，不建议删除--AI
#    VERBATIM                      				  # VERBATIM是现代 CMake 跨平台配置的 “最佳实践”，不建议删除--AI
#    
#    COMMAND ${CMAKE_COMMAND} -E remove_directory "${RESOURCE_TARGET_DIR}"# 先删除旧的目的目录
#    COMMAND ${CMAKE_COMMAND} -E copy_directory "${RESOURCE_SOURCE_DIR}" "${RESOURCE_TARGET_DIR}"# 再拷贝源目录
#)
#
## 新项目CopyResource成为${PROJECT_NAME}（EXE）的依赖，必须先构建CopyResource（拷贝资源），无论任何情况
#add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_CopyResource)
