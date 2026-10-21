#pragma once
#include <QWidget>
class TerrainComponent;

class TerrainWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TerrainWidget(TerrainComponent& terrain,QWidget* parent=nullptr);
signals:
    void terrainChanged();
    void frameRequested(TerrainComponent* terrain);
    void brushRequested(TerrainComponent* terrain,int mode,float radius,float strength,int layer,float flatHeight);
};
