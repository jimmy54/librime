//
// Copyright RIME Developers
// Distributed under the BSD License
//
#include <algorithm>
#include <filesystem>
#include <rime/dict/user_dict_drop_in.h>

namespace rime {

namespace fs = std::filesystem;

namespace {

const string kDictYamlSuffix = ".dict.yaml";

}  // namespace

bool IsDictYamlFilename(const string& name) {
  return name.size() >= kDictYamlSuffix.size() &&
         name.compare(name.size() - kDictYamlSuffix.size(),
                      kDictYamlSuffix.size(), kDictYamlSuffix) == 0;
}

vector<path> ListDictYamlFiles(const path& dir) {
  vector<path> files;
  std::error_code ec;
  if (!fs::is_directory(dir, ec) || ec)
    return files;
  for (fs::directory_iterator iter(dir, ec), end; !ec && iter != end;
       iter.increment(ec)) {
    if (ec)
      break;
    std::error_code file_ec;
    if (!iter->is_regular_file(file_ec) || file_ec)
      continue;
    auto name = iter->path().filename().u8string();
    if (IsDictYamlFilename(name))
      files.push_back(iter->path());
  }
  std::sort(files.begin(), files.end(), [](const path& a, const path& b) {
    return a.filename().u8string() < b.filename().u8string();
  });
  return files;
}

vector<path> ListExtraDictFiles(const path& user_data_dir) {
  return ListDictYamlFiles(user_data_dir / kExtraDictsDirName);
}

}  // namespace rime
