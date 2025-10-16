# TableTranslator vs ScriptTranslator 上下文加权对比分析

## 🎯 核心发现

**是的！TableTranslator 也有 ContextualWeighted 上下文加权操作！**

两者使用的是 **完全相同** 的机制，都通过 `poet_->ContextualWeighted()` 实现。

---

## 📊 代码对比

### TableTranslator::Query()

```cpp
// table_translator.cc L244-309
an<Translation> TableTranslator::Query(const string& input,
                                       const Segment& segment) {
  // ... 查询词典、生成候选 ...
  
  // 1. 去重
  translation = New<DistinctTranslation>(translation);
  
  // 2. ⭐ 判断是否启用上下文建议
  if (contextual_suggestions_) {
    return poet_->ContextualWeighted(translation, input, segment.start, this);
  }
  
  return translation;
}
```

---

### ScriptTranslator::Query()

```cpp
// script_translator.cc L196-223
an<Translation> ScriptTranslator::Query(const string& input,
                                        const Segment& segment) {
  // ... 查询词典、生成候选 ...
  
  // 1. 去重
  auto deduped = New<DistinctTranslation>(result);
  
  // 2. ⭐ 判断是否启用上下文建议
  if (contextual_suggestions_) {
    return poet_->ContextualWeighted(deduped, input, segment.start, this);
  }
  
  return deduped;
}
```

---

## 🔍 完全相同的机制

### 共同点

| 特性 | TableTranslator | ScriptTranslator |
|------|----------------|------------------|
| **触发条件** | `contextual_suggestions_` | `contextual_suggestions_` |
| **调用方法** | `poet_->ContextualWeighted()` | `poet_->ContextualWeighted()` |
| **处理位置** | Query() 末尾 | Query() 末尾 |
| **前置步骤** | DistinctTranslation 去重 | DistinctTranslation 去重 |
| **算法** | ContextualTranslation（分组排序） | ContextualTranslation（分组排序） |
| **最多处理** | 32 个候选 | 32 个候选 |

---

## 📋 完整流程对比

### TableTranslator 的完整流程

```
用户输入 "nihao"
    ↓
TableTranslator::Query()
    ↓
1. 查询词典
   ├─ dict_->LookupWords()        (系统词典)
   ├─ user_dict_->LookupWords()   (用户词典)
   └─ encoder_->LookupPhrases()   (编码器)
    ↓
2. 创建 TableTranslation
    ↓
3. 判断是否需要造句
   ├─ enable_sentence_ = true ?
   │   └─ 是 → MakeSentence()  ← 使用 Octagram 造句
   └─ 否 → 跳过
    ↓
4. 去重
   translation = New<DistinctTranslation>(translation)
    ↓
5. ⭐ 判断是否启用上下文建议
   if (contextual_suggestions_) {
     return poet_->ContextualWeighted(...)  ← 使用 Octagram 重排
   }
    ↓
返回候选列表
```

---

### ScriptTranslator 的完整流程

```
用户输入 "nihao"
    ↓
ScriptTranslator::Query()
    ↓
1. 创建 ScriptTranslation
    ↓
2. ScriptTranslation::Evaluate()
   ├─ 构建音节图
   ├─ 查询词典
   └─ 判断是否需要造句
       └─ MakeSentence()  ← 使用 Octagram 造句
    ↓
3. 去重
   auto deduped = New<DistinctTranslation>(result)
    ↓
4. ⭐ 判断是否启用上下文建议
   if (contextual_suggestions_) {
     return poet_->ContextualWeighted(...)  ← 使用 Octagram 重排
   }
    ↓
返回候选列表
```

---

## 🎨 两者的差异

虽然都使用 `ContextualWeighted`，但在 **造句** 方面有差异：

### 差异1：造句的触发条件

#### TableTranslator
```cpp
// table_translator.cc L293-299
if (enable_sentence_ && !translation) {
  // ⭐ 只在没有候选时才造句
  translation = MakeSentence(input, segment.start, true);
} else if (sentence_over_completion_ && starts_with_completion(translation)) {
  // ⭐ 或者在补全候选之前插入句子
  if (auto sentence = MakeSentence(input, segment.start)) {
    translation = sentence + translation;
  }
}
```

**触发条件**：
1. `enable_sentence_ = true` **且** 没有候选
2. **或** `sentence_over_completion_ = true` **且** 有补全候选

---

#### ScriptTranslator
```cpp
// script_translator.cc L468-474
bool has_at_least_two_syllables = syllable_graph.edges.size() >= 2;
if (has_at_least_two_syllables &&
    !has_exact_match_phrase(phrase_, phrase_iter_, consumed) &&
    !has_exact_match_phrase(user_phrase_, user_phrase_iter_, consumed)) {
  // ⭐ 在没有精确匹配时造句
  sentence_ = MakeSentence(dict, user_dict);
}
```

**触发条件**：
1. 至少有 **2个音节**
2. **没有精确匹配的短语**

---

### 差异2：造句的实现方式

#### TableTranslator::MakeSentence()
```cpp
// table_translator.cc L550-692
an<Translation> TableTranslator::MakeSentence(const string& input,
                                              size_t start,
                                              bool include_prefix_phrases) {
  WordGraph graph;
  
  // 遍历输入的每个位置
  for (size_t start_pos = 0; start_pos < input.length(); ++start_pos) {
    // 查询三个词库源
    if (user_dict_) { /* 查询用户词典 */ }
    if (encoder_) { /* 查询编码器 */ }
    if (dict_) { /* 查询系统词典 */ }
  }
  
  // ⭐ 调用 Poet::MakeSentence
  if (auto sentence = poet_->MakeSentence(graph, input.length(), 
                                          GetPrecedingText(start))) {
    return Cached<SentenceTranslation>(...);
  }
  return nullptr;
}
```

**特点**：
- ✅ 基于 **字符位置** 构建词图
- ✅ 直接遍历输入字符串
- ✅ 适用于 **形码输入法**（五笔、郑码等）

---

#### ScriptTranslation::MakeSentence()
```cpp
// script_translator.cc L649-673
an<Sentence> ScriptTranslation::MakeSentence(Dictionary* dict,
                                             UserDictionary* user_dict) {
  const auto& syllable_graph = syllabifier_->syllable_graph();
  WordGraph graph;
  
  // 遍历音节图的每个位置
  for (const auto& x : syllable_graph.edges) {
    auto& same_start_pos = graph[x.first];
    if (user_dict) { /* 查询用户词典 */ }
    // 查询系统词典
    EnrollEntries(same_start_pos, dict->Lookup(syllable_graph, x.first, ...));
  }
  
  // ⭐ 调用 Poet::MakeSentence
  if (auto sentence = poet_->MakeSentence(graph, 
                                          syllable_graph.interpreted_length,
                                          translator_->GetPrecedingText(start_))) {
    return sentence;
  }
  return nullptr;
}
```

**特点**：
- ✅ 基于 **音节图** 构建词图
- ✅ 遍历音节边界
- ✅ 适用于 **音码输入法**（拼音、注音等）

---

## 🔑 关键发现

### 1. 上下文加权机制完全相同

两者都使用 `poet_->ContextualWeighted()`，这意味着：

```cpp
// poet.h L43-57
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
  // ⭐ 使用 ContextualTranslation 进行上下文评分
  return New<ContextualTranslation>(translation, input, preceding_text,
                                    grammar_.get());
}
```

**共同特点**：
- ✅ 都检查 `contextual_suggestions_` 选项
- ✅ 都获取前置文本（上下文）
- ✅ 都使用 `ContextualTranslation` 重排
- ✅ 都最多处理 32 个候选
- ✅ 都按结束位置分组排序

---

### 2. 造句机制有所不同

| 特性 | TableTranslator | ScriptTranslator |
|------|----------------|------------------|
| **词图基础** | 字符位置 | 音节图 |
| **触发条件** | 没有候选 / 补全优先 | 2+音节 + 无精确匹配 |
| **适用场景** | 形码输入法 | 音码输入法 |
| **实现位置** | TableTranslator::MakeSentence | ScriptTranslation::MakeSentence |

---

### 3. 两者都有两种 Octagram 使用方式

#### 方式1：造句时使用（MakeSentence）

**TableTranslator**:
```cpp
if (enable_sentence_ && !translation) {
  translation = MakeSentence(input, segment.start, true);
  // ↑ 内部调用 poet_->MakeSentence(graph, ...)
}
```

**ScriptTranslator**:
```cpp
if (has_at_least_two_syllables && !has_exact_match_phrase(...)) {
  sentence_ = MakeSentence(dict, user_dict);
  // ↑ 内部调用 poet_->MakeSentence(graph, ...)
}
```

---

#### 方式2：上下文加权（ContextualWeighted）

**TableTranslator**:
```cpp
if (contextual_suggestions_) {
  return poet_->ContextualWeighted(translation, input, segment.start, this);
}
```

**ScriptTranslator**:
```cpp
if (contextual_suggestions_) {
  return poet_->ContextualWeighted(deduped, input, segment.start, this);
}
```

**完全相同！** ✅

---

## 📊 触发场景对比

### TableTranslator 的触发场景

#### 场景1：造句（MakeSentence）

**示例1：没有候选时造句**
```
输入: "woshini" (五笔编码)
词典: 没有 "woshini" 这个编码
    ↓
enable_sentence_ = true ✅
translation = nullptr ✅
    ↓
⭐ 触发造句
    ↓
MakeSentence():
  构建词图（基于字符位置）
  poet_->MakeSentence(graph, ...)
    ↓
返回: "我是你"
```

**示例2：补全优先时造句**
```
输入: "wosh" (五笔编码，未完成)
词典: 有补全候选 "我是" (completion)
    ↓
sentence_over_completion_ = true ✅
starts_with_completion(translation) = true ✅
    ↓
⭐ 触发造句
    ↓
MakeSentence():
  构建完整句子
    ↓
返回: sentence + completion
  "我是你" + "我是"
```

---

#### 场景2：上下文加权（ContextualWeighted）

```
配置: contextual_suggestions: true ✅
输入: "hao" (五笔编码)
上下文: "你" ✅
    ↓
⭐ 触发上下文加权
    ↓
ContextualTranslation::Replenish():
  收集候选（最多32个）
  评分: Octagram("你", "好")
  排序
    ↓
返回: "好" 排第一
```

---

### ScriptTranslator 的触发场景

#### 场景1：造句（MakeSentence）

```
输入: "woshini" (拼音)
音节: wo + shi + ni (3个音节) ✅
词典: 没有 "woshini" 这个词 ✅
    ↓
⭐ 触发造句
    ↓
MakeSentence():
  构建词图（基于音节图）
  poet_->MakeSentence(graph, ...)
    ↓
返回: "我是你"
```

---

#### 场景2：上下文加权（ContextualWeighted）

```
配置: contextual_suggestions: true ✅
输入: "hao" (拼音)
上下文: "你" ✅
    ↓
⭐ 触发上下文加权
    ↓
ContextualTranslation::Replenish():
  收集候选（最多32个）
  评分: Octagram("你", "好")
  排序
    ↓
返回: "好" 排第一
```

---

## 💡 配置方式

### 启用上下文建议（两者相同）

```yaml
# schema.yaml
translator:
  contextual_suggestions: true  # 启用上下文加权
```

### 启用造句（TableTranslator）

```yaml
# schema.yaml
translator:
  enable_sentence: true           # 启用造句
  sentence_over_completion: true  # 句子优先于补全
```

### 启用造句（ScriptTranslator）

**无需配置**，自动根据条件触发：
- 音节数 >= 2
- 没有精确匹配

---

## 🎯 总结

### 核心结论

**TableTranslator 和 ScriptTranslator 都有 ContextualWeighted 上下文加权操作！**

两者使用的是 **完全相同** 的机制：
- ✅ 都通过 `poet_->ContextualWeighted()` 实现
- ✅ 都使用 `ContextualTranslation` 重排候选
- ✅ 都最多处理 32 个候选
- ✅ 都按结束位置分组排序
- ✅ 都使用 Octagram 进行上下文评分

---

### 主要差异

| 特性 | TableTranslator | ScriptTranslator |
|------|----------------|------------------|
| **上下文加权** | ✅ 相同 | ✅ 相同 |
| **造句基础** | 字符位置 | 音节图 |
| **造句触发** | 没有候选 / 补全优先 | 2+音节 + 无精确匹配 |
| **适用输入法** | 形码（五笔、郑码） | 音码（拼音、注音） |

---

### 两种 Octagram 使用方式（两者都有）

| 方式 | 触发条件 | 算法 | 性能 |
|------|---------|------|------|
| **造句** | 需要造句 | 束搜索 | 高效 ⚡⚡⚡ |
| **上下文加权** | 启用选项 + 有上下文 | 分组排序 | 中等 ⚡⚡ |

---

### 对你的 ContextualRankingFilter 的启示

**两个官方 Translator 都使用了 ContextualWeighted！**

这说明：
1. ✅ 上下文加权是 **通用需求**（形码、音码都需要）
2. ✅ 使用 `ContextualTranslation` 是 **标准做法**
3. ✅ 分组排序 + 限制32个是 **最佳实践**
4. ✅ 在 Query() 末尾处理是 **正确位置**

**你的 Filter 可以学习这个模式**：
- 按结束位置分组
- 限制处理数量（32个）
- 智能触发（有上下文才处理）

但 **最佳方案** 仍然是：
- 集成到 Translator（像 MakeSentence）
- 在构建时评分（而不是后处理）
