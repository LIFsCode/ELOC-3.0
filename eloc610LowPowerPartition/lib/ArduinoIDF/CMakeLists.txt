set(SOURCES src/*)
            
idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS include)
					
set_property(SOURCES ${SOURCES} PROPERTY COMPILE_FLAGS
    -Wno-format)