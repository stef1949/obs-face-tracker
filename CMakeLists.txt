cmake_minimum_required(VERSION 3.12)

project(obs-face-tracker VERSION 0.9.1)
set(PLUGIN_AUTHOR "Norihiro Kamae")
set(MACOS_BUNDLEID "net.nagater.obs-face-tracker")
set(MACOS_PACKAGE_UUID "A2EB2F73-9828-40C4-A307-F47282BB8D3F")
set(MACOS_INSTALLER_UUID "652CF739-5A6F-4DAC-9611-B068FAC15BF3")
set(PLUGIN_URL "https://obsproject.com/forum/resources/face-tracker.1294/")
set(LINUX_MAINTAINER_EMAIL "norihiro@nagater.net")

option(WITH_PTZ_TCP "Enable to connect PTZ camera through TCP socket" ON)
option(WITH_DLIB_SUBMODULE "Link dlib from submodule" ON)
option(ENABLE_MONITOR_USER "Enable monitor source for user" OFF)
option(ENABLE_DEBUG_DATA "Enable property to save error and control data" OFF)
option(WITH_DOCK "Enable dock" ON)
option(ENABLE_DATAGEN "Enable generating data" OFF)
option(ENABLE_CUDA "Enable CUDA/cuDNN acceleration for the dlib CNN detector" OFF)
option(ENABLE_YUNET "Enable the YuNet detector through ONNX Runtime" OFF)
option(ENABLE_SCRFD "Enable the SCRFD detector through ONNX Runtime" OFF)
option(ENABLE_ONNXRUNTIME_CUDA "Require the ONNX Runtime CUDA provider on Windows" OFF)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)
set(CUDNN_RUNTIME_DIR "" CACHE PATH "Directory containing cuDNN runtime DLLs to package on Windows")
set(CUDA_RUNTIME_DIRS "" CACHE STRING "Semicolon-separated CUDA runtime DLL directories to package on Windows")
set(ONNXRUNTIME_ROOT "" CACHE PATH "ONNX Runtime SDK or extracted NuGet package root")
set(ONNXRUNTIME_RUNTIME_DIR "" CACHE PATH "Directory containing ONNX Runtime shared libraries to package")

include(CTest)

set(CMAKE_PREFIX_PATH "${QTDIR}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(ENABLE_CUDA)
	set(CUDA_USE_STATIC_CUDA_RUNTIME OFF CACHE BOOL "Use the CUDA runtime DLL" FORCE)
	# dlib 19.24's probe hardcodes sm_50, which CUDA 13 removed. The real
	# dlib target below is still compiled for the requested architecture and
	# therefore remains the authoritative compatibility check.
	if(POLICY CMP0146)
		cmake_policy(SET CMP0146 OLD)
	endif()
	find_package(CUDA 7.5 REQUIRED)
	if(CUDA_VERSION VERSION_GREATER_EQUAL 13.0)
		set(cuda_test_compile_worked ON CACHE BOOL "CUDA 13 compatibility probe" FORCE)
		set(cudnn_test_compile_worked ON CACHE BOOL "cuDNN compatibility probe" FORCE)
		message(STATUS "Skipping dlib's legacy sm_50 CUDA probe for CUDA ${CUDA_VERSION}")
	endif()
endif()

set(plugin_additional_libs)
set(plugin_additional_incs)

if (WITH_PTZ_TCP)
	set(CMAKE_POSITION_INDEPENDENT_CODE true)
	set(WITH_VISCA_SERIAL OFF)
	add_definitions("-DVISCA_API= ")
	add_subdirectory(libvisca/visca)
	set(plugin_additional_libs ${plugin_additional_libs} visca)
	set(plugin_additional_incs ${plugin_additional_incs} libvisca/visca)
endif()

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
include(cmake/ObsPluginHelpers.cmake)
find_qt(VERSION ${QT_VERSION} COMPONENTS Widgets Core Gui)

if (WITH_DLIB_SUBMODULE)
	set(CMAKE_POSITION_INDEPENDENT_CODE True)
	set(DLIB_USE_CUDA ${ENABLE_CUDA} CACHE BOOL "Build dlib with CUDA support" FORCE)
	set(DLIB_NO_GUI_SUPPORT ON)
	set(DLIB_PNG_SUPPORT OFF)
	set(DLIB_GIF_SUPPORT OFF)
	set(DLIB_USE_FFMPEG OFF)
	set(DLIB_JPEG_SUPPORT OFF)
	set(DLIB_LINK_WITH_SQLITE3 OFF)
	set(DLIB_WEBP_SUPPORT OFF)
	set(DLIB_JXL_SUPPORT OFF)
	set(DLIB_TEST_COMPILE_ALL_SOURCE_CPP OFF)
	add_subdirectory(dlib)
else()
	find_package(dlib REQUIRED)
	add_library(dlib ALIAS dlib::dlib)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

configure_file(
	src/plugin-macros.h.in
	plugin-macros.generated.h
)
configure_file(
	installer/installer-Windows.iss.in
	installer-Windows.generated.iss
)

configure_file(
	ci/ci_includes.sh.in
	ci/ci_includes.generated.sh
)
configure_file(
	ci/ci_includes.cmd.in
	ci/ci_includes.generated.cmd
)

set(PLUGIN_SOURCES
	src/module-main.c
	src/face-tracker.cpp
	src/source_list.cc
	src/face-tracker-preset.cpp
	src/face-tracker-manager.cpp
	src/face-tracker-ptz.cpp
	src/face-tracker-monitor.cpp
	src/face-detector-base.cpp
	src/face-detector-dlib-hog.cpp
	src/face-detector-dlib-cnn.cpp
	src/face-tracker-base.cpp
	src/face-tracker-dlib.cpp
	src/texture-object.cpp
	src/helper.cpp
	src/ptz-backend.cpp
	src/obsptz-backend.cpp
	src/dummy-backend.cpp
)

if(ENABLE_YUNET OR ENABLE_SCRFD)
	find_path(ONNXRUNTIME_INCLUDE_DIR
		onnxruntime_cxx_api.h
		PATHS "${ONNXRUNTIME_ROOT}"
		PATH_SUFFIXES include build/native/include
		REQUIRED
	)
	find_library(ONNXRUNTIME_LIBRARY
		NAMES onnxruntime
		PATHS "${ONNXRUNTIME_ROOT}"
		PATH_SUFFIXES lib lib64 runtimes/win-x64/native
		REQUIRED
	)
	if(ENABLE_YUNET)
		list(APPEND PLUGIN_SOURCES src/face-detector-yunet.cpp)
	endif()
	if(ENABLE_SCRFD)
		list(APPEND PLUGIN_SOURCES src/face-detector-scrfd.cpp)
	endif()
	if(OS_WINDOWS AND NOT ONNXRUNTIME_RUNTIME_DIR)
		if(EXISTS "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
			set(ONNXRUNTIME_RUNTIME_DIR "${ONNXRUNTIME_ROOT}/lib")
		else()
			set(ONNXRUNTIME_RUNTIME_DIR "${ONNXRUNTIME_ROOT}/runtimes/win-x64/native")
		endif()
	endif()
endif()

if (WITH_PTZ_TCP)
	set(PLUGIN_SOURCES ${PLUGIN_SOURCES} src/libvisca-thread.cpp)
endif()
if (WITH_DOCK)
	set(PLUGIN_SOURCES
		${PLUGIN_SOURCES}
		ui/face-tracker-dock.cpp
		ui/face-tracker-widget.cpp
	)
endif()

add_library(${CMAKE_PROJECT_NAME} MODULE ${PLUGIN_SOURCES})

if (WITH_DOCK)
	set(plugin_additional_incs ${plugin_additional_incs} src)
	set(plugin_additional_libs ${plugin_additional_libs} Qt::Gui Qt::Widgets Qt::GuiPrivate)
endif()

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/dlib
	${plugin_additional_incs}
)

target_link_libraries(${CMAKE_PROJECT_NAME}
	OBS::libobs
	OBS::obs-frontend-api
	dlib
	${plugin_additional_libs}
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}
)

if(ENABLE_YUNET OR ENABLE_SCRFD)
	target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
	target_link_libraries(${CMAKE_PROJECT_NAME} ${ONNXRUNTIME_LIBRARY})
	if(ENABLE_YUNET)
		target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_YUNET=1)
	endif()
	if(ENABLE_SCRFD)
		target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_SCRFD=1)
	endif()
	if(ENABLE_ONNXRUNTIME_CUDA)
		target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_ONNXRUNTIME_CUDA=1)
	endif()
endif()

if(OS_WINDOWS)
	# Enable Multicore Builds and disable FH4 (to not depend on VCRUNTIME140_1.DLL when building with VS2019)
	if (MSVC)
		add_definitions(/MP /d2FH4-)
		add_definitions("-D_USE_MATH_DEFINES")
		add_definitions("-D_CRT_SECURE_NO_WARNINGS") # to avoid a warning for `fopen`
		add_definitions("-D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR") # TODO: Remove once requiring OBS 30.2 or later.
	endif()

	target_link_libraries(${CMAKE_PROJECT_NAME} OBS::w32-pthreads)
endif()

if(OS_LINUX)
	target_compile_options(${CMAKE_PROJECT_NAME}
		PRIVATE
		-Wall
		-Wextra
		-Wunused-function
		-Werror
		-Wno-error=deprecated-declarations # TODO: Replace obs_frontend_add_dock with the new API.
	)
	target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "-Wl,-z,defs")
endif()

if(APPLE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -fvisibility=default")

	set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES PREFIX "")
	set(MACOSX_PLUGIN_GUI_IDENTIFIER "${MACOS_BUNDLEID}")
	set(MACOSX_PLUGIN_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}")
	set(MACOSX_PLUGIN_SHORT_VERSION_STRING "1")
endif()

setup_plugin_target(${CMAKE_PROJECT_NAME})

if(OS_WINDOWS AND (ENABLE_CUDA OR ENABLE_ONNXRUNTIME_CUDA) AND CUDNN_RUNTIME_DIR)
	file(GLOB CUDNN_RUNTIME_DLLS CONFIGURE_DEPENDS "${CUDNN_RUNTIME_DIR}/cudnn*.dll")
	if(NOT CUDNN_RUNTIME_DLLS)
		message(FATAL_ERROR "CUDNN_RUNTIME_DIR does not contain cudnn*.dll files: ${CUDNN_RUNTIME_DIR}")
	endif()
	install(
		FILES ${CUDNN_RUNTIME_DLLS}
		DESTINATION ${OBS_PLUGIN_DESTINATION}
		COMPONENT ${CMAKE_PROJECT_NAME}_Runtime
	)
	install(
		FILES ${CUDNN_RUNTIME_DLLS}
		DESTINATION $<CONFIG>/${OBS_PLUGIN_DESTINATION}
		COMPONENT obs_rundir
		EXCLUDE_FROM_ALL
	)
endif()

if(OS_WINDOWS AND ENABLE_ONNXRUNTIME_CUDA AND CUDA_RUNTIME_DIRS)
	set(CUDA_RUNTIME_DLLS)
	cmake_path(CONVERT "${CUDA_RUNTIME_DIRS}" TO_CMAKE_PATH_LIST CUDA_RUNTIME_PATHS NORMALIZE)
	foreach(CUDA_RUNTIME_DIR IN LISTS CUDA_RUNTIME_PATHS)
		file(GLOB CUDA_RUNTIME_DIR_DLLS CONFIGURE_DEPENDS
			"${CUDA_RUNTIME_DIR}/cublas*.dll"
			"${CUDA_RUNTIME_DIR}/cudart*.dll"
			"${CUDA_RUNTIME_DIR}/cufft*.dll")
		list(APPEND CUDA_RUNTIME_DLLS ${CUDA_RUNTIME_DIR_DLLS})
	endforeach()
	list(REMOVE_DUPLICATES CUDA_RUNTIME_DLLS)
	if(NOT CUDA_RUNTIME_DLLS)
		message(FATAL_ERROR "CUDA_RUNTIME_DIRS does not contain CUDA runtime DLLs: ${CUDA_RUNTIME_DIRS}")
	endif()
	install(
		FILES ${CUDA_RUNTIME_DLLS}
		DESTINATION ${OBS_PLUGIN_DESTINATION}
		COMPONENT ${CMAKE_PROJECT_NAME}_Runtime
	)
	install(
		FILES ${CUDA_RUNTIME_DLLS}
		DESTINATION $<CONFIG>/${OBS_PLUGIN_DESTINATION}
		COMPONENT obs_rundir
		EXCLUDE_FROM_ALL
	)
endif()

if(OS_WINDOWS AND (ENABLE_YUNET OR ENABLE_SCRFD) AND ONNXRUNTIME_RUNTIME_DIR)
	set(ONNXRUNTIME_RUNTIME_DLLS "${ONNXRUNTIME_RUNTIME_DIR}/onnxruntime.dll")
	if(EXISTS "${ONNXRUNTIME_RUNTIME_DIR}/onnxruntime_providers_shared.dll")
		list(APPEND ONNXRUNTIME_RUNTIME_DLLS "${ONNXRUNTIME_RUNTIME_DIR}/onnxruntime_providers_shared.dll")
	endif()
	if(NOT EXISTS "${ONNXRUNTIME_RUNTIME_DIR}/onnxruntime.dll")
		message(FATAL_ERROR "ONNXRUNTIME_RUNTIME_DIR does not contain onnxruntime.dll: ${ONNXRUNTIME_RUNTIME_DIR}")
	endif()
	if(ENABLE_ONNXRUNTIME_CUDA)
		if(NOT EXISTS "${ONNXRUNTIME_RUNTIME_DIR}/onnxruntime_providers_cuda.dll")
			message(FATAL_ERROR
				"ENABLE_ONNXRUNTIME_CUDA requires onnxruntime_providers_cuda.dll in ONNXRUNTIME_RUNTIME_DIR")
		endif()
		list(APPEND ONNXRUNTIME_RUNTIME_DLLS "${ONNXRUNTIME_RUNTIME_DIR}/onnxruntime_providers_cuda.dll")
	endif()
	install(
		FILES ${ONNXRUNTIME_RUNTIME_DLLS}
		DESTINATION ${OBS_PLUGIN_DESTINATION}
		COMPONENT ${CMAKE_PROJECT_NAME}_Runtime
	)
	install(
		FILES ${ONNXRUNTIME_RUNTIME_DLLS}
		DESTINATION $<CONFIG>/${OBS_PLUGIN_DESTINATION}
		COMPONENT obs_rundir
		EXCLUDE_FROM_ALL
	)
endif()

if(ENABLE_YUNET OR ENABLE_SCRFD)
	foreach(ONNXRUNTIME_NOTICE IN ITEMS LICENSE ThirdPartyNotices.txt)
		if(EXISTS "${ONNXRUNTIME_ROOT}/${ONNXRUNTIME_NOTICE}")
			install(
				FILES "${ONNXRUNTIME_ROOT}/${ONNXRUNTIME_NOTICE}"
				DESTINATION ${OBS_DATA_DESTINATION}/obs-plugins/${CMAKE_PROJECT_NAME}
				RENAME "${ONNXRUNTIME_NOTICE}.onnxruntime"
				COMPONENT ${CMAKE_PROJECT_NAME}_Runtime
			)
			install(
				FILES "${ONNXRUNTIME_ROOT}/${ONNXRUNTIME_NOTICE}"
				DESTINATION $<CONFIG>/${OBS_DATA_DESTINATION}/obs-plugins/${CMAKE_PROJECT_NAME}
				RENAME "${ONNXRUNTIME_NOTICE}.onnxruntime"
				COMPONENT obs_rundir
				EXCLUDE_FROM_ALL
			)
		endif()
	endforeach()
endif()

if(BUILD_TESTING)
	add_executable(${CMAKE_PROJECT_NAME}-tests tests/tracking-utils-test.cpp)
	target_include_directories(${CMAKE_PROJECT_NAME}-tests PRIVATE src)
	add_test(NAME ${CMAKE_PROJECT_NAME}-tracking-utils COMMAND ${CMAKE_PROJECT_NAME}-tests)
	if(ENABLE_YUNET)
		add_executable(${CMAKE_PROJECT_NAME}-yunet-model-test tests/yunet-model-test.cpp)
		target_include_directories(${CMAKE_PROJECT_NAME}-yunet-model-test PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
		target_link_libraries(${CMAKE_PROJECT_NAME}-yunet-model-test PRIVATE ${ONNXRUNTIME_LIBRARY})
		add_test(
			NAME ${CMAKE_PROJECT_NAME}-yunet-model
			COMMAND ${CMAKE_PROJECT_NAME}-yunet-model-test
				"${CMAKE_CURRENT_SOURCE_DIR}/data/yunet_model/face_detection_yunet_2026may.onnx"
		)
		if(OS_WINDOWS)
			add_custom_command(
				TARGET ${CMAKE_PROJECT_NAME}-yunet-model-test POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_RUNTIME_DLLS}
					$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}-yunet-model-test>
			)
		endif()
	endif()
	if(ENABLE_SCRFD AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/scrfd_model/scrfd_2.5g_bnkps.onnx")
		add_executable(${CMAKE_PROJECT_NAME}-scrfd-model-test tests/scrfd-model-test.cpp)
		target_include_directories(${CMAKE_PROJECT_NAME}-scrfd-model-test PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
		target_link_libraries(${CMAKE_PROJECT_NAME}-scrfd-model-test PRIVATE ${ONNXRUNTIME_LIBRARY})
		add_test(
			NAME ${CMAKE_PROJECT_NAME}-scrfd-model
			COMMAND ${CMAKE_PROJECT_NAME}-scrfd-model-test
				"${CMAKE_CURRENT_SOURCE_DIR}/data/scrfd_model/scrfd_2.5g_bnkps.onnx"
		)
		if(OS_WINDOWS)
			add_custom_command(
				TARGET ${CMAKE_PROJECT_NAME}-scrfd-model-test POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_RUNTIME_DLLS}
					$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}-scrfd-model-test>
			)
		endif()
	endif()
endif()

if(ENABLE_SANITIZERS AND ENABLE_THREAD_SANITIZER)
	message(FATAL_ERROR "ENABLE_SANITIZERS and ENABLE_THREAD_SANITIZER cannot be enabled together")
endif()

set(SANITIZER_TARGETS ${CMAKE_PROJECT_NAME})
if(BUILD_TESTING)
	list(APPEND SANITIZER_TARGETS ${CMAKE_PROJECT_NAME}-tests)
	if(ENABLE_YUNET)
		list(APPEND SANITIZER_TARGETS ${CMAKE_PROJECT_NAME}-yunet-model-test)
	endif()
	if(TARGET ${CMAKE_PROJECT_NAME}-scrfd-model-test)
		list(APPEND SANITIZER_TARGETS ${CMAKE_PROJECT_NAME}-scrfd-model-test)
	endif()
endif()
if(ENABLE_SANITIZERS AND NOT MSVC)
	foreach(target IN LISTS SANITIZER_TARGETS)
		target_compile_options(${target} PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)
		target_link_options(${target} PRIVATE -fsanitize=address,undefined)
	endforeach()
elseif(ENABLE_THREAD_SANITIZER AND NOT MSVC)
	foreach(target IN LISTS SANITIZER_TARGETS)
		target_compile_options(${target} PRIVATE -fno-omit-frame-pointer -fsanitize=thread)
		target_link_options(${target} PRIVATE -fsanitize=thread)
	endforeach()
endif()

configure_file(installer/installer-macOS.pkgproj.in installer-macOS.generated.pkgproj)

if(ENABLE_DATAGEN)
	add_executable(face-detector-dlib-hog-datagen
		src/face-detector-dlib-hog-datagen.cpp
	)
	target_include_directories(face-detector-dlib-hog-datagen PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}
	)
	target_link_libraries(face-detector-dlib-hog-datagen
		dlib
	)
endif()
