# librime comment字段读取逻辑分析

## 概述
本文档分析librime源码中`RimeCandidate.comment`字段的读取逻辑和完整调用链。

## 数据结构定义

### 1. RimeCandidate结构体 (rime_api.h:129-133)
```cpp
typedef struct rime_candidate_t {
  char* text;        // 候选词文本
  char* comment;     // 候选词注释
  void* reserved;
} RimeCandidate;
```

### 2. Candidate类层次结构 (rime/candidate.h)
```cpp
class Candidate {
  virtual const string& text() const = 0;
  virtual string comment() const { return string(); }  // 默认返回空字符串
};

// 简单候选词实现
class SimpleCandidate : public Candidate {
  string comment() const { return comment_; }
  
protected:
  string text_;
  string comment_;  // 存储comment字段
  string preedit_;
};

// 词典候选词实现 (rime/gear/translator_commons.h:72)
class Phrase : public Candidate {
  string comment() const { return entry_->comment; }  // 从DictEntry读取
  
protected:
  an<DictEntry> entry_;  // 指向词典条目
};
```

### 3. DictEntry结构体 (rime/dict/vocabulary.h:46-66)
```cpp
struct DictEntry {
  string text;           // 词条文本
  string comment;        // 词条注释 ⭐核心字段
  string preedit;        // 预编辑文本
  Code code;             // 编码
  string custom_code;    // 自定义编码
  double weight = 0.0;   // 权重
  int commit_count = 0;
  int remaining_code_length = 0;
  int matching_code_size = 0;
};
```

## comment字段的来源

### 来源1: 预测匹配的临时comment (dictionary.cc:158)
当进行预测匹配时，系统会生成临时的comment显示剩余编码：

```cpp
an<DictEntry> DictEntryIterator::Peek() {
  // ...
  entry_ = New<DictEntry>();
  entry_->code = chunk.code;
  entry_->text = chunk.table->GetEntryText(e);
  entry_->weight = e.weight - kS + chunk.credibility;
  
  // ⭐ 预测匹配时生成临时comment
  if (!chunk.remaining_code.empty()) {
    entry_->comment = "~" + chunk.remaining_code;  // 格式: "~剩余编码"
    entry_->remaining_code_length = chunk.remaining_code.length();
  }
  // ...
}
```

**特点**：
- 格式：`~` + 剩余编码
- 场景：用户输入部分编码时，显示完整编码的剩余部分
- 示例：输入"zh"，候选词"中国"的完整编码是"zhongguo"，则comment为"~ongguo"

### 来源2: 用户词典的自定义编码comment (user_dictionary.cc:392)
用户词典中的词条可以有自定义编码，超出匹配长度的部分会显示为comment：

```cpp
// UserDictionary::DictLookup方法
auto e = CreateDictEntry(key, value, present_tick, 1.0, &full_code);
if (!e)
  continue;
e->custom_code = full_code;
boost::trim_right(full_code);

// ⭐ 自定义编码超出部分作为comment
if (full_code.length() > len) {
  e->comment = "~" + full_code.substr(len);  // 格式: "~超出编码"
  e->remaining_code_length = full_code.length() - len;
}
```

**特点**：
- 格式：`~` + 超出的自定义编码
- 场景：用户自定义词条的编码长于当前输入
- 来源：用户词典数据库

### 来源3: 词典文件中的原始comment
虽然在当前代码中没有找到直接从词典文件读取comment的逻辑，但`DictEntry`结构体预留了`comment`字段，理论上可以从词典源文件中读取。

**注意**：标准的Table存储格式(table.h:50-53)只包含text和weight：
```cpp
struct Entry {
  StringType text;
  Weight weight;
  // 没有comment字段
};
```

这意味着comment主要是**运行时动态生成**的，而不是从编译后的词典文件中读取的。

## 完整调用链

### 调用链1: 通过RimeCandidateListIterator访问

```
用户代码调用
  ↓
RimeCandidateListNext(iterator)  [rime_api_impl.h:462]
  ↓
rime_candidate_copy(&iterator->candidate, cand)  [rime_api_impl.h:472]
  ↓
src->comment()  [rime_api_impl.h:267]
  ↓
┌─────────────────────────────────────┐
│ 根据Candidate的具体类型分发:        │
├─────────────────────────────────────┤
│ SimpleCandidate::comment()          │
│   → 返回 comment_ 成员变量          │
├─────────────────────────────────────┤
│ Phrase::comment()                   │
│   → 返回 entry_->comment            │
│   → entry_是DictEntry指针           │
├─────────────────────────────────────┤
│ ShadowCandidate::comment()          │
│   → 继承或覆盖原candidate的comment  │
└─────────────────────────────────────┘
  ↓
DictEntry::comment字段
  ↓
┌─────────────────────────────────────┐
│ comment的实际来源:                  │
├─────────────────────────────────────┤
│ 1. 预测匹配临时生成                 │
│    DictEntryIterator::Peek()        │
│    → "~" + remaining_code           │
├─────────────────────────────────────┤
│ 2. 用户词典自定义编码               │
│    UserDictionary::DictLookup()     │
│    → "~" + 超出编码                 │
└─────────────────────────────────────┘
```

### 调用链2: 通过RimeContext访问

```
用户代码调用
  ↓
RimeGetContext(session_id, context)  [rime_api_impl.h:277]
  ↓
rime_candidate_copy(dest, cand)  [rime_api_impl.h:323]
  ↓
(同调用链1的后续流程)
```

## 核心函数详解

### 1. rime_candidate_copy (rime_api_impl.h:264-275)
**作用**：将C++ Candidate对象转换为C API的RimeCandidate结构体

```cpp
static void rime_candidate_copy(RimeCandidate* dest, const an<Candidate>& src) {
  // 复制text字段
  dest->text = new char[src->text().length() + 1];
  std::strcpy(dest->text, src->text().c_str());
  
  // ⭐ 复制comment字段
  string comment(src->comment());  // 调用虚函数comment()
  if (!comment.empty()) {
    dest->comment = new char[comment.length() + 1];
    std::strcpy(dest->comment, comment.c_str());
  } else {
    dest->comment = nullptr;  // 空comment设为nullptr
  }
  
  dest->reserved = nullptr;
}
```

**关键点**：
- 调用`src->comment()`虚函数获取comment字符串
- 动态分配内存存储comment
- 空comment时设置为nullptr

### 2. RimeCandidateListNext (rime_api_impl.h:462-476)
**作用**：迭代器移动到下一个候选词

```cpp
Bool RimeCandidateListNext(RimeCandidateListIterator* iterator) {
  if (!iterator)
    return False;
  
  Menu* menu = reinterpret_cast<Menu*>(iterator->ptr);
  if (!menu)
    return False;
  
  ++iterator->index;
  
  // ⭐ 获取候选词并复制
  if (auto cand = menu->GetCandidateAt((size_t)iterator->index)) {
    delete[] iterator->candidate.text;     // 释放旧内存
    delete[] iterator->candidate.comment;  // 释放旧comment
    rime_candidate_copy(&iterator->candidate, cand);  // 复制新候选词
    return True;
  }
  
  return False;
}
```

### 3. DictEntryIterator::Peek (dictionary.cc:145-166)
**作用**：从词典查询结果中创建DictEntry对象

```cpp
an<DictEntry> DictEntryIterator::Peek() {
  if (!entry_ && !exhausted()) {
    const auto& chunk = query_result_->chunks[chunk_index_];
    const auto& e = chunk.entries[chunk.cursor];
    
    entry_ = New<DictEntry>();
    entry_->code = chunk.code;
    entry_->text = chunk.table->GetEntryText(e);
    entry_->weight = e.weight - kS + chunk.credibility;
    
    // ⭐ 生成预测匹配的comment
    if (!chunk.remaining_code.empty()) {
      entry_->comment = "~" + chunk.remaining_code;
      entry_->remaining_code_length = chunk.remaining_code.length();
    }
    
    // 标记预测匹配
    if (chunk.is_predictive_match()) {
      entry_->matching_code_size = chunk.matching_code_size;
    }
  }
  return entry_;
}
```

### 4. UserDictionary::DictLookup (user_dictionary.cc:380-409)
**作用**：从用户词典查询词条

```cpp
// 简化版本
auto e = CreateDictEntry(key, value, present_tick, 1.0, &full_code);
if (!e)
  continue;

e->custom_code = full_code;
boost::trim_right(full_code);

// ⭐ 生成自定义编码的comment
if (full_code.length() > len) {
  e->comment = "~" + full_code.substr(len);
  e->remaining_code_length = full_code.length() - len;
}

result->Add(std::move(e));
```

## 内存管理

### 分配
- `rime_candidate_copy`中使用`new char[]`分配内存
- 每次调用都会创建新的字符串副本

### 释放
- `RimeCandidateListNext`中释放：`delete[] iterator->candidate.comment`
- `RimeCandidateListEnd`中释放：`delete[] iterator->candidate.comment`

**重要**：调用者必须确保正确调用`RimeCandidateListEnd`来释放内存，否则会导致内存泄漏。

## comment的格式规范

### 1. 预测匹配格式
```
格式: "~" + 剩余编码
示例: "~ongguo"  (输入"zh"，完整编码"zhongguo")
```

### 2. 自定义编码格式
```
格式: "~" + 超出编码
示例: "~abc"  (自定义编码比当前输入长)
```

### 3. 空comment
```
值: nullptr 或 空字符串
场景: 精确匹配、无剩余编码
```

## 使用示例

### C API使用
```c
RimeCandidateListIterator iterator;
if (RimeCandidateListBegin(session_id, &iterator)) {
  do {
    printf("候选词: %s\n", iterator.candidate.text);
    if (iterator.candidate.comment) {
      printf("注释: %s\n", iterator.candidate.comment);
    }
  } while (RimeCandidateListNext(&iterator));
  
  RimeCandidateListEnd(&iterator);  // ⭐ 必须调用以释放内存
}
```

### 通过Context访问
```c
RimeContext context = {0};
context.data_size = sizeof(RimeContext);

if (RimeGetContext(session_id, &context)) {
  for (int i = 0; i < context.menu.num_candidates; i++) {
    RimeCandidate* cand = &context.menu.candidates[i];
    printf("候选词: %s\n", cand->text);
    if (cand->comment) {
      printf("注释: %s\n", cand->comment);
    }
  }
  
  RimeFreeContext(&context);  // ⭐ 释放context内存
}
```

## 总结

### comment字段的特点
1. **动态生成**：主要在运行时生成，不是从词典文件直接读取
2. **两个主要来源**：
   - 预测匹配的剩余编码提示
   - 用户词典的自定义编码超出部分
3. **格式统一**：都使用`~`前缀表示额外的编码信息
4. **可为空**：精确匹配时comment为nullptr或空字符串

### 调用链路总结
```
API层 (rime_api.h)
  ↓ RimeCandidateListNext / RimeGetContext
实现层 (rime_api_impl.h)
  ↓ rime_candidate_copy
抽象层 (candidate.h)
  ↓ Candidate::comment() 虚函数
具体实现 (translator_commons.h, candidate.h)
  ↓ SimpleCandidate / Phrase / ShadowCandidate
数据层 (vocabulary.h)
  ↓ DictEntry::comment
生成逻辑 (dictionary.cc, user_dictionary.cc)
  ↓ 预测匹配 / 自定义编码
```

### 关键文件清单
- `rime_api.h:129` - RimeCandidate结构体定义
- `rime_api.h:451` - candidate_list_next函数声明
- `rime_api_impl.h:264` - rime_candidate_copy实现
- `rime_api_impl.h:462` - RimeCandidateListNext实现
- `rime/candidate.h:35` - Candidate::comment虚函数
- `rime/candidate.h:71` - SimpleCandidate::comment实现
- `rime/gear/translator_commons.h:72` - Phrase::comment实现
- `rime/dict/vocabulary.h:48` - DictEntry::comment字段
- `rime/dict/dictionary.cc:158` - 预测匹配comment生成
- `rime/dict/user_dictionary.cc:392` - 用户词典comment生成
