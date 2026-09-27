add_library(Luwow.FsLibrary STATIC)
target_include_directories(Luwow.FsLibrary PRIVATE
    ${FS_ROOT}/src
    ${ENGINE_ROOT}
)
target_link_libraries(Luwow.FsLibrary PRIVATE
    Luwow.Engine
)

target_sources(Luwow.FsLibrary PRIVATE
    Library.h
    Library.cpp
    File.h
    File.cpp
    Utils.h
)
