//
// Copyright RIME Developers
// Distributed under the BSD License
//
// per-keystroke latency sampler
//
#include <rime_api.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

struct Options {
  std::string shared_data_dir;
  std::string user_data_dir;
  std::string schema = "rime_ice_26";
  std::string keys = "jintiantianqizhenbucuo";
  std::string context;
  std::string csv_path;
  int warmup_rounds = 3;
  int rounds = 20;
  int max_sentences = 0;   // 0 = keep schema setting
  int max_homophones = 0;  // 0 = keep schema setting
  int dump_each = 0;       // >0: print top candidates after each key
  bool apply_to_sentence = false;
  bool contextual_suggestions = true;
  bool dump_candidates = false;
  bool all_pages = false;
  bool verbose = false;
};

void PrintUsage() {
  fprintf(stderr,
          "usage: rime_latency --shared-dir DIR --user-dir DIR [options]\n"
          "  --schema ID            schema to bench (default rime_ice_26)\n"
          "  --keys STRING          key sequence, a-z 0-9 ' (default "
          "jintiantianqizhenbucuo)\n"
          "  --context STRING       external preceding text\n"
          "  --rounds N             measured rounds (default 20)\n"
          "  --warmup N             warmup rounds (default 3)\n"
          "  --apply-to-sentence    enable grammar/apply_to_sentence\n"
          "  --no-contextual        disable translator/contextual_suggestions\n"
          "  --max-sentences N      patch translator/max_sentences\n"
          "  --max-homophones N     patch translator/max_homophones\n"
          "  --dump                 print final candidates\n"
          "  --all-pages            print every candidate page\n"
          "  --dump-each N          print top N candidates after every key\n"
          "  --csv PATH             dump per-key samples\n"
          "  --verbose              keep info level logs\n");
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
    } else if (arg == "--schema") {
      const char* v = next("--schema");
      if (!v)
        return false;
      opts->schema = v;
    } else if (arg == "--keys") {
      const char* v = next("--keys");
      if (!v)
        return false;
      opts->keys = v;
    } else if (arg == "--context") {
      const char* v = next("--context");
      if (!v)
        return false;
      opts->context = v;
    } else if (arg == "--rounds") {
      const char* v = next("--rounds");
      if (!v)
        return false;
      opts->rounds = std::max(1, atoi(v));
    } else if (arg == "--warmup") {
      const char* v = next("--warmup");
      if (!v)
        return false;
      opts->warmup_rounds = std::max(0, atoi(v));
    } else if (arg == "--apply-to-sentence") {
      opts->apply_to_sentence = true;
    } else if (arg == "--no-contextual") {
      opts->contextual_suggestions = false;
    } else if (arg == "--max-sentences") {
      const char* v = next("--max-sentences");
      if (!v)
        return false;
      opts->max_sentences = std::max(0, atoi(v));
    } else if (arg == "--max-homophones") {
      const char* v = next("--max-homophones");
      if (!v)
        return false;
      opts->max_homophones = std::max(0, atoi(v));
    } else if (arg == "--dump") {
      opts->dump_candidates = true;
    } else if (arg == "--all-pages") {
      opts->all_pages = true;
    } else if (arg == "--dump-each") {
      const char* v = next("--dump-each");
      if (!v)
        return false;
      opts->dump_each = std::max(1, atoi(v));
    } else if (arg == "--csv") {
      const char* v = next("--csv");
      if (!v)
        return false;
      opts->csv_path = v;
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
  if (opts->shared_data_dir.empty() || opts->user_data_dir.empty()) {
    PrintUsage();
    return false;
  }
  return true;
}

void WriteFile(const fs::path& path, const std::string& content) {
  std::ofstream out(path);
  out << content;
}

bool PrepareUserDir(const Options& opts) {
  std::error_code ec;
  fs::create_directories(opts.user_data_dir, ec);
  if (ec) {
    fprintf(stderr, "cannot create user dir %s: %s\n",
            opts.user_data_dir.c_str(), ec.message().c_str());
    return false;
  }
  WriteFile(fs::path(opts.user_data_dir) / "default.custom.yaml",
            "patch:\n"
            "  schema_list:\n"
            "    - schema: " +
                opts.schema + "\n");
  std::string patch;
  patch += "patch:\n";
  patch += std::string("  \"grammar/apply_to_sentence\": ") +
           (opts.apply_to_sentence ? "true" : "false") + "\n";
  patch += std::string("  \"translator/contextual_suggestions\": ") +
           (opts.contextual_suggestions ? "true" : "false") + "\n";
  if (opts.max_sentences > 0) {
    patch += "  \"translator/max_sentences\": " +
             std::to_string(opts.max_sentences) + "\n";
  }
  if (opts.max_homophones > 0) {
    patch += "  \"translator/max_homophones\": " +
             std::to_string(opts.max_homophones) + "\n";
  }
  WriteFile(fs::path(opts.user_data_dir) / (opts.schema + ".custom.yaml"),
            patch);
  fs::create_directories(fs::path(opts.user_data_dir) / "log", ec);
  return true;
}

int KeyCodeOf(char c) {
  switch (c) {
    case ' ':
      return 0x20;
    case '\n':
      return 0xff0d;
    default:
      return static_cast<unsigned char>(c);
  }
}

double Percentile(std::vector<double> values, double p) {
  if (values.empty())
    return 0.0;
  std::sort(values.begin(), values.end());
  size_t index = static_cast<size_t>(std::ceil(p * values.size())) - 1;
  index = std::min(index, values.size() - 1);
  return values[index];
}

std::string TrimDouble(double v) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.3f", v);
  return buffer;
}

void PrintCandidates(RimeApi* rime,
                     RimeSessionId session,
                     const std::string& prefix,
                     int limit,
                     bool with_comment) {
  RIME_STRUCT(RimeContext, context);
  if (!rime->get_context(session, &context))
    return;
  printf("%s", prefix.c_str());
  if (context.composition.preedit)
    printf(" preedit=[%s]", context.composition.preedit);
  printf(" cands:");
  int count = std::min(context.menu.num_candidates, limit);
  for (int i = 0; i < count; ++i) {
    printf(" %d.%s", i + 1, context.menu.candidates[i].text);
    if (with_comment && context.menu.candidates[i].comment)
      printf("[%s]", context.menu.candidates[i].comment);
  }
  if (context.menu.num_candidates > count)
    printf(" ...(%d more)", context.menu.num_candidates - count);
  printf("\n");
  rime->free_context(&context);
}

bool IsLastPage(RimeApi* rime, RimeSessionId session) {
  RIME_STRUCT(RimeContext, context);
  bool last = true;
  if (rime->get_context(session, &context)) {
    last = context.menu.is_last_page;
    rime->free_context(&context);
  }
  return last;
}

}  // namespace

int main(int argc, char** argv) {
  Options opts;
  if (!ParseArgs(argc, argv, &opts))
    return 1;
  if (!PrepareUserDir(opts))
    return 1;

  RimeApi* rime = rime_get_api();

  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = opts.shared_data_dir.c_str();
  traits.user_data_dir = opts.user_data_dir.c_str();
  std::string prebuilt_dir = opts.shared_data_dir + "/build";
  traits.prebuilt_data_dir = prebuilt_dir.c_str();
  traits.distribution_name = "Rime Latency Sampler";
  traits.distribution_code_name = "rime-latency";
  traits.distribution_version = "1.0";
  traits.app_name = "rime.latency";
  std::string log_dir = opts.user_data_dir + "/log";
  traits.log_dir = log_dir.c_str();
  traits.min_log_level = opts.verbose ? 0 : 2;

  rime->setup(&traits);
  auto setup_started = Clock::now();
  rime->initialize(&traits);
  rime->start_maintenance(False);
  rime->join_maintenance_thread();
  double setup_ms =
      std::chrono::duration<double, std::milli>(Clock::now() - setup_started)
          .count();

  RimeSessionId session = rime->create_session();
  if (!session) {
    fprintf(stderr, "failed to create session\n");
    return 1;
  }
  if (!rime->select_schema(session, opts.schema.c_str())) {
    fprintf(stderr, "failed to select schema %s\n", opts.schema.c_str());
    return 1;
  }
  if (!opts.context.empty()) {
    rime->set_context_text(session, opts.context.c_str(), "");
  }

  std::vector<char> key_chars(opts.keys.begin(), opts.keys.end());
  std::vector<std::vector<double>> samples(key_chars.size());
  bool all_consumed = true;

  for (int round = 0; round < opts.warmup_rounds + opts.rounds; ++round) {
    bool measured = round >= opts.warmup_rounds;
    rime->clear_composition(session);
    for (size_t i = 0; i < key_chars.size(); ++i) {
      auto started = Clock::now();
      Bool handled = rime->process_key(session, KeyCodeOf(key_chars[i]), 0);
      auto elapsed =
          std::chrono::duration<double, std::milli>(Clock::now() - started)
              .count();
      if (!handled)
        all_consumed = false;
      if (measured)
        samples[i].push_back(elapsed);
      if (measured && opts.dump_each > 0) {
        char prefix[64];
        snprintf(prefix, sizeof(prefix), "key %2zu '%c' %7.3f ms |", i,
                 key_chars[i], elapsed);
        PrintCandidates(rime, session, prefix, opts.dump_each, false);
      }
    }
  }
  if (opts.dump_candidates) {
    PrintCandidates(rime, session, "final |", 10, true);
  }
  if (opts.all_pages) {
    const int kPageDown = 0xff56;
    for (int page = 0; page < 100; ++page) {
      char prefix[64];
      snprintf(prefix, sizeof(prefix), "page %d |", page);
      PrintCandidates(rime, session, prefix, 64, true);
      if (IsLastPage(rime, session))
        break;
      rime->process_key(session, kPageDown, 0);
    }
  }
  rime->clear_composition(session);

  std::vector<double> all;
  for (const auto& s : samples)
    all.insert(all.end(), s.begin(), s.end());

  printf("schema: %s\n", opts.schema.c_str());
  printf("keys: %s (%zu)\n", opts.keys.c_str(), key_chars.size());
  printf("context: %s\n",
         opts.context.empty() ? "(none)" : opts.context.c_str());
  printf("apply_to_sentence: %s, contextual_suggestions: %s\n",
         opts.apply_to_sentence ? "on" : "off",
         opts.contextual_suggestions ? "on" : "off");
  printf("setup+deploy: %s ms, rounds: %d (+%d warmup)\n",
         TrimDouble(setup_ms).c_str(), opts.rounds, opts.warmup_rounds);
  if (!all_consumed)
    printf("warning: some keys were not consumed\n");

  printf("\nper key position (ms):\n");
  printf("%4s %5s %8s %8s %8s %8s\n", "pos", "key", "median", "p90", "p99",
         "max");
  for (size_t i = 0; i < samples.size(); ++i) {
    printf("%4zu %5c %8s %8s %8s %8s\n", i, key_chars[i],
           TrimDouble(Percentile(samples[i], 0.5)).c_str(),
           TrimDouble(Percentile(samples[i], 0.9)).c_str(),
           TrimDouble(Percentile(samples[i], 0.99)).c_str(),
           TrimDouble(Percentile(samples[i], 1.0)).c_str());
  }

  std::sort(all.begin(), all.end());
  double sum = 0.0;
  for (double v : all)
    sum += v;
  size_t over_frame =
      std::count_if(all.begin(), all.end(), [](double v) { return v > 16.7; });
  size_t over_50 =
      std::count_if(all.begin(), all.end(), [](double v) { return v > 50.0; });
  printf(
      "\nsummary: n=%zu mean=%s p50=%s p90=%s p99=%s max=%s ms, "
      ">16.7ms=%zu (%.1f%%), >50ms=%zu\n",
      all.size(), TrimDouble(sum / std::max<size_t>(1, all.size())).c_str(),
      TrimDouble(Percentile(all, 0.5)).c_str(),
      TrimDouble(Percentile(all, 0.9)).c_str(),
      TrimDouble(Percentile(all, 0.99)).c_str(),
      TrimDouble(Percentile(all, 1.0)).c_str(), over_frame,
      all.empty() ? 0.0 : 100.0 * over_frame / all.size(), over_50);

  if (!opts.csv_path.empty()) {
    std::ofstream csv(opts.csv_path);
    csv << "round,pos,key,ms\n";
    for (size_t i = 0; i < samples.size(); ++i) {
      for (size_t r = 0; r < samples[i].size(); ++r) {
        csv << r << ',' << i << ',' << key_chars[i] << ','
            << TrimDouble(samples[i][r]) << '\n';
      }
    }
  }

  rime->destroy_session(session);
  rime->finalize();
  return 0;
}
