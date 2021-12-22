# Universal build for Apple Silicon
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")

project(mac-dal-plugin)

find_library(AVFOUNDATION AVFoundation)
find_library(COCOA Cocoa)
find_library(COREFOUNDATION CoreFoundation)
find_library(COREMEDIA CoreMedia)
find_library(COREVIDEO CoreVideo)
find_library(COCOA Cocoa)
find_library(COREMEDIAIO CoreMediaIO)
find_library(IOSURFACE IOSurface)
find_library(IOKIT IOKit)

# Possible we could remove osme of these
include_directories(${AVFOUNDATION}
					${COCOA}
					${COREFOUNDATION}
					${COREMEDIA}
					${COREVIDEO}
					${COREMEDIAIO}
					${COCOA}
					${IOSURFACE}
					./
					../common)

set(mac-dal-plugin_HEADERS
	Defines.h
	Logging.h
	LONGDALPlugInInterface.h
    LONGDALObjectStore.h
	LONGDALPlugIn.h
	LONGDALDevice.h
    LONGDALStream.h
	CMSampleBufferUtils.h
	LONGDALMachClient.h
	../common/MachProtocol.h)

set(mac-dal-plugin_SOURCES
	LONGDALPlugInMain.mm
	LONGDALPlugInInterface.mm
	LONGDALObjectStore.mm
	LONGDALPlugIn.mm
	LONGDALDevice.mm
    LONGDALStream.mm
	CMSampleBufferUtils.mm
	LONGDALMachClient.mm)

add_library(mac-dal-plugin MODULE
	${mac-dal-plugin_SOURCES}
	${mac-dal-plugin_HEADERS})

set_target_properties(mac-dal-plugin PROPERTIES
	FOLDER "plugins"
	BUNDLE TRUE
	OUTPUT_NAME "vizard-mac-virtualcam"
	COMPILE_FLAGS "-std=gnu++14 -stdlib=libc++ -fobjc-arc -fobjc-weak")

if (XCODE)
	set(TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/Debug")
else (XCODE)
	set(TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}")
endif (XCODE)

target_link_libraries(mac-dal-plugin
	${AVFOUNDATION}
	${COCOA}
	${COREFOUNDATION}
	${COREMEDIA}
	${COREVIDEO}
	${COREMEDIAIO}
	${IOSURFACE}
	${IOKIT})

add_custom_command(TARGET mac-dal-plugin
	POST_BUILD
	COMMAND rm -rf ${TARGET_DIR}/vizard-mac-virtualcam.plugin || true
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${TARGET_DIR}/vizard-mac-virtualcam.bundle ${TARGET_DIR}/vizard-mac-virtualcam.plugin
	COMMENT "Rename bundle to plugin"
)

# Note: Xcode seems to run a command `builtin-infoPlistUtility` to generate the Info.plist, but I'm
# not sure where to find that binary. If we had access to it, the command would look something like:
# builtin-infoPlistUtility ${PROJECT_SOURCE_DIR}/../common/CoreMediaIO/DeviceAbstractionLayer/Devices/Sample/PlugIn/SampleVCam-Info.plist -producttype com.apple.product-type.bundle -expandbuildsettings -platform macosx -o mac-virtualcam.bundle/Contents/Info.plist
# Instead, just copy in one that was already generated from Xcode.
add_custom_command(TARGET mac-dal-plugin
	POST_BUILD
	COMMAND cp ${PROJECT_SOURCE_DIR}/Info.plist ${TARGET_DIR}/vizard-mac-virtualcam.plugin/Contents/Info.plist
    COMMAND mkdir ${TARGET_DIR}/vizard-mac-virtualcam.plugin/Contents/Resources
    COMMAND cp ${PROJECT_SOURCE_DIR}/placeholder.png ${TARGET_DIR}/vizard-mac-virtualcam.plugin/Contents/Resources/placeholder.png
	COMMAND /usr/bin/plutil -insert CFBundleVersion -string "2" ${TARGET_DIR}/vizard-mac-virtualcam.plugin/Contents/Info.plist
	COMMAND /usr/bin/plutil -insert CFBundleShortVersionString -string "${OBS_VERSION}" ${TARGET_DIR}/vizard-mac-virtualcam.plugin/Contents/Info.plist
	DEPENDS {PROJECT_SOURCE_DIR}/Info.plist
	COMMENT "Copy in Info.plist"
)

add_custom_command(TARGET mac-dal-plugin
	POST_BUILD
	COMMAND /usr/bin/codesign --force --deep --sign - --timestamp=none ${TARGET_DIR}/vizard-mac-virtualcam.plugin
	COMMENT "Codesign plugin"
)

add_custom_command(TARGET mac-dal-plugin
	POST_BUILD
	COMMAND rm -rf "${OBS_OUTPUT_DIR}/$<CONFIGURATION>/data/vizard-mac-virtualcam.plugin" || true
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${TARGET_DIR}/vizard-mac-virtualcam.plugin "${OBS_OUTPUT_DIR}/$<CONFIGURATION>/data/obs-plugins/mac-virtualcam/vizard-mac-virtualcam.plugin"
	COMMENT "Copy plugin to destination"
)