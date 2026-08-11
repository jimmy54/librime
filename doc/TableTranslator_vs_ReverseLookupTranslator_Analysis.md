# TableTranslator vs ReverseLookupTranslator 设计差异分析

## 概述

本文档深入分析了 librime 中两个核心翻译器的设计差异,解释了为什么 `TableTranslator` 支持上下文感知排序,而 `ReverseLookupTranslator` 不支持。

---

## 1. 核心设计差异

### 1.1 TableTranslator 的设计

**文件位置:** `src/rime/gear/table_translator.cc`

```cpp
// table_translator.cc 第 304-308 行
translation = New<DistinctTranslation>(translation);
if (contextual_suggestions_) {
  return poet_->ContextualWeighted(translation, input, segment.start, this);
}
return translation;
```

**关键特性:**
- ✅ **有 `poet_` 成员变量** - 在 `Initialize()` 时创建,用于上下文处理
- ✅ **有 `contextual_suggestions_` 配置项** - 控制是否启用上下文感知
- ✅ **实现了 `GetPrecedingText()` 方法** - 获取前文上下文
- ✅ **在返回前包装 Translation** - 用 `ContextualTranslation` 包装原始 Translation
- ✅ **有 `Grammar` 实例** - 通过 `Language` 对象访问语法模型

### 1.2 ReverseLookupTranslator 的设计

**文件位置:** `src/rime/gear/reverse_lookup_translator.cc`

```cpp
// reverse_lookup_translator.cc 第 194-198 行
if (!iter.exhausted()) {
  return Cached<ReverseLookupTranslation>(rev_dict_.get(), options_.get(),
                                          code, segment.start, segment.end,
                                          preedit, std::move(iter), quality);
}
return nullptr;
```

**关键特性:**
- ❌ **没有 `poet_` 成员变量** - 设计为简单查询工具
- ❌ **没有上下文处理逻辑** - 直接返回原始 Translation
- ❌ **没有实现 `GetPrecedingText()` 方法** - 不需要获取上下文
- ❌ **直接返回 Translation** - 没有任何包装或后处理
- ❌ **没有 `Grammar` 实例** - 使用简单的词典查询

---

## 2. 设计目的差异

### 2.1 TableTranslator (表格翻译器)

**用途:**
- 主要输入方法的核心组件(如五笔、拼音、双拼等)
- 用户的主要交互对象
- 需要提供最佳的用户体验

**功能需求:**
1. **智能排序** - 根据词频、上下文调整候选词顺序
2. **上下文感知** - 根据前文推荐更合适的词
3. **自动造句** - 将多个词组合成句子
4. **用户学习** - 记录用户习惯,动态调整
5. **字符集过滤** - 支持扩展字符集控制

**实现复杂度:** 高

### 2.2 ReverseLookupTranslator (反查翻译器)

**用途:**
- 辅助查询工具(如五笔用户临时用拼音查字)
- 通过 `dependencies` 配置引入
- 提供快速、简单的反向查询

**功能需求:**
1. **快速查询** - 根据辅助编码查找汉字
2. **显示提示** - 显示主编码(如五笔码)
3. **简单过滤** - 基本的候选词筛选

**实现复杂度:** 低

---

## 3. 架构层面的限制

### 3.1 librime 的翻译器架构

```
┌─────────────────────────────────────────────────────────┐
│                    Engine (引擎)                         │
│  ┌───────────────────────────────────────────────────┐  │
│  │           Segmentor (分词器)                      │  │
│  │  - 将输入分割成多个 Segment                       │  │
│  └───────────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────────┐  │
│  │           Translators (翻译器们)                  │  │
│  │  ┌─────────────────────────────────────────────┐  │  │
│  │  │ TableTranslator                             │  │  │
│  │  │  - 有 Poet 实例                             │  │  │
│  │  │  - 有 Grammar 实例                          │  │  │
│  │  │  - 返回 ContextualTranslation               │  │  │
│  │  │  - 支持造句、上下文、学习                   │  │  │
│  │  └─────────────────────────────────────────────┘  │  │
│  │  ┌─────────────────────────────────────────────┐  │  │
│  │  │ ReverseLookupTranslator                     │  │  │
│  │  │  - 无 Poet 实例                             │  │  │
│  │  │  - 无 Grammar 实例                          │  │  │
│  │  │  - 返回简单 Translation                     │  │  │
│  │  │  - 仅提供基本查询                           │  │  │
│  │  └─────────────────────────────────────────────┘  │  │
│  │  ┌─────────────────────────────────────────────┐  │  │
│  │  │ ScriptTranslator, EchoTranslator, etc.      │  │  │
│  │  └─────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────────┐  │
│  │           Filters (过滤器们)                      │  │
│  │  - Uniquifier, CharsetFilter, etc.                │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

### 3.2 关键问题

**问题所在:**
- Engine 会合并多个 Translator 的结果
- 但**不会统一应用 ContextualTranslation**
- 每个 Translator 需要**自己决定**是否应用上下文处理
- ReverseLookupTranslator 设计时**没有考虑**上下文功能

**为什么不统一处理?**
1. **性能考虑** - 上下文处理有性能开销,不是所有翻译器都需要
2. **灵活性** - 不同翻译器有不同的需求和优先级
3. **历史原因** - librime 的架构演进过程中形成的设计

---

## 4. Poet::ContextualWeighted 的工作原理

### 4.1 接口定义

**文件位置:** `src/rime/gear/poet.h`

```cpp
template <class TranslatorT>
an<Translation> ContextualWeighted(an<Translation> translation,
                                   const string& input,
                                   size_t start,
                                   TranslatorT* translator) {
  if (!translator->contextual_suggestions() || !grammar_) {
    return translation;
  }
  auto preceding_text = translator->GetPrecedingText(start);
  if (preceding_text.empty()) {
    return translation;
  }
  return New<ContextualTranslation>(translation, input, preceding_text,
                                    grammar_.get(),
                                    translator->contextual_group_by_type());
}
```

### 4.2 要求 Translator 必须实现的接口

1. **`contextual_suggestions()`** 
   - 返回类型: `bool`
   - 作用: 返回是否启用上下文感知功能

2. **`GetPrecedingText(size_t start)`**
   - 返回类型: `string`
   - 作用: 获取当前位置之前的上下文文本
   - 实现逻辑:
     - 优先使用外部上下文 (`external_preceding_text()`)
     - 否则使用内部上下文 (`composition().GetTextBefore()` 或 `commit_history().latest_text()`)

3. **`contextual_group_by_type()`**
   - 返回类型: `bool`
   - 作用: 返回是否按候选词类型分组

### 4.3 ReverseLookupTranslator 缺少的组件

| 组件 | TableTranslator | ReverseLookupTranslator | 说明 |
|------|----------------|------------------------|------|
| `poet_` | ✅ 有 | ❌ 无 | Poet 实例,用于上下文处理 |
| `grammar_` | ✅ 有(通过 Language) | ❌ 无 | 语法模型,用于计算词语搭配概率 |
| `language_` | ✅ 有(通过 Memory) | ❌ 无 | 语言模型,包含语法和词频信息 |
| `contextual_suggestions()` | ✅ 实现 | ❌ 未实现 | 配置项读取方法 |
| `GetPrecedingText()` | ✅ 实现 | ❌ 未实现 | 上下文获取方法 |
| `contextual_group_by_type()` | ✅ 实现 | ❌ 未实现 | 分组配置读取方法 |

---

## 5. 为什么强行添加会崩溃?

### 5.1 尝试的修改(已回退)

```cpp
// 在 ReverseLookupTranslator::Initialize() 中添加
poet_.reset(new Poet(NULL, config));  // ❌ 传 NULL 作为 language
```

### 5.2 崩溃原因分析

1. **Poet 需要 Language 对象**
   - `Poet` 构造函数需要 `Language*` 参数
   - `Language` 对象包含语法模型 (`Grammar`)
   - 传 `NULL` 会导致后续访问 `grammar_` 时崩溃

2. **Dictionary vs Memory 的差异**
   - `TableTranslator` 使用 `Memory` 类(继承自 `Dictionary`)
   - `Memory` 有 `language()` 方法返回 `Language*`
   - `ReverseLookupTranslator` 使用普通 `Dictionary`
   - `Dictionary` **没有** `language()` 方法

3. **GetPrecedingText 的实现问题**
   - 调用 `context->composition().GetTextBefore(start)` 可能在某些情况下崩溃
   - 需要仔细处理边界条件和异常情况

### 5.3 崩溃堆栈示例

```
Segmentation fault: 11

最后的日志:
I20251020 01:38:27.995873 reverse_lookup_translator.cc:191] input = 't', [0, 1)
[程序崩溃]
```

---

## 6. 完整对比表

| 特性 | TableTranslator | ReverseLookupTranslator | 说明 |
|------|----------------|------------------------|------|
| **设计目的** | 主要输入方法 | 辅助反查工具 | 功能定位不同 |
| **使用场景** | 日常打字输入 | 临时查询不会的字 | 使用频率差异大 |
| **Poet 实例** | ✅ 有 | ❌ 无 | 上下文处理核心 |
| **Grammar 实例** | ✅ 有 | ❌ 无 | 语法模型 |
| **Language 实例** | ✅ 有 | ❌ 无 | 语言模型 |
| **上下文处理** | ✅ 支持 | ❌ 不支持 | 核心差异 |
| **自动造句** | ✅ 支持 | ❌ 不支持 | 高级功能 |
| **用户学习** | ✅ 支持 | ❌ 不支持 | 个性化 |
| **GetPrecedingText** | ✅ 实现 | ❌ 未实现 | 上下文获取 |
| **返回类型** | ContextualTranslation | 简单 Translation | 包装层次不同 |
| **配置复杂度** | 高(20+ 配置项) | 低(5-6 配置项) | 功能丰富度 |
| **代码复杂度** | 高(~700 行) | 低(~200 行) | 实现复杂度 |
| **性能开销** | 较高 | 较低 | 功能与性能权衡 |

---

## 7. 解决方案探讨

### 7.1 方案一: 修改 Engine 架构(推荐但工作量大)

**思路:** 让 Engine 统一对所有 Translation 应用 ContextualTranslation

**伪代码:**
```cpp
// 在 Engine::TranslateSegments() 中
for (auto translator : translators) {
  auto translation = translator->Query(input, segment);
  
  // 统一应用上下文处理
  if (contextual_enabled && translation && has_grammar) {
    auto preceding_text = GetPrecedingText(segment.start);
    if (!preceding_text.empty()) {
      translation = New<ContextualTranslation>(
          translation, input, preceding_text, grammar, group_by_type);
    }
  }
  
  results.push_back(translation);
}
```

**优点:**
- ✅ 所有翻译器都能享受上下文功能
- ✅ 统一的处理逻辑,易于维护
- ✅ 不需要修改每个翻译器

**缺点:**
- ❌ 需要修改 librime 核心架构
- ❌ 可能影响性能(所有翻译器都要处理)
- ❌ 需要仔细处理兼容性问题

### 7.2 方案二: 修改词典结构(实用)

**思路:** 让 TableTranslator 同时处理五笔和拼音码

**实现方式:**
1. 词典中每个词同时包含五笔码和拼音码
2. 不需要 ReverseLookupTranslator
3. 所有候选词都通过 TableTranslator 产生

**词典示例:**
```
# wubi_pinyin.dict.yaml
家	pe	jia
庭	ytfp	ting
听	kr	ting
```

**优点:**
- ✅ 所有候选词都享受上下文功能
- ✅ 不需要修改代码
- ✅ 性能更好(只有一个翻译器)

**缺点:**
- ❌ 词典文件变大
- ❌ 需要重新生成词典
- ❌ 维护成本增加

### 7.3 方案三: 为 ReverseLookupTranslator 添加完整支持(复杂)

**思路:** 让 ReverseLookupTranslator 也支持上下文功能

**需要的修改:**
1. 添加 `poet_` 成员变量
2. 添加 `grammar_` 或 `language_` 实例
3. 实现 `GetPrecedingText()` 等方法
4. 在 `Query()` 中应用 `ContextualWeighted()`

**挑战:**
- ❌ ReverseLookupTranslator 使用 `Dictionary`,不是 `Memory`
- ❌ `Dictionary` 没有 `language()` 方法
- ❌ 需要重新设计 ReverseLookupTranslator 的架构
- ❌ 可能破坏现有的简单性设计

### 7.4 方案四: 接受现状(最简单)

**思路:** 承认这是设计限制,不是 bug

**说明:**
- 拼音反查不支持上下文排序
- 这是 librime 架构的设计决策
- 用户可以通过学习适应
- 对于偶尔使用的反查功能,影响不大

**优点:**
- ✅ 不需要任何修改
- ✅ 保持代码简洁
- ✅ 避免引入新问题

**缺点:**
- ❌ 用户体验不够完美
- ❌ 无法充分利用上下文功能

---

## 8. 实际案例分析

### 8.1 问题场景

**配置:** 五笔·拼音混合输入方案
```yaml
# wubi_pinyin.schema.yaml
translator:
  dictionary: wubi86
  enable_sentence: true
  contextual_suggestions: true  # ✅ 对五笔有效

reverse_lookup:
  dictionary: pinyin_simp
  prefix: "`"
  suffix: "'"
  contextual_suggestions: true  # ❌ 无效!
```

**测试步骤:**
1. 输入 `jia` (五笔码),选择 "家"
2. 输入 `` `ting` `` (拼音反查)

**期望结果:**
- "庭" 排在第一位(因为 "家庭" 是常见搭配)

**实际结果:**
- "听" 排在第一位(按词频排序,没有上下文)

**原因:**
- 五笔候选词通过 `TableTranslator` 产生,享受上下文排序
- 拼音候选词通过 `ReverseLookupTranslator` 产生,**不享受**上下文排序

### 8.2 日志分析

**TableTranslator 的日志:**
```
I20251020 01:41:18.735946 poet.cc:129] Lookup(家 + 和) returns 1 results
I20251020 01:41:18.735948 poet.cc:134] match[2] = 11.0359
I20251020 01:41:18.735951 poet.cc:144] update: 家[1] + 和[1] = 5.0359
I20251020 01:41:18.735954 poet.cc:159] context = 家, word = 和 / 5.0359
```
✅ 可以看到上下文计算: `context = 家, word = 和`

**ReverseLookupTranslator 的日志:**
```
I20251020 01:38:27.995873 reverse_lookup_translator.cc:191] input = 't', [0, 1)
I20251020 01:38:27.995881 dictionary.cc:294] lookup: t
I20251020 01:38:27.995973 dictionary.cc:306] found 34 matching keys
```
❌ 没有任何上下文计算的日志

---

## 9. 总结

### 9.1 核心结论

1. **TableTranslator 和 ReverseLookupTranslator 的设计目的不同**
   - TableTranslator: 主要输入方法,功能丰富
   - ReverseLookupTranslator: 辅助查询工具,简单快速

2. **上下文支持需要完整的基础设施**
   - Poet 实例
   - Grammar/Language 实例
   - GetPrecedingText() 实现
   - 配置项支持

3. **ReverseLookupTranslator 缺少这些基础设施**
   - 这是设计决策,不是疏忽
   - 强行添加会导致架构不匹配和崩溃

4. **librime 的架构不支持统一的上下文处理**
   - 每个 Translator 自己决定是否应用上下文
   - Engine 不会统一处理

### 9.2 建议

对于需要五笔拼音混合输入且希望拼音也享受上下文排序的用户:

**推荐方案:** 使用包含拼音码的词典,让 TableTranslator 同时处理五笔和拼音

**临时方案:** 接受现状,拼音反查不支持上下文排序

**长期方案:** 修改 librime 核心架构,支持统一的上下文处理

---

## 10. 参考资料

### 10.1 相关源代码文件

- `src/rime/gear/table_translator.h` - TableTranslator 头文件
- `src/rime/gear/table_translator.cc` - TableTranslator 实现
- `src/rime/gear/reverse_lookup_translator.h` - ReverseLookupTranslator 头文件
- `src/rime/gear/reverse_lookup_translator.cc` - ReverseLookupTranslator 实现
- `src/rime/gear/poet.h` - Poet 上下文处理器
- `src/rime/gear/poet.cc` - Poet 实现
- `src/rime/gear/contextual_translation.h` - ContextualTranslation 头文件
- `src/rime/gear/contextual_translation.cc` - ContextualTranslation 实现

### 10.2 相关配置项

**TableTranslator 配置:**
```yaml
translator:
  enable_sentence: true              # 启用自动造句
  contextual_suggestions: true       # 启用上下文排序
  contextual_group_by_type: false    # 是否按类型分组
  max_homophones: 5                  # 最大同音词数
  max_homographs: 5                  # 最大同形词数
```

**ReverseLookupTranslator 配置:**
```yaml
reverse_lookup:
  dictionary: pinyin_simp            # 反查词典
  prefix: "`"                        # 反查前缀
  suffix: "'"                        # 反查后缀
  tips: 〔拼音〕                      # 提示文本
  enable_completion: true            # 启用补全
  # contextual_suggestions: true     # ❌ 配置无效!
```

---

**文档版本:** 1.0  
**创建日期:** 2025-01-20  
**最后更新:** 2025-01-20  
**作者:** librime 开发团队
