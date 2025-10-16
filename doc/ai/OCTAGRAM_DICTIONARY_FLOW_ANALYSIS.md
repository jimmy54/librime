# Octagram 词库获取与评分流程分析

## 🎯 核心问题

**Octagram 是如何获取 Translator 的词库并进行评分的？**

---

## 📊 完整调用链

### 1. 用户输入触发

```
用户输入 "nihao"
    ↓
Engine::ProcessKey()
    ↓
TableTranslator::Query()  ← 入口
```

---

## 🔍 详细流程分析

### 第一步：TableTranslator 查询词库

```cpp
// table_translator.cc L550-692
an<Translation> TableTranslator::MakeSentence(const string& input,
                                              size_t start,
                                              bool include_prefix_phrases) {
  // 1. 创建词图（WordGraph）
  WordGraph graph;  // map<int, map<int, DictEntryList>>
  
  // 2. 遍历输入的每个位置
  for (size_t start_pos = 0; start_pos < input.length(); ++start_pos) {
    
    // 3. 查询三个词库源
    
    // 3.1 用户词典（user_dict_）
    if (user_dict_ && user_dict_->loaded()) {
      UserDictEntryIterator uter;
      user_dict_->LookupWords(&uter, key, ...);
      collect_entries(homographs, uter, max_homographs_);
      // 收集到 graph[start_pos][end_pos]
    }
    
    // 3.2 编码器（encoder_）- Unity 编码
    if (encoder_ && encoder_->loaded()) {
      UserDictEntryIterator uter;
      encoder_->LookupPhrases(&uter, key, ...);
      collect_entries(homographs, uter, max_homographs_);
      // 收集到 graph[start_pos][end_pos]
    }
    
    // 3.3 系统词典（dict_）
    if (dict_ && dict_->loaded()) {
      DictEntryIterator iter;
      dict_->LookupWords(&iter, active_input.substr(0, m.length), ...);
      collect_entries(homographs, iter, max_homographs_);
      // 收集到 graph[start_pos][end_pos]
    }
  }
  
  // 4. 将词图传给 Poet 进行句子构建和评分
  if (auto sentence = poet_->MakeSentence(graph, input.length(), 
                                          GetPrecedingText(start))) {
    return sentence;
  }
}
```

---

### 第二步：构建 WordGraph（词图）

**WordGraph 的数据结构**：

```cpp
// poet.h L20
using WordGraph = map<int, map<int, DictEntryList>>;

// 结构说明：
// WordGraph[起始位置][结束位置] = DictEntryList
// 
// 例如输入 "nihao":
// graph[0][2] = [DictEntry("你"), DictEntry("泥"), ...]
// graph[0][5] = [DictEntry("你好"), DictEntry("尼好"), ...]
// graph[2][5] = [DictEntry("好"), DictEntry("号"), ...]
```

**DictEntry 包含的信息**：

```cpp
// vocabulary.h L46-66
struct DictEntry {
  string text;              // 词条文本："你好"
  string comment;           // 注释
  string preedit;           // 预编辑文本
  Code code;                // 编码（音节ID序列）
  string custom_code;       // 自定义编码
  double weight = 0.0;      // 词条权重（来自词典）⭐
  int commit_count = 0;     // 提交次数
  int remaining_code_length = 0;
  int matching_code_size = 0;
};
```

**关键点**：
- ✅ `DictEntry.weight` 是从词典中读取的**原始权重**
- ✅ 这个权重反映了词频、用户习惯等信息
- ✅ Octagram 会在这个基础上**叠加上下文评分**

---

### 第三步：Poet 使用 WordGraph 构建句子

```cpp
// poet.cc L245-252
an<Sentence> Poet::MakeSentence(const WordGraph& graph,
                                size_t total_length,
                                const string& preceding_text) {
  // 如果有语法模型，使用束搜索
  return grammar_ ? MakeSentenceWithStrategy<BeamSearch>(
                        graph, total_length, preceding_text)
                  // 否则使用动态规划
                  : MakeSentenceWithStrategy<DynamicProgramming>(
                        graph, total_length, preceding_text);
}
```

---

### 第四步：束搜索 + 上下文评分

```cpp
// poet.cc L191-242
template <class Strategy>
an<Sentence> Poet::MakeSentenceWithStrategy(const WordGraph& graph,
                                            size_t total_length,
                                            const string& preceding_text) {
  map<int, typename Strategy::State> states;
  Strategy::Initiate(states[0]);
  
  // 遍历词图的每个位置
  for (const auto& sv : graph) {
    size_t start_pos = sv.first;
    const auto& source_state = states[start_pos];
    
    // 对当前状态的候选进行扩展
    const auto update = [&](const Line& candidate) {
      // 遍历从当前位置出发的所有边
      for (const auto& ev : sv.second) {
        size_t end_pos = ev.first;
        bool is_rear = end_pos == total_length;
        
        // 遍历这条边上的所有词条（来自 WordGraph）
        const DictEntryList& entries = ev.second;  // ⭐ 这里就是词库数据
        for (const auto& entry : entries) {
          const string& context = 
              candidate.empty() ? preceding_text : candidate.context();
          
          // ⭐ 关键：计算新权重 = 前驱权重 + 词条权重 + 上下文评分
          double weight = candidate.weight +
                          Grammar::Evaluate(context, entry->text, 
                                          entry->weight,  // ← 词典权重
                                          is_rear, grammar_.get());
          
          Line new_line{&candidate, entry.get(), end_pos, weight};
          Line& best = Strategy::BestLineToUpdate(target_state, new_line);
          
          // 只保留最优路径
          if (best.empty() || compare_(best, new_line)) {
            best = new_line;
          }
        }
      }
    };
    
    // 对 Top 7 候选进行扩展
    Strategy::ForEachCandidate(source_state, compare_, update);
  }
  
  // 返回最优句子
  return sentence;
}
```

---

### 第五步：Grammar::Evaluate 计算上下文评分

```cpp
// grammar.h L18-26
inline static double Evaluate(const string& context,
                              const string& entry_text,
                              double entry_weight,      // ← 词典原始权重
                              bool is_rear,
                              Grammar* grammar) {
  const double kPenalty = -18.420680743952367;  // log(1e-8)
  
  // 最终权重 = 词典权重 + 上下文评分
  return entry_weight +
         (grammar ? grammar->Query(context, entry_text, is_rear) : kPenalty);
}
```

---

### 第六步：Octagram::Query 查询语法数据库

```cpp
// octagram.cc L106-163
double Octagram::Query(const string& context,
                       const string& word,
                       bool is_rear) {
  if (!db_ || context.empty()) {
    return config_->non_collocation_penalty;  // -12
  }
  
  double result = config_->non_collocation_penalty;
  GramDb::Match matches[GramDb::kMaxResults];
  
  // 编码上下文和词
  string context_query = grammar::encode(
      last_n_unicode(context, n, context_len),
      str_end(context));
  string word_query = grammar::encode(
      str_begin(word),
      first_n_unicode(word, n, word_query_len));
  
  // 在语法数据库中查询
  for (const char* context_ptr = str_begin(context_query);
       context_len > 0;
       --context_len, context_ptr = grammar::next_unicode(context_ptr)) {
    
    int num_results = db_->Lookup(context_ptr, word_query, matches);
    
    for (auto i = 0; i < num_results; ++i) {
      const auto& match(matches[i]);
      const int match_len = grammar::unicode_length(word_query, match.length);
      const int collocation_len = context_len + match_len;
      
      // 更新最优评分
      if (update_result(result,
                        scale_value(match.value) +
                        (collocation_len >= config_->collocation_min_length
                         ? config_->collocation_penalty      // -12
                         : config_->weak_collocation_penalty))) {  // -24
        // 找到更好的搭配
      }
    }
  }
  
  return result;  // 返回上下文评分
}
```

---

## 🎨 数据流图

```
┌─────────────────────────────────────────────────────────────┐
│ 1. TableTranslator::MakeSentence()                         │
│    - 查询三个词库源                                          │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│ 词库源 1: user_dict_ (用户词典)                             │
│   UserDictEntryIterator uter;                              │
│   user_dict_->LookupWords(&uter, key, ...);                │
│   → DictEntry { text, weight, ... }                        │
└─────────────────┬───────────────────────────────────────────┘
                  │
┌─────────────────┴───────────────────────────────────────────┐
│ 词库源 2: encoder_ (Unity 编码器)                           │
│   encoder_->LookupPhrases(&uter, key, ...);                │
│   → DictEntry { text, weight, custom_code, ... }           │
└─────────────────┬───────────────────────────────────────────┘
                  │
┌─────────────────┴───────────────────────────────────────────┐
│ 词库源 3: dict_ (系统词典)                                   │
│   DictEntryIterator iter;                                  │
│   dict_->LookupWords(&iter, key, ...);                     │
│   → DictEntry { text, weight, code, ... }                  │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. 构建 WordGraph                                           │
│    WordGraph[start_pos][end_pos] = DictEntryList           │
│                                                             │
│    例如：                                                    │
│    graph[0][2] = [你, 泥, 尼, ...]                          │
│    graph[0][5] = [你好, 尼好, ...]                          │
│    graph[2][5] = [好, 号, 毫, ...]                          │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. Poet::MakeSentence(graph, ...)                          │
│    - 接收完整的词图                                          │
│    - 使用束搜索遍历词图                                       │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. 束搜索遍历词图                                            │
│    for (const auto& entry : entries) {  ← 遍历词库数据      │
│      double weight = candidate.weight +                    │
│                      Grammar::Evaluate(                    │
│                          context,                          │
│                          entry->text,                      │
│                          entry->weight,  ← 词典原始权重     │
│                          is_rear,                          │
│                          grammar_.get());                  │
│    }                                                        │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│ 5. Grammar::Evaluate()                                      │
│    return entry_weight +  ← 词典权重                        │
│           grammar->Query(context, entry_text, is_rear);     │
│                          ↑ 上下文评分                        │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│ 6. Octagram::Query()                                        │
│    - 在语法数据库中查询上下文搭配                             │
│    - 返回上下文评分（-12 到 0 之间）                         │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│ 7. 最终权重                                                  │
│    final_weight = 词典权重 + 上下文评分                      │
│                                                             │
│    例如：                                                    │
│    "你好" 词典权重: -5.2                                     │
│    上下文评分: -12.0 (无搭配)                                │
│    最终权重: -17.2                                           │
│                                                             │
│    "你好" 词典权重: -5.2                                     │
│    上下文评分: -2.5 (强搭配)                                 │
│    最终权重: -7.7  ← 更优                                    │
└─────────────────────────────────────────────────────────────┘
```

---

## 📋 关键数据结构

### 1. WordGraph（词图）

```cpp
using WordGraph = map<int, map<int, DictEntryList>>;

// 含义：
// WordGraph[起始位置][结束位置] = 该位置段的所有词条

// 示例（输入 "nihao"）：
WordGraph graph = {
  {0, {  // 从位置 0 开始
    {2, [DictEntry("你"), DictEntry("泥"), ...]},      // 到位置 2
    {5, [DictEntry("你好"), DictEntry("尼好"), ...]},  // 到位置 5
  }},
  {2, {  // 从位置 2 开始
    {5, [DictEntry("好"), DictEntry("号"), ...]},      // 到位置 5
  }},
};
```

### 2. DictEntry（词条）

```cpp
struct DictEntry {
  string text;              // "你好"
  double weight;            // -5.2 (词典权重，log概率)
  string comment;           // 注释
  Code code;                // 音节编码
  int commit_count;         // 用户使用次数
  // ... 其他字段
};
```

### 3. Line（路径）

```cpp
struct Line {
  const Line* predecessor;  // 前驱路径
  const DictEntry* entry;   // 当前词条
  size_t end_pos;           // 结束位置
  double weight;            // 累计权重 ⭐
  
  string context() const {
    // 返回最近2个词作为上下文
    return predecessor->last_word() + last_word();
  }
};
```

---

## 🔑 核心机制总结

### 1. 词库来源（三个源）

| 词库源 | 类型 | 用途 | 查询方法 |
|--------|------|------|----------|
| **user_dict_** | UserDictionary | 用户个人词典 | `LookupWords()` |
| **encoder_** | UnityTableEncoder | 编码词典 | `LookupPhrases()` |
| **dict_** | Dictionary | 系统词典 | `LookupWords()` |

### 2. 权重计算公式

```cpp
最终权重 = 词典权重 + 上下文评分

其中：
- 词典权重：来自 DictEntry.weight（词频、用户习惯）
- 上下文评分：来自 Octagram::Query()（语法搭配）

例如：
entry->weight = -5.2  (词典权重)
contextual_score = -2.5  (上下文评分)
final_weight = -5.2 + (-2.5) = -7.7
```

### 3. 评分时机

```
TableTranslator 查询词库
    ↓
构建 WordGraph（包含所有候选词）
    ↓
Poet 遍历词图（束搜索）
    ↓
对每个词条：
    ✅ 读取词典权重（entry->weight）
    ✅ 计算上下文评分（grammar->Query()）
    ✅ 累加权重（candidate.weight + entry_weight + contextual_score）
    ✅ 只保留最优路径（Top 7）
```

---

## 💡 与 ContextualRankingFilter 的对比

### Octagram (Poet) 的方式

```cpp
// 在构建时就评分
for (const auto& entry : entries) {  // ← 遍历词库
  double weight = candidate.weight +
                  entry->weight +           // ← 词典权重
                  grammar->Query(...);      // ← 上下文评分
  
  if (weight > best.weight) {
    best = new_line;  // ← 立即更新最优
  }
}
```

**特点**：
- ✅ 直接访问 `DictEntry.weight`（词典权重）
- ✅ 在遍历词图时就评分
- ✅ 增量累加权重
- ✅ 实时剪枝（只保留最优）

### ContextualRankingFilter 的方式

```cpp
// 在候选生成后才评分
while (!translation->exhausted()) {
  auto cand = translation->Peek();  // ← 候选已生成
  
  // 重新计算评分
  double contextual_score = grammar_->Query(context, cand->text(), false);
  double total_score = cand->quality() + contextual_score;
  
  scored_candidates.push_back({cand, total_score});
}

// 然后排序
std::stable_sort(scored_candidates);
```

**特点**：
- ❌ 候选已经生成（无法访问原始 DictEntry）
- ❌ 只能通过 `Candidate.quality()` 获取权重
- ❌ 后处理评分
- ❌ 需要额外排序

---

## 🎯 关键发现

### 1. Octagram 直接访问词库数据

```cpp
// poet.cc L213
const DictEntryList& entries = ev.second;  // ← 直接访问词库
for (const auto& entry : entries) {
  // 可以访问 entry->weight, entry->text, entry->code 等所有信息
  double weight = Grammar::Evaluate(context, entry->text, entry->weight, ...);
}
```

### 2. ContextualRankingFilter 只能访问 Candidate

```cpp
// contextual_ranking_filter.cc L126
auto cand = translation->Peek();  // ← 只能访问候选
// 无法访问原始 DictEntry
// 只能通过 cand->quality() 获取权重
double total_score = cand->quality() + left_score + right_score;
```

### 3. 为什么有这个差异？

**Octagram (Poet)**：
- 在 **Translator 内部** 工作
- 直接操作 **WordGraph**（包含原始 DictEntry）
- 在 **构建阶段** 就完成评分

**ContextualRankingFilter**：
- 在 **Filter 层** 工作
- 只能访问 **Translation**（已封装的 Candidate）
- 在 **后处理阶段** 才评分

---

## 🚀 对你的实现的启示

### 问题：ContextualRankingFilter 无法直接访问词库

你的 Filter 只能访问 `Candidate`，无法像 Octagram 那样直接访问 `DictEntry`。

### 解决方案：集成到 Translator

**方案 A：扩展 TableTranslation**（推荐）

```cpp
class ContextualTableTranslation : public TableTranslation {
  an<Candidate> Peek() override {
    auto candidate = TableTranslation::Peek();
    
    // 在这里可以访问原始 DictEntry
    // 通过 PreferredEntry() 获取
    auto entry = PreferredEntry(PreferUserPhrase());
    
    // 计算上下文评分
    if (grammar_ && !context_.empty()) {
      double contextual_score = grammar_->Query(context_, entry->text, false);
      
      // 更新权重（基于原始 entry->weight）
      double new_quality = std::exp(entry->weight) + 
                          options_->initial_quality() +
                          contextual_score;  // ← 叠加上下文评分
      
      candidate->set_quality(new_quality);
    }
    
    return candidate;
  }
};
```

**方案 B：修改 TableTranslator::MakeSentence**

```cpp
// 在构建 WordGraph 后，传入上下文评分
for (auto& [start_pos, edges] : graph) {
  for (auto& [end_pos, entries] : edges) {
    for (auto& entry : entries) {
      // 在这里就叠加上下文评分
      if (grammar_) {
        double contextual_score = grammar_->Query(context, entry->text, false);
        entry->weight += contextual_score;  // ← 修改词条权重
      }
    }
  }
}

// 然后传给 Poet（权重已包含上下文评分）
poet_->MakeSentence(graph, ...);
```

---

## 📊 总结

### Octagram 获取词库的完整流程

1. **TableTranslator** 查询三个词库源（user_dict, encoder, dict）
2. 将所有词条收集到 **WordGraph** 中
3. **Poet** 接收 WordGraph，使用束搜索遍历
4. 对每个 **DictEntry**：
   - 读取词典权重（`entry->weight`）
   - 计算上下文评分（`grammar->Query()`）
   - 累加权重（`candidate.weight + entry_weight + contextual_score`）
5. 只保留最优路径（Top 7），实时剪枝
6. 返回最优句子

### 关键优势

- ✅ **直接访问词库**：可以读取 `DictEntry` 的所有信息
- ✅ **构建时评分**：在遍历词图时就完成评分
- ✅ **增量计算**：累加权重，不重复计算
- ✅ **实时剪枝**：只保留最优，避免无效计算

### 对比 ContextualRankingFilter

| 特性 | Octagram | ContextualRankingFilter |
|------|----------|------------------------|
| 访问数据 | DictEntry（原始词库） | Candidate（封装后） |
| 评分时机 | 构建时 | 后处理 |
| 权重计算 | 增量累加 | 重新计算 |
| 性能 | 高效（集成） | 低效（额外步骤） |

---

## 🎯 最终建议

**要达到 Octagram 的性能，必须在 Translator 层面集成评分，而不是在 Filter 层后处理。**

这样才能：
1. 直接访问词库数据（DictEntry）
2. 在构建时就完成评分
3. 利用增量计算和实时剪枝
4. 避免后处理的额外开销
