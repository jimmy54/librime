# TX 分割符实现说明

## 问题背景

在 `SelectCandidate` 选择候选词时，需要在候选词后添加分割符（如空格），以正确分隔拼音音节。

## 实现方案

### 1. 分割符来源

分割符配置从 `speller/delimiter` 读取，在 `TxEngine` 构造时完成：

```cpp
// tx_engine.cc 第801-815行
TxEngine* TxEngineComponent::Create(const Ticket& ticket) {
  string delimiters;
  if (auto* schema = ticket.schema) {
    auto* config = schema->config();
    config->GetString("speller/delimiter", &delimiters);  // 读取配置
  }
  return new TxEngine(db, delimiters, alphabet);  // 传入构造函数
}
```

### 2. 分割符存储

`TxEngine` 类中的 `delimiters_` 成员变量存储分割符配置：

```cpp
// tx_engine.h 第75行
string delimiters_ = ""; // 分割符配置(从speller/delimiter读取,用于识别输入和添加分割符)
```

### 3. 分割符使用

在 `SelectCandidate` 方法中使用分割符：

```cpp
// tx_engine.cc 第242-248行
// 使用speller/delimiter配置的分割符(在构造函数中已从配置读取到delimiters_)
// 如果未配置,默认使用空格
char delimiter = ' ';
if (!delimiters_.empty()) {
  delimiter = delimiters_[0];
}
LOG(INFO) << "input delimiter: '" << delimiter << "'";
```

### 4. 添加分割符

在替换候选词时添加分割符：

```cpp
// 中间候选词添加分割符
string candidate_with_delimiter = selected_candidate + delimiter;
input.replace(sel_start_, sel_can_size, candidate_with_delimiter);

// 最后一个候选词不添加分割符
input.replace(sel_start_, sel_can_size, selected_candidate);
```

## 两种分割符用途

### 1. 识别原始输入中的分割符
- 位置：第318行 `find_first_not_of(delimiters_)`
- 用途：解析原始输入，跳过分割符找到实际音节
- 使用：内部 `delimiters_` 成员变量

### 2. 添加到输入中的分割符
- 位置：第280、295、331行
- 用途：在选择候选词后添加分割符，分隔音节
- 使用：同样使用 `delimiters_` 的第一个字符

## 配置示例

在 schema 配置文件中：

```yaml
speller:
  delimiter: " '"  # 第一位是空格(分隔符)，第二位是单引号(手动分割)
```

## 关键点

1. **统一配置源**：分割符配置统一从 `speller/delimiter` 读取
2. **构造时读取**：在 `TxEngine` 构造时一次性读取，避免运行时重复访问配置
3. **默认值**：如果未配置，默认使用空格 `' '`
4. **多字符支持**：`delimiters_` 是字符串，支持多个分割符，但目前只使用第一个

## 测试方法

使用 `rime_api_console` 测试：

```bash
# 输入拼音
bubu

# 查看候选词
tx get candidates

# 选择候选词(会自动添加分割符)
tx select 2

# 继续输入(分割符应该已添加)
ge

# 验证
tx get composition
```

## 相关文件

- **配置读取**：`plugins/tx/src/tx_engine.cc` 第810行
- **分割符存储**：`plugins/tx/src/tx_engine.h` 第75行
- **分割符使用**：`plugins/tx/src/tx_engine.cc` 第242-248行
- **添加分割符**：`plugins/tx/src/tx_engine.cc` 第280、295、331行
- **测试工具**：`tools/rime_api_console.cc`
- **测试文档**：`doc/TX_API_CONSOLE_测试指南.md`
