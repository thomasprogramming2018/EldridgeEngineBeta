#include "modules/componentsBackend/TerrainComponent/TerrainComponent.h"
#include "modules/componentsBackend/MeshComponent/MeshComponent.h"
#include "modules/componentsBackend/TransformComponent/TransformComponent.h"
#include "modules/project/ProjectFile.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    try
    {
        Scene scene;auto& object=scene.createTerrain("Landscape");auto* terrain=object.getComponent<TerrainComponent>();
        terrain->resize(33,32);
        require(terrain->heights.size()==1089&&terrain->chunks.size()==1,"Create terrain grid and chunks");
        require(terrain->chunks[0].levels[3]->indices.size()<terrain->chunks[0].levels[0]->indices.size(),"Chunk LOD reduces triangles");
        auto* mesh=object.getComponent<MeshComponent>();require(mesh&&mesh->source()==MeshComponent::Source::Terrain,"Terrain owns generated mesh");
        require(mesh->mesh->vertices[4]>0.99f,"Flat terrain has upward normals");
        terrain->generate(31,6,4);auto heights=terrain->heights;terrain->generate(31,6,4);
        require(heights==terrain->heights,"Noise is deterministic");
        float before=terrain->heightAt(0,0);terrain->brush(0,0,3,2,TerrainComponent::Brush::Raise);
        require(terrain->heightAt(0,0)>before+1.9f,"Raise brush");
        terrain->brush(0,0,3,2,TerrainComponent::Brush::Lower);require(std::abs(terrain->heightAt(0,0)-before)<0.001,"Lower brush");
        terrain->brush(0,0,3,1,TerrainComponent::Brush::Flatten,0,3);
        require(std::abs(terrain->heightAt(0,0)-3)<0.001,"Flatten brush");
        terrain->brush(0,0,3,0.7f,TerrainComponent::Brush::Paint,2);
        auto color=terrain->weights.pixelColor(16,16);
        require(color.blue()>150&&color.red()+color.green()+color.blue()+color.alpha()==255,"Paint normalized weights");
        float mass=std::accumulate(terrain->heights.begin(),terrain->heights.end(),0.f);
        terrain->erode(10,0.2f);
        require(std::abs(std::accumulate(terrain->heights.begin(),terrain->heights.end(),0.f)-mass)<0.02f,"Erosion conserves height mass");
        QVector3D hit;
        require(terrain->raycast({0,50,0},{0,-1,0},hit)&&std::abs(hit.y()-terrain->heightAt(0,0))<0.002f,"Heightfield ray intersection");
        require(!terrain->raycast({100,50,100},{0,-1,0},hit),"Ray misses outside terrain");
        require(!terrain->raycast({0,0,0},{0,0,0},hit),"Reject zero ray direction");
        auto* transform=object.getComponent<TransformComponent>();transform->position={2,5,3};
        require(terrain->raycast({2,50,3},{0,-1,0},hit)&&std::abs(hit.y()-terrain->heightAt(0,0)-5)<0.002f,"Raycast uses object transform");
        QTemporaryDir temp;QString error;
        terrain->heightOffset=-12;terrain->heightRange=24;
        require(terrain->exportHeightmap(temp.filePath("Height.png"),error),"Export 16-bit PNG");
        require(terrain->exportWeights(temp.filePath("Weights.png"),error),"Export RGBA weight map");
        Scene other;auto& second=other.createTerrain();auto* restored=second.getComponent<TerrainComponent>();restored->resize(33,32);restored->heightOffset=-12;restored->heightRange=24;
        require(restored->importHeightmap(temp.filePath("Height.png"),error),"Import heightmap");
        for(size_t i=0;i<terrain->heights.size();++i)require(std::abs(terrain->heights[i]-restored->heights[i])<0.001f,"Preserve 16-bit heights");
        require(restored->importWeights(temp.filePath("Weights.png"),error)&&restored->weights.pixelColor(16,16)==terrain->weights.pixelColor(16,16),"Round trip weights");
        auto snapshot=scene.toJson();require(snapshot["objects"].toArray()[0].toObject().contains("terrain")&&!snapshot["objects"].toArray()[0].toObject().contains("mesh"),"Save samples rather than duplicate generated meshes");
        scene.resetHistory();terrain->brush(0,0,2,1,TerrainComponent::Brush::Raise);scene.recordEdit();
        require(scene.undo()&&scene.toJson()==snapshot,"Undo terrain edit");require(scene.redo(),"Redo terrain edit");
        auto valid=scene.toJson(),broken=valid;auto objects=broken["objects"].toArray();auto first=objects[0].toObject();auto data=first["terrain"].toObject();data["resolution"]=30;first["terrain"]=data;objects[0]=first;broken["objects"]=objects;
        require(!scene.fromJson(broken,error)&&scene.toJson()==valid,"Invalid terrain cannot replace scene");
        require(scene.save(temp.filePath("Landscape.elscene"),error)&&other.load(temp.filePath("Landscape.elscene"),error)&&scene.toJson()==other.toJson(),"Save and load complete terrain");
        terrain=scene.getObjects()[0]->getComponent<TerrainComponent>();
        QImage texture(2,2,QImage::Format_RGB32);texture.fill(Qt::green);texture.save(temp.filePath("Grass.png"));
        terrain->layers[0].texture=temp.filePath("Grass.png");
        require(ProjectFile::savePrefab(temp.filePath("Landscape.elprefab"),scene,*scene.getObjects()[0],error),"Package terrain prefab layers");
        QFile::remove(temp.filePath("Grass.png"));Scene instance;
        require(ProjectFile::loadPrefab(temp.filePath("Landscape.elprefab"),instance,error),"Load packaged terrain without original textures");
        require(instance.getObjects()[0]->getComponent<TerrainComponent>()->resolution==33,"Restore prefab terrain");
    }
    catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
}
