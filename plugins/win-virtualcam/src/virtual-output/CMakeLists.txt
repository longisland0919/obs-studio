cmake_minimum_required(VERSION 3.5)
project(obs-virtualoutput)

find_package(FFmpeg COMPONENTS avutil avfilter REQUIRED)
	
set(virtualoutput_SOURCES
	virtual_main.cpp
	virtualoutput.rc
	virtual_output.cpp
#	virtual_properties.cpp
#	virtual_filter.cpp
	hflip.cpp
	../queue/share_queue_write.cpp
	)

set(virtualoutput_HEADERS
	get_format.h
	virtual_output.h
#	virtual_properties.h
#	virtual_filter.h
	hflip.h
	../queue/share_queue_write.h
	../queue/share_queue.h
	)
	
add_library(obs-virtualoutput MODULE
	${virtualoutput_SOURCES}
	${virtualoutput_HEADERS}
)
	
include_directories(
	${CMAKE_SOURCE_DIR}/libobs
	${FFMPEG_INCLUDE_DIRS}
)
		
target_link_libraries(obs-virtualoutput
	${FFMPEG_LIBRARIES}
	libobs
	w32-pthreads
)
	
if(MSVC)
	foreach(t EXE SHARED MODULE)
		set(CMAKE_${t}_LINKER_FLAGS "${CMAKE_${t}_LINKER_FLAGS} /OPT:REF /OPT:ICF")
		set(CMAKE_${t}_LINKER_FLAGS_DEBUG "${CMAKE_${t}_LINKER_FLAGS_DEBUG} /INCREMENTAL:NO")
		set(CMAKE_${t}_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_${t}_LINKER_FLAGS_RELWITHDEBINFO} /INCREMENTAL:NO /PDBSTRIPPED")
	endforeach()
endif()

install_obs_plugin(obs-virtualoutput data)