#include "inkpages/model/PageMap.h"

#include <algorithm>

namespace inkpages
{
    PageMap PageMap::Insert(int count, int at, int inserted)
    {
        at = std::clamp(at, 0, count);
        PageMap map{ std::vector<int>(count), count + inserted };
        for (int i = 0; i < count; ++i) map.newIndex[i] = i < at ? i : i + inserted;
        return map;
    }

    PageMap PageMap::Remove(int count, int page)
    {
        PageMap map{ std::vector<int>(count), count - (page >= 0 && page < count ? 1 : 0) };
        for (int i = 0; i < count; ++i) map.newIndex[i] = i < page ? i : i == page ? -1 : i - 1;
        return map;
    }

    PageMap PageMap::Move(int count, int from, int to)
    {
        PageMap map{ std::vector<int>(count), count };
        for (int i = 0; i < count; ++i) map.newIndex[i] = i;
        if (from < 0 || from >= count || to < 0 || to >= count) return map;
        for (int i = 0; i < count; ++i)
        {
            if (i == from) map.newIndex[i] = to;
            else if (from < to && i > from && i <= to) map.newIndex[i] = i - 1;
            else if (to < from && i >= to && i < from) map.newIndex[i] = i + 1;
        }
        return map;
    }

    std::set<int> PageMap::Apply(std::set<int> const& pages) const
    {
        std::set<int> moved;
        for (int page : pages)
        {
            if (int n = (*this)(page); n >= 0) moved.insert(n);
        }
        return moved;
    }

    std::vector<int> PageMap::OldIndex() const
    {
        std::vector<int> old(newCount, -1);
        for (int i = 0; i < static_cast<int>(newIndex.size()); ++i)
        {
            if (newIndex[i] >= 0 && newIndex[i] < newCount) old[newIndex[i]] = i;
        }
        return old;
    }
}
