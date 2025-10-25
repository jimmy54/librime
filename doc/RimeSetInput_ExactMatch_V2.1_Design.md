# RimeSetInput 部分精确匹配 V2.1 - 优化设计

## V2.1 优化说明

### 相比 V2.0 的改进

#### 1. 参数命名优化

**V2.0**：
```cpp
void set_input(const string& value, int exact_length = 0);
```

**V2.1**：
```cpp
void set_input(const string& value, int input_exact_length = 0);
```

**优化理由**：
- `input_exact_length` 更明确表达"输入码的精确长度"
- 避免与其他 `length` 概念混淆
- 提高代码可读性

#### 2. 实现优先级调整

**V2.0**：先在 `TableTranslator` 实现

**V2.1**：先在 `ScriptTranslator` 实现

**优化理由**：
1. **ScriptTranslator 更常用**：大多数方案使用 ScriptTranslator（如拼音、注音）
2. **架构更清晰**：ScriptTranslator 使用 SyllableGraph，逻辑更统一
3. **测试更方便**：拼音方案测试更直观
4. **后续扩展**：验证后再扩展到 TableTranslator

---

## ScriptTranslator 架构分析

### 核心流程

```
用户输入 "bubu"
    ↓
ScriptTranslator::Query()
    ↓
ScriptSyllabifier::BuildSyllableGraph()  ← 【关键点】构建音节图
    ↓
Syllabifier::BuildSyllableGraph()
    ↓
Prism::CommonPrefixSearch()  ← 查询所有匹配的拼写
    ↓
SyllableGraph (音节图)
    edges: {
      0: {2: {bu: [bu, bi, ni, nu]}},  ← 位置0-2的所有音节
      2: {4: {bu: [bu, bi, ni, nu]}}   ← 位置2-4的所有音节
    }
    ↓
Dictionary::Lookup(syllable_graph, ...)  ← 根据音节图查询词条
    ↓
候选词
```

### 关键数据结构

#### SyllableGraph

```cpp
struct SyllableGraph {
  // edges[start_pos][end_pos][syllable_id] = SpellingProperties
  map<int, map<int, map<SyllableId, SpellingProperties>>> edges;
  
  // 示例：输入 "bubu"
  // edges[0][2][bu_id] = {type: Normal, ...}
  // edges[0][2][bi_id] = {type: Derived, ...}  ← 派生的
  // edges[0][2][ni_id] = {type: Derived, ...}  ← 派生的
};
```

### 精确匹配的切入点

在 `Syllabifier::BuildSyllableGraph()` 中，可以根据 `input_exact_length` 过滤派生音节：

```cpp
// 伪代码
for (auto& match : prism_matches) {
  size_t start_pos = current_pos;
  size_t end_pos = current_pos + match.length;
  
  // 【关键】：判断是否需要精确匹配
  bool need_exact = (input_exact_length > 0 && 
                     start_pos < input_exact_length);
  
  auto spelling_accessor = prism.QuerySpelling(match.value);
  while (!spelling_accessor.exhausted()) {
    SyllableId syllable_id = spelling_accessor.syllable_id();
    
    if (need_exact) {
      // 精确模式：过滤派生音节
      string spelling = input.substr(start_pos, match.length);
      string syllable = syllabary[syllable_id];
      if (spelling != syllable) {
        continue;  // 跳过派生的音节
      }
    }
    
    // 添加到音节图
    graph->edges[start_pos][end_pos][syllable_id] = properties;
  }
}
```

---

## 详细设计

### 1. Context API（与 V2.0 相同，仅参数名优化）

```cpp
// context.h

class Context {
 public:
  // 【V2.1 优化】：参数名更明确
  void set_input(const string& value, int input_exact_length = 0);
  
  // 【V2.1 优化】：方法名对应
  int input_exact_length() const { return input_exact_length_; }
  
  // 判断指定位置是否需要精确匹配
  bool is_exact_at(size_t pos) const {
    if (input_exact_length_ <= 0)
      return false;
    return pos < static_cast<size_t>(input_exact_length_);
  }
  
 private:
  // 【V2.1 优化】：成员变量名对应
  int input_exact_length_ = 0;
};
```

### 2. Context 实现

```cpp
// context.cc

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

### 3. API 设计

```c
// rime_api.h

// 【V2.1 优化】：参数名更明确
RIME_API Bool RimeSetInputEx(RimeSessionId session_id,
                              const char* input,
                              int input_exact_length);
```

```cpp
// rime_api_impl.h

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

---

## ScriptTranslator 实现方案

### 方案 A：在 Syllabifier 层面过滤（不推荐）

#### 问题：动态性冲突

**致命缺陷**：`input_exact_length` 是动态的，可以随时通过 `RimeSetInputEx` 修改，但音节图在构建后会被缓存。

```cpp
// 场景：用户连续输入
RimeSetInputEx(session, "bu", 0);      // 构建音节图1：bu → {bu,bi,ni,nu}
RimeSetInputEx(session, "bubu", 2);    // 期望：前2码精确
// 问题：音节图已缓存，无法动态过滤！
```

**结论**：方案 A 不可行，因为：
1. 音节图构建后会被缓存复用
2. `input_exact_length` 可以动态修改
3. 无法在构建时确定最终的精确长度

### 方案 B：在 ScriptTranslator 层面过滤（推荐）

#### 优点
- **支持动态修改**：每次查询时根据当前 `input_exact_length` 过滤
- **不修改 Syllabifier**：保持 Syllabifier 的通用性和缓存机制
- **逻辑清晰**：精确匹配是查询时的过滤条件，不影响音节图构建

#### 实现原理

音节图构建和查询分离：
1. **构建阶段**：Syllabifier 构建完整的音节图（包含所有派生）
2. **查询阶段**：ScriptTranslator 根据 `input_exact_length` 动态过滤

#### 实现位置

修改 `src/rime/gear/script_translator.cc` 中的 `ScriptTranslation::MakeSentence()`

#### 核心代码

```cpp
// script_translator.cc

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
    
    if (user_dict) {
      auto user_result = user_dict->Lookup(syllable_graph, start_pos,
                                          kMaxSyllablesForUserPhraseQuery);
      // 【V2.1 新增】：过滤用户词典结果
      if (need_exact && syllabary) {
        FilterLookupResult(user_result, syllable_graph, start_pos, 
                          syllabary, same_start_pos);
      } else {
        EnrollEntries(same_start_pos, user_result);
      }
    }
    
    // 系统词典查询
    auto dict_result = dict->Lookup(syllable_graph, start_pos, 0);
    // 【V2.1 新增】：过滤系统词典结果
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
    
    // 获取拼写
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

// 【V2.1 新增】：获取拼写字符串
string ScriptTranslation::GetSpelling(const SyllableGraph& graph,
                                     size_t start_pos,
                                     size_t end_pos) const {
  // 从音节图中获取拼写
  // 实现方式取决于 SyllableGraph 的具体结构
  return syllabifier_->input_.substr(start_pos, end_pos - start_pos);
}
```

#### 辅助方法声明

```cpp
// script_translator.cc - ScriptTranslation 类

class ScriptTranslation : public Translation {
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
};
```

---

## 推荐方案：方案 B

### 理由

1. **支持动态修改**：`input_exact_length` 可以随时修改，每次查询时动态过滤
2. **不破坏缓存**：音节图保持完整，不影响 Syllabifier 的缓存机制
3. **逻辑清晰**：精确匹配是查询条件，不是构建条件
4. **易于调试**：过滤逻辑集中在 ScriptTranslator，便于追踪

### 实现步骤

1. 在 `ScriptTranslation::MakeSentence()` 中获取 `input_exact_length`
2. 在查询词典时，根据起始位置判断是否需要精确匹配
3. 过滤查询结果，只保留精确匹配的词条

---

## 使用示例

### C++ 示例

```cpp
#include <rime_api.h>

RimeSessionId session = RimeCreateSession();
RimeSelectSchema(session, "luna_pinyin");

// 示例 1：全部派生（默认）
RimeSetInput(session, "bubu");
// 候选：不步、不部、比步、比部...

// 示例 2：前2码精确
RimeSetInputEx(session, "bubu", 2);
// 候选：不步、不部、不比...（第一个音节只有 bu）

// 示例 3：全部精确
RimeSetInputEx(session, "bubu", 4);
// 候选：不步、不部...（两个音节都是 bu）
```

### 14键拼音方案示例

```yaml
# rime_ice_14.schema.yaml
speller:
  algebra:
    - derive/i/u/
    - derive/n/b/
```

```cpp
// 测试
RimeSelectSchema(session, "rime_ice_14");

// 输入 "bubu"，前2码精确
RimeSetInputEx(session, "bubu", 2);

// 音节图：
// [0-2]: bu (精确) → 只有 bu
// [2-4]: bu (派生) → bu, bi, ni, nu

// 候选：
// ✓ 不步 (bu + bu)
// ✓ 不比 (bu + bi)
// ✓ 不你 (bu + ni)
// ✗ 比步 (bi + bu) - 第一个音节不匹配
```

---

## 测试计划

### 单元测试

```cpp
// test_script_translator.cc

TEST(ScriptTranslatorTest, PartialExactMatch) {
  // 准备
  RimeSessionId session = CreateTestSession("luna_pinyin");
  
  // 测试1：全部派生
  RimeSetInputEx(session, "bubu", 0);
  auto candidates = GetCandidates(session);
  EXPECT_TRUE(HasCandidate(candidates, "不步"));
  EXPECT_TRUE(HasCandidate(candidates, "比步"));  // 派生
  
  // 测试2：前2码精确
  RimeSetInputEx(session, "bubu", 2);
  candidates = GetCandidates(session);
  EXPECT_TRUE(HasCandidate(candidates, "不步"));
  EXPECT_TRUE(HasCandidate(candidates, "不比"));
  EXPECT_FALSE(HasCandidate(candidates, "比步"));  // 第一个音节不匹配
  
  // 测试3：全部精确
  RimeSetInputEx(session, "bubu", 4);
  candidates = GetCandidates(session);
  EXPECT_TRUE(HasCandidate(candidates, "不步"));
  EXPECT_FALSE(HasCandidate(candidates, "不比"));
}
```

### 集成测试

```python
#!/usr/bin/env python3
# test_partial_exact_match.py

import rime_api as rime

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
    
    print("✓ 14键方案测试通过")

if __name__ == "__main__":
    test_14key_scheme()
```

---

## 性能分析

### 方案 A（推荐）

**时间复杂度**：O(n * m)
- n: 输入长度
- m: 每个位置的平均匹配数

**额外开销**：
- 字符串比较：每个音节一次
- 音节表查询：已缓存，O(1)

**优化**：
- 只在精确范围内过滤
- 非精确范围直接添加，无额外开销

### 方案 B

**时间复杂度**：O(n * m) + O(k)
- k: 音节图中的音节总数

**额外开销**：
- 遍历音节图：O(k)
- 删除操作：可能导致内存碎片

---

## 注意事项

### 1. Syllabary 访问

需要确保 `Prism::syllabary()` 方法存在：

```cpp
// prism.h
class Prism {
 public:
  const Syllabary* syllabary() const { return syllabary_.get(); }
};
```

### 2. 分隔符处理

如果输入包含分隔符（如 `'`），需要正确计算位置：

```
输入：bu'bu (5字符)
input_exact_length：2

解析：
  bu (0-2): 精确
  ' (2-3): 分隔符
  bu (3-5): 派生
```

### 3. 多音节匹配

某些拼写可能匹配多个长度：

```
输入：xian
可能：xi + an 或 xian

需要确保精确匹配逻辑对两种切分都正确
```

---

## 总结

### V2.1 改进点

1. **命名优化**：`exact_length` → `input_exact_length`
2. **实现优先级**：先 ScriptTranslator，后 TableTranslator
3. **推荐方案**：在 Syllabifier 层面过滤（方案 A1）

### 核心优势

1. **效率高**：在构建音节图时就过滤
2. **逻辑清晰**：精确匹配在音节识别阶段
3. **易于扩展**：其他 Translator 可复用

### 下一步

1. 实施方案 A1
2. 编写单元测试
3. 验证 14键方案
4. 扩展到 TableTranslator
