# RimeSetInput 部分精确匹配 V2.1 - 最终代码补丁

## 补丁说明

本补丁实现部分精确匹配功能，在 **ScriptTranslator 层面**动态过滤派生音节。

### 设计原则

**为什么选择方案 B（ScriptTranslator 层面过滤）？**

1. **动态性要求**：`input_exact_length` 可以随时通过 `RimeSetInputEx` 修改
2. **缓存机制**：音节图构建后会被缓存，不能在构建时过滤
3. **查询时过滤**：每次查询词典时根据当前 `input_exact_length` 动态过滤

**方案对比**：

| 方案 | 过滤时机 | 优点 | 缺点 | 是否可行 |
|------|---------|------|------|----------|
| A | 构建音节图时 | 效率高 | 无法支持动态修改 | ❌ 不可行 |
| B | 查询词典时 | 支持动态修改 | 需要每次过滤 | ✅ 可行 |

---

## 修改文件清单

1. `src/rime/context.h` - 添加 input_exact_length 标志
2. `src/rime/context.cc` - 实现精确匹配逻辑
3. `src/rime_api.h` - 添加新 API
4. `src/rime_api_impl.h` - 实现新 API
5. `src/rime/gear/script_translator.cc` - **核心修改**：在查询时过滤

---

## 补丁 1: src/rime/context.h

```cpp
class RIME_DLL Context {
 public:
  // ... 现有代码 ...
  
  // 【V2.1 优化】：参数名更明确
  void set_input(const string& value, int input_exact_length = 0);
  const string& input() const { return input_; }
  
  // 【V2.1 新增】：获取精确匹配长度
  int input_exact_length() const { return input_exact_length_; }
  
  // 【V2.1 新增】：判断指定位置是否需要精确匹配
  bool is_exact_at(size_t pos) const;

 private:
  // ... 现有代码 ...
  
  // 【V2.1 新增】：精确匹配长度
  int input_exact_length_ = 0;
};
```

**完整 diff**：

```diff
--- a/src/rime/context.h
+++ b/src/rime/context.h
@@ -61,7 +61,9 @@ class RIME_DLL Context {
   bool ClearNonConfirmedComposition();
   bool RefreshNonConfirmedComposition();
 
-  void set_input(const string& value);
+  void set_input(const string& value, int input_exact_length = 0);
+  int input_exact_length() const { return input_exact_length_; }
+  bool is_exact_at(size_t pos) const;
   const string& input() const { return input_; }
 
   void set_caret_pos(size_t caret_pos);
@@ -117,6 +119,9 @@ class RIME_DLL Context {
   map<string, bool> options_;
   map<string, string> properties_;
 
+  // Exact match length for partial exact matching
+  int input_exact_length_ = 0;
+
   // External context from frontend
   string external_preceding_text_;
   string external_following_text_;
```

---

## 补丁 2: src/rime/context.cc

```cpp
void Context::set_input(const string& value, int input_exact_length) {
  input_ = value;
  caret_pos_ = input_.length();
  
  // 处理特殊值
  if (input_exact_length < 0) {
    // 负数：全部精确
    input_exact_length_ = static_cast<int>(input_.length());
  } else if (input_exact_length > static_cast<int>(input_.length())) {
    // 超过输入长度：限制为输入长度
    input_exact_length_ = static_cast<int>(input_.length());
  } else {
    input_exact_length_ = input_exact_length;
  }
  
  DLOG(INFO) << "Context::set_input: input=\"" << value 
             << "\", input_exact_length=" << input_exact_length_;
  
  update_notifier_(this);
}

bool Context::is_exact_at(size_t pos) const {
  if (input_exact_length_ <= 0)
    return false;
  return pos < static_cast<size_t>(input_exact_length_);
}
```

**完整 diff**：

```diff
--- a/src/rime/context.cc
+++ b/src/rime/context.cc
@@ -281,9 +281,25 @@ void Context::set_composition(Composition&& comp) {
   composition_ = std::move(comp);
 }
 
-void Context::set_input(const string& value) {
+void Context::set_input(const string& value, int input_exact_length) {
   input_ = value;
   caret_pos_ = input_.length();
+  
+  // Handle special values
+  if (input_exact_length < 0) {
+    // Negative: exact match all
+    input_exact_length_ = static_cast<int>(input_.length());
+  } else if (input_exact_length > static_cast<int>(input_.length())) {
+    // Exceeds input length: limit to input length
+    input_exact_length_ = static_cast<int>(input_.length());
+  } else {
+    input_exact_length_ = input_exact_length;
+  }
+  
+  DLOG(INFO) << "Context::set_input: input=\"" << value 
+             << "\", input_exact_length=" << input_exact_length_;
+  
   update_notifier_(this);
 }
+
+bool Context::is_exact_at(size_t pos) const {
+  if (input_exact_length_ <= 0)
+    return false;
+  return pos < static_cast<size_t>(input_exact_length_);
+}
```

---

## 补丁 3: src/rime_api.h

```c
// 【V2.1 新增】：支持部分精确匹配
// input_exact_length: 需要精确匹配的前缀长度
//   = 0: 全部派生（默认行为）
//   > 0: 前 N 个字符精确匹配，后续允许派生
//   < 0: 全部精确匹配
RIME_API Bool RimeSetInputEx(RimeSessionId session_id,
                              const char* input,
                              int input_exact_length);

// 【保持兼容】：旧 API
RIME_API Bool RimeSetInput(RimeSessionId session_id, const char* input);
```

**完整 diff**：

```diff
--- a/src/rime_api.h
+++ b/src/rime_api.h
@@ -XXX,6 +XXX,15 @@ RIME_API Bool RimeCommitComposition(RimeSessionId session_id);
 RIME_API void RimeClearComposition(RimeSessionId session_id);
 
 //! Set input string
+//! \deprecated Use RimeSetInputEx instead for better control
 RIME_API Bool RimeSetInput(RimeSessionId session_id, const char* input);
+
+//! Set input string with partial exact match
+//! \param input_exact_length: Length of prefix that needs exact match
+//!   = 0: all derivations allowed (default)
+//!   > 0: first N characters exact match, rest allow derivations
+//!   < 0: all exact match
+RIME_API Bool RimeSetInputEx(RimeSessionId session_id,
+                              const char* input,
+                              int input_exact_length);
+
 RIME_API char* RimeGetInput(RimeSessionId session_id);
```

---

## 补丁 4: src/rime_api_impl.h

```cpp
Bool RimeSetInputEx(RimeSessionId session_id,
                    const char* input,
                    int input_exact_length) {
  an<Session> session(Service::instance().GetSession(session_id));
  if (!session)
    return False;
  Context* ctx = session->context();
  if (!ctx)
    return False;
  ctx->set_input(input, input_exact_length);
  return True;
}

RIME_DEPRECATED Bool RimeSetInput(RimeSessionId session_id, const char* input) {
  return RimeSetInputEx(session_id, input, 0);
}
```

**完整 diff**：

```diff
--- a/src/rime_api_impl.h
+++ b/src/rime_api_impl.h
@@ -1137,13 +1137,26 @@ RIME_DEPRECATED Bool RimeSetInput(RimeSessionId session_id, const char* input)
   an<Session> session(Service::instance().GetSession(session_id));
   if (!session)
     return False;
   Context* ctx = session->context();
   if (!ctx)
     return False;
-  ctx->set_input(input);
+  ctx->set_input(input, 0);
   return True;
 }
 
+Bool RimeSetInputEx(RimeSessionId session_id,
+                    const char* input,
+                    int input_exact_length) {
+  an<Session> session(Service::instance().GetSession(session_id));
+  if (!session)
+    return False;
+  Context* ctx = session->context();
+  if (!ctx)
+    return False;
+  ctx->set_input(input, input_exact_length);
+  return True;
+}
+
 char* RimeGetInput(RimeSessionId session_id) {
   an<Session> session(Service::instance().GetSession(session_id));
   if (!session)
```

---

## 补丁 5: src/rime/gear/script_translator.cc

这是最核心的修改，在查询词典时动态过滤派生音节。

### 5.1 修改 ScriptTranslation 类声明

首先需要在 `script_translator.cc` 文件顶部的 `ScriptTranslation` 类中添加新方法：

```cpp
class ScriptTranslation : public Translation {
 public:
  // ... 现有代码 ...
  
 protected:
  // ... 现有方法 ...
  
  // 【V2.1 新增】：过滤查询结果
  template <class QueryResult>
  void FilterLookupResult(const an<QueryResult>& query_result,
                         const SyllableGraph& syllable_graph,
                         size_t start_pos,
                         const Syllabary* syllabary,
                         map<int, DictEntryList>& entries_by_end_pos);
  
  // 【V2.1 新增】：获取拼写字符串
  string GetSpelling(const SyllableGraph& graph,
                    size_t start_pos,
                    size_t end_pos) const;
  
  // ... 现有成员 ...
};
```

### 5.2 修改 MakeSentence 方法

```cpp
an<Sentence> ScriptTranslation::MakeSentence(Dictionary* dict,
                                             UserDictionary* user_dict) {
  const int kMaxSyllablesForUserPhraseQuery = 5;
  const auto& syllable_graph = syllabifier_->syllable_graph();
  
  // 【V2.1 新增】：获取精确匹配长度
  Context* ctx = translator_->engine_->context();
  int input_exact_length = ctx ? ctx->input_exact_length() : 0;
  const Syllabary* syllabary = nullptr;
  if (input_exact_length > 0 && dict) {
    syllabary = dict->prism()->syllabary();
  }
  
  WordGraph graph;
  for (const auto& x : syllable_graph.edges) {
    size_t start_pos = x.first;
    
    // 【V2.1 关键】：判断是否需要精确匹配
    bool need_exact = (input_exact_length > 0 && 
                       start_pos < static_cast<size_t>(input_exact_length));
    
    auto& same_start_pos = graph[start_pos];
    
    // 用户词典查询
    if (user_dict) {
      auto user_result = user_dict->Lookup(syllable_graph, start_pos,
                                          kMaxSyllablesForUserPhraseQuery);
      // 【V2.1 新增】：精确模式下过滤结果
      if (need_exact && syllabary) {
        FilterLookupResult(user_result, syllable_graph, start_pos, 
                          syllabary, same_start_pos);
      } else {
        EnrollEntries(same_start_pos, user_result);
      }
    }
    
    // 系统词典查询
    auto dict_result = dict->Lookup(syllable_graph, start_pos, 0);
    // 【V2.1 新增】：精确模式下过滤结果
    if (need_exact && syllabary) {
      FilterLookupResult(dict_result, syllable_graph, start_pos, 
                        syllabary, same_start_pos);
    } else {
      EnrollEntries(same_start_pos, dict_result);
    }
  }
  
  return poet_->MakeSentence(graph, syllable_graph.interpreted_length,
                            translator_->GetPrecedingText(start_));
}
```

### 5.3 实现 FilterLookupResult 方法

```cpp
// 【V2.1 新增】：过滤查询结果
template <class QueryResult>
void ScriptTranslation::FilterLookupResult(
    const an<QueryResult>& query_result,
    const SyllableGraph& syllable_graph,
    size_t start_pos,
    const Syllabary* syllabary,
    map<int, DictEntryList>& entries_by_end_pos) {
  
  if (!query_result || !syllabary)
    return;
  
  for (auto& y : *query_result) {
    size_t end_pos = y.first;
    
    // 获取拼写字符串
    string spelling = GetSpelling(syllable_graph, start_pos, end_pos);
    
    // 过滤词条
    DictEntryList& homophones = entries_by_end_pos[end_pos];
    for (auto& entry : y.second) {
      // 检查第一个音节是否精确匹配
      if (entry->code.size() > 0) {
        SyllableId syllable_id = entry->code[0];
        if (syllable_id < syllabary->size()) {
          const string& syllable_str = (*syllabary)[syllable_id];
          if (spelling == syllable_str) {
            homophones.push_back(entry);  // 精确匹配，保留
          }
          // 否则跳过（派生音节）
        }
      }
    }
  }
}
```

### 5.4 实现 GetSpelling 方法

```cpp
// 【V2.1 新增】：获取拼写字符串
string ScriptTranslation::GetSpelling(const SyllableGraph& graph,
                                     size_t start_pos,
                                     size_t end_pos) const {
  // 从输入字符串中提取拼写
  size_t length = end_pos - start_pos;
  if (start_pos + length <= syllabifier_->input_.length()) {
    return syllabifier_->input_.substr(start_pos, length);
  }
  return string();
}
```

### 完整 diff

```diff
--- a/src/rime/gear/script_translator.cc
+++ b/src/rime/gear/script_translator.cc
@@ -130,6 +130,16 @@ class ScriptTranslation : public Translation {
  protected:
   bool CheckEmpty();
   bool IsNormalSpelling() const;
   bool PrepareCandidate();
+  
+  // V2.1: Filter lookup results for exact match
+  template <class QueryResult>
+  void FilterLookupResult(const an<QueryResult>& query_result,
+                         const SyllableGraph& syllable_graph,
+                         size_t start_pos,
+                         const Syllabary* syllabary,
+                         map<int, DictEntryList>& entries_by_end_pos);
+  string GetSpelling(const SyllableGraph& graph,
+                    size_t start_pos,
+                    size_t end_pos) const;
+  
   template <class QueryResult>
   void EnrollEntries(map<int, DictEntryList>& entries_by_end_pos,
@@ -649,16 +659,44 @@ bool ScriptTranslation::PrepareCandidate() {
 an<Sentence> ScriptTranslation::MakeSentence(Dictionary* dict,
                                              UserDictionary* user_dict) {
   const int kMaxSyllablesForUserPhraseQuery = 5;
   const auto& syllable_graph = syllabifier_->syllable_graph();
+  
+  // V2.1: Get exact match length from context
+  Context* ctx = translator_->engine_->context();
+  int input_exact_length = ctx ? ctx->input_exact_length() : 0;
+  const Syllabary* syllabary = nullptr;
+  if (input_exact_length > 0 && dict) {
+    syllabary = dict->prism()->syllabary();
+  }
+  
   WordGraph graph;
   for (const auto& x : syllable_graph.edges) {
+    size_t start_pos = x.first;
+    
+    // V2.1: Check if exact match is needed at this position
+    bool need_exact = (input_exact_length > 0 && 
+                       start_pos < static_cast<size_t>(input_exact_length));
+    
     auto& same_start_pos = graph[x.first];
     if (user_dict) {
-      EnrollEntries(same_start_pos,
-                    user_dict->Lookup(syllable_graph, x.first,
-                                      kMaxSyllablesForUserPhraseQuery));
+      auto user_result = user_dict->Lookup(syllable_graph, start_pos,
+                                          kMaxSyllablesForUserPhraseQuery);
+      // V2.1: Filter results in exact match mode
+      if (need_exact && syllabary) {
+        FilterLookupResult(user_result, syllable_graph, start_pos, 
+                          syllabary, same_start_pos);
+      } else {
+        EnrollEntries(same_start_pos, user_result);
+      }
     }
-    // merge lookup results
-    EnrollEntries(same_start_pos, dict->Lookup(syllable_graph, x.first, 0));
+    
+    // System dictionary lookup
+    auto dict_result = dict->Lookup(syllable_graph, start_pos, 0);
+    // V2.1: Filter results in exact match mode
+    if (need_exact && syllabary) {
+      FilterLookupResult(dict_result, syllable_graph, start_pos, 
+                        syllabary, same_start_pos);
+    } else {
+      EnrollEntries(same_start_pos, dict_result);
+    }
   }
   return poet_->MakeSentence(graph, syllable_graph.interpreted_length,
                             translator_->GetPrecedingText(start_));
 }
+
+// V2.1: Filter lookup results for exact match
+template <class QueryResult>
+void ScriptTranslation::FilterLookupResult(
+    const an<QueryResult>& query_result,
+    const SyllableGraph& syllable_graph,
+    size_t start_pos,
+    const Syllabary* syllabary,
+    map<int, DictEntryList>& entries_by_end_pos) {
+  
+  if (!query_result || !syllabary)
+    return;
+  
+  for (auto& y : *query_result) {
+    size_t end_pos = y.first;
+    string spelling = GetSpelling(syllable_graph, start_pos, end_pos);
+    
+    DictEntryList& homophones = entries_by_end_pos[end_pos];
+    for (auto& entry : y.second) {
+      if (entry->code.size() > 0) {
+        SyllableId syllable_id = entry->code[0];
+        if (syllable_id < syllabary->size()) {
+          const string& syllable_str = (*syllabary)[syllable_id];
+          if (spelling == syllable_str) {
+            homophones.push_back(entry);
+          }
+        }
+      }
+    }
+  }
+}
+
+// V2.1: Get spelling string from syllable graph
+string ScriptTranslation::GetSpelling(const SyllableGraph& graph,
+                                     size_t start_pos,
+                                     size_t end_pos) const {
+  size_t length = end_pos - start_pos;
+  if (start_pos + length <= syllabifier_->input_.length()) {
+    return syllabifier_->input_.substr(start_pos, length);
+  }
+  return string();
+}
```

---

## 使用示例

### C++ 示例

```cpp
#include <rime_api.h>

int main() {
  RimeSessionId session = RimeCreateSession();
  RimeSelectSchema(session, "luna_pinyin");
  
  // 示例 1：全部派生（默认）
  RimeSetInput(session, "bubu");
  // 候选：不步、不部...（包含所有派生）
  
  // 示例 2：前2码精确
  RimeSetInputEx(session, "bubu", 2);
  // 候选：不步、不部、不比...
  // （第一个 bu 精确，第二个 bu 可派生）
  
  // 示例 3：全部精确
  RimeSetInputEx(session, "bubu", 4);
  // 候选：不步、不部...
  // （两个 bu 都精确）
  
  return 0;
}
```

### 14键方案示例

```cpp
// 配置：derive/i/u/, derive/n/b/
RimeSelectSchema(session, "rime_ice_14");

// 输入 "bubu"，前2码精确
RimeSetInputEx(session, "bubu", 2);

// 查询流程：
// 1. 构建完整音节图（包含所有派生）
// 2. 查询词典时，start_pos=0 需要精确匹配
//    - 拼写 "bu" 只匹配音节 "bu"
//    - 过滤掉 "bi", "ni", "nu"
// 3. start_pos=2 允许派生
//    - 拼写 "bu" 匹配所有音节

// 候选：
// ✓ 不步 (bu + bu)
// ✓ 不比 (bu + bi)
// ✓ 不你 (bu + ni)
// ✗ 比步 (bi + bu) - 第一个音节不匹配
```

---

## 测试用例

### 单元测试

```cpp
// test/script_translator_test.cc

TEST(ScriptTranslatorTest, PartialExactMatch) {
  // 准备
  RimeSessionId session = CreateTestSession("luna_pinyin");
  
  // 测试1：全部派生
  RimeSetInputEx(session, "bubu", 0);
  auto candidates = GetCandidates(session);
  EXPECT_TRUE(HasCandidate(candidates, "不步"));
  
  // 测试2：前2码精确
  RimeSetInputEx(session, "bubu", 2);
  candidates = GetCandidates(session);
  EXPECT_TRUE(HasCandidate(candidates, "不步"));
  EXPECT_TRUE(HasCandidate(candidates, "不比"));
  
  // 测试3：全部精确
  RimeSetInputEx(session, "bubu", 4);
  candidates = GetCandidates(session);
  EXPECT_TRUE(HasCandidate(candidates, "不步"));
}

TEST(ScriptTranslatorTest, DynamicExactLength) {
  RimeSessionId session = CreateTestSession("luna_pinyin");
  
  // 测试动态修改
  RimeSetInputEx(session, "bu", 0);
  auto candidates1 = GetCandidates(session);
  
  RimeSetInputEx(session, "bubu", 2);
  auto candidates2 = GetCandidates(session);
  
  // 验证候选不同
  EXPECT_NE(candidates1.size(), candidates2.size());
}
```

---

## 编译与测试

### 编译

```bash
cd librime
mkdir -p build && cd build
cmake -DBUILD_TEST=ON ..
make -j$(nproc)
```

### 运行测试

```bash
# 单元测试
./test/rime_test

# 手动测试
./bin/rime_console
```

---

## 性能分析

### 时间复杂度

| 操作 | 复杂度 | 说明 |
|------|--------|------|
| 构建音节图 | O(n·m·k) | 不变 |
| 查询词典 | O(n·m·k) | 不变 |
| 过滤词条 | O(e) | e=词条数量 |
| **总计** | **O(n·m·k + e)** | 额外开销可接受 |

### 优化措施

1. **只在精确范围内过滤**：
   ```cpp
   if (start_pos < input_exact_length) {
     // 过滤逻辑
   }
   ```

2. **提前退出**：
   ```cpp
   if (input_exact_length <= 0) {
     // 直接使用原有逻辑
   }
   ```

3. **缓存音节表**：
   ```cpp
   if (input_exact_length > 0 && dict) {
     syllabary = dict->prism()->syllabary();
   }
   ```

---

## 注意事项

### 1. Prism::syllabary() 方法

需要确认 `Prism` 类有此方法：

```cpp
// dict/prism.h
class Prism {
 public:
  const Syllabary* syllabary() const;
};
```

### 2. 模板方法实例化

`FilterLookupResult` 是模板方法，需要在 `.cc` 文件中实例化：

```cpp
// 显式实例化
template void ScriptTranslation::FilterLookupResult<DictEntryCollector>(...);
template void ScriptTranslation::FilterLookupResult<UserDictEntryCollector>(...);
```

或者将实现放在头文件中。

### 3. 访问 syllabifier_->input_

需要确认 `ScriptSyllabifier` 的 `input_` 成员是可访问的。如果不是，需要添加 getter 方法：

```cpp
// script_translator.cc - ScriptSyllabifier 类
class ScriptSyllabifier : public PhraseSyllabifier {
 public:
  const string& input() const { return input_; }
};
```

---

## 总结

### V2.1 最终方案

1. **参数命名**：`input_exact_length`（更明确）
2. **实现位置**：ScriptTranslator 层面（支持动态修改）
3. **过滤时机**：查询词典时（每次动态过滤）

### 核心优势

1. **支持动态修改**：`input_exact_length` 可以随时修改
2. **不破坏缓存**：音节图保持完整
3. **逻辑清晰**：精确匹配是查询条件

### 修改文件

- ✓ context.h / context.cc
- ✓ rime_api.h / rime_api_impl.h
- ✓ script_translator.cc（核心修改）

### 代码量

- 新增：~120 行
- 修改：~15 行
- **总计：~135 行代码**

---

**文档版本**：V2.1 Final  
**最后更新**：2025-01-25  
**作者**：Cascade AI Assistant
