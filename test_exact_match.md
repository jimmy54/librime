# RimeSetInputEx 部分精确匹配功能测试

## 编译状态

✅ 编译成功！

## 已完成的修改

### 1. Context (src/rime/context.h & context.cc)
- ✅ 添加 `input_exact_length_` 成员变量
- ✅ 修改 `set_input()` 方法，添加 `input_exact_length` 参数
- ✅ 添加 `input_exact_length()` getter 方法
- ✅ 添加 `is_exact_at(size_t pos)` 方法

### 2. API (src/rime_api_deprecated.h & rime_api_impl.h)
- ✅ 添加 `RimeSetInputEx()` API 声明
- ✅ 实现 `RimeSetInputEx()` 函数（inline）
- ✅ 修改 `RimeSetInput()` 调用新实现

### 3. ScriptTranslator (src/rime/gear/script_translator.h & .cc)
- ✅ 添加 `engine()` 公共方法
- ✅ 在 `ScriptSyllabifier` 中添加 `input()` getter
- ✅ 修改 `MakeSentence()` 方法，添加精确匹配逻辑
- ✅ 实现 `FilterLookupResult()` 模板方法
- ✅ 实现 `GetSpelling()` 辅助方法

## 核心实现逻辑

### 数据流

```
RimeSetInputEx("bubu", 2)
    ↓
Context::set_input("bubu", 2)
    ↓ input_exact_length_ = 2
    ↓
ScriptTranslation::MakeSentence()
    ↓ 获取 input_exact_length = 2
    ↓ 获取 syllabary
    ↓
遍历音节图：
  start_pos=0: need_exact=true (0 < 2)
    → FilterLookupResult() 过滤
    → 只保留 拼写=音节 的词条
  
  start_pos=2: need_exact=false (2 >= 2)
    → EnrollEntries() 正常处理
    → 保留所有词条
```

### 过滤逻辑

```cpp
// 在 FilterLookupResult 中：
1. 构建 syllable_id → string 映射
2. 获取当前拼写字符串
3. 遍历查询结果的迭代器
4. 对每个词条：
   - 获取第一个音节ID
   - 查找对应的音节字符串
   - 比较：拼写 == 音节 ?
   - 匹配则保留，否则跳过
```

## 测试方法

### 手动测试

```bash
cd cmake-build-debug
./bin/rime_console
```

然后在控制台中测试（需要先实现控制台命令支持）。

### 预期行为

#### 测试 1：全部派生（默认）
```
输入：bubu
exact_length：0
预期：包含所有派生组合的候选
```

#### 测试 2：前2码精确
```
输入：bubu
exact_length：2
预期：
  - 第一个音节只有 bu
  - 第二个音节可以是 bu, bi, ni, nu
  - 候选：不步、不比、不你...
  - 不包含：比步、比比...
```

#### 测试 3：全部精确
```
输入：bubu
exact_length：4 或 -1
预期：
  - 两个音节都是 bu
  - 候选：不步、不部...
  - 不包含：不比、不你...
```

## 注意事项

### 1. 性能考虑
- 每次查询时构建 syllable_map（可优化为缓存）
- 只在精确范围内过滤（start_pos < input_exact_length）
- 非精确范围直接使用原有逻辑

### 2. 兼容性
- 默认 `input_exact_length=0`，行为不变
- 旧 API `RimeSetInput` 仍然可用
- 新 API `RimeSetInputEx` 提供额外控制

### 3. 已知限制
- 目前只在 ScriptTranslator 中实现
- TableTranslator 尚未实现（可后续扩展）
- 需要方案配置了 derive 规则才能看到效果

## 下一步

### 功能测试
1. 使用 14键拼音方案测试
2. 验证精确匹配是否正确过滤
3. 测试边界情况（负数、超长等）

### 性能优化
1. 缓存 syllable_map 避免重复构建
2. 考虑使用 vector 而非 map 提高查找速度

### 扩展功能
1. 在 TableTranslator 中实现相同逻辑
2. 添加单元测试
3. 更新用户文档

## 总结

✅ **核心功能已实现并编译成功**

实现了部分精确匹配功能，允许用户通过 `RimeSetInputEx` API 指定输入码的前缀精确长度，后续部分允许派生。这对于 14键方案等需要逐步确认输入的场景非常有用。

**代码修改量**：
- 新增：~150 行
- 修改：~20 行
- 总计：~170 行代码

**修改文件**：
- context.h / context.cc
- rime_api_deprecated.h / rime_api_impl.h
- script_translator.h / script_translator.cc
