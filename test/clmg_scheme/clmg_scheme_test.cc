//
// Copyright RIME Developers
// Distributed under the BSD License
//

#include <gtest/gtest.h>
#include <rime_api.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "clmg_scheme_env.h"

namespace {

using Clock = std::chrono::steady_clock;

struct SchemeCase {
  std::string context;
  std::string code;
  std::string target;
};

struct QueryResult {
  std::vector<std::string> candidates;
  double ms = 0.0;
};

std::vector<SchemeCase> LoadCases(const std::string& path) {
  std::vector<SchemeCase> cases;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty())
      continue;
    const auto tab1 = line.find('\t');
    const auto tab2 =
        tab1 == std::string::npos ? tab1 : line.find('\t', tab1 + 1);
    if (tab1 == std::string::npos || tab2 == std::string::npos)
      continue;
    SchemeCase c;
    c.context = line.substr(0, tab1);
    c.code = line.substr(tab1 + 1, tab2 - tab1 - 1);
    c.target = line.substr(tab2 + 1);
    if (c.code.empty() || c.target.empty())
      continue;
    cases.push_back(std::move(c));
  }
  return cases;
}

QueryResult Query(RimeSessionId session,
                  const std::string& context,
                  const std::string& code,
                  int topk = 32) {
  RimeApi* rime = rime_get_api();
  rime->clear_composition(session);
  rime->set_context_text(session, context.c_str(), "");
  const auto start = Clock::now();
  for (char ch : code) {
    if (ch >= 'a' && ch <= 'z')
      rime->process_key(session, static_cast<int>(ch), 0);
  }
  QueryResult result;
  RIME_STRUCT(RimeContext, ctx);
  if (rime->get_context(session, &ctx)) {
    const int count = std::min(ctx.menu.num_candidates, topk);
    for (int i = 0; i < count; ++i) {
      if (const char* text = ctx.menu.candidates[i].text)
        result.candidates.emplace_back(text);
    }
    rime->free_context(&ctx);
  }
  result.ms =
      std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  return result;
}

std::vector<std::string> RunCase(const std::string& schema,
                                 const SchemeCase& c) {
  RimeApi* rime = rime_get_api();
  const RimeSessionId session = rime->create_session();
  if (!session)
    return {};
  std::vector<std::string> candidates;
  if (rime->select_schema(session, schema.c_str()))
    candidates = Query(session, c.context, c.code).candidates;
  rime->destroy_session(session);
  return candidates;
}

int RankOf(const std::vector<std::string>& candidates,
           const std::string& target) {
  const auto it = std::find(candidates.begin(), candidates.end(), target);
  return it == candidates.end() ? -1 : static_cast<int>(it - candidates.begin());
}

}  // namespace

TEST(RimeClmgSchemeTest, LoadsUserSchemeAndTypes) {
  ClmgSchemeEnvironment& env = GetClmgSchemeEnvironment();
  if (!env.ready)
    GTEST_SKIP() << env.unavailable_reason;

  const auto nihao = RunCase(env.schema, {"", "nihao", "你好"});
  ASSERT_FALSE(nihao.empty());
  EXPECT_EQ("你好", nihao.front());

  const auto shijie = RunCase(env.schema, {"", "shijie", "世界"});
  ASSERT_FALSE(shijie.empty());
  EXPECT_EQ("世界", shijie.front());

  const auto control = RunCase(env.control_schema, {"", "nihao", "你好"});
  ASSERT_FALSE(control.empty());
  EXPECT_EQ("你好", control.front());
}

TEST(RimeClmgSchemeTest, ClmgReranksOnUserScheme) {
  ClmgSchemeEnvironment& env = GetClmgSchemeEnvironment();
  if (!env.ready)
    GTEST_SKIP() << env.unavailable_reason;
  const std::vector<SchemeCase> cases = LoadCases(env.cases_path);
  ASSERT_GT(cases.size(), 1045u) << env.cases_path;

  // Case indices in tools/eval/cases/test.tsv: the first group is moved to
  // rank 0 by CLMG on the supplied 26-key resources, the second group must
  // stay untouched.
  const size_t improved[] = {28, 815, 1031, 1045};
  const size_t stable[] = {0, 1, 2};

  int improved_count = 0;
  for (size_t index : improved) {
    const SchemeCase& c = cases[index];
    const auto base = RunCase(env.control_schema, c);
    const auto clmg = RunCase(env.schema, c);
    const int base_rank = RankOf(base, c.target);
    const int clmg_rank = RankOf(clmg, c.target);
    EXPECT_GE(base_rank, 0) << "case " << index;
    EXPECT_EQ(0, clmg_rank) << "case " << index << " ctx=" << c.context;
    if (base_rank > 0 && clmg_rank == 0)
      ++improved_count;
  }
  EXPECT_EQ(4, improved_count);

  for (size_t index : stable) {
    const SchemeCase& c = cases[index];
    const auto base = RunCase(env.control_schema, c);
    const auto clmg = RunCase(env.schema, c);
    ASSERT_FALSE(base.empty()) << "case " << index;
    EXPECT_EQ(RankOf(base, c.target), RankOf(clmg, c.target))
        << "case " << index;
    EXPECT_EQ(base.front(), clmg.front()) << "case " << index;
  }
}

TEST(RimeClmgSchemeTest, EmptyContextLeavesRankingUnchanged) {
  ClmgSchemeEnvironment& env = GetClmgSchemeEnvironment();
  if (!env.ready)
    GTEST_SKIP() << env.unavailable_reason;

  for (const char* code : {"nihao", "shijie"}) {
    const auto base = RunCase(env.control_schema, {"", code, ""});
    const auto clmg = RunCase(env.schema, {"", code, ""});
    ASSERT_FALSE(base.empty()) << code;
    EXPECT_EQ(base, clmg) << code;
  }
}

TEST(RimeClmgSchemeTest, ContextualQueryLatency) {
  ClmgSchemeEnvironment& env = GetClmgSchemeEnvironment();
  if (!env.ready)
    GTEST_SKIP() << env.unavailable_reason;
  const std::vector<SchemeCase> cases = LoadCases(env.cases_path);
  ASSERT_GT(cases.size(), 815u) << env.cases_path;
  const SchemeCase& c = cases[815];

  RimeApi* rime = rime_get_api();
  const RimeSessionId session = rime->create_session();
  ASSERT_NE(0, session);
  ASSERT_TRUE(rime->select_schema(session, env.schema.c_str()));
  Query(session, "今天天气", "nihao");  // load model, warm the engine paths
  const QueryResult cold = Query(session, c.context, c.code);
  const QueryResult warm = Query(session, c.context, c.code);
  rime->destroy_session(session);

  const RimeSessionId control = rime->create_session();
  ASSERT_NE(0, control);
  ASSERT_TRUE(rime->select_schema(control, env.control_schema.c_str()));
  Query(control, "今天天气", "nihao");
  const QueryResult disabled = Query(control, c.context, c.code);
  rime->destroy_session(control);

  ASSERT_FALSE(cold.candidates.empty());
  EXPECT_EQ(cold.candidates, warm.candidates);
  std::printf(
      "[clmg-scheme] contextual query: cold %.2f ms, warm %.2f ms, "
      "alpha=0 %.2f ms\n",
      cold.ms, warm.ms, disabled.ms);
  EXPECT_LT(cold.ms, 5000.0);
  EXPECT_LT(warm.ms, cold.ms);
  EXPECT_LT(disabled.ms, cold.ms);
}
