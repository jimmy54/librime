#include <gtest/gtest.h>
#include <rime/dict/level_db.h>

using namespace rime;

class LevelDbLifetimeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_ = std::make_unique<LevelDb>(path{"level_db_lifetime_test.db"}, "lifetime");
    if (db_->Exists()) db_->Remove();
    ASSERT_TRUE(db_->Open());
    ASSERT_TRUE(db_->Update("key", "value"));
  }
  void TearDown() override {
    if (db_) {
      if (db_->loaded()) db_->Close();
      db_->Remove();
    }
  }
  the<LevelDb> db_;
};

TEST_F(LevelDbLifetimeTest, CloseInvalidatesRetainedAccessorBeforeIteratorGC) {
  auto accessor = db_->Query("");
  ASSERT_TRUE(accessor);
  ASSERT_FALSE(accessor->exhausted());
  ASSERT_TRUE(db_->Close());
  EXPECT_TRUE(accessor->exhausted());
  EXPECT_FALSE(accessor->Reset());
  EXPECT_FALSE(accessor->Jump("key"));
  string key, value;
  EXPECT_FALSE(accessor->GetNextRecord(&key, &value));
  accessor.reset();
}

TEST_F(LevelDbLifetimeTest, ReopenReleasesFileLockAndKeepsOldCursorInvalid) {
  auto old = db_->Query("");
  ASSERT_TRUE(db_->Close());
  ASSERT_TRUE(db_->Open());
  auto current = db_->Query("key");
  ASSERT_TRUE(current);
  EXPECT_FALSE(current->exhausted());
  EXPECT_TRUE(old->exhausted());
  old.reset();
  string key, value;
  ASSERT_TRUE(current->GetNextRecord(&key, &value));
  EXPECT_EQ("key", key);
  EXPECT_EQ("value", value);
}

TEST_F(LevelDbLifetimeTest, DatabaseDestructionInvalidatesSurvivingAccessor) {
  auto accessor = db_->Query("");
  db_.reset();
  EXPECT_TRUE(accessor->exhausted());
  accessor.reset();
  LevelDb cleanup(path{"level_db_lifetime_test.db"}, "lifetime");
  EXPECT_TRUE(cleanup.Remove());
}

TEST(LevelDbAccessorTest, DefaultAccessorIsExhaustedAndSafeToDestroy) {
  LevelDbAccessor accessor;
  EXPECT_TRUE(accessor.exhausted());
  EXPECT_FALSE(accessor.Reset());
  EXPECT_FALSE(accessor.Jump("key"));
}
