# Filesystem
A light-weight synchronous filesystem library for Luwow-Project.

This repository is not stand-alone. You must build it externally from within another
repository like https://github.com/Luwow-Project/Release

## Project Overview

This project contains the sources for Luwow filesystem library.

It has no external dependencies, only `std::filesystem` and `std::fstream` (C++20). Paths are UTF-8 and relative paths resolve against the working directory. Every function raises an error on failure.

## Library Documentation

### `fs.readFile(path: string) -> string`

Reads the whole file.

### `fs.writeFile(path: string, data: string | buffer)`

Writes `data` to the file, creating or truncating it.

### `fs.appendFile(path: string, data: string | buffer)`

Appends `data` to the file, creating it if needed.

### `fs.exists(path: string) -> boolean`

Returns whether the path exists.

### `fs.type(path: string) -> "file" | "dir" | "symlink" | "other"`

Returns the type of the path, without following symlinks.

### `fs.stat(path: string) -> { type: string, size: number, modified: number, readonly: boolean }`

Returns metadata for the path. `modified` is a Unix timestamp in seconds.

### `fs.listdir(path: string) -> { { name: string, type: string } }`

Lists the entries of a directory.

### `fs.mkdir(path: string, recursive: boolean?)`

Creates a directory. With `recursive`, missing parents are created and existing directories are not an error.

### `fs.remove(path: string)`

Removes a file.

### `fs.rmdir(path: string, recursive: boolean?)`

Removes a directory. With `recursive`, its contents are removed as well.

### `fs.copy(from: string, to: string, overwrite: boolean?)`

Copies a file or a directory recursively.

### `fs.move(from: string, to: string)`

Moves or renames a file or directory.

### `fs.open(path: string, mode: string?) -> File`

Opens a file handle. `mode` is one of `"r"` (default), `"w"`, `"a"`, `"r+"`, `"w+"`, `"a+"`.

## File

### `file:read(count: number?) -> string`

Reads `count` bytes, or everything until EOF when omitted.

### `file:write(data: string | buffer)`

Writes `data` at the current position.

### `file:seek(whence: "set" | "cur" | "end"?, offset: number?) -> number`

Moves the position and returns the new position.

### `file:tell() -> number`

### `file:size() -> number`

### `file:flush()`

### `file:close()`

Closes the handle. The handle is also closed when garbage collected.

### `file.path: string` / `file.closed: boolean`

Read-only properties.
