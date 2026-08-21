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

string DictYamlStem(const string& filename) {
  if (!IsDictYamlFilename(filename))
    return {};
  return filename.substr(0, filename.size() - kDictYamlSuffix.size());
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

vector<string> ListAutoPackNames(const path& user_data_dir) {
  vector<string> names;
  for (const auto& file : ListDictYamlFiles(user_data_dir / kPacksDirName)) {
    names.push_back(DictYamlStem(file.filename().u8string()));
  }
  return names;
}

string AutoPackTableId(const string& dict_name, const string& pack_name) {
  return dict_name + "." + pack_name;
}

bool IsAutoPackTableId(const string& dict_name, const string& table_id) {
  if (dict_name.empty() || table_id.size() <= dict_name.size() + 1)
    return false;
  return table_id.compare(0, dict_name.size(), dict_name) == 0 &&
         table_id[dict_name.size()] == '.';
}

string AutoPackStem(const string& dict_name, const string& table_id) {
  if (!IsAutoPackTableId(dict_name, table_id))
    return table_id;
  return table_id.substr(dict_name.size() + 1);
}

void AppendAutoPacks(vector<string>* packs,
                     const string& dict_name,
                     const path& user_data_dir) {
  if (!packs)
    return;
  hash_set<string> existing;
  for (const auto& pack : *packs) {
    existing.insert(pack);
    existing.insert(AutoPackStem(dict_name, pack));
  }
  for (const auto& name : ListAutoPackNames(user_data_dir)) {
    if (existing.count(name))
      continue;
    string id = AutoPackTableId(dict_name, name);
    if (existing.count(id))
      continue;
    packs->push_back(id);
    existing.insert(id);
    existing.insert(name);
  }
}

}  // namespace rime
