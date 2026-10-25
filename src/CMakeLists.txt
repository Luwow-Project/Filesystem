add_library(Luwow.FsCore STATIC
    Utils.h
    Hash.h
    Hash.cpp
)
target_include_directories(Luwow.FsCore PUBLIC ${FS_ROOT}/src)

luwow_add_library(Luwow.FsLibrary
    NAME fs
    MODES SERIAL
    SOURCES
        Library.h
        Library.cpp
        File.h
        File.cpp
)
target_include_directories(Luwow.FsLibrary PRIVATE ${FS_ROOT}/src)
target_link_libraries(Luwow.FsLibrary PRIVATE Luwow.FsCore)
