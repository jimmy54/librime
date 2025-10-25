# RimeSetInput 精确匹配模式 V2 - 部分精确匹配设计

## 改进说明

### V1 的局限性

V1 方案使用布尔标志 `exact_match`，只能控制"全部精确"或"全部派生"：

```cpp
// V1: 只能二选一
RimeSetInputEx(session, "bubu", True);   // 全部精确
RimeSetInputEx(session, "bubu", False);  // 全部派生
```

**问题**：无法支持"部分精确"的场景。

### V2 的改进

使用整数参数 `exact_length` 指定需要精确匹配的前缀长度：

```cpp
// V2: 灵活控制
RimeSetInputEx(session, "bubu", 2);  // 前2码精确，后面派生
// "bu" 精确 → 只匹配 bu
// "bu" 派生 → 可匹配 bi, ni, nu
```

### 应用场景

#### 场景 1：筛选器逐步确认

```
用户输入：bu
候选：不、步、部、布、比、笔...（包括 bi, ni 的候选）

用户选择：不（bu）
筛选器设置：RimeSetInputEx("bu", 2)  // 前2码精确

继续输入：bu
候选：不步、不部、不比...
       ↑ 第一个 bu 精确
          ↑ 第二个 bu 可派生
```

#### 场景 2：14键连续输入

```yaml
# 配置
- derive/i/u/
- derive/n/b/

输入：bubu (4码)
设置：RimeSetInputEx("bubu", 2)

解析：
  bu (0-2): 精确匹配 → 只匹配音节 bu
  bu (2-4): 派生匹配 → 匹配音节 bu, bi, ni, nu

候选：
  ✓ 不步 (bu + bu)
  ✓ 不比 (bu + bi)
  ✓ 不你 (bu + ni)
  ✗ 比步 (bi + bu) - 第一个音节不匹配
```

## 详细设计

### 1. API 设计

#### C API

```c
// rime_api.h

// 【V2 新增】：支持部分精确匹配
// exact_length: 需要精确匹配的前缀长度（字符数）
//   = 0: 全部派生（默认行为）
//   > 0: 前 N 个字符精确匹配，后续字符允许派生
//   < 0: 全部精确匹配（等同于 strlen(input)）
RIME_API Bool RimeSetInputEx(RimeSessionId session_id,
                              const char* input,
                              int exact_length);

// 【保持兼容】：旧 API
RIME_API Bool RimeSetInput(RimeSessionId session_id, const char* input);
```

#### C++ API

```cpp
// context.h

class Context {
 public:
  // 【V2 修改】：exact_length 替代 exact_match
  void set_input(const string& value, int exact_length = 0);
  
  // 【V2 新增】：获取精确匹配长度
  int exact_length() const { return exact_length_; }
  
  // 【V2 新增】：判断指定位置是否需要精确匹配
  bool is_exact_at(size_t pos) const {
    return exact_length_ < 0 || pos < static_cast<size_t>(exact_length_);
  }
  
 private:
  // 【V2 修改】：精确匹配长度
  // = 0: 全部派生
  // > 0: 前 N 个字符精确
  // < 0: 全部精确
  int exact_length_ = 0;
};
```

### 2. 实现逻辑

#### Context 实现

```cpp
// context.cc

void Context::set_input(const string& value, int exact_length) {
  input_ = value;
  caret_pos_ = input_.length();
  
  // 处理特殊值
  if (exact_length < 0) {
    // 负数表示全部精确
    exact_length_ = input_.length();
  } else if (exact_length > static_cast<int>(input_.length())) {
    // 超过输入长度，限制为输入长度
    exact_length_ = input_.length();
  } else {
    exact_length_ = exact_length;
  }
  
  DLOG(INFO) << "Context::set_input: input=\"" << value 
             << "\", exact_length=" << exact_length_;
  
  update_notifier_(this);
}

bool Context::is_exact_at(size_t pos) const {
  if (exact_length_ <= 0) {
    return false;  // 全部派生
  }
  return pos < static_cast<size_t>(exact_length_);
}
```

#### API 实现

```cpp
// rime_api_impl.h

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

// 保持兼容
RIME_DEPRECATED Bool RimeSetInput(RimeSessionId session_id, const char* input) {
  return RimeSetInputEx(session_id, input, 0);  // 默认全部派生
}
```

### 3. Translator 实现

这是最关键的部分，需要根据输入位置判断是否精确匹配。

#### 核心思路

```
输入：bubu (4字符)
精确长度：2

音节切分：
  位置 0-2: bu → 需要精确匹配
  位置 2-4: bu → 允许派生
```

#### TableTranslator 实现

```cpp
// table_translator.cc

an<Translation> TableTranslator::Query(const string& input,
                                       const Segment& segment,
                                       string* prompt) {
  // ... 现有代码 ...
  
  Context* ctx = engine_->context();
  int exact_length = ctx ? ctx->exact_length() : 0;
  
  // 遍历所有可能的起始位置
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
        
        size_t end_pos = start_pos + m.length;
        
        // 【关键】：判断当前音节是否在精确匹配范围内
        bool need_exact = (exact_length > 0 && start_pos < exact_length);
        
        // 获取拼写字符串
        string spelling_str = active_input.substr(0, m.length);
        
        // 查询音节
        auto spelling_accessor = dict_->prism()->QuerySpelling(m.value);
        const Syllabary* syllabary = dict_->syllabary();
        
        // 收集符合条件的音节
        set<SyllableId> valid_syllables;
        
        while (!spelling_accessor.exhausted()) {
          SyllableId syllable_id = spelling_accessor.syllable_id();
          
          if (need_exact) {
            // 精确匹配：拼写必须等于音节
            if (syllabary && syllable_id < syllabary->size()) {
              const string& syllable_str = (*syllabary)[syllable_id];
              if (spelling_str == syllable_str) {
                valid_syllables.insert(syllable_id);
              }
            }
          } else {
            // 派生匹配：接受所有音节
            valid_syllables.insert(syllable_id);
          }
          
          spelling_accessor.Next();
        }
        
        // 使用有效音节查询词条
        if (!valid_syllables.empty()) {
          DictEntryIterator iter;
          // 这里需要修改 LookupWords 支持音节过滤
          // 或者在查询后过滤
          dict_->LookupWords(&iter, spelling_str, false, 0, &blacklist());
          
          // 过滤不在 valid_syllables 中的词条
          if (need_exact) {
            iter.AddFilter([valid_syllables](const DictEntry& entry) {
              return valid_syllables.count(entry.syllable_id) > 0;
            });
          }
          
          // ... 后续处理 ...
        }
      }
    }
  }
  
  // ... 现有代码 ...
}
```

### 4. 详细实现（完整版）

#### 辅助方法

```cpp
// table_translator.h

class TableTranslator : public Translator {
 protected:
  // 【新增】：判断指定范围是否需要精确匹配
  bool NeedExactMatch(size_t start_pos, size_t length) const;
  
  // 【新增】：过滤音节（精确匹配模式）
  set<SyllableId> FilterSyllables(const string& spelling,
                                   SyllableId spelling_id,
                                   bool exact_match) const;
};
```

```cpp
// table_translator.cc

bool TableTranslator::NeedExactMatch(size_t start_pos, size_t length) const {
  Context* ctx = engine_->context();
  if (!ctx)
    return false;
  
  int exact_length = ctx->exact_length();
  if (exact_length <= 0)
    return false;
  
  // 只要音节的起始位置在精确范围内，就需要精确匹配
  return start_pos < static_cast<size_t>(exact_length);
}

set<SyllableId> TableTranslator::FilterSyllables(
    const string& spelling,
    SyllableId spelling_id,
    bool exact_match) const {
  
  set<SyllableId> result;
  
  if (!dict_ || !dict_->loaded())
    return result;
  
  auto spelling_accessor = dict_->prism()->QuerySpelling(spelling_id);
  const Syllabary* syllabary = dict_->syllabary();
  
  while (!spelling_accessor.exhausted()) {
    SyllableId syllable_id = spelling_accessor.syllable_id();
    
    if (exact_match) {
      // 精确匹配：拼写必须等于音节
      if (syllabary && syllable_id < syllabary->size()) {
        const string& syllable_str = (*syllabary)[syllable_id];
        if (spelling == syllable_str) {
          result.insert(syllable_id);
        }
      }
    } else {
      // 派生匹配：接受所有音节
      result.insert(syllable_id);
    }
    
    spelling_accessor.Next();
  }
  
  return result;
}
```

#### 主查询逻辑

```cpp
// table_translator.cc - Query 方法

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
    
    // 【关键修改】：判断是否需要精确匹配
    bool need_exact = NeedExactMatch(start_pos, m.length);
    string spelling_str = active_input.substr(0, m.length);
    
    // 过滤音节
    set<SyllableId> valid_syllables = 
        FilterSyllables(spelling_str, m.value, need_exact);
    
    if (valid_syllables.empty())
      continue;
    
    // 查询词条
    DictEntryIterator iter;
    dict_->LookupWords(&iter, spelling_str, false, 0, &blacklist());
    
    // 过滤词条
    if (need_exact && !valid_syllables.empty()) {
      iter.AddFilter([valid_syllables](const DictEntry& entry) {
        return valid_syllables.count(entry.syllable_id) > 0;
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
```

## 使用示例

### 示例 1：基本用法

```cpp
// 场景：用户输入 bubu，确认前2码为 bu

// 设置输入
RimeSetInputEx(session, "bubu", 2);

// 查询结果：
// 位置 0-2: bu (精确) → 只匹配 bu
// 位置 2-4: bu (派生) → 匹配 bu, bi, ni, nu

// 候选词：
// ✓ 不步 (bu + bu)
// ✓ 不比 (bu + bi)
// ✓ 不你 (bu + ni)
// ✗ 比步 (bi + bu) - 第一个音节不匹配
```

### 示例 2：全部精确

```cpp
// 方式1：指定长度
RimeSetInputEx(session, "bubu", 4);

// 方式2：使用负数
RimeSetInputEx(session, "bubu", -1);

// 结果：全部精确匹配
// 位置 0-2: bu (精确)
// 位置 2-4: bu (精确)
// 候选：只有 不步 (bu + bu)
```

### 示例 3：全部派生

```cpp
// 方式1：指定0
RimeSetInputEx(session, "bubu", 0);

// 方式2：使用旧API
RimeSetInput(session, "bubu");

// 结果：全部派生
// 候选：bi+bi, bi+bu, bi+ni, bu+bi, bu+bu, bu+ni, ni+bi, ni+bu, ni+ni...
```

### 示例 4：JavaScript Filter

```javascript
// 筛选器：用户选择候选后，确认已输入部分

function filter(input, env) {
  let candidates = [];
  
  for (let cand of input.iter()) {
    candidates.push(cand);
  }
  
  if (candidates.length > 0) {
    let selected = candidates[0];
    let preedit = selected.preedit;  // 例如 "bu"
    
    // 获取当前输入
    let current_input = env.engine.context.input;  // 例如 "bubu"
    
    // 确认前面已选择的部分
    let exact_len = preedit.length;  // 2
    
    // 设置部分精确匹配
    rime.set_input_ex(current_input, exact_len);
    
    // 重新查询
    env.engine.context.refresh();
  }
  
  return input;
}
```

### 示例 5：逐步确认

```cpp
// 场景：用户逐个确认音节

// 第1步：输入 bu
RimeSetInputEx(session, "bu", 0);  // 全部派生
// 候选：不、步、比、笔、你...

// 第2步：用户选择"不"，继续输入 bu
RimeSetInputEx(session, "bubu", 2);  // 前2码精确
// 候选：不步、不比、不你...

// 第3步：用户选择"不步"，继续输入 bi
RimeSetInputEx(session, "bububi", 4);  // 前4码精确
// 候选：不步比、不步笔...
```

## 测试用例

### 测试 1：基本功能

```python
# 配置：derive/i/u/, derive/n/b/

def test_partial_exact_match():
    # 测试1：前2码精确
    set_input_ex("bubu", 2)
    candidates = get_candidates()
    
    # 验证第一个音节只有 bu
    assert "不步" in candidates  # bu + bu ✓
    assert "不比" in candidates  # bu + bi ✓
    assert "比步" not in candidates  # bi + bu ✗
    
    # 测试2：全部精确
    set_input_ex("bubu", 4)
    candidates = get_candidates()
    
    assert "不步" in candidates  # bu + bu ✓
    assert "不比" not in candidates  # bu + bi ✗
    
    # 测试3：全部派生
    set_input_ex("bubu", 0)
    candidates = get_candidates()
    
    assert "不步" in candidates  # bu + bu ✓
    assert "不比" in candidates  # bu + bi ✓
    assert "比步" in candidates  # bi + bu ✓
```

### 测试 2：边界情况

```python
def test_edge_cases():
    # 测试1：exact_length 超过输入长度
    set_input_ex("bu", 10)  # 应该等同于 exact_length=2
    # 期望：全部精确
    
    # 测试2：exact_length 为负数
    set_input_ex("bubu", -1)  # 应该等同于 exact_length=4
    # 期望：全部精确
    
    # 测试3：空输入
    set_input_ex("", 2)
    # 期望：无候选
    
    # 测试4：exact_length = 0
    set_input_ex("bu", 0)
    # 期望：全部派生
```

### 测试 3：14键方案

```yaml
# schema.yaml
speller:
  algebra:
    - derive/w/q/
    - derive/i/u/
    - derive/n/b/
```

```python
def test_14key_scheme():
    # 测试：输入 bububi，前4码精确
    set_input_ex("bububi", 4)
    
    # 位置 0-2: bu (精确)
    # 位置 2-4: bu (精确)
    # 位置 4-6: bi (派生)
    
    candidates = get_candidates()
    
    # 验证
    assert "不步比" in candidates  # bu + bu + bi ✓
    assert "不步笔" in candidates  # bu + bu + bi ✓
    assert "不步你" in candidates  # bu + bu + ni ✓
    assert "不比比" not in candidates  # bu + bi + bi ✗ (第2个音节不匹配)
    assert "比步比" not in candidates  # bi + bu + bi ✗ (第1个音节不匹配)
```

## 性能优化

### 优化 1：缓存音节表

```cpp
class TableTranslator {
 private:
  // 缓存音节表，避免重复查询
  const Syllabary* cached_syllabary_ = nullptr;
  
  const Syllabary* GetSyllabary() const {
    if (!cached_syllabary_ && dict_ && dict_->loaded()) {
      cached_syllabary_ = dict_->syllabary();
    }
    return cached_syllabary_;
  }
};
```

### 优化 2：提前退出

```cpp
set<SyllableId> TableTranslator::FilterSyllables(...) const {
  set<SyllableId> result;
  
  if (!exact_match) {
    // 非精确模式：直接返回所有音节，无需过滤
    auto accessor = dict_->prism()->QuerySpelling(spelling_id);
    while (!accessor.exhausted()) {
      result.insert(accessor.syllable_id());
      accessor.Next();
    }
    return result;
  }
  
  // 精确模式：需要过滤
  // ...
}
```

### 优化 3：避免重复查询

```cpp
// 在 Query 方法中，缓存已查询的音节
map<pair<SyllableId, bool>, set<SyllableId>> syllable_cache;

auto key = make_pair(m.value, need_exact);
if (syllable_cache.count(key) == 0) {
  syllable_cache[key] = FilterSyllables(spelling_str, m.value, need_exact);
}
set<SyllableId>& valid_syllables = syllable_cache[key];
```

## 注意事项

### 1. 音节边界

精确长度是按**字符数**计算，不是音节数：

```
输入：bubu (4字符)
精确长度：2

解析：
  字符 0-2: bu → 精确
  字符 2-4: bu → 派生
```

如果需要按音节数计算，需要额外的逻辑。

### 2. 多音节匹配

某些拼写可能匹配多个长度的音节：

```
输入：xian (4字符)
可能匹配：
  - xi + an (2+2)
  - xian (4)
```

精确长度需要考虑所有可能的切分。

### 3. 分隔符

如果输入包含分隔符（如 `'`），需要正确处理：

```
输入：bu'bu (5字符，包含分隔符)
精确长度：2

解析：
  bu (0-2): 精确
  ' (2-3): 分隔符
  bu (3-5): 派生
```

## 总结

### V2 改进点

1. **更灵活**：支持部分精确匹配
2. **更实用**：适合连续输入场景
3. **向后兼容**：默认行为不变

### 核心原理

```
exact_length = 0: 全部派生（默认）
exact_length > 0: 前 N 个字符精确，后续派生
exact_length < 0: 全部精确
```

### 实现要点

1. Context 存储 `exact_length`
2. Translator 根据 `start_pos` 判断是否精确
3. 精确模式：过滤 `拼写≠音节` 的结果

### 下一步

1. 实施代码修改
2. 编写单元测试
3. 性能测试
4. 文档更新
