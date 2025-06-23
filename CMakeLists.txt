cmake_minimum_required(VERSION 3.5)
project(buyer-backend CXX)

include(CheckIncludeFileCXX)

check_include_file_cxx(any HAS_ANY)
check_include_file_cxx(string_view HAS_STRING_VIEW)
check_include_file_cxx(coroutine HAS_COROUTINE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(${PROJECT_NAME} main.cc)

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
find_path(MLPACK_INCLUDE_DIRS "mlpack.hpp")
find_package(Armadillo CONFIG REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon 
                                              jwt-cpp::jwt-cpp 
                                              unofficial::argon2::libargon2
                                              cppzmq cppzmq-static
                                              armadillo
#                                              redis++::redis++
                                              Boost::uuid                
                      )

# ##############################################################################

# aux_source_directory("services/subber" SERVICE_SRC)
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
                                   ${CMAKE_CURRENT_SOURCE_DIR}/models)
target_sources(${PROJECT_NAME}
               PRIVATE
               ${SRC_DIR}
               ${CTL_SRC}
               ${FILTER_SRC}
               ${PLUGIN_SRC}
               ${MODEL_SRC}
               ${CONFIG_SRC})
#               ${SERVICE_SRC})
# ##############################################################################
# uncomment the following line for dynamically loading views 
# set_property(TARGET ${PROJECT_NAME} PROPERTY ENABLE_EXPORTS ON)

# ##############################################################################

# Database management for tests
if(WIN32)
    add_custom_target(setup_test_db
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/setup_test_db.bat
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Setting up test database"
    )
else()
    add_custom_target(setup_test_db
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/setup_test_db.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Setting up test database"
    )
endif()

add_subdirectory(test)
