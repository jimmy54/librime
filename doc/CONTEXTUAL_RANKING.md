# Contextual Ranking Filter 使用指南

## 功能说明

Contextual Ranking Filter 是一个基于上下文的候选词重排序过滤器，它可以：

1. **接收前端传入的上下文**：左侧文本和右侧文本
2. **利用 Octagram 语言模型**：计算候选词与上下文的搭配概率
3. **智能重排序**：根据上下文相关性重新排列候选词

## 使用场景

假设用户正在输入：
- 左侧上下文：`我爱`
- 光标位置：`[输入拼音]`
- 右侧上下文：`妈妈`

使用本功能后，候选词会根据上下文智能排序：
1. 你（"我爱你妈妈"更通顺）
2. 我
3. 我的

## API 使用方法

### 1. 设置上下文

```c
// 创建会话
RimeSessionId session_id = api->create_session();

// 设置上下文文本
api->set_context_text(session_id, "我爱", "妈妈");

// 正常输入
api->process_key(session_id, 'n', 0);
api->process_key(session_id, 'i', 0);

// 获取候选词（已根据上下文重排序）
RimeContext context = {0};
RIME_STRUCT_INIT(RimeContext, context);
api->get_context(session_id, &context);

// 清除上下文（可选）
api->clear_context_text(session_id);
```

### 2. C++ 示例

```cpp
#include <rime_api.h>

int main() {
    RimeApi* rime = rime_get_api();
    
    // 初始化
    RIME_STRUCT(RimeTraits, traits);
    traits.shared_data_dir = "/usr/share/rime-data";
    traits.user_data_dir = "~/.config/rime";
    traits.app_name = "rime.example";
    rime->setup(&traits);
    rime->initialize(&traits);
    
    // 创建会话
    RimeSessionId session = rime->create_session();
    
    // 设置上下文
    rime->set_context_text(session, "我爱", "妈妈");
    
    // 输入
    rime->process_key(session, 'n', 0);
    rime->process_key(session, 'i', 0);
    
    // 获取结果
    RIME_STRUCT(RimeContext, ctx);
    if (rime->get_context(session, &ctx)) {
        for (int i = 0; i < ctx.menu.num_candidates; ++i) {
            printf("%d. %s\n", i + 1, ctx.menu.candidates[i].text);
        }
        rime->free_context(&ctx);
    }
    
    // 清理
    rime->destroy_session(session);
    rime->finalize();
    
    return 0;
}
```

## 方案配置

在输入方案的 YAML 文件中启用该功能：

```yaml
# luna_pinyin.schema.yaml

engine:
  filters:
    - contextual_ranking_filter  # 添加上下文排序过滤器
    - simplifier
    - uniquifier

# 可选配置
contextual_ranking_filter:
  contextual_ranking: true        # 启用/禁用（默认：true）
  max_rerank_candidates: 20       # 最多重排序的候选词数量（默认：20）

# 语法模型配置
grammar:
  language: zh-hant               # 使用的语言模型
  contextual_suggestions: true    # 启用上下文建议
  collocation_max_length: 4       # 最大搭配长度
  collocation_min_length: 3       # 最小搭配长度
  collocation_penalty: -12        # 搭配惩罚分数
  non_collocation_penalty: -12    # 非搭配惩罚分数
  weak_collocation_penalty: -24   # 弱搭配惩罚分数
  rear_penalty: -18               # 句尾惩罚分数
```

## 工作原理

### 1. 上下文获取优先级

```
外部上下文（API 传入） > 内部上下文（已确认文本） > 提交历史
```

### 2. 评分机制

对每个候选词计算：

```
总分 = 原始质量分 + 左侧上下文分 + 右侧上下文分
```

其中：
- **左侧上下文分**：`Grammar::Query(左侧文本, 候选词, false)`
- **右侧上下文分**：`Grammar::Query(候选词, 右侧文本, true)`

### 3. 排序策略

- 收集前 N 个候选词（默认 20 个）
- 计算每个候选词的总分
- 按总分降序排列
- 将剩余候选词追加到末尾

## 性能优化

1. **限制重排序数量**：只对前 20 个候选词重排序，避免性能损失
2. **稳定排序**：使用 `std::stable_sort` 保持相同分数候选词的原始顺序
3. **延迟计算**：只在有上下文时才进行重排序

## 注意事项

1. **语法数据库**：需要预先训练好的 `.gram` 文件
2. **上下文长度**：Octagram 默认使用最后 3-4 个字符
3. **右侧上下文**：当前实现为简化版，理想情况需要双向语言模型
4. **配置启用**：需要在方案中明确添加 `contextual_ranking_filter`

## 调试

启用日志查看详细信息：

```yaml
# 在 RimeTraits 中设置
traits.min_log_level = 0;  // INFO 级别
traits.log_dir = "/tmp/rime_logs";
```

查看日志输出：
```
ContextualRankingFilter: left="我爱", right="妈妈"
Candidate: "你" quality=10.5 left=8.2 right=6.3 total=25.0
Candidate: "我" quality=12.0 left=5.1 right=3.2 total=20.3
```

## 扩展建议

### 未来改进方向

1. **双向语言模型**：训练支持右侧上下文的模型
2. **动态权重**：根据上下文长度调整权重
3. **缓存机制**：缓存常见上下文的评分结果
4. **多模型融合**：结合词频、语法、语义多个维度

## 相关文件

- API 接口：`src/rime_api.h`, `src/rime_api_impl.h`
- 上下文管理：`src/rime/context.h`, `src/rime/context.cc`
- 过滤器实现：`src/rime/gear/contextual_ranking_filter.h/cc`
- 翻译器集成：`src/rime/gear/script_translator.cc`, `src/rime/gear/table_translator.cc`
- 语法模型：`plugins/octagram/src/octagram.h/cc`

## 示例效果

### 输入场景 1
```
上下文：我爱 [输入:ni] 妈妈
原始排序：1.你 2.泥 3.逆 4.腻
优化排序：1.你 2.泥 3.腻 4.逆
```

### 输入场景 2
```
上下文：今天天气 [输入:hen] 好
原始排序：1.很 2.恨 3.痕 4.狠
优化排序：1.很 2.狠 3.痕 4.恨
```

## 常见问题

**Q: 为什么没有效果？**
A: 检查以下几点：
1. 方案配置中是否添加了 `contextual_ranking_filter`
2. 是否调用了 `set_context_text` API
3. 是否有可用的语法数据库文件
4. `contextual_suggestions` 是否为 true

**Q: 如何训练语法数据库？**
A: 参考 librime-octagram 插件文档，使用语料库训练 `.gram` 文件

**Q: 性能影响如何？**
A: 默认只重排序前 20 个候选词，对性能影响很小（< 1ms）

## 贡献

欢迎提交 Issue 和 Pull Request 改进此功能！
