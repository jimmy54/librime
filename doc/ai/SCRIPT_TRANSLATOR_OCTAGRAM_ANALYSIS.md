# ScriptTranslator 与 Octagram 触发机制分析

## 🎯 核心问题

1. **ScriptTranslator 如何使用 Octagram？**
2. **什么情况下会触发 Octagram 评分？**
3. **不触发 Octagram 时，使用什么算法？**

---

## 📊 ScriptTranslator 的完整流程

### 流程图

```
用户输入 "nihao"
    ↓
ScriptTranslator::Query()
    ↓
创建 ScriptTranslation
    ↓
ScriptTranslation::Evaluate()
    ├─ 构建音节图 (SyllableGraph)
    ├─ 查询词典 (dict->Lookup)
    ├─ 查询用户词典 (user_dict->Lookup)
    └─ 判断是否需要造句
        ↓
        ├─ 有精确匹配的短语？
        │   ├─ 是 → 不造句，直接返回短语
        │   └─ 否 → 调用 MakeSentence() 造句
        │           ↓
        │           poet_->MakeSentence(graph, ...)  ← 使用 Octagram
        ↓
返回 ScriptTranslation
    ↓
判断是否启用上下文建议 (contextual_suggestions_)
    ├─ 是 → poet_->ContextualWeighted()  ← 再次使用 Octagram
    └─ 否 → 直接返回
```

---

## 🔍 详细代码分析

### 第一步：ScriptTranslator::Query()

```cpp
// script_translator.cc L196-223
an<Translation> ScriptTranslator::Query(const string& input,
                                        const Segment& segment) {
  if (!dict_ || !dict_->loaded())
    return nullptr;
  
  // 1. 创建 ScriptTranslation
  auto result = New<ScriptTranslation>(
      this, corrector_.get(), poet_.get(),  // ← 传入 Poet
      input, segment.start, end_of_input);
  
  // 2. 评估（查询词典、构建音节图）
  if (!result || !result->Evaluate(dict_.get(), 
                                   enable_user_dict ? user_dict_.get() : NULL)) {
    return nullptr;
  }
  
  // 3. 去重
  auto deduped = New<DistinctTranslation>(result);
  
  // 4. 【关键】判断是否启用上下文建议
  if (contextual_suggestions_) {
    // ⭐ 使用 Poet 进行上下文加权
    return poet_->ContextualWeighted(deduped, input, segment.start, this);
  }
  
  return deduped;
}
```

**关键点**：
- ✅ `contextual_suggestions_` 决定是否使用上下文评分
- ✅ 如果启用，调用 `poet_->ContextualWeighted()`

---

### 第二步：ScriptTranslation::Evaluate() - 判断是否造句

```cpp
// script_translator.cc L445-477
bool ScriptTranslation::Evaluate(Dictionary* dict, UserDictionary* user_dict) {
  // 1. 构建音节图
  size_t consumed = syllabifier_->BuildSyllableGraph(*dict->prism());
  const auto& syllable_graph = syllabifier_->syllable_graph();
  
  // 2. 查询词典
  phrase_ = dict->Lookup(syllable_graph, 0, &translator_->blacklist(), 
                        predict_word);
  
  // 3. 查询用户词典
  if (user_dict) {
    user_phrase_ = user_dict->Lookup(syllable_graph, 0, ...);
  }
  
  if (!phrase_ && !user_phrase_)
    return false;

  // 4. 【关键】判断是否需要造句
  bool has_at_least_two_syllables = syllable_graph.edges.size() >= 2;
  
  // ⭐ 造句的条件：
  // - 至少有2个音节
  // - 没有精确匹配的短语候选
  if (has_at_least_two_syllables &&
      !has_exact_match_phrase(phrase_, phrase_iter_, consumed) &&
      !has_exact_match_phrase(user_phrase_, user_phrase_iter_, consumed)) {
    // ⭐ 调用 MakeSentence，使用 Octagram 评分
    sentence_ = MakeSentence(dict, user_dict);
  }

  return !CheckEmpty();
}
```

**造句触发条件**：
1. ✅ 至少有 **2个音节**（单字不造句）
2. ✅ **没有精确匹配的短语**（有精确匹配就不造句）

---

### 第三步：ScriptTranslation::MakeSentence() - 使用 Octagram

```cpp
// script_translator.cc L649-673
an<Sentence> ScriptTranslation::MakeSentence(Dictionary* dict,
                                             UserDictionary* user_dict) {
  const int kMaxSyllablesForUserPhraseQuery = 5;
  const auto& syllable_graph = syllabifier_->syllable_graph();
  
  // 1. 构建 WordGraph（词图）
  WordGraph graph;
  for (const auto& x : syllable_graph.edges) {
    auto& same_start_pos = graph[x.first];
    
    // 查询用户词典
    if (user_dict) {
      EnrollEntries(same_start_pos,
                    user_dict->Lookup(syllable_graph, x.first,
                                      kMaxSyllablesForUserPhraseQuery));
    }
    
    // 查询系统词典
    EnrollEntries(same_start_pos, 
                  dict->Lookup(syllable_graph, x.first,
                               &translator_->blacklist()));
  }
  
  // 2. ⭐ 调用 Poet::MakeSentence，使用 Octagram 评分
  if (auto sentence =
          poet_->MakeSentence(graph, syllable_graph.interpreted_length,
                              translator_->GetPrecedingText(start_))) {
    sentence->Offset(start_);
    sentence->set_syllabifier(syllabifier_);
    return sentence;
  }
  return nullptr;
}
```

**关键点**：
- ✅ 构建 `WordGraph`（包含所有候选词）
- ✅ 调用 `poet_->MakeSentence()`，内部使用 Octagram 评分

---

### 第四步：Poet::ContextualWeighted() - 上下文加权

```cpp
// poet.h L43-57
template <class TranslatorT>
an<Translation> ContextualWeighted(an<Translation> translation,
                                   const string& input,
                                   size_t start,
                                   TranslatorT* translator) {
  // 1. 检查是否启用上下文建议
  if (!translator->contextual_suggestions() || !grammar_) {
    return translation;
  }
  
  // 2. 获取前置文本（上下文）
  auto preceding_text = translator->GetPrecedingText(start);
  if (preceding_text.empty()) {
    return translation;
  }
  
  // 3. ⭐ 使用 ContextualTranslation 进行上下文评分
  return New<ContextualTranslation>(translation, input, preceding_text,
                                    grammar_.get());
}
```

---

### 第五步：ContextualTranslation::Replenish() - 批量评分

```cpp
// contextual_translation.cc L11-39
bool ContextualTranslation::Replenish() {
  vector<of<Phrase>> queue;
  size_t end_pos = 0;
  std::string last_type;
  
  // 1. 收集候选（最多32个）
  while (!translation_->exhausted() &&
         cache_.size() + queue.size() < kContextualSearchLimit) {  // 32
    auto cand = translation_->Peek();
    
    // 2. 只对短语类型的候选进行评分
    if (cand->type() == "phrase" || cand->type() == "user_phrase" ||
        cand->type() == "table" || cand->type() == "user_table" ||
        cand->type() == "completion") {
      
      // 3. 按结束位置分组
      if (end_pos != cand->end() || last_type != cand->type()) {
        end_pos = cand->end();
        last_type = cand->type();
        AppendToCache(queue);  // ← 排序并添加到缓存
      }
      
      // 4. ⭐ 评分
      queue.push_back(Evaluate(As<Phrase>(cand)));
    } else {
      AppendToCache(queue);
      cache_.push_back(cand);
    }
    
    if (!translation_->Next()) {
      break;
    }
  }
  
  AppendToCache(queue);
  return !cache_.empty();
}
```

---

### 第六步：ContextualTranslation::Evaluate() - 单个候选评分

```cpp
// contextual_translation.cc L41-49
an<Phrase> ContextualTranslation::Evaluate(an<Phrase> phrase) {
  bool is_rear = phrase->end() == input_.length();
  
  // ⭐ 使用 Grammar::Evaluate 计算上下文评分
  double weight = Grammar::Evaluate(preceding_text_, phrase->text(),
                                    phrase->weight(), is_rear, grammar_);
  
  phrase->set_weight(weight);
  DLOG(INFO) << "contextual suggestion: " << phrase->text()
             << " weight: " << phrase->weight();
  return phrase;
}
```

---

### 第七步：ContextualTranslation::AppendToCache() - 排序

```cpp
// contextual_translation.cc L55-62
void ContextualTranslation::AppendToCache(vector<of<Phrase>>& queue) {
  if (queue.empty())
    return;
  
  DLOG(INFO) << "appending to cache " << queue.size() << " candidates.";
  
  // ⭐ 按权重降序排序
  std::sort(queue.begin(), queue.end(), compare_by_weight_desc);
  
  std::copy(queue.begin(), queue.end(), std::back_inserter(cache_));
  queue.clear();
}
```

---

## 🎨 两种 Octagram 使用场景

### 场景1：造句时使用 Octagram（MakeSentence）

**触发条件**：
1. ✅ 至少有 2 个音节
2. ✅ 没有精确匹配的短语

**流程**：
```
ScriptTranslation::Evaluate()
    ↓
判断：需要造句？
    ↓ 是
ScriptTranslation::MakeSentence()
    ↓
构建 WordGraph
    ↓
poet_->MakeSentence(graph, ...)
    ↓
Poet 使用束搜索 + Octagram 评分
    ↓
返回最优句子
```

**特点**：
- ⭐ 在 **构建句子时** 就使用 Octagram
- ⭐ 使用 **束搜索**（BeamSearch）算法
- ⭐ 只保留 **Top 7** 路径
- ⭐ **增量式评分**，实时剪枝

**示例**：
```
输入: "nihao"
音节: ni + hao (2个音节)
没有精确匹配的短语 "nihao"
    ↓
触发造句：
- 候选路径1: 你 + 好
- 候选路径2: 泥 + 好
- 候选路径3: 尼 + 好
    ↓
Octagram 评分（基于上下文）
    ↓
返回最优句子: "你好"
```

---

### 场景2：上下文加权时使用 Octagram（ContextualWeighted）

**触发条件**：
1. ✅ 启用 `contextual_suggestions_` 选项
2. ✅ 有前置文本（上下文）

**流程**：
```
ScriptTranslator::Query()
    ↓
判断：启用 contextual_suggestions_？
    ↓ 是
poet_->ContextualWeighted()
    ↓
创建 ContextualTranslation
    ↓
ContextualTranslation::Replenish()
    ↓
收集最多 32 个候选
    ↓
对每个候选：Grammar::Evaluate()  ← 使用 Octagram
    ↓
按结束位置分组排序
    ↓
返回重排后的候选
```

**特点**：
- ⭐ 在 **候选生成后** 进行评分
- ⭐ 批量处理（最多 32 个候选）
- ⭐ 按 **结束位置分组** 排序
- ⭐ **后处理模式**（类似 Filter）

**示例**：
```
输入: "hao"
上下文: "你"
    ↓
生成候选:
- 好 (词典权重: -5.0)
- 号 (词典权重: -6.0)
- 毫 (词典权重: -7.0)
    ↓
ContextualTranslation 评分:
- 好: -5.0 + Octagram("你", "好") = -5.0 + (-2.0) = -7.0
- 号: -6.0 + Octagram("你", "号") = -6.0 + (-12.0) = -18.0
- 毫: -7.0 + Octagram("你", "毫") = -7.0 + (-12.0) = -19.0
    ↓
排序后:
1. 好 (-7.0)  ← 最优
2. 号 (-18.0)
3. 毫 (-19.0)
```

---

## 📋 不使用 Octagram 的情况

### 情况1：单字输入

```cpp
// script_translator.cc L469-474
bool has_at_least_two_syllables = syllable_graph.edges.size() >= 2;

if (has_at_least_two_syllables && ...) {
  sentence_ = MakeSentence(dict, user_dict);  // ← 造句
}
```

**示例**：
```
输入: "ni"
音节: ni (只有1个音节)
    ↓
不造句，直接返回短语候选:
- 你
- 泥
- 尼
```

---

### 情况2：有精确匹配的短语

```cpp
// script_translator.cc L470-474
if (has_at_least_two_syllables &&
    !has_exact_match_phrase(phrase_, phrase_iter_, consumed) &&
    !has_exact_match_phrase(user_phrase_, user_phrase_iter_, consumed)) {
  sentence_ = MakeSentence(dict, user_dict);  // ← 造句
}
```

**示例**：
```
输入: "nihao"
音节: ni + hao (2个音节)
词典中有精确匹配: "nihao" → "你好"
    ↓
不造句，直接返回短语:
- 你好 (精确匹配)
```

---

### 情况3：未启用 contextual_suggestions_

```cpp
// script_translator.cc L219-222
if (contextual_suggestions_) {
  return poet_->ContextualWeighted(deduped, input, segment.start, this);
}
return deduped;  // ← 不使用上下文评分
```

**示例**：
```
配置文件:
translator:
  contextual_suggestions: false  # 未启用
    ↓
不使用 ContextualTranslation
直接返回原始候选（按词典权重排序）
```

---

### 情况4：没有上下文

```cpp
// poet.h L48-54
an<Translation> ContextualWeighted(...) {
  if (!translator->contextual_suggestions() || !grammar_) {
    return translation;
  }
  auto preceding_text = translator->GetPrecedingText(start);
  if (preceding_text.empty()) {  // ← 没有上下文
    return translation;
  }
  return New<ContextualTranslation>(...);
}
```

**示例**：
```
输入: "nihao" (第一次输入，没有历史)
上下文: 空
    ↓
不使用上下文评分
返回原始候选
```

---

## 🔑 关键配置选项

### 1. contextual_suggestions

```yaml
# schema.yaml
translator:
  contextual_suggestions: true  # 启用上下文建议
```

**作用**：
- ✅ 控制是否使用 `ContextualTranslation`
- ✅ 影响 **场景2**（上下文加权）

---

### 2. grammar/language

```yaml
# schema.yaml
grammar:
  language: zh-hans  # 语法模型语言
```

**作用**：
- ✅ 指定使用哪个语法数据库（.gram 文件）
- ✅ 影响 **场景1 和 场景2**

---

## 📊 性能对比

### 场景1：MakeSentence（造句）

| 特性 | 值 |
|------|-----|
| 触发频率 | 中等（只在需要造句时） |
| 候选数量 | 动态（束搜索，Top 7） |
| 评分方式 | 增量式（边构建边评分） |
| 算法 | 束搜索 + 动态规划 |
| 性能 | 高效（集成在构建中） |

---

### 场景2：ContextualWeighted（上下文加权）

| 特性 | 值 |
|------|-----|
| 触发频率 | 高（每次输入） |
| 候选数量 | 固定（最多 32 个） |
| 评分方式 | 批量式（后处理） |
| 算法 | 分组排序 |
| 性能 | 中等（后处理开销） |

---

## 🎯 算法对比

### 有 Octagram 时（场景1：MakeSentence）

```cpp
// poet.cc L191-242
// 使用束搜索 (BeamSearch)

for (const auto& sv : graph) {
  // 对 Top 7 候选进行扩展
  Strategy::ForEachCandidate(source_state, compare_, update);
  
  for (const auto& entry : entries) {
    // ⭐ 增量式评分
    double weight = candidate.weight +
                    Grammar::Evaluate(context, entry->text, 
                                     entry->weight, is_rear, grammar_.get());
    
    // 只保留最优
    if (best.empty() || compare_(best, new_line)) {
      best = new_line;
    }
  }
}
```

**特点**：
- ✅ 束搜索（BeamSearch）
- ✅ 保留 Top 7 路径
- ✅ 增量式评分
- ✅ 实时剪枝

---

### 无 Octagram 时（动态规划）

```cpp
// poet.cc L169-188
// 使用动态规划 (DynamicProgramming)

struct DynamicProgramming {
  using State = Line;  // 只保留一条最优路径
  
  static void ForEachCandidate(const State& state, ...) {
    update(state);  // 只更新一个候选
  }
  
  static Line& BestLineToUpdate(State& state, const Line& new_line) {
    return state;  // 直接返回状态
  }
};
```

**特点**：
- ✅ 动态规划（DP）
- ✅ 只保留 1 条最优路径
- ✅ 基于词典权重
- ✅ 无上下文评分

---

### 场景2：ContextualTranslation（分组排序）

```cpp
// contextual_translation.cc L11-62

// 1. 收集候选（最多32个）
while (!translation_->exhausted() && 
       cache_.size() + queue.size() < 32) {
  auto cand = translation_->Peek();
  
  // 2. 按结束位置分组
  if (end_pos != cand->end() || last_type != cand->type()) {
    AppendToCache(queue);  // ← 排序当前组
  }
  
  // 3. 评分
  queue.push_back(Evaluate(As<Phrase>(cand)));
}

// 4. 排序
void AppendToCache(vector<of<Phrase>>& queue) {
  std::sort(queue.begin(), queue.end(), compare_by_weight_desc);
  std::copy(queue.begin(), queue.end(), std::back_inserter(cache_));
}
```

**特点**：
- ✅ 批量处理（32个）
- ✅ 按结束位置分组
- ✅ 每组独立排序
- ✅ 后处理模式

---

## 💡 总结

### Octagram 的两种使用方式

| 场景 | 触发条件 | 算法 | 性能 | 用途 |
|------|---------|------|------|------|
| **场景1: MakeSentence** | 需要造句 | 束搜索 | 高效 | 构建最优句子 |
| **场景2: ContextualWeighted** | 启用上下文建议 | 分组排序 | 中等 | 重排候选列表 |

---

### 不使用 Octagram 的情况

1. ❌ **单字输入**（只有1个音节）
2. ❌ **有精确匹配的短语**
3. ❌ **未启用 contextual_suggestions_**
4. ❌ **没有上下文**（第一次输入）
5. ❌ **没有语法模型**（grammar_ 为空）

---

### 算法选择逻辑

```
有语法模型 (grammar_) ?
    ├─ 是 → 使用束搜索 (BeamSearch)
    │       - 保留 Top 7 路径
    │       - 使用 Octagram 评分
    │
    └─ 否 → 使用动态规划 (DynamicProgramming)
            - 只保留 1 条最优路径
            - 基于词典权重
```

---

### 关键发现

1. **不是每次输入都调用 Octagram**
   - 单字输入不调用
   - 有精确匹配不调用
   - 没有上下文不调用

2. **两种评分模式**
   - **构建时评分**（MakeSentence）- 高效
   - **后处理评分**（ContextualWeighted）- 中等

3. **ContextualTranslation 类似 Filter**
   - 批量处理候选
   - 分组排序
   - 后处理模式
   - 但比 Filter 更智能（分组、限制32个）

4. **束搜索是关键**
   - 只保留 Top 7 路径
   - 避免评估所有候选
   - 增量式评分
   - 实时剪枝

---

## 🚀 对你的实现的启示

### ContextualRankingFilter 的问题

你的 Filter 类似 **场景2**（ContextualWeighted），但有以下问题：

1. ❌ 没有分组（一次性处理所有候选）
2. ❌ 没有限制数量（可能处理很多候选）
3. ❌ 每次输入都触发（即使不需要）
4. ❌ 左右上下文都查询（2倍开销）

### 改进建议

**方案1：学习 ContextualTranslation 的分组策略**

```cpp
// 按结束位置分组
if (end_pos != cand->end()) {
  SortAndAppend(queue);  // 只排序当前组
  queue.clear();
}
queue.push_back(EvaluateCandidate(cand));
```

**方案2：限制处理数量**

```cpp
const int kMaxCandidates = 32;  // 最多处理32个
while (!translation->exhausted() && count < kMaxCandidates) {
  // ...
}
```

**方案3：智能触发**

```cpp
// 只在有上下文且候选数量合适时触发
if (context.empty() || candidate_count < 3) {
  return translation;  // 不处理
}
```

**方案4：集成到 Translator（最佳）**

像 **场景1** 那样，在构建时就评分，而不是后处理。
