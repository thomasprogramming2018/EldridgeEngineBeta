#include "TerrainComponent.h"
#include "../../scene/SceneObject.h"
#include "../MeshComponent/MeshComponent.h"
#include "../TransformComponent/TransformComponent.h"
#include <QBuffer>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
bool validResolution(int value) { int cells=value-1; return value>=33 && value<=513 && (cells&(cells-1))==0; }
float noise(int x, int z, int seed)
{
    quint32 value=quint32(x)*374761393u+quint32(z)*668265263u+quint32(seed)*69069u;
    value=(value^(value>>13))*1274126177u;
    return float(value^(value>>16))/float(std::numeric_limits<quint32>::max())*2.f-1.f;
}
float smoothNoise(float x,float z,int seed)
{
    int ix=int(std::floor(x)), iz=int(std::floor(z));
    float u=x-ix,v=z-iz; u=u*u*(3-2*u); v=v*v*(3-2*v);
    float a=noise(ix,iz,seed)*(1-u)+noise(ix+1,iz,seed)*u;
    float b=noise(ix,iz+1,seed)*(1-u)+noise(ix+1,iz+1,seed)*u;
    return a*(1-v)+b*v;
}
void normalizeWeights(uchar* pixel)
{
    int sum=pixel[0]+pixel[1]+pixel[2]+pixel[3];
    if(!sum) { pixel[0]=255; return; }
    int used=0;
    for(int i=1;i<4;++i) { pixel[i]=int(pixel[i])*255/sum; used+=pixel[i]; }
    pixel[0]=255-used;
}
}
TerrainComponent::TerrainComponent(SceneObject& owner) : Component(owner)
{
    layers = {{{QString(),{0.18f,0.36f,0.09f}}, {QString(),{0.34f,0.22f,0.12f}},
               {QString(),{0.4f,0.42f,0.45f}}, {QString(),{0.85f,0.9f,0.95f}}}};
}
void TerrainComponent::resize(int samples,float worldSize)
{
    if(!validResolution(samples) || !std::isfinite(worldSize) || worldSize<1 || worldSize>10000)
        throw std::runtime_error("Terrain needs 33, 65, 129, 257 or 513 samples and a size between 1 and 10000.");
    resolution=samples; size=worldSize;
    heights.assign(size_t(samples)*samples,heightOffset);
    weights=QImage(samples,samples,QImage::Format_RGBA8888);
    weights.fill(QColor(255,0,0,0));
    rebuild();
}
void TerrainComponent::generate(int seed,float amplitude,float frequency)
{
    amplitude=std::clamp(amplitude,0.f,1000.f); frequency=std::clamp(frequency,0.1f,100.f);
    if(heights.size()!=size_t(resolution)*resolution) resize(resolution,size);
    for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x)
    {
        float h=0,weight=1,total=0,f=frequency;
        for(int octave=0;octave<5;++octave)
        { h+=smoothNoise(float(x)/(resolution-1)*f,float(z)/(resolution-1)*f,seed+octave*31)*weight; total+=weight; weight*=0.5f; f*=2; }
        heights[z*resolution+x]=heightOffset+h/total*amplitude;
    }
    rebuild();
}
void TerrainComponent::erode(int iterations,float talus)
{
    if(heights.empty()) return;
    iterations=std::clamp(iterations,1,100); talus=std::max(talus,0.001f);
    std::vector<float> delta(heights.size());
    for(int iteration=0;iteration<iterations;++iteration)
    {
        std::fill(delta.begin(),delta.end(),0.f);
        for(int z=1;z<resolution-1;++z) for(int x=1;x<resolution-1;++x)
        {
            int index=z*resolution+x,lowest=index;
            for(int neighbor:{index-1,index+1,index-resolution,index+resolution})
                if(heights[neighbor]<heights[lowest]) lowest=neighbor;
            float difference=heights[index]-heights[lowest];
            if(difference>talus) { float amount=(difference-talus)*0.125f; delta[index]-=amount; delta[lowest]+=amount; }
        }
        for(size_t i=0;i<heights.size();++i) heights[i]+=delta[i];
    }
    rebuild();
}
void TerrainComponent::brush(float x,float z,float radius,float strength,Brush mode,int layer,float flatHeight)
{
    if(heights.empty() || radius<=0 || !std::isfinite(radius) || !std::isfinite(strength)) return;
    layer=std::clamp(layer,0,3);
    auto previous=heights;
    float spacing=size/(resolution-1);
    int minX=std::max(0,int(std::floor((x-radius+size*0.5f)/spacing)));
    int maxX=std::min(resolution-1,int(std::ceil((x+radius+size*0.5f)/spacing)));
    int minZ=std::max(0,int(std::floor((z-radius+size*0.5f)/spacing)));
    int maxZ=std::min(resolution-1,int(std::ceil((z+radius+size*0.5f)/spacing)));
    for(int iz=minZ;iz<=maxZ;++iz) for(int ix=minX;ix<=maxX;++ix)
    {
        float dx=ix*spacing-size*0.5f-x,dz=iz*spacing-size*0.5f-z;
        float distance=std::sqrt(dx*dx+dz*dz)/radius;
        if(distance>1) continue;
        float falloff=(1-distance)*(1-distance)*(1+2*distance);
        int index=iz*resolution+ix;
        if(mode==Brush::Paint)
        {
            uchar* pixel=weights.scanLine(iz)+ix*4;
            float blend=std::clamp(std::abs(strength)*falloff,0.f,1.f);
            for(int i=0;i<4;++i) pixel[i]=std::clamp(int(pixel[i]*(1-blend)+(i==layer?255:0)*blend),0,255);
            normalizeWeights(pixel);
        }
        else if(mode==Brush::Smooth || mode==Brush::Flatten)
        {
            float target=flatHeight;
            if(mode==Brush::Smooth)
            {
                target=0; int count=0;
                for(int nz=std::max(0,iz-1);nz<=std::min(resolution-1,iz+1);++nz)
                    for(int nx=std::max(0,ix-1);nx<=std::min(resolution-1,ix+1);++nx)
                    { target+=previous[nz*resolution+nx]; ++count; }
                target/=count;
            }
            heights[index]+=(target-previous[index])*std::clamp(std::abs(strength)*falloff,0.f,1.f);
        }
        else heights[index]+=std::abs(strength)*falloff*(mode==Brush::Lower?-1.f:1.f);
        heights[index]=std::clamp(heights[index],-10000.f,10000.f);
    }
    rebuild();
}
float TerrainComponent::heightAt(float x,float z) const
{
    if(heights.empty()) return heightOffset;
    float gx=std::clamp((x/size+0.5f)*(resolution-1),0.f,float(resolution-1));
    float gz=std::clamp((z/size+0.5f)*(resolution-1),0.f,float(resolution-1));
    int ix=std::min(int(gx),resolution-2),iz=std::min(int(gz),resolution-2);
    float u=gx-ix,v=gz-iz;
    float a=heights[iz*resolution+ix]*(1-u)+heights[iz*resolution+ix+1]*u;
    float b=heights[(iz+1)*resolution+ix]*(1-u)+heights[(iz+1)*resolution+ix+1]*u;
    return a*(1-v)+b*v;
}
QVector3D TerrainComponent::normalAt(int x,int z) const
{
    int left=std::max(0,x-1),right=std::min(resolution-1,x+1),down=std::max(0,z-1),up=std::min(resolution-1,z+1);
    float spacing=size/(resolution-1);
    float dx=(heights[z*resolution+right]-heights[z*resolution+left])/((right-left)*spacing);
    float dz=(heights[up*resolution+x]-heights[down*resolution+x])/((up-down)*spacing);
    return QVector3D(-dx,1,-dz).normalized();
}
std::unique_ptr<Mesh> TerrainComponent::makeMesh(int ox,int oz,int cells,int step,bool skirts) const
{
    auto mesh=std::make_unique<Mesh>();
    int side=cells/step+1;
    for(int z=0;z<side;++z) for(int x=0;x<side;++x)
    {
        int sx=ox+x*step,sz=oz+z*step;
        auto normal=normalAt(sx,sz);
        mesh->vertices.insert(mesh->vertices.end(),{float(sx)/(resolution-1)*size-size*0.5f,heights[sz*resolution+sx],float(sz)/(resolution-1)*size-size*0.5f,
            normal.x(),normal.y(),normal.z(),float(sx)/(resolution-1),float(sz)/(resolution-1)});
    }
    for(int z=0;z<side-1;++z) for(int x=0;x<side-1;++x)
    {
        unsigned int a=z*side+x,b=a+1,c=a+side,d=c+1;
        mesh->indices.insert(mesh->indices.end(),{a,c,b,b,c,d});
    }
    if(skirts)
    {
        std::vector<unsigned int> border;
        for(int x=0;x<side;++x) border.push_back(x);
        for(int z=1;z<side;++z) border.push_back(z*side+side-1);
        for(int x=side-2;x>=0;--x) border.push_back((side-1)*side+x);
        for(int z=side-2;z>0;--z) border.push_back(z*side);
        float lowest=10000,highest=-10000;
        for(int z=oz;z<=oz+cells;++z) for(int x=ox;x<=ox+cells;++x)
        {lowest=std::min(lowest,heights[z*resolution+x]);highest=std::max(highest,heights[z*resolution+x]);}
        float skirtDepth=std::max(2.f,highest-lowest+size/resolution*4);
        unsigned int start=mesh->vertices.size()/8;
        for(unsigned int index:border)
        {
            std::array<float,8> vertex;
            std::copy_n(mesh->vertices.data()+index*8,8,vertex.begin());
            vertex[1]-=skirtDepth;
            mesh->vertices.insert(mesh->vertices.end(),vertex.begin(),vertex.end());
        }
        for(unsigned int i=0;i<border.size();++i)
        {
            unsigned int j=(i+1)%border.size();
            int ax=ox+int(border[i]%side)*step,az=oz+int(border[i]/side)*step;
            int bx=ox+int(border[j]%side)*step,bz=oz+int(border[j]/side)*step;
            // Skirts hide internal LOD seams, not the terrain's outer silhouette.
            if((ax==0&&bx==0)||(ax==resolution-1&&bx==resolution-1)||
               (az==0&&bz==0)||(az==resolution-1&&bz==resolution-1)) continue;
            mesh->indices.insert(mesh->indices.end(),{border[i],start+i,border[j],border[j],start+i,start+j});
        }
    }
    return mesh;
}
void TerrainComponent::rebuild()
{
    if(heights.size()!=size_t(resolution)*resolution) return;
    auto* component=owner_.getComponent<MeshComponent>();
    if(!component) component=&owner_.addComponent<MeshComponent>();
    component->restoreMesh(makeMesh(0,0,resolution-1,1,false),MeshComponent::Source::Terrain,"");
    chunks.clear();
    for(int z=0;z<resolution-1;z+=32) for(int x=0;x<resolution-1;x+=32)
    {
        TerrainChunk chunk;
        float low=10000,high=-10000;
        for(int zSample=z;zSample<=z+32;++zSample) for(int xSample=x;xSample<=x+32;++xSample)
        {low=std::min(low,heights[zSample*resolution+xSample]);high=std::max(high,heights[zSample*resolution+xSample]);}
        chunk.center={float(x+16)/(resolution-1)*size-size/2,(low+high)*0.5f,float(z+16)/(resolution-1)*size-size/2};
        chunk.radius=QVector3D(size/(resolution-1)*16,(high-low)*0.5f,size/(resolution-1)*16).length();
        for(int level=0;level<4;++level) chunk.levels[level]=makeMesh(x,z,32,1<<level,true);
        chunks.push_back(std::move(chunk));
    }
    ++revision;
}
bool TerrainComponent::raycast(const QVector3D& origin,const QVector3D& direction,QVector3D& worldHit) const
{
    if(heights.empty() || direction.lengthSquared()<1e-12f) return false;
    auto* transform=owner_.getComponent<TransformComponent>();
    auto model=transform?transform->getModelMatrix():QMatrix4x4();
    bool invertible; auto inverse=model.inverted(&invertible); if(!invertible) return false;
    auto eye=inverse.map(origin),ray=inverse.mapVector(direction);
    auto range=std::minmax_element(heights.begin(),heights.end());
    QVector3D minimum(-size/2,*range.first-0.01f,-size/2),maximum(size/2,*range.second+0.01f,size/2);
    float near=0,far=100000;
    for(int axis=0;axis<3;++axis)
    {
        if(std::abs(ray[axis])<1e-8f) { if(eye[axis]<minimum[axis]||eye[axis]>maximum[axis]) return false; continue; }
        float a=(minimum[axis]-eye[axis])/ray[axis],b=(maximum[axis]-eye[axis])/ray[axis];
        if(a>b) std::swap(a,b); near=std::max(near,a); far=std::min(far,b);
        if(near>far) return false;
    }
    float previous=near;
    auto difference=[&](float t) { auto p=eye+ray*t; return p.y()-heightAt(p.x(),p.z()); };
    float prior=difference(near);
    for(int i=1;i<=2048;++i)
    {
        float current=near+(far-near)*i/2048.f,value=difference(current);
        if(prior*value<=0)
        {
            float a=previous,b=current;
            for(int j=0;j<16;++j) { float middle=(a+b)*0.5f; if(difference(a)*difference(middle)<=0) b=middle; else a=middle; }
            worldHit=model.map(eye+ray*((a+b)*0.5f)); return true;
        }
        previous=current; prior=value;
    }
    return false;
}
bool TerrainComponent::importHeightmap(const QString& path,QString& error)
{
    QImage image(path);
    if(image.isNull()) { error="Cannot read heightmap image"; return false; }
    image=image.convertToFormat(QImage::Format_Grayscale16).scaled(resolution,resolution,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    heights.resize(size_t(resolution)*resolution);
    for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x)
        heights[z*resolution+x]=heightOffset+reinterpret_cast<const quint16*>(image.constScanLine(z))[x]/65535.f*heightRange;
    if(weights.size()!=QSize(resolution,resolution)) { weights=QImage(resolution,resolution,QImage::Format_RGBA8888); weights.fill(QColor(255,0,0,0)); }
    rebuild(); return true;
}
bool TerrainComponent::exportHeightmap(const QString& path,QString& error) const
{
    auto bounds=std::minmax_element(heights.begin(),heights.end());
    if(bounds.first==heights.end() || *bounds.first<heightOffset-0.001f || *bounds.second>heightOffset+heightRange+0.001f)
    { error="Height offset and range must cover the terrain heights before exporting."; return false; }
    QImage image(resolution,resolution,QImage::Format_Grayscale16);
    for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x)
        reinterpret_cast<quint16*>(image.scanLine(z))[x]=quint16(std::clamp((heights[z*resolution+x]-heightOffset)/std::max(heightRange,0.001f),0.f,1.f)*65535);
    if(!image.save(path,"PNG")) { error="Cannot save heightmap"; return false; } return true;
}
bool TerrainComponent::importWeights(const QString& path,QString& error)
{
    QImage image(path); if(image.isNull()) { error="Cannot read weight map"; return false; }
    weights=image.scaled(resolution,resolution).convertToFormat(QImage::Format_RGBA8888);
    for(int z=0;z<resolution;++z) for(int x=0;x<resolution;++x) normalizeWeights(weights.scanLine(z)+x*4);
    ++revision; return true;
}
bool TerrainComponent::exportWeights(const QString& path,QString& error) const
{
    if(!weights.save(path,"PNG")) { error="Cannot save weight map"; return false; } return true;
}
QJsonObject TerrainComponent::toJson() const
{
    QJsonArray samples,materials;
    for(float h:heights) samples.append(h);
    for(const auto& layer:layers) materials.append(QJsonObject{{"texture",layer.texture},{"color",QJsonArray{layer.color.x(),layer.color.y(),layer.color.z()}}});
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); weights.save(&buffer,"PNG");
    return {{"resolution",resolution},{"size",size},{"heightRange",heightRange},{"heightOffset",heightOffset},
            {"tiling",tiling},{"heights",samples},{"weights",QString::fromLatin1(bytes.toBase64())},{"layers",materials}};
}
bool TerrainComponent::fromJson(const QJsonObject& data,QString& error)
{
    int count=data["resolution"].toInt();
    auto finite=[&](const QString& key,float minimum,float maximum)
    { return data[key].isDouble() && std::isfinite(data[key].toDouble()) && data[key].toDouble()>=minimum && data[key].toDouble()<=maximum; };
    auto samples=data["heights"].toArray(),materials=data["layers"].toArray();
    if(!validResolution(count)||!finite("size",1,10000)||!finite("heightRange",0.001f,10000)||
       !finite("heightOffset",-10000,10000)||!finite("tiling",0.1f,1000)||samples.size()!=count*count||materials.size()!=4)
    { error="Invalid terrain settings"; return false; }
    std::vector<float> loaded;
    for(const auto& value:samples)
    {
        if(!value.isDouble()||!std::isfinite(value.toDouble())||std::abs(value.toDouble())>10000)
        { error="Invalid terrain height"; return false; } loaded.push_back(value.toDouble());
    }
    QImage map; map.loadFromData(QByteArray::fromBase64(data["weights"].toString().toLatin1()),"PNG");
    if(map.size()!=QSize(count,count)) { error="Invalid terrain weight map"; return false; }
    std::array<TerrainLayer,4> loadedLayers;
    for(int i=0;i<4;++i)
    {
        auto layer=materials[i].toObject(); auto color=layer["color"].toArray();
        if(!layer["texture"].isString()||color.size()!=3) { error="Invalid terrain layer"; return false; }
        for(int j=0;j<3;++j) if(!color[j].isDouble()||color[j].toDouble()<0||color[j].toDouble()>1)
        { error="Invalid terrain color"; return false; }
        loadedLayers[i]={layer["texture"].toString(),QVector3D(color[0].toDouble(),color[1].toDouble(),color[2].toDouble())};
    }
    resolution=count; size=data["size"].toDouble(); heightRange=data["heightRange"].toDouble(); heightOffset=data["heightOffset"].toDouble();
    tiling=data["tiling"].toDouble(); heights=std::move(loaded); layers=loadedLayers; weights=map.convertToFormat(QImage::Format_RGBA8888);
    rebuild(); return true;
}

QImage TerrainComponent::bakeAlbedo(int side) const
{
    side=std::clamp(side,32,2048);
    std::array<QImage,4> images;
    for(int i=0;i<4;++i) if(!layers[i].texture.isEmpty()) images[i].load(layers[i].texture);
    QImage result(side,side,QImage::Format_RGB32);
    for(int z=0;z<side;++z) for(int x=0;x<side;++x)
    {
        float u=float(x)/(side-1),v=float(z)/(side-1);
        auto pixel=weights.pixelColor(std::min(int(u*resolution),resolution-1),std::min(int(v*resolution),resolution-1));
        float blend[]={float(pixel.redF()),float(pixel.greenF()),float(pixel.blueF()),float(pixel.alphaF())};
        QVector3D color;
        for(int layer=0;layer<4;++layer)
        {
            auto tint=layers[layer].color;
            if(!images[layer].isNull())
            {
                const auto& image=images[layer];
                float tu=u*tiling-std::floor(u*tiling),tv=v*tiling-std::floor(v*tiling);
                auto texel=image.pixelColor(std::min(int(tu*image.width()),image.width()-1),std::min(int((1-tv)*image.height()),image.height()-1));
                tint={tint.x()*std::pow(float(texel.redF()),2.2f),tint.y()*std::pow(float(texel.greenF()),2.2f),tint.z()*std::pow(float(texel.blueF()),2.2f)};
            }
            color+=tint*blend[layer];
        }
        // Store the baked color as sRGB, like ordinary albedo images.
        for(int axis=0;axis<3;++axis) color[axis]=std::pow(std::clamp(color[axis],0.f,1.f),1.f/2.2f);
        result.setPixelColor(x,side-1-z,QColor::fromRgbF(color.x(),color.y(),color.z()));
    }
    return result;
}
