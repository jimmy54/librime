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
inline constexpr const char* kPacksDirName = "packs";

RIME_DLL bool IsDictYamlFilename(const string& name);
RIME_DLL string DictYamlStem(const string& filename);

// Non-recursive listing of *.dict.yaml, sorted by filename.
RIME_DLL vector<path> ListDictYamlFiles(const path& dir);
RIME_DLL vector<path> ListExtraDictFiles(const path& user_data_dir);
RIME_DLL vector<string> ListAutoPackNames(const path& user_data_dir);

RIME_DLL string AutoPackTableId(const string& dict_name,
                                const string& pack_name);
RIME_DLL bool IsAutoPackTableId(const string& dict_name,
                                const string& table_id);
RIME_DLL string AutoPackStem(const string& dict_name, const string& table_id);

// Merge packs/*.dict.yaml into `packs` using namespaced table ids.
// Explicit pack names (and their stems) are kept and not duplicated.
RIME_DLL void AppendAutoPacks(vector<string>* packs,
                              const string& dict_name,
                              const path& user_data_dir);

}  // namespace rime

#endif  // RIME_USER_DICT_DROP_IN_H_
