#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <initializer_list>
#include <sys/stat.h>
#include <unistd.h>
#include <3ds.h>
#include <dirent.h>

#include "3dssettings.h"
#include "3dsfiles.h"

inline std::string operator "" s(const char* s, size_t length) {
    return std::string(s, length);
}

const std::initializer_list<std::string> VALID_ROM_EXTENSIONS = {".smc", ".sfc", ".fig"};
static std::unordered_map<std::string, DirectoryStatusEntry> checkedDirectories;
static std::unordered_map<std::string, std::vector<unsigned char>> storedFiles;
static std::vector<std::string> thumbnailDirectories;

static char currentDir[_MAX_PATH] = "";
static char currentThumbnailDir[_MAX_PATH] = "";

void file3dsSetthumbnailDirectories(const char *type) {
    snprintf(currentThumbnailDir, _MAX_PATH - 1, "%s/%s/%s", settings3DS.RootDir, "thumbnails", type);   
    
    DIR* directory = opendir(currentThumbnailDir);
    if (directory == nullptr) {
        return;
    }

    struct dirent* entry;
    
    while ((entry = readdir(directory)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            thumbnailDirectories.emplace_back(std::string(entry->d_name));
        }
    }

    closedir(directory);
    std::sort(thumbnailDirectories.begin(), thumbnailDirectories.end());
}

//----------------------------------------------------------------------
// Initialize the library
//----------------------------------------------------------------------
void file3dsInitialize(void)
{
    // create required directories if not available
    std::vector<std::string> directories = {"", "configs", "saves", "savestates", "screenshots"};
    for (const auto& dir : directories) {
        static char reqDir[_MAX_PATH];
        snprintf(reqDir, _MAX_PATH - 1, "%s/%s", settings3DS.RootDir, dir.c_str());

        DIR* d = opendir(reqDir);
        if (d)
            closedir(d);
        else {
            mkdir(reqDir, 0777);
        }
    }

    directories.clear();
    file3dsSetthumbnailDirectories("snaps");

    getcwd(currentDir, _MAX_PATH);
#ifdef RELEASE
    if (currentDir[0] == '/')
    {
        char tempDir[_MAX_PATH];

        sprintf(tempDir, "sdmc:%s", currentDir);
        strcpy(currentDir, tempDir);
    }
#endif
}

void file3dsFinalize(void)
{
    thumbnailDirectories.clear();
    storedFiles.clear();
    checkedDirectories.clear();
}


//----------------------------------------------------------------------
// Gets the current directory.
//----------------------------------------------------------------------
char *file3dsGetCurrentDir(void)
{
    return currentDir;
}


//----------------------------------------------------------------------
// Go up or down a level.
//----------------------------------------------------------------------
void file3dsGoUpOrDownDirectory(const DirectoryEntry& entry) {
    if (entry.Type == FileEntryType::ParentDirectory) {
        file3dsGoToParentDirectory();
    } else if (entry.Type == FileEntryType::ChildDirectory) {
        file3dsGoToChildDirectory(entry.Filename.c_str());
    }
}

//----------------------------------------------------------------------
// Count the directory depth. 1 = root directory
//----------------------------------------------------------------------
int file3dsCountDirectoryDepth(char *dir)
{
    int depth = 0;
    for (int i = 0; i < strlen(dir); i++)
        if (dir[i] == '/')
            depth++;
    return depth;
}

//----------------------------------------------------------------------
// Go up to the parent directory.
//----------------------------------------------------------------------
void file3dsGoToParentDirectory(void)
{
    int len = strlen(currentDir);

    if (len > 1)
    {
        for (int i = len - 2; i>=0; i--)
        {
            if (currentDir[i] == '/')
            {
                currentDir[i + 1] = 0;
                break;
            }
        }
    }
}

//----------------------------------------------------------------------
// Checks if file exists.
//----------------------------------------------------------------------
bool IsFileExists(const char * filename) {
    if (FILE * file = fopen(filename, "r")) {
        fclose(file);
        return true;
    }
    return false;
}


//----------------------------------------------------------------------
// Go up to the child directory.
//----------------------------------------------------------------------
void file3dsGoToChildDirectory(const char* childDir)
{
    strncat(currentDir, &childDir[2], _MAX_PATH);
    strncat(currentDir, "/", _MAX_PATH);
}

std::vector<unsigned char> file3dsGetStoredBufferByFilename(const std::string& filename) {
    std::vector<unsigned char> empty;

    auto it = storedFiles.find(filename);
    if (it != storedFiles.end()) {
        return it->second;
    } else {
        return empty;
    }
}

bool file3dsAddFileBufferToMemory(const std::string& filename) {
    if (filename.empty()) {
        return false;
    }

    // file already stored
    if (!file3dsGetStoredBufferByFilename(filename).empty()) {
       return false;
    }

    std::ifstream file(filename, std::ios::binary);

    if (!file) {
        return false;
    }
    
    // Get the file size
    file.seekg(0, std::ios::end);
    std::streampos fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // Read the file data into a buffer
    std::vector<unsigned char> buffer(fileSize);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    storedFiles[filename] = std::move(buffer);
    file.close();

    return true;
}

//----------------------------------------------------------------------
// Fetch all file names with any of the given extensions
//----------------------------------------------------------------------
void file3dsGetFiles(std::vector<DirectoryEntry>& files)
{
    files.clear();

    if (currentDir[0] == '/')
    {
        char tempDir[_MAX_PATH];
        sprintf(tempDir, "sdmc:%s", currentDir);
        strcpy(currentDir, tempDir);
    }

    struct dirent* dir;
    DIR* d = opendir(currentDir);

    if (file3dsCountDirectoryDepth(currentDir) > 1)
    {
        // Insert the parent directory.
        files.emplace_back(".. (Up to Parent Directory)"s, FileEntryType::ParentDirectory);
    }

    if (d)
    {
        unsigned short romCount = 0;
        while ((dir = readdir(d)) != NULL)
        {
            if (dir->d_name[0] == '.')
                continue;
            if (dir->d_type == DT_DIR)
            {
                files.emplace_back(std::string("\x01 ") + std::string(dir->d_name), FileEntryType::ChildDirectory);
            }
            if (dir->d_type == DT_REG)
            {
                if (file3dsIsValidFilename(dir->d_name))
                {
                    files.emplace_back(std::string(dir->d_name), FileEntryType::File);
                    romCount++;
                }
            }
        }

        closedir(d);

        if (checkedDirectories.find(currentDir) == checkedDirectories.end()) {
            file3dsSetDirStatus(std::string(currentDir), 0, romCount);
        }
    }

    std::sort( files.begin(), files.end(), [](const DirectoryEntry& a, const DirectoryEntry& b) {
        return std::tie(a.Type, a.Filename) < std::tie(b.Type, b.Filename);
    } );
}

void file3dsGetDirStatus(const std::string& lookupId, bool& completed, unsigned short& currentRomCount, unsigned short& totalRomCount) {
    // this should actually never be true, but just in case
    if (checkedDirectories.find(lookupId) == checkedDirectories.end()) {
        completed = true;
        currentRomCount = 0;
        totalRomCount = 0;
    } else {
        completed = checkedDirectories[lookupId].completed;
        currentRomCount = checkedDirectories[lookupId].currentRomCount;
        totalRomCount = checkedDirectories[lookupId].totalRomCount;
    }
}

void file3dsSetDirStatus(const std::string& lookupId, unsigned short currentRomCount, unsigned short totalRomCount) {
    checkedDirectories[lookupId].currentRomCount = currentRomCount;
    checkedDirectories[lookupId].totalRomCount = totalRomCount;
    checkedDirectories[lookupId].completed = currentRomCount == totalRomCount;
}

bool file3dsIsValidFilename(const char* filename) {
    std::string validFilename(filename);
    
    if (validFilename.empty() || validFilename[0] == '.')
        return false;

    size_t dotIndex = validFilename.find_last_of('.');
    if (dotIndex == std::string::npos || dotIndex >= validFilename.size() - 1) {
        return false;
    }

    std::string extension = validFilename.substr(dotIndex);
    auto it = std::find(VALID_ROM_EXTENSIONS.begin(), VALID_ROM_EXTENSIONS.end(), extension);
    
    return it != VALID_ROM_EXTENSIONS.end();
}

std::string file3dsGetFileBasename(const char* filename, bool ext) {
    std::string basename(filename);

    size_t start = basename.find_last_of("/\\");
    size_t end = ext ? basename.size() : basename.find_last_of(".");
    
    if (start != std::string::npos && end != std::string::npos && end > start) {
        basename = basename.substr(start + 1, end - start - 1);
    } else {
        basename = basename.substr(start + 1, end);
    }

    return basename;
}

std::string file3dsGetTrimmedFileBasename(const char* filename, bool ext) {
    std::string basename = file3dsGetFileBasename(filename, ext);

    // remove everything after the actual filename
    // e.g. "Donkey Kong Country   (USA) (V1.2) [!]" -> "Donkey Kong Country"
    std::size_t startPos = basename.find_first_of("([");
    if (startPos != std::string::npos) {
        std:: string extension;

        // quite messy but at least ensures that invalid filenames wouldn't cause a crash
        if (!ext)
            extension = "";
        else {
            size_t dotIndex = basename.find_last_of('.');
            if (dotIndex != std::string::npos && dotIndex < basename.size() - 1) {
                extension = basename.substr(dotIndex);
            }
        }
        
        // remove whitespace
        std::size_t endPos = basename.find_last_not_of(" \t\n\r\f\v", startPos - 1);
        return (basename.substr(0, endPos + 1) + extension);
    }

    return basename;
}

std::string file3dsGetThumbnailFilenameByBasename(const std::string& basename, const char* ext) {
    if (thumbnailDirectories.empty()) {
        return "";
    }

    std::string basenameUppercase;
    std::transform(basename.begin(), basename.end(), std::back_inserter(basenameUppercase), [](unsigned char c) {
        return std::toupper(c);
    });

    char firstChar = std::toupper(basenameUppercase[0]);

    // filenames starting with a non-alpha char
    if (!std::isalpha(firstChar)) {
        return std::string(currentThumbnailDir) + "/#/" + basename + ".png";
    }
    
    std::string subDir;

    for (const std::string& dirName : thumbnailDirectories) {
        std::string dirNameUppercase;
        std::transform(dirName.begin(), dirName.end(), std::back_inserter(dirNameUppercase), [](unsigned char c) {
            return std::toupper(c);
        });

        size_t separatorPos = dirNameUppercase.find("-");

        if (separatorPos != std::string::npos) {
            std::string firstPart = dirNameUppercase.substr(0, separatorPos);
            std::string secondPart = dirNameUppercase.substr(separatorPos + 1);
            
            if (firstChar != firstPart[0] && firstChar != secondPart[0]) {
                continue;
            }

            if (basenameUppercase.length() <= 1) {
                subDir = dirName;
                break;
            }

            char secondChar = basenameUppercase[1];
            char secondCharStart = (firstPart.length() > 1) ? firstPart[1] : secondChar;
            char secondCharEnd = (secondPart.length() > 1) ? secondPart[1] : secondChar;
            
            for (char c = secondCharStart; c <= secondCharEnd; ++c) {
                if (c == secondChar) {
                    subDir = dirName;
                    break;
                }
            }

            if (!subDir.empty()) {
                break;
            }
        } else {
            if (basenameUppercase.compare(0, dirNameUppercase.length(), dirNameUppercase) == 0) {
                subDir = dirName;
                break;
            }
        }
    }

    if (subDir.empty()) {
        return "";
    }

    return std::string(currentThumbnailDir) + "/" + subDir + "/" + basename + ".png";
}

// get the associated filename of the current game (e.g. savestate, config, border, etc.)
std::string file3dsGetAssociatedFilename(const char* filename, const char* ext, const char* targetDir, bool trimmed) {
    std::string basename = trimmed ? file3dsGetTrimmedFileBasename(filename, false) : file3dsGetFileBasename(filename, false);
    std::string extension = ext != nullptr ? std::string(ext) : "";

    if (targetDir == "thumbnails") {    
        return file3dsGetThumbnailFilenameByBasename(basename, ext);
    }
    
    if (targetDir != nullptr) {
        return std::string(settings3DS.RootDir) + "/" + targetDir + "/" + basename + extension;
    }

    // if targetDir is undefined, use current game directory
    std::string dir = std::string(filename);
    size_t lastSlashPos = dir.find_last_of('/');
    if (lastSlashPos != std::string::npos) {
        return dir.substr(0, lastSlashPos) + "/" + basename + extension;
    }

    return basename + extension;
}