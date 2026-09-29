#pragma once
#include "ext2_structs.h"
#include <string>
#include <vector>
#include <shared_mutex>

class Ext2FS {
public:
    Ext2FS(const std::string& image_path);
    ~Ext2FS();

    bool mount();
    
    void print_super_block() const;
    void print_group_descriptors() const;

    void traverse_root() const;

    std::string read_file(const std::string& path) const;

    bool write_file(const std::string& path, const std::string& data, bool append = false);

private:
    std::string image_path_;
    int fd_;
    ext2_super_block sb_;
    std::vector<ext2_group_desc> block_groups_;
    uint32_t block_size_;
    uint32_t num_block_groups_;

    // Locking pool for minimal block locking
    static constexpr size_t NUM_LOCKS = 1024;
    mutable std::shared_mutex block_locks_[NUM_LOCKS];

    std::shared_mutex& get_block_lock(uint32_t block_num) const {
        return block_locks_[block_num % NUM_LOCKS];
    }
    
    // Inode locking pool
    mutable std::shared_mutex inode_locks_[NUM_LOCKS];
    std::shared_mutex& get_inode_lock(uint32_t inode_num) const {
        return inode_locks_[inode_num % NUM_LOCKS];
    }

    bool read_block(uint32_t block_num, void* buf) const;
    bool write_block(uint32_t block_num, const void* buf);
    
    ext2_inode get_inode(uint32_t inode_num) const;
    bool write_inode(uint32_t inode_num, const ext2_inode& inode);
    
    uint32_t allocate_block();
    uint32_t get_or_allocate_block(ext2_inode& inode, uint32_t block_idx, bool& inode_changed);
    
    uint32_t find_inode_by_path(const std::string& path) const;
    void traverse_dir(uint32_t inode_num, const std::string& current_path) const;
    std::vector<uint32_t> get_file_blocks(const ext2_inode& inode) const;
    
    void append_to_file(uint32_t inode_num, const std::string& data);
    void overwrite_file(uint32_t inode_num, const std::string& data);
};
