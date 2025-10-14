//
// Copyright RIME Developers
// Distributed under the BSD License
//
// Contextual Ranking Filter
// Re-rank candidates based on bidirectional context using grammar model
//

#ifndef RIME_CONTEXTUAL_RANKING_FILTER_H_
#define RIME_CONTEXTUAL_RANKING_FILTER_H_

#include <rime/common.h>
#include <rime/filter.h>
#include <rime/gear/grammar.h>

namespace rime {

class Engine;

class ContextualRankingFilter : public Filter {
 public:
  explicit ContextualRankingFilter(const Ticket& ticket);

  virtual an<Translation> Apply(an<Translation> translation,
                                CandidateList* candidates) override;

 private:
  Grammar* grammar_;
  bool enabled_;
  int max_candidates_;  // Maximum number of candidates to re-rank
};

}  // namespace rime

#endif  // RIME_CONTEXTUAL_RANKING_FILTER_H_
