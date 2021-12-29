project(custom-source)

set(custom-source_SOURCES
		custom-source.c)

if(WIN32)
	set(MODULE_DESCRIPTION "OBS custom module")
	configure_file(${CMAKE_SOURCE_DIR}/cmake/winrc/obs-module.rc.in custom-source.rc)
	list(APPEND custom-source_SOURCES
			custom-source.rc)
endif()

add_library(custom-source MODULE
	${custom-source_SOURCES})
target_link_libraries(custom-source
	libobs)
set_target_properties(custom-source PROPERTIES FOLDER "plugins")
install_obs_plugin(custom-source)
