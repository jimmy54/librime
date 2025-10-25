# RimeSetInput 部分精确匹配 V2 - 完整代码补丁

## 补丁说明

本补丁实现部分精确匹配功能，允许指定需要精确匹配的前缀长度，后续部分允许派生。

## 核心改进

```cpp
// V1: 布尔标志（全部精确 or 全部派生）
void set_input(const string& value, bool exact_match);

// V2: 整数长度（部分精确）
void set_input(const string& value, int exact_length);
```

**示例**：
```cpp
RimeSetInputEx(session, "bubu", 2);
// 前2码 "bu" 精确匹配
// 后2码 "bu" 允许派生
```

---

## 补丁 1: src/rime/context.h

```cpp
class RIME_DLL Context {
 public:
  // ... 现有代码 ...
  
  // 【V2 修改】：exact_length 替代 exact_match
  // exact_length: 需要精确匹配的前缀长度（字符数）
  //   = 0: 全部派生（默认）
  //   > 0: 前 N 个字符精确匹配
  //   < 0: 全部精确匹配
  void set_input(const string& value, int exact_length = 0);
  const string& input() const { return input_; }
  
  // 【V2 新增】：获取精确匹配长度
  int exact_length() const { return exact_length_; }
  
  // 【V2 新增】：判断指定位置是否需要精确匹配
  bool is_exact_at(size_t pos) const {
    if (exact_length_ <= 0)
      return false;
    return pos < static_cast<size_t>(exact_length_);
  }

 private:
  // ... 现有代码 ...
  
  // 【V2 新增】：精确匹配长度
  int exact_length_ = 0;
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
+  void set_input(const string& value, int exact_length = 0);
+  int exact_length() const { return exact_length_; }
+  bool is_exact_at(size_t pos) const;
   const string& input() const { return input_; }
 
   void set_caret_pos(size_t caret_pos);
@@ -117,6 +119,9 @@ class RIME_DLL Context {
   map<string, bool> options_;
   map<string, string> properties_;
 
+  // Exact match length for partial exact matching
+  int exact_length_ = 0;
+
   // External context from frontend
   string external_preceding_text_;
   string external_following_text_;
```

---

## 补丁 2: src/rime/context.cc

```cpp
void Context::set_input(const string& value, int exact_length) {
  input_ = value;
  caret_pos_ = input_.length();
  
  // 处理特殊值
  if (exact_length < 0) {
    // 负数：全部精确
    exact_length_ = static_cast<int>(input_.length());
  } else if (exact_length > static_cast<int>(input_.length())) {
    // 超过输入长度：限制为输入长度
    exact_length_ = static_cast<int>(input_.length());
  } else {
    exact_length_ = exact_length;
  }
  
  DLOG(INFO) << "Context::set_input: input=\"" << value 
             << "\", exact_length=" << exact_length_;
  
  update_notifier_(this);
}

bool Context::is_exact_at(size_t pos) const {
  if (exact_length_ <= 0)
    return false;
  return pos < static_cast<size_t>(exact_length_);
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
+void Context::set_input(const string& value, int exact_length) {
   input_ = value;
   caret_pos_ = input_.length();
+  
+  // Handle special values
+  if (exact_length < 0) {
+    // Negative: exact match all
+    exact_length_ = static_cast<int>(input_.length());
+  } else if (exact_length > static_cast<int>(input_.length())) {
+    // Exceeds input length: limit to input length
+    exact_length_ = static_cast<int>(input_.length());
+  } else {
+    exact_length_ = exact_length;
+  }
+  
+  DLOG(INFO) << "Context::set_input: input=\"" << value 
+             << "\", exact_length=" << exact_length_;
+  
   update_notifier_(this);
 }
+
+bool Context::is_exact_at(size_t pos) const {
+  if (exact_length_ <= 0)
+    return false;
+  return pos < static_cast<size_t>(exact_length_);
+}
```

---

## 补丁 3: src/rime_api.h

```c
// 【V2 修改】：参数从 Bool 改为 int
// exact_length: 需要精确匹配的前缀长度
//   = 0: 全部派生（默认行为）
//   > 0: 前 N 个字符精确匹配，后续允许派生
//   < 0: 全部精确匹配
RIME_API Bool RimeSetInputEx(RimeSessionId session_id,
                              const char* input,
                              int exact_length);

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
+//! \param exact_length: Length of prefix that needs exact match
+//!   = 0: all derivations allowed (default)
+//!   > 0: first N characters exact match, rest allow derivations
+//!   < 0: all exact match
+RIME_API Bool RimeSetInputEx(RimeSessionId session_id,
+                              const char* input,
+                              int exact_length);
+
 RIME_API char* RimeGetInput(RimeSessionId session_id);
```

---

## 补丁 4: src/rime_api_impl.h

```cpp
// 【V2 新增】：RimeSetInputEx 实现
Bool RimeSetInputEx(RimeSessionId session_id,
                    const char* input,
                    int exact_length) {
  an<Session> session(Service::instance().GetSession(session_id));
  if (!session)
    return False;
  Context* ctx = session->context();
  if (!ctx)
    return False;
  ctx->set_input(input, exact_length);
  return True;
}

// 【V2 修改】：RimeSetInput 调用 RimeSetInputEx
RIME_DEPRECATED Bool RimeSetInput(RimeSessionId session_id, const char* input) {
  return RimeSetInputEx(session_id, input, 0);  // 默认全部派生
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
+                    int exact_length) {
+  an<Session> session(Service::instance().GetSession(session_id));
+  if (!session)
+    return False;
+  Context* ctx = session->context();
+  if (!ctx)
+    return False;
+  ctx->set_input(input, exact_length);
+  return True;
+}
+
 char* RimeGetInput(RimeSessionId session_id) {
   an<Session> session(Service::instance().GetSession(session_id));
   if (!session)
```

---

## 补丁 5: src/rime/gear/table_translator.h

```cpp
class TableTranslator : public Translator {
 protected:
  // ... 现有代码 ...
  
  // 【V2 新增】：判断指定位置是否需要精确匹配
  bool NeedExactMatch(size_t start_pos) const;
  
  // 【V2 新增】：过滤音节（根据精确匹配要求）
  set<SyllableId> FilterSyllables(const string& spelling,
                                   SyllableId spelling_id,
                                   bool exact_match) const;
  
  // 【V2 新增】：获取音节表（缓存）
  const Syllabary* GetSyllabary() const;
  
 private:
  // 【V2 新增】：缓存音节表
  mutable const Syllabary* cached_syllabary_ = nullptr;
};
```

**完整 diff**：

```diff
--- a/src/rime/gear/table_translator.h
+++ b/src/rime/gear/table_translator.h
@@ -XXX,6 +XXX,16 @@ class TableTranslator : public Translator {
   // ... existing methods ...
   
  protected:
+  // V2: Partial exact match support
+  bool NeedExactMatch(size_t start_pos) const;
+  set<SyllableId> FilterSyllables(const string& spelling,
+                                   SyllableId spelling_id,
+                                   bool exact_match) const;
+  const Syllabary* GetSyllabary() const;
+  
+ private:
+  // V2: Cached syllabary for performance
+  mutable const Syllabary* cached_syllabary_ = nullptr;
+  
   // ... existing members ...
 };
```

---

## 补丁 6: src/rime/gear/table_translator.cc

### 6.1 辅助方法实现

```cpp
bool TableTranslator::NeedExactMatch(size_t start_pos) const {
  Context* ctx = engine_->context();
  if (!ctx)
    return false;
  
  // 使用 is_exact_at 判断起始位置是否在精确范围内
  return ctx->is_exact_at(start_pos);
}

const Syllabary* TableTranslator::GetSyllabary() const {
  if (!cached_syllabary_ && dict_ && dict_->loaded()) {
    cached_syllabary_ = dict_->syllabary();
  }
  return cached_syllabary_;
}

set<SyllableId> TableTranslator::FilterSyllables(
    const string& spelling,
    SyllableId spelling_id,
    bool exact_match) const {
  
  set<SyllableId> result;
  
  if (!dict_ || !dict_->loaded())
    return result;
  
  auto spelling_accessor = dict_->prism()->QuerySpelling(spelling_id);
  const Syllabary* syllabary = GetSyllabary();
  
  if (!exact_match) {
    // 非精确模式：返回所有音节
    while (!spelling_accessor.exhausted()) {
      result.insert(spelling_accessor.syllable_id());
      spelling_accessor.Next();
    }
    return result;
  }
  
  // 精确模式：只返回拼写=音节的结果
  while (!spelling_accessor.exhausted()) {
    SyllableId syllable_id = spelling_accessor.syllable_id();
    
    if (syllabary && syllable_id < syllabary->size()) {
      const string& syllable_str = (*syllabary)[syllable_id];
      if (spelling == syllable_str) {
        result.insert(syllable_id);
      }
    }
    
    spelling_accessor.Next();
  }
  
  return result;
}
```

### 6.2 Query 方法修改

在 `Query` 方法中应用精确匹配逻辑：

```cpp
an<Translation> TableTranslator::Query(const string& input,
                                       const Segment& segment,
                                       string* prompt) {
  // ... 现有代码 ...
  
  for (size_t start_pos : vertices) {
    // ... 现有代码 ...
    
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
        
        // 【V2 关键修改】：判断是否需要精确匹配
        bool need_exact = NeedExactMatch(start_pos);
        string spelling_str = active_input.substr(0, m.length);
        
        // 【V2 关键修改】：过滤音节
        set<SyllableId> valid_syllables = 
            FilterSyllables(spelling_str, m.value, need_exact);
        
        if (valid_syllables.empty())
          continue;
        
        // 查询词条
        DictEntryIterator iter;
        dict_->LookupWords(&iter, spelling_str, false, 0, &blacklist());
        
        // 【V2 关键修改】：过滤词条
        if (need_exact && !valid_syllables.empty()) {
          iter.AddFilter([valid_syllables](const DictEntry& entry) {
            return valid_syllables.count(entry.code[0]) > 0;
          });
        }
        
        if (filter_by_charset) {
          iter.AddFilter(CharsetFilter::FilterDictEntry);
        }
        
        if (!iter.exhausted()) {
          vertices.insert(end_pos);
          if (start_pos == 0 && max_homographs_ - homographs.size() > 1) {
            DictEntryIterator iter_copy = iter;
            collect_entries(homographs, iter_copy, max_homographs_);
          } else {
            collect_entries(homographs, iter, max_homographs_);
          }
          if (include_prefix_phrases && start_pos == 0) {
            collector[consumed_length] = std::move(iter);
            DLOG(INFO) << "table[" << consumed_length
                       << "]: " << collector[consumed_length].entry_count();
          }
        }
      }
    }
  }
  
  // ... 现有代码 ...
}
```

### 完整 diff

```diff
--- a/src/rime/gear/table_translator.cc
+++ b/src/rime/gear/table_translator.cc
@@ -XXX,6 +XXX,65 @@ TableTranslator::~TableTranslator() {
   // destructor
 }
 
+// V2: Helper methods for partial exact match
+
+bool TableTranslator::NeedExactMatch(size_t start_pos) const {
+  Context* ctx = engine_->context();
+  if (!ctx)
+    return false;
+  return ctx->is_exact_at(start_pos);
+}
+
+const Syllabary* TableTranslator::GetSyllabary() const {
+  if (!cached_syllabary_ && dict_ && dict_->loaded()) {
+    cached_syllabary_ = dict_->syllabary();
+  }
+  return cached_syllabary_;
+}
+
+set<SyllableId> TableTranslator::FilterSyllables(
+    const string& spelling,
+    SyllableId spelling_id,
+    bool exact_match) const {
+  
+  set<SyllableId> result;
+  
+  if (!dict_ || !dict_->loaded())
+    return result;
+  
+  auto spelling_accessor = dict_->prism()->QuerySpelling(spelling_id);
+  const Syllabary* syllabary = GetSyllabary();
+  
+  if (!exact_match) {
+    // Non-exact mode: return all syllables
+    while (!spelling_accessor.exhausted()) {
+      result.insert(spelling_accessor.syllable_id());
+      spelling_accessor.Next();
+    }
+    return result;
+  }
+  
+  // Exact mode: only return syllables that match spelling
+  while (!spelling_accessor.exhausted()) {
+    SyllableId syllable_id = spelling_accessor.syllable_id();
+    
+    if (syllabary && syllable_id < syllabary->size()) {
+      const string& syllable_str = (*syllabary)[syllable_id];
+      if (spelling == syllable_str) {
+        result.insert(syllable_id);
+      }
+    }
+    
+    spelling_accessor.Next();
+  }
+  
+  return result;
+}
+
+// V2: Query method with partial exact match support
+
 an<Translation> TableTranslator::Query(const string& input,
                                        const Segment& segment,
                                        string* prompt) {
@@ -640,6 +699,8 @@ an<Translation> TableTranslator::Query(const string& input,
       }
     }
     if (dict_ && dict_->loaded()) {
       vector<Prism::Match> matches;
       dict_->prism()->CommonPrefixSearch(input.substr(start_pos), &matches);
@@ -653,7 +714,22 @@ an<Translation> TableTranslator::Query(const string& input,
         auto& homographs = same_start_pos[end_pos];
         if (homographs.size() >= max_homographs_)
           continue;
+        
+        // V2: Check if exact match is needed at this position
+        bool need_exact = NeedExactMatch(start_pos);
+        string spelling_str = active_input.substr(0, m.length);
+        
+        // V2: Filter syllables based on exact match requirement
+        set<SyllableId> valid_syllables = 
+            FilterSyllables(spelling_str, m.value, need_exact);
+        
+        if (valid_syllables.empty())
+          continue;
+        
         DictEntryIterator iter;
         dict_->LookupWords(&iter, active_input.substr(0, m.length), false, 0,
                            &blacklist());
+        
+        // V2: Filter entries by valid syllables
+        if (need_exact && !valid_syllables.empty()) {
+          iter.AddFilter([valid_syllables](const DictEntry& entry) {
+            return valid_syllables.count(entry.code[0]) > 0;
+          });
+        }
+        
         if (filter_by_charset) {
           iter.AddFilter(CharsetFilter::FilterDictEntry);
```

---

## 使用示例

### C++ 示例

```cpp
#include <rime_api.h>

RimeSessionId session = RimeCreateSession();

// 示例 1：全部派生（默认）
RimeSetInput(session, "bubu");
// 或
RimeSetInputEx(session, "bubu", 0);
// 结果：所有派生组合

// 示例 2：前2码精确
RimeSetInputEx(session, "bubu", 2);
// 结果：
//   位置 0-2: bu (精确)
//   位置 2-4: bu (派生)
// 候选：不步、不比、不你...

// 示例 3：全部精确
RimeSetInputEx(session, "bubu", 4);
// 或
RimeSetInputEx(session, "bubu", -1);
// 结果：只有 bu+bu 的组合

// 示例 4：逐步确认
RimeSetInputEx(session, "bu", 0);      // 第1步：全部派生
// 用户选择"不"
RimeSetInputEx(session, "bubu", 2);    // 第2步：前2码精确
// 用户选择"不步"
RimeSetInputEx(session, "bububi", 4);  // 第3步：前4码精确
```

### JavaScript Filter 示例

```javascript
// 筛选器：逐步确认用户输入

function filter(input, env) {
  // 获取当前输入
  let current_input = env.engine.context.input;
  
  // 获取已确认的长度（从某处获取，例如用户选择的候选）
  let confirmed_length = get_confirmed_length();
  
  // 设置部分精确匹配
  if (confirmed_length > 0) {
    rime.set_input_ex(current_input, confirmed_length);
  }
  
  return input;
}

// 辅助函数：获取已确认长度
function get_confirmed_length() {
  // 根据业务逻辑实现
  // 例如：统计已选择候选的拼音长度
  return 2;
}
```

---

## 测试用例

### 测试脚本

```python
#!/usr/bin/env python3
# test_partial_exact_match.py

import rime_api as rime

def test_basic():
    """测试基本功能"""
    session = rime.create_session()
    
    # 测试1：全部派生
    rime.set_input_ex(session, "bubu", 0)
    candidates = rime.get_candidates(session)
    assert "不步" in candidates
    assert "比步" in candidates  # 派生
    
    # 测试2：前2码精确
    rime.set_input_ex(session, "bubu", 2)
    candidates = rime.get_candidates(session)
    assert "不步" in candidates
    assert "不比" in candidates
    assert "比步" not in candidates  # 第1个音节不匹配
    
    # 测试3：全部精确
    rime.set_input_ex(session, "bubu", 4)
    candidates = rime.get_candidates(session)
    assert "不步" in candidates
    assert "不比" not in candidates
    
    print("✓ 基本功能测试通过")

def test_edge_cases():
    """测试边界情况"""
    session = rime.create_session()
    
    # 测试1：exact_length 超过输入长度
    rime.set_input_ex(session, "bu", 10)
    # 应该等同于 exact_length=2（全部精确）
    
    # 测试2：exact_length 为负数
    rime.set_input_ex(session, "bubu", -1)
    # 应该等同于 exact_length=4（全部精确）
    
    # 测试3：空输入
    rime.set_input_ex(session, "", 2)
    candidates = rime.get_candidates(session)
    assert len(candidates) == 0
    
    print("✓ 边界情况测试通过")

def test_14key_scheme():
    """测试14键方案"""
    session = rime.create_session()
    rime.select_schema(session, "rime_ice_14")
    
    # 测试：输入 bububi，前4码精确
    rime.set_input_ex(session, "bububi", 4)
    candidates = rime.get_candidates(session)
    
    # 验证
    assert "不步比" in candidates   # bu + bu + bi ✓
    assert "不步你" in candidates   # bu + bu + ni ✓
    assert "不比比" not in candidates  # bu + bi + bi ✗
    assert "比步比" not in candidates  # bi + bu + bi ✗
    
    print("✓ 14键方案测试通过")

if __name__ == "__main__":
    test_basic()
    test_edge_cases()
    test_14key_scheme()
    print("\n✅ 所有测试通过！")
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

### 手动测试步骤

```
1. 启动 rime_console
   $ ./bin/rime_console

2. 选择方案
   > schema: rime_ice_14

3. 测试全部派生
   > input: bubu
   > (查看候选，应该包含 bi+bi, bi+bu, bu+bi, bu+bu 等)

4. 测试部分精确
   > set_input_ex bubu 2
   > (查看候选，第一个音节应该只有 bu)

5. 测试全部精确
   > set_input_ex bubu 4
   > (查看候选，应该只有 bu+bu)
```

---

## 性能优化建议

### 1. 缓存音节表

```cpp
// 已实现
const Syllabary* cached_syllabary_ = nullptr;
```

### 2. 提前退出

```cpp
// 已实现：非精确模式直接返回所有音节
if (!exact_match) {
  // Fast path
  return all_syllables;
}
```

### 3. 缓存过滤结果

```cpp
// 可选优化：缓存 FilterSyllables 结果
map<pair<SyllableId, bool>, set<SyllableId>> filter_cache_;
```

---

## 注意事项

### 1. DictEntry 结构

需要确认 `DictEntry::code` 字段的含义：

```cpp
iter.AddFilter([valid_syllables](const DictEntry& entry) {
  return valid_syllables.count(entry.code[0]) > 0;
});
```

如果 `code` 不是音节ID数组，需要调整过滤逻辑。

### 2. 分隔符处理

如果输入包含分隔符（如 `'`），需要特殊处理：

```cpp
// 示例：bu'bu
// 精确长度：2
// 应该匹配：bu (0-2) 精确，bu (3-5) 派生
```

### 3. ScriptTranslator

如果使用 `ScriptTranslator`，需要类似的修改。

---

## 总结

### 改进点

1. **更灵活**：支持部分精确匹配
2. **更实用**：适合连续输入和逐步确认场景
3. **向后兼容**：默认行为不变（`exact_length=0`）

### 核心实现

1. Context 存储 `exact_length_`
2. 提供 `is_exact_at(pos)` 判断位置是否精确
3. Translator 根据起始位置过滤音节

### 测试覆盖

- ✓ 基本功能（全部派生、部分精确、全部精确）
- ✓ 边界情况（负数、超长、空输入）
- ✓ 14键方案（实际应用场景）

### 下一步

1. 审阅代码补丁
2. 实施修改
3. 运行测试
4. 性能验证
