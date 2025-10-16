# 上下文排序性能测试指南

## 测试目的

验证在连续快速输入多个字符时,上下文排序功能是否会导致卡顿。

---

## 测试环境准备

### 1. 确保已编译项目

```bash
# 如果还没有编译,运行:
cmake -B build -DBUILD_TEST=OFF
cmake --build build -j8
```

### 2. 确保有测试数据

需要有输入方案和语法数据库文件:
- `user_profile/` - 用户配置目录
- 输入方案文件 (如 `luna_pinyin.schema.yaml`)
- 语法数据库文件 (如 `zh-hant.gram`)

---

## 测试方法

### 方法一: 使用测试脚本 (推荐)

```bash
# 运行自动化测试脚本
./test_performance.sh
```

该脚本会:
1. 设置上下文 "我爱"
2. 逐个输入字符 "n", "i", "h", "a", "o"
3. 记录每次输入的耗时
4. 输出日志文件位置

### 方法二: 手动交互测试

```bash
# 启动 rime_api_console
./build/bin/rime_api_console

# 在交互界面中输入以下命令:

# 1. 设置上下文
set context 我爱|

# 2. 逐个输入字符 (模拟快速打字)
n
i
h
a
o

# 3. 查看结果并退出
exit
```

### 方法三: 使用内置性能测试命令

```bash
./build/bin/rime_api_console

# 在交互界面中输入:
test performance

# 这会自动运行完整的性能测试
```

---

## 查看测试结果

### 1. 查看日志文件

```bash
# 查看最新的日志
tail -200 ./user_profile/log/rime.INFO

# 过滤性能相关信息
grep -A 10 "ContextualRankingFilter Performance" ./user_profile/log/rime.INFO

# 查看每个Query的耗时
grep "Query took" ./user_profile/log/rime.INFO
```

### 2. 关键指标说明

日志中会显示以下性能指标:

```
ContextualRankingFilter Performance:
  - Candidates processed: 20        # 处理的候选词数量
  - Total queries: 40               # 总查询次数 (每个候选词2次)
  - Query time: 45000μs (45.0ms)    # 所有查询的总耗时
  - Avg per query: 1125μs           # 每次查询的平均耗时
  - Sort time: 50μs                 # 排序耗时
  - Total time: 46000μs (46.0ms)    # 整个过滤器的总耗时
```

### 3. 性能判断标准

**正常性能:**
- 单次Query耗时: < 500μs (0.5ms)
- 总耗时: < 20ms
- 用户无明显感知

**性能问题:**
- 单次Query耗时: > 2000μs (2ms)
- 总耗时: > 50ms
- 用户会感觉卡顿

**严重卡顿:**
- 单次Query耗时: > 5000μs (5ms)
- 总耗时: > 100ms
- 明显影响输入体验

---

## 预期测试结果

根据我的分析,在默认配置下 (max_rerank_candidates=20):

### 问题场景:
```
输入序列: "nihao" (5个字符)
上下文: "我爱"

预期耗时:
- 每个字符触发一次重排序
- 每次重排序: 20个候选词 × 2次查询 = 40次Query
- 每次Query: 1-2ms
- 单次总耗时: 40-80ms
- 5个字符总耗时: 200-400ms

用户感受: 明显卡顿
```

### 优化后 (max_rerank_candidates=8):
```
预期耗时:
- 每次重排序: 8个候选词 × 2次查询 = 16次Query
- 单次总耗时: 16-32ms
- 5个字符总耗时: 80-160ms

用户感受: 轻微延迟,可接受
```

---

## 验证优化效果

### 1. 修改配置

编辑输入方案文件 (如 `user_profile/luna_pinyin.schema.yaml`):

```yaml
contextual_ranking_filter:
  max_rerank_candidates: 8  # 从20改为8
```

### 2. 重新部署

```bash
./build/bin/rime_api_console

# 在交互界面输入:
reload

# 然后重新运行测试:
test performance
```

### 3. 对比结果

对比修改前后的日志:
- Query time 应该减少约75%
- Total time 应该减少约60-70%

---

## 故障排查

### 问题1: 没有日志输出

**原因**: 日志级别设置过高

**解决**: 检查 `rime_api_console.cc` 中的日志设置:
```cpp
traits.min_log_level = 0;  // 0=INFO, 1=WARNING, 2=ERROR
```

### 问题2: 看不到性能统计

**原因**: 代码可能没有重新编译

**解决**: 
```bash
# 清理并重新编译
rm -rf build
cmake -B build -DBUILD_TEST=OFF
cmake --build build -j8
```

### 问题3: 上下文排序没有生效

**原因**: 
1. 方案中没有启用 contextual_ranking_filter
2. 没有语法数据库文件

**解决**:
1. 检查方案配置中是否有 `contextual_ranking_filter`
2. 确保有对应的 `.gram` 文件

---

## 附加测试场景

### 测试1: 不同上下文长度

```bash
# 短上下文
set context 我|

# 中等上下文
set context 我爱你|

# 长上下文
set context 今天天气真好我爱|
```

### 测试2: 不同输入长度

```bash
# 短输入 (2个字符)
ni

# 中等输入 (5个字符)
nihao

# 长输入 (10个字符)
nihaoshijie
```

### 测试3: 有无上下文对比

```bash
# 无上下文
clear context
nihao

# 有上下文
set context 我爱|
nihao
```

---

## 总结

通过这个测试,你可以:
1. ✅ 验证性能问题的存在
2. ✅ 量化实际耗时
3. ✅ 验证优化方案的效果
4. ✅ 找到最佳的配置参数

测试结果将帮助我们决定:
- 是否需要实现智能触发策略
- 是否需要添加查询缓存
- 最佳的 max_rerank_candidates 值
