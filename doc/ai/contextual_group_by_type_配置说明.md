# contextual_group_by_type 配置说明

## 🎯 功能概述

新增配置选项 `contextual_group_by_type`，用于控制 ContextualTranslation 是否按候选类型分组排序。

---

## 📝 配置方式

### 配置文件位置

在你的输入方案配置文件中（如 `wanxiang.schema.yaml`）：

```yaml
translator:
  contextual_suggestions: true           # 启用上下文建议
  contextual_group_by_type: false        # 不按类型分组（推荐新行为）
```

---

## 🔧 配置选项说明

### contextual_group_by_type

**类型**：`boolean`

**默认值**：`true`（保持向后兼容）

**作用**：控制上下文候选排序时是否按类型分组

---

## 🔄 默认值设计说明

**为什么默认值是 `true`？**

为了保持**向后兼容性**：
- ✅ 现有用户无需修改配置
- ✅ 行为与旧版本完全一致
- ✅ 避免突然改变用户习惯
- ✅ 用户可以主动选择启用新行为

**如何启用新的排序方式？**

在配置文件中明确设置为 `false`：

```yaml
translator:
  contextual_suggestions: true
  contextual_group_by_type: false     # 启用新的完全按权重排序
```

---

## 📊 选项值说明

### `true`（默认值，兼容旧版本）

**行为**：
- 候选按类型分组
- 每组内部按上下文评分排序
- 保持类型优先级（user_phrase > phrase > user_table > table）

**优点**：
- ✅ 与旧版本行为一致
- ✅ 用户词组优先显示
- ✅ 保守策略，风险低
- ✅ 无需修改配置

**适用场景**：
- 现有用户（默认行为）
- 希望用户词组始终优先
- 保守用户

**示例**：
```
输入: "jia" → "家"
输入: "ting"

候选排序（按类型分组）:
组1 (user_phrase):
  1. 听 (weight=-24.75)           ← 用户词组优先

组2 (phrase):
  2. 庭 (weight=-6.48)            ← 虽然评分最高，但排在后面
  3. 挺 (weight=-29.82)
```

---

### `false`（推荐新行为）

**行为**：
- 候选完全按上下文评分排序
- 不考虑候选类型（phrase, user_phrase, table, user_table）
- Octagram 评分完全主导排序

**优点**：
- ✅ 真正的权重排序
- ✅ 上下文感知更准确
- ✅ 符合用户期望（如"家庭"的"庭"会排第一）
- ✅ Octagram 效果最大化

**适用场景**：
- 希望上下文评分完全主导排序
- 追求最佳的上下文感知效果
- 愿意尝试新功能的用户

**示例**：
```
输入: "jia" → "家"
输入: "ting"

候选排序（按权重）:
1. 庭 (phrase, weight=-6.48)      ← 上下文最优，排第一 ✅
2. 听 (user_phrase, weight=-24.75)
3. 挺 (phrase, weight=-29.82)
```

---

## 📊 配置对比

| 配置 | 排序方式 | 上下文效果 | 用户词组优先级 | 兼容性 | 推荐度 |
|------|---------|-----------|--------------|--------|--------|
| `true` | 按类型分组 | ⭐⭐⭐ | 高（始终优先） | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| `false` | 完全按权重 | ⭐⭐⭐⭐⭐ | 中等（取决于权重） | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |

---

## 🎨 实际案例

### 案例1：输入"家庭"

**场景**：
```
输入: "jia" → 选择 "家"
输入: "ting"
```

**候选**：
- 听 (user_phrase, 词典权重=-6.75, 上下文评分=-18.0, 最终=-24.75)
- 庭 (phrase, 词典权重=-11.84, 上下文评分=+5.36, 最终=-6.48)
- 挺 (phrase, 词典权重=-11.82, 上下文评分=-18.0, 最终=-29.82)

---

#### `contextual_group_by_type: true`（默认）

```
1. [听]  ← user_phrase 优先
2.  庭   ← 虽然上下文最优，但排第二
3.  挺
```

**用户体验**：
- ❌ 需要翻页选择"庭"
- ❌ 不符合预期（"家庭"是常见搭配）

---

#### `contextual_group_by_type: false`（推荐）

```
1. [庭]  ← 上下文最优（"家庭"是常见搭配）✅
2.  听
3.  挺
```

**用户体验**：
- ✅ 一次命中，无需翻页
- ✅ 符合预期

---

### 案例2：用户经常使用的词

**场景**：
```
输入: "wo"
上下文: "你"
```

**候选**：
- 我 (user_phrase, 词典权重=-5.0, 上下文评分=-12.0, 最终=-17.0)
- 窝 (phrase, 词典权重=-8.0, 上下文评分=-12.0, 最终=-20.0)

---

#### `contextual_group_by_type: true`（默认）

```
1. [我]  ← user_phrase 优先
2.  窝
```

---

#### `contextual_group_by_type: false`

```
1. [我]  ← 权重最高（用户词组本身权重高）✅
2.  窝
```

**结论**：即使不按类型分组，用户词组仍然优先（因为权重高）

---

## 🛠️ 配置示例

### 默认配置（兼容旧版本）

```yaml
# wanxiang.schema.yaml

translator:
  contextual_suggestions: true
  # contextual_group_by_type 未设置，使用默认值 true
```

**行为**：与旧版本完全一致

---

### 推荐配置（启用新行为）

```yaml
# wanxiang.schema.yaml

translator:
  # 基础配置
  dictionary: wanxiang
  enable_sentence: true
  enable_completion: true
  
  # 上下文配置
  contextual_suggestions: true        # 启用上下文建议
  contextual_group_by_type: false     # 不按类型分组（推荐）

grammar:
  language: zh-hans                   # 使用简体中文语法模型
  collocation_min_length: 2
  non_collocation_penalty: -18        # 无搭配惩罚（增大）
  weak_collocation_penalty: -24       # 弱搭配惩罚
  collocation_penalty: -6             # 强搭配惩罚（减小）
```

---

## 📋 迁移指南

### 从旧版本迁移

如果你之前使用的是旧版本（没有 `contextual_group_by_type` 选项）：

#### 步骤1：无需修改配置

```yaml
# 旧配置
translator:
  contextual_suggestions: true

# 新版本（默认行为相同）
translator:
  contextual_suggestions: true
  # contextual_group_by_type 默认为 true，与旧版本一致
```

**结果**：行为完全一致，无需任何修改 ✅

---

#### 步骤2：（可选）尝试新的排序方式

如果想尝试新的完全按权重排序：

```yaml
translator:
  contextual_suggestions: true
  contextual_group_by_type: false     # 启用新行为
```

---

#### 步骤3：测试

```bash
# 重新部署
./rime_deployer --build user_profile

# 测试
./rime_console
# 输入 "jia" → "家"
# 输入 "ting"
# 期望：庭 排第一（如果设置为 false）
```

---

#### 步骤4：如果不适应

如果不适应新的排序方式，可以改回：

```yaml
translator:
  contextual_suggestions: true
  contextual_group_by_type: true      # 改回默认行为（或删除此行）
```

---

## ❓ 常见问题

### Q1: 为什么默认值是 `true`？

**A**: 为了保持向后兼容性。现有用户无需修改配置，行为与旧版本完全一致。

---

### Q2: 我应该使用哪个值？

**A**: 
- **如果你是现有用户**：默认 `true` 即可，无需修改
- **如果你想要更好的上下文感知**：设置为 `false`
- **如果你不确定**：保持默认 `true`，稳定可靠

---

### Q3: 设置为 `false` 后，用户词组还会优先吗？

**A**: 会的！用户词组通常有更高的词典权重（+0.5 加成），所以在大多数情况下仍然会排在前面。只有当上下文评分差距很大时，才会被其他候选超越。

---

### Q4: 这个配置会影响性能吗？

**A**: 不会。两种模式的性能基本相同（都是 O(n log n)，n ≤ 32）。

---

### Q5: 可以动态切换吗？

**A**: 可以。修改配置后，重新部署即可：

```bash
./rime_deployer --build user_profile
```

---

## 🎯 推荐设置

### 现有用户（默认）

```yaml
translator:
  contextual_suggestions: true
  # 不设置 contextual_group_by_type，使用默认值 true
```

**理由**：
- 与旧版本行为一致
- 无需修改配置
- 稳定可靠

---

### 追求最佳体验的用户

```yaml
translator:
  contextual_suggestions: true
  contextual_group_by_type: false     # 推荐
```

**理由**：
- 更好的上下文感知
- 减少翻页次数
- 更智能的候选排序

---

### 不使用上下文建议

```yaml
translator:
  contextual_suggestions: false       # 禁用上下文建议
  # contextual_group_by_type 无效（因为上下文建议已禁用）
```

**理由**：
- 不需要上下文感知
- 追求最快速度
- 或者没有语法模型文件

---

## 📊 性能对比

| 配置 | 排序次数 | 排序数据量 | 耗时 |
|------|---------|-----------|------|
| `true` | 多次 | 每次较小 | < 1ms |
| `false` | 1次 | 最多32个 | < 1ms |

**结论**：性能差异可忽略。

---

## 🔬 调试方法

### 查看日志

启用调试日志：

```bash
export RIME_LOG_DIR=/tmp/rime_logs
./rime_console
```

查看分组信息：

```bash
tail -f /tmp/rime_logs/rime.console.*.log | grep "appending to cache"
```

**`contextual_group_by_type: true`**（默认）：
```
appending to cache 1 candidates.   ← 组1: user_phrase
appending to cache 31 candidates.  ← 组2: phrase
```

**`contextual_group_by_type: false`**：
```
appending to cache 32 candidates.  ← 只有一组
```

---

## 📝 总结

### 核心要点

1. **新增配置**：`contextual_group_by_type`
2. **默认值**：`true`（保持向后兼容）
3. **推荐设置**：`false`（更好的上下文感知）
4. **兼容性**：现有用户无需修改配置

### 选择建议

- ✅ **现有用户** → 默认 `true`（无需修改）
- ✅ **追求最佳体验** → `false`
- ✅ **不确定** → 保持默认 `true`，稳定可靠

### 配置模板

```yaml
# 默认配置（兼容旧版本）
translator:
  contextual_suggestions: true

# 推荐配置（启用新行为）
translator:
  contextual_suggestions: true
  contextual_group_by_type: false

grammar:
  language: zh-hans
  non_collocation_penalty: -18
  collocation_penalty: -6
```
