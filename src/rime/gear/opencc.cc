//
// Copyright RIME Developers
// Distributed under the BSD License
//
#include <sstream>
#include <rime/common.h>
#include <rime/gear/opencc.h>
#include <opencc/Config.hpp>  // Place OpenCC #includes here to avoid VS2015 compilation errors
#include <opencc/Converter.hpp>
#include <opencc/Conversion.hpp>
#include <opencc/ConversionChain.hpp>
#include <opencc/Dict.hpp>
#include <opencc/DictEntry.hpp>

namespace rime {

Opencc::Opencc(const path& config_path)
    : initialized_(false), config_path_(config_path) {}

void Opencc::Initialize() {
  if (initialized_.load(std::memory_order_acquire))
    return;

  std::lock_guard<std::mutex> lock(init_mutex_);

  // 再次检查，防止重复初始化
  if (initialized_.load(std::memory_order_acquire))
    return;

  opencc::Config config;
  try {
    // opencc accepts file path encoded in UTF-8.
    converter_ = config.NewFromFile(config_path_.u8string());

    if (!converter_) {
      LOG(ERROR) << "opencc converter is null after initialization";
      converter_.reset();
      dict_.reset();
      return;
    }

    auto chain = converter_->GetConversionChain();
    if (!chain) {
      LOG(ERROR) << "opencc conversion chain is null after initialization";
      converter_.reset();
      dict_.reset();
      return;
    }

    const list<opencc::ConversionPtr> conversions = chain->GetConversions();

    if (conversions.empty()) {
      LOG(ERROR) << "opencc conversions chain is empty";
      converter_.reset();
      dict_.reset();
      return;
    }

    dict_ = conversions.front()->GetDict();

    initialized_.store(true, std::memory_order_release);

  } catch (const std::exception& e) {
    LOG(ERROR) << "opencc initialization failed: " << e.what()
               << ", path: " << config_path_;
    converter_.reset();
    dict_.reset();
  } catch (...) {
    LOG(ERROR) << "opencc config not found: " << config_path_;
    converter_.reset();
    dict_.reset();
  }
}

bool Opencc::ConvertWord(const string& text, vector<string>* forms) {
  Initialize();

  if (!initialized_.load(std::memory_order_acquire) || converter_ == nullptr) {
    LOG(WARNING) << "opencc not properly initialized, skipping conversion";
    return false;
  }

  auto chain = converter_->GetConversionChain();
  if (!chain) {
    LOG(ERROR) << "opencc conversion chain is null";
    return false;
  }

  const list<opencc::ConversionPtr> conversions = chain->GetConversions();
  if (conversions.empty()) {
    LOG(WARNING) << "opencc conversions list is empty";
    return false;
  }
  vector<string> original_words{text};
  bool matched = false;
  for (auto conversion : conversions) {
    opencc::DictPtr dict = conversion->GetDict();
    if (dict == nullptr) {
      return false;
    }
    set<string> word_set;
    vector<string> converted_words;
    for (const auto& original_word : original_words) {
      opencc::Optional<const opencc::DictEntry*> item =
          dict->Match(original_word);
      if (item.IsNull()) {
        // There is no exact match, but still need to convert partially
        // matched in a chain conversion. Here apply default (max. seg.)
        // match to get the most probable conversion result
        std::ostringstream buffer;
        for (const char* wstr = original_word.c_str(); *wstr != '\0';) {
          opencc::Optional<const opencc::DictEntry*> matched =
              dict->MatchPrefix(wstr);
          size_t matched_length;
          if (matched.IsNull()) {
            matched_length = opencc::UTF8Util::NextCharLength(wstr);
            buffer << opencc::UTF8Util::FromSubstr(wstr, matched_length);
          } else {
            matched_length = matched.Get()->KeyLength();
            buffer << matched.Get()->GetDefault();
          }
          wstr += matched_length;
        }
        const string& converted_word = buffer.str();
        // Even if current dictionary doesn't convert the word
        // (converted_word == original_word), we still need to keep it for
        // subsequent dicts in the chain. e.g. s2t.json expands 里 to 里 and
        // 裏, then t2tw.json passes 里 as-is and converts 裏 to 裡.
        if (word_set.insert(converted_word).second) {
          converted_words.push_back(converted_word);
        }
        continue;
      }
      matched = true;
      const opencc::DictEntry* entry = item.Get();
      for (const auto& converted_word : entry->Values()) {
        if (word_set.insert(converted_word).second) {
          converted_words.push_back(converted_word);
        }
      }
    }
    original_words.swap(converted_words);
  }
  if (!matched) {
    // No dictionary contains the word
    return false;
  }
  *forms = std::move(original_words);
  return forms->size() > 0;
}

bool Opencc::RandomConvertText(const string& text, string* simplified) {
  Initialize();

  if (!initialized_.load(std::memory_order_acquire) || converter_ == nullptr ||
      dict_ == nullptr) {
    LOG(WARNING) << "opencc not properly initialized, skipping conversion";
    return false;
  }

  auto chain = converter_->GetConversionChain();
  if (!chain) {
    LOG(ERROR) << "opencc conversion chain is null";
    return false;
  }

  const list<opencc::ConversionPtr> conversions = chain->GetConversions();
  if (conversions.empty()) {
    LOG(WARNING) << "opencc conversions list is empty";
    return false;
  }
  const char* phrase = text.c_str();
  for (auto conversion : conversions) {
    opencc::DictPtr dict = conversion->GetDict();
    if (dict == nullptr) {
      return false;
    }
    std::ostringstream buffer;
    for (const char* pstr = phrase; *pstr != '\0';) {
      opencc::Optional<const opencc::DictEntry*> matched =
          dict->MatchPrefix(pstr);
      size_t matched_length;
      if (matched.IsNull()) {
        matched_length = opencc::UTF8Util::NextCharLength(pstr);
        buffer << opencc::UTF8Util::FromSubstr(pstr, matched_length);
      } else {
        matched_length = matched.Get()->KeyLength();
        size_t num_values = matched.Get()->NumValues();
        if (num_values == 0) {
          LOG(WARNING) << "matched entry has no values";
          matched_length = opencc::UTF8Util::NextCharLength(pstr);
          buffer << opencc::UTF8Util::FromSubstr(pstr, matched_length);
        } else {
          size_t i = rand() % num_values;
          buffer << matched.Get()->Values().at(i);
        }
      }
      pstr += matched_length;
    }
    *simplified = buffer.str();
    phrase = simplified->c_str();
  }
  return *simplified != text;
}

bool Opencc::ConvertText(const string& text, string* simplified) {
  Initialize();

  if (!initialized_.load(std::memory_order_acquire) || converter_ == nullptr) {
    LOG(WARNING) << "opencc not properly initialized, skipping conversion";
    return false;
  }
  *simplified = converter_->Convert(text);
  return *simplified != text;
}

}  // namespace rime
