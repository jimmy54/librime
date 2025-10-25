/*
 * Copyright RIME Developers
 * Distributed under the BSD License
 *
 * 2011-08-29 GONG Chen <chen.sst@gmail.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rime_api.h>
#include "codepage.h"

void print_status(RimeStatus* status) {
  printf("schema: %s / %s\n", status->schema_id, status->schema_name);
  printf("status: ");
  if (status->is_disabled)
    printf("disabled ");
  if (status->is_composing)
    printf("composing ");
  if (status->is_ascii_mode)
    printf("ascii ");
  if (status->is_full_shape)
    printf("full_shape ");
  if (status->is_simplified)
    printf("simplified ");
  printf("\n");
}

void print_composition(RimeComposition* composition) {
  const char* preedit = composition->preedit;
  if (!preedit)
    return;
  size_t len = strlen(preedit);
  size_t start = composition->sel_start;
  size_t end = composition->sel_end;
  size_t cursor = composition->cursor_pos;
  for (size_t i = 0; i <= len; ++i) {
    if (start < end) {
      if (i == start) {
        putchar('[');
      } else if (i == end) {
        putchar(']');
      }
    }
    if (i == cursor)
      putchar('|');
    if (i < len)
      putchar(preedit[i]);
  }
  printf("\n");
}

void print_menu(RimeMenu* menu) {
  if (menu->num_candidates == 0)
    return;
  printf("page: %d%c (of size %d)\n", menu->page_no + 1,
         menu->is_last_page ? '$' : ' ', menu->page_size);
  for (int i = 0; i < menu->num_candidates; ++i) {
    bool highlighted = i == menu->highlighted_candidate_index;
    printf("%d. %c%s%c%s\n", i + 1, highlighted ? '[' : ' ',
           menu->candidates[i].text, highlighted ? ']' : ' ',
           menu->candidates[i].comment ? menu->candidates[i].comment : "");
  }
}

void print_context(RimeContext* context) {
  if (context->composition.length > 0 || context->menu.num_candidates > 0) {
    print_composition(&context->composition);
  } else {
    printf("(not composing)\n");
  }
  print_menu(&context->menu);
}

// Global variable to accumulate committed text for context
static char g_accumulated_context[1000] = {0};
static bool g_auto_context_enabled = true;

void print(RimeSessionId session_id) {
  RimeApi* rime = rime_get_api();

  RIME_STRUCT(RimeCommit, commit);
  RIME_STRUCT(RimeStatus, status);
  RIME_STRUCT(RimeContext, context);

  if (rime->get_commit(session_id, &commit)) {
    printf("commit: %s\n", commit.text);

    // Auto-update context with committed text
    if (g_auto_context_enabled && RIME_API_AVAILABLE(rime, set_context_text)) {
      // Append new commit to accumulated context
      size_t current_len = strlen(g_accumulated_context);
      size_t commit_len = strlen(commit.text);

      // Keep last ~30 characters (about 10 Chinese characters)
      const size_t max_context_len = 30;
      if (current_len + commit_len > max_context_len) {
        // Shift left to make room
        size_t shift = current_len + commit_len - max_context_len;
        memmove(g_accumulated_context, g_accumulated_context + shift,
                current_len - shift);
        g_accumulated_context[current_len - shift] = '\0';
        current_len = strlen(g_accumulated_context);
      }

      // Append new commit
      strncat(g_accumulated_context, commit.text,
              sizeof(g_accumulated_context) - current_len - 1);

      // Set as external context
      rime->set_context_text(session_id, g_accumulated_context, "");
      printf("  [auto context: \"%s\"]\n", g_accumulated_context);
    }

    rime->free_commit(&commit);
  }

  if (rime->get_status(session_id, &status)) {
    print_status(&status);
    rime->free_status(&status);
  }

  if (rime->get_context(session_id, &context)) {
    print_context(&context);
    rime->free_context(&context);
  }
}

bool execute_special_command(const char* line, RimeSessionId session_id) {
  RimeApi* rime = rime_get_api();
  if (!strcmp(line, "print schema list")) {
    RimeSchemaList list;
    if (rime->get_schema_list(&list)) {
      printf("schema list:\n");
      for (size_t i = 0; i < list.size; ++i) {
        printf("%lu. %s [%s]\n", (i + 1), list.list[i].name,
               list.list[i].schema_id);
      }
      rime->free_schema_list(&list);
    }
    char current[100] = {0};
    if (rime->get_current_schema(session_id, current, sizeof(current))) {
      printf("current schema: [%s]\n", current);
    }
    return true;
  }
  const char* kSelectSchemaCommand = "select schema ";
  size_t command_length = strlen(kSelectSchemaCommand);
  if (!strncmp(line, kSelectSchemaCommand, command_length)) {
    const char* schema_id = line + command_length;
    if (rime->select_schema(session_id, schema_id)) {
      printf("selected schema: [%s]\n", schema_id);
    }
    return true;
  }
  const char* kSelectCandidateCommand = "select candidate ";
  command_length = strlen(kSelectCandidateCommand);
  if (!strncmp(line, kSelectCandidateCommand, command_length)) {
    int index = atoi(line + command_length);
    if (index > 0 &&
        rime->select_candidate_on_current_page(session_id, index - 1)) {
      print(session_id);
    } else {
      fprintf(stderr, "cannot select candidate at index %d.\n", index);
    }
    return true;
  }
  if (!strcmp(line, "print candidate list")) {
    RimeCandidateListIterator iterator = {0};
    if (rime->candidate_list_begin(session_id, &iterator)) {
      while (rime->candidate_list_next(&iterator)) {
        printf("%d. %s", iterator.index + 1, iterator.candidate.text);
        if (iterator.candidate.comment)
          printf(" (%s)", iterator.candidate.comment);
        putchar('\n');
      }
      rime->candidate_list_end(&iterator);
    } else {
      printf("no candidates.\n");
    }
    return true;
  }
  const char* kSetOptionCommand = "set option ";
  command_length = strlen(kSetOptionCommand);
  if (!strncmp(line, kSetOptionCommand, command_length)) {
    Bool is_on = True;
    const char* option = line + command_length;
    if (*option == '!') {
      is_on = False;
      ++option;
    }
    rime->set_option(session_id, option, is_on);
    printf("%s set %s.\n", option, is_on ? "on" : "off");
    return true;
  }
  if (!strcmp(line, "synchronize")) {
    return rime->sync_user_data();
  }
  const char* kDeleteCandidateOnCurrentPage = "delete on current page ";
  command_length = strlen(kDeleteCandidateOnCurrentPage);
  if (!strncmp(line, kDeleteCandidateOnCurrentPage, command_length)) {
    const char* index_str = line + command_length;
    int index = atoi(index_str);
    if (!rime->delete_candidate_on_current_page(session_id, index)) {
      fprintf(stderr, "failed to delete\n");
    }
    return true;
  }
  const char* kDeleteCandidate = "delete ";
  command_length = strlen(kDeleteCandidate);
  if (!strncmp(line, kDeleteCandidate, command_length)) {
    const char* index_str = line + command_length;
    int index = atoi(index_str);
    if (!rime->delete_candidate(session_id, index)) {
      fprintf(stderr, "failed to delete\n");
    }
    return true;
  }
  // Set context text command: "set context <left_text> | <right_text>"
  const char* kSetContextCommand = "set context ";
  command_length = strlen(kSetContextCommand);
  if (!strncmp(line, kSetContextCommand, command_length)) {
    const char* context_text = line + command_length;
    char left[256] = {0};
    char right[256] = {0};

    // Parse left and right context separated by '|'
    const char* separator = strchr(context_text, '|');
    if (separator) {
      size_t left_len = separator - context_text;
      if (left_len > 0 && left_len < sizeof(left)) {
        strncpy(left, context_text, left_len);
        // Trim trailing spaces
        while (left_len > 0 && left[left_len - 1] == ' ') {
          left[--left_len] = '\0';
        }
      }
      // Copy right context, skip leading spaces
      const char* right_start = separator + 1;
      while (*right_start == ' ')
        right_start++;
      strncpy(right, right_start, sizeof(right) - 1);
    } else {
      // Only left context provided
      strncpy(left, context_text, sizeof(left) - 1);
    }

    if (RIME_API_AVAILABLE(rime, set_context_text)) {
      if (rime->set_context_text(session_id, left, right)) {
        printf("✓ Context set: left=\"%s\", right=\"%s\"\n", left, right);
      } else {
        fprintf(stderr, "✗ Failed to set context\n");
      }
    } else {
      fprintf(stderr, "✗ set_context_text API not available\n");
    }
    return true;
  }
  // Clear context command
  if (!strcmp(line, "clear context")) {
    if (RIME_API_AVAILABLE(rime, clear_context_text)) {
      rime->clear_context_text(session_id);
      g_accumulated_context[0] = '\0';  // Also clear accumulated context
      printf("✓ Context cleared\n");
    } else {
      fprintf(stderr, "✗ clear_context_text API not available\n");
    }
    return true;
  }
  // Toggle auto context
  if (!strcmp(line, "auto context on")) {
    g_auto_context_enabled = true;
    printf("✓ Auto context enabled\n");
    return true;
  }
  if (!strcmp(line, "auto context off")) {
    g_auto_context_enabled = false;
    printf("✓ Auto context disabled\n");
    return true;
  }
  // Show current context
  if (!strcmp(line, "show context")) {
    printf("Auto context: %s\n", g_auto_context_enabled ? "ON" : "OFF");
    printf("Accumulated context: \"%s\"\n", g_accumulated_context);
    return true;
  }
  
  // Test RimeSetInputEx - exact match command
  const char* kSetInputExCommand = "set input ex ";
  command_length = strlen(kSetInputExCommand);
  if (!strncmp(line, kSetInputExCommand, command_length)) {
    const char* params = line + command_length;
    char input[256] = {0};
    int exact_length = 0;
    
    // Parse: "set input ex <input> <exact_length>"
    if (sscanf(params, "%s %d", input, &exact_length) == 2) {
      if (RIME_API_AVAILABLE(rime, set_input_ex)) {
        rime->set_input_ex(session_id, input, exact_length);
        printf("✓ Set input: \"%s\" with exact_length=%d\n", input, exact_length);
        print(session_id);
      } else {
        printf("✗ RimeSetInputEx API not available\n");
      }
    } else {
      printf("Usage: set input ex <input> <exact_length>\n");
      printf("Example: set input ex bubu 2\n");
    }
    return true;
  }
  
  // Run exact match test suite
  if (!strcmp(line, "test exact match")) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  RimeSetInputEx 部分精确匹配功能测试\n");
    printf("═══════════════════════════════════════════════════════════\n\n");
    
    if (!RIME_API_AVAILABLE(rime, set_input_ex)) {
      printf("✗ RimeSetInputEx API not available!\n");
      return true;
    }
    
    // Test 1: 全部派生（默认行为）
    printf("【测试 1】全部派生（exact_length=0，默认行为）\n");
    printf("─────────────────────────────────────────────────────────\n");
    printf("输入: \"bubu\", exact_length=0\n");
    printf("预期: 包含所有派生组合的候选\n\n");
    rime->set_input_ex(session_id, "bubu", 0);
    print(session_id);
    printf("\n");
    
    // Test 2: 前2码精确
    printf("【测试 2】前2码精确（部分精确匹配）\n");
    printf("─────────────────────────────────────────────────────────\n");
    printf("输入: \"bubu\", exact_length=2\n");
    printf("预期: 第一个音节只有 bu，第二个音节可派生\n");
    printf("      应包含: 不步、不比、不你...\n");
    printf("      不应包含: 比步、比比...\n\n");
    rime->clear_composition(session_id);
    rime->set_input_ex(session_id, "bubu", 2);
    print(session_id);
    printf("\n");
    
    // Test 3: 全部精确
    printf("【测试 3】全部精确（exact_length=4）\n");
    printf("─────────────────────────────────────────────────────────\n");
    printf("输入: \"bubu\", exact_length=4\n");
    printf("预期: 两个音节都是 bu\n");
    printf("      应包含: 不步、不部...\n");
    printf("      不应包含: 不比、不你...\n\n");
    rime->clear_composition(session_id);
    rime->set_input_ex(session_id, "bubu", 4);
    print(session_id);
    printf("\n");
    
    // Test 4: 负数（全部精确）
    printf("【测试 4】负数处理（exact_length=-1）\n");
    printf("─────────────────────────────────────────────────────────\n");
    printf("输入: \"bubu\", exact_length=-1\n");
    printf("预期: 等同于 exact_length=4（全部精确）\n\n");
    rime->clear_composition(session_id);
    rime->set_input_ex(session_id, "bubu", -1);
    print(session_id);
    printf("\n");
    
    // Test 5: 超长（限制为输入长度）
    printf("【测试 5】超长处理（exact_length=100）\n");
    printf("─────────────────────────────────────────────────────────\n");
    printf("输入: \"bubu\", exact_length=100\n");
    printf("预期: 等同于 exact_length=4（限制为输入长度）\n\n");
    rime->clear_composition(session_id);
    rime->set_input_ex(session_id, "bubu", 100);
    print(session_id);
    printf("\n");
    
    // Test 6: 单音节精确
    printf("【测试 6】单音节精确\n");
    printf("─────────────────────────────────────────────────────────\n");
    printf("输入: \"bu\", exact_length=2\n");
    printf("预期: 只有 bu 音节\n\n");
    rime->clear_composition(session_id);
    rime->set_input_ex(session_id, "bu", 2);
    print(session_id);
    printf("\n");
    
    // Test 7: 三音节测试
    printf("【测试 7】三音节测试（前4码精确）\n");
    printf("─────────────────────────────────────────────────────────\n");
    printf("输入: \"bububi\", exact_length=4\n");
    printf("预期: 前两个音节精确（bu+bu），第三个音节可派生\n\n");
    rime->clear_composition(session_id);
    rime->set_input_ex(session_id, "bububi", 4);
    print(session_id);
    printf("\n");
    
    // Test 8: 智能精确匹配长度管理
    printf("【测试 8】智能精确匹配长度管理（V2.1 新特性）\n");
    printf("─────────────────────────────────────────────────────────\n");
    printf("场景: 逐步选择候选，系统自动管理精确长度\n\n");
    
    printf("步骤 1: 输入 \"bu\", exact_length=0（全部派生）\n");
    rime->clear_composition(session_id);
    rime->set_input_ex(session_id, "bu", 0);
    print(session_id);
    printf("→ 候选包含: 不、步、比、你...\n\n");
    
    printf("步骤 2: 模拟选择第一个候选（\"不\"）后的状态\n");
    printf("→ 系统会自动设置 input_exact_length = 2\n");
    printf("→ 已选择部分（\"不\"）变为精确匹配\n\n");
    
    printf("步骤 3: 模拟继续输入后的状态 input = \"不bu\"\n");
    printf("→ 使用 set_input_ex(\"不bu\", 2) 模拟\n");
    rime->clear_composition(session_id);
    rime->set_input_ex(session_id, "不bu", 2);
    print(session_id);
    printf("→ 前2码（\"不\"）精确匹配\n");
    printf("→ 后2码（\"bu\"）可以派生\n");
    printf("→ 候选应包含: 不步、不比、不你...\n");
    printf("→ 候选不应包含: 你不、比不...（第一个音节不是 bu）\n\n");
    
    printf("💡 关键特性：\n");
    printf("  - 选择候选后，input_exact_length 自动更新为已选择部分的长度\n");
    printf("  - 已选择 = 已确认 = 精确匹配\n");
    printf("  - 未选择部分仍可派生\n");
    printf("  - 无需手动管理精确长度\n\n");
    
    // Test 9: 对比测试（旧API）
    printf("【测试 9】对比测试：使用旧 API RimeSetInput\n");
    printf("─────────────────────────────────────────────────────────\n");
    printf("输入: \"bubu\" (使用 RimeSetInput)\n");
    printf("预期: 等同于 exact_length=0（全部派生）\n\n");
    rime->clear_composition(session_id);
    rime->set_input(session_id, "bubu");
    print(session_id);
    printf("\n");
    
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  测试完成！\n");
    printf("═══════════════════════════════════════════════════════════\n\n");
    
    printf("💡 提示：\n");
    printf("  - 如果方案没有配置 derive 规则，所有测试结果可能相同\n");
    printf("  - 建议使用 14键拼音方案测试（有 derive/i/u/ 等规则）\n");
    printf("  - 可以使用 'set input ex <input> <length>' 手动测试\n");
    printf("  - 使用 'select schema <schema_id>' 切换方案\n");
    printf("  - V2.1 新特性：选择候选后，系统自动管理精确匹配长度\n\n");
    
    return true;
  }
  
  // Show help for new commands
  if (!strcmp(line, "help exact match")) {
    printf("\n");
    printf("RimeSetInputEx 测试命令帮助\n");
    printf("═══════════════════════════════════════════════════════════\n\n");
    printf("命令列表：\n");
    printf("  test exact match\n");
    printf("      运行完整的测试套件\n\n");
    printf("  set input ex <input> <exact_length>\n");
    printf("      手动测试部分精确匹配\n");
    printf("      参数：\n");
    printf("        <input>         - 输入字符串\n");
    printf("        <exact_length>  - 精确匹配长度\n");
    printf("          = 0  : 全部派生（默认）\n");
    printf("          > 0  : 前N个字符精确，后续派生\n");
    printf("          < 0  : 全部精确\n\n");
    printf("示例：\n");
    printf("  set input ex bubu 2    # 前2码精确\n");
    printf("  set input ex bubu 0    # 全部派生\n");
    printf("  set input ex bubu -1   # 全部精确\n\n");
    printf("推荐测试方案：\n");
    printf("  1. 选择 14键拼音方案：\n");
    printf("     select schema rime_ice_14\n\n");
    printf("  2. 运行测试套件：\n");
    printf("     test exact match\n\n");
    printf("  3. 手动测试：\n");
    printf("     set input ex bubu 2\n");
    printf("     select candidate 1\n\n");
    printf("═══════════════════════════════════════════════════════════\n\n");
    return true;
  }
  
  return false;
}

void on_message(void* context_object,
                RimeSessionId session_id,
                const char* message_type,
                const char* message_value) {
  printf("message: [%lu] [%s] %s\n", session_id, message_type, message_value);
  RimeApi* rime = rime_get_api();
  if (RIME_API_AVAILABLE(rime, get_state_label) &&
      !strcmp(message_type, "option")) {
    Bool state = message_value[0] != '!';
    const char* option_name = message_value + !state;
    const char* state_label =
        rime->get_state_label(session_id, option_name, state);
    if (state_label) {
      printf("updated option: %s = %d // %s\n", option_name, state,
             state_label);
    }
  }
}

RimeSessionId ensure_session(RimeApi* rime) {
  RimeSessionId id = rime->create_session();
  if (!id) {
    fprintf(stderr, "Error creating rime session.\n");
  }
  return id;
}

int main(int argc, char* argv[]) {
  unsigned int codepage = SetConsoleOutputCodePage();
  RimeApi* rime = rime_get_api();

  RIME_STRUCT(RimeTraits, traits);
  traits.app_name = "rime.console";
  traits.user_profile_dir = "./user_profile";
//  traits.staging_dir = "./user_profile/build";
  traits.log_dir = "./user_profile/log";
  traits.min_log_level = 0;  // Enable INFO level logging for debugging


  //bim-pinyin
  traits.shared_data_dir = "/Users/jimmy54/Documents/job/BIM/hmos/hmosbim/hmosbim/products/phone/src/main/resources/resfile/SharedSupport";
//  traits.user_data_dir = "/Users/jimmy54/Documents/job/BIM/hmos/hmosbim/hmosbim/products/phone/src/main/resources/resfile/space/schemas/bim-pinyin";
  traits.user_data_dir = "./bim-pinyin";

  rime->setup(&traits);

  rime->set_notification_handler(&on_message, NULL);

  fprintf(stderr, "initializing...\n");
reload:
  rime->initialize(NULL);
  Bool full_check = True;
  if (rime->start_maintenance(full_check))
    rime->join_maintenance_thread();
  fprintf(stderr, "ready.\n");

  RimeSessionId session_id = 0;
  const int kMaxLength = 99;
  char line[kMaxLength + 1] = {0};
  while (fgets(line, kMaxLength, stdin) != NULL) {
    for (char* p = line; *p; ++p) {
      if (*p == '\r' || *p == '\n') {
        *p = '\0';
        break;
      }
    }
    if (!rime->find_session(session_id) &&
        !(session_id = ensure_session(rime))) {
      SetConsoleOutputCodePage(codepage);
      return 1;
    }
    if (!strcmp(line, "exit"))
      break;
    else if (!strcmp(line, "reload")) {
      rime->destroy_session(session_id);
      rime->finalize();
      goto reload;
    }
    if (execute_special_command(line, session_id))
      continue;
    if (rime->simulate_key_sequence(session_id, line)) {
      print(session_id);
    } else {
      fprintf(stderr, "Error processing key sequence: %s\n", line);
    }
  }

  rime->destroy_session(session_id);

  rime->finalize();

  SetConsoleOutputCodePage(codepage);
  return 0;
}
