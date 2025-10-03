cmake_minimum_required(VERSION 3.5)
project(buyer-backend CXX)

include(CheckIncludeFileCXX)

check_include_file_cxx(any HAS_ANY)
check_include_file_cxx(string_view HAS_STRING_VIEW)
check_include_file_cxx(coroutine HAS_COROUTINE)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(${PROJECT_NAME} main.cc)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON
)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
endif()

# ##############################################################################
# If you include the drogon source code locally in your project, use this method
# to add drogon 
# add_subdirectory(drogon) 
# target_link_libraries(${PROJECT_NAME} PRIVATE drogon)
#
# and comment out the following line
# find_package(redis++ CONFIG REQUIRED)
find_package(boost_uuid REQUIRED CONFIG)
find_package(cppzmq CONFIG REQUIRED)
find_package(Drogon CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
find_package(unofficial-argon2 CONFIG REQUIRED)
find_package(aws-cpp-sdk-core REQUIRED)
find_package(aws-cpp-sdk-s3 REQUIRED)
find_package(unordered_dense CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)


target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon 
                                              jwt-cpp::jwt-cpp 
                                              unofficial::argon2::libargon2
                                              cppzmq cppzmq-static
#                                              redis++::redis++
                                              Boost::uuid
                                              aws-cpp-sdk-core
                                              aws-cpp-sdk-s3
                                              unordered_dense::unordered_dense
                                              glaze::glaze                
                      )

# ##############################################################################

file(GLOB_RECURSE SERVICE_SRC CONFIGURE_DEPENDS 
     ${PROJECT_SOURCE_DIR}/services/*
    )
aux_source_directory(controllers CTL_SRC)
aux_source_directory(filters FILTER_SRC)
aux_source_directory(plugins PLUGIN_SRC)
# aux_source_directory(models MODEL_SRC) # Not used for now
aux_source_directory(config CONFIG_SRC)

drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views
                    ${CMAKE_CURRENT_BINARY_DIR})
# use the following line to create views with namespaces.
# drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views
#                     ${CMAKE_CURRENT_BINARY_DIR} TRUE)
# use the following line to create views with namespace CHANGE_ME prefixed
# and path namespaces.
# drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views
#                     ${CMAKE_CURRENT_BINARY_DIR} TRUE CHANGE_ME)

target_include_directories(${PROJECT_NAME}
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                   # ${CMAKE_CURRENT_SOURCE_DIR}/models
                                   aws-cpp-sdk-core 
                                   )
target_sources(${PROJECT_NAME}
               PRIVATE
               ${SRC_DIR}
               ${CTL_SRC}
               ${FILTER_SRC}
               ${PLUGIN_SRC}
               ${MODEL_SRC}
               ${CONFIG_SRC}
               ${SERVICE_SRC})
# ##############################################################################
# uncomment the following line for dynamically loading views 
# set_property(TARGET ${PROJECT_NAME} PROPERTY ENABLE_EXPORTS ON)

# ##############################################################################

# Database management for tests
# Add if not using docker and Postgres is installed locally !!!
# if(WIN32)
#     add_custom_target(setup_test_db
#         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/setup_test_db.bat
#         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
#         COMMENT "Setting up test database"
#     )
# else()
#     add_custom_target(setup_test_db
#         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/setup_test_db.sh
#         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
#         COMMENT "Setting up test database"
#     )
# endif()

option(ENABLE_TESTS "Enable building tests" ON)

if (ENABLE_TESTS)
    add_subdirectory(test)
endif()
