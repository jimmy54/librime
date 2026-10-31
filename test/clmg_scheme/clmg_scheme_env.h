#ifndef RIME_TEST_CLMG_SCHEME_ENV_H_
#define RIME_TEST_CLMG_SCHEME_ENV_H_

#include <string>

struct ClmgSchemeEnvironment {
  bool ready = false;
  std::string unavailable_reason;
  std::string shared_dir;
  std::string user_dir;
  std::string model_id;
  std::string schema;
  std::string control_schema;
  std::string cases_path;
};

ClmgSchemeEnvironment& GetClmgSchemeEnvironment();

#endif  // RIME_TEST_CLMG_SCHEME_ENV_H_
