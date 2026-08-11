# Octagram 影响范围分析

## 🎯 核心问题

**Octagram 会影响所有候选词吗？还是只影响第一个候选？**

---

## 📊 结论

**Octagram 会影响所有参与句子构建的候选词，但通过束搜索(BeamSearch)策略，在每个位置只保留最优的 7 个候选路径。**

---

## 🔍 详细分析

### 1. Octagram 的调用位置

Octagram 在 **Poet::MakeSentence()** 中被调用，用于构建最优句子。

```cpp
// poet.cc L245-252
an<Sentence> Poet::MakeSentence(const WordGraph& graph,
                                size_t total_length,
                                const string& preceding_text) {
  return grammar_ ? MakeSentenceWithStrategy<BeamSearch>(graph, total_length,
                                                         preceding_text)
                  : MakeSentenceWithStrategy<DynamicProgramming>(
                        graph, total_length, preceding_text);
}
```

**关键点**：
- 如果有 `grammar_`（即 Octagram），使用 **BeamSearch** 策略
- 如果没有 `grammar_`，使用 **DynamicProgramming** 策略

---

### 2. BeamSearch 策略：每个位置保留 Top 7

```cpp
// poet.cc L133-150
struct BeamSearch {
  using State = LineCandidates;

  static constexpr int kMaxLineCandidates = 7;  // ← 关键：最多7个候选

  static void ForEachCandidate(const State& state,
                               Poet::Compare compare,
                               UpdateLineCandidate update) {
    auto top_candidates =
        find_top_candidates<kMaxLineCandidates>(state, compare);  // ← 只取前7个
    for (const auto* candidate : top_candidates) {
      update(*candidate);  // ← 对每个候选进行更新
    }
  }
  
  // ...
};
```

**关键点**：
- `kMaxLineCandidates = 7`：每个位置最多保留 **7 个候选路径**
- `find_top_candidates<7>()`：从所有候选中选出权重最高的 7 个
- 对这 **7 个候选** 分别进行扩展和评分

---

### 3. 对每个候选词都进行 Octagram 评分

```cpp
// poet.cc L203-228
const auto update = [this, &states, &sv, start_pos, total_length,
                     &preceding_text](const Line& candidate) {
  for (const auto& ev : sv.second) {
    size_t end_pos = ev.first;
    // ...
    const DictEntryList& entries = ev.second;
    for (const auto& entry : entries) {  // ← 遍历所有词条
      const string& context =
          candidate.empty() ? preceding_text : candidate.context();
      
      // ← 关键：对每个词条都调用 Octagram 评分
      double weight = candidate.weight +
                      Grammar::Evaluate(context, entry->text, entry->weight,
                                        is_rear, grammar_.get());
      
      Line new_line{&candidate, entry.get(), end_pos, weight};
      Line& best = Strategy::BestLineToUpdate(target_state, new_line);
      
      // ← 只有更优的候选才会被保留
      if (best.empty() || compare_(best, new_line)) {
        DLOG(INFO) << "updated line ending at " << end_pos
                   << " with text: ..." << new_line.last_word()
                   << " weight: " << new_line.weight;
        best = new_line;
      }
    }
  }
};
```

**关键点**：
1. **外层循环**：遍历当前位置的前 7 个最优候选路径
2. **内层循环**：对每个候选路径，遍历所有可能的下一个词条
3. **Octagram 评分**：对 **每个词条** 都调用 `Grammar::Evaluate()`
4. **动态更新**：只有权重更高的候选才会替换当前最优候选

---

### 4. Octagram::Query() 的调用

```cpp
// grammar.h L18-26
inline static double Evaluate(const string& context,
                              const string& entry_text,
                              double entry_weight,
                              bool is_rear,
                              Grammar* grammar) {
  const double kPenalty = -18.420680743952367;  // log(1e-8)
  return entry_weight +
         (grammar ? grammar->Query(context, entry_text, is_rear) : kPenalty);
         // ↑ 调用 Octagram::Query()
}
```

```cpp
// octagram.cc L107-161
double Octagram::Query(const string& context,
                       const string& word,
                       bool is_rear) {
  if (!db_ || context.empty()) {
    return config_->non_collocation_penalty;
  }
  
  double result = config_->non_collocation_penalty;
  // ... 数据库查询，计算上下文评分
  
  return result;  // ← 返回评分
}
```

**关键点**：
- `Grammar::Evaluate()` 对 **每个候选词** 都调用 `Octagram::Query()`
- `Octagram::Query()` 查询数据库，计算该词与上下文的搭配评分
- 评分结果累加到候选的权重中

---

## 📈 影响范围总结

### Octagram 影响的候选词数量

| 阶段 | 候选词数量 | 说明 |
|------|-----------|------|
| **输入** | 所有词条 | WordGraph 中所有可能的词条 |
| **BeamSearch 剪枝** | 每个位置最多 7 个 | 只保留权重最高的 7 个路径 |
| **Octagram 评分** | 7 × N 个 | 对每个路径的所有下一个词都评分 |
| **最终输出** | 1 个句子 | 权重最高的完整句子 |

### 具体例子

假设输入 `"nihao"`，WordGraph 如下：

```
位置 0-2: ["你好", "尼豪", "泥浩"]  (3个候选)
位置 0-5: ["你好啊", "尼豪啊"]      (2个候选)
位置 2-5: ["啊", "吖", "呵"]        (3个候选)
```

**Octagram 的处理流程**：

1. **位置 0 → 2**：
   - 评估 3 个候选：`["你好", "尼豪", "泥浩"]`
   - Octagram 对每个候选评分
   - 保留权重最高的 7 个（这里只有 3 个）

2. **位置 2 → 5**：
   - 从位置 2 的 3 个候选出发
   - 对每个候选，评估 3 个下一个词：`["啊", "吖", "呵"]`
   - **总共评分次数**：3 × 3 = 9 次
   - Octagram 对这 **9 个组合** 都评分
   - 保留权重最高的 7 个

3. **最终输出**：
   - 选择权重最高的完整句子，例如 `"你好啊"`

---

## 🎨 形象比喻

### Octagram 的工作方式

```
🌳 树形搜索（剪枝版）:

位置 0:
  └─ [空]

位置 2:
  ├─ 你好 (评分: 8.5) ✅
  ├─ 尼豪 (评分: 3.2) ✅
  └─ 泥浩 (评分: 1.8) ✅

位置 5:
  ├─ 你好 → 啊 (评分: 12.3) ✅ ← 最优
  ├─ 你好 → 吖 (评分: 9.1) ✅
  ├─ 你好 → 呵 (评分: 8.9) ✅
  ├─ 尼豪 → 啊 (评分: 5.5) ✅
  ├─ 尼豪 → 吖 (评分: 4.2) ✅
  ├─ 尼豪 → 呵 (评分: 3.8) ✅
  └─ 泥浩 → 啊 (评分: 3.1) ✅
  (只保留前7个)
```

**关键点**：
- ✅ **所有候选都评分**：每个可能的词组合都会被 Octagram 评分
- ✅ **动态剪枝**：每个位置只保留权重最高的 7 个路径
- ✅ **最优路径**：最终选择权重最高的完整句子

---

## 🆚 对比：Octagram vs ContextualRankingFilter

| 特性 | Octagram | ContextualRankingFilter |
|------|----------|------------------------|
| **影响范围** | 所有参与构建的候选词 | 只影响最终的 8-20 个候选 |
| **评分时机** | 构建时（边构建边评分） | 后处理（候选生成后） |
| **剪枝策略** | BeamSearch（每个位置保留 7 个） | 无剪枝（评估所有候选） |
| **评分次数** | 7 × N 次（N = 每个位置的词数） | 8-20 次（固定） |
| **最终影响** | 影响句子构建过程 | 只影响候选排序 |

---

## 💡 关键结论

### 1. Octagram 影响所有候选词

**是的**，Octagram 会对 **所有参与句子构建的候选词** 进行评分，而不是只影响第一个候选。

### 2. 但通过 BeamSearch 剪枝

虽然 Octagram 评估所有候选，但通过 **BeamSearch** 策略，每个位置只保留权重最高的 **7 个路径**，避免组合爆炸。

### 3. 影响的是句子构建，而不是候选排序

- **Octagram**：影响 **句子的构建过程**，决定哪些词组合成句子
- **ContextualRankingFilter**：影响 **候选的排序**，决定哪个候选排在前面

### 4. 评分次数对比

假设输入 `"nihao"`，有以下候选：

| 方案 | 评分次数 | 说明 |
|------|---------|------|
| **Octagram** | ~20-50 次 | 对每个位置的 7 个路径 × 每个词评分 |
| **ContextualRankingFilter** | 8-20 次 | 对最终的 8-20 个候选评分 |

**但是**：
- Octagram 的评分是 **增量式** 的（累加权重），不重复计算
- ContextualRankingFilter 的评分是 **独立** 的（每次都重新计算）

---

## 🚀 实际应用

### Octagram 的优势

1. **全局最优**：通过评估所有候选词组合，找到全局最优句子
2. **增量计算**：累加权重，避免重复计算
3. **动态剪枝**：BeamSearch 避免组合爆炸
4. **无需排序**：结果天然有序

### ContextualRankingFilter 的局限

1. **局部最优**：只对最终候选重排，无法改变句子构建
2. **重复计算**：每次都重新评分
3. **后处理开销**：额外的排序步骤
4. **影响范围小**：只影响 8-20 个候选

---

## 📝 总结

**Octagram 通过 BeamSearch 策略，对所有参与句子构建的候选词进行评分，但每个位置只保留权重最高的 7 个路径。这样既保证了全局最优性，又避免了组合爆炸。**

**形象地说**：
- Octagram 就像一个 **智能导航系统**，在每个路口都评估所有可能的路线，但只保留最优的 7 条，最终找到全局最优路径。
- ContextualRankingFilter 就像一个 **事后评审员**，在所有路线都规划好后，再对最终的几条路线进行排序。

**因此，Octagram 的影响范围远大于 ContextualRankingFilter，它影响的是整个句子的构建过程，而不仅仅是最终候选的排序。**
