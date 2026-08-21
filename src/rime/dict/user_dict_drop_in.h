//
// Copyright RIME Developers
// Distributed under the BSD License
//
#ifndef RIME_USER_DICT_DROP_IN_H_
#define RIME_USER_DICT_DROP_IN_H_

#include <rime_api.h>
#include <rime/common.h>

namespace rime {

inline constexpr const char* kExtraDictsDirName = "extra_dicts";

RIME_DLL bool IsDictYamlFilename(const string& name);

// Non-recursive listing of *.dict.yaml, sorted by filename.
RIME_DLL vector<path> ListDictYamlFiles(const path& dir);
RIME_DLL vector<path> ListExtraDictFiles(const path& user_data_dir);

}  // namespace rime

#endif  // RIME_USER_DICT_DROP_IN_H_
