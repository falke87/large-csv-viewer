cmake_minimum_required(VERSION 3.21)
project(LargeCsvViewer VERSION 1.0 LANGUAGES CXX)

# Large CSV Viewer – demo project. Requires Qt 6.4+ (Core, Gui, Widgets; Test for the tests).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(LCV_BUILD_TESTS "Build the unit tests" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
# zlib (for writing .xlsx): Qt for Windows has it built into Qt6Core (Qt6::ZlibPrivate),
# Linux uses the system zlib (apt install zlib1g-dev).
find_package(Qt6 QUIET COMPONENTS ZlibPrivate)
if(TARGET Qt6::ZlibPrivate)
    set(LCV_ZLIB Qt6::ZlibPrivate)
else()
    set(ZLIB_USE_STATIC_LIBS ON)
    find_package(ZLIB REQUIRED)
    set(LCV_ZLIB ZLIB::ZLIB)
endif()
message(STATUS "zlib for .xlsx export: ${LCV_ZLIB}")

# Core: CSV parsing, index, filter, sort, export. No GUI, so tests and tools can use it.
add_library(csvcore STATIC
    src/core/CsvParser.cpp     src/core/CsvParser.h
    src/core/CsvDocument.cpp   src/core/CsvDocument.h
    src/core/RowFilter.cpp     src/core/RowFilter.h
    src/core/RowSorter.cpp     src/core/RowSorter.h
    src/core/ZipWriter.cpp     src/core/ZipWriter.h
    src/core/XlsxWriter.cpp    src/core/XlsxWriter.h
    src/core/Exporter.cpp      src/core/Exporter.h
    src/core/SampleGenerator.cpp src/core/SampleGenerator.h
)
target_include_directories(csvcore PUBLIC src)
target_link_libraries(csvcore PUBLIC Qt6::Core PRIVATE ${LCV_ZLIB})
find_package(Threads REQUIRED)
target_link_libraries(csvcore PUBLIC Threads::Threads)

# Desktop application
add_executable(LargeCsvViewer WIN32
    src/app/main.cpp
    src/app/MainWindow.cpp     src/app/MainWindow.h
    src/app/CsvTableModel.cpp  src/app/CsvTableModel.h
    src/app/FilterBar.cpp      src/app/FilterBar.h
    src/app/ExportDialog.cpp   src/app/ExportDialog.h
    src/app/BackgroundJob.cpp  src/app/BackgroundJob.h
    resources/resources.qrc
)
if(WIN32)
    target_sources(LargeCsvViewer PRIVATE resources/app.rc)
endif()
target_link_libraries(LargeCsvViewer PRIVATE csvcore Qt6::Widgets)

# Command line tool: csvgen [rows] [file.csv] – creates the fictional sample file
add_executable(csvgen tools/csvgen.cpp)
target_link_libraries(csvgen PRIVATE csvcore)

if(LCV_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_executable(tst_core tests/tst_core.cpp)
    target_link_libraries(tst_core PRIVATE csvcore Qt6::Test)
    add_test(NAME tst_core COMMAND tst_core)
endif()


# Benchmark: csvbench file.csv – open, filter, sort and export times
add_executable(csvbench tools/csvbench.cpp)
target_link_libraries(csvbench PRIVATE csvcore)
