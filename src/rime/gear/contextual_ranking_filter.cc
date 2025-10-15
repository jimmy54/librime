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
#include <chrono>
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
    : Filter(ticket),
      grammar_(nullptr),
      enabled_(true),
      max_candidates_(8),       // Changed from 20 to 8 (Solution 2)
      min_input_length_(2),     // Solution 5: Skip short inputs
      debounce_delay_ms_(100),  // Solution 5: Debounce fast typing
      last_input_time_(std::chrono::steady_clock::now()) {
  if (ticket.schema) {
    Config* config = ticket.schema->config();
    if (config) {
      // Check if contextual ranking is enabled
      config->GetBool(name_space_ + "/contextual_ranking", &enabled_);
      // Get max candidates to re-rank (Solution 2)
      config->GetInt(name_space_ + "/max_rerank_candidates", &max_candidates_);
      // Get smart triggering parameters (Solution 5)
      config->GetInt(name_space_ + "/min_input_length", &min_input_length_);
      config->GetInt(name_space_ + "/debounce_delay_ms", &debounce_delay_ms_);
      // Create grammar for scoring
      grammar_ = create_grammar(config);
    }
  }

  LOG(INFO) << "ContextualRankingFilter initialized:";
  LOG(INFO) << "  - enabled: " << enabled_;
  LOG(INFO) << "  - max_rerank_candidates: " << max_candidates_;
  LOG(INFO) << "  - min_input_length: " << min_input_length_;
  LOG(INFO) << "  - debounce_delay_ms: " << debounce_delay_ms_;
}

an<Translation> ContextualRankingFilter::Apply(an<Translation> translation,
                                               CandidateList* candidates) {
  // Start timing
  auto start_time = std::chrono::steady_clock::now();

  if (!enabled_ || !grammar_ || !translation || translation->exhausted()) {
    return translation;
  }

  // Get context from engine
  Context* ctx = engine_->context();
  if (!ctx) {
    return translation;
  }

  // === Solution 5: Smart Triggering Strategy ===

  // 1. Check input length - skip if too short
  size_t input_length = ctx->input().length();
  if (input_length < static_cast<size_t>(min_input_length_)) {
    DLOG(INFO) << "ContextualRankingFilter: Skip (input too short: "
               << input_length << " < " << min_input_length_ << ")";
    return translation;
  }

  // 2. Check input interval - skip if typing too fast
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - last_input_time_)
                     .count();
  last_input_time_ = now;

  if (elapsed < debounce_delay_ms_) {
    DLOG(INFO) << "ContextualRankingFilter: Skip (typing too fast: " << elapsed
               << "ms < " << debounce_delay_ms_ << "ms)";
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
    DLOG(INFO) << "ContextualRankingFilter: Skip (no context)";
    return translation;
  }

  LOG(INFO) << "ContextualRankingFilter: Triggered!";
  LOG(INFO) << "  - Input length: " << input_length;
  LOG(INFO) << "  - Time since last: " << elapsed << "ms";
  LOG(INFO) << "  - Context: left=\"" << left_context << "\", right=\""
            << right_context << "\"";

  // Collect candidates with scores
  vector<pair<an<Candidate>, double>> scored_candidates;
  int count = 0;
  int total_queries = 0;
  long long total_query_time_us = 0;

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
      auto query_start = std::chrono::steady_clock::now();
      left_score = grammar_->Query(left_context, cand->text(), false);
      auto query_end = std::chrono::steady_clock::now();
      auto query_duration =
          std::chrono::duration_cast<std::chrono::microseconds>(query_end -
                                                                query_start)
              .count();
      total_query_time_us += query_duration;
      total_queries++;

      DLOG(INFO) << "  Left Query took " << query_duration << "μs";
    }

    if (!right_context.empty()) {
      // For right context, treat current word as context and right text as word
      // This is a simplified approach; ideally we'd have bidirectional model
      auto query_start = std::chrono::steady_clock::now();
      right_score = grammar_->Query(cand->text(), right_context, true);
      auto query_end = std::chrono::steady_clock::now();
      auto query_duration =
          std::chrono::duration_cast<std::chrono::microseconds>(query_end -
                                                                query_start)
              .count();
      total_query_time_us += query_duration;
      total_queries++;

      DLOG(INFO) << "  Right Query took " << query_duration << "μs";
    }

    // Combine scores: original quality + contextual scores
    double total_score = cand->quality() + left_score + right_score;

    LOG(INFO) << "Candidate: \"" << cand->text()
              << "\" quality=" << cand->quality() << " left=" << left_score
              << " right=" << right_score << " total=" << total_score;

    scored_candidates.push_back({cand, total_score});
    translation->Next();
    ++count;
  }

  // If we have candidates to re-rank
  if (!scored_candidates.empty()) {
    // Check if we have enough candidates to make re-ranking worthwhile
    if (scored_candidates.size() < 3) {
      DLOG(INFO)
          << "ContextualRankingFilter: Skip sorting (too few candidates: "
          << scored_candidates.size() << "), but still return collected ones";

      // Return collected candidates without sorting
      auto fifo = New<FifoTranslation>();
      for (auto& [cand, score] : scored_candidates) {
        fifo->Append(cand);
      }
      // Append remaining candidates
      while (!translation->exhausted()) {
        if (auto cand = translation->Peek()) {
          fifo->Append(cand);
        }
        translation->Next();
      }
      return fifo;
    }
    // Sort by total score (descending)
    auto sort_start = std::chrono::steady_clock::now();
    std::stable_sort(
        scored_candidates.begin(), scored_candidates.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    auto sort_end = std::chrono::steady_clock::now();
    auto sort_duration = std::chrono::duration_cast<std::chrono::microseconds>(
                             sort_end - sort_start)
                             .count();

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

    // Calculate and log total time
    auto end_time = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(
                              end_time - start_time)
                              .count();

    LOG(INFO) << "ContextualRankingFilter Performance:";
    LOG(INFO) << "  - Candidates processed: " << count;
    LOG(INFO) << "  - Total queries: " << total_queries;
    LOG(INFO) << "  - Query time: " << total_query_time_us << "μs ("
              << (total_query_time_us / 1000.0) << "ms)";
    LOG(INFO) << "  - Avg per query: "
              << (total_queries > 0 ? total_query_time_us / total_queries : 0)
              << "μs";
    LOG(INFO) << "  - Sort time: " << sort_duration << "μs";
    LOG(INFO) << "  - Total time: " << total_duration << "μs ("
              << (total_duration / 1000.0) << "ms)";

    return fifo;
  }

  return translation;
}

}  // namespace rime
