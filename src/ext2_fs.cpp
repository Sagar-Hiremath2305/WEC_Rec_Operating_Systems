#include "ext2_fs.h"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <stdexcept>
#include <cmath>

Ext2FS::Ext2FS(const std::string& image_path) : image_path_(image_path), fd_(-1) {}

Ext2FS::~Ext2FS() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool Ext2FS::mount() {
    fd_ = open(image_path_.c_str(), O_RDWR);
    if (fd_ < 0) {
        std::cerr << "Failed to open image file.\n";
        return false;
    }

    if (pread(fd_, &sb_, sizeof(ext2_super_block), 1024) != sizeof(ext2_super_block)) {
        std::cerr << "Failed to read super block.\n";
        return false;
    }

    if (sb_.s_magic != EXT2_SUPER_MAGIC) {
        std::cerr << "Not an ext2 filesystem. Magic = 0x" << std::hex << sb_.s_magic << "\n";
        return false;
    }

    block_size_ = 1024 << sb_.s_log_block_size;
    num_block_groups_ = (sb_.s_blocks_count + sb_.s_blocks_per_group - 1) / sb_.s_blocks_per_group;

    uint32_t bg_desc_block = sb_.s_first_data_block + 1;
    block_groups_.resize(num_block_groups_);
    
    if (pread(fd_, block_groups_.data(), num_block_groups_ * sizeof(ext2_group_desc), bg_desc_block * block_size_) != num_block_groups_ * sizeof(ext2_group_desc)) {
        std::cerr << "Failed to read block group descriptors.\n";
        return false;
    }

    return true;
}

void Ext2FS::print_super_block() const {
    std::cout << "--- Superblock ---\n";
    std::cout << "Inodes count: " << sb_.s_inodes_count << "\n";
    std::cout << "Blocks count: " << sb_.s_blocks_count << "\n";
    std::cout << "Free blocks: " << sb_.s_free_blocks_count << "\n";
    std::cout << "Free inodes: " << sb_.s_free_inodes_count << "\n";
    std::cout << "First data block: " << sb_.s_first_data_block << "\n";
    std::cout << "Block size: " << block_size_ << "\n";
    std::cout << "Blocks per group: " << sb_.s_blocks_per_group << "\n";
    std::cout << "Inodes per group: " << sb_.s_inodes_per_group << "\n";
    std::cout << "Magic: 0x" << std::hex << sb_.s_magic << std::dec << "\n\n";
}

void Ext2FS::print_group_descriptors() const {
    std::cout << "--- Group Descriptors ---\n";
    for (uint32_t i = 0; i < num_block_groups_; ++i) {
        const auto& bg = block_groups_[i];
        std::cout << "Group " << i << ":\n";
        std::cout << "  Block bitmap: " << bg.bg_block_bitmap << "\n";
        std::cout << "  Inode bitmap: " << bg.bg_inode_bitmap << "\n";
        std::cout << "  Inode table: " << bg.bg_inode_table << "\n";
        std::cout << "  Free blocks: " << bg.bg_free_blocks_count << "\n";
        std::cout << "  Free inodes: " << bg.bg_free_inodes_count << "\n";
        std::cout << "  Used dirs: " << bg.bg_used_dirs_count << "\n";
    }
    std::cout << "\n";
}

bool Ext2FS::read_block(uint32_t block_num, void* buf) const {
    if (block_num == 0) {
        memset(buf, 0, block_size_);
        return true;
    }
    std::shared_lock<std::shared_mutex> lock(get_block_lock(block_num));
    if (pread(fd_, buf, block_size_, block_num * block_size_) != block_size_) {
        return false;
    }
    return true;
}

bool Ext2FS::write_block(uint32_t block_num, const void* buf) {
    if (block_num == 0) return false;
    std::unique_lock<std::shared_mutex> lock(get_block_lock(block_num));
    if (pwrite(fd_, buf, block_size_, block_num * block_size_) != block_size_) {
        return false;
    }
    return true;
}

ext2_inode Ext2FS::get_inode(uint32_t inode_num) const {
    if (inode_num < 1 || inode_num > sb_.s_inodes_count) {
        throw std::runtime_error("Invalid inode number");
    }

    uint32_t bg_idx = (inode_num - 1) / sb_.s_inodes_per_group;
    uint32_t local_inode_idx = (inode_num - 1) % sb_.s_inodes_per_group;

    const auto& bg = block_groups_[bg_idx];
    
    std::shared_lock<std::shared_mutex> lock(get_inode_lock(inode_num));
    
    ext2_inode inode;
    uint32_t inode_size = sb_.s_inode_size ? sb_.s_inode_size : 128;
    off_t offset = bg.bg_inode_table * block_size_ + local_inode_idx * inode_size;
    if (pread(fd_, &inode, sizeof(ext2_inode), offset) != sizeof(ext2_inode)) {
        throw std::runtime_error("Failed to read inode");
    }

    return inode;
}

bool Ext2FS::write_inode(uint32_t inode_num, const ext2_inode& inode) {
    if (inode_num < 1 || inode_num > sb_.s_inodes_count) {
        return false;
    }

    uint32_t bg_idx = (inode_num - 1) / sb_.s_inodes_per_group;
    uint32_t local_inode_idx = (inode_num - 1) % sb_.s_inodes_per_group;

    const auto& bg = block_groups_[bg_idx];
    
    std::unique_lock<std::shared_mutex> lock(get_inode_lock(inode_num));
    
    uint32_t inode_size = sb_.s_inode_size ? sb_.s_inode_size : 128;
    off_t offset = bg.bg_inode_table * block_size_ + local_inode_idx * inode_size;
    if (pwrite(fd_, &inode, sizeof(ext2_inode), offset) != sizeof(ext2_inode)) {
        return false;
    }
    
    return true;
}

std::vector<uint32_t> Ext2FS::get_file_blocks(const ext2_inode& inode) const {
    std::vector<uint32_t> blocks;
    uint32_t ptrs_per_block = block_size_ / 4;
    
    uint32_t num_blocks = (inode.i_size + block_size_ - 1) / block_size_;
    uint32_t blocks_read = 0;

    for (int i = 0; i < EXT2_NDIR_BLOCKS && blocks_read < num_blocks; ++i) {
        if (inode.i_block[i]) {
            blocks.push_back(inode.i_block[i]);
        }
        blocks_read++;
    }

    if (blocks_read >= num_blocks) return blocks;

    if (inode.i_block[EXT2_IND_BLOCK]) {
        std::vector<uint32_t> ind_block(ptrs_per_block);
        read_block(inode.i_block[EXT2_IND_BLOCK], ind_block.data());
        for (uint32_t i = 0; i < ptrs_per_block && blocks_read < num_blocks; ++i) {
            if (ind_block[i]) blocks.push_back(ind_block[i]);
            blocks_read++;
        }
    }

    if (blocks_read >= num_blocks) return blocks;

    if (inode.i_block[EXT2_DIND_BLOCK]) {
        std::vector<uint32_t> dind_block(ptrs_per_block);
        read_block(inode.i_block[EXT2_DIND_BLOCK], dind_block.data());
        for (uint32_t i = 0; i < ptrs_per_block && blocks_read < num_blocks; ++i) {
            if (dind_block[i]) {
                std::vector<uint32_t> ind_block(ptrs_per_block);
                read_block(dind_block[i], ind_block.data());
                for (uint32_t j = 0; j < ptrs_per_block && blocks_read < num_blocks; ++j) {
                    if (ind_block[j]) blocks.push_back(ind_block[j]);
                    blocks_read++;
                }
            } else {
                blocks_read += ptrs_per_block;
            }
        }
    }
    
    if (blocks_read >= num_blocks) return blocks;
    
    if (inode.i_block[EXT2_TIND_BLOCK]) {
        std::vector<uint32_t> tind_block(ptrs_per_block);
        read_block(inode.i_block[EXT2_TIND_BLOCK], tind_block.data());
        for (uint32_t i = 0; i < ptrs_per_block && blocks_read < num_blocks; ++i) {
            if (tind_block[i]) {
                std::vector<uint32_t> dind_block(ptrs_per_block);
                read_block(tind_block[i], dind_block.data());
                for (uint32_t j = 0; j < ptrs_per_block && blocks_read < num_blocks; ++j) {
                    if (dind_block[j]) {
                        std::vector<uint32_t> ind_block(ptrs_per_block);
                        read_block(dind_block[j], ind_block.data());
                        for (uint32_t k = 0; k < ptrs_per_block && blocks_read < num_blocks; ++k) {
                            if (ind_block[k]) blocks.push_back(ind_block[k]);
                            blocks_read++;
                        }
                    } else {
                        blocks_read += ptrs_per_block;
                    }
                }
            } else {
                blocks_read += ptrs_per_block * ptrs_per_block;
            }
        }
    }

    return blocks;
}

void Ext2FS::traverse_root() const {
    std::cout << "--- Filesystem Layout ---\n";
    traverse_dir(EXT2_ROOT_INO, "/");
}

void Ext2FS::traverse_dir(uint32_t inode_num, const std::string& current_path) const {
    ext2_inode inode = get_inode(inode_num);
    
    if (((inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)) {
        return;
    }

    std::vector<uint32_t> blocks = get_file_blocks(inode);
    std::vector<char> buf(block_size_);

    for (uint32_t block : blocks) {
        if (!read_block(block, buf.data())) continue;

        uint32_t offset = 0;
        while (offset < block_size_) {
            ext2_dir_entry* entry = reinterpret_cast<ext2_dir_entry*>(buf.data() + offset);
            
            if (entry->rec_len == 0) break;
            
            if (entry->inode != 0) {
                std::string name(entry->name, entry->name_len);
                if (name != "." && name != "..") {
                    std::string full_path = current_path == "/" ? current_path + name : current_path + "/" + name;
                    
                    std::cout << full_path;
                    if (entry->file_type == EXT2_FT_DIR) {
                        std::cout << "/\n";
                        traverse_dir(entry->inode, full_path);
                    } else {
                        std::cout << "\n";
                    }
                }
            }
            offset += entry->rec_len;
        }
    }
}

uint32_t Ext2FS::find_inode_by_path(const std::string& path) const {
    if (path.empty() || path[0] != '/') return 0;
    if (path == "/") return EXT2_ROOT_INO;

    uint32_t current_inode = EXT2_ROOT_INO;
    size_t pos = 1;

    while (pos < path.length()) {
        size_t next_slash = path.find('/', pos);
        std::string component;
        if (next_slash == std::string::npos) {
            component = path.substr(pos);
            pos = path.length();
        } else {
            component = path.substr(pos, next_slash - pos);
            pos = next_slash + 1;
        }

        if (component.empty()) continue;

        ext2_inode inode = get_inode(current_inode);
        if (((inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)) return 0;

        std::vector<uint32_t> blocks = get_file_blocks(inode);
        std::vector<char> buf(block_size_);
        bool found = false;

        for (uint32_t block : blocks) {
            if (!read_block(block, buf.data())) continue;

            uint32_t offset = 0;
            while (offset < block_size_) {
                ext2_dir_entry* entry = reinterpret_cast<ext2_dir_entry*>(buf.data() + offset);
                if (entry->rec_len == 0) break;
                if (entry->inode != 0) {
                    std::string name(entry->name, entry->name_len);
                    if (name == component) {
                        current_inode = entry->inode;
                        found = true;
                        break;
                    }
                }
                offset += entry->rec_len;
            }
            if (found) break;
        }
        if (!found) return 0;
    }

    return current_inode;
}

std::string Ext2FS::read_file(const std::string& path) const {
    uint32_t inode_num = find_inode_by_path(path);
    if (inode_num == 0) {
        throw std::runtime_error("File not found");
    }

    ext2_inode inode = get_inode(inode_num);
    if ((inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR) {
        throw std::runtime_error("Is a directory");
    }

    std::vector<uint32_t> blocks = get_file_blocks(inode);
    std::string content;
    content.reserve(inode.i_size);
    std::vector<char> buf(block_size_);

    uint32_t remaining = inode.i_size;
    for (uint32_t block : blocks) {
        if (remaining == 0) break;
        if (!read_block(block, buf.data())) break;
        
        uint32_t to_copy = std::min(remaining, block_size_);
        content.append(buf.data(), to_copy);
        remaining -= to_copy;
    }

    return content;
}

#include <mutex>
static std::mutex alloc_mutex;

uint32_t Ext2FS::allocate_block() {
    std::lock_guard<std::mutex> lock(alloc_mutex);

    for (uint32_t bg_idx = 0; bg_idx < num_block_groups_; ++bg_idx) {
        auto& bg = block_groups_[bg_idx];
        if (bg.bg_free_blocks_count == 0) continue;

        std::vector<uint8_t> bitmap(block_size_);
        if (!read_block(bg.bg_block_bitmap, bitmap.data())) continue;

        for (uint32_t byte_idx = 0; byte_idx < block_size_; ++byte_idx) {
            if (bitmap[byte_idx] == 0xFF) continue;

            for (int bit = 0; bit < 8; ++bit) {
                if (!(bitmap[byte_idx] & (1 << bit))) {
                    bitmap[byte_idx] |= (1 << bit);
                    write_block(bg.bg_block_bitmap, bitmap.data());

                    bg.bg_free_blocks_count--;
                    
                    uint32_t bg_desc_block = sb_.s_first_data_block + 1;
                    std::unique_lock<std::shared_mutex> bg_lock(get_block_lock(bg_desc_block));
                    pwrite(fd_, block_groups_.data(), num_block_groups_ * sizeof(ext2_group_desc), bg_desc_block * block_size_);
                    bg_lock.unlock();

                    sb_.s_free_blocks_count--;
                    std::unique_lock<std::shared_mutex> sb_lock(get_block_lock(0));
                    pwrite(fd_, &sb_, sizeof(ext2_super_block), 1024);
                    sb_lock.unlock();

                    uint32_t block_num = bg_idx * sb_.s_blocks_per_group + sb_.s_first_data_block + byte_idx * 8 + bit;
                    
                    std::vector<char> zero_block(block_size_, 0);
                    write_block(block_num, zero_block.data());
                    
                    return block_num;
                }
            }
        }
    }
    throw std::runtime_error("No free blocks available");
}

bool Ext2FS::write_file(const std::string& path, const std::string& data, bool append) {
    uint32_t inode_num = find_inode_by_path(path);
    if (inode_num == 0) {
        std::cerr << "File not found\n";
        return false;
    }

    if (append) {
        append_to_file(inode_num, data);
    } else {
        overwrite_file(inode_num, data);
    }
    return true;
}


uint32_t Ext2FS::get_or_allocate_block(ext2_inode& inode, uint32_t block_idx, bool& inode_changed) {
    uint32_t ptrs_per_block = block_size_ / 4;

    if (block_idx < EXT2_NDIR_BLOCKS) {
        if (inode.i_block[block_idx] == 0) {
            inode.i_block[block_idx] = allocate_block();
            inode.i_blocks += block_size_ / 512;
            inode_changed = true;
        }
        return inode.i_block[block_idx];
    }
    
    block_idx -= EXT2_NDIR_BLOCKS;
    
    // Singly indirect
    if (block_idx < ptrs_per_block) {
        if (inode.i_block[EXT2_IND_BLOCK] == 0) {
            inode.i_block[EXT2_IND_BLOCK] = allocate_block();
            inode.i_blocks += block_size_ / 512;
            inode_changed = true;
        }
        std::vector<uint32_t> ind_block(ptrs_per_block, 0);
        read_block(inode.i_block[EXT2_IND_BLOCK], ind_block.data());
        
        if (ind_block[block_idx] == 0) {
            ind_block[block_idx] = allocate_block();
            inode.i_blocks += block_size_ / 512;
            inode_changed = true;
            write_block(inode.i_block[EXT2_IND_BLOCK], ind_block.data());
        }
        return ind_block[block_idx];
    }
    
    block_idx -= ptrs_per_block;
    
    // Doubly indirect
    if (block_idx < ptrs_per_block * ptrs_per_block) {
        if (inode.i_block[EXT2_DIND_BLOCK] == 0) {
            inode.i_block[EXT2_DIND_BLOCK] = allocate_block();
            inode.i_blocks += block_size_ / 512;
            inode_changed = true;
        }
        
        uint32_t dind_idx = block_idx / ptrs_per_block;
        uint32_t ind_idx = block_idx % ptrs_per_block;
        
        std::vector<uint32_t> dind_block(ptrs_per_block, 0);
        read_block(inode.i_block[EXT2_DIND_BLOCK], dind_block.data());
        
        bool dind_changed = false;
        if (dind_block[dind_idx] == 0) {
            dind_block[dind_idx] = allocate_block();
            inode.i_blocks += block_size_ / 512;
            inode_changed = true;
            dind_changed = true;
        }
        
        std::vector<uint32_t> ind_block(ptrs_per_block, 0);
        read_block(dind_block[dind_idx], ind_block.data());
        
        if (ind_block[ind_idx] == 0) {
            ind_block[ind_idx] = allocate_block();
            inode.i_blocks += block_size_ / 512;
            inode_changed = true;
            write_block(dind_block[dind_idx], ind_block.data());
        }
        
        if (dind_changed) {
            write_block(inode.i_block[EXT2_DIND_BLOCK], dind_block.data());
        }
        
        return ind_block[ind_idx];
    }
    
    block_idx -= ptrs_per_block * ptrs_per_block;
    
    // Triply indirect
    if (block_idx < ptrs_per_block * ptrs_per_block * ptrs_per_block) {
        if (inode.i_block[EXT2_TIND_BLOCK] == 0) {
            inode.i_block[EXT2_TIND_BLOCK] = allocate_block();
            inode.i_blocks += block_size_ / 512;
            inode_changed = true;
        }
        
        uint32_t tind_idx = block_idx / (ptrs_per_block * ptrs_per_block);
        uint32_t dind_rem = block_idx % (ptrs_per_block * ptrs_per_block);
        uint32_t dind_idx = dind_rem / ptrs_per_block;
        uint32_t ind_idx = dind_rem % ptrs_per_block;
        
        std::vector<uint32_t> tind_block(ptrs_per_block, 0);
        read_block(inode.i_block[EXT2_TIND_BLOCK], tind_block.data());
        
        bool tind_changed = false;
        if (tind_block[tind_idx] == 0) {
            tind_block[tind_idx] = allocate_block();
            inode.i_blocks += block_size_ / 512;
            inode_changed = true;
            tind_changed = true;
        }
        
        std::vector<uint32_t> dind_block(ptrs_per_block, 0);
        read_block(tind_block[tind_idx], dind_block.data());
        
        bool dind_changed = false;
        if (dind_block[dind_idx] == 0) {
            dind_block[dind_idx] = allocate_block();
            inode.i_blocks += block_size_ / 512;
            inode_changed = true;
            dind_changed = true;
        }
        
        std::vector<uint32_t> ind_block(ptrs_per_block, 0);
        read_block(dind_block[dind_idx], ind_block.data());
        
        if (ind_block[ind_idx] == 0) {
            ind_block[ind_idx] = allocate_block();
            inode.i_blocks += block_size_ / 512;
            inode_changed = true;
            write_block(dind_block[dind_idx], ind_block.data());
        }
        
        if (dind_changed) {
            write_block(tind_block[tind_idx], dind_block.data());
        }
        if (tind_changed) {
            write_block(inode.i_block[EXT2_TIND_BLOCK], tind_block.data());
        }
        
        return ind_block[ind_idx];
    }
    
    throw std::runtime_error("File size exceeds maximum supported by ext2");
}

void Ext2FS::append_to_file(uint32_t inode_num, const std::string& data) {
    ext2_inode inode = get_inode(inode_num);
    
    uint32_t current_size = inode.i_size;
    uint32_t data_offset = 0;
    bool inode_changed = false;
    
    while (data_offset < data.size()) {
        uint32_t block_idx = current_size / block_size_;
        uint32_t block_offset = current_size % block_size_;
        uint32_t to_write = std::min((uint32_t)(data.size() - data_offset), block_size_ - block_offset);
        
        uint32_t target_block = get_or_allocate_block(inode, block_idx, inode_changed);
        
        std::vector<char> buf(block_size_, 0);
        if (block_offset > 0 || to_write < block_size_) {
            read_block(target_block, buf.data());
        }
        
        memcpy(buf.data() + block_offset, data.data() + data_offset, to_write);
        write_block(target_block, buf.data());
        
        current_size += to_write;
        data_offset += to_write;
        
        inode.i_size = current_size;
        inode_changed = true;
        
        if (inode_changed) {
            write_inode(inode_num, inode);
            inode_changed = false;
        }
    }
}

void Ext2FS::overwrite_file(uint32_t inode_num, const std::string& data) {
    ext2_inode inode = get_inode(inode_num);
    
    uint32_t current_size = 0;
    uint32_t data_offset = 0;
    bool inode_changed = false;
    
    while (data_offset < data.size()) {
        uint32_t block_idx = current_size / block_size_;
        uint32_t to_write = std::min((uint32_t)(data.size() - data_offset), block_size_);
        
        uint32_t target_block = get_or_allocate_block(inode, block_idx, inode_changed);
        
        std::vector<char> buf(block_size_, 0);
        memcpy(buf.data(), data.data() + data_offset, to_write);
        write_block(target_block, buf.data());
        
        current_size += to_write;
        data_offset += to_write;
    }
    
    inode.i_size = current_size;
    inode_changed = true;
    
    if (inode_changed) {
        write_inode(inode_num, inode);
    }
}
