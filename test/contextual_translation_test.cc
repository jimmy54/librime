//
// Copyright RIME Developers
// Distributed under the BSD License
//

#include <string>
#include <utility>
#include <gtest/gtest.h>
#include <rime/candidate.h>
#include <rime/dict/vocabulary.h>
#include <rime/gear/contextual_translation.h>
#include <rime/gear/grammar.h>
#include <rime/gear/translator_commons.h>
#include <rime/translation.h>

using namespace rime;

namespace {

class FixedGrammar : public Grammar {
 public:
  explicit FixedGrammar(double score) : score_(score) {}
  double Query(const string&, const string&, bool) override { return score_; }

 private:
  double score_;
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

vector<string> Drain(ContextualTranslation* translation) {
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

}  // namespace

TEST(RimeContextualTranslationTest, PreservesOrderWhenScoresTie) {
  vector<an<Candidate>> candidates;
  for (int i = 0; i < 64; ++i) {
    candidates.push_back(MakePhrase("w" + std::to_string(i), 2, 1.0));
  }
  FixedGrammar grammar(0.0);
  ContextualTranslation translation(New<ListTranslation>(std::move(candidates)),
                                    "in", "context", &grammar, false);

  auto texts = Drain(&translation);
  ASSERT_EQ(64u, texts.size());
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ("w" + std::to_string(i), texts[i]);
  }
}

TEST(RimeContextualTranslationTest, PreservesRelativeOrderOfTiedCandidates) {
  vector<an<Candidate>> candidates;
  const double weights[] = {1, 2, 2, 1, 2};
  for (int i = 0; i < 5; ++i) {
    candidates.push_back(MakePhrase("w" + std::to_string(i), 2, weights[i]));
  }
  FixedGrammar grammar(0.0);
  ContextualTranslation translation(New<ListTranslation>(std::move(candidates)),
                                    "in", "context", &grammar, false);

  auto texts = Drain(&translation);
  ASSERT_EQ(5u, texts.size());
  EXPECT_EQ("w1", texts[0]);
  EXPECT_EQ("w2", texts[1]);
  EXPECT_EQ("w4", texts[2]);
  EXPECT_EQ("w0", texts[3]);
  EXPECT_EQ("w3", texts[4]);
}

TEST(RimeContextualTranslationTest, SortsByWeightDescending) {
  vector<an<Candidate>> candidates;
  for (int i = 0; i < 5; ++i) {
    candidates.push_back(MakePhrase("w" + std::to_string(i), 2, i));
  }
  FixedGrammar grammar(0.0);
  ContextualTranslation translation(New<ListTranslation>(std::move(candidates)),
                                    "in", "context", &grammar, false);

  auto texts = Drain(&translation);
  ASSERT_EQ(5u, texts.size());
  EXPECT_EQ("w4", texts[0]);
  EXPECT_EQ("w3", texts[1]);
  EXPECT_EQ("w2", texts[2]);
  EXPECT_EQ("w1", texts[3]);
  EXPECT_EQ("w0", texts[4]);
}

TEST(RimeContextualTranslationTest, KeepsNonPhraseCandidates) {
  vector<an<Candidate>> candidates;
  candidates.push_back(New<SimpleCandidate>("sentence", 0, 2, "s"));
  candidates.push_back(MakePhrase("w0", 2, 1.0));
  candidates.push_back(MakePhrase("w1", 2, 2.0));
  FixedGrammar grammar(0.0);
  ContextualTranslation translation(New<ListTranslation>(std::move(candidates)),
                                    "in", "context", &grammar, false);

  auto texts = Drain(&translation);
  ASSERT_EQ(3u, texts.size());
  EXPECT_EQ("s", texts[0]);
  EXPECT_EQ("w1", texts[1]);
  EXPECT_EQ("w0", texts[2]);
}
