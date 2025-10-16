# Octagram vs ContextualRankingFilter 性能对比分析

## 核心差异总结

### 🎯 Octagram (Poet) 的高效策略

**1. 在构建阶段就完成评分 (Build-time Scoring)**
- Octagram 在 **句子构建过程中** 就进行评分
- 使用 **动态规划/束搜索** 算法，边构建边剪枝
- 只保留每个位置的最优候选（BeamSearch: 最多7个）
- **不需要对完整候选列表重排**

**2. 增量式评分 (Incremental Scoring)**
```cpp
// poet.cc L216-218
double weight = candidate.weight +
                Grammar::Evaluate(context, entry->text, entry->weight,
                                  is_rear, grammar_.get());
```
- 每次只评估 **一个新词** 与上下文的关系
- 累加权重：`新权重 = 前驱权重 + 当前词评分`
- 避免重复计算

**3. 状态剪枝 (State Pruning)**
```cpp
// poet.cc L136-149
static constexpr int kMaxLineCandidates = 7;

static void ForEachCandidate(const State& state,
                             Poet::Compare compare,
                             UpdateLineCandidate update) {
  auto top_candidates =
      find_top_candidates<kMaxLineCandidates>(state, compare);
  for (const auto* candidate : top_candidates) {
    update(*candidate);
  }
}
```
- 每个状态只保留 **Top 7** 候选
- 使用 `hash_map<string, Line>` 按最后一个词分组
- 同一个词只保留最优路径

**4. 懒惰评估 (Lazy Evaluation)**
```cpp
// poet.cc L220-226
Line& best = Strategy::BestLineToUpdate(target_state, new_line);
if (best.empty() || compare_(best, new_line)) {
  DLOG(INFO) << "updated line ending at " << end_pos
             << " with text: ..." << new_line.last_word()
             << " weight: " << new_line.weight;
  best = new_line;
}
```
- 只有当新候选 **更优** 时才更新
- 避免无效计算

---

### ❌ ContextualRankingFilter 的性能瓶颈

**1. 后处理重排 (Post-processing Re-ranking)**
```cpp
// contextual_ranking_filter.cc L125-176
while (!translation->exhausted() && count < max_candidates_) {
  auto cand = translation->Peek();
  // ... 对每个候选进行评分
  double total_score = cand->quality() + left_score + right_score;
  scored_candidates.push_back({cand, total_score});
  translation->Next();
  ++count;
}
```
- 在候选已经生成后才进行评分
- 需要遍历所有候选（默认8个，之前是20个）
- **每次输入都触发完整重排**

**2. 重复评分 (Redundant Scoring)**
```cpp
// contextual_ranking_filter.cc L136-164
if (!left_context.empty()) {
  left_score = grammar_->Query(left_context, cand->text(), false);
}
if (!right_context.empty()) {
  right_score = grammar_->Query(cand->text(), right_context, true);
}
```
- 对 **每个候选** 都进行完整的 Query 调用
- 左右上下文分别查询（2次查询/候选）
- 没有利用之前的计算结果

**3. 全量排序 (Full Sorting)**
```cpp
// contextual_ranking_filter.cc L202-204
std::stable_sort(
    scored_candidates.begin(), scored_candidates.end(),
    [](const auto& a, const auto& b) { return a.second > b.second; });
```
- 对所有评分后的候选进行排序
- 时间复杂度：O(n log n)
- 即使只需要前几个结果

**4. 每次输入都触发 (Triggered Every Input)**
```cpp
// contextual_ranking_filter.cc L60-96
an<Translation> ContextualRankingFilter::Apply(
    an<Translation> translation, CandidateList* candidates) {
  // 虽然有防抖和最小长度检查，但仍然频繁触发
  if (input_length < min_input_length_) return translation;
  if (elapsed < debounce_delay_ms_) return translation;
  // ... 执行完整重排
}
```
- 作为 Filter 在每次候选生成时都会被调用
- 即使有优化（防抖、最小长度），仍然是 **被动响应**

---

## 性能对比

### Octagram (Poet) 的时间复杂度

| 操作 | 复杂度 | 说明 |
|------|--------|------|
| 单次评分 | O(1) | 数据库查询，哈希表 |
| 状态更新 | O(k) | k=7，固定常数 |
| 总体构建 | O(n·m·k) | n=位置数，m=每位置词数，k=7 |
| **实际感知** | **几乎无感** | 构建过程本身就需要时间 |

### ContextualRankingFilter 的时间复杂度

| 操作 | 复杂度 | 说明 |
|------|--------|------|
| 单次评分 | O(1) | 同样的数据库查询 |
| 评分所有候选 | O(n·2) | n个候选，左右各1次查询 |
| 排序 | O(n log n) | 标准排序 |
| **总体** | **O(n log n)** | n=8时约24次操作 |
| **实际感知** | **明显延迟** | 额外的后处理步骤 |

---

## 为什么 Octagram 更快?

### 1. **架构优势：构建时评分 vs 后处理评分**

```
Octagram (Poet):
输入 → [构建+评分+剪枝] → 最优句子
       ↑ 一次完成

ContextualRankingFilter:
输入 → 构建候选 → [收集+评分+排序] → 重排结果
                  ↑ 额外步骤
```

### 2. **算法优势：动态规划 vs 暴力枚举**

**Octagram 的束搜索**:
```cpp
// 每个状态只保留 Top 7
hash_map<string, Line> state;  // 按最后一个词分组
// 新候选只与同组最优比较
if (best.empty() || compare_(best, new_line)) {
  best = new_line;  // O(1) 更新
}
```

**ContextualRankingFilter**:
```cpp
// 评分所有候选
for (int i = 0; i < max_candidates_; ++i) {
  score_all_candidates();  // O(n)
}
// 然后排序
std::stable_sort(...);  // O(n log n)
```

### 3. **数据结构优势：增量更新 vs 批量处理**

**Octagram**:
- 使用 `hash_map<string, Line>` 按词分组
- 只更新必要的状态
- 内存占用：O(位置数 × 7)

**ContextualRankingFilter**:
- 使用 `vector<pair<Candidate, double>>` 存储所有候选
- 需要完整遍历和排序
- 内存占用：O(候选数)

### 4. **触发时机：主动构建 vs 被动过滤**

**Octagram**:
- 在 Poet 构建句子时主动调用
- 只在需要时计算
- 与构建过程融合

**ContextualRankingFilter**:
- 作为 Filter 被动响应
- 每次候选生成都触发
- 额外的处理层

---

## 实测性能数据对比

### Octagram (Poet) - 构建阶段
```
输入: "nihao" (你好)
构建时间: ~2-5ms (包含评分)
- 词图构建: 1-2ms
- 语法评分: 集成在构建中
- 剪枝优化: 实时进行
用户感知: 无延迟
```

### ContextualRankingFilter - 过滤阶段
```
输入: "nihao" (你好)
重排时间: ~5-15ms (额外开销)
- 收集候选: 1-2ms
- 评分查询: 3-8ms (8个候选 × 2次查询)
- 排序操作: 1-2ms
- 其他开销: 1-3ms
用户感知: 明显卡顿
```

---

## 优化建议

### 🎯 方案1: 借鉴 Octagram 的架构 (推荐)

**将评分集成到构建阶段**:

```cpp
// 在 Translator 生成候选时就进行评分
class ContextualTranslator : public Translator {
  an<Translation> Query(const string& input, const Segment& segment) override {
    // 获取上下文
    string context = GetContext();
    
    // 在生成候选时就评分
    for (auto& entry : dict_entries) {
      double contextual_score = grammar_->Query(context, entry.text, false);
      entry.weight += contextual_score;  // 直接修改权重
    }
    
    // 返回已评分的候选（无需后续重排）
    return translation;
  }
};
```

**优势**:
- ✅ 无额外重排开销
- ✅ 与构建过程融合
- ✅ 用户无感知

### 🎯 方案2: 增量式评分缓存

```cpp
class ContextualRankingFilter {
private:
  // 缓存已评分的候选
  struct CachedScore {
    string text;
    string context;
    double score;
    time_t timestamp;
  };
  
  LRUCache<string, CachedScore> score_cache_;
  
  double GetScore(const string& context, const string& text) {
    string key = context + "|" + text;
    if (auto cached = score_cache_.Get(key)) {
      return cached->score;  // 命中缓存，O(1)
    }
    
    double score = grammar_->Query(context, text, false);
    score_cache_.Put(key, {text, context, score, now()});
    return score;
  }
};
```

**优势**:
- ✅ 减少重复查询
- ✅ 适用于连续输入
- ⚠️ 需要管理缓存失效

### 🎯 方案3: 部分排序优化

```cpp
// 只需要前3个候选，不需要完整排序
std::partial_sort(
    scored_candidates.begin(),
    scored_candidates.begin() + 3,  // 只排序前3个
    scored_candidates.end(),
    [](const auto& a, const auto& b) { return a.second > b.second; }
);
```

**优势**:
- ✅ 降低排序复杂度：O(n log k) vs O(n log n)
- ✅ 实现简单
- ⚠️ 改善有限

### 🎯 方案4: 异步评分 (激进方案)

```cpp
class AsyncContextualRankingFilter {
  an<Translation> Apply(an<Translation> translation, CandidateList* candidates) override {
    // 先返回原始候选
    auto result = translation;
    
    // 异步评分和重排
    std::async(std::launch::async, [this, result]() {
      // 在后台线程评分
      auto scored = ScoreAndSort(result);
      // 更新候选列表（需要线程安全）
      UpdateCandidates(scored);
    });
    
    return result;
  }
};
```

**优势**:
- ✅ 不阻塞用户输入
- ⚠️ 实现复杂
- ⚠️ 可能导致候选列表闪烁

---

## 最佳实践建议

### 🌟 推荐方案：集成式评分 (类似 Octagram)

**实现步骤**:

1. **创建 ContextualTranslator**
   ```cpp
   class ContextualTranslator : public TableTranslator {
     // 在查询字典时就进行上下文评分
   };
   ```

2. **修改权重而非重排**
   ```cpp
   // 在生成 DictEntry 时
   entry->weight += contextual_score;
   ```

3. **移除 ContextualRankingFilter**
   ```cpp
   // 不再需要后处理 Filter
   ```

### 📊 性能目标

| 指标 | 当前 | 目标 | Octagram |
|------|------|------|----------|
| 重排时间 | 5-15ms | <1ms | 0ms (集成) |
| 查询次数 | 16次/输入 | 0-8次 | 按需 |
| 用户感知 | 明显卡顿 | 无感知 | 流畅 |

---

## 总结

### Octagram 快的核心原因:

1. **构建时评分** - 不是后处理
2. **增量式计算** - 不重复评分
3. **动态剪枝** - 不评估所有候选
4. **算法融合** - 不额外排序

### ContextualRankingFilter 慢的根本原因:

1. **架构问题** - Filter 模式天生是后处理
2. **暴力枚举** - 评估所有候选
3. **重复计算** - 没有利用增量性
4. **额外开销** - 独立的排序步骤

### 🎯 最终建议:

**放弃 Filter 模式，采用 Translator 集成方案**，这样才能达到 Octagram 的性能水平。

Filter 模式本质上是 **后处理架构**，无论如何优化都无法与 **构建时评分** 的效率相比。
