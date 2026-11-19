#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "inkpages/model/PageMap.h"
#include "inkpages/model/UndoHistory.h"

namespace inkpages::tests
{
    TEST(PageMap, InsertShiftsTheLaterPages)
    {
        PageMap map = PageMap::Insert(4, 1, 2);
        EXPECT_EQ((std::vector<int>{ 0, 3, 4, 5 }), map.newIndex);
        EXPECT_EQ(6, map.newCount);
        EXPECT_EQ((std::vector<int>{ 0, -1, -1, 1, 2, 3 }), map.OldIndex()) << "two new pages at 1 and 2";
        EXPECT_EQ((std::vector<int>{ 0, 1, 2, 3 }), PageMap::Insert(4, 99, 1).newIndex) << "at the end";
    }

    TEST(PageMap, RemoveDropsThePageAndShiftsTheLaterOnes)
    {
        PageMap map = PageMap::Remove(4, 1);
        EXPECT_EQ((std::vector<int>{ 0, -1, 1, 2 }), map.newIndex);
        EXPECT_TRUE(map.Removes(1));
        EXPECT_EQ(3, map.newCount);
        EXPECT_EQ((std::set<int>{ 0, 2 }), map.Apply({ 0, 1, 3 }));
    }

    TEST(PageMap, MoveDownAndUp)
    {
        EXPECT_EQ((std::vector<int>{ 2, 0, 1, 3 }), PageMap::Move(4, 0, 2).newIndex);
        EXPECT_EQ((std::vector<int>{ 1, 2, 0, 3 }), PageMap::Move(4, 2, 0).newIndex);
        EXPECT_EQ((std::vector<int>{ 0, 1, 2 }), PageMap::Move(3, 1, 7).newIndex) << "out of range: nothing moves";
    }

    struct PageEdit : Edit
    {
        explicit PageEdit(int page) : page(page) {}
        std::set<int> Pages() const override { return { page }; }
        bool Empty() const override { return false; }
        void Undo() override {}
        void Redo() override {}
        void RemapPages(PageMap const& map) override { page = map(page); }
        int page;
    };

    TEST(UndoHistory, FollowsPageOperationsAndDropsEditsOfARemovedPage)
    {
        UndoHistory history;
        auto onFirst = std::make_shared<PageEdit>(0);
        auto onSecond = std::make_shared<PageEdit>(1);
        auto onThird = std::make_shared<PageEdit>(2);
        history.Push(onFirst);
        history.Push(onSecond);
        history.Push(onThird);
        history.RemapPages(PageMap::Insert(3, 0, 1)); // a page before all
        EXPECT_EQ((std::set<int>{ 1, 2, 3 }), history.Pages());
        history.RemapPages(PageMap::Remove(4, 2));     // the second page (now index 2) goes, with its edit
        EXPECT_EQ((std::set<int>{ 1, 2 }), history.Pages());
        EXPECT_EQ(1, onFirst->page);
        EXPECT_EQ(2, onThird->page);
        history.RemapPages(PageMap::Move(3, 2, 0));
        EXPECT_EQ(0, onThird->page);
        EXPECT_EQ(2, onFirst->page);
        EXPECT_TRUE(history.Undo());
        EXPECT_TRUE(history.Undo());
        EXPECT_FALSE(history.Undo()) << "the removed page's edit is gone";
    }
}
