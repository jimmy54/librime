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
  // Test context ranking
  if (!strcmp(line, "test context ranking")) {
    printf("\n=== Testing Context Ranking ===\n\n");

    // Test 1: "吃" + "fan"
    printf("[Test 1] Context='吃', Input='fan'\n");
    if (RIME_API_AVAILABLE(rime, set_context_text)) {
      rime->set_context_text(session_id, "吃", "");
    }
    rime->clear_composition(session_id);
    if (rime->simulate_key_sequence(session_id, "fan")) {
      RIME_STRUCT(RimeContext, context);
      if (rime->get_context(session_id, &context)) {
        printf("Candidates:\n");
        for (int i = 0; i < context.menu.num_candidates && i < 5; ++i) {
          printf("  %d. %s", i + 1, context.menu.candidates[i].text);
          if (!strcmp(context.menu.candidates[i].text, "饭")) {
            printf(" ← Target (expected #1)");
          }
          printf("\n");
        }
        rime->free_context(&context);
      }
    }

    // Test 2: "一心一" + "yi"
    printf(
        "\n[Test 2] "
        "Context='中阿斯顿士大夫阿斯顿苛夺苛夺地厅苛夺仍失遥末夺一心一', "
        "Input='yi'\n");
    if (RIME_API_AVAILABLE(rime, set_context_text)) {
      rime->set_context_text(session_id, "一心一", "");
    }
    rime->clear_composition(session_id);
    if (rime->simulate_key_sequence(session_id, "yi")) {
      RIME_STRUCT(RimeContext, context);
      if (rime->get_context(session_id, &context)) {
        printf("Candidates:\n");
        for (int i = 0; i < context.menu.num_candidates && i < 5; ++i) {
          printf("  %d. %s", i + 1, context.menu.candidates[i].text);
          if (!strcmp(context.menu.candidates[i].text, "意")) {
            printf(" ← Target (expected #1)");
          }
          printf("\n");
        }
        rime->free_context(&context);
      }
    }

    // Clear context
    if (RIME_API_AVAILABLE(rime, clear_context_text)) {
      rime->clear_context_text(session_id);
    }
    rime->clear_composition(session_id);

    printf("\n=== Test Complete ===\n");
    printf("Check log: ./user_profile/log/rime.INFO\n\n");
    return true;
  }
  // Performance test for context ranking
  if (!strcmp(line, "test performance")) {
    printf("\n=== Performance Test: Context Ranking ===\n\n");
    printf("This test simulates fast continuous typing to measure latency.\n");
    printf("Check the log file for detailed timing information.\n\n");

    // Test scenario: Fast typing "nihao" with context
    const char* test_sequence = "nihao";
    const char* context = "我爱";

    printf("Scenario: Fast typing '%s' with context '%s'\n", test_sequence,
           context);
    printf("Expected behavior: Each keystroke should complete quickly\n\n");

    // Set context
    if (RIME_API_AVAILABLE(rime, set_context_text)) {
      rime->set_context_text(session_id, context, "");
      printf("✓ Context set: \"%s\"\n\n", context);
    }

    // Simulate fast typing - one character at a time
    rime->clear_composition(session_id);

    for (size_t i = 0; i < strlen(test_sequence); ++i) {
      char key[2] = {test_sequence[i], '\0'};

      printf("[Keystroke %zu] Input: '%c'\n", i + 1, test_sequence[i]);

      // Process the key
      if (rime->process_key(session_id, test_sequence[i], 0)) {
        // Get candidates
        RIME_STRUCT(RimeContext, context_result);
        if (rime->get_context(session_id, &context_result)) {
          printf("  Candidates: ");
          int show_count = context_result.menu.num_candidates < 3
                               ? context_result.menu.num_candidates
                               : 3;
          for (int j = 0; j < show_count; ++j) {
            printf("%s", context_result.menu.candidates[j].text);
            if (j < show_count - 1)
              printf(", ");
          }
          printf(" (total: %d)\n", context_result.menu.num_candidates);
          rime->free_context(&context_result);
        }
      }
      printf("\n");
    }

    printf("=== Performance Test Complete ===\n");
    printf("\nIMPORTANT: Check the log file for timing details:\n");
    printf("  ./user_profile/log/rime.INFO\n\n");
    printf("Look for lines containing:\n");
    printf("  - 'ContextualRankingFilter' - shows filter execution\n");
    printf("  - 'Query took' - shows individual query timing\n");
    printf("  - 'total=' - shows candidate scores\n\n");

    // Clear context
    if (RIME_API_AVAILABLE(rime, clear_context_text)) {
      rime->clear_context_text(session_id);
    }
    rime->clear_composition(session_id);

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
  traits.staging_dir = "./user_profile/build";
  traits.log_dir = "./user_profile/log";
  traits.min_log_level = 0;  // Enable INFO level logging for debugging

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
