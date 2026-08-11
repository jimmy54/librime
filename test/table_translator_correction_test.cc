//
// Copyright RIME Developers
// Distributed under the BSD License
//
// Tests for table_translator spelling correction (enable_correction).
//
#include <algorithm>
#include <gtest/gtest.h>
#include <rime/candidate.h>
#include <rime/common.h>
#include <rime/config.h>
#include <rime/engine.h>
#include <rime/schema.h>
#include <rime/segmentation.h>
#include <rime/ticket.h>
#include <rime/translation.h>
#include <rime/dict/dict_compiler.h>
#include <rime/dict/dictionary.h>
#include <rime/gear/table_translator.h>

using namespace rime;

namespace {

struct CandInfo {
  string text;
  string type;
  string comment;
  double quality = 0;
};

vector<CandInfo> CollectCandidates(an<Translation> translation) {
  vector<CandInfo> result;
  if (!translation)
    return result;
  while (!translation->exhausted()) {
    auto cand = translation->Peek();
    if (cand) {
      result.push_back(
          {cand->text(), cand->type(), cand->comment(), cand->quality()});
    }
    if (!translation->Next())
      break;
  }
  return result;
}

size_t CountType(const vector<CandInfo>& cands, const string& type) {
  return std::count_if(cands.begin(), cands.end(),
                       [&](const CandInfo& c) { return c.type == type; });
}

bool HasText(const vector<CandInfo>& cands, const string& text) {
  return std::any_of(cands.begin(), cands.end(),
                     [&](const CandInfo& c) { return c.text == text; });
}

bool HasCorrectedText(const vector<CandInfo>& cands, const string& text) {
  return std::any_of(cands.begin(), cands.end(), [&](const CandInfo& c) {
    return c.type == "corrected" && c.text == text;
  });
}

}  // namespace

class TableTranslatorCorrectionTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    if (rebuilt_)
      return;
    Dictionary dict(
        "table_correction_test", {},
        {New<Table>(path{"table_correction_test.table.bin"})},
        New<Prism>(path{"table_correction_test.prism.bin"}));
    dict.Remove();
    DictCompiler compiler(&dict);
    if (!compiler.Compile(path())) {
      FAIL() << "failed to compile table_correction_test dictionary";
      return;
    }
    rebuilt_ = true;
  }

  the<Engine> CreateEngine(bool enable_correction) {
    auto* config = new Config;
    if (!config->LoadFromFile(path{"table_correction_test.schema.yaml"})) {
      delete config;
      ADD_FAILURE() << "failed to load table_correction_test.schema.yaml";
      return nullptr;
    }
    config->SetBool("translator/enable_correction", enable_correction);
    config->SetBool("translator/enable_completion", false);
    config->SetBool("translator/enable_sentence", false);
    config->SetBool("translator/enable_user_dict", false);

    auto* schema = new Schema("table_correction_test", config);
    the<Engine> engine(Engine::Create());
    engine->ApplySchema(schema);
    return engine;
  }

  an<Translation> Query(Engine* engine, const string& input) {
    Ticket ticket(engine, "translator", "table_translator");
    TableTranslator translator(ticket);
    Segment segment(0, input.length());
    segment.tags.insert("abc");
    return translator.Query(input, segment);
  }

  static bool rebuilt_;
};

bool TableTranslatorCorrectionTest::rebuilt_ = false;

TEST_F(TableTranslatorCorrectionTest, ExactMatchUnaffected) {
  auto engine = CreateEngine(true);
  ASSERT_TRUE(engine);
  auto cands = CollectCandidates(Query(engine.get(), "aaaa"));
  ASSERT_FALSE(cands.empty());
  EXPECT_EQ("工", cands.front().text);
  EXPECT_NE("corrected", cands.front().type);
  EXPECT_FALSE(HasCorrectedText(cands, "工"));
}

TEST_F(TableTranslatorCorrectionTest, DisabledCorrectionNoCorrectedType) {
  auto engine = CreateEngine(false);
  ASSERT_TRUE(engine);
  auto cands = CollectCandidates(Query(engine.get(), "saaa"));
  ASSERT_FALSE(cands.empty());
  EXPECT_EQ("模式", cands.front().text);
  EXPECT_EQ(0u, CountType(cands, "corrected"));
  EXPECT_FALSE(HasText(cands, "工"));
}

TEST_F(TableTranslatorCorrectionTest, NearKeyTypoYieldsCorrectedCandidate) {
  auto engine = CreateEngine(true);
  ASSERT_TRUE(engine);
  // "saaa": exact hit 模式; s→a recovers aaaa → 工
  auto cands = CollectCandidates(Query(engine.get(), "saaa"));
  ASSERT_FALSE(cands.empty());
  EXPECT_EQ("模式", cands.front().text);
  EXPECT_NE("corrected", cands.front().type);
  ASSERT_TRUE(HasCorrectedText(cands, "工"));

  auto it = std::find_if(cands.begin(), cands.end(), [](const CandInfo& c) {
    return c.type == "corrected" && c.text == "工";
  });
  ASSERT_NE(cands.end(), it);
  EXPECT_EQ("aaaa", it->comment);
  // exact candidate should rank no worse than correction
  EXPECT_GE(cands.front().quality, it->quality);
}

TEST_F(TableTranslatorCorrectionTest, ExactCandidatesPrecedeCorrections) {
  auto engine = CreateEngine(true);
  ASSERT_TRUE(engine);
  auto cands = CollectCandidates(Query(engine.get(), "saaa"));
  ASSERT_FALSE(cands.empty());
  size_t first_corrected = cands.size();
  for (size_t i = 0; i < cands.size(); ++i) {
    if (cands[i].type == "corrected") {
      first_corrected = i;
      break;
    }
  }
  ASSERT_LT(first_corrected, cands.size());
  for (size_t i = 0; i < first_corrected; ++i) {
    EXPECT_NE("corrected", cands[i].type);
  }
}

TEST_F(TableTranslatorCorrectionTest, CorrectionCandidateLimit) {
  auto engine = CreateEngine(true);
  ASSERT_TRUE(engine);
  auto cands = CollectCandidates(Query(engine.get(), "saaa"));
  EXPECT_LE(CountType(cands, "corrected"), 4u);
}

TEST_F(TableTranslatorCorrectionTest, MultiKeyNearTypo) {
  auto engine = CreateEngine(true);
  ASSERT_TRUE(engine);
  // "adsf" exact → 测试乙; can recover "asdf" → 测试 via adjacent swaps
  auto cands = CollectCandidates(Query(engine.get(), "adsf"));
  ASSERT_FALSE(cands.empty());
  EXPECT_TRUE(HasText(cands, "测试乙"));
  EXPECT_TRUE(HasCorrectedText(cands, "测试"));
}

TEST_F(TableTranslatorCorrectionTest, FarTypoNoSpuriousCorrection) {
  auto engine = CreateEngine(true);
  ASSERT_TRUE(engine);
  // far from any code in the tiny syllabary
  auto cands = CollectCandidates(Query(engine.get(), "mnbv"));
  EXPECT_EQ(0u, CountType(cands, "corrected"));
  EXPECT_TRUE(cands.empty() || !HasText(cands, "工"));
}

TEST_F(TableTranslatorCorrectionTest, IsolatedExactHasNoCorrectionNoise) {
  auto engine = CreateEngine(true);
  ASSERT_TRUE(engine);
  auto cands = CollectCandidates(Query(engine.get(), "qwer"));
  ASSERT_FALSE(cands.empty());
  EXPECT_EQ("远码", cands.front().text);
  EXPECT_EQ(0u, CountType(cands, "corrected"));
}
