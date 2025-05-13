cmake_minimum_required(VERSION 3.5)
    
set(PROTO_COMPILER "protoc")
set(PROTO_C_COMPILER "protoc")
set(C_OUT_PATH "${CMAKE_CURRENT_LIST_DIR}/proto-c")
    
set(PROTO_SRCS 
    "${CMAKE_CURRENT_LIST_DIR}/matrx.proto"
)
    
set(pb_csrcs
    "${C_OUT_PATH}/matrx.pb-c.c"
)
    
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    add_custom_command(OUTPUT ${pb_csrcs}
            COMMAND ${PROTO_C_COMPILER} -I ${CMAKE_CURRENT_LIST_DIR} --c_out=${C_OUT_PATH} ${PROTO_SRCS}
            DEPENDS ${PROTO_SRCS}
            VERBATIM)
endif()
    
idf_component_register(SRCS "${pb_csrcs}"
                        INCLUDE_DIRS "proto-c"
                        REQUIRES protobuf-c)
