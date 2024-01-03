
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <ctime>

class WordManager {
public:
    WordManager(const std::string& filename) {
        words = readWordsFromFile(filename);
        srand(static_cast<unsigned int>(time(nullptr)));
    }

    std::string getRandomWord() const {
        int randomIndex = rand() % words.size();
        return words[randomIndex];
    }

private:
    std::vector<std::string> readWordsFromFile(const std::string& filename) const {
        std::vector<std::string> words;
        std::ifstream file(filename);

        if (file.is_open()) {
            std::string word;
            while (std::getline(file, word)) {
                words.push_back(word);
            }
            file.close();
        } else {
            std::cerr << "Unable to open file: " << filename << std::endl;
            exit(EXIT_FAILURE);
        }

        return words;
    }

    std::vector<std::string> words;
};
