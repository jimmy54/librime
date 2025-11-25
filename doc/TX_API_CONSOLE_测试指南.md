# TX API Console 测试指南

## 概述

在 `rime_api_console` 中添加了三个 TX 插件测试命令,用于测试 TX 引擎的候选词获取和选择功能。

## 新增命令

### 1. `tx get candidates`
获取当前 TX 引擎的候选词列表。

**使用示例:**
```
bubu
tx get candidates
```

**输出示例:**
```
TX Candidates (5):
  0. bu
  1. bub
  2. bubu
  3. 不
  4. 布
```

### 2. `tx select <index>`
选择指定索引的 TX 候选词。

**使用示例:**
```
bubu
tx get candidates
tx select 2
```

**说明:**
- `<index>` 是候选词的索引号(从0开始)
- 选择成功后会自动显示更新后的输入状态
- 会在选择的候选词后添加分割符(从 `speller/delimiter` 配置读取)

### 3. `tx get composition`
获取 TX 引擎的组合状态信息。

**使用示例:**
```
bubu
tx get composition
```

**输出示例:**
```
TX Composition:
  sel_end: 4
  tx_activated: true
```

**字段说明:**
- `sel_end`: 当前选择结束位置
- `tx_activated`: TX 引擎是否已激活

## 测试流程示例

### 测试场景1: 基本候选词选择

```bash
# 1. 启动 rime_api_console
./build/bin/rime_api_console

# 2. 选择使用 TX 的输入方案
select schema rime_ice_26

# 3. 输入拼音
bubu

# 4. 查看 TX 候选词
tx get candidates

# 5. 选择第2个候选词(索引为2)
tx select 2

# 6. 继续输入
ge

# 7. 再次查看候选词
tx get candidates

# 8. 选择候选词
tx select 0
```

### 测试场景2: 验证分割符功能

```bash
# 1. 输入拼音
bubu

# 2. 选择候选词
tx select 2

# 3. 查看输入状态(应该看到候选词后有分割符)
# 输出类似: bubu |

# 4. 继续输入下一个音节
ge

# 5. 验证分割符是否正确添加
tx get composition
```

### 测试场景3: 多次选择

```bash
# 1. 输入
bu

# 2. 选择
tx select 0

# 3. 继续输入
bu

# 4. 再次选择
tx select 1

# 5. 查看组合状态
tx get composition
```

## 验证要点

### 1. 分割符添加
- 选择候选词后,应该在候选词后添加配置的分割符
- 默认分割符是空格 `' '`
- 可以在 schema 配置中修改: `speller/delimiter: " '"`

### 2. 候选词列表
- 候选词应该按照 TX 引擎的逻辑排序
- 候选词数量应该合理(不为空)

### 3. 选择位置
- `sel_end` 应该正确更新
- 多次选择后位置应该累加

### 4. 输入状态
- 选择后输入框应该正确更新
- 分割符应该正确插入

## 调试技巧

### 查看日志
TX 引擎在 `SelectCandidate` 中添加了详细的日志输出:

```cpp
LOG(INFO) << "input delimiter from config: '" << delimiter << "'";
LOG(INFO) << "selected candidate: " << selected_candidate;
LOG(INFO) << "updated input: " << input;
```

日志文件位置: `./user_profile/log/rime.console.INFO`

### 常见问题

1. **TX module not found**
   - 确保 TX 插件已正确编译和加载
   - 检查 schema 配置中是否启用了 TX 引擎

2. **No TX candidates available**
   - 确保已经输入了拼音
   - 检查 TX 数据库是否正确加载

3. **Failed to select TX candidate**
   - 检查索引是否越界
   - 确保在 composing 状态下操作

## 编译说明

修改后需要重新编译:

```bash
cd build
cmake ..
make rime_api_console
```

## 相关文件

- **测试工具**: `tools/rime_api_console.cc`
- **TX API**: `plugins/tx/src/rime_tx_api.h`
- **TX 引擎**: `plugins/tx/src/tx_engine.cc`
- **配置示例**: `cmake-build-debug/bin/bim-pinyin/rime_ice_26.schema.yaml`
