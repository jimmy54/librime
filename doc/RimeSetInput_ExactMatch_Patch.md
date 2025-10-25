# RimeSetInput 精确匹配模式 - 代码补丁

## 补丁说明

本补丁为 `RimeSetInput` API 添加精确匹配模式，解决拼写运算派生导致的候选混乱问题。

## 修改文件清单

1. `src/rime/context.h` - 添加精确匹配标志
2. `src/rime/context.cc` - 实现精确匹配逻辑
3. `src/rime_api.h` - 添加新 API
4. `src/rime_api_impl.h` - 实现新 API
5. `src/rime/gear/table_translator.cc` - 应用精确匹配过滤

---

## 补丁 1: src/rime/context.h

```cpp
// 在 Context 类中添加以下内容

class RIME_DLL Context {
 public:
  // ... 现有代码 ...
  
  // 【修改】：set_input 添加 exact_match 参数
  void set_input(const string& value, bool exact_match = false);
  const string& input() const { return input_; }
  
  // 【新增】：查询是否为精确匹配模式
  bool is_exact_match() const { return exact_match_; }

 private:
  // ... 现有代码 ...
  
  // 【新增】：精确匹配标志
  bool exact_match_ = false;
};
```

**完整修改**：

```diff
--- a/src/rime/context.h
+++ b/src/rime/context.h
@@ -61,7 +61,8 @@ class RIME_DLL Context {
   bool ClearNonConfirmedComposition();
   bool RefreshNonConfirmedComposition();
 
-  void set_input(const string& value);
+  void set_input(const string& value, bool exact_match = false);
+  bool is_exact_match() const { return exact_match_; }
   const string& input() const { return input_; }
 
   void set_caret_pos(size_t caret_pos);
@@ -117,6 +118,9 @@ class RIME_DLL Context {
   map<string, bool> options_;
   map<string, string> properties_;
 
+  // Exact match mode flag
+  bool exact_match_ = false;
+
   // External context from frontend
   string external_preceding_text_;
   string external_following_text_;
```

---

## 补丁 2: src/rime/context.cc

```cpp
// 修改 set_input 实现

void Context::set_input(const string& value, bool exact_match) {
  input_ = value;
  caret_pos_ = input_.length();
  exact_match_ = exact_match;  // 【新增】：设置精确匹配标志
  update_notifier_(this);
}
```

**完整修改**：

```diff
--- a/src/rime/context.cc
+++ b/src/rime/context.cc
@@ -281,9 +281,10 @@ void Context::set_composition(Composition&& comp) {
   composition_ = std::move(comp);
 }
 
-void Context::set_input(const string& value) {
+void Context::set_input(const string& value, bool exact_match) {
   input_ = value;
   caret_pos_ = input_.length();
+  exact_match_ = exact_match;
   update_notifier_(this);
 }
```

---

## 补丁 3: src/rime_api.h

```c
// 在 rime_api.h 中添加新 API

// 【新增】：支持精确匹配的输入设置
// exact_match: True 表示精确匹配，禁用拼写运算的派生逻辑
//              False 表示普通模式，允许派生（默认行为）
RIME_API Bool RimeSetInputEx(RimeSessionId session_id, 
                              const char* input,
                              Bool exact_match);
```

**完整修改**：

```diff
--- a/src/rime_api.h
+++ b/src/rime_api.h
@@ -XXX,6 +XXX,13 @@ RIME_API Bool RimeCommitComposition(RimeSessionId session_id);
 RIME_API void RimeClearComposition(RimeSessionId session_id);
 
 //! Set input string
+//! \deprecated Use RimeSetInputEx instead for better control
 RIME_API Bool RimeSetInput(RimeSessionId session_id, const char* input);
+
+//! Set input string with exact match option
+//! \param exact_match: True to disable spelling algebra derivations
+RIME_API Bool RimeSetInputEx(RimeSessionId session_id,
+                              const char* input,
+                              Bool exact_match);
+
 RIME_API char* RimeGetInput(RimeSessionId session_id);
```

---

## 补丁 4: src/rime_api_impl.h

```cpp
// 实现新 API

// 【新增】：RimeSetInputEx 实现
Bool RimeSetInputEx(RimeSessionId session_id, 
                    const char* input,
                    Bool exact_match) {
  an<Session> session(Service::instance().GetSession(session_id));
  if (!session)
    return False;
  Context* ctx = session->context();
  if (!ctx)
    return False;
  ctx->set_input(input, exact_match == True);
  return True;
}

// 【修改】：RimeSetInput 调用 RimeSetInputEx
RIME_DEPRECATED Bool RimeSetInput(RimeSessionId session_id, const char* input) {
  return RimeSetInputEx(session_id, input, False);
}
```

**完整修改**：

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
+  ctx->set_input(input, false);
   return True;
 }
 
+Bool RimeSetInputEx(RimeSessionId session_id,
+                    const char* input,
+                    Bool exact_match) {
+  an<Session> session(Service::instance().GetSession(session_id));
+  if (!session)
+    return False;
+  Context* ctx = session->context();
+  if (!ctx)
+    return False;
+  ctx->set_input(input, exact_match == True);
+  return True;
+}
+
 char* RimeGetInput(RimeSessionId session_id) {
   an<Session> session(Service::instance().GetSession(session_id));
   if (!session)
```

---

## 补丁 5: src/rime/gear/table_translator.cc

这是最关键的修改，需要在查询 Prism 时过滤派生音节。

### 方法一：通过拼写类型过滤（简单但不完美）

```cpp
// 在 Query 方法中，查询 Prism 后添加过滤逻辑

if (dict_ && dict_->loaded()) {
  vector<Prism::Match> matches;
  dict_->prism()->CommonPrefixSearch(input.substr(start_pos), &matches);
  
  if (matches.empty())
    continue;
    
  for (const auto& m : boost::adaptors::reverse(matches)) {
    if (m.length == 0)
      continue;
      
    size_t consumed_length =
        consume_trailing_delimiters(m.length, active_input, delimiters_);
    size_t end_pos = start_pos + consumed_length;
    
    auto& homographs = same_start_pos[end_pos];
    if (homographs.size() >= max_homographs_)
      continue;
      
    DictEntryIterator iter;
    
    // 【新增】：精确匹配模式下的特殊处理
    if (ctx->is_exact_match()) {
      // 获取当前拼写字符串
      string spelling_str = active_input.substr(0, m.length);
      
      // 查询该拼写对应的所有音节
      auto spelling_accessor = dict_->prism()->QuerySpelling(m.value);
      
      // 收集精确匹配的音节ID
      set<SyllableId> exact_syllables;
      while (!spelling_accessor.exhausted()) {
        auto props = spelling_accessor.properties();
        
        // 只接受原始提示为当前拼写的音节
        // tips 字段存储了原始拼写
        if (props.tips.empty() || props.tips == spelling_str) {
          exact_syllables.insert(spelling_accessor.syllable_id());
        }
        
        spelling_accessor.Next();
      }
      
      // 使用精确音节集合查询词条
      if (!exact_syllables.empty()) {
        dict_->LookupWords(&iter, active_input.substr(0, m.length), 
                          false, 0, &blacklist(), &exact_syllables);
      }
    } else {
      // 普通模式：使用所有音节
      dict_->LookupWords(&iter, active_input.substr(0, m.length), 
                        false, 0, &blacklist());
    }
    
    // ... 后续处理保持不变 ...
  }
}
```

### 方法二：通过音节表比较（更准确）

首先需要获取音节表的访问权限：

```cpp
// 在 TableTranslator 类中添加辅助方法

class TableTranslator : public Translator {
 protected:
  // 【新增】：获取音节字符串
  string GetSyllableString(SyllableId syllable_id) const {
    if (!dict_ || !dict_->loaded())
      return string();
    
    // 从词典中获取音节表
    const Syllabary* syllabary = dict_->syllabary();
    if (!syllabary || syllable_id >= syllabary->size())
      return string();
    
    return (*syllabary)[syllable_id];
  }
};
```

然后在查询时使用：

```cpp
if (ctx->is_exact_match()) {
  string spelling_str = active_input.substr(0, m.length);
  auto spelling_accessor = dict_->prism()->QuerySpelling(m.value);
  
  set<SyllableId> exact_syllables;
  while (!spelling_accessor.exhausted()) {
    SyllableId syllable_id = spelling_accessor.syllable_id();
    string syllable_str = GetSyllableString(syllable_id);
    
    // 拼写字符串必须等于音节字符串
    if (spelling_str == syllable_str) {
      exact_syllables.insert(syllable_id);
    }
    
    spelling_accessor.Next();
  }
  
  if (!exact_syllables.empty()) {
    dict_->LookupWords(&iter, active_input.substr(0, m.length),
                      false, 0, &blacklist(), &exact_syllables);
  }
}
```

### 完整补丁（方法二）

```diff
--- a/src/rime/gear/table_translator.cc
+++ b/src/rime/gear/table_translator.cc
@@ -640,6 +640,8 @@ an<Translation> TableTranslator::Query(const string& input,
       }
     }
     if (dict_ && dict_->loaded()) {
+      Context* ctx = engine_->context();
+      
       vector<Prism::Match> matches;
       dict_->prism()->CommonPrefixSearch(input.substr(start_pos), &matches);
       if (matches.empty())
@@ -653,7 +655,38 @@ an<Translation> TableTranslator::Query(const string& input,
         auto& homographs = same_start_pos[end_pos];
         if (homographs.size() >= max_homographs_)
           continue;
+        
         DictEntryIterator iter;
+        
+        // Exact match mode: filter derived syllables
+        if (ctx && ctx->is_exact_match()) {
+          string spelling_str = active_input.substr(0, m.length);
+          auto spelling_accessor = dict_->prism()->QuerySpelling(m.value);
+          
+          // Collect syllables that exactly match the spelling
+          set<SyllableId> exact_syllables;
+          const Syllabary* syllabary = dict_->syllabary();
+          
+          while (!spelling_accessor.exhausted()) {
+            SyllableId syllable_id = spelling_accessor.syllable_id();
+            
+            // Check if syllable string equals spelling string
+            if (syllabary && syllable_id < syllabary->size()) {
+              const string& syllable_str = (*syllabary)[syllable_id];
+              if (spelling_str == syllable_str) {
+                exact_syllables.insert(syllable_id);
+              }
+            }
+            
+            spelling_accessor.Next();
+          }
+          
+          // Lookup with exact syllables only
+          if (!exact_syllables.empty()) {
+            dict_->LookupWordsWithSyllables(&iter, active_input.substr(0, m.length),
+                                           false, 0, &blacklist(), exact_syllables);
+          }
+        } else {
         dict_->LookupWords(&iter, active_input.substr(0, m.length), false, 0,
                            &blacklist());
+        }
+        
         if (filter_by_charset) {
           iter.AddFilter(CharsetFilter::FilterDictEntry);
```

**注意**：上述代码假设 `Dictionary` 类有 `LookupWordsWithSyllables` 方法。如果没有，需要修改 `LookupWords` 方法以支持音节过滤。

### 方法三：修改 Dictionary::LookupWords（推荐）

如果 `LookupWords` 不支持音节过滤，可以在查询后过滤结果：

```cpp
if (ctx && ctx->is_exact_match()) {
  string spelling_str = active_input.substr(0, m.length);
  auto spelling_accessor = dict_->prism()->QuerySpelling(m.value);
  
  // Collect exact syllables
  set<SyllableId> exact_syllables;
  const Syllabary* syllabary = dict_->syllabary();
  
  while (!spelling_accessor.exhausted()) {
    SyllableId syllable_id = spelling_accessor.syllable_id();
    if (syllabary && syllable_id < syllabary->size()) {
      const string& syllable_str = (*syllabary)[syllable_id];
      if (spelling_str == syllable_str) {
        exact_syllables.insert(syllable_id);
      }
    }
    spelling_accessor.Next();
  }
  
  // Normal lookup
  dict_->LookupWords(&iter, active_input.substr(0, m.length), 
                    false, 0, &blacklist());
  
  // Filter by exact syllables
  if (!exact_syllables.empty()) {
    iter.AddFilter([exact_syllables](const DictEntry& entry) {
      return exact_syllables.count(entry.syllable_id) > 0;
    });
  }
}
```

---

## 使用示例

### C++ 代码

```cpp
#include <rime_api.h>

// 创建会话
RimeSessionId session = RimeCreateSession();

// 普通模式（允许派生）
RimeSetInput(session, "bu");
// 或
RimeSetInputEx(session, "bu", False);
// 候选：bi, bu, ni, nu（根据 derive 规则）

// 精确模式（禁用派生）
RimeSetInputEx(session, "bi", True);
// 候选：只有 bi 的词条
```

### JavaScript Filter 示例

```javascript
// 在 RIME 的 JavaScript 插件中使用

function filter(input, env) {
  let candidates = [];
  
  for (let cand of input.iter()) {
    candidates.push(cand);
  }
  
  // 用户选择了某个候选
  if (candidates.length > 0) {
    let selected = candidates[0];
    
    // 使用精确匹配重新设置输入
    rime.set_input_ex(selected.preedit, true);
  }
  
  return input;
}
```

---

## 测试用例

### 测试 1：基本功能

```python
# 配置：derive/i/u/, derive/n/b/

# 测试 1.1：普通模式
input = "bu"
exact_match = False
expected_candidates = ["不", "步", "部", "布"]  # 包括 bi, bu, ni, nu 的候选

# 测试 1.2：精确模式
input = "bi"
exact_match = True
expected_candidates = ["比", "笔", "必"]  # 只有 bi 的候选，不包括 ni

# 测试 1.3：精确模式
input = "ni"
exact_match = True
expected_candidates = ["你", "尼", "泥"]  # 只有 ni 的候选，不包括 bi
```

### 测试 2：14键方案

```yaml
# schema.yaml
speller:
  algebra:
    - derive/w/q/
    - derive/i/u/
    - derive/n/b/
```

```python
# 测试 2.1：输入 bu，普通模式
RimeSetInputEx(session, "bu", False)
# 期望：bi, bu, ni, nu 的所有候选

# 测试 2.2：输入 bi，精确模式
RimeSetInputEx(session, "bi", True)
# 期望：只有 bi 的候选

# 测试 2.3：输入 ni，精确模式
RimeSetInputEx(session, "ni", True)
# 期望：只有 ni 的候选
```

### 测试 3：向后兼容性

```python
# 使用旧 API
RimeSetInput(session, "bu")
# 期望：行为与 RimeSetInputEx(session, "bu", False) 相同
```

---

## 注意事项

### 1. Dictionary 接口

需要确认 `Dictionary::LookupWords` 是否支持音节过滤。如果不支持，有两种选择：

**选项 A**：修改 `LookupWords` 添加音节参数

```cpp
// dict/dictionary.h
class Dictionary {
 public:
  void LookupWords(DictEntryIterator* result,
                   const string& input,
                   bool predictive,
                   size_t limit,
                   const set<string>* blacklist = nullptr,
                   const set<SyllableId>* syllable_filter = nullptr);  // 新增
};
```

**选项 B**：在 Translator 层面过滤

使用 `DictEntryIterator::AddFilter` 过滤不需要的音节。

### 2. 性能考虑

精确匹配模式需要：
1. 遍历所有音节描述符
2. 查询音节表
3. 字符串比较

**优化建议**：
- 缓存音节表访问
- 使用 hash 比较而非字符串比较
- 只在精确模式下执行额外逻辑

### 3. ScriptTranslator

如果使用 `ScriptTranslator`，需要类似的修改。

---

## 编译与测试

### 编译

```bash
cd librime
mkdir build && cd build
cmake -DBUILD_TEST=ON ..
make -j4
```

### 运行测试

```bash
# 单元测试
./test/rime_test

# 手动测试
./bin/rime_console
> schema: luna_pinyin
> input: bu
> (查看候选)
> set_input_ex bi true
> (查看候选，应该只有 bi)
```

---

## 总结

本补丁通过以下修改解决了 `RimeSetInput` 的拼写运算派生问题：

1. **Context**：添加 `exact_match` 标志
2. **API**：新增 `RimeSetInputEx` 支持精确匹配
3. **Translator**：在精确模式下过滤派生音节

**核心原理**：
- 普通模式：拼写 `bi` → 音节 `{bi, ni}`（允许派生）
- 精确模式：拼写 `bi` → 音节 `{bi}`（只接受拼写=音节）

**兼容性**：
- 默认行为不变
- 旧代码无需修改
- 新功能通过新 API 启用
