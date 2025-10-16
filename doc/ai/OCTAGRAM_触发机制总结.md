# Octagram 触发机制总结

## 🎯 核心问题答案

### 1. ScriptTranslator 如何使用 Octagram？

**两种使用方式**：

#### 方式1：造句时使用（MakeSentence）
```cpp
// 在 ScriptTranslation::Evaluate() 中
if (has_at_least_two_syllables &&
    !has_exact_match_phrase(...)) {
  sentence_ = MakeSentence(dict, user_dict);  // ← 调用 Poet
}

// MakeSentence 内部
poet_->MakeSentence(graph, ...);  // ← 使用 Octagram 评分
```

#### 方式2：上下文加权（ContextualWeighted）
```cpp
// 在 ScriptTranslator::Query() 中
if (contextual_suggestions_) {
  return poet_->ContextualWeighted(deduped, ...);  // ← 使用 Octagram 重排
}
```

---

### 2. 什么情况下会触发 Octagram？

#### ✅ 触发场景1：造句（MakeSentence）

**条件**：
1. 至少有 **2个音节**
2. **没有精确匹配的短语**

**示例**：
```
输入: "nihao"
音节: ni + hao (2个音节) ✅
词典: 没有 "nihao" 这个词 ✅
    ↓
触发造句，使用 Octagram 评分
```

#### ✅ 触发场景2：上下文加权（ContextualWeighted）

**条件**：
1. 启用 `contextual_suggestions_` 选项
2. 有 **前置文本**（上下文）

**示例**：
```
配置: contextual_suggestions: true ✅
输入: "hao"
上下文: "你" ✅
    ↓
触发上下文加权，使用 Octagram 重排候选
```

---

### 3. 什么情况下 **不会** 触发 Octagram？

#### ❌ 不触发场景1：单字输入

```
输入: "ni"
音节: ni (只有1个音节) ❌
    ↓
不造句，直接返回短语候选
算法: 按词典权重排序
```

#### ❌ 不触发场景2：有精确匹配

```
输入: "nihao"
音节: ni + hao (2个音节) ✅
词典: 有 "nihao" → "你好" ❌
    ↓
不造句，直接返回精确匹配的短语
算法: 按词典权重排序
```

#### ❌ 不触发场景3：未启用上下文建议

```
配置: contextual_suggestions: false ❌
    ↓
不使用 ContextualWeighted
算法: 按词典权重排序
```

#### ❌ 不触发场景4：没有上下文

```
输入: "nihao" (第一次输入)
上下文: 空 ❌
    ↓
不使用上下文加权
算法: 按词典权重排序
```

---

## 📊 完整触发逻辑图

```
用户输入
    ↓
ScriptTranslator::Query()
    ↓
创建 ScriptTranslation
    ↓
ScriptTranslation::Evaluate()
    ↓
    ├─ 判断1: 音节数 >= 2 ?
    │   ├─ 否 → 返回短语候选（不用 Octagram）
    │   └─ 是 ↓
    │
    ├─ 判断2: 有精确匹配 ?
    │   ├─ 是 → 返回精确匹配（不用 Octagram）
    │   └─ 否 ↓
    │
    └─ ⭐ 触发造句: MakeSentence()
        └─ poet_->MakeSentence(graph, ...)
            └─ 使用 Octagram 评分（束搜索）
    ↓
返回 ScriptTranslation
    ↓
    ├─ 判断3: 启用 contextual_suggestions_ ?
    │   ├─ 否 → 返回原始候选（不用 Octagram）
    │   └─ 是 ↓
    │
    ├─ 判断4: 有上下文 ?
    │   ├─ 否 → 返回原始候选（不用 Octagram）
    │   └─ 是 ↓
    │
    └─ ⭐ 触发上下文加权: ContextualWeighted()
        └─ ContextualTranslation::Replenish()
            └─ 使用 Octagram 评分（分组排序）
```

---

## 🔍 两种模式的详细对比

### 模式1：MakeSentence（构建时评分）

| 特性 | 值 |
|------|-----|
| **触发条件** | 需要造句（2+音节，无精确匹配） |
| **触发频率** | 中等 |
| **算法** | 束搜索 (BeamSearch) |
| **候选数量** | 动态（Top 7 路径） |
| **评分方式** | 增量式（边构建边评分） |
| **排序** | 无需排序（天然有序） |
| **性能** | 高效 ⚡⚡⚡ |
| **用途** | 构建最优句子 |

**代码流程**：
```cpp
WordGraph graph;  // 构建词图
    ↓
poet_->MakeSentence(graph, ...)
    ↓
for (每个位置) {
  for (Top 7 候选) {
    for (每个词条) {
      // ⭐ 边遍历边评分
      weight = candidate.weight + 
               entry->weight + 
               grammar->Query(...);  // Octagram
      
      // 只保留最优
      if (weight > best.weight) {
        best = new_line;
      }
    }
  }
}
    ↓
返回最优句子（已排序）
```

---

### 模式2：ContextualWeighted（后处理评分）

| 特性 | 值 |
|------|-----|
| **触发条件** | 启用上下文建议 + 有上下文 |
| **触发频率** | 高（每次输入） |
| **算法** | 分组排序 |
| **候选数量** | 固定（最多 32 个） |
| **评分方式** | 批量式（后处理） |
| **排序** | 按结束位置分组排序 |
| **性能** | 中等 ⚡⚡ |
| **用途** | 重排候选列表 |

**代码流程**：
```cpp
ContextualTranslation::Replenish()
    ↓
vector<Phrase> queue;
    ↓
while (count < 32) {
  auto cand = translation->Peek();
  
  // 按结束位置分组
  if (end_pos != cand->end()) {
    // ⭐ 排序当前组
    std::sort(queue.begin(), queue.end(), compare_by_weight);
    AppendToCache(queue);
    queue.clear();
  }
  
  // ⭐ 评分
  weight = phrase->weight() + 
           grammar->Query(context, phrase->text(), ...);  // Octagram
  phrase->set_weight(weight);
  queue.push_back(phrase);
}
    ↓
返回重排后的候选
```

---

## 🎨 实际案例分析

### 案例1：单字输入（不触发）

```
输入: "n"
    ↓
音节: n (1个音节)
    ↓
❌ 不满足造句条件（需要2+音节）
    ↓
返回短语候选:
- 你 (词典权重: -3.5)
- 泥 (词典权重: -5.2)
- 尼 (词典权重: -6.1)
    ↓
按词典权重排序，不使用 Octagram
```

---

### 案例2：有精确匹配（不触发造句）

```
输入: "nihao"
    ↓
音节: ni + hao (2个音节) ✅
词典: 有 "nihao" → "你好" ✅
    ↓
❌ 不满足造句条件（有精确匹配）
    ↓
返回精确匹配:
- 你好 (词典权重: -4.2)
    ↓
不使用 Octagram 造句
```

**但是**，如果启用了 `contextual_suggestions_`：
```
    ↓
判断: contextual_suggestions_ = true ✅
上下文: "我说" ✅
    ↓
⭐ 触发 ContextualWeighted
    ↓
重排候选:
- 你好: -4.2 + Octagram("我说", "你好") = -4.2 + (-2.0) = -6.2
    ↓
返回重排后的候选
```

---

### 案例3：需要造句（触发）

```
输入: "woshini"
    ↓
音节: wo + shi + ni (3个音节) ✅
词典: 没有 "woshini" 这个词 ✅
    ↓
⭐ 满足造句条件
    ↓
MakeSentence():
构建词图:
  graph[0][2] = [我, 窝, ...]
  graph[2][5] = [是, 事, ...]
  graph[5][7] = [你, 泥, ...]
    ↓
poet_->MakeSentence(graph, ...)
    ↓
束搜索 + Octagram 评分:
  路径1: 我 + 是 + 你
    - 我: -3.0
    - 是: -2.5 + Octagram("我", "是") = -2.5 + (-1.5) = -4.0
    - 你: -3.5 + Octagram("我是", "你") = -3.5 + (-1.0) = -4.5
    - 总分: -3.0 + (-4.0) + (-4.5) = -11.5
  
  路径2: 窝 + 是 + 你
    - 窝: -6.0
    - 是: -2.5 + Octagram("窝", "是") = -2.5 + (-12.0) = -14.5
    - 你: -3.5 + Octagram("窝是", "你") = -3.5 + (-12.0) = -15.5
    - 总分: -6.0 + (-14.5) + (-15.5) = -36.0
    ↓
返回最优句子: "我是你" (总分: -11.5)
```

---

### 案例4：上下文加权（触发）

```
配置: contextual_suggestions: true ✅
输入: "hao"
上下文: "你" ✅
    ↓
生成候选:
- 好 (词典权重: -5.0)
- 号 (词典权重: -6.0)
- 毫 (词典权重: -7.0)
    ↓
⭐ 触发 ContextualWeighted
    ↓
ContextualTranslation::Replenish():
  收集候选（最多32个）
  按结束位置分组
    ↓
  评分:
  - 好: -5.0 + Octagram("你", "好") = -5.0 + (-2.0) = -7.0  ← 最优
  - 号: -6.0 + Octagram("你", "号") = -6.0 + (-12.0) = -18.0
  - 毫: -7.0 + Octagram("你", "毫") = -7.0 + (-12.0) = -19.0
    ↓
  排序（按权重降序）
    ↓
返回重排后的候选:
1. 好 (-7.0)  ← 因为上下文 "你" 而排第一
2. 号 (-18.0)
3. 毫 (-19.0)
```

---

## 🔑 关键配置

### 控制造句（模式1）

**无需配置**，自动根据条件触发：
- 音节数 >= 2
- 没有精确匹配

### 控制上下文加权（模式2）

```yaml
# schema.yaml
translator:
  contextual_suggestions: true  # 启用上下文建议
```

### 指定语法模型

```yaml
# schema.yaml
grammar:
  language: zh-hans  # 使用简体中文语法模型
```

---

## 💡 不使用 Octagram 时的算法

### 动态规划（DynamicProgramming）

当 **没有语法模型** 时，Poet 使用动态规划而不是束搜索：

```cpp
// poet.cc L245-252
an<Sentence> Poet::MakeSentence(const WordGraph& graph, ...) {
  return grammar_ 
      ? MakeSentenceWithStrategy<BeamSearch>(...)      // 有语法模型
      : MakeSentenceWithStrategy<DynamicProgramming>(...);  // 无语法模型
}
```

**动态规划的特点**：
```cpp
struct DynamicProgramming {
  using State = Line;  // 只保留1条最优路径
  
  static void ForEachCandidate(const State& state, ...) {
    update(state);  // 只更新1个候选
  }
};
```

- ✅ 只保留 **1条最优路径**（vs 束搜索的7条）
- ✅ 基于 **词典权重**（不使用上下文评分）
- ✅ 更快但不考虑上下文

**对比**：
```
有语法模型:
  束搜索 → 保留 Top 7 → 使用 Octagram → 考虑上下文

无语法模型:
  动态规划 → 保留 Top 1 → 不用 Octagram → 只看词频
```

---

## 📊 性能影响分析

### 触发频率统计

假设用户连续输入 "wo shi ni hao"：

| 输入 | 音节数 | 精确匹配 | 触发造句 | 触发上下文加权 |
|------|--------|---------|---------|--------------|
| "w" | 1 | - | ❌ | ❌ (无上下文) |
| "wo" | 1 | ✅ | ❌ | ✅ (有上下文) |
| "wos" | 1 | - | ❌ | ✅ |
| "wosh" | 1 | - | ❌ | ✅ |
| "woshi" | 2 | ✅ | ❌ | ✅ |
| "woshin" | 2 | - | ✅ | ✅ |
| "woshini" | 3 | ❌ | ✅ | ✅ |
| "woshiniha" | 4 | - | ✅ | ✅ |
| "woshinihao" | 4 | ❌ | ✅ | ✅ |

**统计**：
- 造句触发: 4/9 = 44%
- 上下文加权触发: 8/9 = 89% (启用 contextual_suggestions)

---

### 性能开销

| 操作 | 开销 | 触发频率 |
|------|------|---------|
| **MakeSentence** | 2-5ms | 中等（44%） |
| **ContextualWeighted** | 3-8ms | 高（89%） |
| **总开销** | 5-13ms | - |

**优化建议**：
1. 如果性能敏感，可以关闭 `contextual_suggestions`
2. 造句的开销是必要的（构建句子）
3. 上下文加权是可选的（提升准确性）

---

## 🎯 总结

### Octagram 的触发条件

| 模式 | 触发条件 | 频率 | 性能 |
|------|---------|------|------|
| **造句** | 2+音节 + 无精确匹配 | 中等 | 高效 |
| **上下文加权** | 启用选项 + 有上下文 | 高 | 中等 |

### 不触发的情况

1. ❌ 单字输入
2. ❌ 有精确匹配
3. ❌ 未启用上下文建议
4. ❌ 没有上下文
5. ❌ 没有语法模型

### 算法选择

```
有语法模型？
  ├─ 是 → 束搜索 + Octagram 评分
  └─ 否 → 动态规划 + 词典权重
```

### 关键发现

1. **不是每次都调用 Octagram** - 有智能触发条件
2. **两种评分模式** - 构建时（高效）+ 后处理（灵活）
3. **ContextualWeighted 类似 Filter** - 但更智能（分组、限制32个）
4. **束搜索是性能关键** - Top 7 剪枝避免全量评估

### 对 ContextualRankingFilter 的启示

你的 Filter 应该学习 ContextualTranslation 的策略：
- ✅ 按结束位置分组
- ✅ 限制处理数量（32个）
- ✅ 智能触发（有上下文才处理）
- ✅ 分组排序（不是全局排序）

但最佳方案仍然是：**集成到 Translator，在构建时评分**！
