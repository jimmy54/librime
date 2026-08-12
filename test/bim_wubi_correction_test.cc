//
// Copyright RIME Developers
// Distributed under the BSD License
//
// Integration cases against bim-wubi + SharedSupport (real wubi86 table).
// Skips automatically when the local schema directories are absent.
//
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <rime/common.h>
#include <rime/config.h>
#include <rime/dict/corrector.h>
#include <rime/dict/dictionary.h>
#include <rime/dict/prism.h>
#include <rime/dict/table.h>

using namespace rime;
namespace fs = std::filesystem;

namespace {

// Default paths provided for local bim hmos resources. Override with env:
//   BIM_SHARED_SUPPORT, BIM_WUBI_USER_DIR
const char* kDefaultSharedSupport =
    "/Users/jimmy54/Documents/github/bim/app/ime/hmosbim/hmosbim/resources/"
    "SharedSupport";
const char* kDefaultWubiUserDir =
    "/Users/jimmy54/Documents/github/bim/app/ime/hmosbim/hmosbim/resources/"
    "space/schemas/bim-wubi";

path EnvOrDefault(const char* env_name, const char* fallback) {
  if (const char* v = std::getenv(env_name)) {
    if (*v)
      return path(v);
  }
  return path(fallback);
}

path SharedSupportDir() {
  return EnvOrDefault("BIM_SHARED_SUPPORT", kDefaultSharedSupport);
}

path WubiUserDir() {
  return EnvOrDefault("BIM_WUBI_USER_DIR", kDefaultWubiUserDir);
}

path WubiBuildDir() {
  // Prefer SharedSupport/build (has table.bin); fall back to user schema build.
  path shared_build = SharedSupportDir() / "build";
  if (fs::exists(shared_build / "wubi86.table.bin") &&
      fs::exists(shared_build / "wubi86.prism.bin")) {
    return shared_build;
  }
  return WubiUserDir() / "build";
}

bool WubiBinsAvailable() {
  path build = WubiBuildDir();
  return fs::exists(build / "wubi86.table.bin") &&
         fs::exists(build / "wubi86.prism.bin");
}

struct CorrectionHit {
  string corrected_code;
  string text;
  size_t distance = 0;
};

// Mirrors TableTranslator::MakeCorrectionTranslation against a live dictionary.
vector<CorrectionHit> LookupCorrections(Dictionary* dict,
                                        Corrector* corrector,
                                        const string& code,
                                        size_t max_corrections = 4) {
  vector<CorrectionHit> hits;
  if (!dict || !dict->loaded() || !corrector || code.empty())
    return hits;

  auto& prism = *dict->prism();
  set<SyllableId> exact;
  Prism::Match m{0, 0};
  if (prism.GetValue(code, &m.value))
    exact.insert(m.value);

  corrector::Corrections corrections;
  corrector->ToleranceSearch(prism, code, &corrections, 5);

  vector<pair<SyllableId, corrector::Correction>> items(corrections.begin(),
                                                        corrections.end());
  std::sort(items.begin(), items.end(),
            [](const auto& a, const auto& b) {
              if (a.second.distance != b.second.distance)
                return a.second.distance < b.second.distance;
              if (a.second.length != b.second.length)
                return a.second.length > b.second.length;
              return a.first < b.first;
            });

  hash_set<string> seen;
  for (const auto& item : items) {
    if (hits.size() >= max_corrections)
      break;
    if (exact.count(item.first))
      continue;
    if (item.second.length != code.length())
      continue;
    for (auto accessor = prism.QuerySpelling(item.first); !accessor.exhausted();
         accessor.Next()) {
      if (hits.size() >= max_corrections)
        break;
      auto props = accessor.properties();
      if (props.type != kNormalSpelling || props.is_correction)
        continue;
      string corrected =
          dict->primary_table()->GetSyllableById(accessor.syllable_id());
      if (corrected.empty() || corrected == code)
        continue;
      DictEntryIterator iter;
      if (!dict->LookupWords(&iter, corrected, false) || iter.exhausted())
        continue;
      while (!iter.exhausted() && hits.size() < max_corrections) {
        auto entry = iter.Peek();
        if (entry && !seen.count(entry->text)) {
          seen.insert(entry->text);
          hits.push_back({corrected, entry->text, item.second.distance});
        }
        if (!iter.Next())
          break;
      }
    }
  }
  return hits;
}

}  // namespace

class BimWubiCorrectionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!WubiBinsAvailable()) {
      GTEST_SKIP() << "wubi86 binaries not found under " << WubiBuildDir();
    }
    path build = WubiBuildDir();
    dict_.reset(new Dictionary(
        "wubi86", {}, {New<Table>(build / "wubi86.table.bin")},
        New<Prism>(build / "wubi86.prism.bin")));
    ASSERT_TRUE(dict_->Load());
    corrector_.reset(new NearSearchCorrector);
  }

  the<Dictionary> dict_;
  the<Corrector> corrector_;
};

TEST_F(BimWubiCorrectionTest, ResourceLayoutPresent) {
  EXPECT_TRUE(fs::exists(SharedSupportDir() / "wubi86.schema.yaml"));
  EXPECT_TRUE(fs::exists(SharedSupportDir() / "wubi86.dict.yaml"));
  EXPECT_TRUE(fs::is_directory(WubiUserDir()));
  EXPECT_TRUE(fs::exists(WubiBuildDir() / "wubi86.prism.bin"));
  EXPECT_TRUE(fs::exists(WubiBuildDir() / "wubi86.table.bin"));
}

TEST_F(BimWubiCorrectionTest, ExactCode工) {
  DictEntryIterator iter;
  ASSERT_GT(dict_->LookupWords(&iter, "aaaa", false), 0u);
  ASSERT_FALSE(iter.exhausted());
  EXPECT_EQ("工", iter.Peek()->text);
}

TEST_F(BimWubiCorrectionTest, ExactCode我) {
  DictEntryIterator iter;
  ASSERT_GT(dict_->LookupWords(&iter, "trnt", false), 0u);
  ASSERT_FALSE(iter.exhausted());
  EXPECT_EQ("我", iter.Peek()->text);
}

TEST_F(BimWubiCorrectionTest, TypoSaaaCorrectsTo工) {
  // saaa is itself a valid code (模式); correction should still recover aaaa→工
  DictEntryIterator exact;
  ASSERT_GT(dict_->LookupWords(&exact, "saaa", false), 0u);

  auto hits = LookupCorrections(dict_.get(), corrector_.get(), "saaa");
  ASSERT_FALSE(hits.empty());
  bool found = false;
  for (const auto& h : hits) {
    if (h.corrected_code == "aaaa" && h.text == "工") {
      found = true;
      EXPECT_GE(h.distance, 1u);
      break;
    }
  }
  EXPECT_TRUE(found) << "expected correction saaa → aaaa/工";
}

TEST_F(BimWubiCorrectionTest, TypoUglhCorrectsTo美国WithinLimit) {
  // Regression: uglh used to be starved by unordered correction hits, while
  // MakeSentence split into ugl|h (盖+上). Distance-ordered top-N must keep 美国.
  auto hits = LookupCorrections(dict_.get(), corrector_.get(), "uglh", 4);
  ASSERT_FALSE(hits.empty());
  bool found = std::any_of(hits.begin(), hits.end(), [](const CorrectionHit& h) {
    return h.corrected_code == "uglg" && h.text == "美国";
  });
  EXPECT_TRUE(found) << "expected uglh → uglg/美国 within top-4 corrections";
  if (!hits.empty()) {
    EXPECT_LE(hits.front().distance, 1u);
  }
}


TEST_F(BimWubiCorrectionTest, TypoAasaCorrectsTo工) {
  auto hits = LookupCorrections(dict_.get(), corrector_.get(), "aasa");
  ASSERT_FALSE(hits.empty());
  bool found = std::any_of(hits.begin(), hits.end(), [](const CorrectionHit& h) {
    return h.corrected_code == "aaaa" && h.text == "工";
  });
  EXPECT_TRUE(found) << "expected correction aasa → aaaa/工";
}

TEST_F(BimWubiCorrectionTest, CorrectorRecoversTrntFromNeighborTypo) {
  // Real wubi table is dense: capped word collection may starve late hits.
  // Assert at Corrector/Prism layer that trny can recover syllable "trnt".
  corrector::Corrections corrections;
  corrector_->ToleranceSearch(*dict_->prism(), "trny", &corrections, 5);
  ASSERT_FALSE(corrections.empty());
  bool found = false;
  for (const auto& item : corrections) {
    if (item.second.length != 4)
      continue;
    for (auto accessor = dict_->prism()->QuerySpelling(item.first);
         !accessor.exhausted(); accessor.Next()) {
      if (dict_->primary_table()->GetSyllableById(accessor.syllable_id()) ==
          "trnt") {
        found = true;
        EXPECT_GE(item.second.distance, 1u);
        break;
      }
    }
    if (found)
      break;
  }
  EXPECT_TRUE(found) << "NearSearchCorrector should map trny → trnt";
}

TEST_F(BimWubiCorrectionTest, CorrectionRespectsMaxLimit) {
  auto hits = LookupCorrections(dict_.get(), corrector_.get(), "saaa", 4);
  EXPECT_LE(hits.size(), 4u);
}

TEST_F(BimWubiCorrectionTest, FarNoiseDoesNotInvent工) {
  // unlikely to be a single-key neighbor path into aaaa
  auto hits = LookupCorrections(dict_.get(), corrector_.get(), "mnbv");
  bool found_gong = std::any_of(
      hits.begin(), hits.end(),
      [](const CorrectionHit& h) { return h.text == "工"; });
  EXPECT_FALSE(found_gong);
}

TEST_F(BimWubiCorrectionTest, SchemaMentionsTableTranslator) {
  path schema = SharedSupportDir() / "wubi86.schema.yaml";
  ASSERT_TRUE(fs::exists(schema));
  Config config;
  ASSERT_TRUE(config.LoadFromFile(schema));
  auto translators = config.GetList("engine/translators");
  ASSERT_TRUE(bool(translators));
  bool has_table = false;
  for (size_t i = 0; i < translators->size(); ++i) {
    if (auto v = As<ConfigValue>(translators->GetAt(i))) {
      if (v->str() == "table_translator")
        has_table = true;
    }
  }
  EXPECT_TRUE(has_table);
  // document current default: correction is opt-in
  bool enabled = false;
  config.GetBool("translator/enable_correction", &enabled);
  EXPECT_FALSE(enabled);
}
