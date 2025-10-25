# RimeSetInput 部分精确匹配 V2.1 - 总结文档

## 📋 优化总结

### V2.1 相比 V2.0 的改进

| 项目 | V2.0 | V2.1 | 改进说明 |
|------|------|------|----------|
| **参数命名** | `exact_length` | `input_exact_length` | 更明确表达"输入码的精确长度" |
| **实现优先级** | TableTranslator 优先 | ScriptTranslator 优先 | 更常用，测试更方便 |
| **实现位置** | Translator 层 | Syllabifier 层 | 效率更高，逻辑更清晰 |

---

## 🎯 核心功能

### 功能描述

允许指定输入码的前缀精确匹配长度，后续部分允许派生。

### 使用场景

```cpp
// 场景：14键拼音，用户输入 "bubu"
// 配置：derive/i/u/, derive/n/b/

// 1. 全部派生（默认）
RimeSetInput(session, "bubu");
// 结果：bi+bi, bi+bu, bu+bi, bu+bu, ni+ni, ni+bu...

// 2. 前2码精确
RimeSetInputEx(session, "bubu", 2);
// 结果：bu+bi, bu+bu, bu+ni, bu+nu...
// 说明：第一个 bu 精确，第二个 bu 可派生

// 3. 全部精确
RimeSetInputEx(session, "bubu", 4);
// 结果：bu+bu
// 说明：两个 bu 都精确
```

---

## 🏗️ 架构设计

### 数据流

```
用户调用 RimeSetInputEx("bubu", 2)
    ↓
Context::set_input("bubu", 2)
    ↓ 存储 input_exact_length_ = 2
    ↓
ScriptTranslator::Query()
    ↓
ScriptSyllabifier::BuildSyllableGraph()
    ↓ 传递 Context
    ↓
Syllabifier::BuildSyllableGraph(input, prism, graph, ctx)
    ↓ 读取 ctx->input_exact_length()
    ↓
遍历每个位置：
  start_pos=0: 需要精确（0 < 2）
    拼写 "bu" → 音节 {bu, bi, ni, nu}
    过滤：只保留 bu（拼写=音节）
    
  start_pos=2: 允许派生（2 >= 2）
    拼写 "bu" → 音节 {bu, bi, ni, nu}
    保留：全部音节
    ↓
SyllableGraph:
  edges[0][2] = {bu}        ← 精确
  edges[2][4] = {bu,bi,ni,nu} ← 派生
    ↓
Dictionary::Lookup(syllable_graph)
    ↓
候选词：不步、不比、不你...
```

### 核心过滤逻辑

```cpp
// syllabifier.cc - BuildSyllableGraph()

for (size_t start_pos = 0; start_pos < input.length(); ++start_pos) {
  // 判断是否需要精确匹配
  bool need_exact = (input_exact_length > 0 && 
                     start_pos < input_exact_length);
  
  // 查询拼写
  string spelling = input.substr(start_pos, length);
  
  // 遍历音节
  for (auto syllable_id : syllables) {
    if (need_exact) {
      // 精确模式：拼写必须等于音节
      string syllable = syllabary[syllable_id];
      if (spelling != syllable) {
        continue;  // 跳过派生的音节
      }
    }
    
    // 添加到音节图
    graph->edges[start_pos][end_pos][syllable_id] = props;
  }
}
```

---

## 📝 修改文件清单

### 核心文件（必须修改）

1. **src/rime/context.h** - 添加 `input_exact_length_` 成员
2. **src/rime/context.cc** - 实现 `set_input()` 和 `is_exact_at()`
3. **src/rime_api.h** - 添加 `RimeSetInputEx()` API
4. **src/rime_api_impl.h** - 实现 `RimeSetInputEx()`
5. **src/rime/algo/syllabifier.h** - 添加 `Context*` 参数
6. **src/rime/algo/syllabifier.cc** - 实现音节过滤逻辑
7. **src/rime/gear/script_translator.cc** - 传递 Context

### 代码量估算

| 文件 | 新增行数 | 修改行数 | 说明 |
|------|---------|---------|------|
| context.h | +3 | +1 | 添加成员和方法声明 |
| context.cc | +20 | +1 | 实现精确匹配逻辑 |
| rime_api.h | +8 | +1 | 添加新 API |
| rime_api_impl.h | +15 | +1 | 实现新 API |
| syllabifier.h | +2 | +1 | 添加 Context 参数 |
| syllabifier.cc | +25 | +5 | 实现过滤逻辑 |
| script_translator.cc | +3 | +2 | 传递 Context |
| **总计** | **~76** | **~12** | **约 88 行代码** |

---

## ✅ 实施步骤

### 第一阶段：基础实现（1-2小时）

1. ✓ 修改 Context（context.h/cc）
2. ✓ 修改 API（rime_api.h/impl.h）
3. ✓ 修改 Syllabifier（syllabifier.h/cc）
4. ✓ 修改 ScriptTranslator（script_translator.cc）

### 第二阶段：编译测试（30分钟）

```bash
cd librime/build
cmake ..
make -j$(nproc)
```

### 第三阶段：功能测试（1小时）

1. 手动测试（rime_console）
2. 单元测试（syllabifier_test）
3. 集成测试（script_translator_test）

### 第四阶段：性能验证（30分钟）

1. 基准测试
2. 内存分析
3. 性能对比

---

## 🧪 测试计划

### 测试用例

#### 1. 基本功能测试

```cpp
// 测试1：全部派生
RimeSetInputEx(session, "bubu", 0);
EXPECT_HAS_CANDIDATE("不步");

// 测试2：前2码精确
RimeSetInputEx(session, "bubu", 2);
EXPECT_HAS_CANDIDATE("不步");
EXPECT_HAS_CANDIDATE("不比");

// 测试3：全部精确
RimeSetInputEx(session, "bubu", 4);
EXPECT_HAS_CANDIDATE("不步");
```

#### 2. 边界测试

```cpp
// 测试1：exact_length 超过输入长度
RimeSetInputEx(session, "bu", 10);
// 期望：等同于 exact_length=2（全部精确）

// 测试2：exact_length 为负数
RimeSetInputEx(session, "bubu", -1);
// 期望：等同于 exact_length=4（全部精确）

// 测试3：空输入
RimeSetInputEx(session, "", 2);
// 期望：无候选
```

#### 3. 14键方案测试

```cpp
// 配置：derive/i/u/, derive/n/b/
RimeSelectSchema(session, "rime_ice_14");

// 测试：输入 bububi，前4码精确
RimeSetInputEx(session, "bububi", 4);

// 验证
EXPECT_HAS_CANDIDATE("不步比");  // bu+bu+bi ✓
EXPECT_HAS_CANDIDATE("不步你");  // bu+bu+ni ✓
EXPECT_NO_CANDIDATE("不比比");   // bu+bi+bi ✗
EXPECT_NO_CANDIDATE("比步比");   // bi+bu+bi ✗
```

---

## 📊 性能分析

### 时间复杂度

| 操作 | 原始 | V2.1 | 说明 |
|------|------|------|------|
| 构建音节图 | O(n·m·k) | O(n·m·k) | 相同 |
| 精确模式额外开销 | - | O(L) | L=拼写长度 |
| 非精确模式 | - | O(1) | 无额外开销 |

### 空间复杂度

| 项目 | 大小 | 说明 |
|------|------|------|
| Context::input_exact_length_ | 4 bytes | int 类型 |
| Syllabary 指针 | 8 bytes | 临时变量 |
| **总计** | **12 bytes** | 可忽略 |

### 性能优化

1. **只在需要时查询音节表**
   ```cpp
   if (input_exact_length > 0) {
     syllabary = prism.syllabary();
   }
   ```

2. **只在精确范围内过滤**
   ```cpp
   if (start_pos < input_exact_length) {
     // 过滤逻辑
   }
   ```

3. **使用引用避免复制**
   ```cpp
   const string& syllable_str = (*syllabary)[syllable_id];
   ```

---

## ⚠️ 注意事项

### 1. Prism::syllabary() 方法

需要确认 `Prism` 类有此方法：

```cpp
// dict/prism.h
class Prism {
 public:
  const Syllabary* syllabary() const;
};
```

如果没有，需要添加。

### 2. 分隔符处理

输入包含分隔符时的位置计算：

```
输入：bu'bu (5字符)
input_exact_length：2

位置：
  0-2: bu (精确，因为 start_pos=0 < 2)
  3-5: bu (派生，因为 start_pos=3 >= 2)
```

### 3. 多音节匹配

某些拼写可能匹配多个长度：

```
输入：xian
匹配：
  - xi (0-2) + an (2-4)
  - xian (0-4)

input_exact_length：2

结果：
  - xi (0-2): 精确
  - an (2-4): 派生
  - xian (0-4): 精确（start_pos=0 < 2）
```

---

## 🚀 后续扩展

### 扩展到 TableTranslator

在验证 ScriptTranslator 实现后，可以扩展到 TableTranslator：

1. 修改 `table_translator.cc` 的 `Query()` 方法
2. 在查询 Prism 时应用相同的过滤逻辑
3. 添加对应的测试用例

### 支持更多 Translator

- PhoneticTranslator
- ReverseDbTranslator
- EchoTranslator（可能不需要）

---

## 📚 文档清单

### 已创建文档

1. **RimeSetInput_SpellingAlgebra_Issue_Analysis.md**
   - 问题分析
   - 根本原因
   - 多种解决方案

2. **RimeSetInput_ExactMatch_V2.1_Design.md**
   - V2.1 设计文档
   - 架构分析
   - 实现方案对比

3. **RimeSetInput_ExactMatch_V2.1_Patch.md**
   - 完整代码补丁
   - 7个文件的修改
   - 测试用例

4. **RimeSetInput_ExactMatch_V2.1_Summary.md**（本文档）
   - 总结文档
   - 快速参考

---

## 🎓 关键概念

### 拼写 vs 音节

- **拼写（Spelling）**：用户输入的字符串，如 "bu"
- **音节（Syllable）**：实际的发音单位，如 "bu"、"bi"、"ni"
- **派生（Derive）**：通过拼写运算规则生成的映射，如 "bu" → "bi"

### 精确匹配 vs 派生匹配

- **精确匹配**：拼写 = 音节，如 "bu" 只匹配音节 "bu"
- **派生匹配**：拼写 → 多个音节，如 "bu" 匹配 "bu", "bi", "ni", "nu"

### 部分精确匹配

- **前缀精确**：前 N 个字符精确匹配
- **后缀派生**：后续字符允许派生
- **灵活控制**：适合连续输入和逐步确认场景

---

## 💡 使用建议

### 场景 1：筛选器逐步确认

```javascript
// JavaScript Filter
function filter(input, env) {
  let ctx = env.engine.context;
  
  // 获取已确认的拼音长度
  let confirmed = get_confirmed_length();
  
  // 设置部分精确匹配
  if (confirmed > 0) {
    rime.set_input_ex(ctx.input, confirmed);
  }
  
  return input;
}
```

### 场景 2：14键连续输入

```cpp
// 用户输入：bu
RimeSetInputEx(session, "bu", 0);  // 全部派生
// 候选：不、步、比、笔、你...

// 用户选择：不
// 继续输入：bu
RimeSetInputEx(session, "bubu", 2);  // 前2码精确
// 候选：不步、不比、不你...

// 用户选择：不步
// 继续输入：bi
RimeSetInputEx(session, "bububi", 4);  // 前4码精确
// 候选：不步比、不步笔...
```

### 场景 3：智能纠错

```cpp
// 用户输入错误后，系统自动纠正前面的输入
// 例如：输入 "bubi"，系统识别为 "bubu"

// 设置前2码精确（已确认的部分）
RimeSetInputEx(session, "bubu", 2);
// 后续输入可以继续派生
```

---

## 📞 技术支持

### 问题排查

1. **编译错误**：检查 Prism::syllabary() 方法是否存在
2. **功能异常**：检查 Context 是否正确传递
3. **性能问题**：检查是否只在精确范围内过滤

### 调试建议

1. 添加日志输出：
   ```cpp
   DLOG(INFO) << "input_exact_length=" << input_exact_length
              << ", start_pos=" << start_pos
              << ", need_exact=" << need_exact;
   ```

2. 使用调试器：
   - 断点：`Syllabifier::BuildSyllableGraph()`
   - 观察：`input_exact_length`, `start_pos`, `spelling`, `syllable_str`

3. 单元测试：
   - 先测试 Context 的 `set_input()` 和 `is_exact_at()`
   - 再测试 Syllabifier 的过滤逻辑

---

## ✨ 总结

### V2.1 核心优势

1. **命名清晰**：`input_exact_length` 明确表达意图
2. **实现高效**：在 Syllabifier 层面过滤，效率最高
3. **易于测试**：ScriptTranslator 更常用，测试更方便
4. **向后兼容**：默认行为不变（`input_exact_length=0`）

### 实施建议

1. 按照补丁文档逐步修改
2. 每修改一个文件就编译测试
3. 先测试基本功能，再测试边界情况
4. 验证通过后再扩展到 TableTranslator

### 预期效果

- ✓ 解决 14键方案的候选混乱问题
- ✓ 支持筛选器逐步确认场景
- ✓ 提供灵活的精确匹配控制
- ✓ 保持向后兼容性

---

**文档版本**：V2.1  
**最后更新**：2025-01-25  
**作者**：Cascade AI Assistant
