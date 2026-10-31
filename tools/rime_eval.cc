//
// Copyright RIME Developers
// Distributed under the BSD License
//
// batch candidate-ranking evaluator
//
// Each case is "context<TAB>code<TAB>target": set the external preceding text,
// type the code, then report where target lands in the menu. Meant as a
// regression harness for ranking changes; case generation and offline
// re-scoring live in tools/eval/.
//
#include <rime_api.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

// One shared candidate budget keeps runs comparable: the reference re-ranker
// scores 32 candidates per code.
constexpr int kMaxHomophones = 32;
constexpr int kMaxHomographs = 32;

struct Options {
  std::string shared_data_dir;
  std::string user_data_dir;
  std::string cases_path;
  std::string out_path;
  std::string schema = "rime_ice_26";
  std::vector<std::pair<std::string, std::string>> configs;
  int page_size = 32;
  int topk = 32;
  bool contextual_suggestions = true;
  bool verbose = false;
};

void PrintUsage() {
  fprintf(
      stderr,
      "usage: rime_eval --shared-dir DIR --user-dir DIR --cases FILE "
      "[options]\n"
      "  --schema ID          schema to evaluate (default rime_ice_26)\n"
      "  --out PATH           write per-case results here (default stdout)\n"
      "  --config KEY=VALUE   extra schema patch, e.g. grammar/component=clmg\n"
      "                       (repeatable; VALUE is written as raw YAML)\n"
      "  --no-contextual      disable translator/contextual_suggestions\n"
      "  --page-size N        menu page size (default 32)\n"
      "  --topk N             candidates to record per case (default 32)\n"
      "  --verbose            keep info level logs\n"
      "  --help               show this message\n"
      "\n"
      "cases: lines of \"context<TAB>code<TAB>target\"; blank lines are\n"
      "ignored. The menu is normalized to\n"
      "max_homophones=max_homographs=32, max_sentences=0 and\n"
      "grammar/apply_to_sentence=false so that every run sees the same\n"
      "word-level candidate pool.\n"
      "\n"
      "output: \"idx<TAB>rank<TAB>cand1|cand2|...\" plus '#'-prefixed\n"
      "summary lines; rank -1 means target was not among the top-k.\n");
}

bool ParseArgs(int argc, char** argv, Options* opts) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto next = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing value for %s\n", name);
        return nullptr;
      }
      return argv[++i];
    };
    if (arg == "--shared-dir") {
      const char* v = next("--shared-dir");
      if (!v)
        return false;
      opts->shared_data_dir = v;
    } else if (arg == "--user-dir") {
      const char* v = next("--user-dir");
      if (!v)
        return false;
      opts->user_data_dir = v;
    } else if (arg == "--cases") {
      const char* v = next("--cases");
      if (!v)
        return false;
      opts->cases_path = v;
    } else if (arg == "--out") {
      const char* v = next("--out");
      if (!v)
        return false;
      opts->out_path = v;
    } else if (arg == "--schema") {
      const char* v = next("--schema");
      if (!v)
        return false;
      opts->schema = v;
    } else if (arg == "--config") {
      const char* v = next("--config");
      if (!v)
        return false;
      std::string patch = v;
      auto eq = patch.find('=');
      if (eq == std::string::npos || eq == 0) {
        fprintf(stderr, "malformed --config: %s\n", v);
        return false;
      }
      opts->configs.emplace_back(patch.substr(0, eq), patch.substr(eq + 1));
    } else if (arg == "--no-contextual") {
      opts->contextual_suggestions = false;
    } else if (arg == "--page-size") {
      const char* v = next("--page-size");
      if (!v)
        return false;
      opts->page_size = std::max(1, atoi(v));
    } else if (arg == "--topk") {
      const char* v = next("--topk");
      if (!v)
        return false;
      opts->topk = std::max(1, atoi(v));
    } else if (arg == "--verbose") {
      opts->verbose = true;
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage();
      return false;
    } else {
      fprintf(stderr, "unknown argument: %s\n", arg.c_str());
      PrintUsage();
      return false;
    }
  }
  if (opts->shared_data_dir.empty() || opts->user_data_dir.empty() ||
      opts->cases_path.empty()) {
    PrintUsage();
    return false;
  }
  return true;
}

bool WriteFile(const fs::path& path, const std::string& content) {
  std::ofstream out(path);
  if (!out) {
    fprintf(stderr, "cannot write %s\n", path.string().c_str());
    return false;
  }
  out << content;
  return true;
}

bool PrepareUserDir(const Options& opts) {
  std::error_code ec;
  fs::create_directories(opts.user_data_dir, ec);
  if (ec) {
    fprintf(stderr, "cannot create user dir %s: %s\n",
            opts.user_data_dir.c_str(), ec.message().c_str());
    return false;
  }
  if (!WriteFile(fs::path(opts.user_data_dir) / "default.custom.yaml",
                 "patch:\n"
                 "  schema_list:\n"
                 "    - schema: " +
                     opts.schema + "\n"))
    return false;
  std::string patch;
  patch += "patch:\n";
  patch += std::string("  \"translator/contextual_suggestions\": ") +
           (opts.contextual_suggestions ? "true" : "false") + "\n";
  patch +=
      "  \"translator/max_homophones\": " + std::to_string(kMaxHomophones) +
      "\n";
  patch +=
      "  \"translator/max_homographs\": " + std::to_string(kMaxHomographs) +
      "\n";
  patch += "  \"translator/max_sentences\": 0\n";
  patch += "  \"grammar/apply_to_sentence\": false\n";
  patch += "  \"menu/page_size\": " + std::to_string(opts.page_size) + "\n";
  for (const auto& config : opts.configs) {
    patch += "  \"" + config.first + "\": " + config.second + "\n";
  }
  if (!WriteFile(fs::path(opts.user_data_dir) / (opts.schema + ".custom.yaml"),
                 patch))
    return false;
  fs::create_directories(fs::path(opts.user_data_dir) / "log", ec);
  return true;
}

double Percentile(std::vector<double> values, double p) {
  if (values.empty())
    return 0.0;
  std::sort(values.begin(), values.end());
  size_t index = static_cast<size_t>(std::ceil(p * values.size())) - 1;
  index = std::min(index, values.size() - 1);
  return values[index];
}

struct CaseStats {
  int cases = 0;
  int skipped_lines = 0;
  int found = 0;
  int top1 = 0;
  int top3 = 0;
  double mrr = 0.0;
  std::vector<double> latency_ms;
};

void PrintSummary(FILE* results, const CaseStats& stats) {
  int n = stats.cases;
  double top1 = n ? 100.0 * stats.top1 / n : 0.0;
  double top3 = n ? 100.0 * stats.top3 / n : 0.0;
  double mrr = n ? stats.mrr / n : 0.0;
  double mean = 0.0;
  for (double v : stats.latency_ms)
    mean += v;
  if (!stats.latency_ms.empty())
    mean /= stats.latency_ms.size();

  fprintf(results, "#cases\t%d\n", n);
  fprintf(results, "#found\t%d\n", stats.found);
  fprintf(results, "#top1\t%.2f\n", top1);
  fprintf(results, "#top3\t%.2f\n", top3);
  fprintf(results, "#mrr\t%.4f\n", mrr);
  fprintf(results, "#latency_mean_ms\t%.2f\n", mean);

  fprintf(stderr, "cases %d, found %d, top1 %.2f%%, top3 %.2f%%, MRR %.4f\n", n,
          stats.found, top1, top3, mrr);
  if (!stats.latency_ms.empty()) {
    fprintf(stderr, "keys+candidates: mean %.2f ms p50 %.2f p90 %.2f\n", mean,
            Percentile(stats.latency_ms, 0.5),
            Percentile(stats.latency_ms, 0.9));
  }
  if (stats.skipped_lines)
    fprintf(stderr, "skipped malformed lines: %d\n", stats.skipped_lines);
}

}  // namespace

int main(int argc, char** argv) {
  Options opts;
  if (!ParseArgs(argc, argv, &opts))
    return 1;
  if (!PrepareUserDir(opts))
    return 1;

  std::ifstream cases(opts.cases_path);
  if (!cases) {
    fprintf(stderr, "cannot open cases file %s\n", opts.cases_path.c_str());
    return 1;
  }

  FILE* results = stdout;
  if (!opts.out_path.empty()) {
    results = fopen(opts.out_path.c_str(), "w");
    if (!results) {
      fprintf(stderr, "cannot open output file %s\n", opts.out_path.c_str());
      return 1;
    }
  }

  RimeApi* rime = rime_get_api();
  if (!RIME_API_AVAILABLE(rime, set_context_text)) {
    fprintf(
        stderr,
        "rime API lacks set_context_text; build against this source tree\n");
    return 1;
  }

  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = opts.shared_data_dir.c_str();
  traits.user_data_dir = opts.user_data_dir.c_str();
  std::string prebuilt_dir = opts.shared_data_dir + "/build";
  traits.prebuilt_data_dir = prebuilt_dir.c_str();
  traits.distribution_name = "Rime Evaluator";
  traits.distribution_code_name = "rime-eval";
  traits.distribution_version = "1.0";
  traits.app_name = "rime.eval";
  std::string log_dir = opts.user_data_dir + "/log";
  traits.log_dir = log_dir.c_str();
  traits.min_log_level = opts.verbose ? 0 : 2;

  rime->setup(&traits);
  auto setup_started = Clock::now();
  rime->initialize(&traits);
  rime->start_maintenance(False);
  rime->join_maintenance_thread();
  fprintf(
      stderr, "init+deploy: %.0f ms\n",
      std::chrono::duration<double, std::milli>(Clock::now() - setup_started)
          .count());

  CaseStats stats;
  std::string line;
  while (std::getline(cases, line)) {
    if (line.empty())
      continue;
    auto tab1 = line.find('\t');
    auto tab2 = tab1 == std::string::npos ? tab1 : line.find('\t', tab1 + 1);
    if (tab1 == std::string::npos || tab2 == std::string::npos) {
      ++stats.skipped_lines;
      continue;
    }
    std::string context = line.substr(0, tab1);
    std::string code = line.substr(tab1 + 1, tab2 - tab1 - 1);
    std::string target = line.substr(tab2 + 1);
    if (target.empty() || code.empty()) {
      ++stats.skipped_lines;
      continue;
    }

    RimeSessionId session = rime->create_session();
    if (!session) {
      fprintf(stderr, "failed to create session at case %d\n", stats.cases);
      return 1;
    }
    if (!rime->select_schema(session, opts.schema.c_str())) {
      fprintf(stderr, "failed to select schema %s\n", opts.schema.c_str());
      return 1;
    }
    rime->set_context_text(session, context.c_str(), "");

    auto started = Clock::now();
    for (char c : code) {
      if (c >= 'a' && c <= 'z')
        rime->process_key(session, static_cast<int>(c), 0);
    }

    std::string candidate_list;
    int rank = -1;
    RIME_STRUCT(RimeContext, context_state);
    if (rime->get_context(session, &context_state)) {
      int count = std::min(context_state.menu.num_candidates, opts.topk);
      for (int i = 0; i < count; ++i) {
        const char* text = context_state.menu.candidates[i].text;
        if (!text)
          continue;
        if (i)
          candidate_list += '|';
        candidate_list += text;
        if (rank < 0 && target == text)
          rank = i;
      }
      rime->free_context(&context_state);
    }
    stats.latency_ms.push_back(
        std::chrono::duration<double, std::milli>(Clock::now() - started)
            .count());

    fprintf(results, "%d\t%d\t%s\n", stats.cases, rank, candidate_list.c_str());
    ++stats.cases;
    if (rank >= 0) {
      ++stats.found;
      stats.mrr += 1.0 / (rank + 1);
      if (rank == 0)
        ++stats.top1;
      if (rank < 3)
        ++stats.top3;
    }
    rime->destroy_session(session);
  }

  PrintSummary(results, stats);
  if (results != stdout)
    fclose(results);
  rime->finalize();
  return 0;
}
