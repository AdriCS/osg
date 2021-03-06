
#include <osgTerrain/Layer>
#include <osgTerrain/TerrainTile>
#include <osg/Texture1D>
#include <osg/Texture2D>
#include <osg/Program>
#include <osg/Uniform>
#include <osg/io_utils>

#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgDB/FileUtils>

#include "ShaderTerrain.h"

using namespace osgTerrain;


const osgTerrain::Locator* osgTerrain::computeMasterLocator(const osgTerrain::TerrainTile* tile)
{
    const osgTerrain::Layer* elevationLayer = tile->getElevationLayer();
    const osgTerrain::Layer* colorLayer = tile->getColorLayer(0);

    const Locator* elevationLocator = elevationLayer ? elevationLayer->getLocator() : 0;
    const Locator* colorLocator = colorLayer ? colorLayer->getLocator() : 0;

    const Locator* masterLocator = elevationLocator ? elevationLocator : colorLocator;
    if (!masterLocator)
    {
        OSG_NOTICE<<"Problem, no locator found in any of the terrain layers"<<std::endl;
        return 0;
    }

    return masterLocator;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  GeometryPool
//
bool GeometryPool::createKeyForTile(TerrainTile* tile, GeometryKey& key)
{
    const osgTerrain::Locator* masterLocator = computeMasterLocator(tile);
    if (masterLocator)
    {
        const osg::Matrixd& matrix = masterLocator->getTransform();
        osg::Vec3d bottom_left = osg::Vec3d(0.0,0.0,0.0) * matrix;
        osg::Vec3d bottom_right = osg::Vec3d(1.0,0.0,0.0) * matrix;
        osg::Vec3d top_left = osg::Vec3d(1.0,1.0,0.0) * matrix;
        key.sx = static_cast<float>((bottom_right-bottom_left).length());
        key.sy = static_cast<float>((top_left-bottom_left).length());

        if (masterLocator->getCoordinateSystemType()==osgTerrain::Locator::GEOCENTRIC)
        {
            // need to differentiate between tiles based of latitude, so use y position of bottom left corner.
            key.y = static_cast<float>(bottom_left.y());
        }
        else
        {
            // when the projection is linear there is no need to differentiate tiles according to their latitude
            key.y = 0.0;
        }

    }

    osgTerrain::HeightFieldLayer* layer = dynamic_cast<osgTerrain::HeightFieldLayer*>(tile->getElevationLayer());
    if (layer)
    {
        osg::HeightField* hf = layer->getHeightField();
        if (hf)
        {
            key.nx = hf->getNumColumns();
            key.ny = hf->getNumRows();
        }
    }
    return true;
}

static int numberGeometryCreated = 0;
static int numberSharedGeometry = 0;

osg::Geometry* GeometryPool::getOrCreateGeometry(osgTerrain::TerrainTile* tile)
{
    OpenThreads::ScopedLock<OpenThreads::Mutex>  lock(_geometryMapMutex);

    GeometryKey key;
    createKeyForTile(tile, key);

    GeometryMap::iterator itr = _geometryMap.find(key);
    if (itr != _geometryMap.end())
    {

        ++numberSharedGeometry;
//        OSG_NOTICE<<"Sharing geometry "<<itr->second.get()<<", number shared = "<<std::dec<<numberSharedGeometry<<", number created "<<numberGeometryCreated<<std::endl;
//        OSG_NOTICE<<"   GeometryKey "<<key.y<<", sx="<<key.sx<<", sy="<<key.sy<<", "<<key.nx<<", "<<key.ny<<std::endl;
        return itr->second.get();
    }

    osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
    _geometryMap[key] = geometry;

    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    geometry->setVertexArray(vertices.get());

    osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array;
    geometry->setNormalArray(normals.get(), osg::Array::BIND_PER_VERTEX);

    osg::ref_ptr<osg::Vec4Array> colours = new osg::Vec4Array;
    geometry->setColorArray(colours.get(), osg::Array::BIND_OVERALL);
    colours->push_back(osg::Vec4(1.0f, 1.0f, 1.0f, 1.0f));

    osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array;
    geometry->setTexCoordArray(0, texcoords.get(), osg::Array::BIND_PER_VERTEX);
    geometry->setTexCoordArray(1, texcoords.get(), osg::Array::BIND_PER_VERTEX);
    geometry->setTexCoordArray(2, texcoords.get(), osg::Array::BIND_PER_VERTEX);
    geometry->setTexCoordArray(3, texcoords.get(), osg::Array::BIND_PER_VERTEX);

    int nx = key.nx;
    int ny = key.nx;
    int numVertices = nx * ny;


    vertices->reserve(numVertices);
    normals->reserve(numVertices);
    texcoords->reserve(numVertices);

    double r_mult = 1.0/static_cast<double>(ny-1);
    double c_mult = 1.0/static_cast<double>(nx-1);

    typedef std::vector<osg::Vec2d> LocationCoords;
    LocationCoords locationCoords;
    locationCoords.reserve(numVertices);

    osg::Vec3d pos(0.0, 0.0, 0.0);
    osg::Vec3d normal(0.0, 0.0, 1.0);
    for(int r=0; r<ny; ++r)
    {
        pos.y () = static_cast<double>(r)*r_mult;
        for(int c=0; c<nx; ++c)
        {
            pos.x() = static_cast<double>(c)*c_mult;
            vertices->push_back(pos);
            normals->push_back(normal);
            texcoords->push_back(osg::Vec2(pos.x(), pos.y()));
            locationCoords.push_back(osg::Vec2d(pos.x(), pos.y()));
        }
    }

    bool smallTile = numVertices <= 16384;
    osg::ref_ptr<osg::DrawElements> elements = smallTile ?
        static_cast<osg::DrawElements*>(new osg::DrawElementsUShort(GL_QUADS)) :
        static_cast<osg::DrawElements*>(new osg::DrawElementsUInt(GL_QUADS));

    elements->reserveElements((nx-1) * (ny-1) * 4);
    geometry->addPrimitiveSet(elements.get());

    for(int r=0; r<ny-1; ++r)
    {
        for(int c=0; c<nx-1; ++c)
        {
            int i = c+r*nx;
            elements->addElement(i);
            elements->addElement(i+1);
            elements->addElement(i+nx+1);
            elements->addElement(i+nx);
        }
    }

    osg::Matrixd matrix;

    osg::Vec3d center(0.5, 0.5, 0.0);

    osg::Vec3d bottom_left(0.0,0.0,0.0);
    osg::Vec3d bottom_right(1.0,0.0,0.0);
    osg::Vec3d top_left(0.0,1.0,0.0);

    const osgTerrain::Locator* locator = computeMasterLocator(tile);
    if (locator)
    {
        matrix = locator->getTransform();

        center = center * matrix;
        bottom_left = bottom_left * matrix;
        bottom_right = bottom_right * matrix;
        top_left = top_left * matrix;

        // shift to center.x() to x=0 and carry all the corners with it.
        bottom_left.x() -= center.x();
        bottom_right.x() -= center.x();
        top_left.x() -= center.x();
        //center.x() = 0.0;

 //       OSG_NOTICE<<"   in lat/longs : bottom_left = "<<bottom_left<<std::endl;
 //       OSG_NOTICE<<"   in lat/longs : bottom_right = "<<bottom_right<<std::endl;
 //       OSG_NOTICE<<"   in lat/longs : top_left = "<<top_left<<std::endl;

        const osg::EllipsoidModel* em = locator->getEllipsoidModel();
        if (em && locator->getCoordinateSystemType()==osgTerrain::Locator::GEOCENTRIC)
        {
            osg::Matrixd localToWorldTransform;
            // note y axis maps to latitude, x axis to longitude
            em->computeLocalToWorldTransformFromLatLongHeight(center.y(), center.x(), center.z(), localToWorldTransform);
 //           OSG_NOTICE<<"We have a EllipsoidModel to take account of "<<localToWorldTransform<<std::endl;

            // note y axis maps to latitude, x axis to longitude
            em->convertLatLongHeightToXYZ(center.y(), center.x(), center.z(), center.x(), center.y(),center.z());
            em->convertLatLongHeightToXYZ(bottom_left.y(), bottom_left.x(), bottom_left.z(), bottom_left.x(), bottom_left.y(),bottom_left.z());
            em->convertLatLongHeightToXYZ(bottom_right.y(), bottom_right.x(), bottom_right.z(), bottom_right.x(), bottom_right.y(),bottom_right.z());
            em->convertLatLongHeightToXYZ(top_left.y(), top_left.x(), top_left.z(), top_left.x(), top_left.y(),top_left.z());

            osg::Matrixd worldToLocalTransform;
            worldToLocalTransform.invert(localToWorldTransform);

            center = center * worldToLocalTransform;
            bottom_left = bottom_left * worldToLocalTransform;
            bottom_right = bottom_right * worldToLocalTransform;
            top_left = top_left * worldToLocalTransform;


            for(int i=0; i<numVertices; ++i)
            {
                const osg::Vec2d& location = locationCoords[i];
                osg::Vec3d pos = osg::Vec3d(location.x(), location.y(), 0.0) * matrix;
                em->convertLatLongHeightToXYZ(pos.y(), pos.x(), 0.0, pos.x(), pos.y(),pos.z());

                osg::Vec3d normal(pos);
                normal = osg::Matrixd::transform3x3(localToWorldTransform, normal);
                normal.normalize();

                pos = pos * worldToLocalTransform;
                pos -= center;

                (*vertices)[i] = pos;
                (*normals)[i] = normal;
            }

        }
    }

    // double tileWidth = (bottom_right-bottom_left).length();
    // double skirtHeight = tileWidth*0.05;

 //   OSG_NOTICE<<"   in local coords : center = "<<center<<std::endl;
 //   OSG_NOTICE<<"   in local coords : bottom_left = "<<bottom_left<<std::endl;
 //   OSG_NOTICE<<"   in local coords : bottom_right = "<<bottom_right<<std::endl;
 //   OSG_NOTICE<<"   in local coords : top_left = "<<top_left<<std::endl;
 //   OSG_NOTICE<<"   skirtHeight = "<<skirtHeight<<std::endl;


    //osgDB::writeNodeFile(*geometry, "geometry.osgt");


    ++ numberGeometryCreated;
//    OSG_NOTICE<<"Creating new geometry "<<geometry.get()<<std::endl;

    return geometry.release();
}

osg::MatrixTransform* GeometryPool::getTileSubgraph(osgTerrain::TerrainTile* tile)
{
    // create or reuse Geometry
    osg::ref_ptr<osg::Geometry> geometry = getOrCreateGeometry(tile);


    osg::ref_ptr<HeightFieldDrawable> hfDrawable = new HeightFieldDrawable();

    osgTerrain::HeightFieldLayer* hfl = dynamic_cast<osgTerrain::HeightFieldLayer*>(tile->getElevationLayer());
    osg::HeightField* hf = hfl ? hfl->getHeightField() : 0;
    hfDrawable->setHeightField(hf);
    hfDrawable->setGeometry(geometry.get());


    // create a transform to place the geometry in the appropriate place
    osg::ref_ptr<osg::MatrixTransform> transform = new osg::MatrixTransform;

//    transform->addChild(geometry.get());
    transform->addChild(hfDrawable.get());

    const osgTerrain::Locator* locator = computeMasterLocator(tile);
    if (locator)
    {
        osg::Matrixd matrix = locator->getTransform();

        osg::Vec3d center = osg::Vec3d(0.5, 0.5, 0.0) * matrix;

        // shift to center.x() to x=0 and carry all the corners with it.
        const osg::EllipsoidModel* em = locator->getEllipsoidModel();
        if (em && locator->getCoordinateSystemType()==osgTerrain::Locator::GEOCENTRIC)
        {
            osg::Matrixd localToWorldTransform;
            // note y axis maps to latitude, x axis to longitude
            em->computeLocalToWorldTransformFromLatLongHeight(center.y(), center.x(), center.z(), localToWorldTransform);
 //           OSG_NOTICE<<"We have a EllipsoidModel to take account of "<<localToWorldTransform<<std::endl;

            transform->setMatrix(localToWorldTransform);

            //osgDB::writeNodeFile(*transform, "subgraph.osgt");
        }
        else
        {
            transform->setMatrix(locator->getTransform());
        }
    }

    osg::Vec3Array* vertices = dynamic_cast<osg::Vec3Array*>(geometry->getVertexArray());
    osg::Vec3Array* normals = dynamic_cast<osg::Vec3Array*>(geometry->getNormalArray());
    if (hf && vertices && normals && (vertices->size()==normals->size()))
    {
        unsigned int nr = hf->getNumRows();
        unsigned int nc = hf->getNumColumns();

        osg::BoundingBox bb;
        osg::FloatArray* heights = hf->getFloatArray();

        for(unsigned int r=0; r<nr; ++r)
        {
            for(unsigned int c=0; c<nc; ++c)
            {
                unsigned int i = r*nc+c;
                float h = (*heights)[i];
                const osg::Vec3& v = (*vertices)[i];
                const osg::Vec3& n = (*normals)[i];

                const osg::Vec3 vt(v+n*h);
                bb.expandBy(vt);
            }
        }
        hfDrawable->setInitialBound(bb);
        // OSG_NOTICE<<"Assigning initial bound ("<<bb.xMin()<<", "<<bb.xMax()<<") ,  ("<<bb.yMin()<<", "<<bb.yMax()<<") ("<<bb.zMin()<<", "<<bb.zMax()<<")"<< std::endl;
        // bb = hfDrawable->getBoundingBox();
        //OSG_NOTICE<<"         getBoundingBox ("<<bb.xMin()<<", "<<bb.xMax()<<") ,  ("<<bb.yMin()<<", "<<bb.yMax()<<") ("<<bb.zMin()<<", "<<bb.zMax()<<")"<< std::endl;
    }



    osg::ref_ptr<osg::StateSet> stateset = transform->getOrCreateStateSet();

    // apply colour layers
    applyLayers(tile, stateset.get());

    return transform.release();
}

osg::Program* GeometryPool::getOrCreateProgram(LayerTypes& layerTypes)
{
#if 0
    OSG_NOTICE<<"getOrCreateProgram(";
    for(LayerTypes::iterator itr = layerTypes.begin();
        itr != layerTypes.end();
        ++itr)
    {
        if (itr!= layerTypes.begin()) OSG_NOTICE<<", ";
        switch(*itr)
        {
            case(HEIGHTFIELD_LAYER): OSG_NOTICE<<"HeightField"; break;
            case(COLOR_LAYER): OSG_NOTICE<<"Colour"; break;
            case(CONTOUR_LAYER): OSG_NOTICE<<"Contour"; break;
        }
    }
#endif

    ProgramMap::iterator itr = _programMap.find(layerTypes);
    if (itr!=_programMap.end())
    {
        // OSG_NOTICE<<") returning exisitng Program "<<itr->second.get()<<std::endl;
        return itr->second.get();
    }

    osg::ref_ptr<osg::Program> program = new osg::Program;
    _programMap[layerTypes] = program;

    OSG_NOTICE<<") creating new Program "<<program.get()<<std::endl;

    osg::ref_ptr<osg::Shader> vertex_shader = osgDB::readShaderFile("terrain.vert");
    program->addShader(vertex_shader.get());

    osg::ref_ptr<osg::Shader> fragment_shader = osgDB::readShaderFile("terrain.frag");
    program->addShader(fragment_shader.get());

    return program.get();
}

void GeometryPool::applyLayers(osgTerrain::TerrainTile* tile, osg::StateSet* stateset)
{
    typedef std::map<osgTerrain::Layer*, osg::Texture*> LayerToTextureMap;
    LayerToTextureMap layerToTextureMap;

 //   OSG_NOTICE<<"tile->getNumColorLayers() = "<<tile->getNumColorLayers()<<std::endl;

    LayerTypes layerTypes;

    osgTerrain::HeightFieldLayer* hfl = dynamic_cast<osgTerrain::HeightFieldLayer*>(tile->getElevationLayer());
    if (hfl)
    {
        osg::Texture2D* texture2D = dynamic_cast<osg::Texture2D*>(layerToTextureMap[hfl]);
        if (!texture2D)
        {
            texture2D = new osg::Texture2D;

            osg::ref_ptr<osg::Image> image = new osg::Image;

            const void* dataPtr = hfl->getHeightField()->getFloatArray()->getDataPointer();

            image->setImage(hfl->getNumRows(), hfl->getNumColumns(), 1,
                      GL_LUMINANCE32F_ARB,
                      GL_LUMINANCE, GL_FLOAT,
                      reinterpret_cast<unsigned char*>(const_cast<void*>(dataPtr)),
                      osg::Image::NO_DELETE);

            texture2D->setImage(image.get());
            texture2D->setFilter(osg::Texture2D::MIN_FILTER, osg::Texture2D::NEAREST);
            texture2D->setFilter(osg::Texture2D::MAG_FILTER, osg::Texture2D::NEAREST);
            texture2D->setResizeNonPowerOfTwoHint(false);

            layerToTextureMap[hfl] = texture2D;
        }

        int textureUnit = layerTypes.size();
        stateset->setTextureAttributeAndModes(textureUnit, texture2D, osg::StateAttribute::ON);
        stateset->addUniform(new osg::Uniform("terrainTexture",textureUnit));

        layerTypes.push_back(HEIGHTFIELD_LAYER);
    }
#if 1
    for(unsigned int layerNum=0; layerNum<tile->getNumColorLayers(); ++layerNum)
    {
        osgTerrain::Layer* colorLayer = tile->getColorLayer(layerNum);
        if (!colorLayer) continue;

        osgTerrain::SwitchLayer* switchLayer = dynamic_cast<osgTerrain::SwitchLayer*>(colorLayer);
        if (switchLayer)
        {
            if (switchLayer->getActiveLayer()<0 ||
                static_cast<unsigned int>(switchLayer->getActiveLayer())>=switchLayer->getNumLayers())
            {
                continue;
            }

            colorLayer = switchLayer->getLayer(switchLayer->getActiveLayer());
            if (!colorLayer) continue;
        }

        osg::Image* image = colorLayer->getImage();
        if (!image) continue;

        osgTerrain::ImageLayer* imageLayer = dynamic_cast<osgTerrain::ImageLayer*>(colorLayer);
        osgTerrain::ContourLayer* contourLayer = dynamic_cast<osgTerrain::ContourLayer*>(colorLayer);
        if (imageLayer)
        {
            osg::Texture2D* texture2D = dynamic_cast<osg::Texture2D*>(layerToTextureMap[colorLayer]);
            if (!texture2D)
            {
                texture2D = new osg::Texture2D;
                texture2D->setImage(image);
                texture2D->setMaxAnisotropy(16.0f);
                texture2D->setResizeNonPowerOfTwoHint(false);

                texture2D->setFilter(osg::Texture::MIN_FILTER, colorLayer->getMinFilter());
                texture2D->setFilter(osg::Texture::MAG_FILTER, colorLayer->getMagFilter());

                texture2D->setWrap(osg::Texture::WRAP_S,osg::Texture::CLAMP_TO_EDGE);
                texture2D->setWrap(osg::Texture::WRAP_T,osg::Texture::CLAMP_TO_EDGE);

                bool mipMapping = !(texture2D->getFilter(osg::Texture::MIN_FILTER)==osg::Texture::LINEAR || texture2D->getFilter(osg::Texture::MIN_FILTER)==osg::Texture::NEAREST);
                bool s_NotPowerOfTwo = image->s()==0 || (image->s() & (image->s() - 1));
                bool t_NotPowerOfTwo = image->t()==0 || (image->t() & (image->t() - 1));

                if (mipMapping && (s_NotPowerOfTwo || t_NotPowerOfTwo))
                {
                    OSG_INFO<<"Disabling mipmapping for non power of two tile size("<<image->s()<<", "<<image->t()<<")"<<std::endl;
                    texture2D->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
                }


                layerToTextureMap[colorLayer] = texture2D;

                // OSG_NOTICE<<"Creating new ImageLayer texture "<<layerNum<<" image->s()="<<image->s()<<"  image->t()="<<image->t()<<std::endl;

            }
            else
            {
                // OSG_NOTICE<<"Reusing ImageLayer texture "<<layerNum<<std::endl;
            }

            int textureUnit = layerTypes.size();
            stateset->setTextureAttributeAndModes(textureUnit, texture2D, osg::StateAttribute::ON);

            std::stringstream str;
            str<<"colorTexture"<<textureUnit;
            stateset->addUniform(new osg::Uniform(str.str().c_str(),textureUnit));

            layerTypes.push_back(COLOR_LAYER);

        }
        else if (contourLayer)
        {
            osg::Texture1D* texture1D = dynamic_cast<osg::Texture1D*>(layerToTextureMap[colorLayer]);
            if (!texture1D)
            {
                texture1D = new osg::Texture1D;
                texture1D->setImage(image);
                texture1D->setResizeNonPowerOfTwoHint(false);
                texture1D->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
                texture1D->setFilter(osg::Texture::MAG_FILTER, colorLayer->getMagFilter());

                layerToTextureMap[colorLayer] = texture1D;
            }

            int textureUnit = layerTypes.size();
            stateset->setTextureAttributeAndModes(textureUnit, texture1D, osg::StateAttribute::ON);

            std::stringstream str;
            str<<"contourTexture"<<textureUnit;
            stateset->addUniform(new osg::Uniform(str.str().c_str(),textureUnit));

            layerTypes.push_back(CONTOUR_LAYER);
        }
    }
#endif

    osg::Program* program = getOrCreateProgram(layerTypes);
    if (program)
    {
        stateset->setAttribute(program);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  HeightFieldDrawable
//
HeightFieldDrawable::HeightFieldDrawable()
{
    setSupportsDisplayList(false);
}

HeightFieldDrawable::HeightFieldDrawable(const HeightFieldDrawable& rhs,const osg::CopyOp& copyop):
    osg::Drawable(rhs, copyop),
    _heightField(rhs._heightField),
    _geometry(rhs._geometry)
{
    setSupportsDisplayList(false);
}

void HeightFieldDrawable::drawImplementation(osg::RenderInfo& renderInfo) const
{
    if (_geometry.valid()) _geometry->draw(renderInfo);
}

void HeightFieldDrawable::compileGLObjects(osg::RenderInfo& renderInfo) const
{
    if (_geometry.valid()) _geometry->compileGLObjects(renderInfo);
}

void HeightFieldDrawable::resizeGLObjectBuffers(unsigned int maxSize)
{
    if (_geometry.valid()) _geometry->resizeGLObjectBuffers(maxSize);
}

void HeightFieldDrawable::releaseGLObjects(osg::State* state) const
{
    if (_geometry.valid()) _geometry->releaseGLObjects(state);
}

void HeightFieldDrawable::accept(osg::Drawable::AttributeFunctor& af)
{
    if (_geometry) _geometry->accept(af);
}

void HeightFieldDrawable::accept(osg::Drawable::ConstAttributeFunctor& caf) const
{
    if (_geometry) _geometry->accept(caf);
}

void HeightFieldDrawable::accept(osg::PrimitiveFunctor& pf) const
{
    if (_geometry) _geometry->accept(pf);
}

void HeightFieldDrawable::accept(osg::PrimitiveIndexFunctor& pif) const
{
    if (_geometry) _geometry->accept(pif);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  ShaderTerrain
//
ShaderTerrain::ShaderTerrain()
{
    // OSG_NOTICE<<"ShaderTerrain::ShaderTerrain()"<<std::endl;
    _geometryPool = new GeometryPool;
}

ShaderTerrain::ShaderTerrain(const ShaderTerrain& st,const osg::CopyOp& copyop):
    osgTerrain::TerrainTechnique(st, copyop),
    _geometryPool(st._geometryPool)
{
    // OSG_NOTICE<<"ShaderTerrain::ShaderTerrain(ShaderTerrain&, CopyOp&) "<<_geometryPool.get()<<std::endl;
}

void ShaderTerrain::init(int dirtyMask, bool assumeMultiThreaded)
{
    if (!_terrainTile) return;

    //OSG_NOTICE<<"ShaderTerrain::init("<<dirtyMask<<", "<<assumeMultiThreaded<<")"<<std::endl;

    _transform = _geometryPool->getTileSubgraph(_terrainTile);

    // set tile as no longer dirty.
    _terrainTile->setDirtyMask(0);
}

void ShaderTerrain::update(osgUtil::UpdateVisitor* uv)
{
    if (_terrainTile) _terrainTile->osg::Group::traverse(*uv);

    if (_transform.valid()) _transform->accept(*uv);
}


void ShaderTerrain::cull(osgUtil::CullVisitor* cv)
{
    if (_transform.valid()) _transform->accept(*cv);
}


void ShaderTerrain::traverse(osg::NodeVisitor& nv)
{
    if (!_terrainTile) return;

    // if app traversal update the frame count.
    if (nv.getVisitorType()==osg::NodeVisitor::UPDATE_VISITOR)
    {
        if (_terrainTile->getDirty()) _terrainTile->init(_terrainTile->getDirtyMask(), false);

        osgUtil::UpdateVisitor* uv = dynamic_cast<osgUtil::UpdateVisitor*>(&nv);
        if (uv)
        {
            update(uv);
            return;
        }
    }
    else if (nv.getVisitorType()==osg::NodeVisitor::CULL_VISITOR)
    {
        osgUtil::CullVisitor* cv = dynamic_cast<osgUtil::CullVisitor*>(&nv);
        if (cv)
        {
            cull(cv);
            return;
        }
    }


    if (_terrainTile->getDirty())
    {
        // OSG_INFO<<"******* Doing init ***********"<<std::endl;
        _terrainTile->init(_terrainTile->getDirtyMask(), false);
    }

    if (_transform.valid())
    {
        _transform->accept(nv);
    }
}


void ShaderTerrain::cleanSceneGraph()
{
}

void ShaderTerrain::releaseGLObjects(osg::State* state) const
{
    _transform->releaseGLObjects(state);
}
