#include <gtest/gtest.h>
#include <rime_api.h>
#include <rime/config.h>
#include <rime/schema.h>
#include <rime/service.h>

using namespace rime;

TEST(SwitchGroupsApiTest, ReadsMetadataAndSelectionFromActiveSession) {
  auto* api = rime_get_api();
  ASSERT_TRUE(RIME_API_AVAILABLE(api, get_switch_groups));
  const auto session_id = api->create_session();
  ASSERT_NE(0, session_id);
  const auto session = Service::instance().GetSession(session_id);
  ASSERT_TRUE(session);

  auto* config = new Config;
  auto list = (*config)["switches"];
  list[0]["name"] = "ascii_mode";
  list[0]["title"] = "语言";
  list[0]["description"] = "选择输入语言";
  list[0]["states"][0] = "中文";
  list[0]["states"][1] = "英文";
  list[0]["abbrev"][1] = "英";
  list[1]["options"][0] = "plain_form";
  list[1]["options"][1] = "alternate_form";
  list[1]["options"][2] = "regional_form";
  list[1]["title"] = "字形";
  list[1]["states"][0] = "常用";
  list[1]["states"][2] = "地区";
  auto* schema = new Schema;
  schema->set_config(config);
  session->ApplySchema(schema);
  api->set_option(session_id, "ascii_mode", True);

  auto count = 0;
  const auto check = [](void* data, const RimeSwitchGroup* group) {
    auto& count = *static_cast<int*>(data);
    if (count++ == 0) {
      EXPECT_STREQ(group->id, "ascii_mode");
      EXPECT_STREQ(group->title, "语言");
      EXPECT_STREQ(group->description, "选择输入语言");
      EXPECT_EQ(group->selected_index, 1);
      ASSERT_EQ(group->state_count, 2);
      EXPECT_EQ(group->option_names, nullptr);
      EXPECT_STREQ(group->states[1], "英文");
      EXPECT_STREQ(group->abbrev[1], "英");
    } else {
      EXPECT_STREQ(group->id, "plain_form");
      EXPECT_STREQ(group->title, "字形");
      EXPECT_EQ(group->description, nullptr);
      EXPECT_EQ(group->selected_index, -1);
      ASSERT_EQ(group->state_count, 3);
      ASSERT_NE(group->option_names, nullptr);
      EXPECT_STREQ(group->option_names[2], "regional_form");
      EXPECT_EQ(group->states[1], nullptr);
      EXPECT_STREQ(group->states[2], "地区");
    }
  };
  EXPECT_TRUE(api->get_switch_groups(session_id, check, &count));
  EXPECT_EQ(count, 2);
  api->set_option(session_id, "regional_form", True);
  int selected = -1;
  EXPECT_TRUE(api->get_switch_groups(
      session_id,
      [](void* data, const RimeSwitchGroup* group) {
        if (group->option_names)
          *static_cast<int*>(data) = group->selected_index;
      },
      &selected));
  EXPECT_EQ(selected, 2);
  EXPECT_FALSE(api->get_switch_groups(0, check, &count));
  EXPECT_TRUE(api->destroy_session(session_id));
}
