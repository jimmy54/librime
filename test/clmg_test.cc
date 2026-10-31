//
// Copyright RIME Developers
// Distributed under the BSD License
//

#include <gtest/gtest.h>

#ifdef RIME_TEST_CLMG

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <rime/candidate.h>
#include <rime/config.h>
#include <rime/dict/vocabulary.h>
#include <rime/gear/grammar.h>
#include <rime/gear/poet.h>
#include <rime/gear/translator_commons.h>
#include <rime/translation.h>
#include <utf8.h>

#include "clmg_model.h"

using namespace rime;

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint32_t kShi = 0x4E16;  // 世
constexpr uint32_t kNi = 0x4F60;   // 你
constexpr uint32_t kHao = 0x597D;  // 好
constexpr uint32_t kJie = 0x754C;  // 界

constexpr int kLayers = 2;
constexpr int kHidden = 8;
constexpr int kHeads = 2;
constexpr int kFfn = 16;
constexpr int kMaxSeq = 16;
constexpr int kVocabCount = 4;
constexpr int kVocabSize = kVocabCount + 32;
constexpr uint32_t kHeaderSize = 80;
constexpr uint32_t kParamsOffset = 96;  // 16-aligned after the vocabulary

constexpr size_t SyntheticParamCount() {
  constexpr size_t h = kHidden;
  constexpr size_t per_layer = 2 * h + 4 * (h * h + h) + 2 * h + h * kFfn +
                               kFfn + kFfn * h + h;
  return kVocabSize * h + kMaxSeq * h + 2 * h + kLayers * per_layer +
         kVocabSize * h;
}

// Field offsets of ClmgModelHeader (see clmg_model.h).
constexpr size_t kVersionOffset = 8;
constexpr size_t kVocabCountOffset = 36;
constexpr size_t kVocabHashOffset = 40;
constexpr size_t kParamCountOffset = 48;
constexpr size_t kVocabOffsetOffset = 56;
constexpr size_t kParamsOffsetOffset = 60;

uint64_t Fnv1a(uint64_t hash, const void* data, size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 0x100000001B3ULL;
  }
  return hash;
}

uint64_t SyntheticVocabHash(const uint32_t* codepoints) {
  uint64_t hash = 0xcbf29ce484222325ULL;
  const uint64_t vocab_size = kVocabSize;
  hash = Fnv1a(hash, &vocab_size, sizeof(vocab_size));
  for (int i = 0; i < kVocabCount; ++i)
    hash = Fnv1a(hash, &codepoints[i], sizeof(codepoints[i]));
  return hash;
}

void PutU32(std::vector<unsigned char>* bytes, size_t offset, uint32_t value) {
  std::memcpy(bytes->data() + offset, &value, sizeof(value));
}

void PutU64(std::vector<unsigned char>* bytes, size_t offset, uint64_t value) {
  std::memcpy(bytes->data() + offset, &value, sizeof(value));
}

uint64_t GetU64(const std::vector<unsigned char>& bytes, size_t offset) {
  uint64_t value = 0;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

class Rng {
 public:
  explicit Rng(uint64_t seed) : state_(seed) {}

  float Next() {
    state_ ^= state_ >> 12;
    state_ ^= state_ << 25;
    state_ ^= state_ >> 27;
    const uint64_t bits = state_ * 0x2545F4914F6CDD1DULL;
    return static_cast<float>(bits >> 40) / 8388608.0f - 0.5f;
  }

 private:
  uint64_t state_;
};

std::vector<unsigned char> BuildSyntheticModel() {
  const uint32_t codepoints[kVocabCount] = {kShi, kNi, kHao, kJie};
  const size_t param_count = SyntheticParamCount();
  std::vector<unsigned char> bytes(kParamsOffset + param_count * 4, 0);
  std::memcpy(bytes.data(), "RimeCLMG", 8);
  PutU32(&bytes, kVersionOffset, 1);
  PutU32(&bytes, 12, kLayers);
  PutU32(&bytes, 16, kHidden);
  PutU32(&bytes, 20, kHeads);
  PutU32(&bytes, 24, kFfn);
  PutU32(&bytes, 28, kVocabSize);
  PutU32(&bytes, 32, kMaxSeq);
  PutU32(&bytes, kVocabCountOffset, kVocabCount);
  PutU64(&bytes, kVocabHashOffset, SyntheticVocabHash(codepoints));
  PutU64(&bytes, kParamCountOffset, param_count);
  PutU32(&bytes, kVocabOffsetOffset, kHeaderSize);
  PutU32(&bytes, kParamsOffsetOffset, kParamsOffset);
  for (int i = 0; i < kVocabCount; ++i)
    PutU32(&bytes, kHeaderSize + 4 * i, codepoints[i]);
  Rng rng(0x9E3779B97F4A7C15ULL);
  for (size_t i = 0; i < param_count; ++i) {
    const float value = rng.Next();
    std::memcpy(bytes.data() + kParamsOffset + 4 * i, &value, sizeof(value));
  }
  return bytes;
}

bool WriteFile(const std::filesystem::path& file,
               const std::vector<unsigned char>& bytes) {
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!out)
    return false;
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  return out.good();
}

const std::filesystem::path& TestDir() {
  static const std::filesystem::path dir = [] {
    auto path = std::filesystem::path(testing::TempDir()) / "rime_clmg_test";
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return path;
  }();
  return dir;
}

string StripBinSuffix(const string& file) {
  constexpr const char* kSuffix = ".bin";
  const size_t n = std::strlen(kSuffix);
  if (file.size() > n && file.compare(file.size() - n, n, kSuffix) == 0)
    return file.substr(0, file.size() - n);
  return file;
}

struct Fixture {
  bool written = false;
  std::filesystem::path file;
  string model_id;
};

const Fixture& GetFixture() {
  static const Fixture fixture = [] {
    Fixture f;
    f.file = TestDir() / "clmg_test_model.bin";
    f.written = WriteFile(f.file, BuildSyntheticModel());
    f.model_id = StripBinSuffix(std::filesystem::absolute(f.file).string());
    return f;
  }();
  return fixture;
}

vector<int> EncodeIds(const ClmgModel& model,
                      const string& context,
                      int limit) {
  vector<int> ids;
  const char* p = context.c_str();
  const char* end = p + context.size();
  while (p != end) {
    const int id = model.Encode(utf8::unchecked::next(p));
    if (id >= 0)
      ids.push_back(id);
  }
  if (static_cast<int>(ids.size()) > limit)
    ids.erase(ids.begin(), ids.end() - limit);
  ids.insert(ids.begin(), 1);  // BOS
  return ids;
}

double DirectScore(const ClmgModel& model,
                   const string& context,
                   const string& word,
                   int limit = 32) {
  vector<float> hidden;
  model.ComputeHidden(EncodeIds(model, context, limit), &hidden);
  return model.WordScore(hidden, word);
}

void AddClmgConfig(Config* config,
                   const string& model_id,
                   double alpha,
                   int context_length = 32,
                   int cache_size = 8) {
  config->SetString("grammar/component", "clmg");
  config->SetString("clmg/model", model_id);
  config->SetDouble("clmg/alpha", alpha);
  config->SetInt("clmg/context_length", context_length);
  config->SetInt("clmg/cache_size", cache_size);
}

void AddWord(WordGraph* graph,
             int start,
             int end,
             const string& text,
             double weight) {
  auto entry = New<DictEntry>();
  entry->text = text;
  entry->weight = weight;
  (*graph)[start][end].push_back(entry);
}

// Picks the word with the higher CLMG residual and assigns entry weights so
// that raw weights prefer `low` while grammar-enabled scores prefer `high`.
struct RerankPlan {
  string high;
  string low;
  double alpha = 0.5;
  double high_weight = -1.0;
  double low_weight = -1.0;
};

RerankPlan MakeRerankPlan(const ClmgModel& model, const string& context) {
  RerankPlan plan;
  const double hao = DirectScore(model, context, "好");
  const double ni = DirectScore(model, context, "你");
  plan.high = hao >= ni ? "好" : "你";
  plan.low = hao >= ni ? "你" : "好";
  const double gap = plan.alpha * std::abs(hao - ni);
  plan.high_weight = -1.0;
  plan.low_weight = -1.0 + gap / 2.0;
  return plan;
}

class FakeTranslator {
 public:
  explicit FakeTranslator(string preceding) : preceding_(std::move(preceding)) {}

  bool contextual_suggestions() const { return true; }
  string GetPrecedingText(size_t) const { return preceding_; }
  bool contextual_group_by_type() const { return false; }

 private:
  string preceding_;
};

class ListTranslation : public Translation {
 public:
  explicit ListTranslation(vector<an<Candidate>> candidates)
      : candidates_(std::move(candidates)) {
    set_exhausted(candidates_.empty());
  }

  bool Next() override {
    if (exhausted())
      return false;
    if (++cursor_ >= candidates_.size())
      set_exhausted(true);
    return true;
  }

  an<Candidate> Peek() override {
    return exhausted() ? nullptr : candidates_[cursor_];
  }

 private:
  vector<an<Candidate>> candidates_;
  size_t cursor_ = 0;
};

an<Phrase> MakePhrase(const string& text, size_t end, double weight) {
  auto entry = New<DictEntry>();
  entry->text = text;
  entry->weight = weight;
  return New<Phrase>(nullptr, "phrase", 0, end, entry);
}

vector<string> Drain(Translation* translation) {
  vector<string> texts;
  while (!translation->exhausted()) {
    auto candidate = translation->Peek();
    if (!candidate)
      break;
    texts.push_back(candidate->text());
    translation->Next();
  }
  return texts;
}

double Millis(Clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

double Micros(Clock::duration duration) {
  return std::chrono::duration<double, std::micro>(duration).count();
}

double Mean(const vector<double>& values) {
  double sum = 0.0;
  for (double value : values)
    sum += value;
  return values.empty() ? 0.0 : sum / values.size();
}

double Percentile(vector<double> values, double p) {
  if (values.empty())
    return 0.0;
  std::sort(values.begin(), values.end());
  return values[static_cast<size_t>(p * (values.size() - 1))];
}

void ExpectRejected(std::vector<unsigned char> bytes, const char* name) {
  const auto file = TestDir() / name;
  ASSERT_TRUE(WriteFile(file, bytes)) << name;
  ClmgModel model(file);
  EXPECT_FALSE(model.Load()) << name;
}

}  // namespace

TEST(RimeClmgTest, SyntheticModelEncodesAndScores) {
  const Fixture& fixture = GetFixture();
  ASSERT_TRUE(fixture.written) << fixture.file;
  ClmgModel model(fixture.file);
  ASSERT_TRUE(model.Load());

  EXPECT_EQ(kHidden, model.hidden_size());
  EXPECT_EQ(kMaxSeq - 1, model.max_context());
  EXPECT_EQ(32, model.Encode(kShi));
  EXPECT_EQ(33, model.Encode(kNi));
  EXPECT_EQ(34, model.Encode(kHao));
  EXPECT_EQ(35, model.Encode(kJie));
  EXPECT_EQ(31, model.Encode(0x9F98));  // 龘: CJK but not in the vocabulary
  EXPECT_EQ(5, model.Encode('a'));
  EXPECT_EQ(30, model.Encode('Z'));
  EXPECT_EQ(-1, model.Encode('!'));

  vector<float> hidden;
  model.ComputeHidden(EncodeIds(model, "世界", 32), &hidden);
  ASSERT_EQ(kHidden, static_cast<int>(hidden.size()));
  for (float value : hidden)
    EXPECT_TRUE(std::isfinite(value));

  const double forward = model.WordScore(hidden, "你好");
  EXPECT_NE(0.0, forward);
  EXPECT_DOUBLE_EQ(forward, model.WordScore(hidden, "好你"));
  EXPECT_DOUBLE_EQ(0.0, model.WordScore(hidden, "a!"));
}

TEST(RimeClmgTest, SyntheticModelRejectsCorruptFiles) {
  const auto base = BuildSyntheticModel();

  auto bad_magic = base;
  bad_magic[0] = 'X';
  ExpectRejected(bad_magic, "clmg_bad_magic.bin");

  auto bad_version = base;
  PutU32(&bad_version, kVersionOffset, 2);
  ExpectRejected(bad_version, "clmg_bad_version.bin");

  auto bad_hash = base;
  PutU64(&bad_hash, kVocabHashOffset, GetU64(base, kVocabHashOffset) ^ 1);
  ExpectRejected(bad_hash, "clmg_bad_hash.bin");

  auto bad_vocab_count = base;
  PutU32(&bad_vocab_count, kVocabCountOffset, kVocabCount - 1);
  ExpectRejected(bad_vocab_count, "clmg_bad_vocab_count.bin");

  auto bad_param_count = base;
  PutU64(&bad_param_count, kParamCountOffset, SyntheticParamCount() - 1);
  ExpectRejected(bad_param_count, "clmg_bad_param_count.bin");

  auto truncated = base;
  truncated.resize(truncated.size() - 4);
  ExpectRejected(truncated, "clmg_truncated.bin");
}

TEST(RimeClmgTest, ComponentReadsConfigAndScores) {
  const Fixture& fixture = GetFixture();
  ASSERT_TRUE(fixture.written) << fixture.file;
  ClmgModel model(fixture.file);
  ASSERT_TRUE(model.Load());

  auto* component = Grammar::Require("clmg");
  ASSERT_NE(nullptr, component);

  Config config;
  AddClmgConfig(&config, fixture.model_id, 0.5);
  the<Grammar> grammar(component->Create(&config));
  ASSERT_TRUE(bool(grammar));

  const double expected = 0.5 * DirectScore(model, "世界", "你好");
  EXPECT_NEAR(expected, grammar->Query("世界", "你好", true), 1e-9);
  EXPECT_EQ(0.0, grammar->Query("", "你好", true));

  Config disabled;
  AddClmgConfig(&disabled, fixture.model_id, 0.0);
  the<Grammar> disabled_grammar(component->Create(&disabled));
  ASSERT_TRUE(bool(disabled_grammar));
  EXPECT_EQ(0.0, disabled_grammar->Query("世界", "你好", true));

  Config missing;
  AddClmgConfig(&missing, "rime_clmg_test_missing_model", 0.5);
  the<Grammar> missing_grammar(component->Create(&missing));
  ASSERT_TRUE(bool(missing_grammar));
  EXPECT_EQ(0.0, missing_grammar->Query("世界", "你好", true));
}

TEST(RimeClmgTest, ContextLengthTruncatesFromTheLeft) {
  const Fixture& fixture = GetFixture();
  ASSERT_TRUE(fixture.written) << fixture.file;
  auto* component = Grammar::Require("clmg");
  ASSERT_NE(nullptr, component);

  Config config;
  AddClmgConfig(&config, fixture.model_id, 1.0, /*context_length=*/2);
  the<Grammar> grammar(component->Create(&config));
  ASSERT_TRUE(bool(grammar));

  const double long_context = grammar->Query("世界你好", "好", true);
  const double short_context = grammar->Query("你好", "好", true);
  EXPECT_DOUBLE_EQ(short_context, long_context);

  const double other_context = grammar->Query("世界", "好", true);
  EXPECT_GT(std::abs(short_context - other_context), 1e-6);
}

TEST(RimeClmgTest, CacheKeepsResultsStable) {
  const Fixture& fixture = GetFixture();
  ASSERT_TRUE(fixture.written) << fixture.file;
  auto* component = Grammar::Require("clmg");
  ASSERT_NE(nullptr, component);

  Config config;
  AddClmgConfig(&config, fixture.model_id, 1.0, /*context_length=*/32,
                /*cache_size=*/1);
  the<Grammar> grammar(component->Create(&config));
  ASSERT_TRUE(bool(grammar));

  const char* contexts[] = {"世界", "你好", "好", "你", "界"};
  vector<double> first;
  vector<double> second;
  for (const char* context : contexts)
    first.push_back(grammar->Query(context, "好", true));
  for (const char* context : contexts)
    second.push_back(grammar->Query(context, "好", true));
  ASSERT_EQ(first.size(), second.size());
  for (size_t i = 0; i < first.size(); ++i)
    EXPECT_DOUBLE_EQ(first[i], second[i]) << "context " << contexts[i];
}

TEST(RimeClmgTest, PoetSelectsClmgGrammarFromConfig) {
  const Fixture& fixture = GetFixture();
  ASSERT_TRUE(fixture.written) << fixture.file;
  ClmgModel model(fixture.file);
  ASSERT_TRUE(model.Load());
  const RerankPlan plan = MakeRerankPlan(model, "世界");
  ASSERT_GT(plan.low_weight - plan.high_weight, 1e-4);

  WordGraph graph;
  AddWord(&graph, 0, 1, plan.high, plan.high_weight);
  AddWord(&graph, 0, 1, plan.low, plan.low_weight);
  AddWord(&graph, 1, 2, "x", -1.0);  // WordScore("x") == 0

  Config config;
  AddClmgConfig(&config, fixture.model_id, plan.alpha);
  config.SetBool("grammar/apply_to_sentence", true);
  Poet poet(nullptr, &config);
  auto sentence = poet.MakeSentence(graph, 2, "世界");
  ASSERT_TRUE(bool(sentence));
  ASSERT_EQ(2u, sentence->components().size());
  EXPECT_EQ(plan.high, sentence->components()[0].text);
  EXPECT_EQ("x", sentence->components()[1].text);

  Config raw_config;
  AddClmgConfig(&raw_config, fixture.model_id, plan.alpha);
  Poet raw_poet(nullptr, &raw_config);
  auto raw_sentence = raw_poet.MakeSentence(graph, 2, "世界");
  ASSERT_TRUE(bool(raw_sentence));
  ASSERT_EQ(2u, raw_sentence->components().size());
  EXPECT_EQ(plan.low, raw_sentence->components()[0].text);
}

TEST(RimeClmgTest, ContextualWeightedReranksWithClmg) {
  const Fixture& fixture = GetFixture();
  ASSERT_TRUE(fixture.written) << fixture.file;
  ClmgModel model(fixture.file);
  ASSERT_TRUE(model.Load());
  const string preceding = "世界";
  const RerankPlan plan = MakeRerankPlan(model, preceding);
  ASSERT_GT(plan.low_weight - plan.high_weight, 1e-4);

  const string input = "ab";
  auto candidates = [&] {
    vector<an<Candidate>> list;
    list.push_back(MakePhrase(plan.high, input.size(), plan.high_weight));
    list.push_back(MakePhrase(plan.low, input.size(), plan.low_weight));
    return list;
  };
  FakeTranslator translator(preceding);

  Config config;
  AddClmgConfig(&config, fixture.model_id, plan.alpha);
  Poet poet(nullptr, &config);
  auto ranked = poet.ContextualWeighted(New<ListTranslation>(candidates()),
                                        input, 0, &translator);
  ASSERT_TRUE(bool(ranked));
  auto texts = Drain(ranked.get());
  ASSERT_EQ(2u, texts.size());
  EXPECT_EQ(plan.high, texts[0]);

  Config disabled;
  AddClmgConfig(&disabled, fixture.model_id, 0.0);
  Poet disabled_poet(nullptr, &disabled);
  auto raw = disabled_poet.ContextualWeighted(New<ListTranslation>(candidates()),
                                              input, 0, &translator);
  ASSERT_TRUE(bool(raw));
  auto raw_texts = Drain(raw.get());
  ASSERT_EQ(2u, raw_texts.size());
  EXPECT_EQ(plan.low, raw_texts[0]);
}

TEST(RimeClmgTest, RealModelBenchmark) {
  const char* env = std::getenv("RIME_CLMG_MODEL");
  if (!env || !*env)
    GTEST_SKIP() << "set RIME_CLMG_MODEL=/path/to/clmg.bin to run the "
                    "real-model benchmark";
  const std::filesystem::path model_file =
      std::filesystem::absolute(std::filesystem::path(env));
  ASSERT_TRUE(std::filesystem::exists(model_file)) << model_file;

  ClmgModel model(model_file);
  ASSERT_TRUE(model.Load()) << model_file;
  const string model_id = StripBinSuffix(model_file.string());

  // Touch the mmap and warm up the forward path so timings are steady state.
  {
    vector<float> warmup;
    model.ComputeHidden(EncodeIds(model, "热身", 32), &warmup);
  }

  const vector<string> contexts = {
      "今天",           "今天天气",       "今天天气很好",
      "我们一起去公园散步", "这个周末",       "这个周末打算去哪里玩",
      "最近工作很忙",     "最近工作很忙没有时间", "北京上海广州深圳",
      "计算机科学与技术",   "人工智能改变了世界",  "请问一下这个问题",
      "明天早上八点开会",   "明天早上八点开会不要迟到", "天气不错适合出门",
      "天气不错适合出门走走看看"};
  const string word = "一";
  constexpr double kAlpha = 0.25;

  vector<double> forward_ms;
  vector<double> direct_scores;
  for (const auto& context : contexts) {
    const auto ids = EncodeIds(model, context, 32);
    vector<float> hidden;
    const auto start = Clock::now();
    model.ComputeHidden(ids, &hidden);
    forward_ms.push_back(Millis(Clock::now() - start));
    direct_scores.push_back(model.WordScore(hidden, word));
  }
  for (double score : direct_scores)
    EXPECT_TRUE(std::isfinite(score));
  EXPECT_GT(*std::max_element(direct_scores.begin(), direct_scores.end()) -
                *std::min_element(direct_scores.begin(), direct_scores.end()),
            1e-6);

  auto* component = Grammar::Require("clmg");
  ASSERT_NE(nullptr, component);
  Config config;
  AddClmgConfig(&config, model_id, kAlpha);
  the<Grammar> grammar(component->Create(&config));
  ASSERT_TRUE(bool(grammar));

  vector<double> query_ms;
  for (size_t i = 0; i < contexts.size(); ++i) {
    const auto start = Clock::now();
    const double score = grammar->Query(contexts[i], word, true);
    query_ms.push_back(Millis(Clock::now() - start));
    EXPECT_NEAR(kAlpha * direct_scores[i], score, 1e-4);
  }

  const double warm_reference = grammar->Query(contexts.back(), word, true);
  constexpr int kWarmIterations = 200;
  vector<double> warm_us;
  for (int i = 0; i < kWarmIterations; ++i) {
    const auto start = Clock::now();
    const double score = grammar->Query(contexts.back(), word, true);
    warm_us.push_back(Micros(Clock::now() - start));
    EXPECT_DOUBLE_EQ(warm_reference, score);
  }

  Config disabled;
  AddClmgConfig(&disabled, model_id, 0.0);
  the<Grammar> disabled_grammar(component->Create(&disabled));
  ASSERT_TRUE(bool(disabled_grammar));
  vector<double> disabled_us;
  for (int i = 0; i < kWarmIterations; ++i) {
    const auto start = Clock::now();
    const double score = disabled_grammar->Query(contexts.back(), word, true);
    disabled_us.push_back(Micros(Clock::now() - start));
    EXPECT_EQ(0.0, score);
  }

  const double forward_mean = Mean(forward_ms);
  const double query_mean = Mean(query_ms);
  const double warm_mean = Mean(warm_us);
  const double disabled_mean = Mean(disabled_us);
  std::printf(
      "[clmg] real model: hidden %d, max_context %d\n", model.hidden_size(),
      model.max_context());
  std::printf(
      "[clmg] cold forward (cache miss): mean %.2f ms, p50 %.2f ms\n",
      forward_mean, Percentile(forward_ms, 0.5));
  std::printf(
      "[clmg] cold query (with forward): mean %.2f ms, p50 %.2f ms\n",
      query_mean, Percentile(query_ms, 0.5));
  std::printf(
      "[clmg] warm query (cache hit): mean %.2f us, p50 %.2f us\n", warm_mean,
      Percentile(warm_us, 0.5));
  std::printf("[clmg] alpha=0 query: mean %.2f us\n", disabled_mean);

  EXPECT_LT(forward_mean, 5000.0);
  EXPECT_LT(query_mean, 5000.0);
  EXPECT_LT(warm_mean, 1000.0);
  EXPECT_LT(warm_mean / 1000.0, query_mean);
  EXPECT_LT(disabled_mean, 1000.0);
}

#endif  // RIME_TEST_CLMG
