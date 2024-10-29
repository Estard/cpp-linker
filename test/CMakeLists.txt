set(TEST_CPPLD_SOURCES
   TestSimple.cpp
   Tester.cpp
   TestUnits.cpp
   )

add_executable(tester ${TEST_CPPLD_SOURCES})
target_link_libraries(tester
   GTest::GTest
   cppld)