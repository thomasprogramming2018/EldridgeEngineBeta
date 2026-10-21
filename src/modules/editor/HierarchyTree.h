#pragma once

#include <QTreeWidget>
#include <QDropEvent>

class HierarchyTree : public QTreeWidget
{
    Q_OBJECT

public:
    explicit HierarchyTree(QWidget* parent = nullptr)
        : QTreeWidget(parent)
    {
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::InternalMove);
        setDefaultDropAction(Qt::MoveAction);
        setSelectionMode(QAbstractItemView::SingleSelection);
    }

signals:
    void reparentRequested(
        QTreeWidgetItem* dragged,
        QTreeWidgetItem* parent
    );

protected:
    void dropEvent(QDropEvent* event) override
    {
        auto* dragged = currentItem();
        auto* target = itemAt(event->position().toPoint());

        if (event->source() != this || !dragged)
        {
            event->ignore();
            return;
        }

        // First version: drop onto an object or empty background.
        if (target && dropIndicatorPosition() != QAbstractItemView::OnItem)
        {
            event->ignore();
            return;
        }

        event->ignore();
        emit reparentRequested(dragged, target);
    }
};