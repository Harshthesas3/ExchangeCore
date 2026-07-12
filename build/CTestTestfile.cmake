# CMake generated Testfile for 
# Source directory: C:/projects/ExchangeCore
# Build directory: C:/projects/ExchangeCore/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test("CoreTests" "C:/projects/ExchangeCore/build/Debug/ExchangeCoreTests.exe")
  set_tests_properties("CoreTests" PROPERTIES  _BACKTRACE_TRIPLES "C:/projects/ExchangeCore/CMakeLists.txt;83;add_test;C:/projects/ExchangeCore/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test("CoreTests" "C:/projects/ExchangeCore/build/Release/ExchangeCoreTests.exe")
  set_tests_properties("CoreTests" PROPERTIES  _BACKTRACE_TRIPLES "C:/projects/ExchangeCore/CMakeLists.txt;83;add_test;C:/projects/ExchangeCore/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test("CoreTests" "C:/projects/ExchangeCore/build/MinSizeRel/ExchangeCoreTests.exe")
  set_tests_properties("CoreTests" PROPERTIES  _BACKTRACE_TRIPLES "C:/projects/ExchangeCore/CMakeLists.txt;83;add_test;C:/projects/ExchangeCore/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test("CoreTests" "C:/projects/ExchangeCore/build/RelWithDebInfo/ExchangeCoreTests.exe")
  set_tests_properties("CoreTests" PROPERTIES  _BACKTRACE_TRIPLES "C:/projects/ExchangeCore/CMakeLists.txt;83;add_test;C:/projects/ExchangeCore/CMakeLists.txt;0;")
else()
  add_test("CoreTests" NOT_AVAILABLE)
endif()
subdirs("_deps/ftxui-build")
subdirs("_deps/googletest-build")
