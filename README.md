# Ext2 Filesystem Manager

A robust, concurrent C++ program designed to parse, read, modify, and manage custom `ext2` filesystem images. This project implements low-level filesystem interactions entirely in user-space without relying on kernel-level mounting or external OS structures.

## Features

### Core Capabilities
* **Read Core Structures (`info`)**: Parses and displays critical filesystem metadata from the Superblock and the Block Group Descriptors.
* **Traverse Directories (`ls`)**: Recursively navigates through the filesystem starting from the root directory, correctly resolving dynamic-length directory entries to print a complete tree layout.
* **Read File Contents (`cat`)**: Supports locating and reading file data across direct, singly-indirect, doubly-indirect, and triply-indirect block pointers.
* **Update Existing Files (`append`, `write`)**: Allows modifying existing files. Features a dynamic block allocation algorithm that provisions and maps direct and all levels of indirect blocks as the file size grows.

### Advanced Concurrency (Bonus Implementations)
* **Lock-Free Concurrent Access**: Designed to allow multiple threads to safely read from the filesystem simultaneously. By relying on POSIX `pread` and a `std::shared_mutex` pool mapped to shared locks, readers execute concurrently without blocking each other or altering a global file pointer. 
* **Minimal Block Locking Writes**: Implements a fine-grained locking strategy utilizing a lock pool array hashed by block and inode numbers. Write operations acquire a `std::unique_lock` *only* on the specific metadata and data blocks being modified, maximizing parallelism and minimizing contention across the broader filesystem.

## Getting Started

### Prerequisites
* A C++17 compatible compiler (e.g., `g++` or `clang++`)
* `make`

### Building the Project
To compile the project, simply run `make` in the root directory:
```bash
make
```
This will generate the `ext2_reader` executable.

## Usage

Start the program by providing it with a valid `ext2` binary filesystem image:
```bash
./ext2_reader <path_to_ext2_image.img>
```

Upon successful mounting, you will enter an interactive shell where you can manage the filesystem using the following commands:

| Command | Description |
| :--- | :--- |
| `info` | Print the filesystem Superblock and Block Group Descriptors. |
| `ls` | Traverse and print the complete filesystem layout. |
| `cat <path>` | Read and display the full contents of the file at `<path>`. |
| `write <path> <data>` | Overwrite the target file entirely with `<data>`. |
| `append <path> <data>`| Append `<data>` to the end of the target file. |
| `test_concurrent <path>` | Spawns 10 background threads to test lock-free concurrent reads on a file. |
| `exit` | Exit the interactive shell. |

### Example Session
```text
$ ./ext2_reader disk-backup.img
Mounted ext2 image: disk-backup.img
Commands:
  info                 - Print superblock and group descriptors
  ls                   - Traverse and print filesystem layout
  cat <path>           - Read file contents
  write <path> <data>  - Overwrite file with data
  append <path> <data> - Append data to file
  test_concurrent <path> - Start 10 threads to read the file simultaneously
  exit                 - Exit
> info
--- Superblock ---
Inodes count: 1024
Blocks count: 4096
...

> ls
--- Filesystem Layout ---
/
/hello.txt
/docs/
/docs/readme.txt

> cat /hello.txt
Hello, World!

> append /hello.txt  How are you?
Write successful.

> cat /hello.txt
Hello, World! How are you?
```

## Architecture Notes
* **Data Structures**: Standard `ext2` structures are explicitly defined in `include/ext2_structs.h` with tightly packed structs (`#pragma pack`) to ensure cross-platform compatibility (macOS/Linux) regardless of kernel headers.
* **Memory Management**: The `Ext2FS` class manages file descriptors, read/write block operations, and the concurrency locking pool. Extracted data streams are maintained securely using modern standard library containers (`std::vector`, `std::string`).
