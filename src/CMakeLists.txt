project(PycpLibrary)

file(GLOB SOURCE "*.cpp")

add_library(${PROJECT_NAME} ${SOURCE})

target_include_directories(${PROJECT_NAME} PRIVATE ${XXHASH_INCLUDE_DIR})

target_link_libraries(${PROJECT_NAME} PRIVATE ${XXHASH_LIBRARY})
