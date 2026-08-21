//
// Copyright RIME Developers
// Distributed under the BSD License
//
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <rime/algo/syllabifier.h>
#include <rime/common.h>
#include <rime/dict/dict_compiler.h>
#include <rime/dict/dictionary.h>
#include <rime/dict/prism.h>
#include <rime/dict/table.h>
#include <rime/dict/user_dict_drop_in.h>

namespace fs = std::filesystem;

namespace {

using rime::path;
using rime::string;
using rime::vector;

string PinyinDictYaml(const string& name) {
  return "---\nname: " + name +
         "\nversion: \"1.0\"\nsort: by_weight\n...\n"
         "中\tzhong\t100\n国\tguo\t100\n";
}

const char kWubiDict[] = R"(---
name: uddi_wb
version: "1.0"
sort: by_weight
encoder:
  rules:
    - length_equal: 2
      formula: "AaAbBaBb"
...
工	aaaa	100
人	bbbb	100
)";

void WriteText(const path& file, const string& content) {
  if (!file.parent_path().empty())
    fs::create_directories(file.parent_path());
  std::ofstream out(file.c_str());
  ASSERT_TRUE(out.good()) << file;
  out << content;
}

void RemovePath(const path& file) {
  std::error_code ec;
  fs::remove(file, ec);
}

void RemoveTree(const path& dir) {
  std::error_code ec;
  fs::remove_all(dir, ec);
}

void WriteExtraDict(const string& stem, const string& body) {
  WriteText(path(rime::kExtraDictsDirName) / (stem + ".dict.yaml"), body);
}

rime::an<rime::Dictionary> MakeDict(const string& name) {
  vector<rime::of<rime::Table>> tables{
      rime::New<rime::Table>(path(name + ".table.bin"))};
  return rime::New<rime::Dictionary>(
      name, vector<string>{}, std::move(tables),
      rime::New<rime::Prism>(path(name + ".prism.bin")));
}

void RemoveArtifacts(const string& name) {
  RemovePath(path(name + ".table.bin"));
  RemovePath(path(name + ".prism.bin"));
  RemovePath(path(name + ".reverse.bin"));
  RemovePath(path(name + ".dict.yaml"));
}

bool CompileDict(rime::Dictionary* dict, bool enable_drop_in = true) {
  rime::DictCompiler compiler(dict);
  compiler.set_enable_drop_in(enable_drop_in);
  return compiler.Compile(path());
}

bool HasWord(rime::Dictionary* dict, const string& code, const string& text) {
  rime::DictEntryIterator it;
  dict->LookupWords(&it, code, false);
  for (; !it.exhausted(); it.Next()) {
    auto entry = it.Peek();
    if (entry && entry->text == text)
      return true;
  }
  return false;
}

bool HasPhrase(rime::Dictionary* dict,
               const string& input,
               const string& text) {
  rime::SyllableGraph graph;
  rime::Syllabifier syllabifier;
  if (syllabifier.BuildSyllableGraph(input, *dict->prism(), &graph) <= 0)
    return false;
  auto collector = dict->Lookup(graph, 0);
  if (!collector)
    return false;
  for (auto& item : *collector) {
    auto& it = item.second;
    for (; !it.exhausted(); it.Next()) {
      auto entry = it.Peek();
      if (entry && entry->text == text)
        return true;
    }
  }
  return false;
}

}  // namespace

class UserDictDropInTest : public ::testing::Test {
 protected:
  void SetUp() override {
    RemoveTree(path(rime::kExtraDictsDirName));
    RemoveTree(path("uddi_scan"));
  }
  void TearDown() override {
    RemoveTree(path(rime::kExtraDictsDirName));
    RemoveTree(path("uddi_scan"));
    RemoveArtifacts("uddi_py");
    RemoveArtifacts("uddi_wb");
  }
};

TEST_F(UserDictDropInTest, ListMissingDirectoryIsEmpty) {
  auto files = rime::ListDictYamlFiles(path("uddi_scan/missing"));
  EXPECT_TRUE(files.empty());
}

TEST_F(UserDictDropInTest, ListEmptyDirectoryIsEmpty) {
  fs::create_directories("uddi_scan/empty");
  auto files = rime::ListDictYamlFiles(path("uddi_scan/empty"));
  EXPECT_TRUE(files.empty());
}

TEST_F(UserDictDropInTest, ListOnlyDictYamlFilesSorted) {
  WriteText(path("uddi_scan/zoo.dict.yaml"),
            "---\nname: zoo\nversion: \"1\"\n...\n");
  WriteText(path("uddi_scan/alpha.dict.yaml"),
            "---\nname: alpha\nversion: \"1\"\n...\n");
  WriteText(path("uddi_scan/foo.txt"), "ignore");
  WriteText(path("uddi_scan/bar.yaml"), "ignore: true\n");
  WriteText(path("uddi_scan/note.dict.yaml.bak"), "ignore");
  fs::create_directories("uddi_scan/nested");
  WriteText(path("uddi_scan/nested/hidden.dict.yaml"),
            "---\nname: hidden\nversion: \"1\"\n...\n");

  auto files = rime::ListDictYamlFiles(path("uddi_scan"));
  ASSERT_EQ(2u, files.size());
  EXPECT_EQ("alpha.dict.yaml", files[0].filename().u8string());
  EXPECT_EQ("zoo.dict.yaml", files[1].filename().u8string());
}

TEST_F(UserDictDropInTest, ExtraDictEncodesUncodedPhrase) {
  WriteText(path("uddi_py.dict.yaml"), PinyinDictYaml("uddi_py"));
  WriteExtraDict("contact", R"(---
name: contact
version: "1.0"
...
中国		100
Tom	tom	100
)");
  auto dict = MakeDict("uddi_py");
  dict->Remove();
  ASSERT_TRUE(CompileDict(dict.get()));
  ASSERT_TRUE(dict->Load());
  EXPECT_TRUE(HasWord(dict.get(), "zhong", "中"));
  EXPECT_TRUE(HasPhrase(dict.get(), "zhongguo", "中国"));
  EXPECT_TRUE(HasWord(dict.get(), "tom", "Tom"));
}

TEST_F(UserDictDropInTest, ExtraDictMergesMultipleFiles) {
  WriteText(path("uddi_py.dict.yaml"), PinyinDictYaml("uddi_py"));
  WriteExtraDict("aaa", R"(---
name: aaa
version: "1.0"
...
中国		100
)");
  WriteExtraDict("zzz", R"(---
name: zzz
version: "1.0"
...
国人	guo	100
)");
  auto dict = MakeDict("uddi_py");
  dict->Remove();
  ASSERT_TRUE(CompileDict(dict.get()));
  ASSERT_TRUE(dict->Load());
  EXPECT_TRUE(HasPhrase(dict.get(), "zhongguo", "中国"));
  EXPECT_TRUE(HasWord(dict.get(), "guo", "国人"));
}

TEST_F(UserDictDropInTest, ExtraDictDeletionRebuildsAndDropsEntries) {
  WriteText(path("uddi_py.dict.yaml"), PinyinDictYaml("uddi_py"));
  WriteExtraDict("contact", R"(---
name: contact
version: "1.0"
...
中国		100
)");
  auto dict = MakeDict("uddi_py");
  dict->Remove();
  ASSERT_TRUE(CompileDict(dict.get()));
  ASSERT_TRUE(dict->Load());
  ASSERT_TRUE(HasPhrase(dict.get(), "zhongguo", "中国"));
  uint32_t checksum_with_extra = dict->primary_table()->dict_file_checksum();
  dict->primary_table()->Close();

  RemovePath(path(rime::kExtraDictsDirName) / "contact.dict.yaml");
  ASSERT_TRUE(CompileDict(dict.get()));
  ASSERT_TRUE(dict->Load());
  EXPECT_NE(checksum_with_extra, dict->primary_table()->dict_file_checksum());
  EXPECT_FALSE(HasPhrase(dict.get(), "zhongguo", "中国"));
}

TEST_F(UserDictDropInTest, ExtraDictContentChangeUpdatesChecksum) {
  WriteText(path("uddi_py.dict.yaml"), PinyinDictYaml("uddi_py"));
  WriteExtraDict("contact", R"(---
name: contact
version: "1.0"
...
中国		100
)");
  auto dict = MakeDict("uddi_py");
  dict->Remove();
  ASSERT_TRUE(CompileDict(dict.get()));
  ASSERT_TRUE(dict->Load());
  uint32_t first = dict->primary_table()->dict_file_checksum();
  dict->primary_table()->Close();

  ASSERT_TRUE(CompileDict(dict.get()));
  ASSERT_TRUE(dict->Load());
  EXPECT_EQ(first, dict->primary_table()->dict_file_checksum());
  dict->primary_table()->Close();

  WriteExtraDict("contact", R"(---
name: contact
version: "1.0"
...
中国		200
)");
  ASSERT_TRUE(CompileDict(dict.get()));
  ASSERT_TRUE(dict->Load());
  EXPECT_NE(first, dict->primary_table()->dict_file_checksum());
}

TEST_F(UserDictDropInTest, ExtraDictSkipsSelfNamedAndInvalidYaml) {
  WriteText(path("uddi_py.dict.yaml"), PinyinDictYaml("uddi_py"));
  WriteExtraDict("self", R"(---
name: uddi_py
version: "1.0"
...
幽灵		100
)");
  WriteExtraDict("bad", "not a dict header\n");
  WriteExtraDict("ok", R"(---
name: ok
version: "1.0"
...
中国		100
)");
  auto dict = MakeDict("uddi_py");
  dict->Remove();
  ASSERT_TRUE(CompileDict(dict.get()));
  ASSERT_TRUE(dict->Load());
  EXPECT_TRUE(HasPhrase(dict.get(), "zhongguo", "中国"));
  EXPECT_FALSE(HasPhrase(dict.get(), "zhongguo", "幽灵"));
}

TEST_F(UserDictDropInTest, ExtraDictWubiEncodesUncodedWord) {
  WriteText(path("uddi_wb.dict.yaml"), kWubiDict);
  WriteExtraDict("contact", R"(---
name: contact
version: "1.0"
...
工人		100
)");
  auto dict = MakeDict("uddi_wb");
  dict->Remove();
  ASSERT_TRUE(CompileDict(dict.get()));
  ASSERT_TRUE(dict->Load());
  EXPECT_TRUE(HasWord(dict.get(), "aabb", "工人"));
}

TEST_F(UserDictDropInTest, DropInDisabledSkipsExtraDicts) {
  WriteText(path("uddi_py.dict.yaml"), PinyinDictYaml("uddi_py"));
  WriteExtraDict("contact", R"(---
name: contact
version: "1.0"
...
中国		100
)");
  auto dict = MakeDict("uddi_py");
  dict->Remove();
  ASSERT_TRUE(CompileDict(dict.get(), /*enable_drop_in=*/false));
  ASSERT_TRUE(dict->Load());
  EXPECT_FALSE(HasPhrase(dict.get(), "zhongguo", "中国"));
}
