# RimeSetInputEx 部分精确匹配功能 - 测试指南

## 🎉 编译状态

✅ **编译成功！** 所有代码已通过编译。

## 📋 测试准备

### 1. 编译项目

```bash
cd /Users/jimmy54/Pictures/jimmy_librime/librime
cmake -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cd cmake-build-debug
ninja
```

### 2. 启动测试控制台

```bash
cd cmake-build-debug
./bin/rime_api_console
```

## 🧪 测试命令

### 快速开始

启动控制台后，输入以下命令查看帮助：

```
help exact match
```

### 自动化测试套件

运行完整的测试套件（8个测试用例）：

```
test exact match
```

这将自动运行以下测试：

1. **测试 1**：全部派生（exact_length=0，默认行为）
2. **测试 2**：前2码精确（部分精确匹配）
3. **测试 3**：全部精确（exact_length=4）
4. **测试 4**：负数处理（exact_length=-1）
5. **测试 5**：超长处理（exact_length=100）
6. **测试 6**：单音节精确
7. **测试 7**：三音节测试（前4码精确）
8. **测试 8**：对比测试（使用旧 API）

### 手动测试

使用 `set input ex` 命令手动测试：

```
set input ex <input> <exact_length>
```

**示例**：

```bash
# 前2码精确
set input ex bubu 2

# 全部派生
set input ex bubu 0

# 全部精确
set input ex bubu -1

# 三音节测试
set input ex bububi 4
```

## 📝 测试场景

### 场景 1：14键拼音方案测试

**前提**：方案需要配置 derive 规则，例如：

```yaml
speller:
  algebra:
    - derive/i/u/    # i 可派生为 u
    - derive/n/b/    # n 可派生为 b
```

**测试步骤**：

1. 选择 14键方案（如果有）：
   ```
   select schema rime_ice_14
   ```

2. 运行测试：
   ```
   test exact match
   ```

3. 观察结果：
   - 测试 1（exact_length=0）：应包含 bi+bi, bi+bu, bu+bi, bu+bu, ni+ni 等所有组合
   - 测试 2（exact_length=2）：第一个音节只有 bu，第二个可派生
   - 测试 3（exact_length=4）：两个音节都是 bu

### 场景 2：标准拼音方案测试

如果方案没有 derive 规则，所有测试结果可能相同（因为没有派生）。

**测试步骤**：

1. 选择标准拼音方案：
   ```
   select schema luna_pinyin
   ```

2. 手动测试：
   ```
   set input ex bubu 2
   ```

3. 预期：由于没有 derive 规则，结果应该相同

### 场景 3：对比测试

对比新旧 API 的行为：

```bash
# 旧 API（全部派生）
clear composition
set input bubu

# 新 API（全部派生）
clear composition
set input ex bubu 0

# 新 API（前2码精确）
clear composition
set input ex bubu 2

# 新 API（全部精确）
clear composition
set input ex bubu 4
```

## 📊 预期结果

### 有 derive 规则的方案

假设配置了 `derive/i/u/` 和 `derive/n/b/`：

| 测试 | 输入 | exact_length | 第一个音节 | 第二个音节 | 示例候选 |
|------|------|--------------|-----------|-----------|---------|
| 1 | bubu | 0 | bu,bi,ni,nu | bu,bi,ni,nu | 不步、比步、你步... |
| 2 | bubu | 2 | **bu** | bu,bi,ni,nu | 不步、不比、不你... |
| 3 | bubu | 4 | **bu** | **bu** | 不步、不部... |
| 4 | bubu | -1 | **bu** | **bu** | 同测试 3 |
| 5 | bubu | 100 | **bu** | **bu** | 同测试 3 |

**粗体**表示精确匹配（不派生）

### 无 derive 规则的方案

所有测试结果应该相同，因为没有派生规则。

## 🔍 验证要点

### 1. 精确匹配验证

**测试 2**（前2码精确）应该：
- ✅ 包含：不步、不比、不你（第一个音节是 bu）
- ❌ 不包含：比步、你步（第一个音节不是 bu）

### 2. 边界条件验证

**测试 4**（负数）和**测试 5**（超长）：
- 应该等同于**测试 3**（全部精确）
- 验证 Context 正确处理特殊值

### 3. API 兼容性验证

**测试 8**（旧 API）：
- 使用 `RimeSetInput` 应该等同于 `RimeSetInputEx(..., 0)`
- 验证向后兼容性

## 🐛 调试技巧

### 查看日志

如果结果不符合预期，可以：

1. 检查方案配置：
   ```
   print schema list
   ```

2. 查看当前方案：
   ```
   get current schema
   ```

3. 查看候选列表：
   ```
   print candidate list
   ```

### 常见问题

#### Q1: 所有测试结果都相同？

**原因**：方案可能没有配置 derive 规则。

**解决**：
1. 检查方案的 `speller.algebra` 配置
2. 或者使用有 derive 规则的方案（如 14键方案）

#### Q2: 看不到候选？

**原因**：可能词典没有对应的词条。

**解决**：
1. 尝试其他输入，如 "zhong", "guo" 等常见拼音
2. 确保词典已部署

#### Q3: API 不可用？

**原因**：可能是旧版本的 librime。

**解决**：
1. 确认已重新编译
2. 检查 `RIME_API_AVAILABLE(rime, set_input_ex)` 返回值

## 📈 性能测试

### 基准测试

对比不同 exact_length 的性能：

```bash
# 测试 1000 次查询
for i in {1..1000}; do
  set input ex bubu 0
  clear composition
done

for i in {1..1000}; do
  set input ex bubu 2
  clear composition
done
```

### 预期性能

- exact_length=0：与原有逻辑相同，无额外开销
- exact_length>0：额外开销主要在过滤阶段，应该可以忽略

## 📚 其他测试命令

### 方案管理

```bash
# 列出所有方案
print schema list

# 选择方案
select schema <schema_id>

# 查看当前方案
get current schema
```

### 候选管理

```bash
# 查看所有候选
print candidate list

# 选择候选
select candidate <index>
```

### 上下文管理

```bash
# 设置上下文
set context <left_text> | <right_text>

# 清除上下文
clear context

# 查看上下文
show context
```

## 🎓 测试报告模板

完成测试后，可以使用以下模板记录结果：

```
# RimeSetInputEx 测试报告

## 测试环境
- 日期：YYYY-MM-DD
- 方案：<schema_name>
- 是否有 derive 规则：是/否

## 测试结果

### 测试 1：全部派生
- 输入：bubu, exact_length=0
- 候选数量：X
- 前3个候选：xxx, xxx, xxx
- 结果：✅ 通过 / ❌ 失败

### 测试 2：前2码精确
- 输入：bubu, exact_length=2
- 候选数量：X
- 前3个候选：xxx, xxx, xxx
- 验证：第一个音节是否只有 bu？ ✅ / ❌
- 结果：✅ 通过 / ❌ 失败

### 测试 3：全部精确
- 输入：bubu, exact_length=4
- 候选数量：X
- 前3个候选：xxx, xxx, xxx
- 验证：两个音节是否都是 bu？ ✅ / ❌
- 结果：✅ 通过 / ❌ 失败

... (其他测试)

## 总结
- 通过测试：X/8
- 失败测试：X/8
- 整体评价：优秀/良好/需改进
```

## 🚀 下一步

测试通过后，可以：

1. **集成到实际应用**：在输入法前端使用 `RimeSetInputEx` API
2. **性能优化**：如果需要，可以缓存 syllable_map
3. **扩展功能**：在 TableTranslator 中实现相同逻辑
4. **编写单元测试**：添加自动化测试用例

## 📞 技术支持

如有问题，请检查：
1. 编译日志
2. 运行时日志（使用 DLOG）
3. 方案配置文件

---

**祝测试顺利！** 🎉
