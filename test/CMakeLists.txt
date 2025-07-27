cmake_minimum_required(VERSION 3.5)
project(buyer_backend_test CXX)


add_executable(${PROJECT_NAME} 
  test_main.cc
  test_authentication.cc
  test_search.cc
  test_chats.cc
  test_community.cc
  test_users.cc
  test_orders.cc
  test_dashboard.cc
  test_offers_workflow.cc
  test_proofs_and_escrow.cc
)

# ##############################################################################
# If you include the drogon source code locally in your project, use this method
# to add drogon 
# target_link_libraries(${PROJECT_NAME} PRIVATE drogon)
#
# and comment out the following lines
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)

# Custom target to configure tests with a fresh database
# comment out if you are using docker compose !!!
add_custom_target(configure_tests
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target setup_test_db
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${PROJECT_NAME}
    COMMENT "Setting up test database and running tests"
)

ParseAndAddDrogonTests(${PROJECT_NAME})

enable_testing()
