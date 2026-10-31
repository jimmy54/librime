//
// Copyright RIME Developers
// Distributed under the BSD License
//
// Runs the CLMG integration tests against a real Rime scheme (default
// rime_ice_26). Everything is env-gated so a plain `rime_test` run stays
// hermetic:
//
//   RIME_CLMG_SHARED_DIR=/path/SharedSupport \
//   RIME_CLMG_USER_DIR=/path/user \
//   RIME_CLMG_MODEL=/path/clmg.bin \
//   rime_clmg_scheme_test
//
// The user dir is copied to a temp workspace before deployment; the source
// directories are never modified.
//

#include <gtest/gtest.h>
#include <rime_api.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

#include "clmg_scheme_env.h"

namespace fs = std::filesystem;

namespace {

#ifndef RIME_CLMG_CASES_PATH
#define RIME_CLMG_CASES_PATH "tools/eval/cases/test.tsv"
#endif

std::string GetEnv(const char* name) {
  const char* value = std::getenv(name);
  return value ? value : "";
}

std::string ReadText(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

bool WriteText(const fs::path& file, const std::string& text) {
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!out)
    return false;
  out << text;
  return out.good();
}

std::string StripBinSuffix(std::string file) {
  constexpr const char* kSuffix = ".bin";
  const size_t n = std::char_traits<char>::length(kSuffix);
  if (file.size() > n && file.compare(file.size() - n, n, kSuffix) == 0)
    file.resize(file.size() - n);
  return file;
}

std::string MakePatch(const ClmgSchemeEnvironment& env, double alpha) {
  std::string patch = "patch:\n";
  patch += "  \"translator/contextual_suggestions\": true\n";
  patch += "  \"translator/max_homophones\": 32\n";
  patch += "  \"translator/max_homographs\": 32\n";
  patch += "  \"translator/max_sentences\": 0\n";
  patch += "  \"grammar/apply_to_sentence\": false\n";
  patch += "  \"menu/page_size\": 32\n";
  patch += "  \"grammar/component\": clmg\n";
  patch += "  \"clmg/model\": \"" + env.model_id + "\"\n";
  patch += "  \"clmg/alpha\": " + std::to_string(alpha) + "\n";
  patch += "  \"clmg/context_length\": 32\n";
  patch += "  \"clmg/cache_size\": 8\n";
  return patch;
}

bool PrepareWorkspace(ClmgSchemeEnvironment* env) {
  env->shared_dir = GetEnv("RIME_CLMG_SHARED_DIR");
  const std::string user_dir = GetEnv("RIME_CLMG_USER_DIR");
  const std::string model = GetEnv("RIME_CLMG_MODEL");
  env->schema = GetEnv("RIME_CLMG_SCHEMA");
  if (env->schema.empty())
    env->schema = "rime_ice_26";
  env->control_schema = env->schema + "_clmg0";
  env->cases_path = GetEnv("RIME_CLMG_CASES");
  if (env->cases_path.empty())
    env->cases_path = RIME_CLMG_CASES_PATH;

  if (env->shared_dir.empty() || user_dir.empty() || model.empty()) {
    env->unavailable_reason =
        "set RIME_CLMG_SHARED_DIR, RIME_CLMG_USER_DIR and RIME_CLMG_MODEL "
        "to run the scheme integration test";
    return false;
  }
  if (!fs::is_directory(env->shared_dir) || !fs::is_directory(user_dir) ||
      !fs::is_regular_file(model)) {
    env->unavailable_reason =
        "RIME_CLMG_SHARED_DIR, RIME_CLMG_USER_DIR or RIME_CLMG_MODEL does "
        "not exist";
    return false;
  }
  env->model_id = StripBinSuffix(fs::absolute(model).string());

  std::error_code ec;
  const fs::path workspace =
      fs::path(testing::TempDir()) /
      ("rime_clmg_scheme_" + std::to_string(::getpid()));
  const fs::path work_user = workspace / "user";
  fs::remove_all(workspace, ec);
  fs::create_directories(workspace, ec);
  fs::copy(user_dir, work_user,
           fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
  if (ec) {
    env->unavailable_reason = "cannot copy user dir: " + ec.message();
    return false;
  }
  env->user_dir = work_user.string();

  const fs::path schema_source =
      fs::exists(work_user / (env->schema + ".schema.yaml"))
          ? work_user / (env->schema + ".schema.yaml")
          : fs::path(env->shared_dir) / (env->schema + ".schema.yaml");
  std::string schema_yaml = ReadText(schema_source);
  const std::string schema_id = "schema_id: " + env->schema;
  const auto id_pos = schema_yaml.find(schema_id);
  if (id_pos == std::string::npos) {
    env->unavailable_reason = "cannot find schema_id in " + schema_source.string();
    return false;
  }
  schema_yaml.replace(id_pos, schema_id.size(),
                      "schema_id: " + env->control_schema);
  if (!WriteText(work_user / (env->control_schema + ".schema.yaml"),
                 schema_yaml)) {
    env->unavailable_reason = "cannot write control schema";
    return false;
  }

  std::string defaults = "patch:\n  schema_list:\n";
  defaults += "    - schema: " + env->schema + "\n";
  defaults += "    - schema: " + env->control_schema + "\n";
  if (!WriteText(work_user / "default.custom.yaml", defaults) ||
      !WriteText(work_user / (env->schema + ".custom.yaml"),
                 MakePatch(*env, 0.25)) ||
      !WriteText(work_user / (env->control_schema + ".custom.yaml"),
                 MakePatch(*env, 0.0))) {
    env->unavailable_reason = "cannot write deploy patches";
    return false;
  }
  fs::create_directories(work_user / "log", ec);
  return true;
}

}  // namespace

ClmgSchemeEnvironment& GetClmgSchemeEnvironment() {
  static ClmgSchemeEnvironment env;
  return env;
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  ClmgSchemeEnvironment& env = GetClmgSchemeEnvironment();
  if (!PrepareWorkspace(&env)) {
    return RUN_ALL_TESTS();
  }

  RimeApi* rime = rime_get_api();
  if (!rime) {
    env.unavailable_reason = "rime_get_api() returned null";
    return RUN_ALL_TESTS();
  }

  const std::string prebuilt_dir = env.shared_dir + "/build";
  const std::string log_dir = env.user_dir + "/log";
  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = env.shared_dir.c_str();
  traits.user_data_dir = env.user_dir.c_str();
  traits.prebuilt_data_dir = prebuilt_dir.c_str();
  traits.distribution_name = "Rime CLMG Scheme Test";
  traits.distribution_code_name = "rime-clmg-scheme-test";
  traits.distribution_version = "1.0";
  traits.app_name = "rime.clmg.scheme";
  traits.log_dir = log_dir.c_str();
  traits.min_log_level = 2;

  rime->setup(&traits);
  rime->initialize(&traits);
  rime->start_maintenance(False);
  rime->join_maintenance_thread();
  env.ready = true;

  const int result = RUN_ALL_TESTS();
  env.ready = false;
  rime->finalize();
  return result;
}
