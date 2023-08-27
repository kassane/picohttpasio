#include <fstream>

class FileReader {
public:
    bool readFile(const std::string& file_path, std::string& content) {
        std::ifstream file(file_path, std::ios::binary);
        if (file) {
            file.seekg(0, std::ios::end);
            content.resize(file.tellg());
            file.seekg(0, std::ios::beg);
            file.read(&content[0], content.size());
            file.close();
            return true;
        }
        return false;
    }
};