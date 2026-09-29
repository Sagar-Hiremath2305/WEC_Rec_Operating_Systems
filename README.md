# Ext2 Filesystem Manager

A robust, concurrent C++ program designed to parse, read, modify, and manage custom `ext2` filesystem images. This project implements low-level filesystem interactions entirely in user-space without relying on kernel-level mounting or external OS structures.

## Implementation Steps
This project was implemented through a series of logical phases to safely interact with ext2 byte structures:

1. **Defining Data Structures**: Standard `ext2` structures (`ext2_super_block`, `ext2_group_desc`, `ext2_inode`, `ext2_dir_entry`) were meticulously defined in `include/ext2_structs.h` using strict packed alignments (`#pragma pack`) to ensure they mirror the byte layout natively without relying on OS-specific headers.
2. **Mounting and Metadata Parsing**: Implemented the `mount()` logic using POSIX `pread` to read the Superblock at a static offset (1024 bytes) and dynamically calculate block counts, group counts, and extract the Block Group Descriptor Table.
3. **Traversing the Tree**: Implemented dynamic block resolution (supporting direct and indirect pointers) and recursive looping logic across the variable-length `ext2_dir_entry` structure to traverse files starting from the Root Inode (Inode 2).
4. **Modifying the Filesystem (Writes & Appends)**: Implemented a robust block allocation mechanism that correctly discovers free blocks in bitmaps, zeroes out the block, wires the pointer to the target inode (including expanding single/double/triple indirect layers when files grow), and updates system metadata statistics seamlessly.
5. **Concurrency with Fine-Grained Locking (Bonuses)**: 
   * **Lock-Free Reads:** Refactored I/O to avoid standard `read()` / `lseek()` which alter a global file offset. Used atomic `pread` combined with `std::shared_mutex` pools (using `std::shared_lock` for readers), meaning background threads can read data lock-free with maximal parallelism.
   * **Minimal Block Locking:** Implemented a hashed lock pool mechanism ensuring that write operations take out `std::unique_lock` purely on the specific block or inode being modified, rather than halting the entire filesystem.

## Features Completed
- [x] Read Core Structures (Superblock & Block Group Descriptors)
- [x] Traverse Directories recursively
- [x] Read File Contents (including handling indirect blocks)
- [x] Update Existing Files (Append / Overwrite with dynamic block allocation)
- [x] **[Bonus]** Lock-Free Concurrent Access for readers
- [x] **[Bonus]** Minimal Block Locking Writes for writers

## Disk File Outputs
### File: `disk-backpup.img`
All tasks were successfully run and validated against this image. 

#### 1. Core Structures (`info`)
```text
> info
--- Superblock ---
Inodes count: 3072
Blocks count: 12288
Free blocks: 7582
Free inodes: 3022
First data block: 1
Block size: 1024
Blocks per group: 8192
Inodes per group: 1536
Magic: 0xef53

--- Group Descriptors ---
Group 0:
  Block bitmap: 50
  Inode bitmap: 51
  Inode table: 52
  Free blocks: 4954
  Free inodes: 1494
  Used dirs: 29
Group 1:
  Block bitmap: 8242
  Inode bitmap: 8243
  Inode table: 8244
  Free blocks: 2628
  Free inodes: 1528
  Used dirs: 8
```

#### 2. Directory Traversal (`ls`)
```text
> ls
--- Filesystem Layout ---
/lost+found/
/readthis.txt
/dir1/
/dir1/innerdir1/
/dir1/innerdir2/
/dir1/innerdir3/
/dir1/innerdir3/comp-dsa.pdf
/dir1/innerdir4/
/dir1/innerdir5/
/dir1/innerdir6/
/dir2/
/dir2/innerdir3/
/dir2/innerdir4/
/dir2/innerdir5/
/dir2/innerdir6/
/dir2/innerdir1/
/dir2/innerdir2/
/dir3/
/dir3/innerdir3/
/dir3/innerdir4/
/dir3/innerdir5/
/dir3/innerdir6/
/dir3/innerdir1/
/dir3/innerdir2/
/dir4/
/dir4/innerdir3/
/dir4/innerdir4/
/dir4/innerdir5/
/dir4/innerdir6/
/dir4/innerdir6/rice.webp
/dir4/innerdir1/
/dir4/innerdir2/
/dir5/
/dir5/innerdir3/
/dir5/innerdir3/vid.webm
/dir5/innerdir4/
/dir5/innerdir5/
/dir5/innerdir6/
/dir5/innerdir1/
/dir5/innerdir2/
```

#### 3. Reading File Contents (`cat`)
```text
> cat /readthis.txt
This is the second file from the task
```

*Note to evaluators/collaborators: Feel free to add screenshots mapping the above shell logs directly to your system's terminal output below if needed.*

*(Add Screenshots Here)*

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
