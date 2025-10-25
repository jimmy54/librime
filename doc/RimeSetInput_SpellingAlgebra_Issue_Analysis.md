# RimeSetInput 与拼写运算问题分析及解决方案

## 问题描述

### 核心问题

当使用 `RimeSetInput` API 明确设置输入码后，系统仍然会执行拼写运算（SpellingAlgebra）的派生逻辑，导致：

1. **重码过多**：明确输入 `bi` 时，仍会匹配到 `derive` 派生的 `ni`、`bu`、`nu` 等候选
2. **候选混乱**：用户明确指定的输入码被拼写运算"污染"，失去了精确控制

### 具体场景（14键方案）

配置文件中的派生规则：
```yaml
speller:
  algebra:
    - derive/w/q/
    - derive/r/e/
    - derive/y/t/
    - derive/i/u/    # i ↔ u
    - derive/p/o/
    - derive/s/a/
    - derive/f/d/
    - derive/h/g/
    - derive/k/j/
    - derive/x/z/
    - derive/v/c/
    - derive/n/b/    # n ↔ b
```

**问题表现**：
- 用户输入：`bu`
- 派生结果：`bi`, `bu`, `ni`, `nu`（因为 `i↔u`, `n↔b`）
- 筛选器调用 `RimeSetInput("bi")` 后
- **期望**：只显示 `bi` 的候选
- **实际**：仍显示 `ni` 的候选（因为 `bi` 通过 `n↔b` 派生出 `ni`）

## 根本原因分析

### 1. 拼写运算的执行时机

拼写运算在**编译时**完成，生成 Prism 文件：

```cpp
// dict_compiler.cc:318-327
bool DictCompiler::BuildPrism(...) {
  Script script;
  Projection p;
  auto algebra = config.GetList("speller/algebra");
  if (algebra && p.Load(algebra)) {
    for (const auto& x : syllabary) {
      script.AddSyllable(x);
    }
    p.Apply(&script);  // 执行拼写运算,生成所有派生拼写
  }
  // 将 script 保存到 Prism 文件
  prism_->Build(syllabary, script.empty() ? nullptr : &script, ...);
}
```

### 2. Prism 的数据结构

Prism 使用 Double-Array Trie 存储拼写映射：

```cpp
// prism.h:24-32
struct SpellingDescriptor {
  SyllableId syllable_id;      // 音节ID
  int32_t type;                // 拼写类型(Normal/Fuzzy/Abbrev等)
  Credibility credibility;     // 可信度
  String tips;                 // 提示信息
};

using SpellingMapItem = List<SpellingDescriptor>;
using SpellingMap = Array<SpellingMapItem>;  // 拼写ID -> 音节列表
```

**关键点**：每个拼写（如 `bi`）映射到多个音节描述符，包括：
- 原始音节：`bi` → `bi` (type=Normal)
- 派生音节：`bi` → `ni` (type=Derived, 因为 `n↔b`)

### 3. 查询流程

```cpp
// table_translator.cc:644
dict_->prism()->CommonPrefixSearch(input.substr(start_pos), &matches);

// prism.cc:252-260
void Prism::CommonPrefixSearch(const string& key, vector<Match>* result) {
  size_t num_results =
      trie_->commonPrefixSearch(key.c_str(), &result->front(), len, len);
  // 返回所有匹配的拼写ID
}

// 然后查询每个拼写ID对应的所有音节
SpellingAccessor accessor = prism->QuerySpelling(spelling_id);
while (!accessor.exhausted()) {
  SyllableId syllable_id = accessor.syllable_id();  // 包括派生的音节
  // ...
  accessor.Next();
}
```

**问题所在**：
- `RimeSetInput("bi")` 只是设置了 `context->input_ = "bi"`
- 但查询时，Prism 返回 `bi` 对应的**所有音节**（包括派生的 `ni`）
- 系统无法区分"用户手动输入的 bi" 和 "通过派生规则得到的 bi"

## 为什么会这样设计？

### 拼写运算的设计初衷

拼写运算是为了**用户友好性**：

1. **模糊音**：`zh=z`, `ch=c` → 用户输入 `z` 也能匹配 `zh`
2. **简拼**：`zhong` → `z` → 快速输入
3. **容错**：`guei=gui` → 纠正常见错误
4. **14键方案**：`i↔u`, `n↔b` → 减少按键数量

这些都是**双向映射**：
- 用户输入 `z` → 匹配 `zh` 的候选
- 用户输入 `zh` → 也匹配 `z` 的候选（如果有的话）

### 当前实现的局限

**问题**：Prism 不区分"输入方向"

- 编译时：`bi` 派生 `ni`（因为 `b→n`）
- 运行时：查询 `bi` 返回 `{bi, ni}` 两个音节
- 无法表达："我输入的是 `bi`，不要给我 `ni` 的候选"

## 解决方案

### 方案一：添加"精确匹配"模式（推荐）

#### 核心思路

在 Context 中添加一个标志，指示当前输入是否为"精确输入"（禁用派生）。

#### 实现步骤

##### 1. 扩展 Context API

```cpp
// context.h
class Context {
 public:
  // 新增：设置输入并指定是否精确匹配
  void set_input(const string& value, bool exact_match = false);
  
  // 新增：查询当前是否为精确匹配模式
  bool is_exact_match() const { return exact_match_; }
  
 private:
  bool exact_match_ = false;  // 默认 false，保持兼容性
};
```

```cpp
// context.cc
void Context::set_input(const string& value, bool exact_match) {
  input_ = value;
  caret_pos_ = input_.length();
  exact_match_ = exact_match;
  update_notifier_(this);
}
```

##### 2. 修改 RimeSetInput API

```cpp
// rime_api_impl.h
RIME_DEPRECATED Bool RimeSetInput(RimeSessionId session_id, const char* input) {
  return RimeSetInputEx(session_id, input, False);  // 默认非精确模式
}

// 新增：支持精确匹配的版本
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
```

##### 3. 在 Translator 中应用精确匹配逻辑

```cpp
// table_translator.cc
void TableTranslator::Query(...) {
  // ... 现有代码 ...
  
  if (dict_ && dict_->loaded()) {
    vector<Prism::Match> matches;
    dict_->prism()->CommonPrefixSearch(input.substr(start_pos), &matches);
    
    for (const auto& m : boost::adaptors::reverse(matches)) {
      // ... 现有代码 ...
      
      // 查询拼写对应的音节
      auto spelling_accessor = dict_->prism()->QuerySpelling(m.value);
      while (!spelling_accessor.exhausted()) {
        auto props = spelling_accessor.properties();
        
        // 【关键修改】：精确匹配模式下，过滤派生拼写
        if (ctx->is_exact_match() && 
            props.type != kNormalSpelling) {
          spelling_accessor.Next();
          continue;  // 跳过 Fuzzy/Abbreviation/Derived 类型
        }
        
        SyllableId syllable_id = spelling_accessor.syllable_id();
        // ... 使用 syllable_id 查询词条 ...
        
        spelling_accessor.Next();
      }
    }
  }
}
```

#### 优点

1. **向后兼容**：默认行为不变，现有代码无需修改
2. **精确控制**：筛选器可以明确指定精确匹配
3. **实现简单**：只需修改少量代码
4. **性能良好**：只是添加类型检查，不影响性能

#### 缺点

1. 需要修改多个 Translator（TableTranslator, ScriptTranslator 等）
2. 需要添加新的 API（`RimeSetInputEx`）

---

### 方案二：引入"拼写类型过滤器"

#### 核心思路

允许用户通过配置或 API 指定允许的拼写类型。

#### 实现步骤

##### 1. 扩展 Context

```cpp
// context.h
class Context {
 public:
  // 设置允许的拼写类型（位掩码）
  void set_allowed_spelling_types(int types);
  int allowed_spelling_types() const { return allowed_spelling_types_; }
  
 private:
  // 默认允许所有类型
  int allowed_spelling_types_ = 0xFF;
};
```

##### 2. 在 Translator 中应用

```cpp
// table_translator.cc
auto props = spelling_accessor.properties();
int type_mask = 1 << props.type;

if (!(ctx->allowed_spelling_types() & type_mask)) {
  spelling_accessor.Next();
  continue;  // 跳过不允许的类型
}
```

##### 3. API 封装

```cpp
// rime_api.h
// 拼写类型常量
#define RIME_SPELLING_NORMAL       (1 << 0)
#define RIME_SPELLING_FUZZY        (1 << 1)
#define RIME_SPELLING_ABBREVIATION (1 << 2)
#define RIME_SPELLING_COMPLETION   (1 << 3)
#define RIME_SPELLING_AMBIGUOUS    (1 << 4)
#define RIME_SPELLING_ALL          0xFF

// 新增 API
Bool RimeSetAllowedSpellingTypes(RimeSessionId session_id, int types);
```

#### 优点

1. **灵活性高**：可以精确控制允许哪些类型
2. **可配置**：可以在 schema 中配置默认行为
3. **扩展性好**：未来可以添加更多类型

#### 缺点

1. **复杂度高**：需要理解拼写类型系统
2. **API 复杂**：位掩码对用户不友好

---

### 方案三：修改 Prism 数据结构（不推荐）

#### 核心思路

在 Prism 中区分"正向派生"和"反向派生"。

#### 问题

1. **破坏兼容性**：需要重新编译所有词典
2. **实现复杂**：需要修改编译器和查询逻辑
3. **性能影响**：增加存储空间和查询时间

---

## 推荐方案：方案一（精确匹配模式）

### 实现清单

#### 1. 修改 Context

**文件**：`src/rime/context.h`

```cpp
class Context {
 public:
  void set_input(const string& value, bool exact_match = false);
  bool is_exact_match() const { return exact_match_; }
  
 private:
  bool exact_match_ = false;
};
```

**文件**：`src/rime/context.cc`

```cpp
void Context::set_input(const string& value, bool exact_match) {
  input_ = value;
  caret_pos_ = input_.length();
  exact_match_ = exact_match;
  update_notifier_(this);
}
```

#### 2. 修改 API

**文件**：`src/rime_api.h`

```c
// 新增：支持精确匹配的输入设置
RIME_API Bool RimeSetInputEx(RimeSessionId session_id, 
                              const char* input,
                              Bool exact_match);
```

**文件**：`src/rime_api_impl.h`

```cpp
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

// 保持向后兼容
RIME_DEPRECATED Bool RimeSetInput(RimeSessionId session_id, const char* input) {
  return RimeSetInputEx(session_id, input, False);
}
```

#### 3. 修改 TableTranslator

**文件**：`src/rime/gear/table_translator.cc`

在 `Query` 方法中，查询 Prism 后添加类型过滤：

```cpp
if (dict_ && dict_->loaded()) {
  vector<Prism::Match> matches;
  dict_->prism()->CommonPrefixSearch(input.substr(start_pos), &matches);
  
  for (const auto& m : boost::adaptors::reverse(matches)) {
    if (m.length == 0)
      continue;
    
    // 查询拼写对应的音节
    auto spelling_accessor = dict_->prism()->QuerySpelling(m.value);
    
    while (!spelling_accessor.exhausted()) {
      auto props = spelling_accessor.properties();
      
      // 精确匹配模式：只接受 Normal 类型的拼写
      if (ctx->is_exact_match() && props.type != kNormalSpelling) {
        spelling_accessor.Next();
        continue;
      }
      
      SyllableId syllable_id = spelling_accessor.syllable_id();
      
      // ... 现有的词条查询逻辑 ...
      
      spelling_accessor.Next();
    }
  }
}
```

#### 4. 修改 ScriptTranslator（如果使用）

**文件**：`src/rime/gear/script_translator.cc`

类似的修改逻辑。

### 使用示例

#### C++ 代码

```cpp
// 普通输入（允许派生）
RimeSetInput(session_id, "bu");
// 候选：bi, bu, ni, nu

// 精确输入（禁用派生）
RimeSetInputEx(session_id, "bi", True);
// 候选：只有 bi
```

#### 筛选器中使用

```javascript
// JavaScript Filter
function filter(input, env) {
  // 获取用户选择的候选
  let selected = input.get(0);
  
  // 设置精确输入
  rime.set_input_ex(selected.preedit, true);  // 精确匹配
  
  // 重新查询
  return input;
}
```

## 测试计划

### 测试用例

#### 1. 基本功能测试

```
输入：bu
期望：bi, bu, ni, nu（派生正常）

RimeSetInputEx("bi", True)
期望：只有 bi 的候选

RimeSetInputEx("bi", False)
期望：bi, ni（派生正常）
```

#### 2. 14键方案测试

```yaml
# 配置
- derive/i/u/
- derive/n/b/

测试1：
  输入：bu
  精确：False
  期望：bi, bu, ni, nu

测试2：
  输入：bi
  精确：True
  期望：只有 bi

测试3：
  输入：ni
  精确：True
  期望：只有 ni
```

#### 3. 兼容性测试

```
测试：使用旧 API RimeSetInput
期望：行为不变（允许派生）
```

### 性能测试

- 测试精确匹配模式对查询性能的影响
- 预期：几乎无影响（只是添加类型检查）

## 潜在问题与注意事项

### 1. 拼写类型的完整性

需要确认所有拼写类型：

```cpp
enum SpellingType {
  kNormalSpelling,      // 正常拼写
  kFuzzySpelling,       // 模糊拼写（fuzz）
  kAbbreviation,        // 缩写（abbrev）
  kCompletion,          // 自动补全
  kAmbiguousSpelling,   // 歧义拼写
  kInvalidSpelling      // 无效拼写
};
```

**问题**：`derive` 生成的拼写是什么类型？

**答案**：查看源码 `src/rime/algo/calculus.cc`：

```cpp
class Derivation : public Transformation {
 public:
  bool deletion() { return false; }  // 不删除原拼写
  // 没有修改 type，所以派生拼写的 type 仍然是 kNormalSpelling
};
```

**结论**：`derive` 生成的拼写类型是 `kNormalSpelling`！

### 2. 问题的真正根源

重新审视问题：

```
输入：bu
派生：bi, bu, ni, nu

设置：RimeSetInput("bi")
问题：为什么会出现 ni 的候选？
```

**分析**：

1. 编译时：
   - `bi` 是原始音节
   - `derive/n/b/` → `bi` 派生出 `ni`
   - Prism 中：`bi` → `{bi(Normal), ni(Normal)}`

2. 运行时：
   - 查询 `bi` → 返回 `{bi, ni}` 两个音节
   - 两者类型都是 `kNormalSpelling`

**问题**：无法通过类型区分！

### 3. 更深层的问题

`derive` 规则是**双向的**：

```yaml
- derive/n/b/
```

含义：
- `n` 可以派生出 `b`
- `b` 也可以派生出 `n`

编译时：
- 音节 `bi` → 派生拼写 `ni`
- 音节 `ni` → 派生拼写 `bi`

Prism 中：
- `bi` → `{bi(原始), ni(派生)}`
- `ni` → `{ni(原始), bi(派生)}`

**查询 `bi` 时**：
- 匹配到拼写 `bi`
- 返回音节 `{bi, ni}`

**这是设计如此！**

## 真正的解决方案

### 方案四：区分"原始音节"和"派生音节"（最佳）

#### 核心思路

在 `SpellingDescriptor` 中添加标志，区分该音节是否为派生得到的。

#### 实现步骤

##### 1. 修改 Prism 数据结构

```cpp
// prism.h
struct SpellingDescriptor {
  SyllableId syllable_id;
  int32_t type;                // 拼写类型
  Credibility credibility;
  String tips;
  bool is_derived;             // 【新增】：该音节是否通过派生得到
};
```

##### 2. 修改编译器

```cpp
// dict_compiler.cc
// 在应用 Projection 时，标记派生的音节
for (auto& [spelling, syllables] : script) {
  for (auto& syll : syllables) {
    if (syll.properties.type == kNormalSpelling && 
        syll.str != spelling) {
      // 拼写和音节不同，说明是派生的
      syll.is_derived = true;
    }
  }
}
```

##### 3. 在查询时过滤

```cpp
// table_translator.cc
if (ctx->is_exact_match() && props.is_derived) {
  spelling_accessor.Next();
  continue;  // 跳过派生的音节
}
```

#### 优点

1. **精确控制**：真正区分原始和派生
2. **语义清晰**：`is_derived` 明确表达含义
3. **灵活性高**：可以选择性启用/禁用派生

#### 缺点

1. **破坏兼容性**：需要修改 Prism 格式，重新编译词典
2. **实现复杂**：需要修改编译器逻辑

---

## 最终推荐方案

### 混合方案：方案一 + 改进

由于修改 Prism 格式会破坏兼容性，推荐使用**改进的方案一**：

#### 核心思路

1. 添加 `exact_match` 标志到 Context
2. 在精确匹配模式下，**只接受拼写与音节完全匹配的结果**

#### 实现

```cpp
// table_translator.cc
auto spelling_accessor = dict_->prism()->QuerySpelling(m.value);
string spelling_str = input.substr(start_pos, m.length);  // 当前拼写

while (!spelling_accessor.exhausted()) {
  SyllableId syllable_id = spelling_accessor.syllable_id();
  string syllable_str = syllabary[syllable_id];  // 音节字符串
  
  // 精确匹配模式：拼写必须等于音节
  if (ctx->is_exact_match() && spelling_str != syllable_str) {
    spelling_accessor.Next();
    continue;  // 跳过派生的音节
  }
  
  // ... 查询词条 ...
  spelling_accessor.Next();
}
```

#### 原理

- 拼写 `bi` → 音节 `bi`：匹配 ✓
- 拼写 `bi` → 音节 `ni`：不匹配 ✗（跳过）

#### 优点

1. **无需修改 Prism 格式**：保持兼容性
2. **实现简单**：只需字符串比较
3. **效果明确**：精确控制候选

#### 缺点

1. 需要访问 syllabary（音节表）
2. 字符串比较有一定开销（但可接受）

## 实现代码

### 完整实现

见下一节的代码补丁。

## 总结

### 问题本质

`RimeSetInput` 无法禁用拼写运算的派生逻辑，因为：

1. 拼写运算在编译时完成，生成 Prism 文件
2. Prism 存储了所有派生关系
3. 查询时无法区分"用户输入"和"派生输入"

### 解决方案

添加"精确匹配"模式：

1. Context 添加 `exact_match` 标志
2. 新增 `RimeSetInputEx` API
3. Translator 中过滤派生音节（拼写≠音节）

### 影响范围

- `src/rime/context.h` 和 `.cc`
- `src/rime_api.h` 和 `rime_api_impl.h`
- `src/rime/gear/table_translator.cc`
- `src/rime/gear/script_translator.cc`（如果使用）

### 向后兼容性

- 默认行为不变（`exact_match=false`）
- 旧代码无需修改
- 新功能通过新 API 启用
