//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-07-10 GONG Chen <chen.sst@gmail.com>
//
#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <cmath>
#include <utf8.h>
#include <rime/candidate.h>
#include <rime/common.h>
#include <rime/composition.h>
#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/schema.h>
#include <rime/translation.h>
#include <rime/dict/corrector.h>
#include <rime/dict/dictionary.h>
#include <rime/dict/user_dictionary.h>
#include <rime/gear/charset_filter.h>
#include <rime/gear/poet.h>
#include <rime/gear/table_translator.h>
#include <rime/gear/translator_commons.h>
#include <rime/gear/unity_table_encoder.h>

namespace rime {

static const char* kUnitySymbol = " \xe2\x98\xaf ";
// log(0.01); aligned with syllabifier / script_translator correction penalty
static const double kCorrectionCredibility = -4.605170185988091;
static const size_t kMaxCorrections = 4;
// Sentence path is stricter: only adjacent-key (distance <= 1) full-span fixes,
// and only when exact prefix search found nothing (avoid competing with valid
// exact splits such as uglh → 盖+上; Query still surfaces 美国 as corrected).
static const size_t kSentenceCorrectionTolerance = 1;
static const size_t kMaxSentenceCorrectionsPerPos = 2;

using CorrectionItem = pair<SyllableId, corrector::Correction>;

static vector<CorrectionItem> SortedCorrections(
    const corrector::Corrections& corrections) {
  vector<CorrectionItem> items(corrections.begin(), corrections.end());
  std::sort(items.begin(), items.end(),
            [](const CorrectionItem& a, const CorrectionItem& b) {
              if (a.second.distance != b.second.distance)
                return a.second.distance < b.second.distance;
              // Prefer longer consumed spans when distance ties.
              if (a.second.length != b.second.length)
                return a.second.length > b.second.length;
              return a.first < b.first;
            });
  return items;
}

// TableTranslation

TableTranslation::TableTranslation(TranslatorOptions* options,
                                   const Language* language,
                                   const string& input,
                                   size_t start,
                                   size_t end,
                                   const string& preedit,
                                   DictEntryIterator&& iter,
                                   UserDictEntryIterator&& uter)
    : options_(options),
      language_(language),
      input_(input),
      start_(start),
      end_(end),
      preedit_(preedit),
      iter_(std::move(iter)),
      uter_(std::move(uter)) {
  if (options_)
    options_->preedit_formatter().Apply(&preedit_);
  CheckEmpty();
}

bool TableTranslation::Next() {
  if (exhausted())
    return false;
  if (PreferUserPhrase()) {
    uter_.Next();
    if (uter_.exhausted())
      FetchMoreUserPhrases();
  } else {
    iter_.Next();
    if (iter_.exhausted())
      FetchMoreTableEntries();
  }
  return !CheckEmpty();
}

static bool is_constructed(const DictEntry* e) {
  return UnityTableEncoder::HasPrefix(e->custom_code);
}

an<Candidate> TableTranslation::Peek() {
  if (exhausted())
    return nullptr;
  bool is_user_phrase = PreferUserPhrase();
  auto e = PreferredEntry(is_user_phrase);
  string comment(is_constructed(e.get()) ? kUnitySymbol : e->comment);
  if (options_) {
    options_->comment_formatter().Apply(&comment);
  }
  bool incomplete = e->remaining_code_length != 0;
  auto type = incomplete       ? "completion"
              : is_user_phrase ? "user_table"
                               : "table";
  auto phrase = New<Phrase>(language_, type, start_, end_, e);
  if (phrase) {
    phrase->set_comment(comment);
    phrase->set_preedit(preedit_);
    phrase->set_quality(std::exp(e->weight) + options_->initial_quality() +
                        (incomplete ? -1 : 0) + (is_user_phrase ? 0.5 : 0));
  }
  return phrase;
}

bool TableTranslation::CheckEmpty() {
  bool is_empty = iter_.exhausted() && uter_.exhausted();
  set_exhausted(is_empty);
  return is_empty;
}

bool TableTranslation::PreferUserPhrase() {
  if (uter_.exhausted())
    return false;
  if (iter_.exhausted())
    return true;
  if (iter_.Peek()->remaining_code_length == 0 &&
      (uter_.Peek()->remaining_code_length != 0 ||
       is_constructed(uter_.Peek().get())))
    return false;
  else
    return true;
}

// LazyTableTranslation

class LazyTableTranslation : public TableTranslation {
 public:
  static const size_t kInitialSearchLimit = 10;
  static const size_t kExpandingFactor = 10;

  LazyTableTranslation(TableTranslator* translator,
                       const string& input,
                       size_t start,
                       size_t end,
                       const string& preedit,
                       bool enable_user_dict);
  bool FetchUserPhrases(TableTranslator* translator);
  virtual bool FetchMoreUserPhrases();
  virtual bool FetchMoreTableEntries();

 private:
  Dictionary* dict_;
  const hash_set<string>* blacklist_;
  UserDictionary* user_dict_;
  size_t limit_;
  size_t user_dict_limit_;
  string user_dict_key_;
};

LazyTableTranslation::LazyTableTranslation(TableTranslator* translator,
                                           const string& input,
                                           size_t start,
                                           size_t end,
                                           const string& preedit,
                                           bool enable_user_dict)
    : TableTranslation(translator,
                       translator->language(),
                       input,
                       start,
                       end,
                       preedit),
      dict_(translator->dict()),
      blacklist_(&translator->blacklist()),
      user_dict_(enable_user_dict ? translator->user_dict() : NULL),
      limit_(kInitialSearchLimit),
      user_dict_limit_(kInitialSearchLimit) {
  FetchUserPhrases(translator) || FetchMoreUserPhrases();
  FetchMoreTableEntries();
  CheckEmpty();
}

bool LazyTableTranslation::FetchUserPhrases(TableTranslator* translator) {
  if (!user_dict_)
    return false;
  // fetch all exact match entries
  user_dict_->LookupWords(&uter_, input_, false, 0, &user_dict_key_);
  auto encoder = translator->encoder();
  if (encoder && encoder->loaded()) {
    encoder->LookupPhrases(&uter_, input_, false);
  }
  return !uter_.exhausted();
}

bool LazyTableTranslation::FetchMoreUserPhrases() {
  if (!user_dict_ || user_dict_limit_ == 0)
    return false;
  size_t count = user_dict_->LookupWords(&uter_, input_, true, user_dict_limit_,
                                         &user_dict_key_);
  if (count < user_dict_limit_) {
    DLOG(INFO) << "all user dict entries obtained.";
    user_dict_limit_ = 0;  // no more try
  } else {
    user_dict_limit_ *= kExpandingFactor;
  }
  return !uter_.exhausted();
}

bool LazyTableTranslation::FetchMoreTableEntries() {
  if (!dict_ || limit_ == 0)
    return false;
  size_t previous_entry_count = iter_.entry_count();
  DLOG(INFO) << "fetching more table entries: limit = " << limit_
             << ", count = " << previous_entry_count;
  DictEntryIterator more;
  if (dict_->LookupWords(&more, input_, true, limit_, blacklist_) < limit_) {
    DLOG(INFO) << "all table entries obtained.";
    limit_ = 0;  // no more try
  } else {
    limit_ *= kExpandingFactor;
  }
  if (more.entry_count() > previous_entry_count) {
    more.Skip(previous_entry_count);
    iter_ = std::move(more);
  }
  return true;
}

// TableTranslator

TableTranslator::TableTranslator(const Ticket& ticket)
    : Translator(ticket), Memory(ticket), TranslatorOptions(ticket) {
  if (!engine_)
    return;
  if (Config* config = engine_->schema()->config()) {
    config->GetBool(name_space_ + "/enable_charset_filter",
                    &enable_charset_filter_);
    config->GetBool(name_space_ + "/enable_sentence", &enable_sentence_);
    config->GetBool(name_space_ + "/sentence_over_completion",
                    &sentence_over_completion_);
    config->GetBool(name_space_ + "/enable_encoder", &enable_encoder_);
    config->GetBool(name_space_ + "/encode_commit_history",
                    &encode_commit_history_);
    config->GetBool(name_space_ + "/enable_correction", &enable_correction_);
    config->GetInt(name_space_ + "/max_phrase_length", &max_phrase_length_);
    config->GetInt(name_space_ + "/max_homographs", &max_homographs_);
    if (enable_sentence_ || sentence_over_completion_ ||
        contextual_suggestions_) {
      poet_.reset(new Poet(language(), config, Poet::LeftAssociateCompare));
    }
  }
  if (enable_encoder_ && user_dict_) {
    encoder_.reset(new UnityTableEncoder(user_dict_.get()));
    encoder_->Load(ticket);
  }
  if (enable_correction_) {
    if (auto* corrector = Corrector::Require("corrector")) {
      corrector_.reset(corrector->Create(ticket));
    }
  }
}

TableTranslator::~TableTranslator() = default;

static bool starts_with_completion(an<Translation> translation) {
  if (!translation)
    return false;
  auto cand = translation->Peek();
  return cand && cand->type() == "completion";
}

an<Translation> TableTranslator::Query(const string& input,
                                       const Segment& segment) {
  if (!segment.HasAnyTagIn(tags_))
    return nullptr;
  DLOG(INFO) << "input = '" << input << "', [" << segment.start << ", "
             << segment.end << ")";

  FinishSession();

  bool enable_user_dict =
      user_dict_ && user_dict_->loaded() && !IsUserDictDisabledFor(input);

  const string& preedit(input);
  string code = input;
  boost::trim_right_if(code, boost::is_any_of(delimiters_));

  an<Translation> translation;
  if (enable_completion_) {
    translation = Cached<LazyTableTranslation>(this, code, segment.start,
                                               segment.start + input.length(),
                                               preedit, enable_user_dict);
  } else {
    DictEntryIterator iter;
    if (dict_ && dict_->loaded()) {
      dict_->LookupWords(&iter, code, false, 0, &blacklist());
    }
    UserDictEntryIterator uter;
    if (enable_user_dict) {
      user_dict_->LookupWords(&uter, code, false);
      if (encoder_ && encoder_->loaded()) {
        encoder_->LookupPhrases(&uter, code, false);
      }
    }
    if (!iter.exhausted() || !uter.exhausted())
      translation = Cached<TableTranslation>(
          this, language(), code, segment.start, segment.start + input.length(),
          preedit, std::move(iter), std::move(uter));
  }
  if (translation) {
    bool filter_by_charset =
        enable_charset_filter_ &&
        !engine_->context()->get_option("extended_charset");
    if (filter_by_charset) {
      translation = New<CharsetFilterTranslation>(translation);
    }
  }
  if (translation && translation->exhausted()) {
    translation.reset();  // discard futile translation
  }
  if (enable_sentence_ && !translation) {
    translation = MakeSentence(input, segment.start,
                               /* include_prefix_phrases = */ true);
  } else if (sentence_over_completion_ && starts_with_completion(translation)) {
    if (auto sentence = MakeSentence(input, segment.start)) {
      translation = sentence + translation;
    }
  }
  if (enable_correction_ && corrector_) {
    if (auto correction = MakeCorrectionTranslation(
            code, segment.start, segment.start + input.length(), preedit)) {
      translation = translation ? translation + correction : correction;
    }
  }
  if (translation && translation->exhausted()) {
    return nullptr;
  }
  translation = New<DistinctTranslation>(translation);
  if (contextual_suggestions_) {
    return poet_->ContextualWeighted(translation, input, segment.start, this);
  }
  return translation;
}

an<Translation> TableTranslator::MakeCorrectionTranslation(
    const string& code,
    size_t start,
    size_t end,
    const string& preedit) {
  if (code.empty() || !dict_ || !dict_->loaded() || !corrector_)
    return nullptr;

  auto& prism = *dict_->prism();
  set<SyllableId> exact_match_spellings;
  Prism::Match exact{0, 0};
  if (prism.GetValue(code, &exact.value)) {
    exact_match_spellings.insert(exact.value);
  }

  corrector::Corrections corrections;
  // NearSearchCorrector mutates the key buffer temporarily; pass a copy.
  string correction_key = code;
  corrector_->ToleranceSearch(prism, correction_key, &corrections, 5);
  if (corrections.empty())
    return nullptr;

  string formatted_preedit = preedit;
  preedit_formatter_.Apply(&formatted_preedit);

  auto result = New<FifoTranslation>();
  size_t correction_count = 0;
  hash_set<string> seen_text;

  // Dense tables yield dozens of hits; prefer shorter edit distance first so
  // true adjacent-key recoveries (e.g. uglh → uglg) are not starved.
  for (const auto& item : SortedCorrections(corrections)) {
    if (correction_count >= kMaxCorrections)
      break;
    if (exact_match_spellings.count(item.first))
      continue;
    // full-code correction only for Query
    if (item.second.length != code.length())
      continue;

    for (auto accessor = prism.QuerySpelling(item.first); !accessor.exhausted();
         accessor.Next()) {
      if (correction_count >= kMaxCorrections)
        break;
      auto props = accessor.properties();
      if (props.type != kNormalSpelling || props.is_correction)
        continue;

      string corrected =
          dict_->primary_table()->GetSyllableById(accessor.syllable_id());
      if (corrected.empty() || corrected == code)
        continue;

      DictEntryIterator iter;
      if (!dict_->LookupWords(&iter, corrected, false, 0, &blacklist()) ||
          iter.exhausted()) {
        continue;
      }

      while (!iter.exhausted() && correction_count < kMaxCorrections) {
        auto entry = iter.Peek();
        if (entry && !seen_text.count(entry->text)) {
          seen_text.insert(entry->text);
          auto adjusted = New<DictEntry>(*entry);
          adjusted->weight += kCorrectionCredibility;
          auto phrase =
              New<Phrase>(language(), "corrected", start, end, adjusted);
          phrase->set_preedit(formatted_preedit);
          string comment = corrected;
          comment_formatter_.Apply(&comment);
          phrase->set_comment(comment);
          phrase->set_quality(std::exp(adjusted->weight) + initial_quality());
          result->Append(phrase);
          ++correction_count;
        }
        if (!iter.Next())
          break;
      }
    }
  }

  if (result->exhausted())
    return nullptr;
  return result;
}

bool TableTranslator::Memorize(const CommitEntry& commit_entry) {
  if (!user_dict_)
    return false;
  for (const DictEntry* e : commit_entry.elements) {
    if (is_constructed(e)) {
      DictEntry blessed(*e);
      UnityTableEncoder::RemovePrefix(&blessed.custom_code);
      user_dict_->UpdateEntry(blessed, 1);
    } else {
      user_dict_->UpdateEntry(*e, 1);
    }
  }
  if (encoder_ && encoder_->loaded()) {
    if (commit_entry.elements.size() > 1) {
      encoder_->EncodePhrase(commit_entry.text, "1");
    }
    if (encode_commit_history_) {
      const auto& history(engine_->context()->commit_history());
      if (!history.empty()) {
        DLOG(INFO) << "history: " << history.repr();
        auto it = history.rbegin();
        if (it->type == "punct") {  // ending with punctuation
          ++it;
        }
        string phrase;
        for (; it != history.rend(); ++it) {
          if (it->type != "table" && it->type != "user_table" &&
              it->type != "sentence" && it->type != "uniquified")
            break;
          if (phrase.empty()) {
            phrase = it->text;  // last word
            continue;
          }
          phrase = it->text + phrase;  // prepend another word
          size_t phrase_length = utf8::unchecked::distance(
              phrase.c_str(), phrase.c_str() + phrase.length());
          if (static_cast<int>(phrase_length) > max_phrase_length_)
            break;
          DLOG(INFO) << "phrase: " << phrase;
          encoder_->EncodePhrase(phrase, "0");
        }
      }
    }
  }
  return true;
}

string TableTranslator::GetPrecedingText(size_t start) const {
  return !contextual_suggestions_ ? string()
         : start > 0 ? engine_->context()->composition().GetTextBefore(start)
                     : engine_->context()->commit_history().latest_text();
}

// SentenceSyllabifier

class SentenceSyllabifier : public PhraseSyllabifier {
 public:
  virtual Spans Syllabify(const Phrase* phrase);
};

Spans SentenceSyllabifier::Syllabify(const Phrase* phrase) {
  Spans result;
  if (auto sentence = dynamic_cast<const Sentence*>(phrase)) {
    size_t stop = sentence->start();
    result.AddVertex(stop);
    for (size_t len : sentence->word_lengths()) {
      stop += len;
      result.AddVertex(stop);
    }
  }
  return result;
}

// SentenceTranslation

class SentenceTranslation : public Translation {
 public:
  SentenceTranslation(TableTranslator* translator,
                      an<Sentence>&& sentence,
                      DictEntryCollector&& collector,
                      UserDictEntryCollector&& ucollector,
                      const string& input,
                      size_t start);
  virtual bool Next();
  virtual an<Candidate> Peek();

 protected:
  void PrepareSentence();
  bool CheckEmpty();
  bool PreferUserPhrase() const;

  TableTranslator* translator_;
  an<Sentence> sentence_;
  DictEntryCollector collector_;
  UserDictEntryCollector user_phrase_collector_;
  string input_;
  size_t start_;
};

SentenceTranslation::SentenceTranslation(TableTranslator* translator,
                                         an<Sentence>&& sentence,
                                         DictEntryCollector&& collector,
                                         UserDictEntryCollector&& ucollector,
                                         const string& input,
                                         size_t start)
    : translator_(translator),
      sentence_(std::move(sentence)),
      collector_(std::move(collector)),
      user_phrase_collector_(std::move(ucollector)),
      input_(input),
      start_(start) {
  PrepareSentence();
  CheckEmpty();
}

bool SentenceTranslation::Next() {
  if (sentence_) {
    sentence_.reset();
    return !CheckEmpty();
  }
  if (PreferUserPhrase()) {
    auto r = user_phrase_collector_.rbegin();
    if (!r->second.Next()) {
      user_phrase_collector_.erase(r->first);
    }
  } else {
    auto r = collector_.rbegin();
    if (!r->second.Next()) {
      collector_.erase(r->first);
    }
  }
  return !CheckEmpty();
}

an<Candidate> SentenceTranslation::Peek() {
  if (exhausted())
    return nullptr;
  if (sentence_) {
    return sentence_;
  }
  size_t code_length = 0;
  an<DictEntry> entry;
  bool is_user_phrase = PreferUserPhrase();
  if (is_user_phrase) {
    auto r = user_phrase_collector_.rbegin();
    code_length = r->first;
    entry = r->second.Peek();
  } else {
    auto r = collector_.rbegin();
    code_length = r->first;
    entry = r->second.Peek();
  }
  auto result = New<Phrase>(translator_ ? translator_->language() : NULL,
                            is_user_phrase ? "user_table" : "table", start_,
                            start_ + code_length, entry);
  if (translator_) {
    string preedit = input_.substr(0, code_length);
    translator_->preedit_formatter().Apply(&preedit);
    result->set_preedit(preedit);
  }
  return result;
}

void SentenceTranslation::PrepareSentence() {
  if (!sentence_)
    return;
  sentence_->Offset(start_);
  sentence_->set_comment(kUnitySymbol);
  sentence_->set_syllabifier(New<SentenceSyllabifier>());

  if (!translator_)
    return;
  string preedit = input_;
  const string& delimiters(translator_->delimiters());
  // split syllables
  size_t pos = 0;
  for (int len : sentence_->word_lengths()) {
    if (pos > 0 && delimiters.find(preedit[pos - 1]) == string::npos) {
      preedit.insert(pos, 1, ' ');
      ++pos;
    }
    pos += len;
  }
  translator_->preedit_formatter().Apply(&preedit);
  sentence_->set_preedit(preedit);
}

bool SentenceTranslation::CheckEmpty() {
  set_exhausted(!sentence_ && collector_.empty() &&
                user_phrase_collector_.empty());
  return exhausted();
}

bool SentenceTranslation::PreferUserPhrase() const {
  // compare code length
  int user_phrase_code_length = 0;
  int table_code_length = 0;
  if (!user_phrase_collector_.empty()) {
    user_phrase_code_length = user_phrase_collector_.rbegin()->first;
  }
  if (!collector_.empty()) {
    table_code_length = collector_.rbegin()->first;
  }
  if (user_phrase_code_length > 0 &&
      user_phrase_code_length >= table_code_length) {
    return true;
  }
  return false;
}

inline static size_t consume_trailing_delimiters(size_t pos,
                                                 const string& input,
                                                 const string& delimiters) {
  while (pos < input.length() && delimiters.find(input[pos]) != string::npos) {
    ++pos;
  }
  return pos;
}

template <class Iter>
inline static void collect_entries(DictEntryList& entries,
                                   Iter& iter,
                                   int max_entries) {
  if (entries.size() < max_entries && !iter.exhausted()) {
    entries.push_back(iter.Peek());
    // alters iter if collecting more than 1 entries
    while (entries.size() < max_entries && iter.Next()) {
      entries.push_back(iter.Peek());
    }
  }
}

an<Translation> TableTranslator::MakeSentence(const string& input,
                                              size_t start,
                                              bool include_prefix_phrases) {
  bool filter_by_charset = enable_charset_filter_ &&
                           !engine_->context()->get_option("extended_charset");
  DictEntryCollector collector;
  UserDictEntryCollector user_phrase_collector;
  WordGraph graph;
  hash_set<int> vertices = {0};
  for (size_t start_pos = 0; start_pos < input.length(); ++start_pos) {
    // find next reachable vertex in word graph
    if (vertices.find(start_pos) == vertices.end())
      continue;
    string active_input = input.substr(start_pos);
    string active_key = active_input + ' ';
    auto& same_start_pos = graph[start_pos];
    // lookup dictionaries
    if (user_dict_ && user_dict_->loaded()) {
      for (size_t len = 1; len <= active_input.length(); ++len) {
        size_t consumed_length =
            consume_trailing_delimiters(len, active_input, delimiters_);
        size_t end_pos = start_pos + consumed_length;
        auto& homographs = same_start_pos[end_pos];
        if (homographs.size() >= max_homographs_)
          continue;
        DLOG(INFO) << "active input: " << active_input << "[0, " << len << ")";
        UserDictEntryIterator uter;
        string resume_key;
        string key = active_input.substr(0, len);
        user_dict_->LookupWords(&uter, key, false, 0, &resume_key);
        if (filter_by_charset) {
          uter.AddFilter(CharsetFilter::FilterDictEntry);
        }
        if (!uter.exhausted()) {
          vertices.insert(end_pos);
          if (start_pos == 0 && max_homographs_ > 1) {
            UserDictEntryIterator uter_copy(uter);
            collect_entries(homographs, uter_copy, max_homographs_);
          } else {
            collect_entries(homographs, uter, max_homographs_);
          }
          if (include_prefix_phrases && start_pos == 0) {
            // also provide words for manual composition
            // uter must not be consumed
            user_phrase_collector[consumed_length] = std::move(uter);
            DLOG(INFO) << "user phrase[" << consumed_length << "] cached: "
                       << user_phrase_collector[consumed_length].cache_size();
          }
        }
        if (resume_key > active_key &&
            !boost::starts_with(resume_key, active_key))
          break;
      }
    }
    if (encoder_ && encoder_->loaded()) {
      UnityTableEncoder::AddPrefix(&active_key);
      for (size_t len = 1; len <= active_input.length(); ++len) {
        size_t consumed_length =
            consume_trailing_delimiters(len, active_input, delimiters_);
        size_t end_pos = start_pos + consumed_length;
        auto& homographs = same_start_pos[end_pos];
        if (!homographs.empty())
          continue;
        DLOG(INFO) << "active input: " << active_input << "[0, " << len << ")";
        UserDictEntryIterator uter;
        string resume_key;
        string key = active_input.substr(0, len);
        encoder_->LookupPhrases(&uter, key, false, 0, &resume_key);
        if (filter_by_charset) {
          uter.AddFilter(CharsetFilter::FilterDictEntry);
        }
        if (!uter.exhausted()) {
          vertices.insert(end_pos);
          if (start_pos == 0 && max_homographs_ > 1) {
            UserDictEntryIterator uter_copy(uter);
            collect_entries(homographs, uter_copy, max_homographs_);
          } else {
            collect_entries(homographs, uter, max_homographs_);
          }
          if (include_prefix_phrases && start_pos == 0) {
            // also provide words for manual composition
            // uter must not be consumed
            user_phrase_collector[consumed_length] = std::move(uter);
            DLOG(INFO) << "unity phrase[" << consumed_length << "] cached: "
                       << user_phrase_collector[consumed_length].cache_size();
          }
        }
        if (resume_key > active_key &&
            !boost::starts_with(resume_key, active_key))
          break;
      }
    }
    if (dict_ && dict_->loaded()) {
      vector<Prism::Match> matches;
      dict_->prism()->CommonPrefixSearch(input.substr(start_pos), &matches);
      set<SyllableId> exact_match_spellings;
      for (const auto& m : matches) {
        exact_match_spellings.insert(m.value);
      }
      if (!matches.empty()) {
        for (const auto& m : boost::adaptors::reverse(matches)) {
          if (m.length == 0)
            continue;
          size_t consumed_length =
              consume_trailing_delimiters(m.length, active_input, delimiters_);
          size_t end_pos = start_pos + consumed_length;
          auto& homographs = same_start_pos[end_pos];
          if (homographs.size() >= max_homographs_)
            continue;
          DictEntryIterator iter;
          dict_->LookupWords(&iter, active_input.substr(0, m.length), false, 0,
                             &blacklist());
          if (filter_by_charset) {
            iter.AddFilter(CharsetFilter::FilterDictEntry);
          }
          if (!iter.exhausted()) {
            vertices.insert(end_pos);
            if (start_pos == 0 && max_homographs_ - homographs.size() > 1) {
              DictEntryIterator iter_copy = iter;
              collect_entries(homographs, iter_copy, max_homographs_);
            } else {
              collect_entries(homographs, iter, max_homographs_);
            }
            if (include_prefix_phrases && start_pos == 0) {
              // also provide words for manual composition
              // iter must not be consumed
              collector[consumed_length] = std::move(iter);
              DLOG(INFO) << "table[" << consumed_length
                         << "]: " << collector[consumed_length].entry_count();
            }
          }
        }
      }
      // Cautious correction edges for sentence building (fallback only):
      // - only when exact prefix search found no matches (dead-end input)
      // - only from segment start (avoid mid-graph explosion / instability)
      // - only adjacent-key distance, full remaining span
      // - capped and strongly down-weighted
      // When exact prefixes exist (e.g. uglh → ugl|h), leave the graph alone;
      // MakeCorrectionTranslation still appends corrected candidates in Query.
      if (enable_correction_ && corrector_ && start_pos == 0 &&
          !active_input.empty() && matches.empty() &&
          same_start_pos.empty()) {
        corrector::Corrections corrections;
        // NearSearchCorrector mutates the key buffer temporarily; pass a copy.
        string correction_input = active_input;
        corrector_->ToleranceSearch(*dict_->prism(), correction_input,
                                    &corrections, kSentenceCorrectionTolerance);
        size_t added = 0;
        for (const auto& item : SortedCorrections(corrections)) {
          if (added >= kMaxSentenceCorrectionsPerPos)
            break;
          if (exact_match_spellings.count(item.first))
            continue;
          if (item.second.distance == 0 ||
              item.second.distance > kSentenceCorrectionTolerance)
            continue;
          if (item.second.length != active_input.length())
            continue;

          for (auto accessor = dict_->prism()->QuerySpelling(item.first);
               !accessor.exhausted(); accessor.Next()) {
            if (added >= kMaxSentenceCorrectionsPerPos)
              break;
            auto props = accessor.properties();
            if (props.type != kNormalSpelling || props.is_correction)
              continue;
            string corrected = dict_->primary_table()->GetSyllableById(
                accessor.syllable_id());
            if (corrected.empty() || corrected == active_input)
              continue;

            size_t consumed_length = consume_trailing_delimiters(
                item.second.length, active_input, delimiters_);
            size_t end_pos = start_pos + consumed_length;
            auto& homographs = same_start_pos[end_pos];
            if (homographs.size() >= static_cast<size_t>(max_homographs_))
              continue;

            DictEntryIterator iter;
            if (!dict_->LookupWords(&iter, corrected, false, 0,
                                    &blacklist()) ||
                iter.exhausted()) {
              continue;
            }
            if (filter_by_charset) {
              iter.AddFilter(CharsetFilter::FilterDictEntry);
            }
            size_t before = homographs.size();
            while (!iter.exhausted() &&
                   homographs.size() < static_cast<size_t>(max_homographs_)) {
              auto entry = iter.Peek();
              if (entry) {
                auto adjusted = New<DictEntry>(*entry);
                adjusted->weight += kCorrectionCredibility;
                homographs.push_back(std::move(adjusted));
              }
              if (!iter.Next())
                break;
            }
            if (homographs.size() > before) {
              vertices.insert(end_pos);
              ++added;
            }
          }
        }
      }
    }
  }
  if (!poet_)
    return nullptr;
  if (auto sentence =
          poet_->MakeSentence(graph, input.length(), GetPrecedingText(start))) {
    auto result = Cached<SentenceTranslation>(
        this, std::move(sentence), std::move(collector),
        std::move(user_phrase_collector), input, start);
    if (result && filter_by_charset) {
      return New<CharsetFilterTranslation>(result);
    }
    return result;
  }
  return nullptr;
}

}  // namespace rime
