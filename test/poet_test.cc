//
// Copyright RIME Developers
// Distributed under the BSD License
//

#include <utility>
#include <gtest/gtest.h>
#include <rime/config.h>
#include <rime/dict/vocabulary.h>
#include <rime/gear/grammar.h>
#include <rime/gear/poet.h>
#include <rime/gear/translator_commons.h>

using namespace rime;

namespace {

class CountingGrammar : public Grammar {
 public:
  double Query(const string&, const string&, bool) override {
    ++query_count;
    return 0.0;
  }

  int query_count = 0;
};

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

// Two paths over the same input:
//   ab + cd + ef : 3 words, total entry weight -3
//   abcd + ef    : 2 words, total entry weight -5
// Without grammar the per-word penalty favors the shorter path; a grammar
// returning 0 favors the path with higher entry weights.
WordGraph MakeTwoPathGraph() {
  WordGraph graph;
  AddWord(&graph, 0, 2, "ab", -1);
  AddWord(&graph, 2, 4, "cd", -1);
  AddWord(&graph, 0, 4, "abcd", -4);
  AddWord(&graph, 4, 6, "ef", -1);
  return graph;
}

}  // namespace

TEST(RimePoetTest, GrammarIsNotUsedInSentenceByDefault) {
  Config config;
  auto grammar = the<Grammar>(new CountingGrammar);
  auto* counter = static_cast<CountingGrammar*>(grammar.get());
  Poet poet(nullptr, std::move(grammar), &config);

  auto sentence = poet.MakeSentence(MakeTwoPathGraph(), 6, "");
  ASSERT_TRUE(bool(sentence));
  ASSERT_EQ(2u, sentence->components().size());
  EXPECT_EQ("abcd", sentence->components()[0].text);
  EXPECT_EQ(0, counter->query_count);
}

TEST(RimePoetTest, GrammarIsUsedInSentenceWhenEnabled) {
  Config config;
  config.SetBool("grammar/apply_to_sentence", true);
  auto grammar = the<Grammar>(new CountingGrammar);
  auto* counter = static_cast<CountingGrammar*>(grammar.get());
  Poet poet(nullptr, std::move(grammar), &config);

  auto sentence = poet.MakeSentence(MakeTwoPathGraph(), 6, "");
  ASSERT_TRUE(bool(sentence));
  ASSERT_EQ(3u, sentence->components().size());
  EXPECT_EQ("ab", sentence->components()[0].text);
  EXPECT_EQ("cd", sentence->components()[1].text);
  EXPECT_GT(counter->query_count, 0);
}

TEST(RimePoetTest, GrammarIsNotUsedInNBestSentencesByDefault) {
  Config config;
  auto grammar = the<Grammar>(new CountingGrammar);
  auto* counter = static_cast<CountingGrammar*>(grammar.get());
  Poet poet(nullptr, std::move(grammar), &config);

  auto sentences = poet.MakeSentences(MakeTwoPathGraph(), 6, "", 3, 0.1);
  EXPECT_FALSE(sentences.empty());
  EXPECT_EQ(0, counter->query_count);
}

TEST(RimePoetTest, GrammarIsUsedInNBestSentencesWhenEnabled) {
  Config config;
  config.SetBool("grammar/apply_to_sentence", true);
  auto grammar = the<Grammar>(new CountingGrammar);
  auto* counter = static_cast<CountingGrammar*>(grammar.get());
  Poet poet(nullptr, std::move(grammar), &config);

  auto sentences = poet.MakeSentences(MakeTwoPathGraph(), 6, "", 3, 0.1);
  EXPECT_FALSE(sentences.empty());
  EXPECT_GT(counter->query_count, 0);
}
