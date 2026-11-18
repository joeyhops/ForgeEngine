#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace forge::test {

class TempDir {
public:
  TempDir();
  ~TempDir();
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  const std::filesystem::path& path() const { return m_path; }
  std::string file(const std::string& name) const { return (m_path / name).string(); }
  std::string write(const std::string& name, std::string_view contents) const;
  std::string read(const std::string& name) const;

private:
  std::filesystem::path m_path;
};

}
