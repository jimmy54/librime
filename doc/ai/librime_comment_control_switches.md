# librime comment显示控制开关分析

## 概述
librime提供了多个配置选项来控制候选词comment（注释）的显示行为。这些开关可以在schema配置文件中设置。

## 核心控制开关

### 1. always_show_comments（总是显示注释）

#### 定义位置
- **头文件**：`rime/gear/script_translator.h:51,61`
- **实现文件**：`rime/gear/script_translator.cc:179-180`

#### 数据结构
```cpp
class TranslatorOptions {
protected:
  bool always_show_comments_ = false;  // 默认值：false
  
public:
  bool always_show_comments() const { return always_show_comments_; }
};
```

#### 配置方式
在schema配置文件的translator部分添加：
```yaml
translator:
  always_show_comments: true  # 或 false
```

#### 加载逻辑
```cpp
ScriptTranslator::ScriptTranslator(const Ticket& ticket)
    : Translator(ticket), Memory(ticket), TranslatorOptions(ticket) {
  if (Config* config = engine_->schema()->config()) {
    // 从配置文件读取 always_show_comments 设置
    config->GetBool(name_space_ + "/always_show_comments",
                    &always_show_comments_);
    // ...
  }
}
```

#### 使用场景
在生成候选词时决定是否显示拼音注释：

```cpp
// script_translator.cc:538-543
if (candidate_->comment().empty()) {
  auto spelling = syllabifier_->GetOriginalSpelling(*candidate_);
  
  // ⭐ 关键判断逻辑
  if (!spelling.empty() && 
      (translator_->always_show_comments() ||           // 条件1：总是显示
       spelling != candidate_->preedit())) {            // 条件2：拼音与预编辑不同
    candidate_->set_comment(spelling);
  }
}
```

#### 行为说明
- **true**：总是显示原始拼音作为comment，即使拼音与预编辑文本相同
- **false**（默认）：只在拼音与预编辑文本不同时显示comment

#### 典型应用场景
```
输入: "zhong"
候选词: "中"

always_show_comments = false:
  - 如果preedit是"zhong"，comment为空（因为相同）
  - 如果preedit是"zhōng"，comment显示"zhong"（因为不同）

always_show_comments = true:
  - 无论preedit是什么，comment都显示"zhong"
```

---

### 2. overwrite_comment（覆盖注释）

#### 定义位置
- **头文件**：`rime/gear/reverse_lookup_filter.h:36`
- **实现文件**：`rime/gear/reverse_lookup_filter.cc:55`

#### 数据结构
```cpp
class ReverseLookupFilter : public Filter, TagMatching {
protected:
  bool overwrite_comment_ = false;  // 默认值：false
  bool append_comment_ = false;
};
```

#### 配置方式
在schema配置文件的reverse_lookup部分添加：
```yaml
reverse_lookup:
  overwrite_comment: true  # 或 false
```

#### 使用逻辑
```cpp
void ReverseLookupFilter::Process(const an<Candidate>& cand) {
  // ⭐ 如果已有comment且不允许覆盖/追加，直接返回
  if (!cand->comment().empty() && 
      !(overwrite_comment_ || append_comment_))
    return;
  
  // 反查编码
  string codes;
  if (rev_dict_->ReverseLookup(phrase->text(), &codes)) {
    comment_formatter_.Apply(&codes);
    if (!codes.empty()) {
      // ⭐ 根据设置决定覆盖还是追加
      if (overwrite_comment_ || cand->comment().empty()) {
        phrase->set_comment(codes);  // 覆盖
      } else {
        phrase->set_comment(cand->comment() + " " + codes);  // 追加
      }
    }
  }
}
```

#### 行为说明
- **true**：反查得到的编码会覆盖原有的comment
- **false**（默认）：如果已有comment，反查编码会追加到后面（如果append_comment为true）

---

### 3. append_comment（追加注释）

#### 定义位置
- **头文件**：`rime/gear/reverse_lookup_filter.h:37`
- **实现文件**：`rime/gear/reverse_lookup_filter.cc:56`

#### 配置方式
```yaml
reverse_lookup:
  append_comment: true  # 或 false
```

#### 使用逻辑
```cpp
void ReverseLookupFilter::Process(const an<Candidate>& cand) {
  // ⭐ 检查是否允许处理已有comment的候选词
  if (!cand->comment().empty() && 
      !(overwrite_comment_ || append_comment_))
    return;  // 既不覆盖也不追加，直接跳过
  
  // ... 反查逻辑
  
  if (!codes.empty()) {
    if (overwrite_comment_ || cand->comment().empty()) {
      phrase->set_comment(codes);  // 覆盖或设置新comment
    } else {
      // ⭐ append_comment为true时执行追加
      phrase->set_comment(cand->comment() + " " + codes);
    }
  }
}
```

#### 行为说明
- **true**：允许在已有comment后追加反查编码
- **false**（默认）：如果已有comment且overwrite_comment为false，不做任何处理

---

## 三个开关的组合效果

### ReverseLookupFilter的comment处理决策表

| overwrite_comment | append_comment | 原comment状态 | 处理行为 |
|-------------------|----------------|---------------|----------|
| false | false | 空 | 设置反查编码 |
| false | false | 非空 | **跳过处理** |
| false | true | 空 | 设置反查编码 |
| false | true | 非空 | **追加反查编码** |
| true | false | 空 | 设置反查编码 |
| true | false | 非空 | **覆盖为反查编码** |
| true | true | 空 | 设置反查编码 |
| true | true | 非空 | **覆盖为反查编码** |

**优先级**：`overwrite_comment` > `append_comment`

---

## 完整配置示例

### 示例1：总是显示拼音注释
```yaml
# schema.yaml
translator:
  always_show_comments: true
  
# 效果：所有候选词都会显示原始拼音
# 输入: "zhong"
# 候选词: "中" [zhong]
```

### 示例2：反查编码覆盖原注释
```yaml
# schema.yaml
reverse_lookup:
  dictionary: stroke
  overwrite_comment: true
  
# 效果：用笔画编码替换原有的拼音注释
# 候选词: "中" [hsmh]  （而不是 "中" [zhong]）
```

### 示例3：反查编码追加到原注释
```yaml
# schema.yaml
translator:
  always_show_comments: true
  
reverse_lookup:
  dictionary: stroke
  append_comment: true
  
# 效果：拼音和笔画编码都显示
# 候选词: "中" [zhong hsmh]
```

### 示例4：只在必要时显示注释（默认行为）
```yaml
# schema.yaml
translator:
  always_show_comments: false  # 或不设置（默认）
  
reverse_lookup:
  dictionary: stroke
  overwrite_comment: false  # 或不设置（默认）
  append_comment: false     # 或不设置（默认）
  
# 效果：只在拼音与预编辑不同时显示
# 输入: "zhong"
# 候选词: "中" []  （如果preedit是"zhong"，无comment）
# 候选词: "中" [zhong]  （如果preedit是"zhōng"，显示原始拼音）
```

---

## 相关配置项

### comment_format（注释格式化）
```yaml
reverse_lookup:
  comment_format:
    - xform/^/〔/
    - xform/$/〕/
    
# 效果：在反查编码外加上括号
# 候选词: "中" 〔hsmh〕
```

### spelling_hints（拼音提示级别）
```yaml
translator:
  spelling_hints: 8  # 超过8个字符才显示拼音提示
  
# 控制何时显示拼音提示，与always_show_comments配合使用
```

---

## 代码调用链

### always_show_comments的调用链
```
Schema配置文件 (*.schema.yaml)
  ↓ translator/always_show_comments
ScriptTranslator构造函数
  ↓ config->GetBool()
TranslatorOptions::always_show_comments_
  ↓ always_show_comments() getter
ScriptTranslation::MakeCandidate()
  ↓ 判断是否设置comment
Candidate::set_comment()
  ↓
SimpleCandidate::comment_字段
  ↓
rime_candidate_copy()
  ↓
RimeCandidate.comment
```

### overwrite_comment/append_comment的调用链
```
Schema配置文件 (*.schema.yaml)
  ↓ reverse_lookup/overwrite_comment
  ↓ reverse_lookup/append_comment
ReverseLookupFilter构造函数
  ↓ config->GetBool()
ReverseLookupFilter::overwrite_comment_
ReverseLookupFilter::append_comment_
  ↓
ReverseLookupFilter::Process()
  ↓ 判断处理策略
Phrase::set_comment()
  ↓
DictEntry::comment字段
  ↓
rime_candidate_copy()
  ↓
RimeCandidate.comment
```

---

## 实际应用建议

### 1. 学习模式（显示详细信息）
```yaml
translator:
  always_show_comments: true
  
reverse_lookup:
  dictionary: stroke
  append_comment: true
  comment_format:
    - "xform/^/ /"
```
**效果**：同时显示拼音和笔画，帮助学习

### 2. 简洁模式（最小化注释）
```yaml
translator:
  always_show_comments: false
  
reverse_lookup:
  overwrite_comment: false
  append_comment: false
```
**效果**：只在必要时显示注释，界面简洁

### 3. 反查优先模式
```yaml
translator:
  always_show_comments: false
  
reverse_lookup:
  dictionary: stroke
  overwrite_comment: true
```
**效果**：优先显示反查编码（如笔画），隐藏拼音

---

## 相关源文件清单

### 核心实现
- `rime/gear/script_translator.h` - always_show_comments定义
- `rime/gear/script_translator.cc` - always_show_comments实现和使用
- `rime/gear/reverse_lookup_filter.h` - overwrite/append_comment定义
- `rime/gear/reverse_lookup_filter.cc` - overwrite/append_comment实现

### 数据结构
- `rime/candidate.h` - Candidate基类和SimpleCandidate
- `rime/gear/translator_commons.h` - Phrase类
- `rime/dict/vocabulary.h` - DictEntry结构体

### API层
- `rime_api.h` - RimeCandidate结构体
- `rime_api_impl.h` - rime_candidate_copy函数

---

## 总结

librime提供了三个主要的comment控制开关：

1. **always_show_comments**
   - 作用域：ScriptTranslator（脚本翻译器）
   - 功能：控制是否总是显示原始拼音
   - 默认值：false

2. **overwrite_comment**
   - 作用域：ReverseLookupFilter（反查过滤器）
   - 功能：控制反查编码是否覆盖原有comment
   - 默认值：false

3. **append_comment**
   - 作用域：ReverseLookupFilter（反查过滤器）
   - 功能：控制反查编码是否追加到原有comment
   - 默认值：false

这些开关可以灵活组合，满足不同的使用场景和用户偏好。通过合理配置，可以实现从"完全不显示注释"到"显示所有可能的注释信息"的各种效果。
