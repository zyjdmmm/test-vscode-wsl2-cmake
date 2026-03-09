cmake_minimum_required (VERSION 3.10)

project(test-vscode-wsl2-cmake)



#-------------------------------------------#
# 默认选择RelWithDebInfo模式(因为如果用户忘记选择，cmake会可能随机选择Debug或Release,在不同的编译器不确定)

# 这样小白用户手动编译忘记选择的情况下至少能保证调试
# 如果是clion或者vscode会传递Debug/Debug/RelWithDebInfo/RelWithDebInfo会覆盖这里的值，这样就能外部指定

#有如下四种编译CMakeLists.txt的方法:
# cmake -B build -DCMAKE_BUILD_TYPE=Debug
# cmake -B build -DCMAKE_BUILD_TYPE=Release
# cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
# cmake -B build -DCMAKE_BUILD_TYPE=MinSizeRel
#-------------------------------------------#
if(NOT CMAKE_BUILD_TYPE)
    # set default build type to RelWithDebInfo, we never supported multi config generators, therefore this is fine
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()




# 一定要放在最后面，因为前面的设置放后面可能导致子项目先编译但是配置还没生效
add_subdirectory(tinyhttpd)
add_subdirectory(tradictional_c_pthread_socket_server)
add_subdirectory(tradictional_c_pthread_socket_client)
add_subdirectory(libevent_c_server)






