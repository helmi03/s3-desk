set(CORE_SRCS src/prayer.c src/timetable.c src/schedule.c src/ms.c src/settings.c src/zones.c src/zones_table.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${CORE_SRCS} INCLUDE_DIRS include PRIV_REQUIRES json)
else()
    add_library(core STATIC ${CORE_SRCS} ${CJSON_DIR}/cJSON.c)
    target_include_directories(core PUBLIC include ${CJSON_DIR})
    set_source_files_properties(${CJSON_DIR}/cJSON.c PROPERTIES COMPILE_OPTIONS "-w;-Wno-error")
endif()
