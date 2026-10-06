luwow_add_library(Luwow.FsLibrary
    NAME fs
    MODES SERIAL
    SOURCES
        Library.h
        Library.cpp
        File.h
        File.cpp
        Utils.h
)
target_include_directories(Luwow.FsLibrary PRIVATE ${FS_ROOT}/src)