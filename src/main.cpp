#include "ext2_fs.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>

void help() {
    std::cout << "Commands:\n"
              << "  info                 - Print superblock and group descriptors\n"
              << "  ls                   - Traverse and print filesystem layout\n"
              << "  cat <path>           - Read file contents\n"
              << "  write <path> <data>  - Overwrite file with data\n"
              << "  append <path> <data> - Append data to file\n"
              << "  test_concurrent <path> - Start 10 threads to read the file simultaneously\n"
              << "  exit                 - Exit\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <ext2_image>\n";
        return 1;
    }

    Ext2FS fs(argv[1]);
    if (!fs.mount()) {
        std::cerr << "Failed to mount filesystem.\n";
        return 1;
    }

    std::cout << "Mounted ext2 image: " << argv[1] << "\n";
    help();

    std::string cmd;
    while (true) {
        std::cout << "> ";
        if (!(std::cin >> cmd)) break;

        if (cmd == "exit") {
            break;
        } else if (cmd == "info") {
            fs.print_super_block();
            fs.print_group_descriptors();
        } else if (cmd == "ls") {
            fs.traverse_root();
        } else if (cmd == "cat") {
            std::string path;
            std::cin >> path;
            try {
                std::string content = fs.read_file(path);
                std::cout << content << "\n";
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << "\n";
            }
        } else if (cmd == "write" || cmd == "append") {
            std::string path;
            std::string data;
            std::cin >> path;
            std::getline(std::cin, data); // rest of line
            if (!data.empty() && data[0] == ' ') {
                data = data.substr(1);
            }
            
            bool is_append = (cmd == "append");
            if (fs.write_file(path, data, is_append)) {
                std::cout << "Write successful.\n";
            } else {
                std::cerr << "Write failed.\n";
            }
        } else if (cmd == "test_concurrent") {
            std::string path;
            std::cin >> path;
            
            auto reader = [&fs, path](int id) {
                try {
                    std::string content = fs.read_file(path);
                    std::cout << "Thread " << id << " read " << content.size() << " bytes.\n";
                } catch (const std::exception& e) {
                    std::cerr << "Thread " << id << " error: " << e.what() << "\n";
                }
            };
            
            std::vector<std::thread> threads;
            for (int i=0; i<10; ++i) {
                threads.emplace_back(reader, i);
            }
            for (auto& t : threads) {
                t.join();
            }
        } else {
            std::cout << "Unknown command.\n";
            help();
        }
    }

    return 0;
}
