#include <axonvex_core/detail/filesystem_compat.hpp>
#include <axonvex_io/files/fileManager.hpp>
#include <gtest/gtest.h>

namespace fs = axonvex_fs;
using axonvex::io::files::FileManager;

TEST(FileManagerTest, BasicReadWrite) {
    auto tmp = fs::temp_directory_path() / "axonvex_fm_test.txt";
    std::string path = tmp.string();

    ASSERT_TRUE(FileManager::writeText(path, "hello"));
    ASSERT_TRUE(FileManager::exists(path));
    ASSERT_TRUE(FileManager::isFile(path));

    auto content = FileManager::readText(path);
    EXPECT_EQ(content, "hello");

    fs::remove(tmp);
}
