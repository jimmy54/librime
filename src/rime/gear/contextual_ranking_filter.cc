//
// Copyright RIME Developers
// Distributed under the BSD License
//
// Contextual Ranking Filter Implementation
//

#include <rime/gear/contextual_ranking_filter.h>
#include <rime/candidate.h>
#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/schema.h>
#include <rime/translation.h>
#include <algorithm>
#include <vector>

namespace rime {

static Grammar* create_grammar(Config* config) {
  if (!config)
    return nullptr;
  if (auto* component = Grammar::Require("grammar")) {
    return component->Create(config);
  }
  return nullptr;
}

ContextualRankingFilter::ContextualRankingFilter(const Ticket& ticket)
    : Filter(ticket), grammar_(nullptr), enabled_(true), max_candidates_(20) {
  if (ticket.schema) {
    Config* config = ticket.schema->config();
    if (config) {
      // Check if contextual ranking is enabled
      config->GetBool(name_space_ + "/contextual_ranking", &enabled_);
      // Get max candidates to re-rank
      config->GetInt(name_space_ + "/max_rerank_candidates", &max_candidates_);
      // Create grammar for scoring
      grammar_ = create_grammar(config);
    }
  }
}

an<Translation> ContextualRankingFilter::Apply(an<Translation> translation,
                                               CandidateList* candidates) {
  if (!enabled_ || !grammar_ || !translation || translation->exhausted()) {
    return translation;
  }

  // Get context from engine
  Context* ctx = engine_->context();
  if (!ctx) {
    return translation;
  }

  // Get external context (priority) or internal context
  string left_context = ctx->external_preceding_text();
  string right_context = ctx->external_following_text();

  // If no external context, use internal context
  if (left_context.empty()) {
    left_context = ctx->commit_history().latest_text();
  }

  // If no context at all, skip re-ranking
  if (left_context.empty() && right_context.empty()) {
    return translation;
  }

  DLOG(INFO) << "ContextualRankingFilter: left=\"" << left_context
             << "\", right=\"" << right_context << "\"";

  // Collect candidates with scores
  vector<pair<an<Candidate>, double>> scored_candidates;
  int count = 0;

  while (!translation->exhausted() && count < max_candidates_) {
    auto cand = translation->Peek();
    if (!cand) {
      translation->Next();
      continue;
    }

    // Calculate contextual score
    double left_score = 0.0;
    double right_score = 0.0;

    if (!left_context.empty()) {
      left_score = grammar_->Query(left_context, cand->text(), false);
    }

    if (!right_context.empty()) {
      // For right context, treat current word as context and right text as word
      // This is a simplified approach; ideally we'd have bidirectional model
      right_score = grammar_->Query(cand->text(), right_context, true);
    }

    // Combine scores: original quality + contextual scores
    double total_score = cand->quality() + left_score + right_score;

    DLOG(INFO) << "Candidate: \"" << cand->text()
               << "\" quality=" << cand->quality() << " left=" << left_score
               << " right=" << right_score << " total=" << total_score;

    scored_candidates.push_back({cand, total_score});
    translation->Next();
    ++count;
  }

  // If we have candidates to re-rank
  if (!scored_candidates.empty()) {
    // Sort by total score (descending)
    std::stable_sort(
        scored_candidates.begin(), scored_candidates.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    // Create new translation with re-ranked candidates
    auto fifo = New<FifoTranslation>();
    for (auto& [cand, score] : scored_candidates) {
      // Update candidate quality with new score
      cand->set_quality(score);
      fifo->Append(cand);
    }

    // Append remaining candidates from original translation
    while (!translation->exhausted()) {
      if (auto cand = translation->Peek()) {
        fifo->Append(cand);
      }
      translation->Next();
    }

    return fifo;
  }

  return translation;
}

}  // namespace rime
