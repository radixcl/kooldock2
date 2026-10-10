// SPDX-License-Identifier: GPL-2.0-or-later

#include "item.h"

int main()
{
    Item task(Item::Kind::Task, QStringLiteral("App"), {}, {}, 1);
    task.addGroupedWindowId(2);
    if (!task.isGrouped() || task.nextWindowId() != 1) return 1;
    task.setPrimaryWindowId(1);
    if (task.nextWindowId() != 2) return 2;
    task.removeGroupedWindowId(1);
    return task.isGrouped() || task.windowId() != 2 ? 3 : 0;
}
