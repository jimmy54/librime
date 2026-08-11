# 代码修改总结：添加 contextual_group_by_type 配置

## 🎯 修改目标

添加配置选项 `contextual_group_by_type`，允许用户选择是否在上下文候选排序时按类型分组。

---

## 📝 修改文件清单

### 1. contextual_translation.h

**文件路径**：`src/rime/gear/contextual_translation.h`

**修改内容**：

#### 1.1 构造函数添加参数

```cpp
// 修改前
ContextualTranslation(an<Translation> translation,
                      string input,
                      string preceding_text,
                      Grammar* grammar)

// 修改后
ContextualTranslation(an<Translation> translation,
                      string input,
                      string preceding_text,
                      Grammar* grammar,
                      bool group_by_type = false)  // 新增参数，默认 false
```

#### 1.2 添加成员变量

```cpp
// 在 private 部分添加
bool group_by_type_;  // whether to group candidates by type
```

**完整修改**：
```cpp
class ContextualTranslation : public PrefetchTranslation {
 public:
  ContextualTranslation(an<Translation> translation,
                        string input,
                        string preceding_text,
                        Grammar* grammar,
                        bool group_by_type = false)
      : PrefetchTranslation(translation),
        input_(input),
        preceding_text_(preceding_text),
        grammar_(grammar),
        group_by_type_(group_by_type) {}

 protected:
  bool Replenish() override;

 private:
  an<Phrase> Evaluate(an<Phrase> phrase);
  void AppendToCache(vector<of<Phrase>>& queue);

  string input_;
  string preceding_text_;
  Grammar* grammar_;
  bool group_by_type_;  // whether to group candidates by type
};
```

---

### 2. contextual_translation.cc

**文件路径**：`src/rime/gear/contextual_translation.cc`

**修改内容**：

#### 2.1 修改分组逻辑

```cpp
// 修改前
if (end_pos != cand->end() || last_type != cand->type()) {

// 修改后
// Group by end position and optionally by type
if (end_pos != cand->end() ||
    (group_by_type_ && last_type != cand->type())) {
```

**完整代码**：
```cpp
bool ContextualTranslation::Replenish() {
  vector<of<Phrase>> queue;
  size_t end_pos = 0;
  std::string last_type;
  while (!translation_->exhausted() &&
         cache_.size() + queue.size() < kContextualSearchLimit) {
    auto cand = translation_->Peek();
    DLOG(INFO) << cand->text() << " cache/queue: " << cache_.size() << "/"
               << queue.size();
    if (cand->type() == "phrase" || cand->type() == "user_phrase" ||
        cand->type() == "table" || cand->type() == "user_table" ||
        cand->type() == "completion") {
      // Group by end position and optionally by type
      if (end_pos != cand->end() ||
          (group_by_type_ && last_type != cand->type())) {
        end_pos = cand->end();
        last_type = cand->type();
        AppendToCache(queue);
      }
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

### 3. translator_commons.h

**文件路径**：`src/rime/gear/translator_commons.h`

**修改内容**：

#### 3.1 添加 getter/setter 方法

```cpp
// 在 public 部分添加
bool contextual_group_by_type() const { return contextual_group_by_type_; }
void set_contextual_group_by_type(bool enabled) {
  contextual_group_by_type_ = enabled;
}
```

#### 3.2 添加成员变量

```cpp
// 在 protected 部分添加
bool contextual_group_by_type_ = false;  // group candidates by type in contextual translation
```

**完整修改位置**：
```cpp
class TranslatorOptions {
 public:
  // ... 其他方法 ...
  
  bool contextual_suggestions() const { return contextual_suggestions_; }
  void set_contextual_suggestions(bool enabled) {
    contextual_suggestions_ = enabled;
  }
  bool contextual_group_by_type() const { return contextual_group_by_type_; }
  void set_contextual_group_by_type(bool enabled) {
    contextual_group_by_type_ = enabled;
  }
  
  // ... 其他方法 ...

 protected:
  string delimiters_;
  vector<string> tags_{"abc"};
  bool contextual_suggestions_ = false;
  bool contextual_group_by_type_ = false;  // 新增
  bool enable_completion_ = true;
  // ... 其他成员变量 ...
};
```

---

### 4. translator_commons.cc

**文件路径**：`src/rime/gear/translator_commons.cc`

**修改内容**：

#### 4.1 在构造函数中读取配置

```cpp
// 在 TranslatorOptions::TranslatorOptions 中添加
config->GetBool(ticket.name_space + "/contextual_group_by_type",
                &contextual_group_by_type_);
```

**完整代码**：
```cpp
TranslatorOptions::TranslatorOptions(const Ticket& ticket) {
  if (!ticket.schema)
    return;
  if (Config* config = ticket.schema->config()) {
    config->GetString(ticket.name_space + "/delimiter", &delimiters_) ||
        config->GetString("speller/delimiter", &delimiters_);
    config->GetBool(ticket.name_space + "/contextual_suggestions",
                    &contextual_suggestions_);
    config->GetBool(ticket.name_space + "/contextual_group_by_type",
                    &contextual_group_by_type_);  // 新增
    config->GetBool(ticket.name_space + "/enable_completion",
                    &enable_completion_);
    // ... 其他配置读取 ...
  }
}
```

---

### 5. poet.h

**文件路径**：`src/rime/gear/poet.h`

**修改内容**：

#### 5.1 修改 ContextualWeighted 方法

```cpp
// 修改前
return New<ContextualTranslation>(translation, input, preceding_text,
                                  grammar_.get());

// 修改后
return New<ContextualTranslation>(translation, input, preceding_text,
                                  grammar_.get(),
                                  translator->contextual_group_by_type());
```

**完整代码**：
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

---

## 📊 修改统计

| 文件 | 新增行 | 修改行 | 删除行 |
|------|--------|--------|--------|
| contextual_translation.h | 2 | 1 | 0 |
| contextual_translation.cc | 1 | 1 | 0 |
| translator_commons.h | 5 | 0 | 0 |
| translator_commons.cc | 2 | 0 | 0 |
| poet.h | 1 | 1 | 0 |
| **总计** | **11** | **3** | **0** |

---

## 🔍 关键修改点

### 1. 配置传递链路

```
schema.yaml
    ↓ (读取配置)
TranslatorOptions::TranslatorOptions()
    ↓ (存储到成员变量)
contextual_group_by_type_
    ↓ (通过 getter 方法)
Poet::ContextualWeighted()
    ↓ (传递给构造函数)
ContextualTranslation::ContextualTranslation()
    ↓ (存储到成员变量)
group_by_type_
    ↓ (在 Replenish 中使用)
分组逻辑判断
```

---

### 2. 分组逻辑变化

#### 修改前（固定按类型分组）

```cpp
if (end_pos != cand->end() || last_type != cand->type()) {
  AppendToCache(queue);
}
```

**行为**：
- 结束位置不同 **OR** 类型不同 → 触发分组
- 固定行为，无法配置

---

#### 修改后（可配置）

```cpp
if (end_pos != cand->end() ||
    (group_by_type_ && last_type != cand->type())) {
  AppendToCache(queue);
}
```

**行为**：
- 结束位置不同 → 总是触发分组
- 类型不同 → **仅当 `group_by_type_` 为 true 时**触发分组
- 可通过配置控制

---

### 3. 默认值设计

**所有默认值都是 `false`**：

```cpp
// contextual_translation.h
bool group_by_type = false  // 构造函数参数默认值

// translator_commons.h
bool contextual_group_by_type_ = false;  // 成员变量默认值
```

**理由**：
- `false` 提供更好的上下文感知效果
- 符合新功能的设计目标
- 用户可以通过配置改为 `true` 保持旧行为

---

## 🧪 测试建议

### 1. 编译测试

```bash
cd /Users/jimmy54/Pictures/jimmy_librime/librime
make clean
make
```

**预期**：编译成功，无错误

---

### 2. 功能测试

#### 测试1：默认行为（不按类型分组）

```yaml
# wanxiang.schema.yaml
translator:
  contextual_suggestions: true
  # contextual_group_by_type 未设置，使用默认值 false
```

```bash
./rime_deployer --build user_profile
./rime_console

# 输入 "jia" → "家"
# 输入 "ting"
# 期望：庭 排第一
```

---

#### 测试2：按类型分组

```yaml
# wanxiang.schema.yaml
translator:
  contextual_suggestions: true
  contextual_group_by_type: true  # 明确设置为 true
```

```bash
./rime_deployer --build user_profile
./rime_console

# 输入 "jia" → "家"
# 输入 "ting"
# 期望：听 排第一（user_phrase 优先）
```

---

#### 测试3：禁用上下文建议

```yaml
# wanxiang.schema.yaml
translator:
  contextual_suggestions: false
  contextual_group_by_type: false  # 此选项无效
```

```bash
./rime_deployer --build user_profile
./rime_console

# 输入 "jia" → "家"
# 输入 "ting"
# 期望：按原始词典权重排序（无上下文评分）
```

---

### 3. 日志验证

```bash
export RIME_LOG_DIR=/tmp/rime_logs
./rime_console

# 输入测试
tail -f /tmp/rime_logs/rime.console.*.log | grep "appending to cache"
```

**`contextual_group_by_type: false`**：
```
appending to cache 32 candidates.  ← 一组
```

**`contextual_group_by_type: true`**：
```
appending to cache 1 candidates.   ← 多组
appending to cache 31 candidates.
```

---

### 4. 性能测试

```bash
# 测量响应时间
time ./rime_console < test_input.txt
```

**预期**：
- `false` 和 `true` 的性能差异 < 1ms
- 无明显性能影响

---

## 🔧 代码格式化

修改完成后，运行代码格式化：

```bash
make clang-format-apply
```

**预期**：
- 所有修改的文件符合项目代码风格
- CI 检查通过

---

## 📝 Git Commit 建议

```bash
git add src/rime/gear/contextual_translation.h
git add src/rime/gear/contextual_translation.cc
git add src/rime/gear/translator_commons.h
git add src/rime/gear/translator_commons.cc
git add src/rime/gear/poet.h

git commit -m "feat: add contextual_group_by_type config option

- Add configurable grouping behavior in ContextualTranslation
- Default to false (no type grouping) for better contextual ranking
- Users can set to true to maintain legacy behavior
- Add contextual_group_by_type option to TranslatorOptions
- Update ContextualWeighted to pass group_by_type parameter

This allows users to choose between:
- Full contextual ranking (group_by_type=false, recommended)
- Type-based grouping (group_by_type=true, legacy behavior)

Example config:
  translator:
    contextual_suggestions: true
    contextual_group_by_type: false  # recommended
"
```

---

## 🎯 向后兼容性

### 兼容性保证

1. **配置兼容**：
   - 旧配置文件不需要修改
   - 未设置 `contextual_group_by_type` 时，使用默认值 `false`

2. **行为变化**：
   - 默认行为改变（从按类型分组变为不分组）
   - 用户可以通过设置 `contextual_group_by_type: true` 恢复旧行为

3. **API 兼容**：
   - 构造函数添加了默认参数，不影响现有调用
   - 所有公共接口保持不变

---

### 迁移建议

#### 对于新用户

```yaml
# 推荐配置
translator:
  contextual_suggestions: true
  contextual_group_by_type: false  # 默认值，可省略
```

---

#### 对于现有用户

**选项1：使用新行为（推荐）**

```yaml
# 不需要修改配置，默认使用新行为
translator:
  contextual_suggestions: true
```

**选项2：保持旧行为**

```yaml
# 明确设置为 true
translator:
  contextual_suggestions: true
  contextual_group_by_type: true
```

---

## 📋 检查清单

在提交代码前，请确认：

- [ ] 所有文件编译通过
- [ ] 代码格式化完成（`make clang-format-apply`）
- [ ] 功能测试通过
  - [ ] 默认行为（不按类型分组）
  - [ ] 按类型分组
  - [ ] 禁用上下文建议
- [ ] 日志验证通过
- [ ] 性能测试通过
- [ ] 配置文档已更新
- [ ] Git commit 信息清晰

---

## 🎯 总结

### 核心修改

1. **添加配置选项**：`contextual_group_by_type`
2. **默认值**：`false`（不按类型分组）
3. **修改文件**：5个文件，14行代码
4. **向后兼容**：用户可选择保持旧行为

### 设计优势

- ✅ 简单直接：只需一个布尔配置
- ✅ 灵活可控：用户可自由选择
- ✅ 向后兼容：支持旧行为
- ✅ 默认最优：新用户获得最佳体验

### 预期效果

- ✅ 提升上下文感知准确性
- ✅ 减少用户翻页次数
- ✅ 保持用户选择自由
- ✅ 无性能影响
